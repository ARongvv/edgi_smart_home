# 2026-11-14 米家摄像头云台校准验收记录

## 范围与来源

- 时间：2026-11-14，Asia/Shanghai（UTC+8）。下方时间戳为监听端采集时间。
- 设备连接：KitProg3 UART，115200 8N1；监听只接收，没有通过串口下发控制命令。
- 触发入口：操作者发送 Agent 校准命令，并在监听窗口内操作设备卡片。
- 原始采集：本次会话保存的 `edgi_serial_gimbal_calibration_06x2mg3y.log`；
  本文选录相关行，排除二进制噪声、无关轮询与网络配置内容，替换设备标识和调用 ID。
  `[camera]` 始终表示同一设备，两个调用别名分别对应原调用 ID；请求 ID、阶段、返回码、
  时间戳和容量数值保持原值。
- 源码核对：文档更新时工作区 HEAD 为 `bb21203`，工作区含既有改动。
  板端日志未包含可追溯的构建 ID，不将该 HEAD 认定为实际刷入固件的版本。

## 实际观察

| 对象 | 结果与边界 |
| --- | --- |
| Agent 首轮（turn=2，request=2） | `action.2.1` / `action`；HTTP 200，动作回执为成功；之后才出现工具成功事件 |
| 首轮会话 | `turn.complete code=0 session=0 delivery=0`，未复现容量错误或 `Chat incomplete` |
| 现场观察 | 操作者明确确认：“云台执行了校准，界面正常” |
| 卡片动作（request=4、5） | 同一设备、iid、操作类型，均收到 `gateway_confirmed_action`；分别记录为独立卡片提交，不推断为自动重试 |
| 后续 Agent（turn=3，request=6） | 网关动作回执成功、工具返回 0；监听窗口结束时仅捕获到后续模型 HTTP 200，未捕获该轮最终会话状态 |
| 卡片属性（request=3） | `prop.2.4` 写入回执无法识别，随后新状态回读符合目标，报告 `current_state_confirmed`；不认定回执解析问题已全部修复 |

首轮从提交（19:04:58.943）到网关确认（19:04:59.428）为 485 ms，
确认后工具在 19:04:59.444 返回成功；整轮日志报告 6242 ms。
这是一轮样本，不能作为平均、P95 或最坏时延。网关动作确认与现场观察是两类证据，分别记录。

首轮 JSONL 完整记录所需 5067/131072 字节，scratch 峰值 16332/65536 字节；
模型请求最大 15887/65536 字节。本次未超限，不代表长会话、文件整理或全板内存峰值已经验证。

## 脱敏串口摘录

