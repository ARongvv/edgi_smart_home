/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 板级初始化总装。
 *
 * 设计约束：
 *   - 单个外设失败只记录，不中断后续初始化；
 *   - 返回值是成功项位掩码，应用层据此判断降级能力；
 *   - 任何情况下都返回，绝不阻塞启动超过 3 秒（WiFi 模块枚举例外，
 *     由 WiFi 端口自己的超时控制）。
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "board.h"
#include "ports/sca_display_port.h"
#include "ports/sca_touch_port.h"
#include "ports/sca_audio_port.h"
#include "ports/sca_storage_port.h"
#include "ports/sca_wifi_port.h"

#define DBG_TAG    "board"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

uint32_t board_init_all(void)
{
    uint32_t ready = 0;

    /* 顺序有依赖：
     *   storage 必须先于 display —— 字体和图标从文件系统读取；
     *   display 先于 touch —— 触摸校准参数由显示面板型号决定。
     */
    if (sca_storage_port_init() == RT_EOK)
    {
        ready |= SCA_BOARD_STORAGE;
    }
    else
    {
        LOG_E("storage init failed, resources unavailable");
    }

    if (sca_display_port_init() == RT_EOK)
    {
        ready |= SCA_BOARD_DISPLAY;
    }
    else
    {
        LOG_E("display init failed, UI cannot start");
    }

    if (sca_touch_port_init() == RT_EOK)
    {
        ready |= SCA_BOARD_TOUCH;
    }
    else
    {
        LOG_W("touch init failed, UI will be display-only");
    }

    if (sca_audio_port_init() == RT_EOK)
    {
        ready |= SCA_BOARD_AUDIO_OUT | SCA_BOARD_AUDIO_IN;
    }
    else
    {
        LOG_W("audio init failed, voice features disabled");
    }

    /* WiFi 单独判：模块固件可能未上传，这不该阻止 UI 启动。 */
    if (sca_wifi_port_init() == RT_EOK)
    {
        ready |= SCA_BOARD_WIFI;
    }
    else
    {
        LOG_W("wifi port init failed, offline mode");
    }

    LOG_I("board ready mask 0x%02x", ready);
    return ready;
}

const char *board_ready_mask_name(uint32_t mask)
{
    /* 单次调用返回静态缓冲，仅用于启动日志，不可多线程并发。
     * RT-Thread 5.0 移除了 rt_strcat，用带偏移的 rt_snprintf 追加，
     * 顺便保证不越界（原来靠 text[96] 隐式兜底）。 */
    static char text[96];
    size_t used = 0u;

    text[0] = '\0';

#define SCA_APPEND(name, bit)                                              \
    do {                                                                   \
        if ((mask) & (bit)) {                                              \
            int n = rt_snprintf(text + used, sizeof(text) - used,          \
                                "%s ", (name));                            \
            if (n > 0 && (size_t)n < sizeof(text) - used) {                \
                used += (size_t)n;                                         \
            }                                                              \
        }                                                                  \
    } while (0)

    SCA_APPEND("storage", SCA_BOARD_STORAGE);
    SCA_APPEND("display", SCA_BOARD_DISPLAY);
    SCA_APPEND("touch",   SCA_BOARD_TOUCH);
    SCA_APPEND("audio",   SCA_BOARD_AUDIO_OUT);
    SCA_APPEND("wifi",    SCA_BOARD_WIFI);

#undef SCA_APPEND

    return text[0] ? text : "none";
}
