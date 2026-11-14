/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 音频抽象（业务层视角）。
 *
 * 与 board/ports/sca_audio_port.h 的区别：
 *   - port 层是"设备级"：打开 sound 设备、设采样率、裸写 PCM；
 *   - 本层是"服务级"：给业务提供"播一段语音"、"录一段话"的语义，
 *     内部处理重采样、分块、中断、与 KWS 的互斥。
 *
 * 音频播放与采集的设备操作统一收敛到这一层。
 */

#ifndef __SCA_AUDIO_H__
#define __SCA_AUDIO_H__

#include "port/sca_os.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 播报开始/结束回调，用于 UI 显示说话动画。 */
typedef void (*sca_audio_event_fn)(int playing, void *user);

/** 初始化播报与录音服务。 */
int sca_audio_init(void);

/**
 * 播放 16kHz/16bit/mono PCM。
 *
 * 阻塞直到播完或被 sca_audio_play_stop() 打断。
 * 与录音互斥：播放期间采音会丢弃（避免自激）。
 */
int sca_audio_play_pcm(const void *pcm, size_t bytes);

/** 打断当前播报。 */
void sca_audio_play_stop(void);

/**
 * 录音到调用方缓冲。
 *
 * @param pcm       目标缓冲
 * @param max_bytes 缓冲容量
 * @param timeout_ms 最长录制时间
 * @return 实际字节数；负数错误
 */
int sca_audio_record(void *pcm, size_t max_bytes, int timeout_ms);

/** 录音是否正在进行（KWS 据此暂停推理）。 */
bool sca_audio_recording(void);

/** 注册播报状态回调。 */
void sca_audio_set_event_handler(sca_audio_event_fn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_AUDIO_H__ */
