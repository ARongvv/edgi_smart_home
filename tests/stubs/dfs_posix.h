/* 宿主测试桩：dfs_posix.h
 *
 * 声明 RT-Thread DFS 提供的 POSIX 子集。
 * 在宿主上直接转发到标准 libc（tcc/MinGW 都有）。
 */
#ifndef __STUB_DFS_POSIX_H__
#define __STUB_DFS_POSIX_H__

#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef O_RDONLY
#define O_RDONLY  0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY  0x0001
#endif
#ifndef O_RDWR
#define O_RDWR    0x0002
#endif
#ifndef O_CREAT
#define O_CREAT   0x0040
#endif
#ifndef O_TRUNC
#define O_TRUNC   0x0200
#endif
#ifndef O_APPEND
#define O_APPEND  0x0400
#endif
#ifndef SEEK_SET
#define SEEK_SET  0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR  1
#endif
#ifndef SEEK_END
#define SEEK_END  2
#endif

typedef struct
{
    int   fd;                       /* 宿主用 FILE* 更稳，见实现 */
    void *dir;
} DIR;

struct dirent
{
    unsigned char d_type;
    char          d_name[260];
};

#ifndef DT_DIR
#define DT_DIR 4
#endif
#ifndef DT_REG
#define DT_REG 8
#endif

/* 宿主实现：tests/host_dfs.c 提供 */
int      sca_host_open(const char *path, int flags);
int      sca_host_close(int fd);
long     sca_host_read(int fd, void *buf, size_t n);
long     sca_host_write(int fd, const void *buf, size_t n);
long     sca_host_lseek(int fd, long off, int whence);
int      sca_host_fsync(int fd);
int      sca_host_remove(const char *path);
int      sca_host_rename(const char *from, const char *to);
int      sca_host_mkdir(const char *path);
DIR     *sca_host_opendir(const char *path);
struct dirent *sca_host_readdir(DIR *d);
int      sca_host_closedir(DIR *d);
int      sca_host_stat(const char *path, struct stat *st);
int      sca_host_lstat(const char *path, struct stat *st);

#define open(p, f, ...)   sca_host_open((p), (f))
#define close(fd)         sca_host_close(fd)
#define read(fd, b, n)    sca_host_read((fd), (b), (n))
#define write(fd, b, n)   sca_host_write((fd), (b), (n))
#define lseek(fd, o, w)   sca_host_lseek((fd), (o), (w))
#define fsync(fd)         sca_host_fsync(fd)
#define remove(p)         sca_host_remove(p)
#define rename(a, b)      sca_host_rename((a), (b))
#define opendir(p)        sca_host_opendir(p)
#define readdir(d)        sca_host_readdir(d)
#define closedir(d)       sca_host_closedir(d)
#define stat(p, s)        sca_host_stat((p), (s))
#define lstat(p, s)       sca_host_lstat((p), (s))

#endif
