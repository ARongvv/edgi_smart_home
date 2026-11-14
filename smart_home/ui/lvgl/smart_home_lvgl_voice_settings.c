/* SPDX-License-Identifier: Apache-2.0 */
/* Independent voice credentials and configuration, with file/network work off LVGL. */
#include "smart_home/ui/lvgl/smart_home_lvgl_internal.h"
#include "smart_home/config/smart_home_secrets.h"
#include "smart_home/voice/smart_home_voice_result.h"
#include "smart_home/core/smart_home_memory.h"
#if SCA_ENABLE_ASR
#include "smart_home/voice/smart_home_asr.h"
#include "smart_home/voice/smart_home_voice_capture.h"
#endif
#if SCA_ENABLE_TTS
#include "smart_home/voice/smart_home_tts.h"
#include "smart_home/voice/smart_home_voice_player.h"
#include "smart_home/voice/smart_home_voice_play.h"
#endif
#include "port/sca_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/sca_os.h"

struct voice_controls_s {
    smart_home_secret_service_t service;
    smart_home_lvgl_t *ui;
    lv_obj_t *keyboard, *backend, *model, *host, *path, *port, *option;
    lv_obj_t *key, *enabled, *status, *save;
    lv_obj_t *basic, *connection;
};
static struct voice_controls_s g_voice[2];
static struct {
    lv_obj_t *tabs[3], *panel[3], *keyboard;
    int selected;
} g_voice_page;
static sca_mutex_t g_voice_job_mutex = SCA_MUTEX_INITIALIZER;
enum voice_job_action { VOICE_SAVE, VOICE_KEY, VOICE_TEST };
struct voice_job_s {
    smart_home_lvgl_t *ui;
    smart_home_secret_service_t service;
    enum voice_job_action action;
    char backend[32], key[256];
    int update_key;
#if SCA_ENABLE_ASR
    smart_home_asr_config_t asr;
#endif
#if SCA_ENABLE_TTS
    smart_home_tts_config_t tts;
#endif
};
static struct {
    sca_thread_t *thread;
    smart_home_lvgl_t *owner;
    struct voice_controls_s *controls;
    lv_obj_t *status, *key_input;
    int active, ready, result, key_saved;
    char message[256];
} g_voice_job;

static void clear_key(char *key, size_t size)
{
    volatile char *p = key;
    while (size--) *p++ = 0;
}

static const char *backend_id(struct voice_controls_s *v)
{
    size_t index = lv_dropdown_get_selected(v->backend);
#if SCA_ENABLE_ASR
    if (v->service == SMART_HOME_SECRET_ASR) {
        const smart_home_asr_preset_t *p = smart_home_asr_preset_get(index);
        return p ? p->id : NULL;
    }
#endif
#if SCA_ENABLE_TTS
    if (v->service == SMART_HOME_SECRET_TTS) {
        const smart_home_tts_preset_t *p = smart_home_tts_preset_get(index);
        return p ? p->id : NULL;
    }
#endif
    return NULL;
}

