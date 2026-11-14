/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 环境传感器抽象。
 *
 * 业务层不应直接按设备节点读取传感器（如 open("/dev/sensor0") 加
 * ioctl(SNIOC_READ_RAW_DATA) 读 AHT30 温湿度与光照）。RT-Thread 没有
 * 对应的 sensors 设备节点。
 *
 * Edgi-Talk 板上自带 AHT20（温湿度）+ LSM6DS3TR-C（六轴），由 BSP 的
 * sensor 框架注册。本接口把"读一次环境数据"抽象出来，业务层只关心数值。
 */

#ifndef __SCA_SENSOR_H__
#define __SCA_SENSOR_H__

#include "port/sca_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int  temperature_c;        /* 摄氏度，整数 */
    int  humidity_pct;         /* 相对湿度百分比 */
    int  ambient_light;        /* 0~1000，无光照传感器时为 -1 */
    int  valid;                /* 1 = 本次读取成功 */
} sca_env_sample_t;

/**
 * 读一次环境数据。
 *
 * 阻塞不超过 100ms。传感器缺失时返回错误并把 valid 置 0，
 * 调用方（get_indoor_environment 工具）应向上报"不可用"而不是编造数值。
 *
 * @return SCA_OK 成功
 */
int sca_sensor_read_env(sca_env_sample_t *out);

/** 环境传感器是否可用（启动时探测一次）。 */
int sca_sensor_env_available(void);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_SENSOR_H__ */
