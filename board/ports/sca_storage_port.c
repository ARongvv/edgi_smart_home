/* SPDX-License-Identifier: Apache-2.0 */
#include <rtdevice.h>
#include <dfs_fs.h>
#include <dfs_romfs.h>
#include <fal.h>
#include <string.h>

#include "port/sca_config.h"
#include "sca_storage_port.h"

#define DBG_TAG "sca.storage"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define SCA_STORAGE_PARTITION "filesystem"

static const struct romfs_dirent root_entries[] = {
    {ROMFS_DIRENT_DIR, SCA_ROOT_DIR + 1, RT_NULL, 0},
};
static const struct romfs_dirent root_dir = {
    ROMFS_DIRENT_DIR, "/", (const rt_uint8_t *)root_entries,
    sizeof(root_entries) / sizeof(root_entries[0]),
};

rt_err_t sca_storage_port_init(void)
{
    rt_device_t dev;
    const char *mounted;

    if (fal_init() <= 0 || fal_partition_find(SCA_STORAGE_PARTITION) == RT_NULL)
    {
        LOG_E("FAL partition '%s' is unavailable", SCA_STORAGE_PARTITION);
        return -RT_ERROR;
    }

    dev = rt_device_find(SCA_STORAGE_PARTITION);
    if (dev == RT_NULL)
        dev = fal_mtd_nor_device_create(SCA_STORAGE_PARTITION);
    if (dev == RT_NULL || dev->type != RT_Device_Class_MTD)
        return -RT_ERROR;

    mounted = dfs_filesystem_get_mounted_path(dev);
    if (mounted != RT_NULL)
        return strcmp(mounted, SCA_ROOT_DIR) == 0 ? RT_EOK : -RT_EBUSY;

    if (dfs_filesystem_lookup("/") == RT_NULL &&
        dfs_mount(RT_NULL, "/", "rom", 0, &root_dir) != 0)
    {
        LOG_E("Failed to mount the ROMFS root");
        return -RT_ERROR;
    }

    if (dfs_mount(SCA_STORAGE_PARTITION, SCA_ROOT_DIR, "lfs", 0, RT_NULL) != 0)
    {
        LOG_E("LittleFS mount failed on '%s' (errno %d)",
              SCA_STORAGE_PARTITION, rt_get_errno());
        return -RT_ERROR;
    }

    LOG_I("Mounted '%s' at %s", SCA_STORAGE_PARTITION, SCA_ROOT_DIR);
    return RT_EOK;
}
