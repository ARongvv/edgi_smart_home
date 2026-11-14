# 开发阶段进度

> 记录每个阶段的目标、已完成代码、验证方式、进入下一阶段的前置条件。
> 当前状态摘要更新：2026-11-14；下方阶段明细保留既有过程记录（原标注日期 2026-11-11）。


## 当前状态摘要（2026-11-14）

| 范围 | 当前状态 |
| --- | --- |
| 工程骨架与平台适配 | 已实现并有板端运行记录；平台依赖守卫尚有 1 项遗留违规 |
| 设备、技能、工具与状态 | 已实现；本地灯光/空调为虚拟模型，真实米家设备通过 MiLoCo 接入 |
| Agent 与会话 | 已接入，统一容量和阶段诊断；Session 历史与用户偏好独立保存 |
| UI | 800×480 板端运行，主要英文文案并支持中文设备/房间/能力名称 |
| 网络与真实设备 | 2026-11-14 Agent 云台校准实测成功，卡片同动作也有网关确认 |
| 自动化 | 文档更新时当前工作区宿主测试 10/10 通过；不代替 RT-Thread 调度与设备动作验收 |
| 仍待验收 | 重启/掉电恢复、长对话与会话整理峰值、更多设备/后端和异常竞态 |
| 可选功能 | 语音、板载摄像头采集与识别、M33 KWS/NPU 不计入当前完成能力 |

当前预算见 [容量说明](CAPACITY_DIAGNOSTICS.md)，控制语义见 [公共控制说明](CONTROL_RECEIPTS.md)，
真机证据见 [2026-11-14 验收记录](validation/2026-11-14-gimbal-calibration.md)。

**阅读边界：以下阶段明细是历史过程记录。** 其中“待板上验证”“纯英文替代 MiSans”、
旧容量/行数、旧测试数量及旧环境状态不作为当前工程结论，以上方摘要和现行源码为准。

---

## 阶段 0 · 工程骨架 ✅

**目标**：拿到一个可被 scons 识别、结构完整的 RT-Thread 工程，屏能亮。

**已交付**

| 文件 | 作用 |
|---|---|
| `SConstruct` | 构建入口。RTT_ROOT 自动探测（三处候选路径），支持 `RTT_ROOT` / `RTT_EXEC_PATH` 环境变量覆盖 |
| `SConscript` | 按固定顺序收集子目录：packages → board → port → smart_home → resources → applications |
| `Kconfig` | 工程级配置入口，source `port/Kconfig` 与 `smart_home/Kconfig` |
| `board/board.h` | 板级能力宏集中定义（800×480、16kHz 音频、QSPI 分区、ready 位掩码） |
| `board/board.c` | `board_init_all()` 按依赖顺序装配：storage → display → touch → audio → wifi；单点失败只记日志不中断 |
| `board/SConscript` | 板级编译清单 |
| `applications/main.c` | msh 命令 `smarthome` / `sca_status`，启动编排入口 |
| `README.md` | 环境、构建、烧录、资源部署、WiFi 固件上传说明 |
| `.gitignore` | 构建产物、RT-Thread Studio 元数据、packages 下载项 |

**验证方式**：目录结构、SConstruct 语法、Kconfig source 路径、SConscript 依赖关系均为静态可查；`applications/main.c` 通过 tcc 语法检查。

**已验证的取舍**：`board_init_all()` 返回位掩码而非 bool —— 因为 Edgi-Talk 上 WiFi 模组固件可能未上传，这不该阻止 UI 启动。`board_ready_mask_name()` 让启动日志一行看清降级情况。

**前置条件（进入阶段 1）**：无。但**上板前必须先跑通 `Edgi_Talk_M33_Template`**，确认 Secure M33 → M33 → M55 启动链正常。

---

## 阶段 1 · port 抽象层 ✅

**目标**：建成唯一平台出口，业务层零 OS 依赖。

**已交付**

