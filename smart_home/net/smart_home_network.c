/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 网络状态机。
 *
 * ★ 本文件是阶段 6 的核心：联网统一走 port/sca_wifi.h 的四态机 ★
 *
 * 实现路径（RT-Thread）：
 *   sca_wifi_port_init()      → rt_wlan_init()
 *   sca_wifi_port_connect()   → rt_wlan_connect() + 等 DHCP
 *   sca_wifi_port_check_dns() → getaddrinfo 探测
 *
 * 四态：init → ip → dns → online；每一步只调用 port 层，
 * 因此 net 模块本身零平台依赖。
 */

#include "port/sca_config.h"
#include "port/sca_os.h"
#include "smart_home/net/smart_home_network.h"
#include "board/ports/sca_wifi_port.h"
#include "smart_home/config/smart_home_secrets.h"
#include "port/sca_http.h"
#ifdef RT_USING_SNTP
#include "ntp.h"
#endif

#include <agent/error.h>

#include <stdio.h>
#include <string.h>

#define DBG_TAG "net"

/* ── 状态初始化 ────────────────────────────────────────── */

void smart_home_network_status_init(FAR smart_home_network_status_t *status)
{
    if (!status) return;

    memset(status, 0, sizeof(*status));
    status->platform    = SMART_HOME_NETWORK_PLATFORM_UNKNOWN;
    status->backend     = SMART_HOME_NETWORK_BACKEND_UNKNOWN;
    status->ifname      = "wlan0";
    status->init_status = SMART_HOME_NETWORK_STATUS_UNKNOWN;
    status->ip_status   = SMART_HOME_NETWORK_STATUS_UNKNOWN;
    status->dns_status  = SMART_HOME_NETWORK_STATUS_UNKNOWN;
    status->online      = false;
}

/* ── 平台名 ────────────────────────────────────────────── */

const char *smart_home_network_platform_name(
    smart_home_network_platform_t platform)
{
    switch (platform) {
    case SMART_HOME_NETWORK_PLATFORM_SIMULATOR:  return "simulator";
    case SMART_HOME_NETWORK_PLATFORM_DEVICE_WIFI: return "wifi";
    case SMART_HOME_NETWORK_PLATFORM_ETHERNET:   return "ethernet";
    default:                                     return "unknown";
    }
}

/* ── 把 port 层状态翻译成业务层状态 ────────────────────── */

static void apply_port_status(FAR smart_home_network_status_t *status,
                              const sca_wifi_status_t *port)
{
    status->platform = SMART_HOME_NETWORK_PLATFORM_DEVICE_WIFI;
    status->backend  = SMART_HOME_NETWORK_BACKEND_WIFI;

    switch (port->state) {
    case SCA_WIFI_STATE_INIT:
        status->init_status = SMART_HOME_NETWORK_OK;
        status->ip_status   = SMART_HOME_NETWORK_STATUS_NA;
        status->dns_status  = SMART_HOME_NETWORK_STATUS_NA;
        status->online      = false;
        break;

    case SCA_WIFI_STATE_CARRIER:
        status->init_status = SMART_HOME_NETWORK_OK;
        status->ip_status   = SMART_HOME_NETWORK_ERR_DHCP;
        status->dns_status  = SMART_HOME_NETWORK_STATUS_NA;
        status->online      = false;
        break;

    case SCA_WIFI_STATE_IP:
        status->init_status = SMART_HOME_NETWORK_OK;
        status->ip_status   = SMART_HOME_NETWORK_OK;
        status->dns_status  = SMART_HOME_NETWORK_STATUS_UNKNOWN;
        status->online      = false;
        break;

    case SCA_WIFI_STATE_ONLINE:
        status->init_status = SMART_HOME_NETWORK_OK;
        status->ip_status   = SMART_HOME_NETWORK_OK;
        status->dns_status  = SMART_HOME_NETWORK_OK;
        status->online      = true;
        break;

    default:
        status->init_status = SMART_HOME_NETWORK_ERR_IFUP;
        status->ip_status   = SMART_HOME_NETWORK_STATUS_NA;
        status->dns_status  = SMART_HOME_NETWORK_STATUS_NA;
        status->online      = false;
        break;
    }

    if (port->ssid[0]) {
        size_t n = strlen(port->ssid);
        if (n >= sizeof(status->ssid)) n = sizeof(status->ssid) - 1;
        memcpy(status->ssid, port->ssid, n);
        status->ssid[n] = '\0';
    }
}

/* ── 初始化（不连接）────────────────────────────────────── */

