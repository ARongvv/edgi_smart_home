# 设备卡片与 Agent 的公共控制流程

卡片与 `miot_device_control` 都使用 MiLoCo 服务层的
`control_begin → control_poll → control_release`。公共层校验设备、在线状态、
能力、操作类型和值，并分配请求 ID；后台发送前再次校验。两种入口不各自维护确认规则。
卡片用 LVGL 定时器查询，Agent 只在自己的工作线程等待（每次睡眠最多 20 ms）。

## 成功与未确认

| 返回 reason | 含义 |
| --- | --- |
| `gateway_confirmed_action` | 网关确认动作命令；不证明云台已实际完成动作 |
| `gateway_accepted_write` | 网关接受不可回读属性写入 |
| `device_state_confirmed` | 网关回执成功，发送后的新属性观测与目标一致 |
| `current_state_confirmed` | 回执不确定，但新属性观测与目标一致；不保证由本次请求造成 |
| `gateway_result_unconfirmed` | 动作/只写属性未获有效回执，可能已经执行 |
| `dispatched_result_unconfirmed` | 已开始发送，等待超时或调用方取消，执行结果未知 |
| `deadline_before_dispatch` / `cancelled_before_dispatch` | 未开始发送，已从队列移除或在执行闸口阻止 |

Agent 输出 `ok/accepted/request_id/state/code/reason/dispatched/message`。
`accepted` 只表示曾入队，`ok` 只在公共确认规则通过后为 true；未知结果返回工具错误。
模型工具说明与 `resources/skills/smart_home_mijia.md` 都要求如实报告，未知结果不自动重发动作。
资源文档需要按工程既有流程同步到 `/littlefs/res/skills/`。源码文件更新不自动证明
板端资源已同步；验收日志未记录该资源文件的哈希，因此不对其具体部署版本作推断。

## 生命周期与并发

- 卡片默认最多等待 10 秒；Agent 取 10 秒与整轮剩余时间的较小值。均使用单调时钟。
- 已排队控制优先于下一次天气/设备轮询；正在进行的 HTTP 不会被抢占。
- 每次 begin 占用一个受保护结果槽（共 8 个），直到调用方 release。
  同一设备、同一 iid 的重叠请求返回 `control_busy`；不同控制项可以排队。
- 卡片关闭、Agent 取消或超时会撤销尚未发送的命令。发送中的 HTTP 无法撤回，
  保留“未确认”语义；槽在 HTTP 结束前不可复用，迟到回执不会把已报告的未知结果改为成功。
- 队列/结果槽耗尽明确拒绝提交；不覆盖仍被等待的回执。旧有 scene/power 提交接口
  继续支持入队语义，也经过同一参数校验，但不会保留回执供长期等待。
- 网关重配置或设备能力变化会使旧排队请求在发送前被拒绝。
- 关停仍沿用原有调用顺序：调用方须先停止 UI/Agent 对服务的访问，再销毁服务。

## 日志

`[control]` 的 `validate/submit/tool.bind/http/complete/release` 阶段记录请求 ID，
并关联入口（card/agent）、Agent call_id、did、iid、操作和值。
HTTP 阶段记录传输码、HTTP 状态和响应容量；传输失败时字节数为 `unknown`，不伪造已接收长度。
日志不打印 token、请求凭据或完整响应体。

## 验证边界

`tests/test_miloco_control.c` 使用真实服务队列、执行函数、响应解析器及已注册的 Agent
工具适配器，以可控时钟/HTTP/调度桩驱动，不依赖真实网关。覆盖两个入口请求一致、
成功前等待、无效响应/传输失败/业务拒绝、排队和发送中超时、取消、旧缓存防误判、
属性回读、只写属性、能力变化、网关变化、重复提交、容量上限和回执保留。
这不等于 RT-Thread 并发验收或真实设备动作验证。

板上验收应对同一动作分别从卡片和 Agent 触发，核对：

1. did、iid、操作类型一致，每个请求最多发送一次。
2. Done 仅在收到确认后出现；网关确认与物理动作分别观察。
3. 断网/无效回执时两入口都显示失败或未确认，不谎报已执行，也不自动重发动作。
4. 超时/取消后，未发送请求不会稍后执行，界面保持响应。

## 2026-11-14 真机复测

Agent 校准请求收到 `gateway_confirmed_action` 后才出现 `tool.end code=0`，
首轮 `turn.complete` 的执行/会话/交付均为 0。卡片使用同一 did、`action.2.1`
和 action 操作，也收到匹配网关确认；操作者确认云台执行校准、界面正常。
这是本次设备与命令的验证，不覆盖所有设备、异常路径或长期并发稳定性。

同一监听窗口内，另有一次卡片属性写入的回执解析失败，随后新状态回读符合目标，
记录为 `current_state_confirmed`；该结果不能冒充成功动作回执，也不能据此认定回执协议完全无误。
脱敏日志和测量边界见 [2026-11-14 验收记录](validation/2026-11-14-gimbal-calibration.md)。
