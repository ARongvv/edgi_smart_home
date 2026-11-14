#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
UI 文案中英映射表。

为什么单独成文件而不是散落在代码里：
  1. 这是唯一的"文案真相源"，审查时一处就能看全；
  2. 英文化后 UI 只依赖 LVGL 内置 Montserrat，不再需要 MiSans
     （完整 MiSans 7.9MB，16MB HyperRAM 里还要放 1.5MB 帧缓冲）；
  3. 后续如果要做其他语言，加一张表即可，替换逻辑不动。

约定：
  - 键是源码里出现的原始字符串（不含两侧引号，保留 \\n 转义）；
  - 值是英文替换，格式符（%d/%s/%u/%02u）位置与数量必须一致；
  - 键里出现的 … 「」 · ✓ ° 等非 ASCII 标点，英文侧换成 ASCII 或
    LVGL 内置符号，避免内置字体缺字。

用法见 tools/localize_ui.py。
"""

# ── 导航与全局 ────────────────────────────────────────────

NAV = {
    '首页': 'Home',
    '聊天': 'Chat',
    '设置': 'Settings',
    '我的家': 'My Home',
    '轻触进入首页': 'Tap to enter',
}

# ── 通用词汇（多处复用）────────────────────────────────────

COMMON = {
    '关闭': 'Off',
    '已关闭': 'Off',
    '开启': 'On',
    '已开启': 'On',
    '开': 'On',
    '关': 'Off',
    '确定': 'OK',
    '取消': 'Cancel',
    '返回': 'Back',
    '完成': 'Done',
    '失败': 'Failed',
    '成功': 'Done',
    '保存': 'Save',
    '删除': 'Delete',
    '添加': 'Add',
    '执行': 'Run',
    '刷新': 'Refresh',
    '关闭后': 'When off',
    '无': 'None',
    '未知': 'Unknown',
    '在线': 'Online',
    '离线': 'Offline',
    '未连接': 'Disconnected',
    '已连接': 'Connected',
    '未配置': 'Not configured',
    '已配置': 'Configured',
    '未绑定': 'Not linked',
    '已绑定': 'Linked',
    '待绑定': 'Link now',
    '不可用': 'Unavailable',
    '未启用': 'Disabled',
    '未确认': 'Unconfirmed',
    '未检查': 'Not checked',
    '待确认': 'Pending',
    '等待确认': 'Waiting',
    '状态待获取': 'Fetching',
    '状态待确认': 'Unconfirmed',
    '版本未知': 'Unknown version',
    '调用中': 'Calling',
    '执行中': 'Running',
    '运行中': 'Running',
    '待机': 'Idle',
    '启动中': 'Starting',
    '停止中': 'Stopping',
    '预览中': 'Previewing',
    '预览关闭': 'Preview off',
    '正在思考…': 'Thinking...',
    '正在保存…': 'Saving...',
    '正在保存，请稍候…': 'Saving, please wait...',
    '正在保存，请稍候再操作': 'Saving, try again shortly',
    '正在识别…': 'Recognizing...',
    '正在停止采集…': 'Stopping capture...',
    '启动中，等待首帧…': 'Starting, waiting for frame...',
    '正在连接，请稍候…': 'Connecting, please wait...',
    '检查中…': 'Checking...',
    '正在检查': 'Checking',
    '正在读取工具目录…': 'Loading tool list...',
    '暂无工具说明': 'No description',
    '暂停': 'Pause',
    '继续': 'Resume',
    '上一页': 'Prev',
    '下一页': 'Next',
    '停止': 'Stop',
    '发送': 'Send',
    '知道了': 'Got it',
    '稍后再说': 'Later',
    '开始使用': 'Get started',
    '重新配置': 'Reconfigure',
    '去配置': 'Configure',
    '重新保存': 'Save again',
    '确认删除': 'Delete',
    '确认分配': 'Assign',
    '新增房间': 'Add room',
    '房间管理': 'Rooms',
    '全部': 'All',
}

# ── 房间名 ────────────────────────────────────────────────

ROOMS = {
    '客厅': 'Living Room',
    '卧室': 'Bedroom',
    '书房': 'Study',
    '阳台': 'Balcony',
    '厨房': 'Kitchen',
    '餐厅': 'Dining',
    '儿童房': 'Kids Room',
    '客房': 'Guest Room',
    '未分配': 'Unassigned',
}

# ── 设备名与状态 ──────────────────────────────────────────

DEVICES = {
    '吊灯': 'Ceiling Light',
    '立式空调': 'AC Unit',
    '洗衣机': 'Washer',
    '扫地机器人': 'Robot Vacuum',
    '落地风扇': 'Floor Fan',
    '摄像头': 'Camera',
    '加湿器': 'Humidifier',
    '空气净化器': 'Air Purifier',
    '智能插座': 'Smart Plug',
    '温湿度计': 'Thermo-Hygrometer',
    '设备开关': 'Power',
    '亮度': 'Brightness',
    '亮度 / %': 'Brightness / %',
    '目标温度': 'Target',
    '目标温度 / °C': 'Target / degC',
    '运行模式': 'Mode',
    '风速': 'Fan speed',
    '风速 / 档': 'Fan / level',
    '洗涤程序': 'Wash cycle',
    '清扫方式': 'Clean mode',
    '送风方式': 'Air mode',
    '左右摇头': 'Oscillate',
    '运行 / 暂停': 'Run / Pause',
    '返回基站': 'Return to base',
    '结束洗涤': 'End cycle',
    '剩余时间': 'Remaining',
    '电量': 'Battery',
    '高度': 'Height',
    '项数': 'Items',
    '制冷中': 'Cooling',
    '洗涤中': 'Washing',
    '清扫中': 'Cleaning',
    '制冷': 'Cool',
    '制热': 'Heat',
    '除湿': 'Dry',
    '送风': 'Fan',
    '自动': 'Auto',
    '低': 'Low',
    '中': 'Mid',
    '高': 'High',
    '标准洗': 'Normal',
    '快洗': 'Quick',
    '脱水': 'Spin',
    '扫地': 'Sweep',
    '拖地': 'Mop',
    '先扫后拖': 'Sweep+Mop',
    '标准风': 'Normal',
    '自然风': 'Natural',
    '睡眠风': 'Sleep',
    '洗涤': 'Wash',
    '清扫': 'Clean',
    '回充': 'Charge',
    '本机': 'Local',
    '32分': '32 min',
    '2档': 'Level 2',
}

# ── 场景 ──────────────────────────────────────────────────

SCENES = {
    '回家模式': 'Home',
    '观影模式': 'Movie',
    '睡眠模式': 'Sleep',
    '离家模式': 'Away',
    '场景已提交': 'Scene sent',
    '场景未执行': 'Scene failed',
}

# ── 天气 ──────────────────────────────────────────────────

WEATHER = {
    '晴天': 'Clear',
    '多云': 'Cloudy',
    '阴': 'Overcast',
    '小雨': 'Light rain',
    '中雨': 'Rain',
    '大雨': 'Heavy rain',
    '雷阵雨': 'Thunder',
    '雪': 'Snow',
    '雾': 'Fog',
    '霾': 'Haze',
}

# ── 首页 ──────────────────────────────────────────────────

HOME = {
    '设备状态': 'Devices',
    '设备状态已更新': 'Devices updated',
    '家庭网络': 'Network',
    '实时预览': 'Live view',
    '本机摄像头': 'Local camera',
    '智能管家': 'Assistant',
    '新对话': 'New chat',
    '说说你的想法，或输入指令…': 'Ask anything, or type a command...',
    '工具调用': 'Tool call',
    '本机 · 摄像头操作': 'Local - Camera',
    '演示设备不执行真实控制': 'Demo devices do not actuate',
    '演示设备 · 不执行真实控制': 'Demo device - no real actuation',
    '本地设备 · 状态由设备服务确认': 'Local device - confirmed by service',
    '按设备规格显示，最多16项；灰色项等待有效回读':
        'Up to 16 items per spec; grey means no readback',
    '设备未提供可控属性或动作': 'No controllable attributes',
    '此设备暂无可用控制能力': 'No controls available',
    '历史操作：': 'Earlier actions:',
    '已恢复 %u 条历史记录': 'Restored %u messages',
    '历史对话读取失败（%d），原记录已保留。':
        'History read failed (%d), original kept.',
    '暂时无法处理，请稍后重试。': 'Cannot process now, try again.',
    '请等待当前回复或录音结束后再开始新对话。':
        'Wait for the current reply or recording.',
    '无法清空会话，请重试。': 'Cannot clear chat, try again.',
    '已开始新对话': 'New chat started',
    '正在处理上一条消息，请稍候。': 'Still working on the last message.',
    '设备暂时繁忙，请稍后重试。': 'Device busy, try again later.',
    '设备当前离线或控制能力已变化': 'Device offline or controls changed',
    '操作失败，设备可能已移除': 'Failed, device may be gone',
    '已提交，等待设备确认…': 'Sent, awaiting confirmation...',
    '提交失败，请稍后重试': 'Send failed, try again',
    '操作未获成功确认，请检查网关或设备状态':
        'No confirmation, check gateway or device',
    '网关已确认动作': 'Gateway confirmed',
    '网关已接受写入，此属性不支持回读':
        'Write accepted, attribute not readable',
    '设备当前状态已确认': 'Current state confirmed',
    '设备已确认本次操作': 'Device confirmed',
    '未收到设备确认，请检查连接后重试':
        'No confirmation, check connection',
    '设备已移除，请关闭后重试': 'Device removed, close and retry',
    '操作未完成': 'Not completed',
    '操作失败': 'Failed',
    '结果待确认': 'Unconfirmed',
    '提交失败': 'Send failed',
    '设备服务未确认本次操作。': 'Device service did not confirm.',
    '米家网关未接受本次操作。': 'Mi Home gateway rejected it.',
    '米家网关未连接，请先在设置中完成米家绑定。':
        'Mi Home not connected. Link it in Settings.',
    '已对在线设备提交场景动作。': 'Scene sent to online devices.',
    '网关拒绝了本次场景请求。': 'Gateway rejected the scene.',
    '当前固件未启用米家网关。': 'Mi Home gateway not built in.',
    '此操作尚未获得授权，请在设置中检查操作权限。':
        'Not permitted. Check tool permissions in Settings.',
    '该房间暂无设备': 'No devices in this room',
}

# ── 聊天 ──────────────────────────────────────────────────

CHAT = {
    '等待回复超时，请稍后重试。': 'Reply timed out, try again.',
    '暂时无法连接服务，请检查网络后重试。':
        'Cannot reach service. Check network and retry.',
    '智能管家暂时不可用，请检查模型服务设置。':
        'Assistant unavailable. Check model settings.',
    '服务认证失败，请检查密钥或访问权限。':
        'Auth failed. Check the API key.',
    '请求过于频繁，请稍后重试。': 'Rate limited, try again later.',
    '对话内容过长，请开始新对话。': 'Too long. Start a new chat.',
    '本次请求已取消。': 'Request cancelled.',
    '暂时无法完成请求，请稍后重试。': 'Cannot complete, try again.',
    '回复超出显示容量，以上内容已截断。':
        'Reply truncated to fit the display.',
    '回复已显示，历史保存失败。控制可能已执行，请先确认设备状态。':
        'Shown, but history save failed. Verify device state.',
    '暂时无法发送消息，请稍后重试。': 'Cannot send, try again.',
    '本次对话未完成，部分操作可能已提交。请先查看设备状态。':
        'Chat incomplete. Some actions may have run - check devices.',
    '内存不足，请稍后重试': 'Out of memory, try again',
    '保存失败，请重试': 'Save failed, try again',
    '演示 · 查询设备状态': 'Demo - device state',
    '演示 · 调整灯光': 'Demo - dim lights',
    '演示 · 调整空调': 'Demo - set AC',
    '演示 · 切换场景': 'Demo - switch scene',
    '米家 · 提交场景': 'Mi Home - scene',
    '米家 · 查询设备能力': 'Mi Home - capabilities',
    '米家 · 提交设备控制': 'Mi Home - control',
    'Node · 节点操作': 'Node - operation',
    'MCP · 服务调用': 'MCP - call',
    '其他服务操作': 'Other service',
    '查看设备来源': 'List device sources',
    '查看室内环境': 'Indoor environment',
    '查询天气': 'Weather',
    '设置提醒': 'Set reminder',
    '设置闹钟': 'Set alarm',
    '查看提醒': 'List reminders',
    '取消提醒': 'Cancel reminder',
    '查阅操作说明': 'Read the guide',
    '管理用户记忆': 'Manage memory',
}

# ── 语音 ──────────────────────────────────────────────────

VOICE = {
    '语音服务': 'Voice',
    '语音唤醒 KWS': 'Wake word',
    '语音唤醒已关闭': 'Wake word off',
    '语音唤醒已开启': 'Wake word on',
    '语音输入 ASR': 'Speech input (ASR)',
    '语音播报 TTS': 'Speech output (TTS)',
    '语音输入未开启': 'Speech input is off',
    '语音播报未开启': 'Speech output is off',
    '语音服务未开启': 'Voice is off',
    '语音设置任务进行中，请稍后重试': 'Voice task running, retry later',
    '语音输入已在设置中关闭。': 'Speech input is off in Settings.',
    '语音输入启动失败（内存不足）': 'Speech input failed (out of memory)',
    '正在录音，再点一次结束…': 'Recording, tap again to stop...',
    '再点一次结束录音并发送': 'Tap again to stop and send',
    '录满上限': 'Recording limit reached',
    '（已唤醒，请说话）': '(awake, speak now)',
    '你好，这是语音播报测试': 'Hello, this is a speech test',
    '没触发': 'Not triggered',
    '触发了但无声': 'Triggered, no sound',
    '没有反应': 'No response',
    '服务商': 'Provider',
    '启用服务': 'Enable',
    '识别语言': 'Language',
    '中文\\nEnglish\\n自动': 'Chinese\\nEnglish\\nAuto',
    '音色': 'Voice',
    '连接设置': 'Connection',
    '停止播报': 'Stop playback',
    '保存配置': 'Save',
    '清除密钥': 'Clear key',
    '测试已保存配置': 'Test saved config',
    '服务域名': 'Host',
    'API 路径': 'API path',
    '连接参数随「保存配置」一并生效。\\n返回后可继续填写密钥、选择模型与测试。':
        'Connection settings apply with Save.\\nGo back to set the key, model, and test.',
    '返回服务配置': 'Back to service',
    '当前固件未启用此服务，可切换到另一项语音服务进行配置。':
        'Not built in. Try the other voice service.',
    '无法创建保存任务': 'Cannot start save task',
    '正在保存唤醒设置…': 'Saving wake settings...',
    '配置已保存，唤醒服务将在重启后启动':
        'Saved. Wake service starts after reboot.',
    '唤醒配置保存失败，已恢复原设置':
        'Save failed, reverted to previous settings',
    '启用后监听唤醒词。启动时关闭的服务，需要重启后启用。':
        'Listens for the wake word. If off at boot, needs a restart.',
    '唤醒服务已启动': 'Wake service running',
    '唤醒服务未启动': 'Wake service stopped',
    '配置已保存并生效': 'Saved and applied',
    '请等待当前请求结束后重试': 'Wait for the current request',
    '请先停止当前语音播报': 'Stop playback first',
    '内存不足，无法创建语音配置任务': 'Out of memory for voice task',
    '无法创建语音配置任务': 'Cannot create voice task',
    '正在测试；ASR 请连续说话约四秒': 'Testing; speak for about 4s',
    '请填写要保存的 LLM 密钥': 'Enter the LLM API key',
    '内存不足': 'Out of memory',
    '已请求停止播报': 'Stopping playback',
    '手动输入；留空保留当前服务密钥':
        'Type a key; leave blank to keep the current one',
    '密钥已保存；后续操作失败：': 'Key saved; then failed: ',
    '测试完成%s%.200s': 'Test done%s%.200s',
    'LLM 密钥已保存；请重新应用上方模型配置':
        'LLM key saved. Re-apply the model config above.',
    '本服务密钥已清除': 'Service key cleared',
}

# ── 摄像头 ────────────────────────────────────────────────

CAMERA = {
    '摄像头状态读取失败': 'Cannot read camera state',
    '未获取画面，已请求停止；请重新开启':
        'No frame received, stopped. Try again.',
    '预览内存不足，已请求停止；请重新开启':
        'Out of memory for preview, stopped. Try again.',
    '预览内存不足，已请求停止': 'Out of memory for preview, stopped',
    '开启预览后显示实时画面': 'Enable preview to see live video',
    '摄像头操作失败 (%d)': 'Camera error (%d)',
    '无法提交摄像头操作，请重试': 'Cannot send camera command',
    '无法提交摄像头操作，请重试。': 'Cannot send camera command.',
    '预览帧率 %u.%02u FPS\\n采集帧率 %u.%02u FPS':
        'Preview %u.%02u FPS\\nCapture %u.%02u FPS',
}

# ── 米家绑定 ──────────────────────────────────────────────

MILOCO = {
    '米家网关': 'Mi Home',
    '连接米家网关': 'Connect Mi Home',
    '绑定米家账号': 'Link Mi Home account',
    '绑定米家账号，接入家中真实设备': 'Link Mi Home for real devices',
    '米家二维码': 'Mi Home QR code',
    '等待手机完成授权': 'Waiting for phone approval',
    '等待手机完成授权…  已等待 %02u:%02u  ·  每秒自动检查':
        'Waiting for approval... %02u:%02u elapsed',
    '等待手机完成授权…  已等待 00:00': 'Waiting for approval...',
    '仍在等待手机完成授权…': 'Still waiting for approval...',
    '请在手机上完成账号授权，再检查绑定状态。':
        'Approve on your phone, then check the status.',
    '1. 手机扫描二维码进入家庭服务器':
        '1. Scan the QR code on your phone',
    '2. 在手机页面完成账号授权': '2. Approve the account on the page',
    '3. 授权成功后，此处同步设备': '3. Devices sync here automatically',
    '检查绑定状态': 'Check status',
    '返回配置': 'Back',
    '直接扫码绑定': 'Scan to link',
    '保存并扫码授权': 'Save and scan',
    '账号已绑定': 'Account linked',
    '网关已连接': 'Gateway connected',
    '✓ 已绑定 · %u 台设备': 'Linked - %u devices',
    '✓ 已绑定 · %u 台设备，正在进入…': 'Linked - %u devices, opening...',
    '二维码生成失败': 'QR code generation failed',
    '请先在米家网关设置中\\n配置服务器地址':
        'Set the server address in\\nMi Home settings first',
    '家庭服务器地址': 'Server address',
    '访问令牌（服务器未启用鉴权可留空）':
        'Access token (blank if auth is off)',
    '仅本次运行有效，重启后不保留': 'This session only',
    '保存完成后重启保留': 'Survives reboot after saving',
    '配置是否重启保留，以连接弹窗中的说明为准。':
        'See the connect dialog for persistence.',
    '端口须为 1～65535 的整数': 'Port must be 1 to 65535',
    '请填写有效的服务器地址': 'Enter a valid server address',
    '保存已提交，请稍后重新打开查看': 'Saved. Reopen to verify.',
    '连接家庭服务器，再扫码授权账号':
        'Connect the server, then scan to link',
    '连接 / 状态': 'Connect / status',
    '连接失败': 'Connect failed',
    '固件未启用': 'Not built in',
    '网关离线，请检查 Miloco 服务器': 'Gateway offline - check server',
    '端口': 'Port',
}

# ── 设置：网络 ────────────────────────────────────────────

NETWORK = {
    '网络连接': 'Network',
    '无线网络': 'Wi-Fi',
    '家庭 Wi-Fi': 'Home Wi-Fi',
    'Wi-Fi 名称（SSID）': 'SSID',
    '密码': 'Password',
    '开放网络可留空': 'Leave blank if open',
    '连接': 'Connect',
    '切换后端会保留当前草稿': 'Switching keeps this draft',
    '已连接互联网': 'Internet OK',
    '未连接互联网': 'No internet',
    '已连接 Wi-Fi，互联网/DNS 暂不可用':
        'Wi-Fi connected, internet unavailable',
    '当前连接：%s': 'Connected: %s',
    '当前连接：网络名称暂不可用': 'Connected: SSID unavailable',
    '当前连接：未连接': 'Connected: none',
    '当前未连接 Wi-Fi': 'Wi-Fi not connected',
    '连接失败，请检查密码或路由器':
        'Connect failed - check password or router',
    '配置保存或应用失败': 'Save or apply failed',
    '配置已保存并应用': 'Saved and applied',
    '配置已应用（仅本次有效）': 'Applied for this session',
    'SSID 或密码长度不合法': 'Invalid SSID or password length',
    '内存不足，无法开始连接': 'Out of memory for connect',
    '无法创建连接任务': 'Cannot start connect task',
    '连接 2.4 GHz Wi-Fi，天气与在线服务依赖它':
        'Connect 2.4 GHz Wi-Fi for weather and online services',
}

# ── 设置：模型 ────────────────────────────────────────────

MODEL = {
    '模型服务': 'Model service',
    '模型标识': 'Model',
    '模型名称': 'Model name',
    'API 密钥': 'API key',
    '手动输入密钥': 'Enter key',
    '留空沿用已保存密钥': 'Blank keeps the saved key',
    '请求超时 / 毫秒': 'Timeout / ms',
    '最大输出 / 词元': 'Max output / tokens',
    '保存并应用': 'Save and apply',
    '保存 LLM 密钥': 'Save LLM key',
    '配置大模型接口，聊天与语音对话依赖它':
        'Configure the model endpoint for chat and voice',
    '请等待密钥或语音配置任务结束后重试':
        'Wait for the current key or voice task',
    '内存不足，无法应用配置': 'Out of memory to apply',
    '无法创建配置任务': 'Cannot start config task',
    '正在应用，等待当前请求完成…': 'Applying, waiting for request...',
}

# ── 设置：工具权限 ────────────────────────────────────────

TOOLS = {
    '工具权限': 'Tool permissions',
    '接入策略管理': 'Access policy',
    '已允许': 'Allowed',
    '已拒绝': 'Denied',
    '%s · %s · 查看详情': '%s - %s - details',
    '权限已更新 · 本次运行有效': 'Updated for this session',
    '工具正在使用中，请稍后重试': 'Tool in use, retry later',
    '权限更新失败': 'Update failed',
    '工具详情': 'Tool details',
    '关闭后，本次运行将无法调用此工具。':
        'When off, this tool cannot run this session.',
    '此工具的权限由接入服务管理。': 'Managed by the service.',
    '暂无可用工具 · 可稍后刷新': 'No tools - refresh later',
    '%u 项工具 · 权限调整本次运行有效':
        '%u tools - changes apply this session',
    '工具正在使用中，请稍后刷新': 'Tools in use, refresh later',
    '工具目录读取失败': 'Cannot read tool list',
    '按来源管理工具 · 点击卡片查看说明':
        'Grouped by source - tap a card for details',
    '刷新目录': 'Refresh',
    '点击卡片查看说明': 'Tap a card for details',
    '本地工具': 'Local tools',
    '最近错误：': 'Last error: ',
}

# ── 设置：系统健康 ────────────────────────────────────────

HEALTH = {
    '设备与服务': 'Devices & services',
    '系统健康': 'System health',
    '核心服务正常': 'Core services OK',
    '网络与核心服务正常，未启用的可选服务单独标记。':
        'Network and core services OK; disabled extras are marked.',
    '网络或核心服务异常；未启用的可选服务单独标记。':
        'Network or core issue; disabled extras are marked.',
    '部分服务状态未知，点击刷新状态获取最新信息。':
        'Some states unknown - tap refresh.',
    '刷新状态': 'Refresh',
    '网络任务正在进行，完成后可再次刷新。':
        'Network task running - refresh after it finishes.',
    '无法开始检查': 'Cannot start the check',
    '无法创建网络检查任务，请稍后重试。':
        'Cannot start network check, retry later.',
    '正在检测网络，其余服务显示当前状态。':
        'Checking network; others show current state.',
    '已就绪': 'Ready',
    '有服务需要处理': 'Needs attention',
    '服务详情': 'Service details',
    '最后检查 %s · 固件 %s': 'Checked %s - fw %s',
    '已连接 · %u 台设备': 'Connected - %u devices',
    '已连接 · 等待授权': 'Connected - awaiting approval',
    '配置已应用': 'Applied',
    '配置未应用': 'Not applied',
    '暂不可用': 'Unavailable',
}

# ── 设置：显示 ────────────────────────────────────────────

DISPLAY = {
    '显示与声音': 'Display & sound',
    '1分钟': '1 min',
    '5分钟': '5 min',
    '永不': 'Never',
    '屏幕亮度': 'Brightness',
    '播报音量': 'Volume',
    '自动进入屏保': 'Auto screensaver',
    '自动屏保暂不可用': 'Auto screensaver unavailable',
    '设备状态动效': 'Device animations',
    '当前显示静态设备图标': 'Static device icons',
    '不可用': 'Unavailable',
}

# ── 引导页 ────────────────────────────────────────────────

ONBOARDING = {
    '这三项让设备完整工作。可以现在配置，也可以稍后在设置中完成。':
        'These three complete the setup. You can also do it later.',
    '稍后可在「设置」中完成，首页会显示待配置提示':
        'Finish later in Settings; Home will show a reminder',
    '正在保存确认…': 'Saving...',
    '确认失败，请重试': 'Confirm failed, try again',
    '未能保存确认，请重试并检查存储日志':
        'Cannot save. Retry and check storage logs.',
}

# ── 提醒 ──────────────────────────────────────────────────

REMINDER = {
    '提醒已到期': 'Reminder due',
    '本地响铃失败，请检查音频设备日志':
        'Local chime failed - check audio logs',
    '已错过的提醒（静音）': 'Missed reminders (silent)',
    '重启恢复的提醒（静音）': 'Restored reminders (silent)',
    '无法启动本地响铃，请检查音频服务':
        'Cannot start chime - check audio service',
    '本地提示音 5 秒，点击确认关闭提醒':
        'Chime plays for 5s; tap to dismiss',
    '本次已生效，保存失败；可返回管理页重新保存':
        'Applied but not saved - save again from the list',
    '本次已生效，无法保存；请返回管理页重新保存':
        'Applied but not saved - save again from the list',
    '数量已达上限': 'Limit reached',
    '操作未完成：名称重复、房间不存在或参数无效':
        'Not saved: duplicate name, missing room, or bad value',
    '内存不足，尚未删除房间': 'Out of memory, room not deleted',
    '至少保留一个房间，无法删除最后一个房间':
        'Keep at least one room',
    '新增房间 · 最多 6 个，名称最多 7 个汉字或 23 字节':
        'Add room - up to 6, name up to 23 bytes',
    '没有可用房间': 'No rooms available',
    '此前提保存失败，可在管理页重新保存':
        'Earlier save failed - save again from the list',
    '此前保存失败，可在管理页重新保存':
        'Earlier save failed - save again from the list',
    '仅调整本屏房间，不修改米家云端房间。':
        'Only affects this screen, not Mi Home rooms.',
    '本地设备和已发现的米家设备将一同迁移；米家云端房间不变。':
        'Local and discovered devices move together.',
    '将设备分配到以下房间：': 'Assign devices to a room:',
    '删除“%s”前，请选择设备迁往的房间。':
        'Before removing "%s", pick a room for its devices.',
    '%s  ·  %d 台已分配设备': '%s - %d devices',
}

# ── 选择项列表（dropdown 文本）──────────────────────────────

LISTS = {
    '书房\\n阳台\\n厨房\\n餐厅\\n儿童房\\n客房\\n客厅\\n卧室':
        'Study\\nBalcony\\nKitchen\\nDining\\nKids\\nGuest\\nLiving\\nBedroom',
    '制冷\\n制热\\n除湿\\n送风\\n自动': 'Cool\\nHeat\\nDry\\nFan\\nAuto',
}

# ── 星期与日期 ────────────────────────────────────────────

DATE = {
    '日': 'Sun',
    '一': 'Mon',
    '二': 'Tue',
    '三': 'Wed',
    '四': 'Thu',
    '五': 'Fri',
    '六': 'Sat',
    '%d月%d日 星期%s': '%s %d/%d',
}


# ── 补充项（首轮 check 的缺口）──────────────────────────

EXTRA = {
    '分配房间': 'Assign room',
    '预览已关闭': 'Preview off',
    '尚未连接': 'Not connected',
    '正在保存房间配置…': 'Saving room config...',
    '房间配置已保存': 'Room config saved',
    '当前固件未启用此服务，暂时无法连接。':
        'This service is not built in.',
    '启用 CONFIG_SMART_HOME_MILOCO_BRIDGE 并重新构建固件后，可在此配置连接。':
        'Enable SCA_MILOCO and rebuild to configure it here.',
    'Wi-Fi 已连接，互联网暂不可用': 'Wi-Fi OK, no internet',
    '等待检查': 'Pending',
    '当前固件未启用语音输入或语音播报。\\n启用语音服务后，即可在这里配置 ASR 和 TTS。':
        'Voice input and output are not built in.\\nEnable voice to configure ASR and TTS.',
    '中文\\nEnglish\\n自动': 'Chinese\\nEnglish\\nAuto',
    '连接参数随「保存配置」一并生效。\\n返回后可继续填写密钥、选择模型与测试。':
        'Connection settings apply on Save.\\nGo back to set the key, model, and test.',
}

# 星期与日期由 shell.c 直接按英文习惯格式化（"Mon, Nov 11"），
# 不走字面量替换，因此这里不需要映射。

# ── 汇总 ──────────────────────────────────────────────────

ALL = {}
for table in (NAV, COMMON, ROOMS, DEVICES, SCENES, WEATHER, HOME, CHAT, VOICE,
              CAMERA, MILOCO, NETWORK, MODEL, TOOLS, HEALTH, DISPLAY,
              ONBOARDING, REMINDER, LISTS, EXTRA):
    ALL.update(table)


def lookup(text):
    """返回英文替换；没有映射时返回 None。"""
    return ALL.get(text)
