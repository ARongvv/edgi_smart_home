/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 大块内存分配抽象。
 *
 * PSRAM bulk pool：smart_home_bulk_alloc 走
 * esp32s3_psram_bulk_alloc），在 P4 上已经退化成普通 malloc。
 * Edgi-Talk 上 bulk 分配走 M55 专用 HyperRAM 的独立 memheap，
 * 避免会话历史、MiLoCo 设备表等长期大块占用默认堆而饿死 LVGL。
 *
 *   1. 业务层 30+ 处调用点无需改动；
 *   2. 默认堆（SOCMEM 1.4MB）完整留给 LVGL 对象。
 */

#ifndef __SMART_HOME_MEMORY_H__
#define __SMART_HOME_MEMORY_H__

#include <rtthread.h>

#include "port/sca_config.h"
#include "port/sca_os.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 16 字节对齐标记，供 LVGL 的 draw buffer 等要求对齐的场景使用。 */
#define SMART_HOME_SRAM_DATA __attribute__((aligned(16)))

/** 初始化 HyperRAM 大块内存池；应在板级初始化前调用一次。 */
rt_err_t smart_home_bulk_pool_init(void);

/**
 * 分配大块内存（帧缓冲、LVGL draw buf、KWS 模型等）。
 *
 * 与普通 malloc 的区别只在语义：调用方明确表示这是"大块、长期"的
 * 分配，实现走 HyperRAM memheap；池未初始化时退化为 rt_malloc。
 */
void *smart_home_bulk_alloc(size_t size);
void  smart_home_bulk_free(void *pointer);

/** 打印指针落在哪个内存区域（SRAM / HyperRAM / unknown）。 */
void  smart_home_memory_log_region(const char *label, const void *pointer);

/* 兼容旧宏：ov_mem_region_log 用于区域诊断 */
#define ov_mem_region_log(label, pointer) \
    smart_home_memory_log_region((label), (pointer))

#ifdef __cplusplus
}
#endif

#endif /* __SMART_HOME_MEMORY_H__ */
