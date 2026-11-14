/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __SCA_DISPLAY_PORT_H__
#define __SCA_DISPLAY_PORT_H__

#include <rtthread.h>
#include <stdint.h>

typedef struct
{
    uint16_t width;
    uint16_t height;
} sca_display_info_t;

rt_err_t sca_display_port_init(void);
void sca_display_port_info(sca_display_info_t *info);
void sca_display_port_flush(const void *pixels, uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height,
                            uint32_t stride_pixels, rt_bool_t present);

#endif
