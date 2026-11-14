/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>

#include "packages/cagent/ports/rtthread/agent_rtthread_transport.h"
#include "port/sca_http.h"

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);      \
            return 1;                                                         \
        }                                                                     \
    } while (0)

typedef struct
{
    char events[8];
    size_t event_count;
    int body_calls;
    int saw_content_type;
    agent_error_t headers_result;
    agent_error_t body_result;
} capture_t;

static int g_mock_status;
static const char *g_mock_body;

static void record_event(capture_t *capture, char event)
{
    if (capture->event_count < sizeof(capture->events) - 1u)
    {
        capture->events[capture->event_count++] = event;
        capture->events[capture->event_count] = '\0';
    }
}

static agent_error_t capture_headers(void *context, unsigned int status,
                                     const agent_http_header_t *headers,
                                     size_t count)
{
    capture_t *capture = (capture_t *)context;
    size_t i;

    (void)status;
    record_event(capture, 'H');
    for (i = 0; i < count; i++)
    {
        if (headers[i].name.size == 12u &&
            memcmp(headers[i].name.data, "Content-Type", 12u) == 0 &&
            headers[i].value.size == 16u &&
            memcmp(headers[i].value.data, "application/json", 16u) == 0)
        {
            capture->saw_content_type = 1;
        }
    }
    return capture->headers_result;
}

static agent_error_t capture_body(void *context, const void *data, size_t size)
{
    capture_t *capture = (capture_t *)context;

    (void)data;
    (void)size;
    record_event(capture, 'B');
    capture->body_calls++;
    return capture->body_result;
}

static uint64_t mock_now_ms(void *context)
{
    (void)context;
    return 0;
}

/* 模拟 sca_http 的真实契约：先回填响应头并回调，再流式投递 body。 */
int sca_http_request(const sca_http_request_t *req, sca_http_response_t *rsp)
{
    static const char raw_headers[] =
        "Content-Type: application/json\nX-Test: yes\n";
    size_t body_len = strlen(g_mock_body);

    if (req->response_header_buf != NULL && req->response_header_size > 0)
    {
        size_t copy = sizeof(raw_headers) - 1u;
        if (copy >= req->response_header_size)
        {
            copy = req->response_header_size - 1u;
        }
        memcpy(req->response_header_buf, raw_headers, copy);
        req->response_header_buf[copy] = '\0';
    }

    rsp->status = g_mock_status;
    rsp->content_length = (long)body_len;
    if (req->headers_ready == NULL ||
        req->headers_ready(g_mock_status, req->sink_user) != 0)
    {
        return SCA_ERR_CANCELLED;
    }

    if (body_len > 0 && req->sink != NULL)
    {
        if (req->sink(g_mock_body, body_len, req->sink_user) != 0)
        {
            return SCA_ERR_CANCELLED;
        }
    }
    rsp->body_bytes = body_len;
    return SCA_OK;
}

static agent_error_t execute_request(int http_status, capture_t *capture)
{
    agent_port_rtthread_transport_t state;
    agent_port_rtthread_transport_config_t config;
    agent_transport_t transport;
    agent_http_request_t request;
    agent_http_sink_t sink;
    agent_error_t ret;
    char url[128];
    char response_headers[512];

    memset(&state, 0, sizeof(state));
    memset(&config, 0, sizeof(config));
    memset(&transport, 0, sizeof(transport));
    memset(&request, 0, sizeof(request));
    memset(&sink, 0, sizeof(sink));

    config.runtime.now_ms = mock_now_ms;
    config.request_timeout_ms = 1000u;
    config.url_buffer = url;
    config.url_buffer_size = sizeof(url);
    config.response_header_buffer = response_headers;
    config.response_header_buffer_size = sizeof(response_headers);
    ret = agent_port_rtthread_transport_init(&transport, &state, &config);
    if (ret != AGENT_OK)
    {
        return ret;
    }

    request.method = agent_string_view("POST", 4u);
    request.url = agent_string_view(
        "http://example.test/v1/chat/completions",
        sizeof("http://example.test/v1/chat/completions") - 1u);
    request.max_response_bytes = 1024u;
    request.max_response_header_bytes = sizeof(response_headers);
    sink.headers = capture_headers;
    sink.body = capture_body;
    sink.context = capture;

    g_mock_status = http_status;
    g_mock_body = "{\"ok\":true}";
    return transport.ops->request(transport.context, &request, &sink);
}

static int test_headers_before_body(void)
{
    capture_t capture;
    agent_error_t ret;

    memset(&capture, 0, sizeof(capture));
    ret = execute_request(200, &capture);
    CHECK(ret == AGENT_OK);
    CHECK(strcmp(capture.events, "HB") == 0);
    CHECK(capture.body_calls == 1);
    CHECK(capture.saw_content_type == 1);
    return 0;
}

static int test_header_error_skips_body(void)
{
    capture_t capture;
    agent_error_t ret;

    memset(&capture, 0, sizeof(capture));
    capture.headers_result = AGENT_ERROR_AUTH;
    ret = execute_request(401, &capture);
    CHECK(ret == AGENT_ERROR_AUTH);
    CHECK(strcmp(capture.events, "H") == 0);
    CHECK(capture.body_calls == 0);
    return 0;
}

static int test_body_error_is_preserved(void)
{
    capture_t capture;
    agent_error_t ret;

    memset(&capture, 0, sizeof(capture));
    capture.body_result = AGENT_ERROR_CAPACITY;
    ret = execute_request(200, &capture);
    CHECK(ret == AGENT_ERROR_CAPACITY);
    CHECK(strcmp(capture.events, "HB") == 0);
    CHECK(capture.body_calls == 1);
    return 0;
}

int main(void)
{
    if (test_headers_before_body() != 0) return 1;
    if (test_header_error_skips_body() != 0) return 1;
    if (test_body_error_is_preserved() != 0) return 1;
    printf("test_rtthread_transport: 3 cases passed\n");
    return 0;
}
