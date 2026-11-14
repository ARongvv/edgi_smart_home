#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
宿主侧单元测试运行器。

在没有 RT-Thread 与交叉工具链的 PC 上，用任意可用的 C 编译器
编译并运行 tests/ 下的测试。默认按 tcc → gcc → cc → clang 顺序探测。

用法：
    python tests/run_host_tests.py              # 自动探测编译器
    python tests/run_host_tests.py --cc gcc     # 指定编译器
    python tests/run_host_tests.py --keep       # 保留中间产物

测试失败返回 1，全部通过返回 0。
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

INCLUDES = [
    os.path.join(HERE, 'stubs'),
    ROOT,
    os.path.join(ROOT, 'packages', 'cagent', 'include'),
    os.path.join(ROOT, 'packages', 'cagent', 'src'),
    os.path.join(ROOT, 'packages', 'cagent', 'codecs', 'json'),
    os.path.join(ROOT, 'packages', 'cagent', 'codecs', 'json', 'vendor', 'jsmn'),
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'model', 'openai', 'include'),
    os.path.join(ROOT, 'packages', 'cjson'),
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'storage', 'jsonl', 'include'),
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'storage', 'jsonl', 'src'),
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'storage', 'files', 'include'),
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'storage', 'ram', 'include'),
    os.path.join(ROOT, 'packages', 'cagent', 'ports', 'rtthread'),
]

# 每个测试 = (名字, [源文件相对路径], [额外编译选项])
TESTS = [
    ('test_fs', [
        'tests/test_fs.c',
        'port/sca_fs.c',
        'port/sca_os_rtthread.c',
        'tests/host_dfs.c',
        'tests/host_rtthread.c',
    ], []),
    ('test_rtthread_transport', [
        'tests/test_rtthread_transport.c',
        'packages/cagent/ports/rtthread/agent_rtthread_transport.c',
    ], []),
    ('test_http_body', [
        'tests/test_http_body.c',
        'port/sca_http_body.c',
    ], []),
    ('test_agent_loop', [
        'tests/test_agent_loop.c',
        'packages/cjson/cJSON.c',
        'smart_home/device/smart_home_device.c',
        'smart_home/device/smart_home_device_service.c',
        'smart_home/tools/smart_home_tools.c',
    ] + sorted(
        os.path.relpath(path, ROOT) for pattern in (
            os.path.join(ROOT, 'packages', 'cagent', 'src', '**', '*.c'),
            os.path.join(ROOT, 'packages', 'cagent', 'codecs', 'json', '*.c'),
            os.path.join(ROOT, 'packages', 'cagent', 'codecs', 'json', 'vendor', 'jsmn', '*.c'),
        ) for path in glob.glob(pattern, recursive=True)
    ), ['-std=c99']),
    ('test_device_persistence', [
        'tests/test_device_persistence.c',
        'packages/cjson/cJSON.c',
        'smart_home/device/smart_home_device.c',
        'smart_home/device/smart_home_device_service.c',
        'smart_home/device/smart_home_device_persistence.c',
    ], ['-std=c99']),
    ('test_openai_provider', [
        'tests/test_openai_provider.c',
        'packages/cagent/providers/model/openai/src/model_openai.c',
        'packages/cagent/providers/model/openai/src/openai_request.c',
        'packages/cagent/providers/model/openai/src/openai_response.c',
        'packages/cagent/src/core/error.c',
        'packages/cagent/src/runtime/runtime.c',
        'packages/cagent/src/run/cancel.c',
        'packages/cagent/src/transport/transport.c',
    ] + sorted(
        os.path.relpath(path, ROOT) for pattern in (
            os.path.join(ROOT, 'packages', 'cagent', 'codecs', 'json', '*.c'),
            os.path.join(ROOT, 'packages', 'cagent', 'codecs', 'json', 'vendor', 'jsmn', '*.c'),
        ) for path in glob.glob(pattern, recursive=True)
    ), ['-std=c99']),
    ('test_miloco_protocol', [
        'tests/test_miloco_protocol.c',
        'packages/cjson/cJSON.c',
        'smart_home/miloco/smart_home_miloco_protocol.c',
        'smart_home/miloco/smart_home_miloco_specscan.c',
    ], ['-std=c99']),
]


