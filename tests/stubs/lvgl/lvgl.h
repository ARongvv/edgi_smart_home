/*
 * 宿主测试桩：lvgl/lvgl.h
 *
 * 只声明 UI 层引用的 LVGL 符号子集，用于语法检查。
 * 不提供任何真实行为 —— UI 的验证必须在板上或带 LVGL 的 sim 环境做。
 */
#ifndef __STUB_LVGL_H__
#define __STUB_LVGL_H__
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_display_t lv_display_t;
typedef struct _lv_indev_t lv_indev_t;
typedef struct _lv_timer_t lv_timer_t;
typedef struct _lv_event_t lv_event_t;
typedef struct _lv_font_t lv_font_t;
typedef struct _lv_image_dsc_t lv_image_dsc_t;
typedef int32_t lv_coord_t;

typedef struct { lv_coord_t x1, y1, x2, y2; } lv_area_t;
typedef struct { lv_coord_t x, y; } lv_point_t;
typedef struct { lv_point_t point; uint32_t state; uint32_t key; } lv_indev_data_t;
typedef struct { lv_coord_t x, y; } lv_point_precise_t;

#define LV_INDEV_STATE_RELEASED 0
#define LV_INDEV_STATE_PRESSED  1
#define LV_INDEV_TYPE_POINTER   1
#define LV_COLOR_FORMAT_RGB565  0x12
#define LV_DISPLAY_RENDER_MODE_PARTIAL 0
#define LV_ALIGN_CENTER 0
#define LV_ALIGN_BOTTOM_MID 1
#define LV_FLEX_FLOW_ROW 1
#define LV_FLEX_FLOW_COLUMN 2
#define LV_FLEX_ALIGN_START 0
#define LV_FLEX_ALIGN_CENTER 1
#define LV_FLEX_ALIGN_SPACE_EVENLY 2
#define LV_FLEX_ALIGN_SPACE_BETWEEN 3
#define LV_OPA_TRANSP 0
#define LV_OPA_COVER 255
#define LV_BORDER_SIDE_TOP 1
#define LV_STATE_PRESSED 1
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_EVENT_CLICKED 1

typedef struct { int blue, green, red; } lv_color_t;

static inline int lv_pct(int x) { return x; }
static inline void lv_obj_set_flex_flow(lv_obj_t *o, int f) { (void)o;(void)f; }
static inline void lv_obj_set_flex_align(lv_obj_t *o, int a, int b, int c)
{ (void)o;(void)a;(void)b;(void)c; }
static inline void lv_obj_set_style_pad_column(lv_obj_t *o, int v, int s) { (void)o;(void)v;(void)s; }
static inline void lv_obj_set_style_pad_hor(lv_obj_t *o, int v, int s) { (void)o;(void)v;(void)s; }

typedef void (*lv_event_cb_t)(lv_event_t *e);
typedef void (*lv_display_flush_cb_t)(lv_display_t *, const lv_area_t *, uint8_t *);
typedef void (*lv_indev_read_cb_t)(lv_indev_t *, lv_indev_data_t *);

static inline void lv_init(void) {}
static inline bool lv_is_initialized(void) { return false; }
static inline void lv_deinit(void) {}
static inline uint32_t lv_tick_get(void) { return 0; }
static inline uint32_t lv_tick_elaps(uint32_t t) { (void)t; return 0; }
static inline uint32_t lv_timer_handler(void) { return 10; }
static inline void lv_memzero(void *p, size_t n) { memset(p, n); }

static inline lv_display_t *lv_display_create(int w, int h) { (void)w;(void)h; return NULL; }
static inline void lv_display_set_color_format(lv_display_t *d, int f) { (void)d;(void)f; }
static inline void lv_display_set_buffers(lv_display_t *d, void *a, void *b, size_t n, int m)
{ (void)d;(void)a;(void)b;(void)n;(void)m; }
static inline void lv_display_set_flush_cb(lv_display_t *d, lv_display_flush_cb_t cb) { (void)d;(void)cb; }
static inline void lv_display_set_user_data(lv_display_t *d, void *u) { (void)d;(void)u; }
static inline void *lv_display_get_user_data(lv_display_t *d) { (void)d; return NULL; }
static inline void lv_display_delete(lv_display_t *d) { (void)d; }
static inline void lv_display_flush_ready(lv_display_t *d) { (void)d; }
static inline lv_display_t *lv_display_get_default(void) { return NULL; }
static inline int32_t lv_display_get_horizontal_resolution(lv_display_t *d) { (void)d; return 800; }
static inline int32_t lv_display_get_vertical_resolution(lv_display_t *d) { (void)d; return 480; }

static inline lv_indev_t *lv_indev_create(void) { return NULL; }
static inline void lv_indev_set_type(lv_indev_t *i, int t) { (void)i;(void)t; }
static inline void lv_indev_set_read_cb(lv_indev_t *i, lv_indev_read_cb_t cb) { (void)i;(void)cb; }
static inline void lv_indev_set_display(lv_indev_t *i, lv_display_t *d) { (void)i;(void)d; }
static inline void lv_indev_delete(lv_indev_t *i) { (void)i; }

#endif
