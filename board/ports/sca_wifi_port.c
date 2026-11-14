/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * WiFi 端口实现。
 *
 * 基于 RT-Thread 的 WLAN 框架（components/drivers/wlan）+ SAL。
 * CYW55500 的 WHD 驱动由 BSP 提供，并在后台注册 STA 设备。
 *
 * WHD_RESOURCES_IN_MEMORY 将模组固件、CLM 与 NVRAM 编入应用镜像，
 * 不依赖额外 FAL 资源分区；初始化失败时仍允许系统进入离线 UI。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#ifdef RT_USING_WIFI
#include <wlan_dev.h>
#include <wlan_mgnt.h>
#include <wlan_prot.h>
#endif

#ifdef RT_USING_SAL
/* RT-Thread 5.0 没有统一的 <sal.h>：
 *   <sys/socket.h>（sal/include/socket/sys_socket 下）—— socket 系列
 *   <sal_netdb.h>                                     —— getaddrinfo
 * 不要 include <sal_socket.h>：它自带一套 sockaddr_storage 定义，
 * 与 sys/socket.h 冲突。 */
#include <sys/socket.h>
#include <sal_netdb.h>
#endif
#ifdef RT_USING_NETDEV
#include <netdev.h>
#endif

#include "board/board.h"
#include "board/ports/sca_wifi_port.h"

#define DBG_TAG    "sca.wifi"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

#ifndef SCA_WIFI_DEVICE_NAME
#define SCA_WIFI_DEVICE_NAME    RT_WLAN_DEVICE_STA_NAME
#endif

#define SCA_DNS_PROBE_HOST      "www.baidu.com"
#define SCA_WIFI_JOIN_ATTEMPTS  3
#define SCA_WIFI_DEVICE_WAIT_MS 5000
#define SCA_WIFI_DNS_ATTEMPTS   3

/* 部分手机热点下发的 DNS 无法解析外网域名，探测失败时改用公共 DNS 重试。 */
#define SCA_DNS_FALLBACK_0      "223.5.5.5"
#define SCA_DNS_FALLBACK_1      "114.114.114.114"

static rt_bool_t g_inited = RT_FALSE;
static sca_wifi_status_t g_status =
{
    .state = SCA_WIFI_STATE_UNKNOWN,
};

#ifdef RT_USING_WIFI
static struct rt_wlan_device *wifi_station(void)
{
    rt_device_t device = rt_device_find(SCA_WIFI_DEVICE_NAME);
    struct rt_wlan_device *station = (struct rt_wlan_device *)device;

    return device && device->type == RT_Device_Class_NetIf &&
           station->mode == RT_WLAN_STATION ? station : RT_NULL;
}

static rt_bool_t wifi_ipv4(char ip[16])
{
    ip[0] = '\0';
    if (!rt_wlan_is_connected())
    {
        return RT_FALSE;
    }
#ifdef RT_USING_NETDEV
    /* wlan0 is the radio device. Its lwIP netdev has a separate name. */
    struct rt_wlan_device *station = wifi_station();
    struct netdev *netdev = station ? station->netdev : RT_NULL;
    if (netdev && netdev_is_up(netdev) && netdev_is_link_up(netdev))
    {
        ip_addr_t addr = netdev->ip_addr;
        if (!ip_addr_isany(&addr) && IP_GET_TYPE(&addr) == IPADDR_TYPE_V4)
        {
            return inet_ntoa_r(addr, ip, 16) != RT_NULL;
        }
    }
    return RT_FALSE;
#else
    return rt_wlan_is_ready();
#endif
}

/* 把 STA 的 DNS 切到公共服务器，供解析失败后重试。
 * 只改运行时 netdev 配置，不改持久化的 Wi-Fi 凭据。 */
static rt_bool_t wifi_use_fallback_dns(void)
{
#ifdef RT_USING_NETDEV
    struct rt_wlan_device *station = wifi_station();
    struct netdev *netdev = station ? station->netdev : RT_NULL;
    ip_addr_t addr;

    if (netdev == RT_NULL)
    {
        return RT_FALSE;
    }

    if (inet_aton(SCA_DNS_FALLBACK_0, &addr) == 0)
    {
        return RT_FALSE;
    }
    netdev_set_dns_server(netdev, 0, &addr);
    if (inet_aton(SCA_DNS_FALLBACK_1, &addr) != 0)
    {
        netdev_set_dns_server(netdev, 1, &addr);
    }

    LOG_W("%s dns unavailable, fallback to %s", netdev->name,
          SCA_DNS_FALLBACK_0);
    return RT_TRUE;
#else
    return RT_FALSE;
#endif
}
#endif

rt_err_t sca_wifi_port_init(void)
{
#ifdef RT_USING_WIFI
    if (g_inited)
    {
        return RT_EOK;
    }

    if (rt_wlan_is_ready() == RT_TRUE)
    {
        g_status.state = SCA_WIFI_STATE_CARRIER;
        g_inited = RT_TRUE;
        LOG_I("wlan already ready");
        return RT_EOK;
    }

    /* Initialize the management framework. WHD registers the device later. */
    if (rt_wlan_init() != RT_EOK)
    {
        LOG_W("wlan init failed (firmware uploaded?)");
        g_status.state = SCA_WIFI_STATE_ERROR;
        return -RT_ERROR;
    }

    g_status.state = SCA_WIFI_STATE_INIT;
    g_inited = RT_TRUE;
    LOG_I("wlan framework ready, device=%s", SCA_WIFI_DEVICE_NAME);
    return RT_EOK;
#else
    LOG_W("RT_USING_WIFI not enabled in rtconfig.h");
    return -RT_ERROR;
#endif
}