# Exercise the real Core and JSONL encoder using the exact firmware profile.
TESTS.append(('test_agent_capacity', [
    'tests/test_agent_capacity.c',
    'smart_home/agent/smart_home_agent_diag.c',
] + sorted(os.path.relpath(path, ROOT) for pattern in (
    os.path.join(ROOT, 'packages', 'cagent', 'src', '**', '*.c'),
    os.path.join(ROOT, 'packages', 'cagent', 'codecs', 'json', '*.c'),
    os.path.join(ROOT, 'packages', 'cagent', 'codecs', 'json', 'vendor', 'jsmn', '*.c'),
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'storage', 'jsonl', 'src', '*.c'),
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'storage', 'files', 'src', '*.c'),
) for path in glob.glob(pattern, recursive=True)), ['-std=c99']))


TESTS.append(('test_backend_capacity', [
    'tests/test_backend_capacity.c',
    'smart_home/agent/smart_home_agent_backend.c',
] + TESTS[-1][1][1:] + sorted(os.path.relpath(path, ROOT) for pattern in (
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'storage', 'ram', 'src', '*.c'),
    os.path.join(ROOT, 'packages', 'cagent', 'providers', 'model', 'openai', 'src', '*.c'),
) for path in glob.glob(pattern)), ['-std=c99']))


TESTS.append(('test_miloco_control', [
    'tests/test_miloco_control.c',
    'smart_home/miloco/smart_home_miloco_tools.c',
    'smart_home/miloco/smart_home_miloco_protocol.c',
    'smart_home/miloco/smart_home_miloco_specscan.c',
    'packages/cjson/cJSON.c',
], ['-std=c99', '-DCONFIG_SCA_MILOCO=1', '-lm']))


def find_compiler(explicit):
    if explicit:
        return explicit

    candidates = [
        os.path.join(os.environ.get('TEMP', '/tmp'), 'tcc', 'tcc', 'tcc.exe'),
        shutil.which('gcc'),
        shutil.which('cc'),
        shutil.which('clang'),
        shutil.which('tcc'),
    ]
    for c in candidates:
        if c and os.path.exists(c):
            return c
    return None


def run_test(compiler, name, sources, extra_flags, outdir):
    exe = os.path.join(outdir, name + ('.exe' if os.name == 'nt' else ''))
    flags = list(extra_flags)
    # tcc links against msvcrt and has no separate libm or GNU linker options.
    if os.path.basename(compiler).lower().startswith('tcc'):
        flags = [f for f in flags if f != '-lm' and not f.startswith('-Wl,')]
    cmd = [compiler, '-include', os.path.join(ROOT, 'port', 'sca_agent_profile.h')]
    cmd += sources
    for inc in INCLUDES:
        cmd += ['-I', inc]
    cmd += flags
    cmd += ['-o', exe]

    print('  cc: %s' % ' '.join(os.path.basename(s) for s in sources))
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        print('  COMPILE FAILED')
        print(result.stdout[-2000:])
        print(result.stderr[-2000:])
        return False

    proc = subprocess.run([exe], cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(proc.stdout)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
    return proc.returncode == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default=None, help='C compiler path')
    parser.add_argument('--keep', action='store_true', help='keep artifacts')
    args = parser.parse_args()

    compiler = find_compiler(args.cc)
    if not compiler:
        print('No C compiler found. Install gcc/clang, or pass --cc <path>.')
        return 2

    print('compiler: %s' % compiler)
    outdir = tempfile.mkdtemp(prefix='sca_host_test_')
    print('workdir : %s\n' % outdir)

    failures = 0
    for name, sources, flags in TESTS:
        print('--- %s ---' % name)
        if not run_test(compiler, name, sources, flags, outdir):
            failures += 1
        print()

    if not args.keep:
        shutil.rmtree(outdir, ignore_errors=True)

    total = len(TESTS)
    print('%d/%d passed' % (total - failures, total))
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
