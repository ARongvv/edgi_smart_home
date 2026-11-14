# 第三方依赖准备

本工程的 BSP、工具链和第三方库主体源码外置。仓库已包含项目构建脚本、LVGL 配置和 cAGENT RT-Thread 端口；下面的命令将固定版本源码补入 `packages/`，保留这些适配文件。

## 版本与来源

| 目标目录 | 上游 | 固定版本 |
| --- | --- | --- |
| `cagent/` | [Scintela/cAGENT](https://github.com/Scintela/cAGENT) | `059b568210c0441dbe231038cdd120a3ab5cdbd2` + 项目补丁 |
| `cjson/` | [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) | `v1.7.18` |
| `lvgl/` | [lvgl/lvgl](https://github.com/lvgl/lvgl) | `v9.2.2` |
| `mbedtls/` | [Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls) | `mbedtls-3.6.5`（`e185d7fd85499c8ce5ca2a54f5cf8fe7dbe3f8df`） |
| `netutils/` | [RT-Thread-packages/netutils](https://github.com/RT-Thread-packages/netutils) | `1.3.3`（`19d1a2908f82e8d7c2a63e1e69083fd798b520bf`） |
| `qrcodegen/` | [nayuki/QR-Code-generator](https://github.com/nayuki/QR-Code-generator) | `bcfe1b018cec5f1f91b5705eca5bb4cdd34e9f8d` 的 C 实现 |

cAGENT 的 [项目补丁](../docs/patches/cagent-059b568-rtthread.patch) 保存了当前移植使用的传统 include guard、编译器兼容改动和 JSONL 尾部容量预留修复。

## 首次准备依赖（Windows PowerShell）

在 `edgi_smart_home` 工程根目录运行。源码缓存会建立在工程同级的 `edgi-deps/`，以下命令用于首次准备；已有缓存时可换一个新缓存目录。

```powershell
$ErrorActionPreference = 'Stop'
$project = (Get-Location).Path
$deps = Join-Path (Split-Path $project -Parent) 'edgi-deps'
New-Item -ItemType Directory -Force $deps | Out-Null

# PowerShell 5.1 中显式检查 Git 的退出码。
function Invoke-Git {
    & git @args
    if ($LASTEXITCODE -ne 0) { throw "Git command failed: $args" }
}

Invoke-Git clone --config core.autocrlf=false https://github.com/Scintela/cAGENT.git "$deps/cagent"
Invoke-Git -C "$deps/cagent" checkout 059b568210c0441dbe231038cdd120a3ab5cdbd2
Invoke-Git -C "$deps/cagent" apply --check "$project/docs/patches/cagent-059b568-rtthread.patch"
Invoke-Git -C "$deps/cagent" apply "$project/docs/patches/cagent-059b568-rtthread.patch"

Invoke-Git clone --config core.autocrlf=false --branch v1.7.18 --depth 1 https://github.com/DaveGamble/cJSON.git "$deps/cjson"
Invoke-Git clone --config core.autocrlf=false --branch v9.2.2 --depth 1 https://github.com/lvgl/lvgl.git "$deps/lvgl"
Invoke-Git clone --config core.autocrlf=false --branch mbedtls-3.6.5 --depth 1 --recurse-submodules https://github.com/Mbed-TLS/mbedtls.git "$deps/mbedtls"
Invoke-Git clone --config core.autocrlf=false --branch 1.3.3 --depth 1 https://github.com/RT-Thread-packages/netutils.git "$deps/netutils"
Invoke-Git clone --config core.autocrlf=false https://github.com/nayuki/QR-Code-generator.git "$deps/qrcodegen"
Invoke-Git -C "$deps/qrcodegen" checkout bcfe1b018cec5f1f91b5705eca5bb4cdd34e9f8d

foreach ($name in @('cagent', 'cjson', 'lvgl', 'mbedtls', 'netutils', 'qrcodegen')) {
    New-Item -ItemType Directory -Force "$project/packages/$name" | Out-Null
}

# cAGENT：只复制内核、编解码器与 providers，保留项目 ports/rtthread。
foreach ($name in @('include', 'src', 'codecs', 'providers')) {
    Copy-Item "$deps/cagent/$name" "$project/packages/cagent/" -Recurse -Force
}
Copy-Item "$deps/cagent/LICENSE" "$project/packages/cagent/" -Force

# cJSON。
foreach ($name in @('cJSON.c', 'cJSON.h', 'LICENSE')) {
    Copy-Item "$deps/cjson/$name" "$project/packages/cjson/" -Force
}

# LVGL：保留项目 SConscript 和 lv_conf.h。
Copy-Item "$deps/lvgl/src" "$project/packages/lvgl/" -Recurse -Force
foreach ($name in @('lvgl.h', 'lv_version.h', 'LICENCE.txt')) {
    Copy-Item "$deps/lvgl/$name" "$project/packages/lvgl/" -Force
}

# Mbed TLS：编译本项目 TLS 配置需要的源文件。
foreach ($name in @('include', 'library', '3rdparty')) {
    Copy-Item "$deps/mbedtls/$name" "$project/packages/mbedtls/" -Recurse -Force
}
Copy-Item "$deps/mbedtls/LICENSE" "$project/packages/mbedtls/" -Force

# netutils 仅使用 NTP 客户端。
Copy-Item "$deps/netutils/ntp" "$project/packages/netutils/" -Recurse -Force
Copy-Item "$deps/netutils/LICENSE" "$project/packages/netutils/" -Force

# QR-Code-generator 仅使用 C 实现，许可信息保留在源码头部。
Copy-Item "$deps/qrcodegen/c/qrcodegen.c" "$project/packages/qrcodegen/" -Force
Copy-Item "$deps/qrcodegen/c/qrcodegen.h" "$project/packages/qrcodegen/" -Force
```

## 核对目录

准备后应包含：

```text
packages/
├── SConscript
├── cagent/
│   ├── include/agent.h
│   ├── include/agent/
│   ├── src/
│   ├── codecs/json/vendor/jsmn/
│   ├── providers/model/openai/
│   ├── providers/storage/{ram,files,jsonl}/
│   └── ports/rtthread/                    仓库自带，保留
├── cjson/{cJSON.c,cJSON.h}
├── lvgl/
│   ├── src/
│   ├── lvgl.h
│   ├── lv_version.h
│   ├── lv_conf.h                         仓库自带，保留
│   └── SConscript                        仓库自带，保留
├── mbedtls/{include,library,3rdparty}/
├── netutils/ntp/{ntp.c,ntp.h}
└── qrcodegen/{qrcodegen.c,qrcodegen.h}
```

检查关键文件：

```powershell
$required = @(
    'packages/cagent/include/agent.h',
    'packages/cagent/src/run/react_loop.c',
    'packages/cagent/providers/storage/jsonl/src/session_jsonl.c',
    'packages/cagent/ports/rtthread/agent_rtthread_transport.c',
    'packages/cjson/cJSON.c',
    'packages/lvgl/lvgl.h',
    'packages/lvgl/lv_conf.h',
    'packages/lvgl/src/libs/tiny_ttf/lv_tiny_ttf.c',
    'packages/mbedtls/include/mbedtls/build_info.h',
    'packages/netutils/ntp/ntp.c',
    'packages/qrcodegen/qrcodegen.c'
)
foreach ($path in $required) {
    if (-not (Test-Path $path)) { throw "Missing dependency: $path" }
}
'Package files ready'
```

依赖就绪后，返回 [工程 README 的构建步骤](../README.md#4-配置路径并构建-m55)。库源码使用各自许可证；项目专用构建与配置文件随本仓库保存。