```text
19:04:58.943 : [control] stage=submit source=agent request=2 did=[camera] iid=action.2.1 op=action value=0
19:04:58.952 : [control] stage=tool.bind request=2 call_id=[calibration-1] code=0 reason=queued
19:04:59.419 : [control] stage=http request=2 did=[camera] iid=action.2.1 op=action transport=0 http=200 bytes=238 capacity=32768
19:04:59.428 : [control] stage=complete request=2 raw_code=0 code=0 reason=gateway_confirmed_action sent=1
19:04:59.435 : [control] stage=release request=2 code=0 reason=gateway_confirmed_action sent=1
19:04:59.444 [agent_diag] turn=2 stage=tool.end tool=miot_device_control call_id=[calibration-1] code=0
19:04:59.471 [agent_diag] turn=2 stage=model.request bytes=15887 capacity=65536
19:05:00.847 [agent_diag] turn=2 stage=session.append role=3 tool=- call_id= code=0 content_bytes=98 encoded_bytes=129 used_before=4927 used=5057 required=5067 capacity=131072 messages=8/48 calls=3/48
19:05:01.263 [agent_diag] turn=2 stage=turn.complete code=0 session=0 delivery=0 models=4 tools=3 scratch_peak=16332 scratch_capacity=65536 output_bytes=98 output_required=98
19:05:01.266 : run ok elapsed=6242ms out=98
19:08:45.448 : [control] stage=submit source=card request=3 did=[camera] iid=prop.2.4 op=set value=0
19:08:45.808 : [control] stage=http request=3 did=[camera] iid=prop.2.4 op=set transport=0 http=200 bytes=155 capacity=32768
19:08:45.812 : [miloco] control response: invalid result
19:08:46.505 : [control] stage=complete request=3 raw_code=-10001 code=0 reason=current_state_confirmed sent=1
19:08:46.572 : [control] stage=release request=3 code=0 reason=current_state_confirmed sent=1
19:08:48.517 : [control] stage=submit source=card request=4 did=[camera] iid=action.2.1 op=action value=0
19:08:49.015 : [control] stage=http request=4 did=[camera] iid=action.2.1 op=action transport=0 http=200 bytes=239 capacity=32768
19:08:49.023 : [control] stage=complete request=4 raw_code=0 code=0 reason=gateway_confirmed_action sent=1
19:08:49.031 : [control] stage=release request=4 code=0 reason=gateway_confirmed_action sent=1
19:08:49.039 : [control] stage=submit source=card request=5 did=[camera] iid=action.2.1 op=action value=0
19:08:49.338 : [control] stage=http request=5 did=[camera] iid=action.2.1 op=action transport=0 http=200 bytes=239 capacity=32768
19:08:49.346 : [control] stage=complete request=5 raw_code=0 code=0 reason=gateway_confirmed_action sent=1
19:08:49.422 : [control] stage=release request=5 code=0 reason=gateway_confirmed_action sent=1
19:09:36.526 : [control] stage=submit source=agent request=6 did=[camera] iid=action.2.1 op=action value=0
19:09:36.535 : [control] stage=tool.bind request=6 call_id=[calibration-2] code=0 reason=queued
19:09:37.669 : [control] stage=http request=6 did=[camera] iid=action.2.1 op=action transport=0 http=200 bytes=238 capacity=32768
19:09:37.678 : [control] stage=complete request=6 raw_code=0 code=0 reason=gateway_confirmed_action sent=1
19:09:37.685 : [control] stage=release request=6 code=0 reason=gateway_confirmed_action sent=1
19:09:37.694 [agent_diag] turn=3 stage=tool.end tool=miot_device_control call_id=[calibration-2] code=0
19:09:39.239 [agent_diag] turn=3 stage=model.http status=200
```

## 文档更新时的自动化核对

测试对象为当时本地 `edgi_smart_home` 工作区；编译器由运行器选取 GCC。
执行 `python3 tests/run_host_tests.py`，结果为 **10/10 passed**：

- `test_fs`
- `test_rtthread_transport`
- `test_http_body`
- `test_agent_loop`
- `test_device_persistence`
- `test_openai_provider`
- `test_miloco_protocol`
- `test_agent_capacity`
- `test_backend_capacity`
- `test_miloco_control`

`test_miloco_control` 驱动真实队列、执行与解析代码及 Agent 工具适配器，但使用可控时钟、
调度/锁和 HTTP 桩，不是 RT-Thread 并发、真实网络或实际硬件验收。

平台依赖守卫仍报告已知的 1 项违规，未因文档更新修改代码：

```text
FAIL  1 violation(s):
  smart_home/core/smart_home_memory.h:19  <rtthread.h>   (RT-Thread core header)
```

此前控制修改完成了三个 ARM 目标文件的编译检查，使用 RT-Thread 声明桩及真实 LVGL 头文件。
本次文档更新没有重新完整链接、烧录、重启或触发设备动作，也没有验证板端资源文件哈希。

## 后续验收

- 更多设备、动作、属性与模型后端需分别验证。
- 断网、无效回执、取消/超时、队列拥塞及同一控制项并发需在板上测试。
- 继续测量长期对话、会话文件整理、UI 并行运行时的内存峰值和稳定性。
- Session、用户偏好和提醒的重启/掉电恢复分别验收，不重放已执行的设备动作。
- 补齐可追溯构建 ID、资源版本、真实界面截图及现场录像，形成版本对应关系。
