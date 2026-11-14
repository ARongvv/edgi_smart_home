/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件系统抽象。
 *
 * 业务层不直接调用 open/read/write，也不拼路径字符串。所有落盘都走
 * 这里，理由有三：
 *
 *   1. 路径前缀集中管理 —— 从 /data 换到 /littlefs 只改 sca_config.h；
 *   2. 原子写统一实现 —— "写 tmp → rename"策略在这里收口；
 *   3. 容量检查集中做 —— 写之前先判剩余空间，避免 LittleFS 写满后
 *      行为不可预期。
 */

#ifndef __SCA_FS_H__
#define __SCA_FS_H__

#include "port/sca_os.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sca_file sca_file_t;

/** 打开文件。mode 用 "r" "w" "a" "rb" "wb" "ab"。 */
sca_file_t *sca_fs_open(const char *path, const char *mode);
int         sca_fs_read(sca_file_t *f, void *buf, size_t bytes);
int         sca_fs_write(sca_file_t *f, const void *buf, size_t bytes);
/** @return SCA_OK / SCA_ERR */
int         sca_fs_seek(sca_file_t *f, long offset, int whence);
long        sca_fs_tell(sca_file_t *f);
int         sca_fs_flush(sca_file_t *f);
void        sca_fs_close(sca_file_t *f);
/** 文件总长度，失败返回 -1 */
long        sca_fs_size(const char *path);
int         sca_fs_exists(const char *path);

/**
 * 整文件读取到堆缓冲。调用方负责 sca_fs_free_buffer()。
 *
 * @param path      文件路径
 * @param out_len   输出：实际长度（可为 NULL）
 * @param max_bytes 上限，超过则拒绝（防止误读超大文件 OOM）
 * @return 缓冲首地址；失败 NULL
 */
void *sca_fs_read_all(const char *path, size_t *out_len, size_t max_bytes);

void sca_fs_free_buffer(void *buffer);

/**
 * 原子写整个文件。
 *
 * 实现：写 <path>.tmp → fsync → close → rename(path.tmp, path)。
 * 中途掉电时旧文件保持完整，新文件丢弃。
 *
 * @return SCA_OK 成功
 */
int sca_fs_write_atomic(const char *path, const void *data, size_t bytes);

/** 递归创建目录（mkdir -p）。路径必须绝对。 */
int sca_fs_mkdir_p(const char *path);

/** 删除文件。不存在不算错误。 */
int sca_fs_remove(const char *path);

/** 已挂载分区的剩余字节数；失败返回 -1。 */
long sca_fs_free_bytes(const char *path);

/**
 * 列出目录下匹配后缀的文件名。
 *
 * 回调式遍历，避免业务层持有 DIR 句柄。
 * @param path    目录
 * @param suffix  只回调以该后缀结尾的文件；NULL 表示全部
 * @param visit   回调，返回非 0 中止遍历
 * @param user    透传
 * @return 回调次数；负数表示目录打不开
 */
int sca_fs_list(const char *path, const char *suffix,
                int (*visit)(const char *name, void *user), void *user);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_FS_H__ */
