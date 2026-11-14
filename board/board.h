/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 板级配置入口。
 *
 * 本文件只做两件事：
 *   1. 汇总本工程需要的板级能力宏（屏幕/触摸/音频/存储）；
 *   2. 声明 board/ports/ 下各端口的初始化原型。
 *
 * 具体外设初始化一律在 board/ports 中完成。应用层不得直接
 * 引用 PSE84 的 HAL 头文件，也不得 include rtdevice.h。
 */

#ifndef __SCA_BOARD_H__
#define __SCA_BOARD_H__

#include <rtthread.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 音频 ───────────────────────────────────────────────── */
#define BOARD_AUDIO_SAMPLE_RATE     16000
#define BOARD_AUDIO_CHANNELS        1
#define BOARD_AUDIO_BITS            16
#define BOARD_AUDIO_FRAME_BYTES     2           /* 16bit * mono */
#define BOARD_AUDIO_PLAY_DEVICE     "sound0"
#define BOARD_AUDIO_RECORD_DEVICE   "mic0"

/* ── 板级初始化 ─────────────────────────────────────────── */

/**
 * 初始化所有板级外设。由 applications/sca_init.c 在系统启动后调用。
 *
 * @return 已就绪外设的位掩码，见 SCA_BOARD_* 定义；0 表示全部失败。
 *
 * 每个外设失败只记录、不中断，返回值让上层可以判断降级能力。
 */
uint32_t board_init_all(void);

#define SCA_BOARD_DISPLAY   (1u << 0)
#define SCA_BOARD_TOUCH     (1u << 1)
#define SCA_BOARD_AUDIO_OUT (1u << 2)
#define SCA_BOARD_AUDIO_IN  (1u << 3)
#define SCA_BOARD_STORAGE   (1u << 4)
#define SCA_BOARD_WIFI      (1u << 5)

/** 把位掩码转成可读字符串，用于启动日志。 */
const char *board_ready_mask_name(uint32_t mask);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_BOARD_H__ */
