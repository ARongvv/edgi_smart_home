/*
 * 宿主测试桩：rtthread.h
 *
 * 只在 PC 上跑单测时使用，让 port/ 层能在没有 RT-Thread 源码的情况下
 * 编译。不提供任何真实行为。
 */
#ifndef __STUB_RTTHREAD_H__
#define __STUB_RTTHREAD_H__

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RT_EOK              0
#define RT_ERROR            1
#define RT_EINVAL           22
#define RT_ENOMEM           12
#define RT_EIO              5
#define RT_TIMEOUT          146
#define RT_ETIMEOUT         146
#define RT_EINTR            4
#define RT_EBUSY            16
#define RT_ENOSPC           28
#define RT_ENOENT           2
#define RT_ENAMETOOLONG     36

#define RT_NULL             0
#define RT_TRUE             1
#define RT_FALSE            0

#define RT_WAITING_FOREVER  (-1)

typedef int              rt_err_t;
typedef long             rt_off_t;
typedef long             rt_ssize_t;
typedef long             rt_ssize_t;
typedef void *           rt_thread_t;
typedef unsigned long    rt_tick_t;
typedef unsigned char    rt_uint8_t;
typedef unsigned short   rt_uint16_t;
typedef unsigned int     rt_uint32_t;
typedef unsigned long    rt_ubase_t;
typedef signed char      rt_int8_t;
typedef short            rt_int16_t;
typedef int              rt_int32_t;
typedef int              rt_bool_t;
typedef long             rt_base_t;

#define RT_IPC_FLAG_PRIO    0
#define RT_IPC_FLAG_FIFO    1

#define RT_DEVICE_OFLAG_RDONLY  0x001
#define RT_DEVICE_OFLAG_WRONLY  0x002
#define RT_DEVICE_FLAG_RDONLY   0x001

#define RT_THREAD_CTRL_BIND_CPU 0x10

#define RT_TOUCH_EVENT_DOWN     0x01
#define RT_TOUCH_EVENT_UP       0x02
#define RT_TOUCH_EVENT_MOVE     0x03

struct rt_touch_data {
    unsigned char  event;
    unsigned char  track_id;
    unsigned short x_coordinate;
    unsigned short y_coordinate;
    unsigned       timestamp;
    unsigned char  width;
};

struct rt_audio_caps {
    int  main_type;
    int  sub_type;
    union {
        struct { unsigned samplerate; unsigned channels; unsigned samplebits; } config;
    } udata;
};

#define AUDIO_TYPE_OUTPUT   0x01
#define AUDIO_TYPE_INPUT    0x02
#define AUDIO_DSP_PARAM     0x100
#define AUDIO_CTL_CONFIGURE 0x01

#define RT_TICK_PER_SECOND  1000
#define RT_THREAD_PRIORITY_MAX 32

struct rt_thread { char name[16]; int dummy; };
struct rt_mutex { int dummy; };
struct rt_semaphore { unsigned short value; unsigned short reserved; };
struct rt_device { int dummy; };
struct rt_thread_dummy2 { int x; };
typedef struct rt_device *rt_device_t;

rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms);
void      rt_thread_mdelay(rt_int32_t ms);
void      rt_thread_yield(void);
rt_thread_t rt_thread_create(const char *, void (*)(void *), void *,
                             rt_uint32_t, rt_uint8_t, rt_uint32_t);
rt_err_t  rt_thread_init(struct rt_thread *, const char *, void (*)(void *),
                         void *, void *, rt_uint32_t, rt_uint8_t, rt_uint32_t);
rt_err_t  rt_thread_startup(struct rt_thread *);
rt_err_t  rt_thread_detach(struct rt_thread *);
rt_err_t  rt_thread_delete(struct rt_thread *);
rt_thread_t rt_thread_find(char *);
rt_err_t  rt_thread_control(struct rt_thread *, int, void *);

rt_err_t  rt_mutex_init(struct rt_mutex *, const char *, int);
rt_err_t  rt_mutex_detach(struct rt_mutex *);
rt_err_t  rt_mutex_take(struct rt_mutex *, rt_int32_t);
rt_err_t  rt_mutex_release(struct rt_mutex *);

rt_err_t  rt_sem_init(struct rt_semaphore *, const char *, unsigned, int);
rt_err_t  rt_sem_detach(struct rt_semaphore *);
rt_err_t  rt_sem_take(struct rt_semaphore *, rt_int32_t);
rt_err_t  rt_sem_release(struct rt_semaphore *);

void *rt_malloc(size_t);
void  rt_free(void *);
void *rt_memalign(size_t, size_t);
void  rt_memset(void *, int, size_t);
void  rt_memcpy(void *, const void *, size_t);
char *rt_strcpy(char *, const char *);
char *rt_strncpy(char *, const char *, size_t);
char *rt_strcat(char *, const char *);
int    rt_strcmp(const char *, const char *);
int    rt_strncmp(const char *, const char *, size_t);
int    rt_strncasecmp(const char *, const char *, size_t);
size_t rt_strlen(const char *);
int    rt_snprintf(char *, size_t, const char *, ...);
int    rt_vsnprintf(char *, size_t, const char *, va_list);
int    rt_vprintf(const char *, va_list);
int    rt_kprintf(const char *, ...);

uint32_t rt_hw_interrupt_disable(void);
void     rt_hw_interrupt_enable(uint32_t);

int rt_get_errno(void);
void rt_set_errno(int);

#endif
