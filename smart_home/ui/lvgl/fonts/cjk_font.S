/*
 * 把 CJK 子集字体直接编入固件。
 *
 * 运行时资源分区只有 1MB，放不下约 870KB 的字体，而且免去了随技能
 * 文档一起部署字体文件的步骤。数据位于 .rodata，TinyTTF 直接从这里
 * 读取字形，不占用 RAM。
 */

/* font-digest: 6cfb2e2649241c87 */
/* 由 tools/subset_font_cjk.py 更新；内容变化时触发重新编译。 */

    .section .rodata.cjk_font, "a", %progbits
    .balign 4
    .global sca_cjk_font_start
    .global sca_cjk_font_end
    .type sca_cjk_font_start, %object

sca_cjk_font_start:
    .incbin "resources/fonts/MiSans-Normal-CJK.ttf"
sca_cjk_font_end:

    .size sca_cjk_font_start, sca_cjk_font_end - sca_cjk_font_start