static void voice_settings_worker(void *argument)
{
    struct voice_job_s *job = argument;
    smart_home_voice_result_t result = {AGENT_OK, 0, SMART_HOME_VOICE_RESPONSE};
    char message[256] = "";
    int ret = AGENT_OK, saved = 0;
    uint8_t *pcm = NULL;
    size_t bytes = 0;
#if SCA_ENABLE_TTS
    uint32_t rate = 0;
#endif
    if (job->update_key) {
        ret = smart_home_secrets_set_api_key(job->service, job->backend, job->key);
        saved = ret == AGENT_OK;
    }
    if (ret == AGENT_OK && job->action == VOICE_SAVE) {
#if SCA_ENABLE_ASR
        if (job->service == SMART_HOME_SECRET_ASR)
            ret = smart_home_asr_config_save(&job->asr);
#endif
#if SCA_ENABLE_TTS
        if (job->service == SMART_HOME_SECRET_TTS)
            ret = smart_home_tts_config_save(&job->tts);
#endif
    }
    if (ret == AGENT_OK && job->action == VOICE_TEST) {
#if SCA_ENABLE_ASR
        if (job->service == SMART_HOME_SECRET_ASR) {
            result.stage = SMART_HOME_VOICE_CAPTURE;
            ret = voice_capture_record(4, NULL, &pcm, &bytes);
            result.code = ret;
            if (ret == AGENT_OK)
                ret = smart_home_asr_recognize_ex(pcm, bytes, message,
                                                  sizeof(message), &result);

            /* 采集服务是 ASR 与 KWS 共用的单实例，voice_capture_record()
             * 结束时已把消费者槽位还给上一个注册者。这里不能 deinit：
             * 那会销毁 worker，而常驻 KWS 不会自己重启它。 */
        }
#endif
#if SCA_ENABLE_TTS
        if (job->service == SMART_HOME_SECRET_TTS) {
            ret = smart_home_tts_synth_ex("Hello, this is a speech test", &pcm, &bytes,
                                           &rate, &result);
            if (ret == AGENT_OK) {
                result.stage = SMART_HOME_VOICE_PLAYBACK;
                ret = smart_home_voice_player_play_mem(pcm, bytes, rate, NULL);
                result.code = ret;
            }
        }
#endif
        smart_home_bulk_free(pcm);
    }
    clear_key(job->key, sizeof(job->key));
    sca_mutex_lock(&g_voice_job_mutex);
    g_voice_job.result = ret;
    g_voice_job.key_saved = saved;
    if (ret != AGENT_OK) {
        snprintf(g_voice_job.message, sizeof(g_voice_job.message), "%s%s (ret=%d)",
            saved ? "Key saved; then failed: " : "",
            job->action == VOICE_TEST ? smart_home_voice_result_text(&result) :
                smart_home_secrets_error_text(ret), ret);
    } else if (job->action == VOICE_TEST) {
        snprintf(g_voice_job.message, sizeof(g_voice_job.message),
            "Test done%s%.200s", message[0] ? "：" : "", message);
    } else if (job->action == VOICE_KEY && job->service == SMART_HOME_SECRET_LLM) {
        snprintf(g_voice_job.message, sizeof(g_voice_job.message),
            "LLM key saved. Re-apply the model config above.");
    } else {
        snprintf(g_voice_job.message, sizeof(g_voice_job.message),
            job->action == VOICE_KEY ? "Service key cleared" : "Saved and applied");
    }
    g_voice_job.ready = 1;
    sca_mutex_unlock(&g_voice_job_mutex);
    clear_key((char *)job, sizeof(*job));
    free(job);
    return;
}

static int start_job(struct voice_job_s *job, struct voice_controls_s *v,
                      lv_obj_t *status, lv_obj_t *key_input)
{
    /* 栈由 sca_thread_create 内部按 stack_bytes 分配并对齐，
     * 不再需要 smart_home_bulk_alloc + STACK_ALIGN_UP 那套手工处理。
     * 32768 是因为 VOICE_TEST 走 TLS 握手 + 播放，比保存路径深得多。 */
    sca_thread_attr_t attr;
    int ret;
    const int testing = job->action == VOICE_TEST;
    int busy = g_voice_job.active || job->ui->request_inflight;
#if SCA_ENABLE_ASR
    busy = busy || job->ui->asr_active;
#endif
    if (busy) {
        lv_label_set_text(status, "Wait for the current request");
        clear_key((char *)job, sizeof(*job));
        free(job);
        return AGENT_ERROR_BUSY;
    }
#if SCA_ENABLE_TTS
    if (voice_play_state() != VOICE_PLAY_IDLE) {
        lv_label_set_text(status, "Stop playback first");
        clear_key((char *)job, sizeof(*job));
        free(job);
        return AGENT_ERROR_BUSY;
    }
#endif

    g_voice_job.owner = job->ui;
    g_voice_job.status = status;
    g_voice_job.key_input = key_input;
    g_voice_job.controls = v;
    g_voice_job.ready = 0;

    memset(&attr, 0, sizeof(attr));
    attr.name        = "voice-cfg";
    attr.fn          = voice_settings_worker;
    attr.arg         = job;
    attr.stack_bytes = 32768u;
    attr.priority    = 10;
    attr.cpu         = 0xFF;
    g_voice_job.thread = sca_thread_create(&attr);
    ret = g_voice_job.thread ? 0 : -1;

    if (ret) {
        clear_key((char *)job, sizeof(*job));
        free(job);
        lv_label_set_text(status, "Cannot create voice task");
        return AGENT_ERROR;
    }
    g_voice_job.active = 1;
    lv_label_set_text(status, testing ?
        "Testing; speak for about 4s" : "Saving...");
    return AGENT_OK;
}

