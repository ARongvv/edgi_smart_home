/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: MIT
 *
 * cAGENT HTTP transport 端口（RT-Thread / sca_http 后端）。
 *
 * 为什么需要重写而不是移植：
 *   上游 ports/transport 直接 #include <netutils/webclient.h>，那是
 *   NuttX 的 netutils 包。RT-Thread 既没有这个包，其 webclient 软件包
 *   的回调模型也不同（以静态缓冲为前提，不支持流式投递）。
 *
 *   本端口把请求全部委托给 port/sca_http.h，只负责两件事：
 *     1. 把 cAGENT 的 agent_http_request_t 翻译成 sca_http_request_t；
 *     2. 把 sca_http 的 sink 回调桥接到 cAGENT 的 agent_http_sink_t。
 *
 * 与上游的接口契约完全一致（同一个 agent_transport_t 抽象），
 * 因此 cAGENT 内核与调用方（smart_home/agent）不需要任何改动。
 */

#pragma once

#include <agent/runtime.h>
#include <agent/transport.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    agent_runtime_t runtime;              /* 需要 now_ms */
    uint32_t        request_timeout_ms;   /* 单次请求总超时（0=默认 60s） */
    char           *response_header_buffer; /* 调用方提供的响应头缓冲，可为 NULL */
    size_t          response_header_buffer_size;
    char           *url_buffer;           /* 复用缓冲：拼绝对 URL 用 */
    size_t          url_buffer_size;
} agent_port_rtthread_transport_config_t;

typedef struct
{
    agent_port_rtthread_transport_config_t config;
    int active;                           /* 同时只允许一个请求 */
} agent_port_rtthread_transport_t;

/**
 * 初始化 transport 端口。
 *
 * @param out    输出的 agent_transport_t（内容指向 state）
 * @param state  调用方持有的端口状态，生命周期需覆盖 transport
 * @param config 配置
 * @return AGENT_OK 成功
 */
agent_error_t agent_port_rtthread_transport_init(
    agent_transport_t *out,
    agent_port_rtthread_transport_t *state,
    const agent_port_rtthread_transport_config_t *config);

#ifdef __cplusplus
}
#endif
