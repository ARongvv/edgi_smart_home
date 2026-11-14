/* Room metadata is local to this panel. Gateway room configuration is untouched. */
#include "smart_home/ui/lvgl/smart_home_lvgl_internal.h"
#include "smart_home_lvgl_room_dialog.h"
#include "smart_home/config/smart_home_config_store.h"
#include "smart_home/core/smart_home_memory.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "port/sca_os.h"
#if SCA_ENABLE_MILOCO
#include "smart_home/miloco/smart_home_miloco.h"
#endif

typedef struct {
    smart_home_lvgl_t *ui;
    lv_obj_t *mask, *panel, *content, *status, *input, *choice, *keyboard;
    lv_timer_t *timer;
    int content_width;
    int content_height;
    smart_home_state_t state;
    int targets[SMART_HOME_MAX_ROOMS];
    char source[SMART_HOME_ROOM_NAME_SIZE];
    char key[SMART_HOME_V9_KEY_SIZE];
    int was_busy;
} room_dialog_t;
static room_dialog_t *g_room;
static int g_save_attempted;

const char *smart_home_lvgl_room_display(const char *room) {
    if (!room || !room[0])
        return "Unassigned";
    if (!strcmp(room, "living_room"))
        return "Living Room";
    if (!strcmp(room, "bedroom"))
        return "Bedroom";
    return room;
}
static int save_status(int *result) {
    smart_home_device_persistence_status_t status;
    if (!g_room)
        return 0;
    smart_home_device_persistence_status(
        &g_room->ui->app->device_persistence, &status);
    if (result)
        *result = status.last_result;
    return status.busy;
}
static int save_async(room_dialog_t *ctx) {
    int ret = smart_home_device_persistence_request_save(
        &ctx->ui->app->device_persistence);
    g_save_attempted = 1;
    if (ret == AGENT_OK)
        ctx->was_busy = 1;
    return ret;
}
static void status(room_dialog_t *ctx, const char *text, int error) {
    lv_label_set_text(ctx->status, text);
    lv_obj_set_style_text_color(
        ctx->status,
        error ? SMART_HOME_UI_COLOR_DANGER : SMART_HOME_UI_COLOR_TEXT_SECONDARY, 0);
}
static void deleted(lv_event_t *event) {
    room_dialog_t *ctx = lv_event_get_user_data(event);
    lv_timer_delete(ctx->timer);
    if (g_room == ctx)
        g_room = NULL;
    smart_home_bulk_free(ctx);
}
void smart_home_lvgl_room_dialog_close(void) {
    if (g_room)
        lv_obj_delete(g_room->mask);
}
static void close_cb(lv_event_t *event) {
    (void)event;
    smart_home_lvgl_room_dialog_close();
}
static void stop_cb(lv_event_t *event) {
    lv_event_stop_bubbling(event);
}
static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int size) {
    lv_obj_t *label = smart_home_lvgl_label_create(
        parent, value, SMART_HOME_UI_COLOR_TEXT_PRIMARY, size);
    lv_obj_set_pos(label, x, y);
    return label;
}
static lv_obj_t *button(lv_obj_t *parent, const char *name, int x, int y,
                        lv_event_cb_t callback, void *data) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 120, 40);
    lv_obj_set_pos(btn, x, y);
    smart_home_lvgl_set_bg(btn, SMART_HOME_UI_COLOR_SURFACE_ON);
    lv_obj_set_style_radius(btn, 10, 0);
    smart_home_lvgl_press_feedback(btn);
    lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, data);
    lv_obj_center(text(btn, name, 0, 0, 16));
    return btn;
}
static void poll(lv_timer_t *timer) {
    room_dialog_t *ctx = lv_timer_get_user_data(timer);
    int result;
    int busy = save_status(&result);
    if (busy)
        status(ctx, "Saving room config...", 0);
    else if (ctx->was_busy) {
        status(ctx,
               result == AGENT_OK ? "Room config saved"
                                  : "Applied but not saved - save again from the list",
               result != AGENT_OK);
        ctx->was_busy = 0;
        smart_home_lvgl_v9_refresh_views(ctx->ui);
    }
}
static int ready(room_dialog_t *ctx) {
    if (save_status(NULL)) {
        status(ctx, "Saving, try again shortly", 0);
        return 0;
    }
    return 1;
}
static void applied(room_dialog_t *ctx, int result) {
    if (result != AGENT_OK) {
        status(ctx,
               result == AGENT_ERROR_LIMIT
                   ? "Limit reached"
                   : "Not saved: duplicate name, missing room, or bad value",
               1);
        return;
    }
    smart_home_device_service_copy_state(&ctx->ui->app->device_service, &ctx->state);
    smart_home_lvgl_v9_refresh_views(ctx->ui);
    if (save_async(ctx) != AGENT_OK)
        status(ctx, "Applied but not saved - save again from the list", 1);
    else
        status(ctx, "Saving room config...", 0);
}
static void clear_content(room_dialog_t *ctx) {
    lv_obj_clean(ctx->content);
    ctx->input = ctx->choice = ctx->keyboard = NULL;
    smart_home_device_service_copy_state(&ctx->ui->app->device_service, &ctx->state);
}
static void manager(room_dialog_t *ctx);
static void back_cb(lv_event_t *event) {
    manager(lv_event_get_user_data(event));
}
static void retry_cb(lv_event_t *event) {
    room_dialog_t *ctx = lv_event_get_user_data(event);
    if (ready(ctx))
        applied(ctx, AGENT_OK);
}
static void add_cb(lv_event_t *event) {
    room_dialog_t *ctx = lv_event_get_user_data(event);
    if (!ready(ctx))
        return;
    const char *name = lv_textarea_get_text(ctx->input);
    /* Preserve canonical IDs for the two built-in Chinese labels. */
    if (!strcmp(name, "Living Room"))
        name = "living_room";
    if (!strcmp(name, "Bedroom"))
        name = "bedroom";
    int ret = smart_home_device_service_add_room(&ctx->ui->app->device_service, name);
    if (ret == AGENT_OK)
        manager(ctx);
    applied(ctx, ret);
}
static void preset_cb(lv_event_t *event) {
    room_dialog_t *ctx = lv_event_get_user_data(event);
    char name[32];
    lv_dropdown_get_selected_str(ctx->choice, name, sizeof(name));
    lv_textarea_set_text(ctx->input, name);
}
static void add_page_cb(lv_event_t *event) {
    room_dialog_t *ctx = lv_event_get_user_data(event);
    clear_content(ctx);
    int name_width = ctx->content_width * 3 / 5;
    text(ctx->content, "Add room - up to 6, name up to 23 bytes", 0, 0, 16);
    ctx->input = lv_textarea_create(ctx->content);
    lv_obj_set_pos(ctx->input, 0, 34);
    lv_obj_set_size(ctx->input, name_width, 46);
    lv_textarea_set_one_line(ctx->input, true);
    lv_obj_set_style_text_font(ctx->input, smart_home_lvgl_font(18), 0);
    lv_textarea_set_max_length(ctx->input, 23);
    lv_textarea_set_text(ctx->input, "Study");
    ctx->choice = lv_dropdown_create(ctx->content);
    smart_home_lvgl_style_dropdown(ctx->choice, 18);
    lv_obj_set_pos(ctx->choice, name_width + 12, 34);
    lv_obj_set_width(ctx->choice, ctx->content_width - name_width - 12);
    lv_dropdown_set_options(ctx->choice,
                            "Study\nBalcony\nKitchen\nDining\nKids\nGuest\nLiving\nBedroom");
    lv_obj_add_event_cb(ctx->choice, preset_cb, LV_EVENT_VALUE_CHANGED, ctx);
    ctx->keyboard = lv_keyboard_create(ctx->content);
    lv_obj_set_align(ctx->keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ctx->keyboard, 0, 96);
    lv_obj_set_size(ctx->keyboard, lv_pct(100), ctx->content_height - 148);
    lv_keyboard_set_textarea(ctx->keyboard, ctx->input);
    button(ctx->content, "Back", 0, ctx->content_height - 40, back_cb, ctx);
    button(ctx->content, "Add", ctx->content_width - 120, ctx->content_height - 40, add_cb, ctx);
}
static int targets(room_dialog_t *ctx, const char *exclude) {
    char options[SMART_HOME_MAX_ROOMS * (SMART_HOME_ROOM_NAME_SIZE + 1)] = "";
    int count = 0;
    for (int i = 0; i < ctx->state.room_count; i++) {
        if (exclude && !strcmp(exclude, ctx->state.rooms[i]))
            continue;
        if (count)
            strcat(options, "\n");
        strcat(options, smart_home_lvgl_room_display(ctx->state.rooms[i]));
        ctx->targets[count++] = i;
    }
    ctx->choice = lv_dropdown_create(ctx->content);
    smart_home_lvgl_style_dropdown(ctx->choice, 18);
    lv_obj_set_pos(ctx->choice, 0, 120);
    lv_obj_set_size(ctx->choice, lv_pct(100), 48);
    lv_dropdown_set_options(ctx->choice, count ? options : "No rooms available");
    if (!count)
        lv_obj_add_state(ctx->choice, LV_STATE_DISABLED);
    return count;
}
static void delete_confirm_cb(lv_event_t *event) {
    room_dialog_t *ctx = lv_event_get_user_data(event);
    if (!ready(ctx) || ctx->state.room_count <= 1)
        return;
    unsigned selected = lv_dropdown_get_selected(ctx->choice);
    if (selected >= (unsigned)(ctx->state.room_count - 1))
        return;
    char target[SMART_HOME_ROOM_NAME_SIZE];
    snprintf(target, sizeof target, "%s", ctx->state.rooms[ctx->targets[selected]]);
    char keys[SMART_HOME_ROOM_BINDINGS][SMART_HOME_ROOM_KEY_SIZE];
    size_t count = 0;
#if SCA_ENABLE_MILOCO
    if (ctx->ui->app->miloco) {
        smart_home_miloco_device_t *devices =
            smart_home_bulk_alloc(sizeof(*devices) * SMART_HOME_MILOCO_MAX_DEVICES);
        if (!devices) {
            status(ctx, "Out of memory, room not deleted", 1);
            return;
        }
        smart_home_device_service_copy_state(&ctx->ui->app->device_service,
                                             &ctx->state);
        size_t total = smart_home_miloco_list(ctx->ui->app->miloco, devices,
                                              SMART_HOME_MILOCO_MAX_DEVICES, NULL);
        for (size_t i = 0; i < total && i < SMART_HOME_MILOCO_MAX_DEVICES &&
                           count < SMART_HOME_ROOM_BINDINGS;
             i++) {
            char key[SMART_HOME_ROOM_KEY_SIZE];
            snprintf(key, sizeof key, "milo:%s", devices[i].did);
            const char *room = smart_home_device_room_override(&ctx->state, key);
            if (!room)
                room = devices[i].room;
            if (!strcmp(smart_home_lvgl_room_display(room),
                        smart_home_lvgl_room_display(ctx->source)))
                snprintf(keys[count++], SMART_HOME_ROOM_KEY_SIZE, "%s", key);
        }
        smart_home_bulk_free(devices);
    }
#endif
    int ret = smart_home_device_service_delete_room_with_keys(
        &ctx->ui->app->device_service, ctx->source, target, keys, count);
    if (ret == AGENT_OK)
        manager(ctx);
    applied(ctx, ret);
}
static void delete_page_cb(lv_event_t *event) {
    room_dialog_t *ctx = g_room;
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if (!ctx || !ready(ctx) || index < 0 || index >= ctx->state.room_count)
        return;
    snprintf(ctx->source, sizeof ctx->source, "%s", ctx->state.rooms[index]);
    clear_content(ctx);
    char message[160];
    snprintf(message, sizeof message, "Before removing \"%s\", pick a room for its devices.",
             smart_home_lvgl_room_display(ctx->source));
    lv_obj_set_width(text(ctx->content, message, 0, 8, 20), lv_pct(100));
    text(ctx->content, "Local and discovered devices move together.", 0,
         64, 16);
    int count = targets(ctx, ctx->source);
    button(ctx->content, "Cancel", 0, ctx->content_height - 40, back_cb, ctx);
    lv_obj_t *confirm =
        button(ctx->content, "Delete", ctx->content_width - 120, ctx->content_height - 40, delete_confirm_cb, ctx);
    if (!count) {
        lv_obj_add_state(confirm, LV_STATE_DISABLED);
        status(ctx, "Keep at least one room", 1);
    }
}
static void manager(room_dialog_t *ctx) {
    clear_content(ctx);
    for (int i = 0; i < ctx->state.room_count; i++) {
        int count = 0;
        for (int j = 0; j < SMART_HOME_MAX_DEVICES; j++)
            if (ctx->state.devices[j].used &&
                !strcmp(ctx->state.devices[j].room, ctx->state.rooms[i]))
                count++;
        for (int j = 0; j < SMART_HOME_ROOM_BINDINGS; j++)
            if (ctx->state.room_bindings[j].key[0] &&
                !strcmp(ctx->state.room_bindings[j].room, ctx->state.rooms[i]))
                count++;
        char line[96];
        snprintf(line, sizeof line, "%s - %d devices",
                 smart_home_lvgl_room_display(ctx->state.rooms[i]), count);
        text(ctx->content, line, 0, i * 44 + 8, 18);
        button(ctx->content, "Delete", ctx->content_width - 120, i * 44, delete_page_cb, (void *)(intptr_t)i);
    }
    lv_obj_t *add = button(ctx->content, "Add room", 0, ctx->content_height - 40, add_page_cb, ctx);
    if (ctx->state.room_count >= SMART_HOME_MAX_ROOMS)
        lv_obj_add_state(add, LV_STATE_DISABLED);
    button(ctx->content, "Save again", ctx->content_width - 120, ctx->content_height - 40, retry_cb, ctx);
}
static void assign_cb(lv_event_t *event) {
    room_dialog_t *ctx = lv_event_get_user_data(event);
    if (!ready(ctx))
        return;
    unsigned selected = lv_dropdown_get_selected(ctx->choice);
    if (selected >= (unsigned)ctx->state.room_count)
        return;
    applied(ctx, smart_home_device_service_assign_room(
                     &ctx->ui->app->device_service, ctx->key,
                     ctx->state.rooms[ctx->targets[selected]]));
}
static room_dialog_t *open_dialog(smart_home_lvgl_t *ui, const char *title) {
    smart_home_lvgl_room_dialog_close();
    if (!ui || !ui->app || !ui->screen_home ||
        !ui->app->device_service.mutex_initialized)
        return NULL;
    room_dialog_t *ctx = smart_home_bulk_alloc(sizeof(*ctx));
    if (!ctx)
        return NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ui = ui;
    g_room = ctx;
    ctx->mask = lv_obj_create(ui->screen_home);
    lv_obj_remove_style_all(ctx->mask);
    lv_obj_set_size(ctx->mask, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(ctx->mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ctx->mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->mask, LV_OPA_30, 0);
    lv_obj_add_event_cb(ctx->mask, close_cb, LV_EVENT_CLICKED, NULL);
    ctx->panel = lv_obj_create(ctx->mask);
    lv_obj_remove_style_all(ctx->panel);
    smart_home_lvgl_fit_dialog(ctx->panel, 840, 480);
    smart_home_lvgl_card_style(ctx->panel);
    lv_obj_set_style_radius(ctx->panel, 18, 0);
    lv_obj_set_style_pad_all(ctx->panel, 16, 0);
    lv_obj_update_layout(ctx->panel);
    ctx->content_width = lv_obj_get_content_width(ctx->panel);
    ctx->content_height = lv_obj_get_content_height(ctx->panel) - 86;
    lv_obj_add_event_cb(ctx->panel, stop_cb, LV_EVENT_CLICKED, NULL);
    text(ctx->panel, title, 0, 0, 24);
    button(ctx->panel, "Off", ctx->content_width - 120, 0, close_cb, NULL);
    ctx->content = lv_obj_create(ctx->panel);
    lv_obj_remove_style_all(ctx->content);
    lv_obj_set_pos(ctx->content, 0, 52);
    lv_obj_set_size(ctx->content, ctx->content_width, ctx->content_height);
    lv_obj_clear_flag(ctx->content, LV_OBJ_FLAG_SCROLLABLE);
    ctx->status = text(ctx->panel, "", 0, 0, 14);
    lv_obj_set_width(ctx->status, lv_pct(100));
    lv_obj_align(ctx->status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    ctx->timer = lv_timer_create(poll, 200, ctx);
    lv_obj_add_event_cb(ctx->mask, deleted, LV_EVENT_DELETE, ctx);
    int result;
    ctx->was_busy = save_status(&result);
    if (g_save_attempted && !ctx->was_busy)
        status(ctx,
               result == AGENT_OK ? "Room config saved"
                                  : "Earlier save failed - save again from the list",
               result != AGENT_OK);
    return ctx;
}
void smart_home_lvgl_room_dialog_open(smart_home_lvgl_t *ui) {
    room_dialog_t *ctx = open_dialog(ui, "Rooms");
    if (ctx)
        manager(ctx);
}
void smart_home_lvgl_room_assign_open(smart_home_lvgl_t *ui,
                                      const smart_home_v9_device_view_t *view) {
    if (!view || view->source == SMART_HOME_V9_SOURCE_DEMO)
        return;
    room_dialog_t *ctx = open_dialog(ui, "Assign room");
    if (!ctx)
        return;
    clear_content(ctx);
    snprintf(ctx->key, sizeof ctx->key, "%s", view->key);
    lv_obj_set_width(text(ctx->content, view->name, 0, 8, 20), lv_pct(100));
    text(ctx->content,
         view->source == SMART_HOME_V9_SOURCE_MILOCO
             ? "Only affects this screen, not Mi Home rooms."
             : "Assign devices to a room:",
         0, 64, 16);
    targets(ctx, NULL);
    for (int i = 0; i < ctx->state.room_count; i++)
        if (!strcmp(ctx->state.rooms[i], view->room_id))
            lv_dropdown_set_selected(ctx->choice, i);
    button(ctx->content, "Assign", ctx->content_width - 120, ctx->content_height - 40, assign_cb, ctx);
}
