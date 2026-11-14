/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 音频端口实现。
 *
 * 基于 RT-Thread 的 Audio 框架（sound 设备）。PSE84 板上 ES8388 codec
 * 由 BSP 注册为 sound 设备；本文件只负责：
 *   - 打开设备并设置采样格式
 *   - 阻塞式写入/读取
 *   - 播放中的可中断
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/audio.h>

#include <string.h>

#include "board/board.h"
#include "board/ports/sca_audio_port.h"

#define DBG_TAG    "sca.audio"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

static rt_device_t g_play_dev = RT_NULL;
static rt_device_t g_rec_dev  = RT_NULL;
static volatile rt_bool_t g_inited = RT_FALSE;
static volatile rt_bool_t g_stop   = RT_FALSE;

rt_err_t sca_audio_port_init(void)
{
    struct rt_audio_caps caps;

    if (g_inited)
    {
        return RT_EOK;
    }

    g_play_dev = rt_device_find(BOARD_AUDIO_PLAY_DEVICE);
    if (g_play_dev)
    {
        if (rt_device_open(g_play_dev, RT_DEVICE_OFLAG_WRONLY) != RT_EOK)
        {
            LOG_E("open %s failed", BOARD_AUDIO_PLAY_DEVICE);
            g_play_dev = RT_NULL;
        }
        else
        {
            rt_memset(&caps, 0, sizeof(caps));
            caps.main_type      = AUDIO_TYPE_OUTPUT;
            caps.sub_type       = AUDIO_DSP_PARAM;
            caps.udata.config.samplerate = BOARD_AUDIO_SAMPLE_RATE;
            caps.udata.config.channels   = BOARD_AUDIO_CHANNELS;
            caps.udata.config.samplebits = BOARD_AUDIO_BITS;
            if (rt_device_control(g_play_dev, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
            {
                LOG_W("play device configure failed, using defaults");
            }
        }
    }
    else
    {
        LOG_W("play device '%s' not found", BOARD_AUDIO_PLAY_DEVICE);
    }

    g_rec_dev = rt_device_find(BOARD_AUDIO_RECORD_DEVICE);
    if (g_rec_dev)
    {
        if (rt_device_open(g_rec_dev, RT_DEVICE_OFLAG_RDONLY) != RT_EOK)
        {
            LOG_E("open %s failed", BOARD_AUDIO_RECORD_DEVICE);
            g_rec_dev = RT_NULL;
        }
        else
        {
            rt_memset(&caps, 0, sizeof(caps));
            caps.main_type      = AUDIO_TYPE_INPUT;
            caps.sub_type       = AUDIO_DSP_PARAM;
            caps.udata.config.samplerate = BOARD_AUDIO_SAMPLE_RATE;
            caps.udata.config.channels   = BOARD_AUDIO_CHANNELS;
            caps.udata.config.samplebits = BOARD_AUDIO_BITS;
            if (rt_device_control(g_rec_dev, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
            {
                LOG_W("record device configure failed, using defaults");
            }
        }
    }
    else
    {
        LOG_W("record device '%s' not found", BOARD_AUDIO_RECORD_DEVICE);
    }

    if (g_play_dev == RT_NULL && g_rec_dev == RT_NULL)
    {
        return -RT_ERROR;
    }

    g_inited = RT_TRUE;
    LOG_I("audio %dHz/%dch/%dbit, play=%s rec=%s",
          BOARD_AUDIO_SAMPLE_RATE, BOARD_AUDIO_CHANNELS, BOARD_AUDIO_BITS,
          g_play_dev ? "ok" : "none", g_rec_dev ? "ok" : "none");
    return RT_EOK;
}

rt_err_t sca_audio_port_play(const void *pcm, size_t bytes)
{
    const uint8_t *cursor = (const uint8_t *)pcm;
    size_t remaining = bytes;

    if (g_play_dev == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (pcm == RT_NULL || bytes == 0 || (bytes & 1u))
    {
        return -RT_EINVAL;
    }

    g_stop = RT_FALSE;

    /* 分块写入：一次写入大缓冲会长时间占住设备锁，
     * 分块让 stop() 能在块边界生效。 */
    while (remaining > 0 && !g_stop)
    {
        size_t chunk = remaining > 4096u ? 4096u : remaining;
        rt_size_t written = rt_device_write(g_play_dev, 0, cursor, chunk);

        if (written == 0)
        {
            LOG_W("audio write stalled at %u/%u",
                  (unsigned)(bytes - remaining), (unsigned)bytes);
            return -RT_EIO;
        }

        cursor    += written;
        remaining -= written;
    }

    return g_stop ? -RT_EINTR : RT_EOK;
}

int sca_audio_port_record(void *pcm, size_t bytes, int timeout_ms)
{
    uint8_t *cursor = (uint8_t *)pcm;
    size_t remaining = bytes;
    rt_tick_t deadline = 0;

    if (g_rec_dev == RT_NULL)
    {
        return -RT_ERROR;
    }
    if (pcm == RT_NULL || bytes == 0 || (bytes & 1u))
    {
        return -RT_EINVAL;
    }

    if (timeout_ms > 0)
    {
        deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);
    }

    while (remaining > 0)
    {
        rt_size_t got = rt_device_read(g_rec_dev, 0, cursor,
                                      remaining > 4096u ? 4096u : remaining);
        if (got == 0)
        {
            if (deadline != 0 && rt_tick_get() > deadline)
            {
                break;
            }
            rt_thread_mdelay(5);
            continue;
        }

        cursor    += got;
        remaining -= got;
    }

    return (int)(bytes - remaining);
}

void sca_audio_port_stop(void)
{
    g_stop = RT_TRUE;
}