| 文件 | 行数 | 作用 |
|---|---:|---|
| `port/sca_config.h` | 175 | 配置收敛：统一平台宏，`CONFIG_SMART_HOME_*` → `SCA_ENABLE_*` 重映射；路径常量集中 |
| `port/sca_os.h` | 175 | 线程/互斥/信号量/时钟/日志/临界区契约 |
| `port/sca_os_rtthread.c` | 400 | RT-Thread 实现，含编译期尺寸断言 |
| `port/sca_fs.h/.c` | 100 / 400 | 文件读写、原子写（tmp→rename）、mkdir -p、目录遍历、容量检查 |
| `port/sca_http.h/.c` | 100 / 400 | HTTP/HTTPS 客户端，含 mbedTLS 集成 |
| `port/sca_json.h` | 25 | cJSON 来源收敛，统一从该头文件转发 |
| `port/sca_camera.h` | 100 | 摄像头抽象（V4L2 / UVC 都实现它） |
| `port/sca_audio.h` | 80 | 音频服务级抽象 |
| `port/sca_sensor.h` | 60 | 环境传感器抽象（AHT20 / LSM6DS3） |
| `port/Kconfig` | 20 | TLS 开关、自动格式化开关 |
| `port/SConscript` | 10 | 编译清单 |
| `tools/check_no_os_deps.py` | 190 | 守卫脚本 |

**三个关键设计决策**

1. **互斥量做成 POD 内嵌**。业务结构体按值内嵌锁（如 `smart_home_device_service_t.mutex`）。若改成不透明指针，所有结构体要补 malloc/free 与失败路径。POD 方案把改动成本降到两处机械替换。安全性由 `sca_os_rtthread.c` 的静态断言保证：

   ```c
   typedef char sca_assert_mutex_fits[
       (SCA_OPAQUE_BYTES(SCA_MUTEX_OPAQUE_WORDS) >= sizeof(struct rt_mutex)) ? 1 : -1];
   ```

   RT-Thread 的 `rt_mutex` 若增长超出预留的 12 个指针宽度，编译期直接失败。

2. **线程 join 的语义降级**。RT-Thread 没有 `pthread_join` 语义。`sca_thread_join()` 改为轮询 `running` 标志，返回后必须调用 `sca_thread_destroy()` 释放。这是本实现唯一的语义降级，已在注释中明确约定。

3. **守卫脚本放行 cAGENT**。`<agent.h>` 与 `<agent/*.h>` 只依赖 `stdint/stdbool/stddef`，是纯 C 可移植库而非平台，业务层可直接用它的 error/tool/skill 类型。

**验证方式**

```bash
python tools/check_no_os_deps.py     # PASS
python tests/run_host_tests.py       # 1/1 passed（sca_fs 的 17 个断言全通过）
```

守卫脚本本身也被验证过：删掉一处 include 会立即 FAIL 并指出文件行号。

**宿主测试的边界**：`tests/host_rtthread.c` 的互斥量/信号量是空操作，因此宿主测试**不能验证并发正确性**，那必须上板或用 RT-Thread 的 sim BSP。

**前置条件（进入阶段 2）**：守卫脚本对空业务层通过。

---

## 阶段 2 · 纯逻辑模块 ✅ 9/9 完成

**目标**：把零平台依赖的模块搬到 `smart_home/`，宿主可编译。

**已完成**

| 模块 | 文件 | 改动 |
|---|---|---|
| `device/` | device.c/h、device_service.c/h | `pthread_mutex_t` → `sca_mutex_t`；`clock_gettime(CLOCK_REALTIME)` → `sca_time_ms()`（单调时钟，板上无 RTC 更稳）；`cjson_compat.h` → `sca_json.h` |
| `skills/` | scene_catalog、skill_loader、skills | `fopen+fseek/ftell+fread` → `sca_fs_read_all`（newlib-nano 的 ftell 对 littlefs 不可靠）；`opendir/readdir` 循环 → `sca_fs_list` 回调式遍历 |
| `tools/` | tools.c、device_inventory.c、tool_io.h、tool_metadata.h | 不依赖平台板级头与 sensors ioctl，AHT30 走 `port/sca_sensor.h`；`CONFIG_SMART_HOME_*` → `SCA_ENABLE_*` |

