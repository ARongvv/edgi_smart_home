/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 应用顶层装配。
 *
 * 生命周期：
 *   smart_home_app_start()  → 依次拉起设备/技能/Agent/网络，进入 UI 主循环
 *   smart_home_app_request_exit() → 让 UI 主循环退出
 *
 * 应用结构体（smart_home_agent_app_t）定义在 agent/smart_home_agent.h，
 * 因为它的字段几乎全属于 agent 域（cAGENT 句柄、技能库、工具策略）。
 * 本文件只暴露"启动/退出/状态查询"三件事。
 */

#ifndef __SMART_HOME_APP_H__
#define __SMART_HOME_APP_H__

#include "port/sca_config.h"
#include "port/sca_os.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 启动状态（供设置页与 msh 查询）────────────────────── */

typedef struct
{
    uint32_t board_mask;        /* SCA_BOARD_* 位掩码 */
    int      network_state;     /* 0=未启动 1=就绪 负数=错误 */
    int      agent_state;
    int      ui_state;
    int      storage_state;
    char     last_error[96];
} smart_home_boot_status_t;

/** 全局启动状态，永不为 NULL。 */
smart_home_boot_status_t *smart_home_boot_status(void);

/** 记录一条启动错误，同时打印。只保留最后一条。 */
void smart_home_boot_error(const char *stage, int code);

/* ── 应用主体 ──────────────────────────────────────────── */

/**
 * 启动 SmartHome 全流程。
 *
 * 内部顺序：设备服务 → 技能加载 → Agent → 网络 → UI 主循环。
 * UI 主循环返回后本函数才返回。
 *
 * @param boot 启动状态结构，由调用方提供（通常是 boot_status()）
 * @return SCA_OK 正常退出
 */
int  smart_home_app_start(smart_home_boot_status_t *boot);

/** 请求退出（可从 msh 或 UI 调用）。 */
void smart_home_app_request_exit(void);

/** 是否需要退出。UI 主循环每帧检查。 */
bool smart_home_app_exiting(void);

/* Reset only local demo state; credentials, sessions and reminders are untouched. */
int smart_home_app_demo_reset(void);
/* Execute the registered set_light tool and verify its JSON, state and save. */
int smart_home_app_demo_check(char *tool_result, size_t result_size);

#ifdef __cplusplus
}
#endif

#endif /* __SMART_HOME_APP_H__ */
