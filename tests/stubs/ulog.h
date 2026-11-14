/* 宿主测试桩：ulog.h */
#ifndef __STUB_ULOG_H__
#define __STUB_ULOG_H__
#include <rtthread.h>
#define ulog_e(tag, ...) rt_kprintf("[E/%s] " __VA_ARGS__)
#define ulog_w(tag, ...) rt_kprintf("[W/%s] " __VA_ARGS__)
#define ulog_i(tag, ...) rt_kprintf("[I/%s] " __VA_ARGS__)
#define ulog_d(tag, ...) rt_kprintf("[D/%s] " __VA_ARGS__)
#endif
