/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 线程/CPU 归属调试。
 *
 * 通过 sched_getcpu() + sched_getaffinity() 打印当前线程
 * 落在哪个核、允许哪个核集合。Edgi-Talk 的 M55 是单核（另一颗 M33 跑
 * 独立固件，不走同一 OS 调度），因此这里退化为"打印线程名 + 优先级"，
 * 但保留接口，便于将来若启用 M55 侧多核时补充实现。
 */

#ifndef __SMART_HOME_CPU_DEBUG_H__
#define __SMART_HOME_CPU_DEBUG_H__

#include "port/sca_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 打印调用线程的调度信息。role 是调用点标签（"ui" / "agent" / ...）。 */
void smart_home_cpu_debug_log(const char *role);

#ifdef __cplusplus
}
#endif

#endif /* __SMART_HOME_CPU_DEBUG_H__ */
