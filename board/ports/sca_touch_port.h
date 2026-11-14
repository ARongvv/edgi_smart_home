/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 触摸端口头文件。
 *
 * 上层通过 sca_touch_port_read() 按顺序读取单指触摸状态。
 */

#ifndef __SCA_TOUCH_PORT_H__
#define __SCA_TOUCH_PORT_H__

#include <rtthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int16_t  x;
    int16_t  y;
    uint8_t  pressed;      /* 1 = 按下，0 = 抬起 */
} sca_touch_point_t;

/** 初始化触摸控制器。 */
rt_err_t sca_touch_port_init(void);

/**
 * 读取队列中的下一帧。队列为空时返回上次交付的状态。
 * 由 LVGL 线程调用；more 表示还有帧待交付。
 */
void sca_touch_port_read(sca_touch_point_t *point, bool *more);
bool sca_touch_port_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_TOUCH_PORT_H__ */
