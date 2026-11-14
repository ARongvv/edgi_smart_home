/* SPDX-License-Identifier: Apache-2.0 */
/**
 * smart_home_config_store: 统一 JSON 配置存取模块。
 *
 * 通用骨架（load/save/原子写/恢复策略）+ backends/settings/state 薄封装。
 * secrets.json 不在本模块（config/README：普通 store 不加载/回退/写回 secrets）。
 */

#include "smart_home/config/smart_home_config_store.h"
#include "smart_home/config/smart_home_backends.h"
#include "smart_home/device/smart_home_device.h"

#include <agent/error.h>

#include "port/sca_fs.h"
#include "port/sca_os.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 通用骨架 ── */

#define CONFIG_STORE_VERSION 1
#define CONFIG_STORE_TMP_ATTEMPTS 16

static const char *config_error_str(int status)
{
    switch (status) {
    case AGENT_ERROR_NOMEM:   return "out of memory";
    case AGENT_ERROR_INVALID: return "invalid argument";
    case AGENT_ERROR_PARSE:   return "JSON parse error";
    case AGENT_ERROR_LIMIT:   return "limit exceeded";
    default:                  return "config error";
    }
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        snprintf(error, error_size, "%s", message ? message : "config error");
    }
}

static int check_version(const cJSON *root, char *error, size_t error_size)
{
    const cJSON *version;

    if (!root || !cJSON_IsObject(root)) {
        set_error(error, error_size, "config is not a JSON object");
        return AGENT_ERROR_PARSE;
    }

    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!version || !cJSON_IsNumber(version)) {
        set_error(error, error_size, "missing integer version");
        return AGENT_ERROR_PARSE;
    }
    if (version->valueint != CONFIG_STORE_VERSION) {
        set_error(error, error_size, "unsupported version");
        return AGENT_ERROR_PARSE;
    }

    return AGENT_OK;
}

/* 读取文件到堆缓冲。返回 0 成功；-1 文件缺失；1 读取/IO/过大错误。 */
static int read_file(const char *path,
                     char **out_text,
                     size_t *out_size,
                     char *error,
                     size_t error_size)
{
    char *text;
    size_t size = 0u;

    /* 读取统一走 sca_fs_read_all（内部用 stat + read，并做上限检查）。 */
    if (!sca_fs_exists(path)) {
        return -1;              /* 缺失，交给恢复策略 */
    }

    text = (char *)sca_fs_read_all(path, &size, CONFIG_STORE_MAX_FILE_SIZE);
    if (!text) {
        set_error(error, error_size, "read failed or file too large");
        return 1;
    }

    *out_text = text;
    *out_size = size;
    return 0;
}

/* 走恢复策略：返回默认工厂结果（RECOVER_DEFAULT）或 NULL（REJECT）。 */
static cJSON *recover(smart_home_config_default_fn make_default,
                      smart_home_config_recovery_t recovery,
                      char *error,
                      size_t error_size)
{
    if (recovery == SMART_HOME_CONFIG_RECOVER_DEFAULT && make_default) {
        return make_default(NULL);
    }
    set_error(error, error_size, "config rejected");
    return NULL;
}

