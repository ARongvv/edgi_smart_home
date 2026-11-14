/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 应用主体装配。
 *
 * 启动顺序（每步失败都记录但不 panic，让 UI 能显示降级原因）：
 *   1. 设备服务     —— 本地虚拟设备模型 + 事件环
 *   2. 技能加载     —— 从 SCA_SKILL_DIR 读 Markdown
 *   3. Agent        —— cAGENT 内核 + 工具注册 + 策略
 *   4. 网络         —— WiFi 四态机
 *   5. UI 主循环    —— 阻塞直到用户退出
 *
 * 初始化拆成独立的 stage 函数，便于按阶段调试，也让主流程一眼看清。
 */

#include "port/sca_config.h"
#include "port/sca_os.h"
#include "smart_home/core/smart_home_app.h"
#include "smart_home/agent/smart_home_agent.h"
#include "smart_home/config/smart_home_secrets.h"
#if SCA_ENABLE_MILOCO
#include "smart_home/miloco/smart_home_miloco.h"
#endif
#if SCA_ENABLE_REMINDERS
#include "smart_home/reminder/smart_home_reminder.h"
#endif

#if SCA_ENABLE_LVGL
#include "smart_home/ui/lvgl/smart_home_lvgl.h"
#endif

#define DBG_TAG "sca.app"

static volatile bool g_exit_requested = false;
static smart_home_boot_status_t g_boot;
static smart_home_agent_app_t   g_app;
static sca_thread_t            *g_network_worker;

static void network_autoconnect_worker(void *argument)
{
    smart_home_network_status_t status;
    char ssid[33] = {0};
    char password[65] = {0};
    int ret;

    (void)argument;
    smart_home_network_status_init(&status);
    ret = smart_home_secrets_get_wifi_credentials(ssid, sizeof(ssid),
                                                  password, sizeof(password));
    if (ret == AGENT_OK && ssid[0]) {
        ret = smart_home_network_connect_credentials(&status, ssid, password);
        smart_home_agent_app_set_network_status(&g_app, &status);
        if (ret != AGENT_OK) {
            SCA_LOGW(DBG_TAG, "启动自动连接失败 (%d)，可在设置页重试", ret);
        } else {
#if SCA_ENABLE_MILOCO
            /* 联网成功立即拉天气，不等 worker 的 60 s 失败重试窗口。 */
            smart_home_miloco_kick_weather(g_app.miloco);
#endif
        }
    }
    memset(password, 0, sizeof(password));
}

smart_home_boot_status_t *smart_home_boot_status(void)
{
    return &g_boot;
}

void smart_home_boot_error(const char *stage, int code)
{
    smart_home_boot_status_t *st = smart_home_boot_status();

    SCA_LOGE(DBG_TAG, "%s failed (%d)", stage, code);

    snprintf(st->last_error, sizeof(st->last_error),
             "%s:%d", stage ? stage : "?", code);
}

/* ── 各阶段 ────────────────────────────────────────────── */

static int stage_device(void)
{
    int ret = smart_home_agent_app_init(&g_app);

    g_boot.agent_state = ret;
    if (ret != AGENT_OK) {
        smart_home_boot_error("agent-init", ret);
        return ret;
    }

    SCA_LOGI(DBG_TAG, "设备服务就绪, 技能 %u 个",
             (unsigned)g_app.skill_store.count);
    return AGENT_OK;
}

static int stage_network(void)
{
    sca_thread_attr_t attr;
    int ret = smart_home_network_probe(&g_app.system_status.network_status);

    if (!g_network_worker) {
        memset(&attr, 0, sizeof(attr));
        attr.name = "net-auto";
        attr.fn = network_autoconnect_worker;
        attr.stack_bytes = 16384;
        attr.priority = 11;
        attr.cpu = 0xFF;
        g_network_worker = sca_thread_create(&attr);
        if (!g_network_worker) {
            SCA_LOGW(DBG_TAG, "自动连接任务创建失败，可在设置页手动连接");
        }
    }

    g_boot.network_state = ret;
    if (ret == AGENT_OK) {
        SCA_LOGI(DBG_TAG, "网络 %s, online=%d",
                 smart_home_network_platform_name(
                     g_app.system_status.network_status.platform),
                 g_app.system_status.network_status.online);
    } else {
        SCA_LOGW(DBG_TAG, "网络未就绪，进入离线模式");
    }
    return ret;
}

static int stage_ui(void)
{
#if SCA_ENABLE_LVGL
    int ret = smart_home_lvgl_run(&g_app, NULL);

    g_boot.ui_state = ret;
    return ret;
#else
    /* 无 UI 时保持 msh 可用，便于串口调试 */
    SCA_LOGI(DBG_TAG, "UI 未编译，保持 msh 前台");
    while (!g_exit_requested) {
        sca_sleep_ms(500);
    }
    g_boot.ui_state = SCA_OK;
    return SCA_OK;
#endif
}

/* ── 主流程 ────────────────────────────────────────────── */

