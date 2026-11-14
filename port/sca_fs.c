/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 文件系统抽象实现（基于 RT-Thread DFS + POSIX 接口）。
 *
 * 注意 rename 的原子性：LittleFS 保证同目录内 rename 是原子的，
 * 因此 .tmp 必须与目标同目录。本文件所有原子写都遵循这一点。
 */

#include <rtthread.h>

/* RT-Thread 5.0 的 DFS 有两套目录：
 *   dfs_v1  —— 头是 <dfs_file.h> / <dfs_fs.h>，POSIX 函数（open/read/
 *              write）在 dfs_posix.c 里实现，但声明只出现在
 *              libc/compilers/common/extension/fcntl/octal/fcntl.h
 *   dfs_v2  —— 才有 <dfs_posix.h>
 *
 * 本工程用 v1（CONFIG 里 RT_USING_DFS_V1），所以不能 include
 * dfs_posix.h。open() 的声明由 <fcntl.h> 提供 —— 它经 SConscript
 * 的 CPPPATH 指向 RT-Thread 的 extension/fcntl/octal 版本。 */
#include <dfs_file.h>
#include <dfs_fs.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <errno.h>

#include "port/sca_fs.h"

#define DBG_TAG "sca.fs"

/* mkdir 的签名在两个平台不同：RT-Thread DFS 是 mkdir(path, mode)，
 * 宿主测试桩是 sca_host_mkdir(path)。统一到一个宏后面，业务代码不感知。 */
#if defined(RT_USING_DFS)
#define sca_mkdir_one(path)   mkdir((path), 0)
#else
/* 宿主测试桩走 sca_host_mkdir，避免与 windows.h 的 mkdir 冲突 */
#define sca_mkdir_one(path)   sca_host_mkdir(path)
#endif

struct sca_file
{
    int fd;
};

sca_file_t *sca_fs_open(const char *path, const char *mode)
{
    sca_file_t *f;
    int flags = 0;

    if (path == RT_NULL || mode == RT_NULL)
    {
        return RT_NULL;
    }

    /* 把 stdio 模式翻成 open 标志。
     * 不用 fopen：RT-Thread 的 fopen 依赖 newlib 且缓冲行为不一致。 */
    if (mode[0] == 'r')
    {
        flags = O_RDONLY;
        if (mode[1] == '+')
        {
            flags = O_RDWR;
        }
    }
    else if (mode[0] == 'w')
    {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (mode[1] == '+')
        {
            flags = O_RDWR | O_CREAT | O_TRUNC;
        }
    }
    else if (mode[0] == 'a')
    {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        if (mode[1] == '+')
        {
            flags = O_RDWR | O_CREAT | O_APPEND;
        }
    }
    else
    {
        return RT_NULL;
    }

    f = rt_malloc(sizeof(*f));
    if (f == RT_NULL)
    {
        return RT_NULL;
    }

    f->fd = open(path, flags, 0666);
    if (f->fd < 0)
    {
        rt_free(f);
        return RT_NULL;
    }

    return f;
}