/* 三个按钮（保存 / 清除密钥 / 测试）共用一个回调：
 * 按钮索引通过 lv_obj_set_user_data 带进来，转成 VOICE_SAVE/KEY/TEST。
 *
 * 这里只做"装配 job"，真正的配置读写与网络测试都在
 * voice_settings_worker 里跑，避免阻塞 LVGL 线程。 */
static void voice_button_cb(lv_event_t *event)
{
    struct voice_controls_s *v = lv_event_get_user_data(event);
    struct voice_job_s *job;
    const char *id = backend_id(v);
    int action = (int)(intptr_t)lv_obj_get_user_data(
        lv_event_get_current_target(event));

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !id) {
        return;
    }
    job = calloc(1, sizeof(*job));
    if (!job) {
        lv_label_set_text(v->status, "Out of memory");
        return;
    }
    job->ui      = v->ui;
    job->service = v->service;
    job->action  = action;
    snprintf(job->backend, sizeof(job->backend), "%s", id);
    if (action != VOICE_TEST) {
        snprintf(job->key, sizeof(job->key), "%s",
                 lv_textarea_get_text(v->key));
        job->update_key = job->key[0] || action == VOICE_KEY;
    }
#if SCA_ENABLE_ASR
    if (v->service == SMART_HOME_SECRET_ASR) {
        smart_home_asr_config_t *c = &job->asr;
        /* 语言下拉的顺序必须与 build 时的 items 一致 */
        const char *langs[] = {"zh", "en", "auto"};
        c->enabled = lv_obj_has_state(v->enabled, LV_STATE_CHECKED);
        snprintf(c->backend_id, sizeof(c->backend_id), "%s", id);
        snprintf(c->model, sizeof(c->model), "%s",
                 lv_textarea_get_text(v->model));
        snprintf(c->host, sizeof(c->host), "%s",
                 lv_textarea_get_text(v->host));
        snprintf(c->path, sizeof(c->path), "%s",
                 lv_textarea_get_text(v->path));
        snprintf(c->port, sizeof(c->port), "%s",
                 lv_textarea_get_text(v->port));
        snprintf(c->language, sizeof(c->language), "%s",
                 langs[lv_dropdown_get_selected(v->option)]);
    }
#endif
#if SCA_ENABLE_TTS
    if (v->service == SMART_HOME_SECRET_TTS) {
        smart_home_tts_config_t *c = &job->tts;
        c->enabled = lv_obj_has_state(v->enabled, LV_STATE_CHECKED);
        snprintf(c->backend_id, sizeof(c->backend_id), "%s", id);
        snprintf(c->model, sizeof(c->model), "%s",
                 lv_textarea_get_text(v->model));
        snprintf(c->host, sizeof(c->host), "%s",
                 lv_textarea_get_text(v->host));
        snprintf(c->path, sizeof(c->path), "%s",
                 lv_textarea_get_text(v->path));
        snprintf(c->port, sizeof(c->port), "%s",
                 lv_textarea_get_text(v->port));
        snprintf(c->voice, sizeof(c->voice), "%s",
                 lv_textarea_get_text(v->option));
        c->sample_rate = 16000;
    }
#endif
    start_job(job, v, v->status, v->key);
}