rt_err_t sca_wifi_port_connect(const char *ssid, const char *password,
                               int timeout_ms)
{
#ifdef RT_USING_WIFI
    rt_tick_t start;
    rt_tick_t device_wait = rt_tick_from_millisecond(SCA_WIFI_DEVICE_WAIT_MS);
    rt_tick_t dhcp_wait = timeout_ms > 0 ? rt_tick_from_millisecond(timeout_ms) : 0;
    rt_err_t ret = -RT_ERROR;

    if (ssid == RT_NULL || ssid[0] == '\0')
    {
        return -RT_EINVAL;
    }

    if (sca_wifi_port_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    start = rt_tick_get();
    while (!wifi_station())
    {
        if (rt_tick_get() - start >= device_wait)
        {
            LOG_W("Wi-Fi station did not register");
            g_status.state = SCA_WIFI_STATE_ERROR;
            return -RT_ETIMEOUT;
        }
        rt_thread_mdelay(100);
    }

    g_status.ip[0] = '\0';
    g_status.ssid[0] = '\0';
    for (unsigned attempt = 0; attempt < SCA_WIFI_JOIN_ATTEMPTS; ++attempt)
    {
        ret = rt_wlan_connect(ssid, password);
        if (ret == RT_EOK)
        {
            break;
        }
        LOG_W("Wi-Fi association attempt %u failed (%d)", attempt + 1, ret);
        if (ret == -RT_EINVAL || ret == -RT_ENOMEM ||
            attempt + 1 == SCA_WIFI_JOIN_ATTEMPTS)
        {
            break;
        }
        rt_thread_mdelay(500);
    }
    if (ret != RT_EOK)
    {
        g_status.state = SCA_WIFI_STATE_ERROR;
        return ret;
    }

    rt_strncpy(g_status.ssid, ssid, sizeof(g_status.ssid) - 1);
    g_status.state = SCA_WIFI_STATE_CARRIER;

    /* Require the STA link and its own current IPv4 lease. */
    start = rt_tick_get();
    while (timeout_ms <= 0 || rt_tick_get() - start < dhcp_wait)
    {
        if (wifi_ipv4(g_status.ip))
        {
            g_status.state = SCA_WIFI_STATE_IP;
            LOG_I("connected to %s, ip=%s", g_status.ssid, g_status.ip);
            return RT_EOK;
        }
        rt_thread_mdelay(100);
    }

    LOG_E("connect to %s timeout after %d ms", ssid, timeout_ms);
    g_status.state = SCA_WIFI_STATE_ERROR;
    return -RT_ETIMEOUT;
#else
    (void)ssid; (void)password; (void)timeout_ms;
    return -RT_ERROR;
#endif
}

rt_err_t sca_wifi_port_disconnect(void)
{
#ifdef RT_USING_WIFI
    if (g_inited && rt_wlan_disconnect() == RT_EOK)
    {
        g_status.state = SCA_WIFI_STATE_INIT;
        g_status.ip[0] = '\0';
        g_status.ssid[0] = '\0';
        return RT_EOK;
    }
#endif
    return -RT_ERROR;
}

void sca_wifi_port_status(sca_wifi_status_t *out)
{
    if (out == RT_NULL)
    {
        return;
    }

#ifdef RT_USING_WIFI
    /* 用框架的真实状态覆盖缓存的 state，避免缓存过期 */
    if (g_inited)
    {
        if (wifi_ipv4(g_status.ip))
        {
            if (g_status.state < SCA_WIFI_STATE_IP)
            {
                g_status.state = SCA_WIFI_STATE_IP;
            }
        }
        else if (!rt_wlan_is_connected())
        {
            g_status.ssid[0] = '\0';
            if (g_status.state > SCA_WIFI_STATE_INIT)
            {
                g_status.state = SCA_WIFI_STATE_INIT;
            }
        }
        else if (g_status.state > SCA_WIFI_STATE_CARRIER)
        {
            g_status.state = SCA_WIFI_STATE_CARRIER;
        }
    }
#endif

    *out = g_status;
}

rt_err_t sca_wifi_port_check_dns(void)
{
#if defined(RT_USING_SAL) && defined(RT_USING_LWIP)
    struct addrinfo hints;
    struct addrinfo *res = RT_NULL;
    int ret;
#ifdef RT_USING_WIFI
    rt_bool_t fallback_applied = RT_FALSE;
#endif

    if (g_status.state < SCA_WIFI_STATE_IP)
    {
        return -RT_ERROR;
    }

    rt_memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    for (unsigned attempt = 0; attempt < SCA_WIFI_DNS_ATTEMPTS; ++attempt)
    {
        ret = sal_getaddrinfo(SCA_DNS_PROBE_HOST, RT_NULL, &hints, &res);
        if (ret == 0)
        {
            if (res)
            {
                sal_freeaddrinfo(res);
            }
            g_status.state = SCA_WIFI_STATE_ONLINE;
            LOG_I("dns probe ok, link online");
            return RT_EOK;
        }
        if (attempt + 1 < SCA_WIFI_DNS_ATTEMPTS)
        {
#ifdef RT_USING_WIFI
            /* 首轮失败后切换到公共 DNS，用剩余尝试验证是否为本机 DNS 问题。 */
            if (!fallback_applied)
            {
                fallback_applied = wifi_use_fallback_dns();
            }
#endif
            rt_thread_mdelay(750);
        }
    }

    LOG_W("dns probe %s failed (%d)", SCA_DNS_PROBE_HOST, ret);
    g_status.state = SCA_WIFI_STATE_IP;
    return -RT_ERROR;
#else
    return -RT_ERROR;
#endif
}
