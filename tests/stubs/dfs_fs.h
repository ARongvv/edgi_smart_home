/* 宿主测试桩：dfs_fs.h */
#ifndef __STUB_DFS_FS_H__
#define __STUB_DFS_FS_H__
#include <sys/statfs.h>

/* 宿主侧不挂载文件系统，返回失败让 sca_fs_free_bytes 返回 -1。
 * 测试改成直接检查文件读写而非容量。 */
int dfs_statfs(const char *path, struct statfs *st)
{
    (void)path; (void)st;
    return -1;
}

int dfs_mount(const char *dev, const char *mp, const char *fs,
                     unsigned long flags, const void *data)
{
    (void)dev; (void)mp; (void)fs; (void)flags; (void)data;
    return -1;
}

int dfs_mkfs(const char *fs, const char *dev)
{
    (void)fs; (void)dev;
    return -1;
}
#endif
