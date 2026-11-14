/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: MIT
 *
 * cAGENT runtime 端口（RT-Thread 后端）。
 *
 * 上游 ports/runtime 实现只依赖 clock_gettime(CLOCK_MONOTONIC)，
 * 在 RT-Thread 上可以直接工作；本文件存在的意义是提供一个
 * 与 openvela 版本对称的入口，并允许未来插入 tick 补偿逻辑。
 */

#pragma once

#include <agent/runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 初始化 runtime 端口。
 *
 * 填写 out->now_ms 与 out->clock_context。RT-Thread 的时钟由
 * port/sca_os.h 的 sca_time_ms() 提供，因此本端口不含平台代码。
 *
 * @param out 输出
 * @return AGENT_OK 成功
 */
agent_error_t agent_port_rtthread_runtime_init(agent_runtime_t *out);

#ifdef __cplusplus
}
#endif