void smart_home_lvgl_voice_settings_poll(smart_home_lvgl_t *ui)
{
    int ready;
    if (!g_voice_job.active || g_voice_job.owner != ui) return;
    sca_mutex_lock(&g_voice_job_mutex);
    ready = g_voice_job.ready;
    sca_mutex_unlock(&g_voice_job_mutex);
    if (!ready) return;
    if (g_voice_job.thread) {
        sca_thread_join(g_voice_job.thread, 10000);
        sca_thread_destroy(g_voice_job.thread);
        g_voice_job.thread = NULL;
    }
    g_voice_job.active = 0;
    lv_label_set_text(g_voice_job.status, g_voice_job.message);
    if (g_voice_job.key_saved && g_voice_job.key_input)
        lv_textarea_set_text(g_voice_job.key_input, "");
    lv_obj_set_style_text_color(g_voice_job.status, g_voice_job.result == AGENT_OK ?
        SMART_HOME_UI_COLOR_SUCCESS : SMART_HOME_UI_COLOR_DANGER, 0);
    if (g_voice_job.result != AGENT_OK && g_voice_job.controls) {
        int enabled = 0;
#if SCA_ENABLE_ASR
        if (g_voice_job.controls->service == SMART_HOME_SECRET_ASR) {
            smart_home_asr_config_t c; smart_home_asr_config_load(&c); enabled = c.enabled;
        }
#endif
#if SCA_ENABLE_TTS
        if (g_voice_job.controls->service == SMART_HOME_SECRET_TTS) {
            smart_home_tts_config_t c; smart_home_tts_config_load(&c); enabled = c.enabled;
        }
#endif
        if (enabled) lv_obj_add_state(g_voice_job.controls->enabled, LV_STATE_CHECKED);
        else lv_obj_remove_state(g_voice_job.controls->enabled, LV_STATE_CHECKED);
    }
}

void smart_home_lvgl_voice_settings_deinit(smart_home_lvgl_t *ui)
{
    if (g_voice_job.owner == ui) {
        if (g_voice_job.thread) {
            sca_thread_join(g_voice_job.thread, 10000);
            sca_thread_destroy(g_voice_job.thread);
            g_voice_job.thread = NULL;
        }
        memset(&g_voice_job, 0, sizeof(g_voice_job));
    }
    memset(g_voice, 0, sizeof(g_voice));
    memset(&g_voice_page, 0, sizeof(g_voice_page));
}

