/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * OS 抽象层。
 *
 * 业务层（smart_home/ 下所有文件）只允许通过本文件使用线程、互斥、
 * 信号量、时钟。不允许直接 include <rtthread.h>、<pthread.h>。
 *
 * ★ 为什么互斥量/信号量是 POD 内嵌 ★
 *   业务结构体按值内嵌锁（如 smart_home_device_service_t.mutex），
 *   避免不透明指针带来的 create/destroy、malloc/free 与失败路径。
 *
 *   因此这里给出 POD 版本：结构体内嵌一块足以容纳底层 rt_mutex /
 *   rt_semaphore 的对齐字节区，接口是 init/destroy 而非 create/free：
 *       sca_mutex_t mutex;
 *       sca_mutex_init(&mutex, "name");
 *   其余 lock / trylock / unlock / destroy 语义与 POSIX 一致。
 *
 *   静态大小断言在 sca_os_rtthread.c 里：若 RT-Thread 的 rt_mutex 增长
 *   超出预留区，编译期直接失败，不会静默越界。
 */

#ifndef __SCA_OS_H__
#define __SCA_OS_H__

#include "port/sca_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 返回值约定 ─────────────────────────────────────────── */
#define SCA_OK              0
#define SCA_ERR            (-1)
#define SCA_ERR_PARAM      (-2)
#define SCA_ERR_NOMEM      (-3)
#define SCA_ERR_TIMEOUT    (-4)
#define SCA_ERR_BUSY       (-5)
#define SCA_ERR_IO         (-6)
#define SCA_ERR_NOT_FOUND  (-7)
#define SCA_ERR_EXISTS     (-8)
#define SCA_ERR_STATE      (-9)
#define SCA_ERR_NOSPC      (-10)
#define SCA_ERR_CANCELLED  (-11)
#define SCA_ERR_NOT_SUP    (-12)

/* ── 时间 ──────────────────────────────────────────────── */

/** 单调时钟，毫秒。用于超时计算，不受系统时间调整影响。 */
uint64_t sca_time_ms(void);

/** 睡眠指定毫秒。 */
void sca_sleep_ms(uint32_t ms);

/* ── 互斥量（POD，按值内嵌）────────────────────────────── */

/* 底层 rt_mutex 在 Cortex-M(32 位) 上是 52 字节：
 *   rt_ipc_object(28) + 4 个 u8(4) + owner(4) + taken_list(16)
 * 预留 14 字（56 字节），留 4 字节余量。sca_os_rtthread.c 里有
 * 静态断言，RT-Thread 若改动结构会在编译期失败而不是运行期踩内存。 */
#define SCA_MUTEX_OPAQUE_WORDS   14

typedef struct sca_mutex
{
    void    *opaque[SCA_MUTEX_OPAQUE_WORDS];
    uint32_t magic;                 /* 初始化标记 */
} sca_mutex_t;

/**
 * 初始化一个就地互斥量。
 * @param name 调试名，可为 NULL
 * @return SCA_OK 成功
 */
int  sca_mutex_init(sca_mutex_t *m, const char *name);
void sca_mutex_destroy(sca_mutex_t *m);
int  sca_mutex_lock(sca_mutex_t *m);
/** @return SCA_OK 拿到锁；SCA_ERR_BUSY 锁被占用 */
int  sca_mutex_trylock(sca_mutex_t *m);
void sca_mutex_unlock(sca_mutex_t *m);
/** 是否已初始化（防止未 init 就 lock） */
int  sca_mutex_ready(const sca_mutex_t *m);

#define SCA_MUTEX_INITIALIZER { { 0 }, 0 }

/* ── 信号量（POD，按值内嵌）────────────────────────────── */

/* rt_semaphore 是 40 字节，预留 12 个字（48 字节）。 */
#define SCA_SEM_OPAQUE_WORDS     12

typedef struct sca_sem
{
    void    *opaque[SCA_SEM_OPAQUE_WORDS];
    uint32_t magic;
} sca_sem_t;

int  sca_sem_init(sca_sem_t *s, const char *name, uint32_t initial);
void sca_sem_destroy(sca_sem_t *s);
/** 阻塞等待，timeout_ms 为 0 表示永久等待。 */
int  sca_sem_wait(sca_sem_t *s, uint32_t timeout_ms);
int  sca_sem_trywait(sca_sem_t *s);
void sca_sem_post(sca_sem_t *s);

