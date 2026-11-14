/**
 * LVGL 9.2.2 配置（RT-Thread / Edgi-Talk PSE84 M55）
 *
 * 三条原则：
 *   1. 只启用 UI 真正用到的模块。LVGL 默认把所有 widget 都编进来，
 *      在 16MB flash 的板上是浪费；这里按 smart_home/ui 的实际引用裁剪。
 *   2. 内存走 RT-Thread 堆（LV_USE_STDLIB_MALLOC == LV_STDLIB_RTTHREAD），
 *      字符串/格式化用 newlib 的实现（见下方说明）。
 *   3. 调度用 LV_OS_RTTHREAD 之外的最简方案：LVGL 的单线程模型已经
 *      足够（所有 LVGL 调用都在 UI 线程），把 LV_USE_OS 设为 NONE，
 *      避免引入 lv_thread / lv_mutex 的额外抽象层。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* ── 颜色与像素 ─────────────────────────────────────────── */

#define LV_COLOR_DEPTH 16

/* ── 标准库映射 ─────────────────────────────────────────── */

/* malloc/free 走 LVGL 自带的 rtthread 后端（lv_mem_core_rtthread.c，
 * 内部调 rt_malloc/rt_free）。注意只有 malloc 有这个后端，
 * string / sprintf 只有 builtin 与 clib 两种实现 ——
 * 用 clib 直接落到 newlib（memcpy/strlen 有优化，也不再复制一份代码）。 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_RTTHREAD
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* LVGL 主循环由 smart_home/ui/lvgl/smart_home_lvgl.c 手写：
 * 它需要和 agent 的 pending 队列共享同一个循环节拍，用 LVGL 自带的
 * 线程封装反而多一层。因此这里关掉 LV_USE_OS。 */
#define LV_USE_OS               LV_OS_NONE

/* ── 日志与断言 ─────────────────────────────────────────── */

#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      0
#define LV_USE_ASSERT_MALLOC    0
#define LV_USE_ASSERT_STYLE     0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ       0

/* ── 基础对象与布局 ─────────────────────────────────────── */

#define LV_USE_FLEX             1
#define LV_USE_GRID             1

/* ── 字体 ───────────────────────────────────────────────── */

/* 内置 Montserrat：只编 smart_home_lvgl_font() 用到的尺寸。
 * 全部打开约 1.1MB .rodata，只留 12 个尺寸约 500KB。 */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_22   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_26   1
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_32   1
#define LV_FONT_MONTSERRAT_38   1
#define LV_FONT_MONTSERRAT_48   1

#define LV_FONT_DEFAULT         &lv_font_montserrat_16

/* 内置 Montserrat 只有 ASCII 字形。米家设备名、房间名与二级菜单的
 * 控制项/枚举项来自云端中文，缺字会被 placeholder 画成方块。
 * TinyTTF 从编入固件的 GB2312 一级子集（见 ui/lvgl/fonts/cjk_font.S）
 * 按需光栅化，纯英文文本仍走上面的内置字体，不增加开销。 */
#define LV_USE_TINY_TTF             1
#if LV_USE_TINY_TTF
    #define LV_TINY_TTF_FILE_SUPPORT    0
    #define LV_TINY_TTF_CACHE_GLYPH_CNT 128
#endif

#define LV_USE_FONT_PLACEHOLDER 1

/* ── widget 裁剪 ────────────────────────────────────────── */

#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CANVAS       1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMAGE        1
#define LV_USE_IMAGEBUTTON  1
#define LV_USE_KEYBOARD     1
#define LV_USE_LABEL        1
#define LV_USE_LED          1
#define LV_USE_LINE         1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_ROLLER       1
#define LV_USE_SCALE        1
#define LV_USE_SLIDER       1
#define LV_USE_SPAN         1
#define LV_USE_SPINBOX      1
#define LV_USE_SPINNER      1
#define LV_USE_SWITCH       1
#define LV_USE_TABLE        1
#define LV_USE_TABVIEW      1
#define LV_USE_TEXTAREA     1
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          1
#define LV_USE_CHART        1
#define LV_USE_CALENDAR     0
#define LV_USE_COLORWHEEL   0
#define LV_USE_IMGBTN       0
#define LV_USE_MATRIX       0
#define LV_USE_3DTEXTURE    0
#define LV_USE_ARCLABEL      0

/* ── 图片解码 ───────────────────────────────────────────── */

/* PNG 图标是构建期转成 C 数组（ARGB8888）编进固件的，
 * 运行时不需要解码器。若以后改成从文件系统读 PNG 再打开。 */
#define LV_USE_LODEPNG      0
#define LV_USE_LIBPNG       0
#define LV_USE_BMP          0
#define LV_USE_TJPGD        0
#define LV_USE_GIF          0
#define LV_USE_QRCODE       0
#define LV_USE_BARCODE      0
#define LV_USE_SVG          0

/* ── 文件系统 ───────────────────────────────────────────── */

/* UI 不读盘：图标编译进固件，字体也用编入固件的子集。 */
#define LV_USE_FS_STDIO     0
#define LV_USE_FS_POSIX     0
#define LV_USE_FS_WIN32     0
#define LV_USE_FS_FATFS     0
#define LV_USE_FS_MEMFS     0
#define LV_USE_FS_LITTLEFS  0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#define LV_USE_FS_ARDUINO_SD 0

/* ── 性能参数 ───────────────────────────────────────────── */

/* 绘制缓冲由 sca_display.c 用双缓冲 48 行（约 150KB）申请，
 * LVGL 这侧只需知道刷新模式。 */
#define LV_DEF_REFR_PERIOD  16      /* ms，约 60Hz 上限 */
#define LV_DPI_DEF          130     /* 4.3" 800x480，约 217 PPI 的一半 */

/* 对象与样式池：UI 峰值约 600 个对象（首页设备卡 + 设置页长列表）。 */
#define LV_MEM_SIZE         (64 * 1024)
#define LV_OBJ_STYLE_CACHE  1

/* 动画：只保留 UI 用到的淡入淡出与位移，关掉大表以省 flash。 */
#define LV_USE_ANIMIMG      0

/* 主题：默认主题足够，不需要额外主题包。 */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0

/* ── 其它 ───────────────────────────────────────────────── */

#define LV_USE_SYSMON       0
#define LV_USE_PROFILER     0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0
#define LV_USE_REFR_DEBUG   0
#define LV_USE_OBSERVER     1
#define LV_USE_SNAPSHOT     0

/* 输入设备：触摸在 sca_display.c 里手动绑定，不需要 LVGL 自带的
 * evdev/libinput 驱动。 */
#define LV_USE_EVDEV        0
#define LV_USE_LIBINPUT     0

#endif /* LV_CONF_H */
