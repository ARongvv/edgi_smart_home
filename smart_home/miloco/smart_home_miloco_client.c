/* SPDX-License-Identifier: Apache-2.0 */

#include "smart_home/miloco/smart_home_miloco_client.h"

#include "port/sca_http.h"
#include "port/sca_os.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MILOCO_HTTP_CONNECT_TIMEOUT_MS 3000
#define MILOCO_HTTP_IO_TIMEOUT_MS      5000

/* 解析 RFC 7231 日期 "Thu, 20 Sep 2026 19:30:00 GMT" 为 epoch 秒。
 * 失败时 *out 保持调用方预置值。纯计算无时区依赖（GMT 即 UTC）。 */
static void parse_http_date(const char *text, time_t *out)
{
    static const char months[12][4] =
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    char mon[8] = "";
    int mon_index = -1;
    int i;

    if (!text || !out) {
        return;
    }

    /* 不用 %*[^,] 扫描集：默认 libc 可能未开启 CONFIG_LIBC_SCANSET，
     * sscanf 不识别 '[' 转换符导致解析永远失败（曾致 date=-1）。
     * 用 strchr 跳过星期缩写，从逗号后开始解析。 */
    {
        const char *p = strchr(text, ',');

        if (!p) {
            return;
        }

        if (sscanf(p + 1, " %d %7s %d %d:%d:%d",
                   &day, mon, &year, &hour, &minute, &second) < 6) {
            return;
        }
    }

    for (i = 0; i < 12; i++) {
        if (strcmp(mon, months[i]) == 0) {
            mon_index = i;
            break;
        }
    }
    if (mon_index < 0 || day < 1 || day > 31 || year < 2020 ||
        year > 2100 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 59) {
        return;
    }

    /* days_from_civil（Howard Hinnant 算法）：1970-01-01 = 0。 */
    {
        long y = year;
        long m = mon_index + 1;
        long era;
        long yoe;
        long doy;
        long doe;
        long days;

        y -= (m <= 2) ? 1 : 0;
        era = (y >= 0 ? y : y - 399) / 400;
        yoe = y - era * 400;
        doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
        doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        days = era * 146097 + doe - 719468;

        *out = (time_t)(days * 86400L + hour * 3600L + minute * 60L +
                        second);
    }
}

/* sca_http 的响应体 sink：把分段数据追加到调用方缓冲 */
typedef struct {
    char  *response;
    size_t capacity;
    size_t used;
    int    overflow;
} body_sink_t;

static int body_sink(const void *data, size_t len, void *user)
{
    body_sink_t *sink = (body_sink_t *)user;

    if (sink->used + len + 1u > sink->capacity) {
        sink->overflow = 1;
        return 1;                  /* 中止接收 */
    }

    memcpy(sink->response + sink->used, data, len);
    sink->used += len;
    sink->response[sink->used] = '\0';
    return 0;
}

/* ── 请求执行 ─────────────────────────────────────────────
 *
 * 手写 socket + poll + RFC 头解析约 240 行，现全部
 * 委托给 port/sca_http：
 *   - 非阻塞 connect + 超时      → sca_http 内部处理
 *   - HTTP/1.0 + Connection:close → 保留（服务端行为不变）
 *   - 响应头逐字节状态机          → sca_http 的 read_line
 *   - Date 头解析                 → 仍在这里做（业务需求：对时）
 *
 * 关键约束不变：请求行仍是 HTTP/1.0，服务器不会返回 chunked，
 * 因此 Content-Length 或连接关闭就是响应边界。
 */

static int http_request(const smart_home_miloco_client_config_t *config,
                        const char *method,
                        const char *path,
                        const char *body,
                        char *response,
                        size_t response_size,
                        int *http_status_out,
                        time_t *date_out)
{
    sca_http_request_t req;
    sca_http_response_t rsp;
    sca_http_header_t headers[4];
    body_sink_t sink;
    char header_buf[256];
    char url[SCA_HTTP_MAX_URL];
    char authorization[80];
    int ret;
    int n;

    if (!config || !path || !response || response_size == 0) {
        return -EINVAL;
    }
    response[0] = '\0';
    if (config->host[0] == '\0' || config->port == 0) {
        return -EINVAL;
    }
    if (http_status_out) {
        *http_status_out = 0;
    }

    n = snprintf(url, sizeof(url), "http://%s:%u%s",
                 config->host, (unsigned)config->port, path);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        return -ENAMETOOLONG;
    }

    n = snprintf(authorization, sizeof(authorization), "Bearer %s",
                 config->token[0] ? config->token : "-");
    if (n < 0 || (size_t)n >= sizeof(authorization)) {
        return -ENAMETOOLONG;
    }

    memset(&req, 0, sizeof(req));
    memset(&rsp, 0, sizeof(rsp));
    memset(&sink, 0, sizeof(sink));

    req.method     = (method && method[0] == 'P') ? SCA_HTTP_POST
                                                  : SCA_HTTP_GET;
    req.url        = url;
    req.body       = body;
    req.body_len   = body ? strlen(body) : 0u;
    req.timeout_ms = MILOCO_HTTP_CONNECT_TIMEOUT_MS + MILOCO_HTTP_IO_TIMEOUT_MS;
    req.sink       = body_sink;
    req.sink_user  = &sink;
    req.response_header_buf  = header_buf;
    req.response_header_size = sizeof(header_buf);
    header_buf[0] = '\0';

    headers[0].name  = "Authorization";
    headers[0].value = authorization;
    req.headers      = headers;
    req.header_count = 1;

    if (body) {
        headers[1].name  = "Content-Type";
        headers[1].value = "application/json";
        req.header_count = 2;
    }

    sink.response = response;
    sink.capacity = response_size;

    SCA_LOGI("milo", "http: %s %s", method ? method : "GET", path);

    ret = sca_http_request(&req, &rsp);
    if (sink.overflow) {
        return -ENOMEM;
    }
    if (ret != SCA_OK) {
        SCA_LOGW("milo", "http 失败: %d", ret);
        return -EIO;
    }

    if (http_status_out) {
        *http_status_out = rsp.status;
    }

    /* Date 头用于对时（原设计：天气拉取成功即完成对时，不跑 SNTP）。
     * sca_http 把响应头回填到 response_header_buffer，这里扫一遍。 */
    if (date_out) {
        const char *line = strstr(header_buf, "Date: ");

        if (line) {
            parse_http_date(line + 6, date_out);
        }
    }

    SCA_LOGI("milo", "http: done status=%d len=%u",
             rsp.status, (unsigned)sink.used);
    return 0;
}

int smart_home_miloco_http_get_date(
    const smart_home_miloco_client_config_t *config,
    const char *path,
    char *response,
    size_t response_size,
    int *http_status_out,
    time_t *date_out)
{
    if (date_out) {
        *date_out = (time_t)-1;
    }
    return http_request(config, "GET", path, NULL, response, response_size,
                        http_status_out, date_out);
}

int smart_home_miloco_http_get(const smart_home_miloco_client_config_t *config,
                               const char *path,
                               char *response,
                               size_t response_size,
                               int *http_status_out)
{
    return http_request(config, "GET", path, NULL, response, response_size,
                        http_status_out, NULL);
}

int smart_home_miloco_http_post(const smart_home_miloco_client_config_t *config,
                                const char *path,
                                const char *body,
                                char *response,
                                size_t response_size,
                                int *http_status_out)
{
    return http_request(config, "POST", path, body, response, response_size,
                        http_status_out, NULL);
}
