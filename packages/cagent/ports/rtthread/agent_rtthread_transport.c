/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: MIT
 *
 * cAGENT transport 端口实现（面向 port/sca_http）。
 *
 * 三层结构：
 *   cAGENT 内核  ──agent_http_request_t──▶  本文件  ──sca_http_request_t──▶  sca_http
 *                ◀──agent_http_sink_t───           ◀──sink 回调───
 *
 * 关键映射：
 *   - sca_http 在响应头解析完成后先触发 headers_ready，本端口在这里
 *     调用 cAGENT 的 sink->headers。
 *   - 随后的响应体仍通过 sink 实时分段投递，保证 headers 严格先于 body，
 *     同时不需要额外缓冲整份模型响应。
 */

#include <string.h>

#include "port/sca_http.h"
#include "agent_rtthread_transport.h"

/* ── 单次交换的上下文 ──────────────────────────────────── */

typedef struct
{
    agent_port_rtthread_transport_t *state;
    const agent_http_request_t      *request;
    const agent_http_sink_t         *sink;

    /* 消费者拒绝 headers/body 时保存原始错误并中止接收。 */
    agent_error_t status;
    int           sink_aborted;
    size_t        body_bytes;

    /* 响应头文本，供 headers 回调构造 agent_http_header_t，最多 512 字节。 */
    char   header_text[512];
    size_t header_text_used;
} exchange_t;

/* 把 sca_http 的 body 回调桥接到 cAGENT 的 sink->body */
static int transport_body_sink(const void *data, size_t len, void *user)
{
    exchange_t *ex = (exchange_t *)user;
    agent_error_t ret;

    if (ex->sink_aborted)
    {
        return 1;
    }
    if (ex->sink == NULL || ex->sink->body == NULL)
    {
        /* 没有消费者也要收完（保持内容长度语义），返回 0 不中止 */
        ex->body_bytes += len;
        return 0;
    }

    ret = ex->sink->body(ex->sink->context, data, len);
    if (ret != AGENT_OK)
    {
        ex->sink_aborted = 1;
        ex->status = ret;
        return 1;              /* 中止接收 */
    }

    ex->body_bytes += len;
    return 0;
}

/* 解析 "Name: Value" 行，填 agent_http_header_t 数组 */
static size_t parse_header_lines(char *text, agent_http_header_t *out,
                                 size_t capacity)
{
    size_t count = 0;
    char *line = text;

    if (text == NULL || out == NULL || capacity == 0)
    {
        return 0;
    }

    while (line != NULL && *line != '\0' && count < capacity)
    {
        char *next = strchr(line, '\n');
        char *colon;

        if (next != NULL)
        {
            *next = '\0';
            next++;
        }

        if (*line == '\0')
        {
            line = next;
            continue;
        }

        colon = strchr(line, ':');
        if (colon == NULL)
        {
            line = next;
            continue;
        }

        *colon = '\0';
        colon++;

        /* 去掉值前导空格 */
        while (*colon == ' ')
        {
            colon++;
        }

        out[count].name.data  = line;
        out[count].name.size  = strlen(line);
        out[count].value.data = colon;
        out[count].value.size = strlen(colon);
        count++;

        line = next;
    }

    return count;
}

/* sca_http 保证此回调先于任何 body 回调执行。 */
static int transport_headers_ready(int status, void *user)
{
    exchange_t *ex = (exchange_t *)user;
    agent_http_header_t parsed[16];
    size_t parsed_count = 0;

    if (ex->sink_aborted)
    {
        return 1;
    }

    if (ex->state->config.response_header_buffer != NULL &&
        ex->state->config.response_header_buffer_size > 0)
    {
        size_t copy = ex->state->config.response_header_buffer_size;

        if (copy >= sizeof(ex->header_text))
        {
            copy = sizeof(ex->header_text) - 1;
        }
        memcpy(ex->header_text, ex->state->config.response_header_buffer, copy);
        ex->header_text[copy] = '\0';
        parsed_count = parse_header_lines(ex->header_text, parsed, 16);
    }

    if (ex->sink->headers != NULL)
    {
        agent_error_t ret = ex->sink->headers(ex->sink->context,
                                              (unsigned int)status,
                                              parsed, parsed_count);
        if (ret != AGENT_OK)
        {
            ex->status = ret;
            ex->sink_aborted = 1;
            return 1;
        }
    }

    return 0;
}

/* ── transport 的 request 实现 ─────────────────────────── */

