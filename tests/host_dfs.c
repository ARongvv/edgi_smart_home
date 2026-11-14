/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 宿主 DFS 实现。
 *
 * 只在 PC 上跑单元测试时编译（见 tests/SConscript）。
 * 把 port/sca_fs.c 依赖的 DFS 接口映射到标准 libc，
 * 使 sca_fs 的逻辑可以在没有 RT-Thread 的情况下被验证。
 *
 * 注意：这里刻意不复用 RT-Thread 的行为细节（如短写、fsync 语义），
 * 目标是验证 sca_fs 自身的逻辑（原子写、目录遍历、容量检查），
 * 而不是模拟 littlefs。
 */

#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif
#include <stdlib.h>
#include <string.h>

#include <dfs_posix.h>

/* ★ 关键：dfs_posix.h 把 open/read/rename 等定义成宏，转发到
 * sca_host_*。本文件就是这些 sca_host_* 的实现，如果宏保持生效，
 * 函数体里的 rename() 会展开成 sca_host_rename() 自己，形成无限
 * 递归 → 栈溢出（0xC0000409）。
 *
 * 因此在实现之前必须把全部转发宏 #undef 掉。 */
#undef open
#undef close
#undef read
#undef write
#undef lseek
#undef fsync
#undef remove
#undef rename
#undef opendir
#undef readdir
#undef closedir
#undef stat
#undef lstat

#define SCA_HOST_MAX_FD   64

static FILE *g_files[SCA_HOST_MAX_FD];

static int fd_alloc(FILE *fp)
{
    for (int i = 0; i < SCA_HOST_MAX_FD; i++)
    {
        if (g_files[i] == NULL)
        {
            g_files[i] = fp;
            return i;
        }
    }
    return -1;
}

int sca_host_open(const char *path, int flags)
{
    const char *mode;
    FILE *fp;
    int fd;

    if (flags & O_RDWR)
    {
        mode = (flags & O_CREAT) ? "r+b" : "rb";
    }
    else if (flags & O_WRONLY)
    {
        mode = (flags & O_APPEND) ? "ab" : "wb";
    }
    else
    {
        mode = "rb";
    }

    fp = fopen(path, mode);
    if (fp == NULL && (flags & O_CREAT))
    {
        /* r+b 在文件不存在时失败，退回 w+b 建新文件 */
        fp = fopen(path, "w+b");
    }
    if (fp == NULL)
    {
        return -1;
    }

    fd = fd_alloc(fp);
    if (fd < 0)
    {
        fclose(fp);
        return -1;
    }
    return fd;
}

int sca_host_close(int fd)
{
    if (fd < 0 || fd >= SCA_HOST_MAX_FD || g_files[fd] == NULL)
    {
        return -1;
    }
    fclose(g_files[fd]);
    g_files[fd] = NULL;
    return 0;
}

long sca_host_read(int fd, void *buf, size_t n)
{
    if (fd < 0 || fd >= SCA_HOST_MAX_FD || g_files[fd] == NULL)
    {
        return -1;
    }
    return (long)fread(buf, 1, n, g_files[fd]);
}

long sca_host_write(int fd, const void *buf, size_t n)
{
    if (fd < 0 || fd >= SCA_HOST_MAX_FD || g_files[fd] == NULL)
    {
        return -1;
    }
    return (long)fwrite(buf, 1, n, g_files[fd]);
}

long sca_host_lseek(int fd, long off, int whence)
{
    if (fd < 0 || fd >= SCA_HOST_MAX_FD || g_files[fd] == NULL)
    {
        return -1;
    }
    return (long)fseek(g_files[fd], off, whence);
}

int sca_host_fsync(int fd)
{
    if (fd < 0 || fd >= SCA_HOST_MAX_FD || g_files[fd] == NULL)
    {
        return -1;
    }
    return fflush(g_files[fd]);
}

int sca_host_remove(const char *path)
{
    return remove(path);
}

int sca_host_rename(const char *from, const char *to)
{
    /* POSIX rename() 语义：目标存在时原子替换。
     * Windows 的 rename() 在目标存在时直接失败，必须先删目标 ——
     * 这一步不原子，但宿主测试不考察掉电一致性，可接受。
     * 真实目标平台（RT-Thread + littlefs）的 rename 是原子的。 */
#ifdef _WIN32
    if (GetFileAttributesA(to) != INVALID_FILE_ATTRIBUTES)
    {
        if (!DeleteFileA(to))
        {
            return -1;
        }
    }
#endif
    return rename(from, to);
}

int sca_host_mkdir(const char *path)
{
#ifdef _WIN32
    /* tcc 的 <direct.h> 提供 _mkdir */
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

DIR *sca_host_opendir(const char *path)
{
    /* 用一个极简 DIR：把目录项一次性读进数组。
     * 目录很深时会有一次性内存开销，测试场景可接受。 */
    DIR *d = (DIR *)calloc(1, sizeof(DIR));
    if (d == NULL)
    {
        return NULL;
    }

    /* Windows 无 dirent，用系统命令列出文件名太脏；
     * 这里用一个约定：测试只列平铺目录，走 opendir 的简化实现。 */
    d->fd  = 0;
    d->dir = NULL;
    return d;
}

struct dirent *sca_host_readdir(DIR *d)
{
    /* 宿主侧目录遍历不做真实实现：sca_fs_list 的验证改由
     * 直接构造临时目录 + 读回文件的方式完成（见 test_fs.c）。
     * 返回 NULL 表示遍历立即结束。 */
    (void)d;
    return NULL;
}

int sca_host_closedir(DIR *d)
{
    if (d != NULL)
    {
        free(d);
    }
    return 0;
}

int sca_host_stat(const char *path, struct stat *st)
{
#ifdef _WIN32
    return stat(path, st);
#else
    return stat(path, st);
#endif
}

int sca_host_lstat(const char *path, struct stat *st)
{
    return sca_host_stat(path, st);
}