static void voice_focus_cb(lv_event_t *event)
{
    struct voice_controls_s *v = lv_event_get_user_data(event);
    if (!v || !v->keyboard) return;
    lv_keyboard_set_textarea(v->keyboard, lv_event_get_target(event));
    /* 编辑时覆盖底栏，留出第三行音色输入的完整可视区域。 */
    lv_obj_align(v->keyboard, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_clear_flag(v->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(v->keyboard);
}

static lv_obj_t *voice_input(struct voice_controls_s *v, lv_obj_t *panel,
                              int x, int y, int width, const char *caption,
                              const char *value, unsigned int max_length)
{
    lv_obj_t *label = smart_home_lvgl_label_create(panel, caption,
        SMART_HOME_UI_COLOR_TEXT_MUTED, 13);
    lv_obj_t *input = lv_textarea_create(panel);
    lv_obj_set_pos(label, x, y - 23);
    lv_obj_set_pos(input, x, y);
    lv_obj_set_size(input, width, 44);
    smart_home_lvgl_style_input(input, 16);
    lv_textarea_set_one_line(input, true);
    lv_textarea_set_max_length(input, max_length);
    lv_textarea_set_text(input, value ? value : "");
    lv_obj_add_event_cb(input, voice_focus_cb, LV_EVENT_FOCUSED, v);
    return input;
}

static void voice_select_cb(lv_event_t *event)
{
    struct voice_controls_s *v = lv_event_get_user_data(event);
    const char *host = "", *path = "", *port = "443", *model = "";
    size_t index = lv_dropdown_get_selected(v->backend);
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
#if SCA_ENABLE_ASR
    if (v->service == SMART_HOME_SECRET_ASR) {
        const smart_home_asr_preset_t *p = smart_home_asr_preset_get(index);
        if (!p) return;
        host = p->host; path = p->path; port = p->port; model = p->default_model;
    }
#endif
#if SCA_ENABLE_TTS
    if (v->service == SMART_HOME_SECRET_TTS) {
        const smart_home_tts_preset_t *p = smart_home_tts_preset_get(index);
        if (!p) return;
        host = p->host; path = p->path; port = p->port; model = p->default_model;
        lv_textarea_set_text(v->option, p->default_voice);
    }
#endif
    lv_textarea_set_text(v->host, host); lv_textarea_set_text(v->path, path);
    lv_textarea_set_text(v->port, port); lv_textarea_set_text(v->model, model);
    lv_textarea_set_text(v->key, "");
    lv_label_set_text(v->status, smart_home_secrets_error_text(
        smart_home_secrets_api_key_status(v->service, backend_id(v))));
}

static lv_obj_t *voice_container(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 704, 320);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static lv_obj_t *voice_button(lv_obj_t *parent, int x, int y, int width,
                              const char *caption, bool primary)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 44);
    lv_obj_set_ext_click_area(button, 2);
    smart_home_lvgl_set_bg(button, primary ? SMART_HOME_UI_COLOR_PRIMARY_DARK :
                                                    SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_border_color(button, SMART_HOME_UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(button, primary ? 0 : 1, 0);
    lv_obj_set_style_radius(button, 12, 0);
    smart_home_lvgl_press_feedback(button);
    label = smart_home_lvgl_label_create(button, caption,
        primary ? SMART_HOME_UI_COLOR_SURFACE : SMART_HOME_UI_COLOR_TEXT_PRIMARY, 15);
    lv_obj_center(label);
    return button;
}

static void voice_hide_keyboard(void)
{
    if (g_voice_page.keyboard) {
        lv_obj_add_flag(g_voice_page.keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(g_voice_page.keyboard, NULL);
    }
}

static void voice_connection_cb(lv_event_t *event)
{
    struct voice_controls_s *v = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !v) return;
    bool opening = lv_obj_has_flag(v->connection, LV_OBJ_FLAG_HIDDEN);
    voice_hide_keyboard();
    if (opening) {
        lv_obj_add_flag(v->basic, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(v->connection, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(v->connection, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(v->basic, LV_OBJ_FLAG_HIDDEN);
    }
}

static void voice_tab_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    int selected = (int)(intptr_t)lv_event_get_user_data(event);
    voice_hide_keyboard();
    g_voice_page.selected = selected;
    for (int i = 0; i < 3; ++i) {
        if (!g_voice_page.panel[i]) continue;
        if (i == selected) lv_obj_remove_flag(g_voice_page.panel[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_voice_page.panel[i], LV_OBJ_FLAG_HIDDEN);
        smart_home_lvgl_set_bg(g_voice_page.tabs[i], i == selected ?
            SMART_HOME_UI_COLOR_SURFACE_ON : SMART_HOME_UI_COLOR_SURFACE);
    }
}

#if SCA_ENABLE_TTS
static void voice_stop_cb(lv_event_t *event)
{
    struct voice_controls_s *v = lv_event_get_user_data(event);
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    voice_play_stop();
    lv_label_set_text(v->status, "Stopping playback");
}
#endif

static void build_voice_panel(smart_home_lvgl_t *ui, lv_obj_t *panel,
                              lv_obj_t *keyboard, smart_home_secret_service_t service)
{
    struct voice_controls_s *v = &g_voice[service == SMART_HOME_SECRET_ASR ? 0 : 1];
    const char *id = "mimo", *host = "", *path = "", *port = "443", *model = "";
    const char *option = "";
    char choices[256] = "";
    size_t count = 0, selected = 0;
    int enabled = 0;
    lv_obj_t *label;
#if SCA_ENABLE_ASR
    smart_home_asr_config_t asr;
    if (service == SMART_HOME_SECRET_ASR) {
        smart_home_asr_config_load(&asr);
        id = asr.backend_id; host = asr.host; path = asr.path; port = asr.port;
        model = asr.model; option = asr.language; enabled = asr.enabled;
        count = smart_home_asr_preset_count();
    }
#endif
#if SCA_ENABLE_TTS
    smart_home_tts_config_t tts;
    if (service == SMART_HOME_SECRET_TTS) {
        smart_home_tts_config_load(&tts);
        id = tts.backend_id; host = tts.host; path = tts.path; port = tts.port;
        model = tts.model; option = tts.voice; enabled = tts.enabled;
        count = smart_home_tts_preset_count();
    }
#endif
    v->service = service; v->ui = ui; v->keyboard = keyboard;
    v->basic = voice_container(panel);
    v->connection = voice_container(panel);
    lv_obj_add_flag(v->connection, LV_OBJ_FLAG_HIDDEN);
    panel = v->basic;
    label = smart_home_lvgl_label_create(panel, "Provider", SMART_HOME_UI_COLOR_TEXT_MUTED, 13);
    lv_obj_set_pos(label, 0, 0);
    label = smart_home_lvgl_label_create(panel, "Enable", SMART_HOME_UI_COLOR_TEXT_MUTED, 13);
    lv_obj_set_pos(label, 622, 0);
    v->enabled = lv_switch_create(panel);
    lv_obj_set_size(v->enabled, 54, 30);
    lv_obj_set_pos(v->enabled, 640, 26);
    lv_obj_set_style_bg_color(v->enabled, SMART_HOME_UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(v->enabled, SMART_HOME_UI_COLOR_PRIMARY,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (enabled) lv_obj_add_state(v->enabled, LV_STATE_CHECKED);
    v->backend = lv_dropdown_create(panel);
    lv_obj_set_pos(v->backend, 0, 22); lv_obj_set_size(v->backend, 240, 44);
    for (size_t i = 0; i < count; i++) {
        const char *name = "", *candidate = "";
#if SCA_ENABLE_ASR
        if (service == SMART_HOME_SECRET_ASR) {
            const smart_home_asr_preset_t *p = smart_home_asr_preset_get(i);
            name = p->name; candidate = p->id;
        }
#endif
#if SCA_ENABLE_TTS
        if (service == SMART_HOME_SECRET_TTS) {
            const smart_home_tts_preset_t *p = smart_home_tts_preset_get(i);
            name = p->name; candidate = p->id;
        }
#endif
        if (i) strncat(choices, "\n", sizeof(choices) - strlen(choices) - 1);
        strncat(choices, name, sizeof(choices) - strlen(choices) - 1);
        if (!strcmp(id, candidate)) selected = i;
    }
    lv_dropdown_set_options(v->backend, choices); lv_dropdown_set_selected(v->backend, selected);
    smart_home_lvgl_style_dropdown(v->backend, 16);
    v->model = voice_input(v, panel, 256, 22, 328, "Model", model, 63);
    v->key = voice_input(v, panel, 0, 94, 704, "API key", "", 255);
    lv_textarea_set_password_mode(v->key, true);
    lv_textarea_set_placeholder_text(v->key, "Type a key; leave blank to keep the current one");
    if (service == SMART_HOME_SECRET_ASR) {
        label = smart_home_lvgl_label_create(panel, "Language", SMART_HOME_UI_COLOR_TEXT_MUTED, 13);
        lv_obj_set_pos(label, 0, 143);
        v->option = lv_dropdown_create(panel); lv_obj_set_pos(v->option, 0, 166);
        lv_obj_set_size(v->option, 336, 44); lv_dropdown_set_options(v->option, "Chinese\nEnglish\nAuto");
        lv_dropdown_set_selected(v->option, !strcmp(option, "en") ? 1 : !strcmp(option, "auto") ? 2 : 0);
        smart_home_lvgl_style_dropdown(v->option, 16);
    } else v->option = voice_input(v, panel, 0, 166, 336, "Voice", option, 63);
    lv_obj_t *button = voice_button(panel, 360, 166, 344, "Connection", false);
    lv_obj_add_event_cb(button, voice_connection_cb, LV_EVENT_CLICKED, v);
    v->status = smart_home_lvgl_label_create(panel, smart_home_secrets_error_text(
        smart_home_secrets_api_key_status(service, id)), SMART_HOME_UI_COLOR_TEXT_MUTED, 13);
    lv_obj_set_pos(v->status, 0, 222); lv_obj_set_size(v->status, 704, 40);
    lv_label_set_long_mode(v->status, LV_LABEL_LONG_WRAP);
#if SCA_ENABLE_TTS
    if (service == SMART_HOME_SECRET_TTS) {
        lv_obj_set_width(v->status, 480);
        button = voice_button(panel, 500, 222, 204, "Stop playback", false);
        lv_obj_set_height(button, 36);
        lv_obj_add_event_cb(button, voice_stop_cb, LV_EVENT_CLICKED, v);
    }
#endif
    for (int i = 0; i < 3; i++) {
        const char *titles[] = {"Save", "Clear key", "Test saved config"};
        button = voice_button(panel, i * 240, 274, 224, titles[i], i == VOICE_SAVE);
        if (i == VOICE_SAVE) v->save = button;
        lv_obj_set_user_data(button, (void *)(intptr_t)i);
        lv_obj_add_event_cb(button, voice_button_cb, LV_EVENT_CLICKED, v);
    }
    v->host = voice_input(v, v->connection, 0, 22, 520, "Host", host, 127);
    v->port = voice_input(v, v->connection, 544, 22, 160, "Port", port, 7);
    v->path = voice_input(v, v->connection, 0, 94, 704, "API path", path, 127);
    label = smart_home_lvgl_label_create(v->connection,
        "Connection settings apply on Save.\nGo back to set the key, model, and test.",
        SMART_HOME_UI_COLOR_TEXT_SECONDARY, 15);
    lv_obj_set_pos(label, 0, 166);
    button = voice_button(v->connection, 0, 274, 224, "Back to service", false);
    lv_obj_add_event_cb(button, voice_connection_cb, LV_EVENT_CLICKED, v);
    lv_obj_add_event_cb(v->backend, voice_select_cb, LV_EVENT_VALUE_CHANGED, v);
}

void smart_home_lvgl_build_voice_settings(smart_home_lvgl_t *ui, lv_obj_t *panel,
                                          lv_obj_t *keyboard)
{
    lv_obj_t *label = smart_home_lvgl_label_create(panel, "Voice",
                                                 SMART_HOME_UI_COLOR_TEXT_PRIMARY, 22);
    lv_obj_set_pos(label, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    g_voice_page.keyboard = keyboard;
    int tabs = 2;
#if SCA_ENABLE_KWS
    tabs = 3;
#endif
    for (int i = 0; i < tabs; ++i) {
        const char *names[] = {"Speech input (ASR)", "Speech output (TTS)", "Wake word"};
        g_voice_page.tabs[i] = voice_button(panel, 196 + i * 172, 0, 164, names[i], false);
        lv_obj_set_height(g_voice_page.tabs[i], 40);
        lv_obj_add_event_cb(g_voice_page.tabs[i], voice_tab_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_voice_page.panel[i] = voice_container(panel);
        lv_obj_set_pos(g_voice_page.panel[i], 0, 54);
#if SCA_ENABLE_KWS
        if (i == 2) {
            smart_home_lvgl_build_wake_settings(ui, g_voice_page.panel[i]);
            continue;
        }
#endif
        bool available = false;
#if SCA_ENABLE_ASR
        if (i == 0) available = true;
#endif
#if SCA_ENABLE_TTS
        if (i == 1) available = true;
#endif
        if (available) {
            build_voice_panel(ui, g_voice_page.panel[i], keyboard,
                               i == 0 ? SMART_HOME_SECRET_ASR : SMART_HOME_SECRET_TTS);
        } else {
            label = smart_home_lvgl_label_create(g_voice_page.panel[i],
                i == 0 ? "Speech input is off" : "Speech output is off",
                SMART_HOME_UI_COLOR_TEXT_PRIMARY, 22);
            lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 100);
            label = smart_home_lvgl_label_create(g_voice_page.panel[i],
                "Not built in. Try the other voice service.",
                SMART_HOME_UI_COLOR_TEXT_SECONDARY, 15);
            lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 150);
        }
    }
#if SCA_ENABLE_ASR
    int selected = 0;
#else
    int selected = 1;
#endif
    lv_obj_send_event(g_voice_page.tabs[selected], LV_EVENT_CLICKED, NULL);
}

int smart_home_lvgl_voice_settings_busy(smart_home_lvgl_t *ui)
{
    return g_voice_job.active && g_voice_job.owner == ui;
}