int smart_home_app_start(smart_home_boot_status_t *boot)
{
    int ret;

    if (!boot) return SCA_ERR_PARAM;
    if (g_app.agent) {
        SCA_LOGW(DBG_TAG, "应用已在运行");
        return SCA_ERR_STATE;
    }

    g_exit_requested = false;
    memset(&g_app, 0, sizeof(g_app));

    ret = stage_device();
    if (ret != AGENT_OK) {
        boot->agent_state = ret;
        return ret;
    }

#if SCA_ENABLE_REMINDERS
    ret = smart_home_reminder_start(NULL);
    if (ret != AGENT_OK) {
        SCA_LOGW(DBG_TAG, "提醒服务启动失败 (%d)，继续本地模式", ret);
    }
#endif

    (void)stage_network();      /* 失败不阻断 */

    boot->agent_state = AGENT_OK;
    ret = stage_ui();

    if (g_network_worker) {
        if (sca_thread_join(g_network_worker, 35000) != SCA_OK) {
            SCA_LOGW(DBG_TAG, "自动连接任务未及时退出");
        }
        sca_thread_destroy(g_network_worker);
        g_network_worker = NULL;
    }
#if SCA_ENABLE_REMINDERS
    smart_home_reminder_stop();
#endif
    smart_home_agent_app_deinit(&g_app);
    return ret;
}

void smart_home_app_request_exit(void)
{
    g_exit_requested = true;
}

bool smart_home_app_exiting(void)
{
    return g_exit_requested;
}

int smart_home_app_demo_reset(void)
{
    int ret;

    if (!g_app.agent || !g_app.agent_mutex_initialized) return AGENT_ERROR_STATE;
    sca_mutex_lock(&g_app.agent_mutex);
    ret = smart_home_device_service_reset(&g_app.device_service);
    sca_mutex_unlock(&g_app.agent_mutex);
    if (ret == AGENT_OK)
        ret = smart_home_device_persistence_flush(&g_app.device_persistence, 3000);
    return ret;
}

typedef struct {
    agent_tool_execute_fn execute;
    agent_tool_validate_fn validate;
    void *user_data;
    int found;
} demo_tool_t;

typedef struct {
    char *buffer;
    size_t size;
    size_t used;
} demo_output_t;

static agent_error_t demo_find_set_light(void *user_data, const agent_tool_t *tool)
{
    demo_tool_t *found = user_data;
    if (tool->name.size == 9u && memcmp(tool->name.data, "set_light", 9u) == 0) {
        found->execute = tool->execute;
        found->validate = tool->validate;
        found->user_data = tool->user_data;
        found->found = 1;
    }
    return AGENT_OK;
}

static agent_error_t demo_output_write(void *user_data, agent_string_view_t text)
{
    demo_output_t *output = user_data;
    if (text.size >= output->size - output->used) return AGENT_ERROR_TRUNCATED;
    memcpy(output->buffer + output->used, text.data, text.size);
    output->used += text.size;
    output->buffer[output->used] = '\0';
    return AGENT_OK;
}

static int demo_tool_result_valid(const char *text)
{
    cJSON *root = cJSON_Parse(text);
    const cJSON *source = cJSON_GetObjectItemCaseSensitive(root, "source");
    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    int valid = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")) &&
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "on")) &&
        cJSON_IsString(source) && strcmp(source->valuestring, "demo") == 0 &&
        cJSON_IsNumber(brightness) && brightness->valueint == 72;
    cJSON_Delete(root);
    return valid;
}

int smart_home_app_demo_check(char *tool_result, size_t result_size)
{
    static const char call_id[] = "demo-check";
    static const char tool_name[] = "set_light";
    smart_home_device_t light;
    agent_limits_t limits = AGENT_LIMITS_DEFAULT;
    agent_tool_context_t context;
    agent_text_sink_t sink;
    demo_output_t output;
    demo_tool_t tool;
    char arguments[160];
    int ret;

    if (!tool_result || result_size == 0u) return AGENT_ERROR_INVALID;
    tool_result[0] = '\0';
    if (!g_app.agent || !g_app.agent_mutex_initialized) return AGENT_ERROR_STATE;
    memset(&tool, 0, sizeof(tool));
    memset(&context, 0, sizeof(context));
    memset(&output, 0, sizeof(output));
    output.buffer = tool_result;
    output.size = result_size;
    sink.write = demo_output_write;
    sink.context = &output;

    sca_mutex_lock(&g_app.agent_mutex);
    ret = smart_home_device_service_find_first(&g_app.device_service,
        "living_room", SMART_HOME_DEVICE_LIGHT, &light);
    if (ret == AGENT_OK) {
        snprintf(arguments, sizeof(arguments),
            "{\"room\":\"living_room\",\"device_id\":%d,"
            "\"on\":true,\"brightness\":72}", light.id);
        context.call.id = agent_string_view(call_id, sizeof(call_id) - 1u);
        context.call.name = agent_string_view(tool_name, sizeof(tool_name) - 1u);
        context.call.arguments_json = agent_string_view(arguments, strlen(arguments));
        context.limits = &limits;
        ret = agent_tool_enumerate(g_app.agent, demo_find_set_light, &tool);
        if (ret == AGENT_OK && (!tool.found || !tool.execute))
            ret = AGENT_ERROR_NOT_FOUND;
        if (ret == AGENT_OK && tool.validate)
            ret = tool.validate(tool.user_data, &context);
        if (ret == AGENT_OK)
            ret = tool.execute(tool.user_data, &context, &sink);
    }
    sca_mutex_unlock(&g_app.agent_mutex);
    if (ret != AGENT_OK) return ret;
    if (!demo_tool_result_valid(tool_result)) return AGENT_ERROR_PARSE;
    ret = smart_home_device_service_find_first(&g_app.device_service,
        "living_room", SMART_HOME_DEVICE_LIGHT, &light);
    if (ret == AGENT_OK && (!light.on || light.brightness != 72))
        ret = AGENT_ERROR_TOOL_FAILED;
    if (ret == AGENT_OK)
        ret = smart_home_device_persistence_flush(&g_app.device_persistence, 3000);
    return ret;
}
