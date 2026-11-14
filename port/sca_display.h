/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 显示与触摸的 LVGL 绑定抽象。
 *
 * 显示初始化需要完成：
 *   - 打开显示后端（帧缓冲）
 *   - 打开触摸后端（输入设备）
 *   - 填充 lv_display_t / lv_indev_t
 *
 * Edgi-Talk 的显示链路是 "LVGL → 帧缓冲 → MIPI-DSI 面板"（可能经
 * VGLite GPU），触摸是 "LVGL indev → 触摸 IC 轮询"。
 *
 * 本接口把这两件事收敛成两个函数，业务层不碰 LVGL 驱动细节。
 */

#ifndef __SCA_DISPLAY_H__
#define __SCA_DISPLAY_H__

#include "port/sca_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 绑定结果，供诊断与 UI 判断降级。 */
typedef struct
{
    void *disp;             /* lv_display_t*，对业务层不透明 */
    void *indev;            /* lv_indev_t*，触摸不可用时为 NULL */
    int   width;
    int   height;
} sca_display_bind_t;

/**
 * 把板级帧缓冲与触摸接入 LVGL。
 *
 * 调用前必须已调用 sca_display_port_init() / sca_touch_port_init()。
 * 内部完成：
 *   - lv_display_create + 双缓冲 + flush 回调
 *   - lv_indev_create + read 回调（触摸不可用时跳过）
 *
 * @return SCA_OK 成功
 */
int sca_display_bind(sca_display_bind_t *out);

/** 解绑并释放 LVGL 侧的 display/indev（不关闭硬件）。 */
void sca_display_unbind(sca_display_bind_t *bind);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_DISPLAY_H__ */