int smart_home_config_store_load_compatible(const char *path,
                                 int previous_version,
                                 smart_home_config_default_fn make_default,
                                 smart_home_config_recovery_t recovery,
                                 smart_home_config_validate_fn validate,
                                 void *user_data,
                                 cJSON **out_root,
                                 char *error,
                                 size_t error_size)
{
    char *text;
    size_t size;
    cJSON *root;
    int read_ret;
    int ret;

    if (!path || !out_root) {
        return AGENT_ERROR_INVALID;
    }
    *out_root = NULL;

    read_ret = read_file(path, &text, &size, error, error_size);
    if (read_ret < 0) {
        /* 文件缺失 → 恢复策略 */
        root = recover(make_default, recovery, error, error_size);
        if (!root) {
            return AGENT_ERROR_PARSE;
        }
        *out_root = root;
        return AGENT_OK;
    }
    if (read_ret > 0) {
        /* 权限/IO/过大 → 同样走恢复策略（保留原文件） */
        root = recover(make_default, recovery, error, error_size);
        if (!root) {
            return AGENT_ERROR_PARSE;
        }
        *out_root = root;
        return AGENT_OK;
    }

    root = cJSON_ParseWithLength(text, size);
    free(text);
    if (!root) {
        /* 损坏 → 恢复策略（保留原文件） */
        root = recover(make_default, recovery, error, error_size);
        if (!root) {
            return AGENT_ERROR_PARSE;
        }
        *out_root = root;
        return AGENT_OK;
    }

    ret = check_version(root, error, error_size);
    if (ret != AGENT_OK && previous_version > 1) {
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
        if (cJSON_IsNumber(version) && version->valuedouble == previous_version) {
            ret = AGENT_OK;
        }
    }
    if (ret != AGENT_OK) {
        cJSON_Delete(root);
        root = recover(make_default, recovery, error, error_size);
        if (!root) {
            return AGENT_ERROR_PARSE;
        }
        *out_root = root;
        return AGENT_OK;
    }

    if (validate) {
        ret = validate(root, user_data);
        if (ret != AGENT_OK) {
            cJSON_Delete(root);
            set_error(error, error_size, config_error_str(ret));
            root = recover(make_default, recovery, error, error_size);
            if (!root) {
                return AGENT_ERROR_PARSE;
            }
            *out_root = root;
            return AGENT_OK;
        }
    }

    *out_root = root;
    return AGENT_OK;
}

int smart_home_config_store_load(const char *path,
                                 smart_home_config_default_fn make_default,
                                 smart_home_config_recovery_t recovery,
                                 smart_home_config_validate_fn validate,
                                 void *user_data, cJSON **out_root,
                                 char *error, size_t error_size)
{
    return smart_home_config_store_load_compatible(path, 0, make_default,
        recovery, validate, user_data, out_root, error, error_size);
}


/* 确保配置目录存在（首次运行可能不存在） */
static int ensure_config_dir(const char *path)
{
    char dir[SMART_HOME_CONFIG_PATH_MAX];
    const char *slash;
    size_t len;

    if (!path) {
        return -1;
    }

    slash = strrchr(path, '/');
    if (!slash || slash == path) {
        return 0;
    }

    len = (size_t)(slash - path);
    if (len >= sizeof(dir)) {
        return -1;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';

    if (sca_fs_mkdir_p(dir) == 0) {
        return 0;
    }
    return (errno == EEXIST) ? 0 : -1;
}

int smart_home_config_store_save(const char *path, const cJSON *root)
{
    char tmp_path[SMART_HOME_CONFIG_PATH_MAX];
    char *json;
    int fd;
    int attempt;
    int ret;

    if (!path || !root) {
        return AGENT_ERROR_INVALID;
    }

    if (ensure_config_dir(path) != 0) {
        return AGENT_ERROR_PARSE;
    }

    json = cJSON_PrintUnformatted(root);
    if (!json) {
        return AGENT_ERROR_NOMEM;
    }

    /* 原子写统一交给 port/sca_fs：内部实现是
     * 写 <path>.tmp → fsync → rename，掉电时旧文件保持完整。
     *
     * 原子写使用 O_EXCL + 唯一 generation 后缀 + 目录 fsync，
     * 是为了解决并发写同一文件的问题。但 config 模块的
     * 写入本来就由调用方串行化（设置页单线程），那套复杂度可以省掉。
     */
    ret = sca_fs_write_atomic(path, json, strlen(json));
    free(json);
    if (ret != SCA_OK) {
        return AGENT_ERROR_PARSE;
    }

    return AGENT_OK;
}

/* ── backends.json ── */

/* 默认工厂：从编译期内置预设表 g_llm_presets[] 构造（唯一默认来源） */
static cJSON *backends_make_default(void *user_data)
{
    cJSON *root;
    cJSON *backends;
    size_t count;
    size_t i;

    (void)user_data;

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "version", CONFIG_STORE_VERSION);
    backends = cJSON_AddArrayToObject(root, "backends");
    if (!backends) {
        cJSON_Delete(root);
        return NULL;
    }

    count = smart_home_llm_backend_count();
    for (i = 0u; i < count; i++) {
        const smart_home_llm_backend_preset_t *preset =
            smart_home_llm_backend_get(i);
        cJSON *item;

        if (!preset) {
            continue;
        }
        item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(item, "backend_id", preset->id);
        cJSON_AddStringToObject(item, "label", preset->name);
        cJSON_AddStringToObject(item, "host", preset->host);
        cJSON_AddStringToObject(item, "path", preset->path);
        cJSON_AddStringToObject(item, "port", preset->port);
        cJSON_AddStringToObject(item, "default_model", preset->default_model);
        cJSON_AddNumberToObject(item, "default_timeout_ms",
                                (double)preset->default_timeout_ms);
        cJSON_AddItemToArray(backends, item);
    }

    return root;
}

