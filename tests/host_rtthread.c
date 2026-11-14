/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * RT-Thread 内核 API 的宿主实现（单线程桩）。
 *
 * 目的：让 port/sca_os_rtthread.c 能在 PC 上编译并运行测试。
 *
 * 限制（有意为之）：
 *   - 所有 rt_mutex / rt_sem 都是空操作，因为测试是单线程的；
 *   - rt_thread_* 不创建真实线程；
 *   - rt_tick_get 用单调时钟模拟。
 *
 * 这些限制的含义：宿主测试能验证 sca_fs 的逻辑与协议解析，
 * 但**不能**验证并发正确性 —— 那必须上板或用 RT-Thread 的 sim BSP。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>

#include <rtthread.h>

/* ── 时间 ──────────────────────────────────────────────── */

rt_tick_t rt_tick_get(void)
{
    /* 用标准 time() 而非 clock_gettime：MinGW/tcc 的 CLOCK_MONOTONIC
     * 定义不一致，宿主侧精度要求也不高（毫秒级够用）。 */
    static time_t start;
    time_t now;

    if (start == 0)
    {
        start = time(NULL);
        if (start == 0) start = 1;      /* 0 作为"未初始化"标记 */
    }
    now = time(NULL);

    return (rt_tick_t)((now - start) * 1000L);
}

rt_tick_t rt_tick_from_millisecond(rt_int32_t ms)
{
    return (rt_tick_t)ms;
}

void rt_thread_mdelay(rt_int32_t ms)
{
    /* 宿主测试里不真的睡，避免浪费时间 */
    (void)ms;
}

void rt_thread_yield(void)
{
}

/* ── 互斥量（空操作）───────────────────────────────────── */

rt_err_t rt_mutex_init(struct rt_mutex *m, const char *name, int flag)
{
    (void)name; (void)flag;
    return m ? RT_EOK : -RT_EINVAL;
}

rt_err_t rt_mutex_detach(struct rt_mutex *m)
{
    (void)m;
    return RT_EOK;
}

rt_err_t rt_mutex_take(struct rt_mutex *m, rt_int32_t timeout)
{
    (void)timeout;
    return m ? RT_EOK : -RT_EINVAL;
}

rt_err_t rt_mutex_release(struct rt_mutex *m)
{
    (void)m;
    return RT_EOK;
}

/* ── 信号量（空操作）───────────────────────────────────── */

rt_err_t rt_sem_init(struct rt_semaphore *s, const char *name,
                     unsigned value, int flag)
{
    (void)name; (void)flag;
    if (!s) return -RT_EINVAL;
    s->value = (unsigned short)value;
    return RT_EOK;
}

rt_err_t rt_sem_detach(struct rt_semaphore *s)
{
    (void)s;
    return RT_EOK;
}

rt_err_t rt_sem_take(struct rt_semaphore *s, rt_int32_t timeout)
{
    (void)timeout;
    if (!s) return -RT_EINVAL;
    if (s->value > 0) { s->value--; return RT_EOK; }
    return -RT_ETIMEOUT;
}

rt_err_t rt_sem_release(struct rt_semaphore *s)
{
    if (!s) return -RT_EINVAL;
    s->value++;
    return RT_EOK;
}

/* ── 线程（不创建真实线程）─────────────────────────────── */

rt_thread_t rt_thread_create(const char *name, void (*entry)(void *),
                             void *param, rt_uint32_t stack_size,
                             rt_uint8_t priority, rt_uint32_t tick)
{
    static struct rt_thread t;
    (void)entry; (void)param; (void)stack_size; (void)priority; (void)tick;
    strncpy(t.name, name ? name : "host", sizeof(t.name) - 1);
    return &t;
}

rt_err_t rt_thread_init(struct rt_thread *t, const char *name,
                        void (*entry)(void *), void *param,
                        void *stack, rt_uint32_t stack_size,
                        rt_uint8_t priority, rt_uint32_t tick)
{
    (void)entry; (void)param; (void)stack;
    (void)stack_size; (void)priority; (void)tick;
    if (!t) return -RT_EINVAL;
    strncpy(t->name, name ? name : "host", sizeof(t->name) - 1);
    return RT_EOK;
}

rt_err_t rt_thread_startup(struct rt_thread *t) { (void)t; return RT_EOK; }
rt_err_t rt_thread_detach(struct rt_thread *t)  { (void)t; return RT_EOK; }
rt_err_t rt_thread_delete(struct rt_thread *t)  { (void)t; return RT_EOK; }

rt_thread_t rt_thread_find(char *name)
{
    (void)name;
    return NULL;                 /* 宿主上"线程"立即结束 */
}

rt_err_t rt_thread_control(struct rt_thread *t, int cmd, void *arg)
{
    (void)t; (void)cmd; (void)arg;
    return RT_EOK;
}

/* ── 内存与字符串 ──────────────────────────────────────── */

void *rt_malloc(size_t n)              { return malloc(n); }
void  rt_free(void *p)                 { free(p); }
void  rt_memset(void *p, int c, size_t n) { memset(p, c, n); }
void  rt_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
char *rt_strcpy(char *d, const char *s)  { return strcpy(d, s); }
char *rt_strncpy(char *d, const char *s, size_t n) { return strncpy(d, s, n); }
char *rt_strcat(char *d, const char *s)  { return strcat(d, s); }
int   rt_strcmp(const char *a, const char *b) { return strcmp(a, b); }
int   rt_strncmp(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
int   rt_strncasecmp(const char *a, const char *b, size_t n)
{
    while (n--)
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        if (!ca) return 0;
        a++; b++;
    }
    return 0;
}
size_t rt_strlen(const char *s)          { return strlen(s); }

int rt_snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return ret;
}

int rt_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    return vsnprintf(buf, n, fmt, ap);
}

int rt_vprintf(const char *fmt, va_list ap)
{
    return vprintf(fmt, ap);
}

int rt_kprintf(const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

/* ── 中断与 errno ──────────────────────────────────────── */

uint32_t rt_hw_interrupt_disable(void) { return 0; }
void     rt_hw_interrupt_enable(uint32_t s) { (void)s; }

int rt_get_errno(void) { return errno; }
void rt_set_errno(int e) { errno = e; }