**memory 与 reminder 的额外改动**

`reminder/` 是本阶段改动最深的一个，因为它同时用了 pthread 的三套原语：

| 原写法 | 新写法 | 原因 |
|---|---|---|
| `pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER` | `static sca_cond_t g_cond;` + 启动时 init | RT-Thread 无 condvar，见下 |
| `pthread_attr_setstack` + `pthread_attr_setschedparam` | `sca_thread_attr_t` 的 `stack_bytes` / `priority` / `cpu` 字段 | 线程属性统一由 sca_os 管理 |
| `pthread_create` + `pthread_join` | `sca_thread_create` + `sca_thread_join` + `sca_thread_destroy` | join 语义降级，见阶段 1 |
| `usleep(200000)` | `sca_sleep_ms(200)` | 去掉 `unistd.h` |
| `clock_gettime(CLOCK_MONOTONIC)` | `sca_time_ms() / 1000` | RT-Thread 的 newlib 不保证有 CLOCK_MONOTONIC |
| `struct sched_param {.sched_priority = 90}` | `attr.priority = 9` | RT-Thread 优先级 0~31，需按比例折算 |

**为条件变量新增 port 原语**

业务层只有 reminder 用到条件变量（3 处），但它是"等外部事件 + 可被广播唤醒"的正确工具，换成信号量会丢失唤醒语义。RT-Thread 没有 condvar，`port/sca_os.h` 新增了 `sca_cond_t`（POD，14 个指针宽度），实现是"代次计数 + 信号量"：

- 等待者记录当前 `generation`，`waiters++`，释放外部锁，睡在内部信号量上；
- `broadcast` 把 `generation++`，并按 `waiters` 数量 post 信号量；
- 醒来后重新获取外部锁。

不保证 FIFO 公平性，但对"广播唤醒全部"的用法无影响。同样有静态断言保护尺寸。

**验证方式**

```bash
# 编译验证（11/11 通过）
INC="-Itests/stubs -I. -Ipackages/cagent/include -Ipackages/cjson"
for f in smart_home/{device,skills,tools,memory,reminder,core}/*.c; do
    tcc -c "$f" $INC
done
```

**miloco 的最终归属**：它原本是阶段 2 的尾巴，实际放在阶段 6 一起做 —— 因为它的 socket 直调要换成 `sca_http`，而 `sca_http` 的行为要等联网链路一起验证。结果是对的：client 从 376 → 约 220 行，service 的 7 个文件全部编译通过。

`opendir→sca_fs_list` 的遍历语义：循环式实现在 `while(readdir)` 内遇到错误就 `closedir` 后 return；回调式遍历不能提前关目录，改用 `ctx->error` 传递首个错误 + 回调返回 1 中止。语义保持"首个错误立即上报"。

**前置条件（进入阶段 3）**：cAGENT 的四个 port 就绪。

---

## 阶段 3 · cAGENT 平台适配 ✅

**目标**：cAGENT 内核在 RT-Thread 上编译并完成一次真实 HTTP 请求。

**已交付**

| 文件 | 行数 | 说明 |
|---|---:|---|
| `packages/cagent/` | — | 内核快照：`src/`（19 文件）、`codecs/`、`providers/`、`include/`。**删除** `ports/` 下不适用于本板的旧端口（`posix`/`tls`/`transport` 等） |
| `packages/cagent/ports/rtthread/agent_rtthread_runtime.h/.c` | 40 / 40 | 时钟端口，复用 `sca_time_ms()`，不含平台头 |
| `packages/cagent/ports/rtthread/agent_rtthread_transport.h/.c` | 60 / 350 | HTTP 端口，全部委托 `sca_http` |

**transport 为什么必须重写**

