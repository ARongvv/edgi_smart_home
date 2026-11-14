#!/usr/bin/env python3
"""Generate the CJK subset font used by the LVGL device UI.

Covers ASCII, Latin-1 punctuation (so "24°" renders from the same font),
common CJK punctuation, fullwidth forms and the complete GB2312 level-1 +
level-2 hanzi set (6763 characters).

GPOS/GSUB/GDEF/kern are dropped on purpose. LVGL's stb_truetype backend
reads the GPOS LookupList without adding the table base offset, so any
font carrying GPOS makes every kerning query walk ~20k bogus records and
stalls the UI before the first frame. CJK text does not need kerning.

Usage:
    python tools/subset_font_cjk.py
    python tools/subset_font_cjk.py --source path/to/MiSans-Normal.ttf
"""
import argparse
import hashlib
import os
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = (ROOT.parent / "contest2026_031_niudanxianqianchong" /
                  "demos" / "smart_home" / "res" / "fonts" / "MiSans-Normal.ttf")
DEFAULT_OUTPUT = ROOT / "resources" / "fonts" / "MiSans-Normal-CJK.ttf"


def collect_chars():
    chars = {chr(code) for code in range(0x20, 0x7F)}
    # Latin-1 补充：° ± × ÷ µ 版权符号等
    chars.update(chr(code) for code in range(0xA0, 0x100))
    # 常用标点、货币、字母式符号、箭头、数学符号
    chars.update(chr(code) for code in range(0x2000, 0x2070))
    chars.update(chr(code) for code in range(0x20A0, 0x20D0))
    chars.update(chr(code) for code in range(0x2100, 0x2150))
    chars.update(chr(code) for code in range(0x2190, 0x2200))
    chars.update(chr(code) for code in range(0x2200, 0x2300))
    # CJK 标点与全角字符
    chars.update(chr(code) for code in range(0x3000, 0x3040))
    chars.update(chr(code) for code in range(0xFF01, 0xFF5F))
    chars.add("\uFFE5")  # ￥
    # GB2312 一级 + 二级汉字
    for first in range(0xB0, 0xF8):
        for second in range(0xA1, 0xFF):
            try:
                chars.add(bytes((first, second)).decode("gb2312"))
            except UnicodeDecodeError:
                continue
    return chars


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    source = args.source.resolve()
    output = args.output.resolve()
    if not source.is_file():
        print("font source is missing: %s" % source, file=sys.stderr)
        return 1

    chars = collect_chars()
    output.parent.mkdir(parents=True, exist_ok=True)

    text_file = Path(tempfile.gettempdir()) / "sca_cjk_chars.txt"
    text_file.write_text("".join(sorted(chars)), encoding="utf-8")

    from fontTools import subset

    subset.main([
        str(source),
        "--text-file=%s" % text_file,
        "--output-file=%s" % output,
        "--layout-features=",
        "--drop-tables+=GPOS,GSUB,GDEF,kern,vhea,vmtx",
        "--glyph-names",
        "--symbol-cmap",
        "--legacy-cmap",
        "--notdef-glyph",
        "--notdef-outline",
    ])

    # .incbin 的二进制文件不在 SCons 依赖图里，而且 SCons 按内容判断
    # 是否需要重编。把字体摘要写进 cjk_font.S 的注释，字体一变就会
    # 触发 cjk_font.o 重新编译，不会误用旧字形。
    stamp = ROOT / "smart_home" / "ui" / "lvgl" / "fonts" / "cjk_font.S"
    if stamp.is_file():
        digest = hashlib.sha256(output.read_bytes()).hexdigest()[:16]
        text = stamp.read_text(encoding="utf-8")
        if re.search(r"font-digest: [0-9a-f]+", text):
            text = re.sub(r"font-digest: [0-9a-f]+",
                          "font-digest: " + digest, text)
        else:
            text = text.replace(".incbin",
                                "/* font-digest: %s */\n    .incbin" % digest)
        stamp.write_text(text, encoding="utf-8")

    print("characters: %d" % len(chars))
    print("output: %s (%d bytes)" % (output, output.stat().st_size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
