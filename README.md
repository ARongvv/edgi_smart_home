# Edgi Smart Home · RT-Thread / Edgi-Talk

基于 RT-Thread 5.0.2、LVGL 9.2.2 和 cAGENT 的触屏智能家居中控，运行于 Edgi-Talk 的 Cortex-M55。支持房间与设备卡片、文字对话、工具权限管理，以及通过 MiLoCo 网关控制米家设备。

[GitHub 仓库](https://github.com/ARongvv/edgi_smart_home) · [演示视频](https://www.bilibili.com/video/BV1AXsV6rEYW/) · [产品与架构](docs/Edgi_Smart_Home_产品与架构说明.md) · [按图号整理的图片](docs/文档图片/图片清单.md)

## 快速开始

按以下顺序准备：**安装工具 → 获取 BSP 和工程 → 补齐依赖 → 构建 M55 → 准备 M33 启动固件 → 烧录 → 首次配置**。下文命令使用 Windows PowerShell。

### 1. 准备硬件与工具

| 项目 | 准备内容 |
| --- | --- |
| 开发板 | Edgi-Talk（PSoC Edge E84），连接板载 LCD、触摸屏 |
| USB | 使用可传输数据的 USB 线连接板载 DAP / KitProg3 接口 |
| Git | 用于下载工程、BSP 与第三方依赖 |
| Python | 建议 Python 3.10 或更新版本，安装时加入 PATH |
| SCons | 本工程的固件构建工具 |
| Arm GNU Toolchain | 13.3.Rel1，选择 Windows 的 **arm-none-eabi** 版本 |
| RT-Thread Studio | 2.2.9 或更新版本，用于准备 BSP 和 M33 模板固件 |
| Infineon OpenOCD | 使用 Infineon 版本，工具目录应包含 `bin/openocd.exe`、`scripts/interface/kitprog3.cfg`、`scripts/target/infineon/pse84xgxs2.cfg` |

工具入口：[RT-Thread Studio](https://www.rt-thread.org/studio.html)、[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)、[Infineon OpenOCD](https://github.com/Infineon/openocd/releases)。

安装 Python 工具并检查：

```powershell
python -m pip install "scons>=4,<5" pyserial
python --version
python -m SCons --version
git --version
```

### 2. 获取官方 BSP 和本工程

下面统一使用 `D:/edgi-workspace`，可替换为自己的工作目录。

```powershell
New-Item -ItemType Directory -Force D:/edgi-workspace | Out-Null
Set-Location D:/edgi-workspace

git clone --config core.autocrlf=false --branch 1.4.0 --depth 1 https://github.com/RT-Thread-Studio/sdk-bsp-psoc_e84-edgi-talk.git
git clone https://github.com/ARongvv/edgi_smart_home.git

Set-Location D:/edgi-workspace/edgi_smart_home
```

目录关系如下：

```text
D:/edgi-workspace/
├── sdk-bsp-psoc_e84-edgi-talk/   官方 BSP，包含 RT-Thread、HAL、启动代码和 Wi-Fi 资源
├── edgi_smart_home/             本工程，在此执行构建命令
└── edgi-deps/                   下一步下载第三方源码的缓存目录
```

BSP 使用官方 `1.4.0` 标签，其中包含 RT-Thread 5.0.2。工程复用 `projects/Edgi_Talk_M55_LVGL` 的板级配置和链接脚本。

### 3. 补齐第三方依赖

按 [依赖准备说明](packages/README.md#首次准备依赖windows-powershell) 执行下载、补丁和复制命令，再返回本 README 继续。

| 依赖 | 使用版本 |
| --- | --- |
| cAGENT | `059b568` + [本工程配套补丁](docs/patches/cagent-059b568-rtthread.patch) |
| cJSON | 1.7.18 |
| LVGL | 9.2.2 |
| Mbed TLS | 3.6.5 |
| RT-Thread netutils | 1.3.3 |
| QR-Code-generator | `bcfe1b0`，使用 C 实现 |

仓库已包含 `packages/SConscript`、`packages/lvgl/SConscript`、`packages/lvgl/lv_conf.h` 和 `packages/cagent/ports/rtthread/`，准备依赖时保留这些项目适配文件。

### 4. 配置路径并构建 M55

在本工程根目录设置路径。`EDGI_SDK_ROOT` 指向 **BSP 根目录**，`RTT_EXEC_PATH` 指向交叉编译器的 **bin 目录**。

```powershell
Set-Location D:/edgi-workspace/edgi_smart_home

$env:EDGI_SDK_ROOT = (Resolve-Path '../sdk-bsp-psoc_e84-edgi-talk').Path
$env:RTT_EXEC_PATH = 'D:/tools/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin'
$env:PATH = "$env:RTT_EXEC_PATH;$env:PATH"
$env:PYTHONUTF8 = '1'

arm-none-eabi-gcc --version
Test-Path "$env:EDGI_SDK_ROOT/rt-thread/tools/building.py"
Test-Path "$env:EDGI_SDK_ROOT/projects/Edgi_Talk_M55_LVGL/board/linker_scripts/link.ld"

python -m SCons -j8
```

两项 `Test-Path` 均应返回 `True`。构建成功后，工程根目录生成：

| 文件 | 用途 |
| --- | --- |
| `rtthread.hex` | **烧录文件**，包含目标地址 |
| `rtthread.elf` | 调试符号与可执行镜像 |
| `rtthread.bin` | 原始二进制镜像 |
| `rtthread.map` | 链接布局与内存占用 |

配置集中在 `rtconfig.h`，Agent 容量配置在 `port/sca_agent_profile.h`。默认启用 LVGL、米家接入和会话保存，语音与板载摄像头采集默认关闭。重新构建可使用：

```powershell
python -m SCons -c
python -m SCons -j8
```

### 5. 首次准备 M33 启动固件

Edgi-Talk 的启动顺序为 **Secure M33 → M33 → M55**。首次使用或板上启动固件被替换后，先用 RT-Thread Studio 构建并下载官方 `projects/Edgi_Talk_M33_Template`。

在该 M33 工程中确认 `RT-Thread Settings → Hardware → select SOC Multi Core Mode → Enable CM55 Core` 已开启。已有正确 M33 启动固件的开发板，后续只需更新本工程的 M55 HEX。

具体入口见官方 [M33 模板说明](https://github.com/RT-Thread-Studio/sdk-bsp-psoc_e84-edgi-talk/blob/1.4.0/projects/Edgi_Talk_M33_Template/README_zh.md) 与 [M55 LVGL 启动说明](https://github.com/RT-Thread-Studio/sdk-bsp-psoc_e84-edgi-talk/blob/1.4.0/projects/Edgi_Talk_M55_LVGL/README_zh.md)。

### 6. 烧录 M55 应用

连接 KitProg3，关闭占用调试器的 IDE 下载/调试会话。在工程根目录执行，修改 `$openocdRoot` 为实际安装目录：

```powershell
$openocdRoot = 'D:/tools/openocd'
$hex = (Resolve-Path './rtthread.hex').Path.Replace([char]92, [char]47)

& "$openocdRoot/bin/openocd.exe" -s "$openocdRoot/scripts" `
  -f interface/kitprog3.cfg `
  -c 'transport select swd' `
  -c 'set SMIF_BANKS {0 {addr 0x60000000 size 0x01000000 psize 0x00001000 esize 0x00040000}}' `
  -f target/infineon/pse84xgxs2.cfg `
  -c "init; reset init; targets cat1d.cm33; flash probe 2; flash write_image erase {$hex}; verify_image {$hex}; reset run; shutdown"
```

命令通过 `cat1d.cm33` 访问共享 Flash，实际写入地址由 M55 HEX 决定。M55 应用区为 `0x60580000–0x60D7FFFF`。看到校验成功信息后，板卡复位并运行应用。

### 7. 查看串口与首次配置

KitProg3 UART 参数为 **115200、8N1、无流控**。查询串口并打开终端，将 `COM5` 替换为实际端口：

```powershell
python -m serial.tools.list_ports
python -m serial.tools.miniterm COM5 115200
```

启动日志包含 `SmartHome on Edgi-Talk`、`Mounted 'filesystem' at /littlefs` 和 `[agent_profile]`。屏幕进入 Home / Chat / Settings 界面后，可在 `msh />` 执行：

| 命令 | 作用 |
| --- | --- |
| `sca_status` | 查看板级、存储、网络、Agent 和 UI 状态 |
| `sca_selftest` | 检查线程生命周期 |
| `sca_demo_reset` | 恢复本地演示设备，保留网络与服务凭据 |
| `sca_demo_check` | 检查本地演示工具、状态与保存通路 |

然后在屏幕上完成配置：

1. **Settings → Network**：连接 Wi-Fi。
2. **Settings → Model service**：选择模型后端，填写服务地址、模型名称和 API Key。
3. **Settings → Devices & services**：填写兼容本工程 API 的 MiLoCo 网关地址、端口和 Token，完成米家账号绑定；默认网关端口为 `1810`。
4. **Settings → Tool permissions**：允许所需工具。
5. 在 Home 点按设备卡片，或在 Chat 输入设备控制指令。

`set_light` / `set_ac` 操作本地演示模型；实际米家设备由网关控制。密钥在设置页输入，保存在板端 `/littlefs/smart_home/secrets.json`。

## 运行时资源

中文字体和 Wi-Fi 固件资源编入 M55 镜像。首屏启动无需另外上传字体；场景目录与技能文档放在板端 `/littlefs/res/skills/`。

在 PC 上生成并核对技能资源目录：

```powershell
python tools/deploy_resources.py
python tools/deploy_resources.py --verify
```

输出为 `build/resources/res/skills/` 和 `build/resources/resource-manifest.json`。把生成的 `skills` 目录通过板端文件传输方式复制到 `/littlefs/res/`；以上命令负责 PC 侧整理与校验。

| 板端目录 | 内容 |
| --- | --- |
| `/littlefs/res/skills/` | 技能文档与场景目录 |
| `/littlefs/smart_home/` | 设备状态、设置、提醒与凭据 |
| `/littlefs/smart_home/sessions/` | 对话历史 |

## 常见问题

| 现象 | 处理方式 |
| --- | --- |
| `Set EDGI_SDK_ROOT to the Edgi-Talk BSP checkout` | 核对第 4 步的两项 `Test-Path`，SDK 路径应指向 BSP 根目录 |
| 找不到 `agent/*.h`、`cJSON.h` 或 `lvgl.h` | 按依赖说明补齐 `packages/`，应用 cAGENT 配套补丁 |
| 找不到 `55500A1.trxcse` 等资源 | 使用完整官方 BSP，检查其 `resources/` 下的 Wi-Fi 固件、CLM、NVRAM 三个文件 |
| 编译器不识别 `cortex-m55` | 使用 Arm GNU Toolchain 13.3.Rel1 的 `arm-none-eabi` 工具链 |
| OpenOCD 找不到目标配置 | 使用 Infineon OpenOCD，并检查 `-s` 指定的 scripts 目录 |
| 烧录成功但屏幕无显示 | 先检查串口 `sca_status` 和启动错误，再核对 M33 是否启用 CM55、LCD 连接是否正确 |
| `LittleFS mount failed` | 检查 `filesystem` 分区；空白板首次使用时可按下方步骤初始化 |
| 中文显示为方框 | 保留仓库的 `lv_conf.h` 与 `SConscript`，确认 `LV_USE_TINY_TTF=1` 且编译了 `lv_tiny_ttf.c` |
| 工具执行后对话失败 | 查看 `[agent_diag]` 的失败阶段、工具名、字节数与容量；控制结果见 `[control]` |

空白 `filesystem` 分区可在 MSH 中初始化，**此命令会清空该分区的配置、凭据和历史数据**；已有数据的板卡先备份：

```text
mkfs -t lfs filesystem
```

完成后按板卡复位键重新启动。应用自身只尝试挂载 LittleFS。

## 开发入口

| 路径 | 内容 |
| --- | --- |
| `applications/` | 应用入口与串口命令 |
| `board/` | 显示、触摸、存储、Wi-Fi 等板级适配 |
| `port/` | RT-Thread 系统、文件、HTTP/TLS 抽象 |
| `smart_home/` | 设备、Agent、米家、网络、配置、LVGL 等业务模块 |
| `packages/` | 第三方源码与项目适配文件 |
| `resources/` | 字体、技能与场景资源 |
| `tests/` | 宿主侧测试与网关 Mock |

在安装了 GCC / Clang / TCC 的 PC 上，可执行已有宿主测试：

```powershell
python tests/run_host_tests.py
```

[依赖准备](packages/README.md) · [产品与架构](docs/Edgi_Smart_Home_产品与架构说明.md) · [控制回执](docs/CONTROL_RECEIPTS.md) · [容量与诊断](docs/CAPACITY_DIAGNOSTICS.md) · [官方 BSP](https://github.com/RT-Thread-Studio/sdk-bsp-psoc_e84-edgi-talk)
