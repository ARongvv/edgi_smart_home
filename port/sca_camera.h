/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 摄像头抽象。
 *
 * 为什么需要这层接口：
 *   业务层不应直接依赖摄像头驱动接口（VIDIOC_REQBUFS / QBUF / DQBUF /
 *   STREAMON）。Edgi-Talk 板上没有 MIPI-CSI，只有 USB-UVC，摄像头
 *   操作统一收敛到这一层。
 *
 *   把"启动、停止、取最新帧"抽成接口后，业务层（相机弹窗、
 *   快照工具）完全不变，只需替换 port 实现。
 *
 * 实现方：
 *   board/ports/sca_camera_uvc.c   USB-UVC（本板）
 *   board/ports/sca_camera_csi.c   MIPI-CSI（若换板子）
 */

#ifndef __SCA_CAMERA_H__
#define __SCA_CAMERA_H__

#include "port/sca_os.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SCA_CAMERA_OFF = 0,
    SCA_CAMERA_STARTING,
    SCA_CAMERA_RUNNING,
    SCA_CAMERA_ERROR,
} sca_camera_state_t;

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint8_t  fps;
    uint32_t sequence;         /* 已产出的帧数，用于判断是否有新帧 */
    sca_camera_state_t state;
    int      last_error;
} sca_camera_status_t;

/**
 * 打开摄像头并开始出流。
 *
 * 首次调用才真正上电探测，重复调用幂等。
 */
int sca_camera_start(void);

/** 停止出流并释放缓冲。 */
void sca_camera_stop(void);

/** 查询状态。 */
void sca_camera_status(sca_camera_status_t *out);

/**
 * 复制最新一帧到调用方缓冲。
 *
 * 业务层的预览弹窗每 66ms 调一次。实现应保证：
 *   - 不阻塞采集线程；
 *   - 没有新帧时返回 SCA_ERR_BUSY，让 UI 跳过本次刷新；
 *   - 输出格式固定 RGB565。
 *
 * @param buffer     目标缓冲，大小 >= width*height*2
 * @param capacity   缓冲容量
 * @param out_seq    输出：本次复制的是第几帧
 * @return SCA_OK 有新帧；SCA_ERR_BUSY 无新帧
 */
int sca_camera_copy_latest(void *buffer, size_t capacity, uint32_t *out_seq);

/**
 * 抓一帧并编码为 JPEG 存盘（用于快照工具）。
 *
 * @param path 目标路径
 * @return SCA_OK 成功
 */
int sca_camera_snapshot(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_CAMERA_H__ */
