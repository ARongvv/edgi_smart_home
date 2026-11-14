/** SmartHome V9 three-item product navigation. */

#include "smart_home/ui/lvgl/smart_home_lvgl_internal.h"
#include "smart_home_lvgl_camera_dialog.h"
#include "smart_home/ui/lvgl/images/smart_home_icons.h"
#if SCA_ENABLE_MILOCO
#include "smart_home/miloco/smart_home_miloco.h"
#endif

#include <stdint.h>

static const char *const g_nav_titles[SMART_HOME_NAV_COUNT] = {
    "Home", "Chat", "Settings"
};

static const char *const g_nav_icons[SMART_HOME_NAV_COUNT] = {
    ICON_NAV_HOME, ICON_NAV_CHAT, ICON_NAV_SETTINGS
};

static int tab_for_screen(const smart_home_lvgl_t *ui, const lv_obj_t *screen)
{
    if (!ui || !screen) return SMART_HOME_TAB_HOME;
    if (screen == ui->screen_chat) return SMART_HOME_TAB_CHAT;
    if (screen == ui->screen_settings_v9) return SMART_HOME_TAB_SETTINGS;
    return SMART_HOME_TAB_HOME;
}

void smart_home_lvgl_load_tab(smart_home_lvgl_t *ui, int tab)
{
    lv_obj_t *screen = NULL;

    if (!ui || tab < 0 || tab >= SMART_HOME_TAB_COUNT) {
        return;
    }
    switch (tab) {
    case SMART_HOME_TAB_HOME:     screen = ui->screen_home; break;
    case SMART_HOME_TAB_CHAT:     screen = ui->screen_chat; break;
    case SMART_HOME_TAB_SETTINGS:
        screen = ui->screen_settings_v9;
        break;
    default: break;
    }
    if (!screen) return;

#if SCA_ENABLE_MILOCO
    /* Device polling follows the pages that show device cards. */
    smart_home_lvgl_miloco_poll_set_enabled(
        ui, tab == SMART_HOME_TAB_HOME);
#endif
    if (tab != SMART_HOME_TAB_HOME) smart_home_lvgl_camera_dialog_close();
    ui->active_tab = tab;
    if (tab == SMART_HOME_TAB_HOME) {
        smart_home_lvgl_refresh_home(ui);
    }

    if (lv_scr_act() == screen) {
        return;
    }

    /* V9 removes full-screen slide transitions: switching tabs is an instant
     * state change, keeping the 33 ms software renderer budget for content. */
    if (lv_scr_act() == ui->screen_screensaver) {
        lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 220, 0, false);
    } else {
        lv_scr_load(screen);
    }
}

static void nav_btn_click(lv_event_t *event)
{
    smart_home_lvgl_t *ui = lv_event_get_user_data(event);
    int tab = (int)(intptr_t)lv_obj_get_user_data(
        lv_event_get_current_target(event));

    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !ui) {
        return;
    }
    smart_home_lvgl_load_tab(ui, tab);
}

/* The icon/text container is the actual hit target on some LVGL input paths. */
static void nav_content_press_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED) {
        lv_obj_add_state(button, LV_STATE_PRESSED);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_remove_state(button, LV_STATE_PRESSED);
    }
}

static lv_obj_t *nav_item_content(lv_obj_t *button, const char *icon_name,
                                  const char *title, lv_color_t color)
{
    lv_obj_t *content = lv_obj_create(button);
    lv_obj_t *icon;
    lv_obj_t *label;

    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), SMART_HOME_NAV_ITEM_H);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);

    /* 图标 + 文字用 flex 横向排布并居中，替代原来写死的 (58,10)/(102,14)。
     * 那组坐标是按 216 宽算的，换屏后图标会偏向一侧。 */
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(content, 10, 0);

    icon = smart_home_lvgl_icon_create(content, icon_name, 32, 32);
    if (icon) {
        smart_home_lvgl_icon_color(icon, color);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    }

    label = smart_home_lvgl_label_create(content, title, color, 17);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);

    return content;
}

lv_obj_t *smart_home_lvgl_build_nav_bar(lv_obj_t *screen,
                                        smart_home_lvgl_t *ui)
{
    lv_obj_t *bar;
    lv_obj_t *content;
    int active = tab_for_screen(ui, screen);

    if (!screen) {
        return NULL;
    }

    bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, smart_home_lvgl_disp_w(), SMART_HOME_NAV_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    smart_home_lvgl_set_bg(bar, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, SMART_HOME_UI_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    content = lv_obj_create(bar);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);

    /* 横向 flex：三个导航项等分屏宽并居中。
     * 替代原 NAV_X0/NAV_STEP 绝对定位 —— 那两个常量是按 1024 宽算的，
     * 156 + 3×216 + 2×32 = 1024 正好对称，换到 800 就全错。 */
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(content, 40, 0);

    for (int i = 0; i < SMART_HOME_NAV_COUNT; i++) {
        lv_obj_t *button = lv_btn_create(content);
        lv_obj_t *button_content;
        lv_color_t color = i == active ? SMART_HOME_UI_COLOR_TEXT_PRIMARY :
                                         SMART_HOME_UI_COLOR_TEXT_SECONDARY;

        lv_obj_remove_style_all(button);
        /* 宽度由 flex 分配（占满剩余空间的三分之一），只固定高度 */
        lv_obj_set_size(button, (smart_home_lvgl_disp_w() - 80) / 3,
                        SMART_HOME_NAV_ITEM_H);
        lv_obj_set_style_radius(button, 14, 0);
        lv_obj_set_ext_click_area(button, 2);
        lv_obj_set_user_data(button, (void *)(intptr_t)i);
        lv_obj_add_event_cb(button, nav_btn_click, LV_EVENT_CLICKED, ui);
        smart_home_lvgl_press_feedback(button);
        lv_obj_set_style_bg_color(button, SMART_HOME_UI_COLOR_SURFACE_ON,
                                  LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
        if (i == active) {
            smart_home_lvgl_set_bg(button, SMART_HOME_UI_COLOR_SURFACE_ON);
        } else {
            lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        }

        button_content = nav_item_content(button, g_nav_icons[i],
                                          g_nav_titles[i], color);
        lv_obj_set_user_data(button_content, (void *)(intptr_t)i);
        lv_obj_add_event_cb(button_content, nav_content_press_cb,
                            LV_EVENT_PRESSED, button);
        lv_obj_add_event_cb(button_content, nav_content_press_cb,
                            LV_EVENT_RELEASED, button);
        lv_obj_add_event_cb(button_content, nav_content_press_cb,
                            LV_EVENT_PRESS_LOST, button);
        lv_obj_add_event_cb(button_content, nav_btn_click,
                            LV_EVENT_CLICKED, ui);
    }

    /* The test and callers expect the outer bar to own the content row. */
    return content;
}
