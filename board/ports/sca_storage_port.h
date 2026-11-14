/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 存储端口头文件。
 *
 * 职责：把 QSPI NOR 上的文件系统分区挂载到 SCA_ROOT_DIR。
 * 上层只通过 DFS（POSIX 风格 open/read/write）访问，不关心底层是
 * littlefs 还是别的。
 */

#ifndef __SCA_STORAGE_PORT_H__
#define __SCA_STORAGE_PORT_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 挂载数据分区。可重复调用，已挂载时直接返回 RT_EOK。
 *
 * @return RT_EOK 挂载成功（或已挂载）；负数失败。
 */
rt_err_t sca_storage_port_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_STORAGE_PORT_H__ */
