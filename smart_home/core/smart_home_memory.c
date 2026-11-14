/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 大块内存分配实现。
 *
 * Edgi-Talk 的默认 RT-Thread 堆只有 SOCMEM 里的 1.4 MB。会话历史
 * （512 KB 解码缓冲）、MiLoCo 设备表（约 430 KB）、模型请求/响应
 * 缓冲等长期大块分配会把默认堆占满，导致 LVGL 在构建首页时分配
 * 失败并崩溃。
 *
 * 因此这里在 M55 专用 HyperRAM（2 MB @ 0x64200000，链接脚本中
 * 未被任何段占用）上建立独立 memheap，所有 bulk 分配走它，
 * 把默认堆完整留给 LVGL 对象。
 */

#include <rtthread.h>

#include "smart_home/core/smart_home_memory.h"

#define DBG_TAG "sca.mem"

#define SMART_HOME_BULK_HEAP_BASE  0x64200000u
#define SMART_HOME_BULK_HEAP_SIZE  (2u * 1024u * 1024u)

static struct rt_memheap s_bulk_heap;
static rt_bool_t s_bulk_ready = RT_FALSE;

static rt_bool_t bulk_contains(const void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;

    return s_bulk_ready &&
           address >= (uintptr_t)SMART_HOME_BULK_HEAP_BASE &&
           address < (uintptr_t)SMART_HOME_BULK_HEAP_BASE +
                     SMART_HOME_BULK_HEAP_SIZE;
}

rt_err_t smart_home_bulk_pool_init(void)
{
    if (s_bulk_ready)
    {
        return RT_EOK;
    }

    if (rt_memheap_init(&s_bulk_heap, "hyper",
                        (void *)SMART_HOME_BULK_HEAP_BASE,
                        SMART_HOME_BULK_HEAP_SIZE) != RT_EOK)
    {
        SCA_LOGE(DBG_TAG, "bulk pool init failed");
        return -RT_ERROR;
    }

    s_bulk_ready = RT_TRUE;
    SCA_LOGI(DBG_TAG, "bulk pool base=0x%08x size=%u KB",
             (unsigned)SMART_HOME_BULK_HEAP_BASE,
             (unsigned)(SMART_HOME_BULK_HEAP_SIZE / 1024u));
    return RT_EOK;
}

void *smart_home_bulk_alloc(size_t size)
{
    void *pointer;

    if (size == 0)
    {
        return NULL;
    }

    size = RT_ALIGN(size, 16);

    pointer = s_bulk_ready ? rt_memheap_alloc(&s_bulk_heap, size)
                           : rt_malloc(size);
    if (pointer == NULL)
    {
        SCA_LOGE(DBG_TAG, "bulk alloc %u failed", (unsigned)size);
    }
    return pointer;
}

void smart_home_bulk_free(void *pointer)
{
    if (pointer == NULL)
    {
        return;
    }

    if (bulk_contains(pointer))
    {
        rt_memheap_free(pointer);
    }
    else
    {
        rt_free(pointer);
    }
}

void smart_home_memory_log_region(const char *label, const void *pointer)
{
    const char *region;

    if (pointer == NULL)
    {
        region = "null";
    }
    else
    {
        /* RT-Thread 有多个堆时可用 rt_memheap 判断；
         * 当前单堆配置下统一报 heap。 */
        region = "heap";
    }

    SCA_LOGI(DBG_TAG, "%s ptr=%p region=%s",
             label ? label : "pointer", pointer, region);
}
