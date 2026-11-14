/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 显示与触摸的 LVGL 绑定实现。
 *
 * 显示与触摸初始化统一走两个函数：
 *   sca_display_bind / sca_display_unbind
 *
 * LVGL renders RGB565 dirty rectangles. The BSP handles scanout and rotation.
 */

#include <rtthread.h>
#include <lvgl.h>

#include "port/sca_config.h"
#include "port/sca_os.h"
#include "port/sca_display.h"

#include "board/ports/sca_display_port.h"
#include "board/ports/sca_touch_port.h"

#define DBG_TAG "sca.disp"

#define SCA_DRAW_BUF_LINES 48

static void *g_draw_buf;

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t width = area->x2 - area->x1 + 1;
    uint32_t height = area->y2 - area->y1 + 1;
    uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);

    sca_display_port_flush(px_map, area->x1, area->y1, width, height,
                           stride / sizeof(uint16_t), lv_display_flush_is_last(disp));
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    sca_touch_point_t point;

    (void)indev;

    sca_touch_port_read(&point, &data->continue_reading);
    data->point.x = point.x;
    data->point.y = point.y;
    data->state = point.pressed ? LV_INDEV_STATE_PRESSED
                                    : LV_INDEV_STATE_RELEASED;
}

int sca_display_bind(sca_display_bind_t *out)
{
    sca_display_info_t info;
    lv_display_t *disp;
    lv_indev_t *indev = NULL;
    uint32_t buf_bytes;

    if (!out) {
        return SCA_ERR_PARAM;
    }

    rt_memset(out, 0, sizeof(*out));

    sca_display_port_info(&info);
    if (info.width == 0 || info.height == 0) {
        SCA_LOGE(DBG_TAG, "板级显示未初始化");
        return SCA_ERR_STATE;
    }

    buf_bytes = lv_draw_buf_width_to_stride(info.width, LV_COLOR_FORMAT_RGB565) *
                SCA_DRAW_BUF_LINES;
    g_draw_buf = rt_malloc_align(buf_bytes, LV_DRAW_BUF_ALIGN);
    if (!g_draw_buf) {
        SCA_LOGE(DBG_TAG, "Failed to allocate LCD draw buffer (%u bytes)", (unsigned)buf_bytes);
        return SCA_ERR_NOMEM;
    }

    disp = lv_display_create(info.width, info.height);
    if (!disp) {
        rt_free_align(g_draw_buf);
        g_draw_buf = NULL;
        return SCA_ERR_NOMEM;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, g_draw_buf, NULL, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_tick_set_cb(rt_tick_get_millisecond);

    /* 触摸可选：没有触摸时 UI 仍可显示（只读模式） */
    {
        if (sca_touch_port_is_ready()) {
            indev = lv_indev_create();
            if (indev) {
                lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
                lv_indev_set_read_cb(indev, touch_read_cb);
                lv_indev_set_display(indev, disp);
            }
        } else {
            SCA_LOGW(DBG_TAG, "触摸不可用，UI 进入只读模式");
        }
    }

    out->disp   = disp;
    out->indev  = indev;
    out->width  = info.width;
    out->height = info.height;

    SCA_LOGI(DBG_TAG, "%dx%d RGB565", info.width, info.height);
    return SCA_OK;
}

void sca_display_unbind(sca_display_bind_t *bind)
{
    if (!bind) {
        return;
    }

    if (bind->indev) {
        lv_indev_delete((lv_indev_t *)bind->indev);
        bind->indev = NULL;
    }
    if (bind->disp) {
        lv_display_delete((lv_display_t *)bind->disp);
        bind->disp = NULL;
    }

    if (g_draw_buf) {
        rt_free_align(g_draw_buf);
        g_draw_buf = NULL;
    }
}
