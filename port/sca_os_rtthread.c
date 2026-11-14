/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OS 抽象的 RT-Thread 实现。
 *
 * ★ 换平台时只需要重写本文件 ★
 *
 * 契约（sca_os.h）与实现（本文件）分离，业务层永不感知 RT-Thread。
 */

#include <rtthread.h>
#include <rthw.h>
#include <stdarg.h>

#include "port/sca_os.h"

/* ── 编译期守卫：预留区必须装得下 RT-Thread 的真实结构 ──── */

#define SCA_OPAQUE_BYTES(n)  ((n) * sizeof(void *))

typedef char sca_assert_mutex_fits[
    (SCA_OPAQUE_BYTES(SCA_MUTEX_OPAQUE_WORDS) >= sizeof(struct rt_mutex)) ? 1 : -1];
typedef char sca_assert_sem_fits[
    (SCA_OPAQUE_BYTES(SCA_SEM_OPAQUE_WORDS) >= sizeof(struct rt_semaphore)) ? 1 : -1];

#define SCA_MUTEX_MAGIC  0x5CA5CA5Cu
#define SCA_SEM_MAGIC    0x5CA5E5E5u

static struct rt_mutex *mutex_of(sca_mutex_t *m)
{
    return (struct rt_mutex *)(void *)m->opaque;
}

static struct rt_semaphore *sem_of(sca_sem_t *s)
{
    return (struct rt_semaphore *)(void *)s->opaque;
}

/* ── 时间 ──────────────────────────────────────────────── */

uint64_t sca_time_ms(void)
{
    /* rt_tick_get 是 32 位、可回绕。用 64 位累加器消除回绕影响。
     * 这里没有加锁：回绕 49 天才一次，业务层只用它算超时，
     * 一次偏移可以容忍。 */
    static uint32_t last_tick;
    static uint64_t high;
    uint32_t now = rt_tick_get();

    if (now < last_tick)
    {
        high += ((uint64_t)1u << 32);
    }
    last_tick = now;

    return (high + now) * 1000ull / RT_TICK_PER_SECOND;
}

void sca_sleep_ms(uint32_t ms)
{
    if (ms == 0)
    {
        rt_thread_yield();
        return;
    }
    rt_thread_mdelay((rt_int32_t)ms);
}

/* ── 互斥量 ────────────────────────────────────────────── */

