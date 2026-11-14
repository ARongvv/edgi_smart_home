/**
 * smart_home LVGL UI style resources.
 *
 * ★ 字体策略：只用 LVGL 内置 Montserrat，不加载外部 TTF ★
 *
 * 不从 /res/fonts/MiSans-Normal.ttf 预加载整个字体到堆里（7.9MB），
 * 再用 TinyTTF 按 20 个字号各建一个实例。问题有两个：
 *
 *   1. 空间：7.9MB 字体 + 20 个实例的字形缓存，在 16MB HyperRAM 里
 *      要和 1.5MB 帧缓冲、LVGL 绘制缓冲、模型响应缓冲抢地方；
 *   2. 速度：TinyTTF 对 LittleFS 做大量小块 seek 定位 CJK 字形，
 *      冷启动要几秒，期间 UI 空白。
 *
 * 现在 UI 文案已全部英文化（见 tools/ui_strings_en.py），只需要 ASCII
 * 字形，用内置 Montserrat 就够：编译进 .rodata，零加载时间、零堆占用。
 *
 * 代价是字号受限于内置的 8/10/12/14/16/18/20/22/24/26/28/30/32/34/36/
 * 38/40/42/44/46/48。超出就近向下取（见 smart_home_lvgl_font）。
 */

#include "smart_home/ui/lvgl/smart_home_lvgl_style.h"
#include "smart_home/ui/lvgl/icons/smart_home_lvgl_png_icons.h"

#include "port/sca_config.h"
#include "port/sca_os.h"

#include <stddef.h>
#include <string.h>

/* ── 字号槽位 ─────────────────────────────────────────────
 * 20 个槽位与传入的 size 一一对应，UI 各处调用不用改。
 *
 * 96 号（屏保时钟）在内置字体里没有，映射到 48 —— 屏保显示 "08:30"
 * 这种 5 字符，48 号在 800 宽屏上仍有充足留白。
 * 若以后要更大，用 lv_obj_set_style_transform_scale 把 48 号放大 2 倍
 * （位图缩放，边缘略糊但可接受），比带一套外部字体划算。
 */

typedef struct {
    int              size;      /* UI 请求的字号 */
    const lv_font_t *font;      /* 实际使用的内置字体 */
} font_slot_t;

static const font_slot_t g_font_slots[] = {
    {  11, &lv_font_montserrat_12 },
    {  12, &lv_font_montserrat_12 },
    {  13, &lv_font_montserrat_14 },
    {  14, &lv_font_montserrat_14 },
    {  15, &lv_font_montserrat_16 },
    {  16, &lv_font_montserrat_16 },
    {  17, &lv_font_montserrat_18 },
    {  18, &lv_font_montserrat_18 },
    {  19, &lv_font_montserrat_20 },
    {  20, &lv_font_montserrat_20 },
    {  22, &lv_font_montserrat_22 },
    {  24, &lv_font_montserrat_24 },
    {  26, &lv_font_montserrat_26 },
    {  28, &lv_font_montserrat_28 },
    {  32, &lv_font_montserrat_32 },
    {  38, &lv_font_montserrat_38 },
    {  48, &lv_font_montserrat_48 },
    {  50, &lv_font_montserrat_48 },
    {  56, &lv_font_montserrat_48 },
    {  96, &lv_font_montserrat_48 },
};

#define FONT_SLOT_COUNT (sizeof(g_font_slots) / sizeof(g_font_slots[0]))

int smart_home_lvgl_style_init(void)
{
    /* 字体全部编译进 .rodata，无需运行时初始化。
     * 保留函数是为了维持调用点不变，也方便将来若真要换外部字体
     * （例如改回多语言）时在这里补加载。 */
    SCA_LOGI("lvgl", "fonts: builtin Montserrat, %u slots",
             (unsigned)FONT_SLOT_COUNT);
    return 0;
}

void smart_home_lvgl_style_deinit(void)
{
    /* 内置字体是常量数据，不能也不能释放。 */
}

const lv_font_t *smart_home_lvgl_font(int size)
{
    size_t i;

    if (size <= 0) {
        return &lv_font_montserrat_14;
    }

    /* 线性查找 20 个槽位：UI 只在建对象时调一次，比二分更易读，
     * 也比加一张 96 项索引表省 flash。 */
    for (i = 0; i < FONT_SLOT_COUNT; i++) {
        if (size <= g_font_slots[i].size) {
            return g_font_slots[i].font;
        }
    }

    /* 超出最大槽位：用最大的内置字号 */
    return g_font_slots[FONT_SLOT_COUNT - 1].font;
}

