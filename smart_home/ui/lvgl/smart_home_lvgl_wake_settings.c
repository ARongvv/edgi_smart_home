/* SPDX-License-Identifier: Apache-2.0 */
#include "smart_home/ui/lvgl/smart_home_lvgl_internal.h"
#if SCA_ENABLE_KWS
#include "smart_home/voice/smart_home_kws_service.h"
#include <stdlib.h>
#include <string.h>

static struct {
    smart_home_lvgl_t *owner;
    lv_obj_t *toggle, *status;
    pthread_t thread;
    smart_home_kws_config_t config;
    int active, ready, result, applied;
} g_wake;

static void *wake_save_worker(void *argument)
{
    smart_home_lvgl_t *ui = argument;
    int ret = smart_home_kws_config_save(&g_wake.config);
    sca_mutex_lock(&ui->pending_mutex);
    g_wake.result = ret;
    g_wake.ready = 1;
    sca_mutex_unlock(&ui->pending_mutex);
    return NULL;
}

static void wake_switch_cb(lv_event_t *event)
{
    smart_home_lvgl_t *ui = lv_event_get_user_data(event);
    pthread_attr_t attr;
    int ret;
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || g_wake.active) return;
    g_wake.config.enabled = lv_obj_has_state(g_wake.toggle, LV_STATE_CHECKED);
    g_wake.active = 1;
    ret = pthread_attr_init(&attr);
    if (ret == 0) {
        ret = pthread_attr_setstacksize(&attr, 16384u);
        if (ret == 0) ret = pthread_create(&g_wake.thread, &attr, wake_save_worker, ui);
        pthread_attr_destroy(&attr);
    }
    if (ret != 0) {
        g_wake.active = 0;
        if (g_wake.applied) lv_obj_add_state(g_wake.toggle, LV_STATE_CHECKED);
        else lv_obj_remove_state(g_wake.toggle, LV_STATE_CHECKED);
        lv_label_set_text(g_wake.status, "Cannot start save task");
        return;
    }
    lv_obj_add_state(g_wake.toggle, LV_STATE_DISABLED);
    lv_label_set_text(g_wake.status, "Saving wake settings...");
}

void smart_home_lvgl_wake_settings_poll(smart_home_lvgl_t *ui)
{
    int ready;
    if (g_wake.owner != ui) return;
    sca_mutex_lock(&ui->pending_mutex);
    ready = g_wake.ready;
    g_wake.ready = 0;
    sca_mutex_unlock(&ui->pending_mutex);
    if (!ready) return;
    pthread_join(g_wake.thread, NULL);
    g_wake.active = 0;
    lv_obj_remove_state(g_wake.toggle, LV_STATE_DISABLED);
    if (g_wake.result == AGENT_OK) {
        g_wake.applied = g_wake.config.enabled;
        if (kws_service_running()) {
            kws_service_set_paused(!g_wake.applied);
            lv_label_set_text(g_wake.status, g_wake.applied ? "Wake word on" : "Wake word off");
        } else lv_label_set_text(g_wake.status, g_wake.applied ?
            "Saved. Wake service starts after reboot." : "Wake word off");
    } else {
        if (g_wake.applied) lv_obj_add_state(g_wake.toggle, LV_STATE_CHECKED);
        else lv_obj_remove_state(g_wake.toggle, LV_STATE_CHECKED);
        lv_label_set_text(g_wake.status, "Save failed, reverted to previous settings");
    }
}

void smart_home_lvgl_build_wake_settings(smart_home_lvgl_t *ui, lv_obj_t *panel)
{
    g_wake.owner = ui;
    smart_home_kws_config_load(&g_wake.config);
    g_wake.applied = g_wake.config.enabled;
    lv_obj_t *label = smart_home_lvgl_label_create(panel, "Wake word",
        SMART_HOME_UI_COLOR_TEXT_PRIMARY, 20);
    lv_obj_set_pos(label, 0, 16);
    label = smart_home_lvgl_label_create(panel,
        "Listens for the wake word. If off at boot, needs a restart.",
        SMART_HOME_UI_COLOR_TEXT_SECONDARY, 14);
    lv_obj_set_pos(label, 0, 64);
    lv_obj_set_width(label, 610);
    g_wake.toggle = lv_switch_create(panel);
    lv_obj_set_size(g_wake.toggle, 54, 30);
    lv_obj_set_pos(g_wake.toggle, 640, 20);
    if (g_wake.applied) lv_obj_add_state(g_wake.toggle, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_wake.toggle, wake_switch_cb, LV_EVENT_VALUE_CHANGED, ui);
    g_wake.status = smart_home_lvgl_label_create(panel, g_wake.applied ?
        (kws_service_running() ? "Wake service running" : "Wake service stopped") : "Wake word off",
        SMART_HOME_UI_COLOR_TEXT_MUTED, 14);
    lv_obj_set_pos(g_wake.status, 0, 140);
}

void smart_home_lvgl_wake_settings_deinit(smart_home_lvgl_t *ui)
{
    if (g_wake.owner != ui) return;
    if (g_wake.active) pthread_join(g_wake.thread, NULL);
    memset(&g_wake, 0, sizeof(g_wake));
}
#endif
