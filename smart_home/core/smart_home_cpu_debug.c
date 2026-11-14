/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 线程/CPU 归属调试实现。
 *
 * 单核 M55：只报告线程身份。若以后启用 SMP 或把部分任务挪到 M33，
 * 在这里补 rt_thread_self()->oncpu 之类的实现即可，调用点不动。
 */

#include <rtthread.h>

#include "smart_home/core/smart_home_cpu_debug.h"

#define DBG_TAG "sca.cpu"

void smart_home_cpu_debug_log(const char *role)
{
#if SCA_ENABLE_DEBUG_LOG
    rt_thread_t self = rt_thread_self();

    if (self == RT_NULL)
    {
        SCA_LOGI(DBG_TAG, "%s: no thread context", role ? role : "?");
        return;
    }

    SCA_LOGI(DBG_TAG, "%s: thread=%s prio=%u",
             role ? role : "?", self->name, (unsigned)self->current_priority);
#else
    (void)role;
#endif
}