上游 `ports/transport/src/transport.c` 直接 `#include <netutils/webclient.h>`，那是 NuttX 的 netutils 包。RT-Thread 既没有它，其 webclient 软件包的模型也不同（以静态缓冲为前提，不支持流式投递）。

新端口只做两件翻译：

```
cAGENT 内核 ──agent_http_request_t──▶ 本端口 ──sca_http_request_t──▶ sca_http
            ◀──agent_http_sink_t───          ◀──sink 回调───
```

**两库的回调顺序差异**（这是实现里最需要留意的点）：

- cAGENT 要求**先 headers 后 body**；
- sca_http 是**边收边投递 body、headers 在请求返回后才有**。

当前实现采取的策略：body 通过 sink 实时投递，headers 在 `sca_http_request()` 返回后补投。代价是 body 先于 headers 到达消费者。若 cAGENT 的 provider 对顺序敏感（例如依赖 Content-Type 决定解析方式），需要改成先缓冲 body 再统一投递 —— 这是**待板端验证的风险点**。

**一处已修的 bug**：初版把请求头的字符串缓冲写成 `static char header_storage[16][96]`，多线程会串数据。已改为函数内局部数组，并把 `sca_http_request()` 调用移入同一作用域（保证 headers 指针在调用期间有效）。

**验证方式**

```bash
# 端口层编译通过
tcc -c packages/cagent/ports/rtthread/agent_rtthread_transport.c
tcc -c packages/cagent/ports/rtthread/agent_rtthread_runtime.c

# 内核编译 19/22 通过；3 个失败是 tcc 0.9.27 的限制
# （"index too large" 出现在块作用域内 const struct 数组初始化）
```

`react_loop.c:136`、`tool_guard.c:58`、`tool_registry.c` 的报错都是 tcc 的已知限制，arm-none-eabi-gcc 不会有此问题。**但这三个文件必须在真实工具链上复验**。

**agent 编排层（smart_home/agent/）**

| 改动 | 说明 |
|---|---|
| 删除 addons/ + quickapp/ 引用 | Node 网关 / MCP 桥 / App Bridge / 快应用，Kconfig 里本就默认关闭，合计 3.7k 行。`agent.c` 从 1107 → 992 行 |
| `pthread_mutex_t` → `sca_mutex_t` | `agent_mutex` 及 5 处访问 |
| `agent_rtthread_*` 端口 | runtime 与 transport 统一命名 |
| 独立 TLS 端口整体移除 | TLS 已下沉到 `port/sca_http`，backend 不再持有 tls 上下文 |
| `agent_posix_file_store` | session 持久化使用 POSIX 接口（RT-Thread DFS 兼容） |
| `syslog(...)` → `SCA_LOG*` | 12 处 |
| `smart_home_agent_run_service.c` 删除 | 只服务 App Bridge，无其它调用方 |

**backend 结构体精简**：去掉 5 个不再需要的字段
（`request_header_text[2048]`、`request_headers[16]`、`io[2048]`、
`response_headers[16]`、`tls`），共省约 5KB 静态 RAM。

**前置条件（进入阶段 4）**：需要 arm-none-eabi-gcc 与真实 RT-Thread 源码完成一次真实编译。

---

## 阶段 4 · 配置与持久化 ✅

**已交付**

| 文件 | 改动 |
|---|---|
| `config/smart_home_config_store.c` | `fopen+fseek/ftell+fread` → `sca_fs_read_all`；原子写从手写 `O_EXCL`+generation 后缀+目录 fsync（约 90 行）改成一行 `sca_fs_write_atomic` |
| `config/smart_home_secrets.c` | 同上；`pthread_mutex_t` → `sca_mutex_t`；`syslog` → `SCA_LOG` |
| `config/smart_home_backends.c` | 无需改动（纯 cJSON 逻辑） |
| `board/ports/sca_storage_port.c` | QSPI Flash 挂载 LittleFS，首次失败自动格式化（`SCA_STORAGE_AUTO_FORMAT` 控制） |