int sca_fs_read(sca_file_t *f, void *buf, size_t bytes)
{
    ssize_t got;

    if (f == RT_NULL || f->fd < 0 || buf == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    got = read(f->fd, buf, bytes);
    if (got < 0)
    {
        return SCA_ERR_IO;
    }
    return (int)got;
}

int sca_fs_write(sca_file_t *f, const void *buf, size_t bytes)
{
    ssize_t put;

    if (f == RT_NULL || f->fd < 0 || buf == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    put = write(f->fd, buf, bytes);
    if (put < 0)
    {
        return SCA_ERR_IO;
    }
    /* 短写：LittleFS 可能只写一部分，调用方需自行循环。
     * 这里如实返回写入量，由上层判断。 */
    return (int)put;
}

int sca_fs_seek(sca_file_t *f, long offset, int whence)
{
    if (f == RT_NULL || f->fd < 0)
    {
        return SCA_ERR_PARAM;
    }
    return (lseek(f->fd, offset, whence) >= 0) ? SCA_OK : SCA_ERR_IO;
}

long sca_fs_tell(sca_file_t *f)
{
    if (f == RT_NULL || f->fd < 0)
    {
        return -1;
    }
    return (long)lseek(f->fd, 0, SEEK_CUR);
}

int sca_fs_flush(sca_file_t *f)
{
    if (f == RT_NULL || f->fd < 0)
    {
        return SCA_ERR_PARAM;
    }
    return (fsync(f->fd) == 0) ? SCA_OK : SCA_ERR_IO;
}

void sca_fs_close(sca_file_t *f)
{
    if (f == RT_NULL)
    {
        return;
    }
    if (f->fd >= 0)
    {
        close(f->fd);
    }
    rt_free(f);
}

long sca_fs_size(const char *path)
{
    struct stat st;

    if (path == RT_NULL || stat(path, &st) != 0)
    {
        return -1;
    }
    return (long)st.st_size;
}

int sca_fs_exists(const char *path)
{
    struct stat st;

    if (path == RT_NULL)
    {
        return 0;
    }
    return stat(path, &st) == 0;
}

void *sca_fs_read_all(const char *path, size_t *out_len, size_t max_bytes)
{
    sca_file_t *f;
    long size;
    void *buffer;
    int got;
    size_t offset = 0;

    if (out_len)
    {
        *out_len = 0;
    }

    size = sca_fs_size(path);
    if (size <= 0)
    {
        return RT_NULL;
    }
    if (max_bytes != 0 && (size_t)size > max_bytes)
    {
        SCA_LOGW(DBG_TAG, "%s too large (%ld > %u), refused",
                 path, size, (unsigned)max_bytes);
        return RT_NULL;
    }

    /* +1 字节用于调用方可能的字符串终止 */
    buffer = rt_malloc((size_t)size + 1u);
    if (buffer == RT_NULL)
    {
        return RT_NULL;
    }

    f = sca_fs_open(path, "rb");
    if (f == RT_NULL)
    {
        rt_free(buffer);
        return RT_NULL;
    }

    while (offset < (size_t)size)
    {
        got = sca_fs_read(f, (uint8_t *)buffer + offset,
                         (size_t)size - offset);
        if (got <= 0)
        {
            break;
        }
        offset += (size_t)got;
    }

    sca_fs_close(f);

    if (offset != (size_t)size)
    {
        SCA_LOGW(DBG_TAG, "short read %s: %u/%ld",
                 path, (unsigned)offset, size);
        rt_free(buffer);
        return RT_NULL;
    }

    ((char *)buffer)[size] = '\0';
    if (out_len)
    {
        *out_len = (size_t)size;
    }
    return buffer;
}

void sca_fs_free_buffer(void *buffer)
{
    if (buffer)
    {
        rt_free(buffer);
    }
}

int sca_fs_write_atomic(const char *path, const void *data, size_t bytes)
{
    char tmp_path[160];
    sca_file_t *f;
    size_t offset = 0;
    int put;
    int path_len;

    if (path == RT_NULL || (data == RT_NULL && bytes > 0))
    {
        return SCA_ERR_PARAM;
    }

    path_len = rt_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (path_len < 0 || (size_t)path_len >= sizeof(tmp_path))
    {
        return SCA_ERR_PARAM;
    }

    f = sca_fs_open(tmp_path, "wb");
    if (f == RT_NULL)
    {
        SCA_LOGE(DBG_TAG, "open %s for write failed", tmp_path);
        return SCA_ERR_IO;
    }

    while (offset < bytes)
    {
        put = sca_fs_write(f, (const uint8_t *)data + offset, bytes - offset);
        if (put <= 0)
        {
            SCA_LOGE(DBG_TAG, "write %s failed at %u/%u",
                     tmp_path, (unsigned)offset, (unsigned)bytes);
            sca_fs_close(f);
            remove(tmp_path);
            return SCA_ERR_IO;
        }
        offset += (size_t)put;
    }

    /* fsync 再 rename：保证 rename 之前数据已落盘 */
    sca_fs_flush(f);
    sca_fs_close(f);

    if (rename(tmp_path, path) != 0)
    {
        SCA_LOGE(DBG_TAG, "rename %s -> %s failed", tmp_path, path);
        remove(tmp_path);
        return SCA_ERR_IO;
    }

    return SCA_OK;
}

static int sca_mkdir_if_needed(const char *path)
{
    struct stat info;

    if (stat(path, &info) == 0 && S_ISDIR(info.st_mode))
        return SCA_OK;
    if (sca_mkdir_one(path) == 0)
        return SCA_OK;
    if (stat(path, &info) == 0 && S_ISDIR(info.st_mode))
        return SCA_OK;

    SCA_LOGW(DBG_TAG, "cannot create directory %s", path);
    return SCA_ERR_IO;
}

int sca_fs_mkdir_p(const char *path)
{
    char buffer[160];
    size_t len;
    size_t i;

    if (path == RT_NULL || path[0] != '/')
    {
        return SCA_ERR_PARAM;
    }

    len = rt_strlen(path);
    if (len >= sizeof(buffer))
    {
        return SCA_ERR_PARAM;
    }

    rt_memcpy(buffer, path, len + 1);

    for (i = 1; i < len; i++)
    {
        if (buffer[i] == '/')
        {
            buffer[i] = '\0';
            if (sca_mkdir_if_needed(buffer) != SCA_OK)
                return SCA_ERR_IO;
            buffer[i] = '/';
        }
    }

    return sca_mkdir_if_needed(buffer);
}

int sca_fs_remove(const char *path)
{
    if (path == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }
    if (remove(path) != 0)
    {
        int err = rt_get_errno();
        if (err == ENOENT)
        {
            return SCA_OK;
        }
        return SCA_ERR_IO;
    }
    return SCA_OK;
}

long sca_fs_free_bytes(const char *path)
{
    struct statfs st;

    if (path == RT_NULL || dfs_statfs(path, &st) != 0)
    {
        return -1;
    }
    return (long)st.f_bfree * (long)st.f_bsize;
}

int sca_fs_list(const char *path, const char *suffix,
                int (*visit)(const char *name, void *user), void *user)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    size_t suffix_len = 0;

    if (path == RT_NULL || visit == RT_NULL)
    {
        return SCA_ERR_PARAM;
    }

    if (suffix)
    {
        suffix_len = rt_strlen(suffix);
    }

    dir = opendir(path);
    if (dir == RT_NULL)
    {
        return SCA_ERR_NOT_FOUND;
    }

    while ((entry = readdir(dir)) != RT_NULL)
    {
        size_t name_len;

        if (entry->d_name[0] == '.')
        {
            continue;
        }
        if (entry->d_type == DT_DIR)
        {
            continue;
        }

        if (suffix_len > 0)
        {
            name_len = rt_strlen(entry->d_name);
            if (name_len <= suffix_len)
            {
                continue;
            }
            if (rt_strcmp(entry->d_name + name_len - suffix_len, suffix) != 0)
            {
                continue;
            }
        }

        count++;
        if (visit(entry->d_name, user) != 0)
        {
            break;
        }
    }

    closedir(dir);
    return count;
}