static agent_error_t rtthread_transport_request(
    void *context,
    const agent_http_request_t *request,
    const agent_http_sink_t *sink)
{
    agent_port_rtthread_transport_t *state =
        (agent_port_rtthread_transport_t *)context;
    sca_http_request_t req;
    sca_http_response_t rsp;
    sca_http_header_t headers[16];
    exchange_t ex;
    int ret;
    size_t i;

    if (state == NULL || request == NULL || sink == NULL)
    {
        return AGENT_ERROR_INVALID;
    }
    if (state->active)
    {
        return AGENT_ERROR_BUSY;
    }
    if (request->method.size == 0 || request->url.size == 0)
    {
        return AGENT_ERROR_INVALID;
    }

    /* URL 必须是以 http/https 开头的绝对地址，且调用方保证 NUL 结尾
     * （cAGENT 的 string_view 不保证；这里拷到复用缓冲） */
    if (request->url.size >= state->config.url_buffer_size)
    {
        return AGENT_ERROR_CAPACITY;
    }
    memcpy(state->config.url_buffer, request->url.data, request->url.size);
    state->config.url_buffer[request->url.size] = '\0';

    memset(&req, 0, sizeof(req));
    memset(&rsp, 0, sizeof(rsp));
    memset(&ex, 0, sizeof(ex));

    req.url        = state->config.url_buffer;
    req.body       = request->body;
    req.body_len   = request->body_size;
    req.timeout_ms = state->config.request_timeout_ms;
    req.sink       = transport_body_sink;
    req.sink_user  = &ex;
    req.headers_ready = transport_headers_ready;
    req.response_header_buf  = state->config.response_header_buffer;
    req.response_header_size = state->config.response_header_buffer_size;
    if (req.response_header_buf != NULL && req.response_header_size > 0)
    {
        req.response_header_buf[0] = '\0';
    }

    /* 方法映射 */
    if (request->method.size == 3 &&
        memcmp(request->method.data, "GET", 3) == 0)
    {
        req.method = SCA_HTTP_GET;
    }
    else if (request->method.size == 4 &&
             memcmp(request->method.data, "POST", 4) == 0)
    {
        req.method = SCA_HTTP_POST;
    }
    else if (request->method.size == 3 &&
             memcmp(request->method.data, "PUT", 3) == 0)
    {
        req.method = SCA_HTTP_PUT;
    }
    else
    {
        return AGENT_ERROR_NOT_SUPPORTED;
    }

    /* 请求头：cAGENT 用 string_view，sca_http 要 NUL 结尾，必须先复制。
     * 缓冲在栈上：每项名字+值各 96 字节，16 项约 3KB。
     * 原来这里错写成 static，会在多线程下串数据；改成局部数组。
     */
    if (request->header_count > SCA_HTTP_MAX_HEADERS)
    {
        return AGENT_ERROR_CAPACITY;
    }

    {
        char name_buf[SCA_HTTP_MAX_HEADERS][64];
        char value_buf[SCA_HTTP_MAX_HEADERS][192];

        for (i = 0; i < request->header_count; i++)
        {
            size_t name_len  = request->headers[i].name.size;
            size_t value_len = request->headers[i].value.size;

            if (name_len + 1u >= sizeof(name_buf[0]) ||
                value_len + 1u >= sizeof(value_buf[0]))
            {
                return AGENT_ERROR_CAPACITY;
            }

            memcpy(name_buf[i], request->headers[i].name.data, name_len);
            name_buf[i][name_len] = '\0';
            memcpy(value_buf[i], request->headers[i].value.data, value_len);
            value_buf[i][value_len] = '\0';

            headers[i].name  = name_buf[i];
            headers[i].value = value_buf[i];
        }

        req.headers      = headers;
        req.header_count = request->header_count;

        /* headers 指向的栈缓冲必须活到 sca_http_request 返回，
         * 所以整段调用要在这个作用域内完成。 */
        ex.state   = state;
        ex.request = request;
        ex.sink    = sink;

        state->active = 1;
        ret = sca_http_request(&req, &rsp);
        state->active = 0;
    }

    /* 消费者错误优先于传输层的中止错误，保留 AUTH/CAPACITY 等语义。 */
    if (ex.sink_aborted)
    {
        return ex.status;
    }

    if (ret != SCA_OK)
    {
        switch (ret)
        {
        case SCA_ERR_TIMEOUT:   return AGENT_ERROR_TIMEOUT;
        case SCA_ERR_NOMEM:     return AGENT_ERROR_NOMEM;
        case SCA_ERR_PARAM:     return AGENT_ERROR_INVALID;
        case SCA_ERR_CANCELLED: return AGENT_ERROR_CANCELLED;
        default:                return AGENT_ERROR_IO;
        }
    }

    /* cAGENT 用 max_response_bytes 做容量上限；我们在 sca_http 层已经
     * 按 Content-Length 收完，这里补一次校验，越界按 CAPACITY 上报。 */
    if (request->max_response_bytes != 0 &&
        rsp.body_bytes > request->max_response_bytes)
    {
        return AGENT_ERROR_CAPACITY;
    }

    return AGENT_OK;
}

static const agent_transport_ops_t g_transport_ops =
{
    .request = rtthread_transport_request,
};

agent_error_t agent_port_rtthread_transport_init(
    agent_transport_t *out,
    agent_port_rtthread_transport_t *state,
    const agent_port_rtthread_transport_config_t *config)
{
    if (out == NULL || state == NULL || config == NULL)
    {
        return AGENT_ERROR_INVALID;
    }
    if (config->runtime.now_ms == NULL)
    {
        return AGENT_ERROR_INVALID;
    }

    memset(state, 0, sizeof(*state));
    state->config = *config;

    if (state->config.request_timeout_ms == 0)
    {
        state->config.request_timeout_ms = 60000u;
    }

    out->ops     = &g_transport_ops;
    out->context = state;
    return AGENT_OK;
}