**为什么可以砍掉那 90 行原子写**

多线程同时写同一配置文件需要处理冲突，常见做法是
`O_EXCL` + 唯一 generation 后缀 + 重试 16 次 + 目录 fsync。但：

1. 配置写入本来就由设置页单线程触发；
2. `sca_fs_write_atomic` 内部已是 `写 .tmp → fsync → rename`，rename 的原子性由 littlefs 保证。

留那套复杂度只会让改动面变大，且掩盖真正的失败点。

**验证方式**：`tests/test_fs.c` 覆盖了原子写的三条承诺 ——
写后读回一致、覆盖写不残留旧内容、`.tmp` 被 rename 消耗。17 个断言全通过。

---

## 阶段 5 · UI 适配 ✅ 代码完成，待板上验证

---

### 图形绑定

新增 `port/sca_display.c`（190 行）—— 显示与触摸初始化的唯一封装：

| 参考 API | 新实现 |
|---|---|
| `lv_nuttx_dsc_init(&info)` + `lv_nuttx_init(&info, &result)` | `sca_display_bind(&bind)` |
| `lv_nuttx_deinit(&result)` | `sca_display_unbind(&bind)` |
| `lv_nuttx_uv_loop(...)`（libuv） | 删除；只留 `for(;;){ process_pending; lv_timer_handler; sleep }` |

flush 回调直接逐行 `memcpy` 进板级帧缓冲（800 宽一次 1600 字节），
不做格式转换、不做缩放。绘制缓冲用 48 行 × 2 块（约 150KB），
比整屏双缓冲省 1.3MB。

### 几何自适应

`smart_home_lvgl_internal.h` 的屏参从 1024×600 改为 800×480，
并**删除三个与屏宽绑定死的常量**：

```
SMART_HOME_NAV_X0   156    ← 删
SMART_HOME_NAV_STEP 248    ← 删
SMART_HOME_NAV_ITEM_W 216  ← 删
```

`nav.c` 改用 `lv_obj_set_flex_flow(LV_FLEX_FLOW_ROW)` +
`LV_FLEX_ALIGN_SPACE_EVENLY`，导航项宽度按 `(屏宽 - 80) / 3` 计算。
导航项内部的图标与文字也从写死的 `(58,10)` / `(102,14)` 改成 flex 居中。

这样以后换任何尺寸的屏都不需要再算这组数字。

### 线程简化

| 位置 | 原写法 | 新写法 |
|---|---|---|
| `lvgl_agent.c` 的对话 worker | `pthread_attr_setstack` + 手工 PSRAM 对齐 | `sca_thread_create` 的 `stack_bytes` 字段 |
| `lvgl_agent.c` 的 ASR worker | 同上 | 同上 |
| `room_dialog.c` 的落盘 worker | `pthread_attr_setdetachstate(DETACHED)` | 新增 `sca_thread_spawn_detached()` |

`sca_thread_spawn_detached()` 是本项目新增的 port 原语：
线程函数返回后自行释放栈与包装结构，等价于 `pthread_detach`。
它解决的是 UI 里"一次性小任务不需要 join"这一类用法（现有 4 处）。

### 删除的可选面板

`settings_v9.c` 的"设备与服务"页删掉了 MCP 与局域网 Node 两行
（对应组件已在阶段 3 移除），只保留米家网关。文件从 2752 → 约 2500 行。

### 字体（待定）

完整 MiSans 7.9MB 放不进 16MB HyperRAM（帧缓冲已占 1.5MB）。
需要在板上验证时用 `scripts/subset_font.sh` 子集化到 2~3MB。
这是阶段 5 唯一未闭环的项，但不阻塞代码。

---

## 阶段 6 · 联网与云端对话 ✅ 代码完成，待真机验证

汇聚点。依赖阶段 2~5 全部完成。

