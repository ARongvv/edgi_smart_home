/* OpenAI-compatible provider tests with a scripted local transport. */
#include <agent_openai_model.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static const char *g_response;
static unsigned int g_status;
static agent_error_t g_transport_error;
static int g_authorized;
static int g_model_seen;

static uint64_t now_ms(void *context) { (void)context; return 1000u; }

static agent_error_t mock_request(void *context,
                                  const agent_http_request_t *request,
                                  const agent_http_sink_t *sink)
{
    const char *cursor = g_response;
    size_t remaining = strlen(g_response);
    size_t i;
    agent_http_header_t content_type = {
        agent_string_view("Content-Type", 12),
        agent_string_view("application/json", 16)
    };
    (void)context;
    g_authorized = 0;
    g_model_seen = request->body && strstr((const char *)request->body,
                                           "\"model\":\"deepseek-chat\"") != NULL;
    for (i = 0u; i < request->header_count; ++i) {
        if (request->headers[i].name.size == 13u &&
            memcmp(request->headers[i].name.data, "Authorization", 13u) == 0 &&
            request->headers[i].value.size == 11u &&
            memcmp(request->headers[i].value.data, "Bearer test", 11u) == 0)
            g_authorized = 1;
    }
    if (g_transport_error != AGENT_OK) return g_transport_error;
    if (sink->headers) {
        agent_error_t ret = sink->headers(sink->context, g_status, &content_type, 1u);
        if (ret != AGENT_OK) return ret;
    }
    while (remaining && sink->body) {
        size_t chunk = remaining > 3u ? 3u : remaining;
        agent_error_t ret = sink->body(sink->context, cursor, chunk);
        if (ret != AGENT_OK) return ret;
        cursor += chunk;
        remaining -= chunk;
    }
    return AGENT_OK;
}

static agent_error_t capture_text(void *context, agent_string_view_t text)
{
    char *output = (char *)context;
    size_t used = strlen(output);
    memcpy(output + used, text.data, text.size);
    output[used + text.size] = '\0';
    return AGENT_OK;
}

static agent_error_t capture_call(void *context, const agent_tool_call_view_t *call)
{
    int *seen = (int *)context;
    if (call->name.size == 9u && memcmp(call->name.data, "set_light", 9u) == 0 &&
        call->arguments_json.size == 11u &&
        memcmp(call->arguments_json.data, "{\"on\":true}", 11u) == 0)
        *seen = 1;
    return AGENT_OK;
}

static agent_error_t complete(agent_openai_provider_t *provider,
                              agent_model_sink_t *sink)
{
    agent_message_view_t message = {0};
    agent_model_request_t request = {0};
    static const char input[] = "Hello";
    message.role = AGENT_MESSAGE_ROLE_USER;
    message.content = agent_string_view(input, sizeof(input) - 1u);
    request.messages = &message;
    request.message_count = 1u;
    request.max_output_tokens = 128u;
    return agent_openai_model_ops()->complete(provider, &request, sink);
}

int main(void)
{
    static char request_buffer[8192];
    static char response_buffer[8192];
    static char decoded_buffer[8192];
    agent_openai_provider_t provider;
    agent_openai_config_t config;
    agent_transport_ops_t transport_ops = {.request = mock_request};
    agent_model_sink_t sink;
    agent_error_t ret;
    char output[256];
    int call_seen = 0;

    memset(&config, 0, sizeof(config));
    config.transport.ops = &transport_ops;
    config.runtime.now_ms = now_ms;
    {
        static const char endpoint[] = "https://api.deepseek.com/v1/chat/completions";
        config.endpoint = agent_string_view(endpoint, sizeof(endpoint) - 1u);
    }
    config.model = agent_string_view("deepseek-chat", 13);
    config.authorization = agent_string_view("Bearer test", 11);
    config.use_legacy_max_tokens = true;
    config.request_buffer = request_buffer;
    config.request_capacity = sizeof(request_buffer);
    config.response_buffer = response_buffer;
    config.response_capacity = sizeof(response_buffer);
    config.decoded_buffer = decoded_buffer;
    config.decoded_capacity = sizeof(decoded_buffer);
    config.response_token_capacity = 512u;
    config.max_response_header_bytes = 1024u;
    config.max_json_depth = 16u;
    CHECK(agent_openai_provider_init(&provider, &config) == AGENT_OK);

    output[0] = '\0';
    sink.text = capture_text;
    sink.tool_call = capture_call;
    sink.context = output;
    g_status = 200u;
    g_transport_error = AGENT_OK;
    g_response = "{\"choices\":[{\"message\":{\"role\":\"assistant\","
                 "\"content\":\"Hello.\"},\"finish_reason\":\"stop\"}]}";
    ret = complete(&provider, &sink);
    CHECK(ret == AGENT_OK);
    CHECK(strcmp(output, "Hello.") == 0);
    CHECK(g_authorized && g_model_seen);

    sink.context = &call_seen;
    g_response = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,"
                 "\"tool_calls\":[{\"id\":\"call1\",\"type\":\"function\","
                 "\"function\":{\"name\":\"set_light\","
                 "\"arguments\":\"{\\\"on\\\":true}\"}}]},"
                 "\"finish_reason\":\"tool_calls\"}]}";
    ret = complete(&provider, &sink);
    CHECK(ret == AGENT_OK);
    CHECK(call_seen == 1);

    output[0] = '\0';
    sink.context = output;
    g_status = 401u;
    g_response = "{\"error\":{\"message\":\"invalid key\"}}";
    CHECK(complete(&provider, &sink) == AGENT_ERROR_AUTH);

    g_transport_error = AGENT_ERROR_TIMEOUT;
    CHECK(complete(&provider, &sink) == AGENT_ERROR_TIMEOUT);

    printf("test_openai_provider: text, fragmented tool call, 401 and timeout passed\n");
    return 0;
}
