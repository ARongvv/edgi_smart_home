/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * HTTP / HTTPS 客户端抽象。
 *
 * ★ 这是 HTTP / HTTPS 请求的唯一入口 ★
 *
 * cAGENT 的 transport 端口不直接依赖任何平台 HTTP 库。本文件定义
 * 一套足够小的接口，让 cAGENT 的 rtthread transport 端口基于它实现。
 *
 * 接口刻意做成"一次性、阻塞、整响应返回"：
 *   - 大模型的流式响应不在这里处理，由调用方用 sink 分段收；
 *   - 不做连接复用（板端一次对话一次连接，简化状态机）；
 *   - 缓冲全部调用方提供，本层不 malloc。
 */

#ifndef __SCA_HTTP_H__
#define __SCA_HTTP_H__

#include "port/sca_os.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCA_HTTP_MAX_HEADERS    16
#define SCA_HTTP_MAX_URL        256

typedef enum
{
    SCA_HTTP_GET = 0,
    SCA_HTTP_POST,
    SCA_HTTP_PUT,
} sca_http_method_t;

typedef struct
{
    const char *name;
    const char *value;
} sca_http_header_t;

/* 响应体分段投递。返回非 0 中止接收。 */
typedef int (*sca_http_sink_fn)(const void *data, size_t len, void *user);
/* 响应头解析完成、开始投递响应体前通知。返回非 0 中止接收。 */
typedef int (*sca_http_headers_ready_fn)(int status, void *user);

typedef struct
{
    sca_http_method_t method;
    const char *url;                 /* 必须是 https:// 或 http:// 全 URL */
    const sca_http_header_t *headers;
    size_t header_count;
    const void *body;
    size_t body_len;
    /** 总超时（毫秒）。0 使用默认 60s。 */
    uint32_t timeout_ms;
    /** 响应体回调；NULL 表示丢弃响应体（只关心状态码）。 */
    sca_http_sink_fn sink;
    void *sink_user;
    /** 调用方提供的响应头缓冲，用于返回 Content-Type 等。 */
    char *response_header_buf;
    size_t response_header_size;
    /** 响应头就绪回调；user 复用 sink_user，NULL 表示不通知。 */
    sca_http_headers_ready_fn headers_ready;
    /** 解码后响应体上限；0 使用默认 1 MiB。 */
    size_t max_body_bytes;
} sca_http_request_t;

typedef struct
{
    int  status;                     /* HTTP 状态码，0 表示连接层失败 */
    long content_length;             /* -1 表示未提供 */
    size_t body_bytes;               /* 实际收到的体长度 */
    char content_type[64];
} sca_http_response_t;

/**
 * 执行一次同步 HTTP 请求。
 *
 * @return SCA_OK 请求完成（不代表 2xx，看 response->status）
 *         SCA_ERR_TIMEOUT / SCA_ERR_IO / SCA_ERR_NOMEM / SCA_ERR_PARAM
 */
int sca_http_request(const sca_http_request_t *req, sca_http_response_t *rsp);

/** 全局初始化（TLS 上下文、随机数种子等）。可重复调用。 */
int sca_http_init(void);

/** NTP/RTC 校时成功后调用；未校时时 HTTPS 请求会被拒绝。 */
void sca_http_notify_time_sync(void);
/** HTTPS 证书时间校验是否已具备可信的 RTC 时间。 */
int sca_http_time_ready(void);

/** URL 解析辅助：取出 host / port / path，供 TLS 层使用。 */
typedef struct
{
    char host[128];
    uint16_t port;
    char path[SCA_HTTP_MAX_URL];
    int  tls;                        /* 1 = https */
} sca_url_t;

int sca_url_parse(const char *url, sca_url_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __SCA_HTTP_H__ */