**已备好的基础设施**
- `port/sca_http.c` 的 HTTP/HTTPS 客户端（含 mbedTLS 握手、超时、流式 sink）
- `board/ports/sca_wifi_port.c` 的四态机（init → carrier → ip → online）+ DNS 探测
- `packages/cagent/ports/rtthread/` 的 runtime 与 transport

**已交付**

| 文件 | 改动 |
|---|---|
| `net/smart_home_network.c` | 从 562 行重写为 229 行。`board_esp_hosted_wifi_connect` / `wapi_*` / `netlib_get_ipv4addr` / `dns_add_nameserver` 全部换成 `sca_wifi_port_*` 四态机调用 |
| `net/smart_home_wifi.c/.h` | **删除**（ESP-Hosted 直调封装，已由 `board/ports/sca_wifi_port.c` 取代） |
| `miloco/smart_home_miloco_client.c` | 手写 socket + poll + 逐字节头解析（约 240 行）→ 委托 `sca_http`。`Date` 头解析保留（原设计用它对时，不跑 SNTP） |
| `miloco/smart_home_miloco_service.c` | `sem_timedwait(CLOCK_REALTIME)` → `sca_sem_wait` 相对超时；worker 线程改 `sca_thread_create`，保留"绑 CPU1 与 UI 分核"的原决策 |
| `miloco/*` | `pthread_*` / `sem_*` / `syslog` / `sleep` 全部替换 |
| `port/sca_display.c` 新增 | 见阶段 5 |
| `packages/qrcodegen/` 新增 | 米家扫码绑定用的 QR 编码库 |

**状态机映射保持不变**

网络采用四态设计（init → carrier → ip → dns）：

```c
switch (port->state) {
case SCA_WIFI_STATE_INIT:    /* 设备就绪，未关联 */        → if_up 失败
case SCA_WIFI_STATE_CARRIER: /* 已关联 AP，未拿到 IP */    → DHCP 失败
case SCA_WIFI_STATE_IP:      /* 有 IP，DNS 未验证 */      → 有 IP
case SCA_WIFI_STATE_ONLINE:  /* DNS 解析成功 */           → online
}
```

**待真机验证**
- CYW55512 三个固件的 YMODEM 上传（README 已写流程）
- 网络缓冲调优（IOB 过小会导致握手失败；RT-Thread 对应 `RT_LWIP_TCP_MSS` / `RT_LWIP_TCP_RECVMBOX_SIZE`）
- transport 的 headers/body 顺序问题（见阶段 3 风险点）

---

## 环境状态

| 项 | 状态 |
|---|---|
| RT-Thread 5.0.2 源码 | ✅ `D:/rt-thread-env/rt-thread`（include/src/libcpu/components/tools 完整） |
| arm-none-eabi-gcc | ⬜ 多次下载被截断，未成功。需手动下载或换网络环境 |
| Python scons 4.11 | ✅ 已安装 |
| 宿主 C 编译器 | ✅ tcc 0.9.27（临时置于 `%TEMP%/tcc`，仅供语法检查，非交付依赖） |

**工具链下载受阻的说明**：GitHub 大文件在本网络环境下反复被截断（约 20~60MB 后断流），已尝试 Python urllib / curl / wget、更换代理端口、gitee 镜像、ARM 官方 CDN 均未成功。建议从 RT-Thread Studio 的 SDK Manager 安装（它会自带工具链），或手动下载 `xpack-arm-none-eabi-gcc-14.2.1` 解压到 `D:/rt-thread-env/toolchain/`。

---

## 目录现状

