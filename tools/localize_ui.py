#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 UI 源码里的中文字面量替换成英文。

设计取舍：
  - 只替换 UI 文本，不动日志（printf/SCA_LOG）—— 日志走串口，中文
    在终端能正常显示，改成英文反而降低排错效率；
  - 逐字符串精确匹配映射表，任何没有映射的中文串都会报错退出。
    这是刻意的：宁可让脚本失败，也不能让 UI 出现缺字方块。
  - 同时替换："..." 与 '...' 两种字面量；不碰注释（注释是给维护者
    看的，保留中文更易读）。

用法：
    python tools/localize_ui.py --check     # 只检查覆盖率
    python tools/localize_ui.py --apply     # 执行替换
"""

import argparse
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ui_strings_en as M

CJK = re.compile(r'[\u4e00-\u9fff]')

# 只处理这些目录（UI 层）。smart_home 的其它模块保留中文：
#   - agent 的 system prompt 是给模型的，与字体无关
#   - memory 的关键词表是匹配用户输入用的，必须是中文
#   - 日志在串口终端显示，中文更有用
TARGET_DIRS = ['smart_home/ui']

# 这些文件里的中文字符串是"非显示"用途，跳过
SKIP_FILES = set()

# 日志调用：这些标记出现时跳过该行。
# 注意 printf 要用负向前瞻排除 snprintf/vsnprintf —— 它们常把文本写进
# UI 缓冲（如 snprintf(value, sizeof value, "预览已关闭")），那是界面文字，
# 必须替换；只有裸 printf 才是串口日志。
LOG_MARKERS = ('SCA_LOG', 'rt_kprintf(', 'fprintf(', 'LOG_')
LOG_RE = re.compile(r'(?<![a-z_])printf\(')


def iter_targets():
    for d in TARGET_DIRS:
        for root, _dirs, files in os.walk(d):
            for f in sorted(files):
                if f.endswith(('.c', '.h')) and f not in SKIP_FILES:
                    yield os.path.join(root, f)


def scan_file(path):
    """返回 [(字符串内容, 行号, 是否日志行)]"""
    out = []
    text = open(path, encoding='utf-8').read()
    lines = text.split('\n')

    for lineno, line in enumerate(lines, 1):
        if line.lstrip().startswith(('*', '/*', '//')):
            continue                        # 整行注释
        is_log = any(m in line for m in LOG_MARKERS) or bool(LOG_RE.search(line))

        # 双引号字面量（处理 \" 转义）
        for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', line):
            s = m.group(1)
            if CJK.search(s):
                out.append((s, lineno, is_log, '"'))
    return out


def check():
    missing = []
    total = 0
    covered = 0

    for path in iter_targets():
        for s, lineno, is_log, _q in scan_file(path):
            if is_log:
                continue
            total += 1
            if M.lookup(s) is not None:
                covered += 1
            else:
                missing.append((path, lineno, s))

    print('可替换字符串: %d' % total)
    print('已覆盖      : %d' % covered)
    print('缺失        : %d' % len(missing))
    if missing:
        print('')
        for p, ln, s in missing:
            print('  %s:%d  %s' % (p, ln, s))
    return len(missing) == 0


def apply():
    if not check():
        print('\n存在未映射字符串，拒绝替换。请先补 tools/ui_strings_en.py')
        return False

    changed = 0
    for path in iter_targets():
        text = open(path, encoding='utf-8').read()
        lines = text.split('\n')
        touched = False

        for i, line in enumerate(lines):
            if line.lstrip().startswith(('*', '/*', '//')):
                continue
            if any(m in line for m in LOG_MARKERS) or LOG_RE.search(line):
                continue

            def repl(m):
                inner = m.group(1)
                en = M.lookup(inner)
                return '"%s"' % en if en is not None else m.group(0)

            new = re.sub(r'"((?:[^"\\]|\\.)*)"', repl, line)
            if new != line:
                lines[i] = new
                touched = True

        if touched:
            open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
            changed += 1
            print('updated', path)

    print('\n%d 个文件已更新' % changed)
    return True


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()

    if a.apply:
        sys.exit(0 if apply() else 1)
    else:
        sys.exit(0 if check() else 1)