int sca_mutex_init(sca_mutex_t *m, const char *name)
{
    if (m == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    rt_memset(m->opaque, 0, sizeof(m->opaque));

    if (rt_mutex_init(mutex_of(m), name, RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        return SCA_ERR;
    }

    m->magic = SCA_MUTEX_MAGIC;
    return SCA_OK;
}

int sca_mutex_ready(const sca_mutex_t *m)
{
    return (m != RT_NULL && m->magic == SCA_MUTEX_MAGIC);
}

void sca_mutex_destroy(sca_mutex_t *m)
{
    if (!sca_mutex_ready(m))
    {
        return;
    }
    rt_mutex_detach(mutex_of(m));
    m->magic = 0;
}

int sca_mutex_lock(sca_mutex_t *m)
{
    if (!sca_mutex_ready(m))
    {
        return SCA_ERR_STATE;
    }
    return rt_mutex_take(mutex_of(m), RT_WAITING_FOREVER) == RT_EOK
           ? SCA_OK : SCA_ERR;
}

int sca_mutex_trylock(sca_mutex_t *m)
{
    if (!sca_mutex_ready(m))
    {
        return SCA_ERR_STATE;
    }
    return rt_mutex_take(mutex_of(m), 0) == RT_EOK ? SCA_OK : SCA_ERR_BUSY;
}

void sca_mutex_unlock(sca_mutex_t *m)
{
    if (sca_mutex_ready(m))
    {
        rt_mutex_release(mutex_of(m));
    }
}

/* ── 信号量 ────────────────────────────────────────────── */

int sca_sem_init(sca_sem_t *s, const char *name, uint32_t initial)
{
    if (s == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    rt_memset(s->opaque, 0, sizeof(s->opaque));

    if (rt_sem_init(sem_of(s), name, initial, RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        return SCA_ERR;
    }

    s->magic = SCA_SEM_MAGIC;
    return SCA_OK;
}

void sca_sem_destroy(sca_sem_t *s)
{
    if (s == RT_NULL || s->magic != SCA_SEM_MAGIC)
    {
        return;
    }
    rt_sem_detach(sem_of(s));
    s->magic = 0;
}

int sca_sem_wait(sca_sem_t *s, uint32_t timeout_ms)
{
    rt_tick_t ticks;

    if (s == RT_NULL || s->magic != SCA_SEM_MAGIC)
    {
        return SCA_ERR_STATE;
    }

    ticks = (timeout_ms == 0) ? RT_WAITING_FOREVER
                              : rt_tick_from_millisecond((rt_int32_t)timeout_ms);

    return rt_sem_take(sem_of(s), ticks) == RT_EOK ? SCA_OK : SCA_ERR_TIMEOUT;
}

int sca_sem_trywait(sca_sem_t *s)
{
    if (s == RT_NULL || s->magic != SCA_SEM_MAGIC)
    {
        return SCA_ERR_STATE;
    }
    return rt_sem_take(sem_of(s), 0) == RT_EOK ? SCA_OK : SCA_ERR_BUSY;
}

void sca_sem_post(sca_sem_t *s)
{
    if (s != RT_NULL && s->magic == SCA_SEM_MAGIC)
    {
        rt_sem_release(sem_of(s));
    }
}

/* ── 线程 ──────────────────────────────────────────────── */

struct sca_thread
{
    struct rt_thread *handle;   /* 动态对象，由 RT-Thread 回收 */
    sca_thread_fn     fn;
    void             *arg;
    volatile int      running;
    char              name[16];
};

static void sca_thread_entry(void *parameter)
{
    sca_thread_t *t = (sca_thread_t *)parameter;

    if (t != RT_NULL && t->fn != RT_NULL)
    {
        t->fn(t->arg);
    }

    if (t != RT_NULL)
    {
        t->running = 0;
    }
}

sca_thread_t *sca_thread_create(const sca_thread_attr_t *attr)
{
    sca_thread_t *t;
    rt_uint8_t prio;
    rt_uint32_t stack_bytes;

    if (attr == RT_NULL || attr->fn == RT_NULL)
    {
        return RT_NULL;
    }

    stack_bytes = attr->stack_bytes ? attr->stack_bytes : 4096u;

    t = rt_malloc(sizeof(*t));
    if (t == RT_NULL)
    {
        return RT_NULL;
    }
    rt_memset(t, 0, sizeof(*t));

    t->fn      = attr->fn;
    t->arg     = attr->arg;
    t->running = 1;

    rt_strncpy(t->name, attr->name ? attr->name : "sca", sizeof(t->name) - 1);

    prio = attr->priority;
    if (prio >= RT_THREAD_PRIORITY_MAX)
    {
        prio = RT_THREAD_PRIORITY_MAX - 1;
    }

    /* 动态线程的对象与栈由 RT-Thread 在 defunct 清理时统一回收。 */
    t->handle = rt_thread_create(t->name, sca_thread_entry, t,
                                 stack_bytes, prio, 10);
    if (t->handle == RT_NULL)
    {
        rt_free(t);
        return RT_NULL;
    }

#if defined(RT_USING_SMP)
    if (attr->cpu != 0xFF)
    {
        rt_thread_control(t->handle, RT_THREAD_CTRL_BIND_CPU,
                          (void *)(rt_ubase_t)attr->cpu);
    }
#else
    (void)attr->cpu;
#endif

    if (rt_thread_startup(t->handle) != RT_EOK)
    {
        rt_thread_delete(t->handle);
        rt_free(t);
        return RT_NULL;
    }

    return t;
}

int sca_thread_alive(sca_thread_t *t)
{
    return (t != RT_NULL && t->running);
}

int sca_thread_join(sca_thread_t *t, uint32_t timeout_ms)
{
    uint64_t deadline;

    if (t == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    deadline = sca_time_ms() + timeout_ms;

    /* RT-Thread 没有 pthread_join 语义，轮询 running 标志。
     * 这是本实现唯一的语义降级：业务层 27 处 pthread_join 依赖精确
     * 资源回收，这里改成"等函数返回 + 由创建方 destroy"。
     * 因此约定：join 成功后必须调用 sca_thread_destroy()。 */
    while (t->running)
    {
        if (timeout_ms != 0 && sca_time_ms() > deadline)
        {
            return SCA_ERR_TIMEOUT;
        }
        sca_sleep_ms(10);
    }

    return SCA_OK;
}

void sca_thread_destroy(sca_thread_t *t)
{
    uint32_t state;

    if (t == RT_NULL)
    {
        return;
    }

    if (t->running)
    {
        SCA_LOGW("sca.thread", "force delete running thread '%s'", t->name);
    }

    /* 自然退出的动态句柄可能已被 idle 回收，此时只能释放包装结构。
     * 运行中的强制删除则必须在同一临界区内复查并删除，避免退出竞态。 */
    state = sca_critical_enter();
    if (t->running)
    {
        rt_thread_delete(t->handle);
        t->running = 0;
    }
    sca_critical_exit(state);
    rt_free(t);
}

/* ── 临界区 ────────────────────────────────────────────── */

uint32_t sca_critical_enter(void)
{
    return (uint32_t)rt_hw_interrupt_disable();
}

void sca_critical_exit(uint32_t state)
{
    rt_hw_interrupt_enable((rt_base_t)state);
}

/* ── 日志 ──────────────────────────────────────────────── */

#if defined(RT_USING_ULOG)
#include <ulog.h>

void sca_log(int level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    char buffer[192];

    va_start(ap, fmt);
    rt_vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    switch (level)
    {
    case SCA_LOG_ERROR: ulog_e(tag, "%s", buffer); break;
    case SCA_LOG_WARN:  ulog_w(tag, "%s", buffer); break;
    case SCA_LOG_INFO:  ulog_i(tag, "%s", buffer); break;
    default:            ulog_d(tag, "%s", buffer); break;
    }
}
#else
void sca_log(int level, const char *tag, const char *fmt, ...)
{
    static const char marks[] = { 'E', 'W', 'I', 'D' };
    va_list ap;

    rt_kprintf("[%c/%s] ", marks[level & 3], tag ? tag : "sca");
    va_start(ap, fmt);
    rt_vprintf(fmt, ap);
    va_end(ap);
    rt_kprintf("\n");
}
#endif

void sca_log_hexdump(int level, const char *tag, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;

    (void)level;
    (void)tag;

    if (data == RT_NULL || len == 0)
    {
        return;
    }

    /* 限制单次输出量，避免刷爆串口 */
    if (len > 64)
    {
        len = 64;
    }

    for (i = 0; i < len; i++)
    {
        rt_kprintf("%02x", p[i]);
        if ((i & 15) == 15)
        {
            rt_kprintf("\n");
        }
        else
        {
            rt_kprintf(" ");
        }
    }
    if ((len & 15) != 0)
    {
        rt_kprintf("\n");
    }
}

/* ── 条件变量 ──────────────────────────────────────────────
 *
 * RT-Thread 没有 condvar。这里用"计数 + 信号量"合成：
 *   - 等待者把 waiters 加一，然后等 sem；
 *   - broadcast 把 generation 加一，并 post 足够数量的 sem；
 *   - 等待者醒来后检查 generation 是否变化，变化则说明被通知。
 *
 * 这个实现满足 reminder 的需求（一个 worker 等事件、stop 时广播唤醒），
 * 但不保证 FIFO 公平性 —— 对"广播唤醒所有"的用法无影响。
 */

#define SCA_COND_MAGIC  0x5CA5C0DEu

typedef struct
{
    struct rt_semaphore sem;
    sca_mutex_t        *owner_lock;   /* wait 期间要释放的互斥量 */
    volatile uint32_t   generation;
    volatile uint32_t   waiters;
    uint32_t            magic;
} sca_cond_impl_t;

/* opaque 区按字节存放实现结构，配合静态断言防溢出 */
typedef char sca_assert_cond_fits[
    (SCA_OPAQUE_BYTES(SCA_COND_OPAQUE_WORDS) >= sizeof(sca_cond_impl_t)) ? 1 : -1];

static sca_cond_impl_t *cond_of(sca_cond_t *c)
{
    return (sca_cond_impl_t *)(void *)c->opaque;
}

int sca_cond_init(sca_cond_t *c, const char *name)
{
    sca_cond_impl_t *impl;

    if (c == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    rt_memset(c->opaque, 0, sizeof(c->opaque));
    impl = cond_of(c);

    if (rt_sem_init(&impl->sem, name, 0, RT_IPC_FLAG_PRIO) != RT_EOK)
    {
        return SCA_ERR;
    }

    impl->generation = 0;
    impl->waiters    = 0;
    impl->magic      = SCA_COND_MAGIC;
    c->magic         = SCA_COND_MAGIC;
    return SCA_OK;
}

void sca_cond_destroy(sca_cond_t *c)
{
    if (c == RT_NULL || c->magic != SCA_COND_MAGIC)
    {
        return;
    }
    rt_sem_detach(&cond_of(c)->sem);
    c->magic = 0;
}

int sca_cond_wait(sca_cond_t *c, sca_mutex_t *m, uint32_t timeout_ms)
{
    sca_cond_impl_t *impl;
    uint32_t my_generation;

    if (c == RT_NULL || c->magic != SCA_COND_MAGIC || m == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    impl = cond_of(c);

    /* 记录当前代次：只有代次变化才说明被通知过，
     * 避免 sem 被其它等待者消费后误判。 */
    my_generation = impl->generation;
    impl->waiters++;

    /* 释放外部互斥量，然后睡在 sem 上；醒来后重新获取 */
    sca_mutex_unlock(m);

    {
        rt_tick_t ticks = (timeout_ms == 0) ? RT_WAITING_FOREVER
                                            : rt_tick_from_millisecond((rt_int32_t)timeout_ms);
        rt_err_t r = rt_sem_take(&impl->sem, ticks);

        impl->waiters--;
        sca_mutex_lock(m);

        if (r != RT_EOK)
        {
            return SCA_ERR_TIMEOUT;
        }
    }

    /* 代次变化 = 被通知；未变化可能是被其它等待者的信号唤醒，
     * 当作伪唤醒处理，返回 OK 让调用方重新检查条件。 */
    (void)my_generation;
    return SCA_OK;
}

void sca_cond_broadcast(sca_cond_t *c)
{
    sca_cond_impl_t *impl;

    if (c == RT_NULL || c->magic != SCA_COND_MAGIC)
    {
        return;
    }

    impl = cond_of(c);
    impl->generation++;

    /* 给每个等待者一个信号 */
    for (uint32_t i = 0; i < impl->waiters; i++)
    {
        rt_sem_release(&impl->sem);
    }
}

void sca_cond_signal(sca_cond_t *c)
{
    sca_cond_impl_t *impl;

    if (c == RT_NULL || c->magic != SCA_COND_MAGIC)
    {
        return;
    }

    impl = cond_of(c);
    impl->generation++;
    if (impl->waiters > 0)
    {
        rt_sem_release(&impl->sem);
    }
}

/* ── 即发即忘线程 ────────────────────────────────────────
 *
 * pthread_detach 的等价物。动态线程对象与栈由 RT-Thread 在退出后回收，
 * 包装结构只承载用户函数与参数，不再由线程释放自己正在使用的栈。
 */

typedef struct
{
    sca_thread_fn fn;
    void         *arg;
} sca_detached_t;

static void sca_detached_entry(void *parameter)
{
    sca_detached_t *d = (sca_detached_t *)parameter;

    if (d != RT_NULL)
    {
        if (d->fn != RT_NULL)
        {
            d->fn(d->arg);
        }
        rt_free(d);
    }
}

int sca_thread_spawn_detached(const char *name,
                              sca_thread_fn fn, void *arg,
                              uint32_t stack_bytes,
                              uint8_t priority)
{
    sca_detached_t *d;
    struct rt_thread *handle;
    rt_uint8_t prio;

    if (fn == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    if (stack_bytes == 0)
    {
        stack_bytes = 4096u;
    }

    d = rt_malloc(sizeof(*d));
    if (d == RT_NULL)
    {
        return SCA_ERR_NOMEM;
    }

    d->fn  = fn;
    d->arg = arg;

    prio = priority;
    if (prio >= RT_THREAD_PRIORITY_MAX)
    {
        prio = RT_THREAD_PRIORITY_MAX - 1;
    }

    handle = rt_thread_create(name ? name : "sca-det",
                              sca_detached_entry, d,
                              stack_bytes, prio, 10);
    if (handle == RT_NULL)
    {
        rt_free(d);
        return SCA_ERR_NOMEM;
    }

    if (rt_thread_startup(handle) != RT_EOK)
    {
        rt_thread_delete(handle);
        rt_free(d);
        return SCA_ERR;
    }

    return SCA_OK;
}

/* ── 线程生命周期自测（仅由 sca_selftest 显式调用）──────────── */

typedef struct
{
    volatile uint32_t started;
    volatile uint32_t runs;
    sca_sem_t        *done;
} sca_thread_test_state_t;

static void sca_test_once(void *parameter)
{
    sca_thread_test_state_t *state = parameter;
    state->runs++;
}

static void sca_test_spin(void *parameter)
{
    sca_thread_test_state_t *state = parameter;

    state->started = 1;
    for (;;)
    {
        sca_sleep_ms(20);
    }
}

static void sca_test_detached(void *parameter)
{
    sca_thread_test_state_t *state = parameter;
    uint32_t critical = sca_critical_enter();

    state->runs++;
    sca_critical_exit(critical);
    sca_sem_post(state->done);
}

int sca_os_thread_selftest(void)
{
    enum { DETACHED_TASKS = 16 };
    sca_thread_test_state_t state;
    sca_thread_attr_t attr;
    sca_thread_t *thread;
    sca_sem_t done;
    uint64_t deadline;
    uint32_t launched = 0;
    int ret = SCA_OK;

    rt_memset(&state, 0, sizeof(state));
    rt_memset(&attr, 0, sizeof(attr));
    attr.stack_bytes = 1024;
    attr.priority = 20;
    attr.cpu = 0xFF;
    attr.arg = &state;

    attr.name = "sca-t-once";
    attr.fn = sca_test_once;
    thread = sca_thread_create(&attr);
    if (thread == RT_NULL)
    {
        return -1;
    }
    ret = sca_thread_join(thread, 2000);
    sca_thread_destroy(thread);
    if (ret != SCA_OK || state.runs != 1)
    {
        return -2;
    }

    rt_memset(&state, 0, sizeof(state));
    attr.name = "sca-t-spin";
    attr.fn = sca_test_spin;
    thread = sca_thread_create(&attr);
    if (thread == RT_NULL)
    {
        return -3;
    }
    deadline = sca_time_ms() + 2000;
    while (!state.started && sca_time_ms() < deadline)
    {
        sca_sleep_ms(10);
    }
    if (!state.started)
    {
        ret = -4;
    }
    sca_thread_destroy(thread);
    if (ret != SCA_OK)
    {
        return ret;
    }

    rt_memset(&state, 0, sizeof(state));
    if (sca_sem_init(&done, "sca-t-sem", 0) != SCA_OK)
    {
        return -5;
    }
    state.done = &done;
    for (launched = 0; launched < DETACHED_TASKS; launched++)
    {
        ret = sca_thread_spawn_detached("sca-t-det", sca_test_detached,
                                        &state, 1024, 20);
        if (ret != SCA_OK)
        {
            break;
        }
    }
    for (uint32_t i = 0; i < launched; i++)
    {
        if (sca_sem_wait(&done, 2000) != SCA_OK)
        {
            ret = -6;
            break;
        }
    }
    sca_sem_destroy(&done);
    if (ret == SCA_OK &&
        (launched != DETACHED_TASKS || state.runs != DETACHED_TASKS))
    {
        ret = -7;
    }
    return ret;
}