```
edgi_smart_home/
├── SConstruct / SConscript / Kconfig / README.md / .gitignore
├── applications/          main.c + SConscript            ✅
├── board/                 board.c/h + ports/×5 + SConscript ✅
├── port/                  8 头 + 3 实现 + Kconfig + SConscript ✅
├── smart_home/
│   ├── core/              app.h/.c, tool_metadata.h       ✅
│   ├── device/            4 文件                            ✅
│   ├── skills/            6 文件                            ✅
│   ├── tools/             5 文件                            ✅
│   ├── memory/            ⬜
│   ├── reminder/          ⬜
│   ├── miloco/            ⬜
│   ├── agent/             ⬜
│   ├── config/            ⬜
│   ├── net/               ⬜
│   ├── ui/                ⬜
│   ├── voice/ camera/     ⬜（阶段 7/8）
│   ├── Kconfig            ✅
│   └── SConscript         ✅
├── packages/
│   ├── cagent/            内核 + ports/rtthread          🚧
│   ├── cjson/             cJSON 1.7.18                    ✅
│   └── SConscript         ✅
├── resources/             skills 待搬 / 字体待子集化        ⬜
├── tests/                 stubs/ + 3 个宿主桩 + test_fs + 运行器 ✅
└── tools/check_no_os_deps.py                              ✅
```

---

## 字体：改成纯英文（替代 MiSans）

**决策**：UI 不再加载外部 TTF，全部改用 LVGL 内置 Montserrat。

**为什么**。不从 `/res/fonts/MiSans-Normal.ttf` 读 7.9MB 字体进堆，
再用 TinyTTF 建 20 个字号实例。两个代价：

| 项 | 数值 |
|---|---|
| 字体文件常驻堆 | 7.9 MB |
| 20 个实例的字形缓存 | 约 1.5 MB |
| 冷启动（LittleFS 上大量小块 seek 定位 CJK 字形） | 数秒，期间 UI 空白 |
| 内置 Montserrat（12 个尺寸，编进 .rodata） | 约 500 KB |
| 字体加载时间 | 0 |

16MB HyperRAM 里还有 1.5MB 帧缓冲 + 150KB LVGL 绘制缓冲 + 8~16KB
模型响应缓冲。省下 9MB 换来的是：模型上下文可以开大、会话历史可以多留。

**怎么做**。分三步，每步都可独立验证：

1. **文案抽取**。`tools/ui_strings_en.py` 是唯一的文案真相源，441 条
   中文映射按领域分表（导航/设备/房间/语音/网络/模型/工具/健康…）。
2. **自动替换**。`tools/localize_ui.py` 逐字符串精确匹配替换，
   任何没有映射的中文串都让脚本失败退出（宁可脚本红，也不要屏上出现
   缺字方块）。同时跳过注释与串口日志 —— 日志在终端显示，中文更易读。
3. **字体收敛**。`smart_home_lvgl_style.c` 改成 20 个槽位到内置字号的
   静态映射表，96 号（屏保时钟）映射到 48。

**一处连带改动**：日期格式。原来是 `"%d月%d日 星期%s"`，英文化后改成
`"Mon, Nov 11"`，用两张静态表（7 个星期 + 12 个月）而不是 `strftime` ——
newlib-nano 的 `strftime` 在部分配置下 `%a/%b` 返回空串。

**一处删掉的代码**：`miloco_service.c` 里的天气中文映射表
（`g_weather_cn_map`，19 条）与 `condition_cn` 字段。UI 只用
`condition_en`，中文映射不可能被渲染，留着只是死代码。

**系统提示词也改成英文**。这不是"偏好"问题：`g_system_prompt` 原本要求
模型"必须始终使用简体中文回复"，而 UI 已无 CJK 字形 —— 那条规则会让
每个气泡都变成方块。改成 always reply in English，并保留"用户用中文
提问也照样用英文答"的说明。

**验证**

```bash
python tools/localize_ui.py --check    # 可替换字符串 0 / 缺失 0
```

`--check` 输出 0 是可替换字符串已经是 0 的结果（全部换完），
不是脚本失效：脚本只统计含中文的字面量，换完后自然没有可统计对象。

**保留中文的地方**（有意为之）

| 位置 | 原因 |
|---|---|
| `memory/smart_home_user_memory.c` 的关键词表 | 匹配中文用户输入，必须是中文 |
| 所有 `SCA_LOG*` / `printf` | 串口终端显示，中文更易读 |
| 代码注释 | 给维护者看，中文更准确 |
| `agent/smart_home_agent.c` 的注释 | 同上 |