/* ── 条件变量（POD，按值内嵌）────────────────────────────
 * 业务层只有 reminder 用到条件变量（3 处），它是"等待外部
 * 事件 + 可被广播唤醒"这一模式的正确工具，换成信号量会丢失唤醒语义。
 * RT-Thread 没有 condvar，这里用"信号量 + 计数器"合成。
 * ──────────────────────────────────────────────────────── */

#define SCA_COND_OPAQUE_WORDS    16

typedef struct sca_cond
{
    void    *opaque[SCA_COND_OPAQUE_WORDS];
    uint32_t magic;
} sca_cond_t;

int  sca_cond_init(sca_cond_t *c, const char *name);
void sca_cond_destroy(sca_cond_t *c);
/** 等待通知。调用方必须已持有配套的 sca_mutex_t；内部会释放再重新获取。 */
int  sca_cond_wait(sca_cond_t *c, sca_mutex_t *m, uint32_t timeout_ms);
/** 唤醒全部等待者。 */
void sca_cond_broadcast(sca_cond_t *c);
/** 唤醒一个等待者。 */
void sca_cond_signal(sca_cond_t *c);

/* ── 线程（句柄式，生命周期由创建方持有）──────────────── */

typedef void (*sca_thread_fn)(void *arg);

typedef struct
{
    const char *name;
    sca_thread_fn fn;
    void       *arg;
    uint32_t    stack_bytes;
    uint8_t     priority;      /* 数值越小优先级越高（0 最高） */
    /** 绑核：0 = CPU0，1 = CPU1，0xFF = 不指定。单核平台忽略。 */
    uint8_t     cpu;
} sca_thread_attr_t;

typedef struct sca_thread sca_thread_t;

sca_thread_t *sca_thread_create(const sca_thread_attr_t *attr);
/**
 * 等待线程函数返回。
 * @param timeout_ms 0 = 永久
 * @return SCA_OK / SCA_ERR_TIMEOUT
 *
 * 返回 SCA_OK 后必须调用 sca_thread_destroy() 释放包装句柄；
 * 具体实现负责内核线程对象与栈的回收。
 */
int           sca_thread_join(sca_thread_t *t, uint32_t timeout_ms);
/** 强制结束并释放（用于关停路径，正常流程不要用）。 */
void          sca_thread_destroy(sca_thread_t *t);
/** 线程是否还在运行。 */
int           sca_thread_alive(sca_thread_t *t);

/**
 * 创建"即发即忘"的短任务线程（等价于 pthread_detach）。
 *
 * 用于 UI 里那些一次性的落盘/保存任务：不需要 join，结束时自动回收。
 * 比走 sca_thread_create + join + destroy 简单，也避免句柄泄漏。
 *
 * @return SCA_OK 成功
 */
int           sca_thread_spawn_detached(const char *name,
                                        sca_thread_fn fn, void *arg,
                                        uint32_t stack_bytes,
                                        uint8_t priority);

/** 串口触发的线程生命周期自测；返回 SCA_OK 或负数阶段码。 */
int           sca_os_thread_selftest(void);

/* ── 临界区 ────────────────────────────────────────────── */

/** 关中断级别的最小临界区，仅用于几行内存操作。返回原中断状态。 */
uint32_t sca_critical_enter(void);
void     sca_critical_exit(uint32_t state);

/* ── 日志 ──────────────────────────────────────────────── */

void sca_log(int level, const char *tag, const char *fmt, ...);
void sca_log_hexdump(int level, const char *tag, const void *data, size_t len);

#define SCA_LOG_ERROR   0
#define SCA_LOG_WARN    1
#define SCA_LOG_INFO    2
#define SCA_LOG_DEBUG   3

#define SCA_LOGE(tag, ...)  sca_log(SCA_LOG_ERROR, tag, __VA_ARGS__)
#define SCA_LOGW(tag, ...)  sca_log(SCA_LOG_WARN,  tag, __VA_ARGS__)
#define SCA_LOGI(tag, ...)  sca_log(SCA_LOG_INFO,  tag, __VA_ARGS__)
#if SCA_ENABLE_DEBUG_LOG
#define SCA_LOGD(tag, ...)  sca_log(SCA_LOG_DEBUG, tag, __VA_ARGS__)
#else
#define SCA_LOGD(tag, ...)  do { } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __SCA_OS_H__ */
