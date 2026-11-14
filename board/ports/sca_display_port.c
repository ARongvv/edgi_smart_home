/* SPDX-License-Identifier: Apache-2.0 */
#include <rtdevice.h>
#include "drv_gpio.h"

#include "sca_display_port.h"

#define DBG_TAG "sca.display"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define LCD_BACKLIGHT_POWER GET_PIN(15, 7)
#define LCD_BACKLIGHT_ENABLE GET_PIN(20, 6)

/* The Edgi-Talk HAL owns the scanout buffer and synchronizes presentation. */
extern void lcd_flush_rgb565_area(const void *pixels, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height,
                                  uint32_t src_stride, rt_bool_t present);

static sca_display_info_t g_info;
static rt_bool_t g_backlight_on;

rt_err_t sca_display_port_init(void)
{
    rt_device_t lcd = rt_device_find("lcd");
    struct rt_device_graphic_info info = {0};

    if (lcd == RT_NULL ||
        rt_device_control(lcd, RTGRAPHIC_CTRL_GET_INFO, &info) != RT_EOK ||
        info.framebuffer == RT_NULL || info.width == 0 || info.height == 0 ||
        info.pixel_format != RTGRAPHIC_PIXEL_FORMAT_RGB565)
    {
        LOG_E("BSP LCD is unavailable or is not RGB565");
        return -RT_ERROR;
    }

    g_info.width = info.width;
    g_info.height = info.height;
    LOG_I("BSP LCD %ux%u RGB565", g_info.width, g_info.height);
    return RT_EOK;
}

void sca_display_port_info(sca_display_info_t *info)
{
    *info = g_info;
}

void sca_display_port_flush(const void *pixels, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height,
                            uint32_t stride_pixels, rt_bool_t present)
{
    lcd_flush_rgb565_area(pixels, x, y, width, height, stride_pixels, present);
    if (present && !g_backlight_on)
    {
        rt_pin_mode(LCD_BACKLIGHT_POWER, PIN_MODE_OUTPUT);
        rt_pin_mode(LCD_BACKLIGHT_ENABLE, PIN_MODE_OUTPUT);
        rt_pin_write(LCD_BACKLIGHT_POWER, PIN_HIGH);
        rt_pin_write(LCD_BACKLIGHT_ENABLE, PIN_HIGH);
        g_backlight_on = RT_TRUE;
        LOG_I("First frame submitted, backlight enabled");
    }
}