/* ── CJK 子集字体 ─────────────────────────────────────────
 *
 * 内置 Montserrat 没有汉字字形，米家的设备名、房间、控制项描述与
 * 枚举选项会显示成方块。字体数据由 ui/lvgl/fonts/cjk_font.S 用
 * .incbin 编入 .rodata（ASCII + 标点 + GB2312 一二级共 6763 字，
 * 约 1.6MB，见 tools/subset_font_cjk.py），TinyTTF 直接从常量区
 * 按需光栅化。
 *
 * 只建 16/24 两档实例，避免为每个 UI 字号都占用一份字形缓存。
 * 纯 ASCII 文本不走这条路径，仍用内置字体：零额外 RAM 与耗时。
 * 字体数据缺失或解析失败时回退内置字体并只报一次错误，UI 不崩溃。
 *
 * 必须关闭 kerning：LVGL 自带的 stb_truetype 解析 GPOS 时漏加了
 * 表基址，字距查询会空转两万多次并卡住首帧。子集字体也已在生成
 * 时丢弃 GPOS/GSUB，这里再显式关掉，双保险且省去无意义的查表。
 */

#define SCA_CJK_FONT_SMALL 16
#define SCA_CJK_FONT_LARGE 24

#if LV_USE_TINY_TTF

extern const unsigned char sca_cjk_font_start[];
extern const unsigned char sca_cjk_font_end[];

static lv_font_t *g_cjk_fonts[2];
static int g_cjk_font_failed;

static int text_has_non_ascii(const char *text)
{
    const unsigned char *p;

    if (!text) {
        return 0;
    }
    for (p = (const unsigned char *)text; *p; p++) {
        if (*p >= 0x80) {
            return 1;
        }
    }
    return 0;
}

const lv_font_t *smart_home_lvgl_font_for_text(const char *text, int size)
{
    unsigned slot;
    lv_font_t *font;

    if (!text_has_non_ascii(text) || g_cjk_font_failed) {
        return smart_home_lvgl_font(size);
    }

    slot = size > 18 ? 1u : 0u;
    font = g_cjk_fonts[slot];
    if (!font) {
        font = lv_tiny_ttf_create_data_ex(
            sca_cjk_font_start,
            (size_t)(sca_cjk_font_end - sca_cjk_font_start),
            slot ? SCA_CJK_FONT_LARGE : SCA_CJK_FONT_SMALL,
            LV_FONT_KERNING_NONE,
            LV_TINY_TTF_CACHE_GLYPH_CNT);
        if (!font) {
            g_cjk_font_failed = 1;
            SCA_LOGE("lvgl", "CJK 字体解析失败，回退内置字体");
            return smart_home_lvgl_font(size);
        }
        g_cjk_fonts[slot] = font;
        SCA_LOGI("lvgl", "CJK 字体已加载：%dpx",
                 slot ? SCA_CJK_FONT_LARGE : SCA_CJK_FONT_SMALL);
    }
    return font;
}

#else /* !LV_USE_TINY_TTF */

const lv_font_t *smart_home_lvgl_font_for_text(const char *text, int size)
{
    (void)text;
    return smart_home_lvgl_font(size);
}

#endif /* LV_USE_TINY_TTF */

