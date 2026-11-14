# Agent 容量与失败诊断

`port/sca_agent_profile.h` 统一定义本项目调整的 Agent 工作区、模型缓冲与会话预算。
工具描述等注册元数据还受 cAGENT 独立上限约束，见文末。
固件 C/C++ 构建、独立交叉编译检查和宿主测试均强制包含它；不要在
被 Git 忽略的 `packages/` 中维护另一份配置。`port/sca_config.h` 的
`SCA_*` 容量只作为兼容别名，实际分配与边界检查使用同一配置。

本配置参照 2026-11-14 本机 OpenVela `nuttx/.config` 的 SmartHome 配置。
这表示配置对齐。2026-11-14 云台校准已获操作者确认实际动作，详见文末验收记录；
其他设备、持续运行与会话整理时的内存峰值仍需实测。

| 项目 | 字节数 / 数量 |
|---|---:|
| Core 工作区（包含 scratch） | 98304 |
| scratch | 65536 |
| 输入 / 上下文 / 工具输出 / 模型输出 | 2048 / 8192 / 4096 / 4096 |
| 工具槽 / 投影消息数 | 24 / 48 |
| 模型请求 / 响应 / 解码 | 65536 / 16384 / 8192 |
| 模型 JSON token 数 | 512 |
| 会话 JSONL 写入 / 读取，各自 | 131072 |
| 会话解码 / token 工作区 | 65536 / 16384 个 int |
| 模型历史投影 / 磁盘最多保留 | 3 轮 / 20 轮 |
| 会话文件上限 | 1048576 |

会话解码从 512 KiB 调整为 64 KiB，补足 JSONL 行与 Core 工作区、模型请求
预算后，大数组总预算约减少 96 KiB（不含结构体增量和对齐）。Core scratch
包含在 Core 工作区内，不额外计入。界面最终输出缓冲另外保留一个 NUL 字节。会话整理目前还会临时读取整个会话文件，
接近 1 MiB 文件时仍须检查 2 MiB HyperRAM 池的峰值和可分配空间。

## 日志

- `[agent_profile]`：启动时的编译容量。确认新固件已生效时先查看它。
- `tool.begin` / `tool.end`：本轮编号、工具名、调用 ID、参数长度和返回码。
- `session.append`：角色、对应工具、调用 ID、原始内容及编码后的字节数、
  追加前后已用字节、完整记录所需字节和真实行容量，并附消息及调用槽占用。
  `required` 包含 JSON 转义、逗号、记录尾部和终止 NUL。
- `session.begin` / `session.history` / `session.finish` / `session.prepare`：
  区分打开、历史读取、完成和文件整理失败。
- `model.request` / `model.http` / `model.response` / `model.decode`：
  区分请求编码、传输和响应解码。只记录状态和长度，不输出密钥、完整参数或响应。
- 终态记录包含 execution/session/delivery 三种状态、模型和工具调用数、
  scratch 峰值与容量、最终输出实际和所需长度。

尚未完成的 HTTP 响应、provider 内部失败编码所需总长度无法由当前公开接口
准确取得，记录 `required=unknown` 和已观察到的字节数、各层真实容量。
不能把已接收长度冒充完整响应长度。`last_tool` 仅表示最近执行的工具；
只有 `session.append` 的 `tool` 是按调用 ID 对应到该条工具结果的名称。

工具结束事件仍早于会话追加，后续存储失败不代表工具未执行。
当前 `miot_device_control` 与设备卡片共用确认流程，入队本身不直接返回工具成功：
动作确认表示网关接受动作命令，可读属性按新状态核对，未获确认则报告未知。
旧 scene/power 提交接口仍保留入队语义，不能把所有米家入口都描述为完成确认。
容量错误不会重试或重放工具。界面明确显示 conversation/agent capacity exceeded，
出现过工具时提醒核对设备状态后再重试。

## 验证

```bash
python3 tests/run_host_tests.py
```

`test_agent_capacity` 使用真实 Core、JSONL 编码器和 Storage Provider，配合内存文件
及模拟模型：验证 JSON 转义计量一致、包含尾部/NUL 的精确边界、超过旧 4 KiB
的完整工具回合、旧上限下工具成功后会话容量报错、失败不覆盖已有历史，以及
重新绑定 Storage 后恢复历史且不重放工具。

固件使用 README 中的 SDK 和 SCons 构建方式。2026-11-14 已监听一次成功的 Agent
云台校准回合，网关确认后工具返回成功，执行/会话/交付均为 0；操作者确认云台实际动作、
界面正常。首轮 JSONL 所需 5067/131072 字节，scratch 峰值 16332/65536 字节；
这验证本次请求未超限，不代表长期对话或会话整理的最坏内存峰值已验证。
脱敏摘录见 [2026-11-14 验收记录](validation/2026-11-14-gimbal-calibration.md)。

`test_backend_capacity` 进一步覆盖实际 backend/provider/诊断 transport 的正常分段回复、
HTTP 401、响应容量超限、解码失败和资源释放；该测试使用 RAM 会话分支，
并不连接云服务。持久化分支另以真实 JSONL provider 的测试及 ARM 编译检查覆盖。

## 注册字段的独立上限

运行时工作区足够不代表工具一定能注册。当前 cAGENT 默认工具描述上限
`AGENT_MAX_DESCRIPTION_BYTES` 为 256 字节；超限返回 `AGENT_ERROR_LIMIT`，
关键工具注册失败会使当前应用装配中止。`[tool] register failed` 会记录工具名、
描述及 Schema 字节数和返回码。该上限来自 cAGENT 元数据配置，与上表的模型/会话预算分别核对。
