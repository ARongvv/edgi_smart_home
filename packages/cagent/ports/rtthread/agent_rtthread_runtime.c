/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: MIT
 *
 * cAGENT runtime 端口实现。
 *
 * 单调时钟由 port/sca_os.h 提供（sca_time_ms），因此本文件
 * 不包含任何平台头，也不依赖 RT-Thread 的 tick 频率。
 */

#include "port/sca_os.h"
#include "agent_rtthread_runtime.h"

static uint64_t rtthread_now_ms(void *context)
{
    (void)context;

    /* 契约要求失败时返回 UINT64_MAX（让绝对超时立即失效）。
     * sca_time_ms 不会失败，直接返回即可。 */
    return sca_time_ms();
}

agent_error_t agent_port_rtthread_runtime_init(agent_runtime_t *out)
{
    if (out == NULL)
    {
        return AGENT_ERROR_INVALID;
    }

    *out = (agent_runtime_t){0};
    out->now_ms        = rtthread_now_ms;
    out->clock_context = NULL;
    return AGENT_OK;
}