void smart_home_lvgl_set_bg(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

/* 复用当前渲染器的着色能力，不缩放控件，也不增加过渡动画。 */
void smart_home_lvgl_press_feedback(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_image_recolor(obj, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_set_style_image_recolor_opa(obj, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_image_recolor_opa(obj, LV_OPA_TRANSP,
        LV_STATE_DISABLED | LV_STATE_PRESSED);
    lv_obj_set_style_opa(obj, LV_OPA_50, LV_STATE_DISABLED);
}

void smart_home_lvgl_card_style(lv_obj_t *obj)
{
    smart_home_lvgl_set_bg(obj, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_radius(obj, 18, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, SMART_HOME_UI_COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(obj, 15, 0);
}

void smart_home_lvgl_soft_card_style(lv_obj_t *obj)
{
    smart_home_lvgl_set_bg(obj, SMART_HOME_UI_COLOR_SURFACE_SOFT);
    lv_obj_set_style_radius(obj, 11, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 12, 0);
}

void smart_home_lvgl_style_keyboard(lv_obj_t *keyboard)
{
    if (!keyboard) {
        return;
    }

    /* 键盘按键是 ASCII 加 LVGL 内置符号字形（退格、回车、Shift），
     * Montserrat 全部覆盖，直接用内置 20 号。 */
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_all(keyboard, 2, 0);
}

void smart_home_lvgl_style_input(lv_obj_t *input, int size)
{
    lv_obj_set_style_text_font(input, smart_home_lvgl_font(size), 0);
    lv_obj_set_style_text_color(input, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(input, smart_home_lvgl_font(size),
                               LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_color(input, SMART_HOME_UI_COLOR_TEXT_MUTED,
                                LV_PART_TEXTAREA_PLACEHOLDER);
    smart_home_lvgl_set_bg(input, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_radius(input, 10, 0);
    lv_obj_set_style_border_width(input, 1, 0);
    lv_obj_set_style_border_color(input, SMART_HOME_UI_COLOR_BORDER, 0);
    lv_obj_set_style_pad_hor(input, 14, 0);
    lv_obj_set_style_pad_ver(input, 10, 0);
    /* 密码掩码用 ASCII 星号：内置字体有它，不会出现缺字方块。 */
    lv_textarea_set_password_bullet(input, "*");
}

void smart_home_lvgl_style_dropdown(lv_obj_t *dropdown, int size)
{
    lv_obj_t *list = lv_dropdown_get_list(dropdown);
    const lv_image_dsc_t *arrow = smart_home_lvgl_png_icon_get("asset:chevron-right", 20);
    /* 米家枚举项的文案是中文，按文本内容选择字体。 */
    const lv_font_t *font =
        smart_home_lvgl_font_for_text(lv_dropdown_get_options(dropdown), size);
    if (arrow) lv_dropdown_set_symbol(dropdown, arrow);
    lv_obj_set_style_text_font(dropdown, font, 0);
    lv_obj_set_style_text_color(dropdown, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
    smart_home_lvgl_set_bg(dropdown, SMART_HOME_UI_COLOR_SURFACE);
    lv_obj_set_style_radius(dropdown, 10, 0);
    lv_obj_set_style_border_width(dropdown, 1, 0);
    lv_obj_set_style_border_color(dropdown, SMART_HOME_UI_COLOR_BORDER, 0);
    lv_obj_set_style_pad_hor(dropdown, 14, 0);
    lv_obj_set_style_pad_ver(dropdown, 10, 0);
    if (list) {
        lv_obj_set_style_text_font(list, font, 0);
        lv_obj_set_style_text_color(list, SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
        smart_home_lvgl_set_bg(list, SMART_HOME_UI_COLOR_SURFACE);
        lv_obj_set_style_bg_color(list, SMART_HOME_UI_COLOR_SURFACE_ON,
                                  LV_PART_SELECTED);
    }
}

lv_obj_t *smart_home_lvgl_label_create(lv_obj_t *parent,
                                       const char *text,
                                       lv_color_t color,
                                       int size)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_font(label, smart_home_lvgl_font_for_text(text, size), 0);
    return label;
}

/* ── 图标 ─────────────────────────────────────────────────
 *
 * 两条路径：
 *   1. "asset:xxx"  —— 走编译进固件的 PNG 资源表
 *      （smart_home_lvgl_png_icon_get）。这是本工程的默认路径，
 *      因为 LVGL 的 lv_fs 已关闭，读不到 littlefs 上的 png。
 *   2. LVGL 内置符号（文本以 0x80 以上字节开头，如 LV_SYMBOL_xxx）
 *      —— 用 label + Montserrat 绘制。键盘与少量系统图标走这条。
 *
 * 不保留"从文件系统读 png"的分支：
 * LV_USE_FS_* 全关的情况下 access()/lv_fs 都不存在，
 * 留着只会编不过。
 */

static int icon_is_product_asset(const char *icon)
{
    return icon && strncmp(icon, "asset:", strlen("asset:")) == 0;
}

/* LVGL 内置符号是 UTF-8 编码的私有区字符，首字节 >= 0x80。 */
static int icon_is_lv_symbol(const char *icon)
{
    unsigned char first;

    if (!icon || !icon[0])
    {
        return 0;
    }

    first = (unsigned char)icon[0];
    return first >= 0x80;
}

static const lv_font_t *smart_home_lvgl_symbol_font(int size)
{
    if (size <= 12) return &lv_font_montserrat_12;
    if (size <= 14) return &lv_font_montserrat_14;
    if (size <= 16) return &lv_font_montserrat_16;
    return &lv_font_montserrat_20;
}

const lv_font_t *smart_home_lvgl_embedded_icon_font(const char *icon)
{
    /* 保留这个兼容钩子：调用方若传 LVGL 内置符号，返回 NULL 让
     * icon_create 回落到 symbol_font。 */
    (void)icon;
    return NULL;
}

lv_obj_t *smart_home_lvgl_icon_create(lv_obj_t *parent,
                                      const char *filename,
                                      int width,
                                      int height)
{
    lv_obj_t *img;

    if (!parent || !filename)
    {
        return NULL;
    }

    if (icon_is_product_asset(filename))
    {
        const lv_image_dsc_t *asset = smart_home_lvgl_png_icon_get(
            filename, width > height ? width : height);

        if (!asset)
        {
            return NULL;
        }

        img = lv_image_create(parent);
        lv_image_set_src(img, asset);

        if (width > 0)
        {
            int draw_w = width;
            int draw_h = height > 0 ? height : width;

            /* A8 位图不缩放，按原始尺寸绘制；
             * 若没有更小的变体，放宽控件到原生尺寸以免被裁掉。 */
            if (asset->header.w > draw_w) draw_w = asset->header.w;
            if (asset->header.h > draw_h) draw_h = asset->header.h;
            lv_obj_set_size(img, draw_w, draw_h);
        }

        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);

        if (asset->header.cf == LV_COLOR_FORMAT_A8)
        {
            lv_obj_set_style_image_recolor(img,
                                           SMART_HOME_UI_COLOR_TEXT_PRIMARY, 0);
            lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
        }
        else
        {
            /* 品牌彩色 PNG：保留原始颜色。 */
            lv_obj_set_style_image_recolor_opa(img, LV_OPA_TRANSP, 0);
        }
        return img;
    }

    if (icon_is_lv_symbol(filename))
    {
        int font_size = width > 0 ? width : 16;
        const lv_font_t *font = smart_home_lvgl_embedded_icon_font(filename);
        lv_obj_t *label = lv_label_create(parent);

        if (height > font_size)
        {
            font_size = height;
        }

        lv_label_set_text(label, filename);
        lv_obj_set_style_text_font(label,
                                   font ? font
                                        : smart_home_lvgl_symbol_font(font_size),
                                   0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        if (width > 0)
        {
            lv_obj_set_width(label, width);
        }
        return label;
    }

    /* 既不是 asset: 也不是内置符号：当作资源表里的裸名字再试一次，
     * 失败就返回 NULL（调用方都会判空）。 */
    {
        const lv_image_dsc_t *asset =
            smart_home_lvgl_png_icon_get(filename, width > 0 ? width : 32);

        if (!asset)
        {
            return NULL;
        }

        img = lv_image_create(parent);
        lv_image_set_src(img, asset);
        if (width > 0)
        {
            lv_obj_set_size(img, width, height > 0 ? height : width);
        }
        return img;
    }
}

lv_obj_t *smart_home_lvgl_icon_with_text(lv_obj_t *parent,
                                         const char *filename,
                                         int icon_w,
                                         int icon_h,
                                         const char *text,
                                         lv_color_t text_color,
                                         int text_size)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_t *icon;
    lv_obj_t *label;

    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    icon = smart_home_lvgl_icon_create(cont, filename, icon_w, icon_h);
    if (icon)
    {
        lv_obj_set_style_margin_bottom(icon, 4, 0);
        lv_obj_set_style_text_color(icon, text_color, 0);
        lv_obj_set_style_image_recolor(icon, text_color, 0);
        lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
    }

    label = smart_home_lvgl_label_create(cont, text, text_color, text_size);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    return cont;
}

void smart_home_lvgl_icon_color(lv_obj_t *icon, lv_color_t color)
{
    if (!icon)
    {
        return;
    }

    if (lv_obj_check_type(icon, &lv_image_class))
    {
        const void *src = lv_image_get_src(icon);

        if (src && lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE &&
            ((const lv_image_dsc_t *)src)->header.cf != LV_COLOR_FORMAT_A8)
        {
            /* 彩色 PNG 不染色，否则品牌色会丢 */
            lv_obj_set_style_image_recolor_opa(icon, LV_OPA_TRANSP, 0);
        }
        else
        {
            lv_obj_set_style_image_recolor(icon, color, 0);
            lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
        }
    }
    else
    {
        lv_obj_set_style_text_color(icon, color, 0);
    }
}