int smart_home_network_init(FAR smart_home_network_status_t *status)
{
    sca_wifi_status_t port;
    int ret;

    if (!status) return AGENT_ERROR_INVALID;

    smart_home_network_status_init(status);
    status->platform = SMART_HOME_NETWORK_PLATFORM_DEVICE_WIFI;
    status->backend  = SMART_HOME_NETWORK_BACKEND_WIFI;

    ret = sca_wifi_port_init();
    if (ret != SCA_OK) {
        SCA_LOGE(DBG_TAG, "WLAN 初始化失败（模组固件是否已上传？）");
        status->init_status = SMART_HOME_NETWORK_ERR_IFUP;
        status->ip_status   = SMART_HOME_NETWORK_STATUS_NA;
        status->dns_status  = SMART_HOME_NETWORK_STATUS_NA;
        return AGENT_ERROR;
    }

    sca_wifi_port_status(&port);
    apply_port_status(status, &port);
    SCA_LOGI(DBG_TAG, "网络已就绪，设备 %s", status->ifname);
    return AGENT_OK;
}

/* ── 配网准备：只保证设备就绪，不启动 DHCP ─────────────── */

int smart_home_network_prepare_setup(FAR smart_home_network_status_t *status)
{
    int ret;

    if (!status) return AGENT_ERROR_INVALID;

    ret = sca_wifi_port_init();
    if (ret != SCA_OK) {
        status->init_status = SMART_HOME_NETWORK_ERR_IFUP;
        return AGENT_ERROR;
    }

    status->init_status = SMART_HOME_NETWORK_OK;
    status->ip_status   = SMART_HOME_NETWORK_STATUS_NA;
    status->dns_status  = SMART_HOME_NETWORK_STATUS_NA;
    status->online      = false;
    return AGENT_OK;
}

/* ── 探测当前链路（不主动连接）─────────────────────────── */

int smart_home_network_probe(FAR smart_home_network_status_t *status)
{
    sca_wifi_status_t port;

    if (!status) return AGENT_ERROR_INVALID;

    sca_wifi_port_status(&port);
    apply_port_status(status, &port);

    /* 已有 IP 但未确认 DNS：顺手做一次解析探测。
     * 这个探测本身幂等，失败只影响 online 标志。 */
    if (status->ip_status == SMART_HOME_NETWORK_OK && !status->online) {
        if (sca_wifi_port_check_dns() == SCA_OK) {
            sca_wifi_port_status(&port);
            apply_port_status(status, &port);
        } else {
            status->dns_status = SMART_HOME_NETWORK_ERR_DNS;
            status->online = false;
        }
    }

    return AGENT_OK;
}

/* ── 用指定凭据连接 ────────────────────────────────────── */

int smart_home_network_connect_credentials(
    FAR smart_home_network_status_t *status,
    FAR const char *ssid, FAR const char *password)
{
    sca_wifi_status_t port;
    int ret;

    if (!status || !ssid || !ssid[0]) return AGENT_ERROR_INVALID;

    SCA_LOGI(DBG_TAG, "连接 %s ...", ssid);

    /* 阻塞直到拿到 IP 或超时。30 秒对家用 AP 足够；
     * 密码错误时 rt_wlan_connect 会更快返回失败。 */
    ret = sca_wifi_port_connect(ssid, password ? password : "", 30000);
    if (ret != SCA_OK) {
        SCA_LOGE(DBG_TAG, "连接 %s 失败（%d）", ssid, ret);
        status->init_status = SMART_HOME_NETWORK_OK;
        status->ip_status   = SMART_HOME_NETWORK_ERR_DHCP;
        status->online      = false;
        return AGENT_ERROR;
    }

    sca_wifi_port_status(&port);
    apply_port_status(status, &port);

    ret = smart_home_secrets_set_wifi_credentials(ssid,
                                                   password ? password : "");
    if (ret != AGENT_OK) {
        SCA_LOGW(DBG_TAG, "Wi-Fi 凭据保存失败 (%d)，本轮连接仍有效", ret);
    }

    /* DNS 只决定互联网状态；失败时保留 IP，允许局域网设备继续工作。 */
    if (sca_wifi_port_check_dns() == SCA_OK) {
        sca_wifi_port_status(&port);
        apply_port_status(status, &port);
#ifdef RT_USING_SNTP
        if (ntp_sync_to_rtc(RT_NULL) > 0) {
            sca_http_notify_time_sync();
            SCA_LOGI(DBG_TAG, "NTP time synchronized");
        } else {
            SCA_LOGW(DBG_TAG, "NTP sync failed; HTTPS remains unavailable");
        }
#endif
    } else {
        status->dns_status = SMART_HOME_NETWORK_ERR_DNS;
        status->online = false;
    }

    SCA_LOGI(DBG_TAG, "已连接 %s, ip=%s, online=%d",
             status->ssid, port.ip, status->online);
    return AGENT_OK;
}