static bool valid_backend_id(const char *id)
{
    const char *p;

    if (!id || !id[0] || strlen(id) > 32u) {
        return false;
    }
    for (p = id; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-')) {
            return false;
        }
    }

    return true;
}

/* 校验单个 backend 的 host/path/port/model/timeout（对齐 smart_home_model_config_validate）。
 * custom 预设（backend_id == "custom"）是"运行时用户填"的占位，允许 host/path/model 为空。 */
static int validate_backend_fields(const cJSON *item)
{
    const cJSON *id_item;
    const cJSON *host;
    const cJSON *path;
    const cJSON *port;
    const cJSON *model;
    const cJSON *timeout;
    const char *p;
    int is_custom;

    id_item = cJSON_GetObjectItemCaseSensitive(item, "backend_id");
    is_custom = cJSON_IsString(id_item) &&
                strcmp(id_item->valuestring, "custom") == 0;

    host = cJSON_GetObjectItemCaseSensitive(item, "host");
    path = cJSON_GetObjectItemCaseSensitive(item, "path");
    port = cJSON_GetObjectItemCaseSensitive(item, "port");
    model = cJSON_GetObjectItemCaseSensitive(item, "default_model");
    timeout = cJSON_GetObjectItemCaseSensitive(item, "default_timeout_ms");

    if (!cJSON_IsString(host)) {
        return AGENT_ERROR_PARSE;
    }
    if (!is_custom) {
        if (!host->valuestring[0]) {
            return AGENT_ERROR_PARSE;
        }
        for (p = host->valuestring; *p; p++) {
            if ((unsigned char)*p <= 0x20u || *p == 0x7fu || *p == '/') {
                return AGENT_ERROR_PARSE;
            }
        }

        if (!cJSON_IsString(path) || !path->valuestring[0] ||
            path->valuestring[0] != '/') {
            return AGENT_ERROR_PARSE;
        }

        if (!cJSON_IsString(model) || !model->valuestring[0]) {
            return AGENT_ERROR_PARSE;
        }
    }

    if (!cJSON_IsString(port) || !port->valuestring[0]) {
        return AGENT_ERROR_PARSE;
    }
    for (p = port->valuestring; *p; p++) {
        if (*p < '0' || *p > '9') {
            return AGENT_ERROR_PARSE;
        }
    }

    if (cJSON_IsNumber(timeout) &&
        (timeout->valuedouble < 5000.0 ||
         timeout->valuedouble > 60000.0)) {
        return AGENT_ERROR_PARSE;
    }

    return AGENT_OK;
}

