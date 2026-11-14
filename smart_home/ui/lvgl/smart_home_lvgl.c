/**
 * smart_home LVGL UI runtime entry.
 */

#include "smart_home/ui/lvgl/smart_home_lvgl_internal.h"
#include "smart_home_lvgl_camera_dialog.h"
#include "smart_home/core/smart_home_cpu_debug.h"
#include "smart_home/core/smart_home_memory.h"

#include "port/sca_config.h"

#include "smart_home/core/smart_home_app.h"
#include "port/sca_display.h"
#include "port/sca_os.h"

#include <stdio.h>
#include <stdlib.h>


#define SMART_HOME_LVGL_QUEUE_POLL_MS 10u

#if SCA_ENABLE_DEBUG_LOG
static void smart_home_lvgl_log_build_stage(const char *stage,
                                            const char *phase)
{
    printf("[smart_home_lvgl] build %s %s\n", stage, phase);
}
#else
#define smart_home_lvgl_log_build_stage(stage, phase) ((void)0)
#endif

#if 0 /* NuttX mallinfo 专有 */
/* 给性能行打上当前页面标签，板端一份日志就能按场景分组回填 P01-P12。
 * 弹窗叠加在所在页面上，不单独计数。
 */
static const char *v9_perf_scene(smart_home_lvgl_t *ui)
{
    lv_obj_t *active;

    if (!ui) {
        return "none";
    }
    active = lv_screen_active();
    if (active == ui->screen_home) return "home";
    if (active == ui->screen_chat) return "chat";
    if (active == ui->screen_settings_v9) return "settings";
    if (active == ui->screen_screensaver) return "screensaver";
#if SCA_ENABLE_MILOCO
    if (smart_home_lvgl_miloco_bind_is_open(ui)) return "miloco-bind";
#endif
    return "other";
}
#endif

smart_home_lvgl_t *smart_home_lvgl_init(smart_home_agent_app_t *app)
{
    smart_home_lvgl_t *ui;

    ui = (smart_home_lvgl_t *)calloc(1, sizeof(*ui));
    if (!ui) {
        return NULL;
    }

    ui->app = app;
    ui->device_state = app ? &app->device_state : NULL;
    ui->active_tab = SMART_HOME_TAB_HOME;

    if (sca_mutex_init(&ui->pending_mutex, NULL) != 0) {
        free(ui);
        return NULL;
    }

    smart_home_lvgl_build_screensaver_screen(ui);

    smart_home_lvgl_build_home_screen_v9(ui);

    /* V9 camera preview is an on-demand popup. Do not allocate the old
     * security page and its second RGB565 preview buffer at startup. */

    smart_home_lvgl_build_chat_screen(ui);
    (void)smart_home_lvgl_chat_restore(ui);

    smart_home_lvgl_build_settings_screen_v9(ui);

#if SCA_ENABLE_KWS
    /* 语音会话联动：KWS 常驻监听（kws.json enabled 时）+
     * TTS 播报期间自动暂停推理。 */
    smart_home_lvgl_voice_session_start(ui);
#endif
#if SCA_ENABLE_REMINDERS
    smart_home_lvgl_reminder_init(ui);
#endif
    return ui;
}

void smart_home_lvgl_set_app(smart_home_lvgl_t *ui,
                             smart_home_agent_app_t *app)
{
    if (!ui) {
        return;
    }

    ui->app = app;
    ui->device_state = app ? &app->device_state : NULL;
}

void smart_home_lvgl_show(smart_home_lvgl_t *ui)
{
    if (!ui || !ui->screen_screensaver) {
        return;
    }

    lv_scr_load(ui->screen_screensaver);
}

