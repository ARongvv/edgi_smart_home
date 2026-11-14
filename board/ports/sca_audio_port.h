/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 音频端口头文件。
 *
 * 上层（voice/reminder）只用这两个函数：播一段 PCM，或录一段 PCM。
 * 采样格式固定 16kHz / 16bit / 单声道，由 BOARD_AUDIO_* 定义，
 * 任何格式转换在端口内部完成。
 */

#ifndef __SCA_AUDIO_PORT_H__
#define __SCA_AUDIO_PORT_H__

#include <rtthread.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化播放与录音设备。 */
rt_err_t sca_audio_port_init(void);

/**
 * 播放整块 PCM。
 *
 * 阻塞直到播放完成或出错。调用方负责不并发调用。
 *
 * @param pcm    16kHz/16bit/mono 原始数据
 * @param bytes  字节数，必须为 2 的倍数
 * @return RT_EOK 成功
 */
rt_err_t sca_audio_port_play(const void *pcm, size_t bytes);

/**
 * 录音到调用方缓冲。
 *
 * @param pcm     目标缓冲
 * @param bytes   期望录制的字节数（16kHz*2B 下，1 秒 = 32000 字节）
 * @param timeout 超时（毫秒），0 表示不限制
 * @return 实际录到的字节数；负数表示错误
 */
int sca_audio_port_record(void *pcm, size_t bytes, int timeout_ms);

/** 停止正在进行的播放（用于打断播报）。 */
void sca_audio_port_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_AUDIO_PORT_H__ */