static int validate_backends(const cJSON *root, void *user_data)
{
    const cJSON *backends;
    const cJSON *item;
    int count = 0;
    int i;

    (void)user_data;

    backends = cJSON_GetObjectItemCaseSensitive(root, "backends");
    if (!backends || !cJSON_IsArray(backends)) {
        return AGENT_ERROR_PARSE;
    }

    cJSON_ArrayForEach(item, backends) {
        const cJSON *id;
        int ret;

        if (!cJSON_IsObject(item)) {
            return AGENT_ERROR_PARSE;
        }
        id = cJSON_GetObjectItemCaseSensitive(item, "backend_id");
        if (!cJSON_IsString(id) || !valid_backend_id(id->valuestring)) {
            return AGENT_ERROR_PARSE;
        }
        ret = validate_backend_fields(item);
        if (ret != AGENT_OK) {
            return ret;
        }
        count++;
    }

    if (count == 0 || count > CONFIG_SMART_HOME_MAX_BACKENDS) {
        return AGENT_ERROR_LIMIT;
    }

    /* backend_id 唯一性 */
    for (i = 0; i < count; i++) {
        const cJSON *a = cJSON_GetArrayItem(backends, i);
        const cJSON *aid = cJSON_GetObjectItemCaseSensitive(a, "backend_id");
        int j;

        for (j = i + 1; j < count; j++) {
            const cJSON *b = cJSON_GetArrayItem(backends, j);
            const cJSON *bid = cJSON_GetObjectItemCaseSensitive(b, "backend_id");
            if (aid->valuestring && bid->valuestring &&
                strcmp(aid->valuestring, bid->valuestring) == 0) {
                return AGENT_ERROR_INVALID;
            }
        }
    }

    return AGENT_OK;
}

int smart_home_config_backends_load(cJSON **out_root,
                                    char *error,
                                    size_t error_size)
{
    return smart_home_config_store_load(SMART_HOME_CONFIG_BACKENDS_PATH,
                                        backends_make_default,
                                        SMART_HOME_CONFIG_RECOVER_DEFAULT,
                                        validate_backends,
                                        NULL,
                                        out_root,
                                        error,
                                        error_size);
}

int smart_home_config_backends_save(const cJSON *root)
{
    int ret;

    if (!root) {
        return AGENT_ERROR_INVALID;
    }
    /* 保存前也校验，避免非法对象落盘 */
    ret = check_version(root, NULL, 0);
    if (ret != AGENT_OK) {
        return ret;
    }
    ret = validate_backends(root, NULL);
    if (ret != AGENT_OK) {
        return ret;
    }
    return smart_home_config_store_save(SMART_HOME_CONFIG_BACKENDS_PATH, root);
}

/* ── settings.json ── */

bool smart_home_config_weather_city_valid(const char *city)
{
    const unsigned char *p;
    size_t len;

    if (!city || !city[0]) {
        return false;
    }
    len = strlen(city);
    if (len >= SMART_HOME_WEATHER_CITY_MAX) {
        return false;
    }
    for (p = (const unsigned char *)city; *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
            *p == ',' || *p == '.' || *p == '+') {
            continue;
        }
        return false;
    }
    return true;
}

static cJSON *settings_make_default(void *user_data)
{
    cJSON *root;

    (void)user_data;

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "version", CONFIG_STORE_VERSION);
    cJSON_AddStringToObject(root, "active_backend_id", "deepseek");
    cJSON_AddStringToObject(root, "weather_city",
                            SMART_HOME_WEATHER_CITY_DEFAULT);
    return root;
}

static int validate_settings(const cJSON *root, void *user_data)
{
    const cJSON *active;
    const cJSON *weather_city;

    (void)user_data;

    active = cJSON_GetObjectItemCaseSensitive(root, "active_backend_id");
    if (!cJSON_IsString(active) || active->valuestring[0] == '\0') {
        return AGENT_ERROR_PARSE;
    }
    if (!valid_backend_id(active->valuestring)) {
        return AGENT_ERROR_PARSE;
    }

    /* weather_city 为可选字段：旧文件缺失时保持兼容。 */
    weather_city = cJSON_GetObjectItemCaseSensitive(root, "weather_city");
    if (weather_city &&
        (!cJSON_IsString(weather_city) ||
         !smart_home_config_weather_city_valid(weather_city->valuestring))) {
        return AGENT_ERROR_PARSE;
    }

    return AGENT_OK;
}

int smart_home_config_settings_load(cJSON **out_root,
                                    char *error,
                                    size_t error_size)
{
    return smart_home_config_store_load(SMART_HOME_CONFIG_SETTINGS_PATH,
                                        settings_make_default,
                                        SMART_HOME_CONFIG_RECOVER_DEFAULT,
                                        validate_settings,
                                        NULL,
                                        out_root,
                                        error,
                                        error_size);
}

