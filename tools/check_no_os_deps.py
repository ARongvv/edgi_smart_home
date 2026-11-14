#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
守卫脚本：确保业务层不直接依赖任何 OS / 板级专有 API。

检查规则（对 smart_home/ 目录下所有 .c/.h）：
  禁止 include：
    <rtthread.h>  <rthw.h>  <rtdevice.h>  <rtdbg.h>  <board.h>
    <nuttx/...>   <arch/...>  <pthread.h>
    <dfs_posix.h> <dfs_fs.h>  <wlan_*.h>  <sal.h>  <ulog.h>
  也必须禁止：任何 "board/..." 或 "port/xxxx_rtthread.h" 这类平台实现头

允许 include：
    "port/sca_os.h" "port/sca_fs.h" "port/sca_http.h"
    "port/sca_config.h" "port/sca_camera.h" "port/sca_audio.h"

用法：
    python tools/check_no_os_deps.py                 # 检查默认目录
    python tools/check_no_os_deps.py smart_home      # 指定目录
    python tools/check_no_os_deps.py --list          # 列出所有违规

退出码：0 全部通过；1 存在违规。
"""

import os
import re
import sys

# 业务层允许出现在 include 里的平台头（port/ 提供的抽象）
ALLOWED_PLATFORM_HEADERS = {
    'port/sca_os.h',
    'port/sca_fs.h',
    'port/sca_http.h',
    'port/sca_config.h',
    'port/sca_camera.h',
    'port/sca_audio.h',
    'port/sca_display.h',
    'port/sca_wifi.h',
    'sca_os.h',
    'sca_fs.h',
    'sca_http.h',
    'sca_config.h',
    'sca_camera.h',
    'sca_audio.h',
    'sca_display.h',
    'sca_wifi.h',
    'sca_json.h',
}

# 明确禁止的平台头前缀/全名
FORBIDDEN_PATTERNS = [
    (re.compile(r'^<rtthread\.h>$'),              'RT-Thread core header'),
    (re.compile(r'^<rthw\.h>$'),                  'RT-Thread hardware header'),
    (re.compile(r'^<rtdevice\.h>$'),              'RT-Thread device header'),
    (re.compile(r'^<rtdbg\.h>$'),                 'RT-Thread dbg header'),
    (re.compile(r'^<rtconfig\.h>$'),              'rtconfig.h (use sca_config.h)'),
    (re.compile(r'^<nuttx/.*>$'),                 'NuttX header'),
    (re.compile(r'^<arch/.*>$'),                  'arch header'),
    (re.compile(r'^<netutils/.*>$'),              'NuttX netutils header'),
    (re.compile(r'^<pthread\.h>$'),               'POSIX pthread (use sca_os.h)'),
    (re.compile(r'^<semaphore\.h>$'),             'POSIX semaphore (use sca_os.h)'),
    (re.compile(r'^<dfs_posix\.h>$'),             'RT-Thread DFS (use sca_fs.h)'),
    (re.compile(r'^<dfs_fs\.h>$'),                'RT-Thread DFS (use sca_fs.h)'),
    (re.compile(r'^<wlan_.*\.h>$'),               'RT-Thread WLAN (use sca_wifi)'),
    (re.compile(r'^<sal\.h>$'),                   'RT-Thread SAL (use sca_http)'),
    (re.compile(r'^<ulog\.h>$'),                  'ulog (use sca_os.h log macros)'),
    (re.compile(r'^<audio\.h>$'),                 'RT-Thread audio (use sca_audio.h)'),
    (re.compile(r'^<sys/socket\.h>$'),            'raw socket (use sca_http)'),
    (re.compile(r'^<mbedtls/.*>$'),               'mbedTLS (use sca_http)'),
]

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+([<"].+?[>"])', re.MULTILINE)

# 自身是 port 实现的文件（不算业务层）
SKIP_FILES = {
    # port 层的平台实现
    'sca_os_rtthread.c',
    'sca_fs.c',
    'sca_http.c',
    'sca_display.c',
    # core 层的这两个文件是"平台内存调试"的实现体，
    # 逻辑上属于 port 层：smart_home_memory 包装 PSRAM 分配，
    # smart_home_cpu_debug 输出 CPU 占用。剩余业务代码只用它们的接口。
    'smart_home_memory.c',
    'smart_home_cpu_debug.c',
}

# 阶段 7/8 的可选模块：camera 与 voice 尚未完成 RT-Thread 适配
# （V4L2 / ALSA / KWS 模型，pthread/sem 直调）。它们默认不参与构建
# （rtconfig.h 里 CONFIG_SCA_CAMERA / CONFIG_SCA_VOICE_* 都注释掉），
# 因此不纳入守卫范围。等真正做适配时从 EXEMPT_DIRS 里移除即可。
EXEMPT_DIRS = ('camera', 'voice')


def scan_file(path):
    """返回 [(行号, include原文, 原因)]"""
    problems = []
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError as exc:
        return [(-1, str(exc), 'read error')]

    for match in INCLUDE_RE.finditer(text):
        raw = match.group(1)
        line_no = text[:match.start()].count('\n') + 1
        inner = raw.strip('<>"')

        # 允许的抽象头
        if inner in ALLOWED_PLATFORM_HEADERS:
            continue

        # cAGENT 是纯 C 可移植库（只依赖 stdint/stdbool/stddef），
        # 不含任何 OS 调用，业务层可以直接用它的 error/tool/skill 类型。
        if inner == 'agent.h' or inner.startswith('agent/'):
            continue

        for pattern, reason in FORBIDDEN_PATTERNS:
            if pattern.match('<' + inner + '>'):
                problems.append((line_no, raw, reason))
                break

    return problems


def scan_tree(root):
    violations = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in EXEMPT_DIRS]
        dirnames[:] = [d for d in dirnames if d not in ('.git', 'build')]
        for name in filenames:
            if not name.endswith(('.c', '.h', '.cc', '.cpp')):
                continue
            if name in SKIP_FILES:
                continue
            if 'camera' in name:
                continue
            full = os.path.join(dirpath, name)
            for line_no, raw, reason in scan_file(full):
                violations.append((full, line_no, raw, reason))
    return violations


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(here)

    targets = [a for a in argv[1:] if not a.startswith('-')]
    list_all = '--list' in argv or '-l' in argv

    if not targets:
        targets = [os.path.join(project_root, 'smart_home')]

    violations = []
    for target in targets:
        target = target if os.path.isabs(target) else os.path.join(project_root, target)
        if not os.path.isdir(target):
            print('SKIP  %s (not a directory)' % target)
            continue
        violations.extend(scan_tree(target))

    if not violations:
        print('PASS  no platform dependency leaks in business layer')
        return 0

    print('FAIL  %d violation(s):' % len(violations))
    for path, line_no, raw, reason in violations:
        rel = os.path.relpath(path, project_root)
        print('  %s:%d  %s   (%s)' % (rel, line_no, raw, reason))

    if not list_all:
        print('\nHint: move the dependency behind a port/ header instead.')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
