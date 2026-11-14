/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Agent 编排层。
 *
 * 职责：把 cAGENT 内核、设备服务、技能、工具、网络、记忆聚合成一个
 * "可对话"的整体，并向上暴露 UI 需要的极简接口。
 *
 * ★ 组件裁剪说明 ★
 *   以下三类平台耦合组件未纳入构建（合计约 3.7k 行）：
 *     - addons/node_gateway   （局域网节点网关，TCP 服务端）
 *     - addons/mcp_bridge     （MCP 协议桥）
 *     - addons/app_bridge     （手机 App 直连）
 *     - quickapp/             （快应用运行时，RT-Thread 无对应物）
 *   Kconfig 里它们本就默认关闭，保留只会增加维护面。
 */

#pragma once

#include "port/sca_os.h"
#include "port/sca_config.h"

#include "smart_home/core/smart_home_tool_metadata.h"
#include "smart_home/device/smart_home_device_service.h"
#include "smart_home/device/smart_home_device_persistence.h"
#include "smart_home/net/smart_home_network.h"
#include "smart_home/skills/smart_home_skill_loader.h"
#include "smart_home/agent/smart_home_agent_backend.h"

#if SCA_ENABLE_USER_MEMORY
#include "smart_home/memory/smart_home_user_memory.h"
#endif

#include <agent.h>

#include <stddef.h>
#include <stdint.h>

#define SMART_HOME_STATUS_UNKNOWN 1

typedef struct
{
    smart_home_network_status_t network_status;
    int    agent_status;
    int    skills_status;
    int    tools_status;
    int    model_status;
    int    session_status;
    int    persistence_status;
    int    miloco_status;
    size_t skills_loaded;
    char   last_error[96];
} smart_home_system_status_t;

/* 本地工具的用户访问策略；底层工具是否可用仍由 cAGENT enabled 表示。 */
#define SMART_HOME_LOCAL_TOOL_ACCESS_MAX AGENT_MAX_TOOLS
#define SMART_HOME_LOCAL_TOOL_NAME_SIZE  64u

typedef struct
{
    char name[SMART_HOME_LOCAL_TOOL_NAME_SIZE];
    int  allowed;
} smart_home_local_tool_access_t;

typedef struct smart_home_agent_app
{
    agent_t                    *agent;
    smart_home_agent_backend_t *backend;
    smart_home_state_t          device_state;
    smart_home_device_service_t device_service;
    smart_home_device_persistence_t device_persistence;
    smart_home_skill_store_t    skill_store;
    smart_home_model_config_t   model_config;
    smart_home_system_status_t  system_status;

#if SCA_ENABLE_USER_MEMORY
    smart_home_user_memory_t *user_memory;
#endif

#if SCA_ENABLE_MILOCO
    struct smart_home_miloco *miloco;
#endif

    smart_home_local_tool_access_t
        local_tool_access[SMART_HOME_LOCAL_TOOL_ACCESS_MAX];
    size_t local_tool_access_count;

    struct
    {
        char     name[AGENT_MAX_NAME_BYTES + 1u];
        uint16_t group;
        int      read_only;
    } remote_routes[AGENT_MAX_TOOLS];
    size_t remote_route_count;

    uint64_t run_start_ms;
    agent_event_callback_t observer;
    void                  *observer_user_data;

    sca_mutex_t agent_mutex;
    int         agent_mutex_initialized;
} smart_home_agent_app_t;

/* ── 生命周期 ──────────────────────────────────────────── */

int  smart_home_agent_app_init(smart_home_agent_app_t *app);
void smart_home_agent_app_deinit(smart_home_agent_app_t *app);

void smart_home_agent_app_set_network_status(
    smart_home_agent_app_t *app,
    const smart_home_network_status_t *status);

int smart_home_agent_app_apply_model_config(
    smart_home_agent_app_t *app,
    const smart_home_model_config_t *config);

/* ── 对话 ──────────────────────────────────────────────── */

int smart_home_agent_run(smart_home_agent_app_t *app,
                         const char *input,
                         char *output,
                         size_t output_size);

/* ── 工具枚举与权限 ────────────────────────────────────── */

/** 遍历当前 agent registry；回调内不得调用 Agent/应用接口或保存字符串指针。 */
int smart_home_agent_app_enumerate_tools(
    const smart_home_agent_app_t *app,
    smart_home_tool_visit_fn callback,
    void *user_data);

/** UI 查询与权限变更：锁被聊天任务占用时立即返回 BUSY。 */
int smart_home_agent_app_try_enumerate_tools(
    const smart_home_agent_app_t *app,
    smart_home_tool_visit_fn callback,
    void *user_data);

int smart_home_agent_app_try_set_local_tool_access(
    smart_home_agent_app_t *app, const char *tool_name, int allowed);

/** 设置本地工具的用户访问策略；拒绝时工具不会执行，但仍向模型声明。 */
int smart_home_agent_app_set_local_tool_access(
    smart_home_agent_app_t *app, const char *tool_name, int allowed);

/** 查询本地工具的用户访问策略；未显式配置的本地工具默认允许。 */
int smart_home_agent_app_local_tool_access_allowed(
    const smart_home_agent_app_t *app, const char *tool_name, int *allowed);

/* ── 会话 ──────────────────────────────────────────────── */

int smart_home_agent_app_clear_session(smart_home_agent_app_t *app);

int smart_home_agent_app_set_observer(smart_home_agent_app_t *app,
                                      agent_event_callback_t callback,
                                      void *user_data);

/* Complete groups newest first; callback must copy borrowed text and must not reenter. */
int smart_home_agent_app_visit_history(smart_home_agent_app_t *app,
                                       agent_session_visit_fn visit,
                                       void *context);

#if SCA_ENABLE_MILOCO
int smart_home_miloco_tools_register(agent_t *agent,
                                     smart_home_agent_app_t *app);
#endif
