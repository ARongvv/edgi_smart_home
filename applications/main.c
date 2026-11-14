/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 应用启动编排。
 *
 * 顺序原则：先板级、再平台、后业务。任一步失败都记入 ready 掩码，
 * 由 UI 的设置页展示降级原因，绝不在启动阶段 panic。
 *
 * 初始化拆成 sca_init_* 若干小步骤，每步返回状态码，
 * 便于在设置页逐项显示降级原因。
 */

#include <rtthread.h>
#include <agent/error.h>

#include "board/board.h"
#include "port/sca_config.h"
#include "port/sca_os.h"
#include "port/sca_fs.h"
#include "smart_home/core/smart_home_app.h"
#include "smart_home/core/smart_home_memory.h"

#define DBG_TAG "sca.init"

/* 启动状态由 smart_home/core/smart_home_app.c 持有并导出
 * smart_home_boot_status()；这里不再定义第二份。 */

/**
 * 启动 SmartHome 应用。
 *
 * 返回 RT_EOK 表示进入主循环并正常退出；其他值表示启动失败。
 * 失败时 UI 仍会尝试显示错误页（如果 display 可用）。
 */
rt_err_t smart_home_boot(void)
{
    smart_home_boot_status_t *boot = smart_home_boot_status();

    rt_memset(boot, 0, sizeof(*boot));

    /* 0. 建立 HyperRAM 大块内存池：会话/MiLoCo/模型缓冲不再占用
     *    默认堆，否则 LVGL 会在首页构建阶段堆耗尽。 */
    (void)smart_home_bulk_pool_init();

    /* 1. 板级：屏幕、触摸、音频、存储、WiFi */
    boot->board_mask = board_init_all();
    boot->storage_state = (boot->board_mask & SCA_BOARD_STORAGE) ? SCA_OK : SCA_ERR_IO;
    SCA_LOGI(DBG_TAG, "board: %s", board_ready_mask_name(boot->board_mask));

    if ((boot->board_mask & SCA_BOARD_DISPLAY) == 0)
    {
        SCA_LOGE(DBG_TAG, "no display, abort");
        return -RT_ERROR;
    }

    /* 2. 持久化目录：确保 state/sessions/skills 等目录存在。
     *    存储没起来时跳过，后续写文件会失败但不崩溃。 */
    if (boot->board_mask & SCA_BOARD_STORAGE)
    {
        sca_fs_mkdir_p(SCA_STATE_DIR);
        sca_fs_mkdir_p(SCA_SESSION_DIR);

        if (sca_fs_free_bytes(SCA_ROOT_DIR) >= 0)
        {
            SCA_LOGI(DBG_TAG, "storage free: %ld KB",
                     sca_fs_free_bytes(SCA_ROOT_DIR) / 1024);
        }
    }

    /* 3. 业务启动：设备 → 技能 → Agent → 网络 → UI */
    return smart_home_app_start(boot);
}

/* ── RT-Thread 的 main 线程入口 ────────────────────────────
 *
 * RT-Thread 的启动链是：
 *   Reset_Handler → entry() → rtthread_startup()
 *     → 创建 main 线程（入口就是本函数）
 *     → 创建 idle / timer 线程 → 启动调度器
 *
 * main() 返回会被视作"main 线程结束"（RT-Thread 会回收它），
 * 因此这里不返回，转而常驻：先跑一遍业务启动，然后睡在
 * msh 命令上。用户也可以从 msh 手动敲 `smarthome` 重新拉起。
 *
 * 不在 main 里直接无限循环：smart_home_app_start() 自己会进
 * UI 主循环；它返回说明用户退出，此时保持在 msh 可用即可。
 */
int main(void)
{
    rt_kprintf("\n[sca] SmartHome on Edgi-Talk (RT-Thread %d.%d.%d)\n",
               RT_VERSION_MAJOR, RT_VERSION_MINOR, RT_VERSION_PATCH);

    /* 自动启动一次，失败也继续留在 msh，便于串口排错 */
    {
        rt_err_t ret = smart_home_boot();

        rt_kprintf("[sca] boot %s (%d)\n", ret == RT_EOK ? "ok" : "failed",
                   ret);
    }

    /* main 线程转为休眠，把 CPU 让给 UI 与 idle；
     * msh 命令在 tshell 线程里，不受影响。 */
    for (;;)
    {
        rt_thread_mdelay(1000);
    }

    return 0;
}

static int cmd_smarthome(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_err_t ret = smart_home_boot();
    return (ret == RT_EOK) ? 0 : -1;
}
MSH_CMD_EXPORT_ALIAS(cmd_smarthome, smarthome, start SmartHome application);

int cmd_sca_status(int argc, char **argv)
{
    smart_home_boot_status_t *st = smart_home_boot_status();

    (void)argc;
    (void)argv;

    rt_kprintf("board mask : 0x%02x [%s]\n", st->board_mask,
               board_ready_mask_name(st->board_mask));
    rt_kprintf("network    : %d\n", st->network_state);
    rt_kprintf("agent      : %d\n", st->agent_state);
    rt_kprintf("ui         : %d\n", st->ui_state);
    rt_kprintf("storage    : %d\n", st->storage_state);
    rt_kprintf("last error : %s\n", st->last_error[0] ? st->last_error : "-");
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_sca_status, sca_status, show SmartHome boot status);

static int cmd_sca_selftest(int argc, char **argv)
{
    int ret;

    (void)argc;
    (void)argv;

    ret = sca_os_thread_selftest();
    rt_kprintf("thread selftest: %s (%d)\n",
               ret == SCA_OK ? "ok" : "failed", ret);
    return ret == SCA_OK ? 0 : ret;
}
MSH_CMD_EXPORT_ALIAS(cmd_sca_selftest, sca_selftest, test thread lifecycle);

static int cmd_sca_demo_reset(int argc, char **argv)
{
    int ret;
    (void)argc;
    (void)argv;
    ret = smart_home_app_demo_reset();
    rt_kprintf("DEMO_RESET %s (%d)\n", ret == AGENT_OK ? "ok" : "failed", ret);
    return ret == AGENT_OK ? 0 : ret;
}
MSH_CMD_EXPORT_ALIAS(cmd_sca_demo_reset, sca_demo_reset,
                     reset local demo devices without clearing credentials);

static int cmd_sca_demo_check(int argc, char **argv)
{
    char tool_result[1024];
    int ret;
    (void)argc;
    (void)argv;
    ret = smart_home_app_demo_check(tool_result, sizeof(tool_result));
    if (tool_result[0]) rt_kprintf("DEMO_TOOL %s\n", tool_result);
    rt_kprintf("DEMO_CHECK %s (%d)\n", ret == AGENT_OK ? "ok" : "failed", ret);
    return ret == AGENT_OK ? 0 : ret;
}
MSH_CMD_EXPORT_ALIAS(cmd_sca_demo_check, sca_demo_check,
                     verify local tool result device state and persistence);


