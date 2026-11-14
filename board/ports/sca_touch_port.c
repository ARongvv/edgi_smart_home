/* SPDX-License-Identifier: Apache-2.0 */
#include <rtdevice.h>
#include <rthw.h>
#include "drv_touch.h"

#include "sca_display_port.h"
#include "sca_touch_port.h"

#define TOUCH_POLL_MS 5
#define TOUCH_RELEASE_TIMEOUT_MS 40
#define TOUCH_QUEUE_SIZE 32
#define ST7102_REPORT_COORDS 0x08
#define ST7102_COORD_VALID 0x80
#define ST7102_FIRST_POINT (ST7102_POINT0_X_COORD_High - ST7102_Read_Start_Position)
#define ST7102_REPORT_LEN (8 * ST7102_MAX_TOUCH)

static bool g_ready;
static sca_touch_point_t g_touch_queue[TOUCH_QUEUE_SIZE];
static unsigned g_touch_head;
static unsigned g_touch_count;
static sca_touch_point_t g_latest;
static bool g_dropped_press;

static void publish_touch(sca_touch_point_t point)
{
    rt_base_t level = rt_hw_interrupt_disable();

    if (g_dropped_press)
    {
        g_dropped_press = point.pressed;
    }
    else if (point.pressed && !g_latest.pressed && g_touch_count >= TOUCH_QUEUE_SIZE - 1)
    {
        /* Skip a whole contact on overflow. Every queued DOWN reserves its UP. */
        g_dropped_press = true;
    }
    else if (point.pressed != g_latest.pressed ||
             (point.pressed && g_touch_count < TOUCH_QUEUE_SIZE - 1 &&
              (point.x != g_latest.x || point.y != g_latest.y)))
    {
        if (!point.pressed)
        {
            point.x = g_latest.x;
            point.y = g_latest.y;
        }
        g_latest = point;
        g_touch_queue[(g_touch_head + g_touch_count) % TOUCH_QUEUE_SIZE] = point;
        g_touch_count++;
    }

    rt_hw_interrupt_enable(level);
}

/* Return one fresh coordinate frame, zero for no report, or an I2C error. */
static int read_touch_report(struct rt_i2c_bus_device *bus, int *x, int *y,
                             bool *pressed)
{
    uint8_t reg[] = {ST7102_Read_Start_Position >> 8, ST7102_Read_Start_Position & 0xff};
    uint8_t report[ST7102_REPORT_LEN];
    uint8_t ack[] = {ST7102_READ_STATUS, 0};
    struct rt_i2c_msg read[] = {
        {.addr = ST7102_ADDRESS, .flags = RT_I2C_WR, .buf = reg, .len = sizeof(reg)},
        {.addr = ST7102_ADDRESS, .flags = RT_I2C_RD, .buf = report, .len = sizeof(report)},
    };
    struct rt_i2c_msg clear = {
        .addr = ST7102_ADDRESS, .flags = RT_I2C_WR, .buf = ack, .len = sizeof(ack),
    };

    if (rt_i2c_transfer(bus, read, 2) != 2)
        return -RT_EIO;
    if (report[0] != 0 && rt_i2c_transfer(bus, &clear, 1) != 1)
        return -RT_EIO;
    if (!(report[0] & ST7102_REPORT_COORDS))
        return 0;

    /* The controller may leave the previous X/Y and pressure in an UP report. */
    const uint8_t *contact = &report[ST7102_FIRST_POINT];
    *pressed = (contact[0] & ST7102_COORD_VALID) != 0;
    if (*pressed)
    {
        *x = ((contact[0] & 0x3f) << 8) | contact[1];
        *y = ((contact[2] & 0x3f) << 8) | contact[3];
    }
    return 1;
}

static void touch_poll(void *argument)
{
    struct rt_i2c_bus_device *bus = argument;
    sca_display_info_t display;
    sca_touch_point_t point = {0};
    const rt_tick_t release_timeout = rt_tick_from_millisecond(TOUCH_RELEASE_TIMEOUT_MS);
    rt_tick_t last_report = rt_tick_get();

    sca_display_port_info(&display);
    for (;;)
    {
        int raw_x, raw_y;
        bool pressed;
        int result = read_touch_report(bus, &raw_x, &raw_y, &pressed);
        if (result > 0)
        {
            last_report = rt_tick_get();
            point.pressed = pressed;
            if (pressed)
            {
                int x, y;
#if BSP_LCD_ROTATION_DEGREES == 90
                x = raw_y;
                y = display.height - 1 - raw_x;
#elif BSP_LCD_ROTATION_DEGREES == 180
                x = display.width - 1 - raw_x;
                y = display.height - 1 - raw_y;
#elif BSP_LCD_ROTATION_DEGREES == 270
                x = display.width - 1 - raw_y;
                y = raw_x;
#else
                x = raw_x;
                y = raw_y;
#endif
                point.x = x < 0 ? 0 : (x >= display.width ? display.width - 1 : x);
                point.y = y < 0 ? 0 : (y >= display.height ? display.height - 1 : y);
            }
            publish_touch(point);
        }
        else if (result == 0 && point.pressed && rt_pin_read(ST7102_IRQ_PIN) == PIN_HIGH &&
                 rt_tick_get() - last_report >= release_timeout)
        {
            /* Recover a missing UP after an idle interval, never on one empty read. */
            point.pressed = false;
            publish_touch(point);
        }
        rt_thread_mdelay(TOUCH_POLL_MS);
    }
}

rt_err_t sca_touch_port_init(void)
{
    if (g_ready)
        return RT_EOK;

    sca_display_info_t display;
    sca_display_port_info(&display);
    if (!display.width || !display.height)
        return -RT_EINVAL;

    struct rt_i2c_bus_device *bus = rt_i2c_bus_device_find("i2c1");
    if (!bus || rt_hw_ST7102_port() != RT_EOK)
        return -RT_ERROR;

    rt_thread_t thread = rt_thread_create("touch", touch_poll, bus, 2048,
                                          RT_MAIN_THREAD_PRIORITY - 1, 10);
    if (!thread)
        return -RT_ENOMEM;

    rt_err_t result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(thread);
        return result;
    }
    g_ready = true;
    return RT_EOK;
}

bool sca_touch_port_is_ready(void)
{
    return g_ready;
}

void sca_touch_port_read(sca_touch_point_t *point, bool *more)
{
    rt_base_t level = rt_hw_interrupt_disable();

    if (g_touch_count)
    {
        *point = g_touch_queue[g_touch_head];
        g_touch_head = (g_touch_head + 1) % TOUCH_QUEUE_SIZE;
        g_touch_count--;
    }
    else
    {
        *point = g_latest;
    }
    *more = g_touch_count != 0;

    rt_hw_interrupt_enable(level);
}