void smart_home_lvgl_deinit(smart_home_lvgl_t *ui)
{
    if (!ui) {
        return;
    }

#if SCA_ENABLE_REMINDERS
    smart_home_lvgl_reminder_deinit(ui);
#endif
    smart_home_lvgl_camera_dialog_close();
    smart_home_lvgl_settings_v9_deinit(ui);
    if (ui->agent_worker_active && ui->agent_worker) {
        /* sca_thread_join 等函数返回，随后必须 destroy 回收栈与句柄 */
        sca_thread_join(ui->agent_worker, 10000);
        sca_thread_destroy(ui->agent_worker);
        ui->agent_worker = NULL;
        ui->agent_worker_active = 0;
    }
    (void)smart_home_agent_app_set_observer(ui->app, NULL, NULL);
    smart_home_lvgl_discard_pending(ui);
#if SCA_ENABLE_MILOCO
    if (ui->miloco_timer) {
        lv_timer_delete(ui->miloco_timer);
        ui->miloco_timer = NULL;
    }
    if (ui->bind_timer) {
        lv_timer_delete(ui->bind_timer);
        ui->bind_timer = NULL;
    }
#endif
    if (ui->home_time_timer) {
        lv_timer_delete(ui->home_time_timer);
        ui->home_time_timer = NULL;
    }
    if (ui->v9_poll_timer) {
        lv_timer_delete(ui->v9_poll_timer);
        ui->v9_poll_timer = NULL;
    }
    if (ui->screen_screensaver) {
        lv_obj_del(ui->screen_screensaver);
    }
    if (ui->screen_home) {
        lv_obj_del(ui->screen_home);
    }
    if (ui->screen_chat) {
        lv_obj_del(ui->screen_chat);
    }
    if (ui->screen_settings_v9) {
        lv_obj_del(ui->screen_settings_v9);
    }
    sca_mutex_destroy(&ui->pending_mutex);
    free(ui);
}


int smart_home_lvgl_run(smart_home_agent_app_t *app,
                        const char *initial_input)
{
    sca_display_bind_t bind;
    smart_home_lvgl_t *ui;

    if (!app) {
        return AGENT_ERROR_INVALID;
    }

    if (lv_is_initialized()) {
        return AGENT_ERROR_BUSY;
    }

    smart_home_cpu_debug_log("lvgl-run-start");

    lv_init();
#if SCA_ENABLE_DEBUG_LOG
    printf("[lvgl] lv_init done\n");
#endif
    smart_home_lvgl_style_init();
    /* 显示与触摸绑定统一走 sca_display。
     * 失败时不返回错误码而是返回 NULL disp，与原语义一致。 */
    if (sca_display_bind(&bind) != SCA_OK) {
        smart_home_lvgl_style_deinit();
        lv_deinit();
        return AGENT_ERROR;
    }

    SCA_LOGI("lvgl", "disp=%p indev=%p %dx%d",
             bind.disp, bind.indev, bind.width, bind.height);

    ui = smart_home_lvgl_init(app);
    if (!ui) {
#if SCA_ENABLE_DEBUG_LOG
        printf("[lvgl] ui init FAILED\n");
#endif
        sca_display_unbind(&bind);
        smart_home_lvgl_style_deinit();
        lv_deinit();
        return AGENT_ERROR_NOMEM;
    }
#if SCA_ENABLE_DEBUG_LOG
    printf("[lvgl] ui init done\n");
#endif

    smart_home_agent_app_set_observer(app, smart_home_lvgl_event_cb, ui);
    smart_home_lvgl_show(ui);
#if SCA_ENABLE_DEBUG_LOG
    printf("[lvgl] show done, entering run loop\n");
#endif

    if (initial_input && initial_input[0]) {
        ui->active_tab = SMART_HOME_TAB_CHAT;
        lv_scr_load(ui->screen_chat);
        smart_home_lvgl_chat_send_text(ui, initial_input);
    }

    /* 主循环：LVGL 标准三步 —— 处理待投递任务、跑计时器、按需休眠。
     * 只保留裸 for 事件循环（RT-Thread 无 libuv）。 */
    smart_home_cpu_debug_log("lvgl-loop-start");
    while (!smart_home_app_exiting()) {
        uint32_t idle;

        smart_home_lvgl_process_pending(ui);
        idle = lv_timer_handler();
        smart_home_lvgl_process_pending(ui);

        if (idle == 0 || idle > SMART_HOME_LVGL_QUEUE_POLL_MS) {
            idle = SMART_HOME_LVGL_QUEUE_POLL_MS;
        }
        sca_sleep_ms(idle ? idle : 1u);
    }
#if SCA_ENABLE_DEBUG_LOG
    printf("[lvgl] run loop exited\n");
#endif

    smart_home_lvgl_deinit(ui);
    sca_display_unbind(&bind);
    smart_home_lvgl_style_deinit();
    lv_deinit();
    return AGENT_OK;
}
