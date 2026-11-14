/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * WiFi 端口头文件。
 *
 * 与 ESP-Hosted 的板级直调（board_esp_hosted_wifi_connect）不同，
 * 这里走上层可控的三步：
 *   1. 准备(init)    —— 保证 wlan 设备与管理框架就绪
 *   2. 连接(connect) —— 下发 SSID/密码，等 DHCP
 *   3. 状态(status)  —— 查询当前链路四态
 *
 * 这样应用层不需要知道用的是 CYW55512 还是别的模组。
 */

#ifndef __SCA_WIFI_PORT_H__
#define __SCA_WIFI_PORT_H__

#include <rtthread.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 网络四态：init/ip/dns/online 的降级链。 */
typedef enum
{
    SCA_WIFI_STATE_UNKNOWN = 0,
    SCA_WIFI_STATE_INIT,        /* 设备就绪，未关联 */
    SCA_WIFI_STATE_CARRIER,     /* 已关联 AP，未拿到 IP */
    SCA_WIFI_STATE_IP,          /* 有 IP，DNS 未验证 */
    SCA_WIFI_STATE_ONLINE,      /* DNS 解析成功，链路可用 */
    SCA_WIFI_STATE_ERROR = -1,
} sca_wifi_state_t;

typedef struct
{
    sca_wifi_state_t state;
    char     ssid[33];
    char     ip[16];
    int      rssi;              /* dBm，0 表示未取到 */
} sca_wifi_status_t;

/** 初始化 WLAN 设备与管理框架（不连接）。 */
rt_err_t sca_wifi_port_init(void);

/**
 * 连接指定 AP。
 *
 * 阻塞直到拿到 IP 或超时。内部完成 SSID 设置、密码设置、关联、
 * DHCP。DNS 验证由调用方通过 sca_wifi_port_status() 轮询。
 *
 * @param ssid      SSID，最长 32 字节
 * @param password  密码，可为 NULL（开放网络）
 * @param timeout_ms 超时
 * @return RT_EOK 已拿到 IP
 */
rt_err_t sca_wifi_port_connect(const char *ssid, const char *password,
                               int timeout_ms);

/** 断开当前连接。 */
rt_err_t sca_wifi_port_disconnect(void);

/** 查询链路状态。 */
void sca_wifi_port_status(sca_wifi_status_t *out);

/**
 * DNS 连通性探测。
 *
 * 解析一个固定域名，成功则把状态推进到 SCA_WIFI_STATE_ONLINE。
 * @return RT_EOK 解析成功
 */
rt_err_t sca_wifi_port_check_dns(void);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_WIFI_PORT_H__ */
