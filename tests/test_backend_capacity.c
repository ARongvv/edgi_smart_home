/* Exercise the actual backend/provider/diagnostic transport with mocked IO.
 * Persistence is off here to cover the RAM branch and its 3-turn profile.
 */
#include "smart_home/agent/smart_home_agent_backend.h"
#include "agent_rtthread_runtime.h"
#include "agent_rtthread_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define SV(s) ((agent_string_view_t)AGENT_SV_LITERAL(s))
static int mode, allocations, exchanges;
static uint64_t now_ms(void *context) { (void)context; return 1000u; }
void *smart_home_bulk_alloc(size_t size)
{ void *p = malloc(size); if (p) ++allocations; return p; }
void smart_home_bulk_free(void *p) { if (p) { --allocations; free(p); } }
agent_error_t agent_port_rtthread_runtime_init(agent_runtime_t *out)
{ memset(out, 0, sizeof(*out)); out->now_ms = now_ms; return AGENT_OK; }

static agent_error_t http_request(void *context, const agent_http_request_t *request,
                                   const agent_http_sink_t *sink)
{
    static const char body[] = "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Hello.\"},\"finish_reason\":\"stop\"}]}";
    static char too_large[CONFIG_SMART_HOME_MODEL_RESPONSE_BUFFER_SIZE + 1u];
    agent_http_header_t header = {AGENT_SV_LITERAL("Content-Type"), AGENT_SV_LITERAL("application/json")};
    agent_error_t ret;
    (void)context; ++exchanges;
    CHECK(request->body_size > 0u && request->body_size < CONFIG_SMART_HOME_MODEL_REQUEST_BUFFER_SIZE);
    CHECK(request->max_response_bytes == CONFIG_SMART_HOME_MODEL_RESPONSE_BUFFER_SIZE);
    ret = sink->headers(sink->context, mode == 1 ? 401u : 200u, &header, 1u);
    if (ret) return ret;
    if (mode == 2) return sink->body(sink->context, too_large, sizeof(too_large));
    if (mode == 3) return sink->body(sink->context, "bad json", 8u);
    /* Fragmentation must remain transparent through the diagnostic wrapper. */
    ret = sink->body(sink->context, body, 17u);
    return ret ? ret : sink->body(sink->context, body + 17u, sizeof(body) - 1u - 17u);
}
agent_error_t agent_port_rtthread_transport_init(agent_transport_t *out,
    agent_port_rtthread_transport_t *state, const agent_port_rtthread_transport_config_t *config)
{
    static const agent_transport_ops_t ops = {http_request};
    memset(state, 0, sizeof(*state)); state->config = *config;
    out->ops = &ops; out->context = state;
    return AGENT_OK;
}
static void observe(void *context, const agent_event_t *event)
{ smart_home_agent_backend_observe(context, event); }

int main(void)
{
    smart_home_model_config_t config = {0};
    smart_home_agent_backend_t *backend = NULL;
    agent_t *agent = NULL;
    const int expected[] = {AGENT_OK, AGENT_ERROR_AUTH, AGENT_ERROR_CAPACITY, AGENT_ERROR_MODEL_PARSE};
    strcpy(config.host, "test.invalid"); strcpy(config.path, "/chat/completions");
    strcpy(config.port, "443"); strcpy(config.model, "mock"); strcpy(config.api_key, "test-only");
    config.timeout_ms = 5000;
    config.request_buffer_size = CONFIG_SMART_HOME_MODEL_REQUEST_BUFFER_SIZE;
    config.response_buffer_size = CONFIG_SMART_HOME_MODEL_RESPONSE_BUFFER_SIZE;
    CHECK(smart_home_agent_backend_create(&backend, &agent, "Test.", &config) == AGENT_OK);
    CHECK(agent_set_event_callback(agent, observe, backend) == AGENT_OK);
    CHECK(agent_start(agent) == AGENT_OK);
    for (mode = 0; mode < 4; ++mode) {
        char output[128];
        agent_request_t request = {0};
        agent_response_t response = {0};
        int ret;
        smart_home_agent_backend_begin_turn(backend);
        CHECK(smart_home_agent_backend_prepare_turn(backend) == AGENT_OK);
        request.session_id = SV("home"); request.input = SV("Hello");
        response.output = output; response.output_size = sizeof(output);
        ret = agent_run(agent, &request, &response);
        smart_home_agent_backend_log_result(backend, &response, ret);
        CHECK(ret == expected[mode]);
        if (!mode) CHECK(!strcmp(output, "Hello."));
        else CHECK(response.output_written == 0u);
    }
    CHECK(exchanges == 4);
    smart_home_agent_backend_destroy(&backend);
    CHECK(!backend && allocations == 0);
    puts("test_backend_capacity: RAM profile, fragmented reply, auth, response overflow, decode failure and cleanup passed");
    return 0;
}