---

## 工具链：已装好

| 项 | 路径 / 版本 |
|---|---|
| arm-none-eabi-gcc | `D:/rt-thread-env/toolchain/arm-gnu/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin` |
| 版本 | Arm GNU Toolchain 13.3.Rel1 (Build arm-13.24), GCC 13.3.1 |
| 来源 | ARM 官方 CDN（`armkeil.blob.core.windows.net`），297MB，23 秒下完 |
| RT-Thread | `D:/rt-thread-env/rt-thread`（5.0.2） |

之前的 GitHub / gitee 下载全部失败，原因是代理端口每次会话都变。
ARM 官方 CDN 走新代理能到 12.8 MB/s。

**必须的编译选项**

```bash
-mcpu=cortex-m55 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16   # 目标
-D_POSIX_C_SOURCE=1     # 不加会让 newlib 的 sys/types.h 提前拉进
                        # sys/time.h，suseconds_t / clock_t 全未定义
-D__RTTHREAD__          # LVGL 据此走 RT-Thread 分支
-DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE
                        # 强制用 packages/lvgl/lv_conf.h
```

**五条踩过的坑（都写进了 cross_compile_check.sh 的注释）**

1. **不能把 LVGL 的所有子目录都加进 `-I`**。
   `src/libs/thorvg/rapidjson/msinttypes/stdint.h` 是 MSVC 专用头，
   一旦进路径就会遮蔽系统的 `stdint.h`，整个工程编不过。
   LVGL 的头用相对路径互相引用（`"../misc/lv_assert.h"`），
   只需要 `packages/lvgl` 和 `packages/lvgl/src` 两个入口。

2. **不能加 `components/libc/compilers/common/extension`**。
   那是给 Keil/IAR 这类只有 ISO C 的工具链用的，它的 `sys/types.h`
   与 newlib 的冲突（`ssize_t` 重复定义）。
   GCC 下只用 `compilers/common/include`。

3. **SAL 有两套 socket 头，只能选一套**。
   `<sal_socket.h>` 自带 `sockaddr_storage` 定义，
   `<sys/socket.h>`（在 `sal/include/socket/sys_socket/` 下）在
   `SAL_USING_POSIX` 下提供标准 POSIX 声明。两个都 include 会重复定义。
   本工程统一走后者。

4. **`LV_CONF_SKIP` 不能写成 `-DLV_CONF_SKIP=0`**。
   `lv_conf_internal.h` 的判断是 `#if !defined(LV_CONF_SKIP)`，
   只看有没有定义 —— 写 `=0` 反而会让整个 `lv_conf.h` 被跳过，
   于是所有字体与 widget 都回到默认值。

5. **`rtconfig.h` 里 `#ifdef` 只看有没有定义**。
   写 `#define CONFIG_SCA_CAMERA  /* 不定义 */` 仍然算定义了，
   而 `sca_config.h` 用的是 `#ifdef` —— 那行注释不会让它关掉。
   必须整行注释掉。

**交叉编译结果**

```
port + smart_home + board + applications   197 / 197 通过
packages/cagent（内核 + codecs + 端口）      25 / 25 通过
packages/lvgl（12 个字号 + 全部启用模块）    279 / 279 通过
RT-Thread 内核 src/                         15 / 17 通过（cpu.c 与
                                            scheduler_mp.c 是 SMP 专用，
                                            非 SMP 配置下不参与构建）
```

**还没做的**：链接。没有 BSP 的链接脚本、启动文件与 Flash 分区表，
`sca_os_rtthread.c` 里引用的 `rt_hw_board_init` 等符号在板级 port 里，
而板级 port 依赖 Infineon ModusToolbox 的 HAL —— 那部分需要真实的
Edgi-Talk SDK。当前链路是"编译全通，链接待 BSP"。