int smart_home_config_settings_save(const cJSON *root)
{
    int ret;

    if (!root) {
        return AGENT_ERROR_INVALID;
    }
    ret = check_version(root, NULL, 0);
    if (ret != AGENT_OK) {
        return ret;
    }
    ret = validate_settings(root, NULL);
    if (ret != AGENT_OK) {
        return ret;
    }
    return smart_home_config_store_save(SMART_HOME_CONFIG_SETTINGS_PATH, root);
}

int smart_home_config_settings_get_weather_city(char *out, size_t out_size)
{
    cJSON *root = NULL;
    const cJSON *city;
    char error[64];
    int ret;

    if (!out || out_size == 0u) {
        return AGENT_ERROR_INVALID;
    }
    out[0] = '\0';
    ret = smart_home_config_settings_load(&root, error, sizeof(error));
    if (ret != AGENT_OK || !root) {
        cJSON_Delete(root);
        return ret != AGENT_OK ? ret : AGENT_ERROR_PARSE;
    }
    city = cJSON_GetObjectItemCaseSensitive(root, "weather_city");
    if (cJSON_IsString(city) &&
        smart_home_config_weather_city_valid(city->valuestring)) {
        snprintf(out, out_size, "%s", city->valuestring);
    } else {
        snprintf(out, out_size, "%s", SMART_HOME_WEATHER_CITY_DEFAULT);
    }
    cJSON_Delete(root);
    return AGENT_OK;
}

int smart_home_config_settings_set_weather_city(const char *city)
{
    cJSON *root = NULL;
    cJSON *replacement;
    char error[64];
    int ret;

    if (!smart_home_config_weather_city_valid(city)) {
        return AGENT_ERROR_INVALID;
    }
    ret = smart_home_config_settings_load(&root, error, sizeof(error));
    if (ret != AGENT_OK || !root) {
        cJSON_Delete(root);
        return ret != AGENT_OK ? ret : AGENT_ERROR_PARSE;
    }
    replacement = cJSON_CreateString(city);
    if (!replacement) {
        cJSON_Delete(root);
        return AGENT_ERROR_NOMEM;
    }
    if (!cJSON_ReplaceItemInObjectCaseSensitive(root, "weather_city",
                                                 replacement)) {
        /* 旧文件缺少该字段：直接新增，replacement 所有权转移。 */
        if (!cJSON_AddItemToObject(root, "weather_city", replacement)) {
            cJSON_Delete(replacement);
            cJSON_Delete(root);
            return AGENT_ERROR_NOMEM;
        }
    }
    ret = smart_home_config_settings_save(root);
    cJSON_Delete(root);
    return ret;
}

/* ── state.json ── */

/* 默认工厂：复用 smart_home_device_init() 的单灯默认（唯一默认来源） */
static cJSON *state_make_default(void *user_data)
{
    smart_home_state_t state;

    (void)user_data;

    smart_home_device_init(&state);
    return smart_home_device_state_to_json(&state);
}

int smart_home_config_state_load(cJSON **out_root,
                                 char *error,
                                 size_t error_size)
{
    return smart_home_config_store_load(SMART_HOME_CONFIG_STATE_PATH,
                                        state_make_default,
                                        SMART_HOME_CONFIG_RECOVER_DEFAULT,
                                        NULL,
                                        NULL,
                                        out_root,
                                        error,
                                        error_size);
}

int smart_home_config_state_save(const cJSON *root)
{
    smart_home_state_t state;
    int ret;

    if (!root) {
        return AGENT_ERROR_INVALID;
    }
    ret = check_version(root, NULL, 0);
    if (ret != AGENT_OK) {
        return ret;
    }
    /* 用 state_from_json 做完整业务校验（含亮度/温度/枚举/重复 id） */
    ret = smart_home_device_state_from_json(&state, root);
    if (ret != AGENT_OK) {
        return ret;
    }
    return smart_home_config_store_save(SMART_HOME_CONFIG_STATE_PATH, root);
}
