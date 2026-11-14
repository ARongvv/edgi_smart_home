/* Host integration test: model tool call -> real local tool -> model reply. */
#include <agent.h>
#include <stdio.h>
#include <string.h>

#include "smart_home/tools/smart_home_tools.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static agent_workspace_t g_workspace;
static agent_model_workspace_t g_model_workspace;
static int g_now;
static int g_tool_events;
static int g_tool_result_seen;
static int g_device_id;

int sca_mutex_init(sca_mutex_t *m, const char *name)
{ (void)name; memset(m, 0, sizeof(*m)); m->magic = 1; return SCA_OK; }
void sca_mutex_destroy(sca_mutex_t *m) { memset(m, 0, sizeof(*m)); }
int sca_mutex_lock(sca_mutex_t *m) { (void)m; return SCA_OK; }
int sca_mutex_trylock(sca_mutex_t *m) { (void)m; return SCA_OK; }
void sca_mutex_unlock(sca_mutex_t *m) { (void)m; }
int sca_mutex_ready(const sca_mutex_t *m) { return m && m->magic != 0; }
uint64_t sca_time_ms(void) { return (uint64_t)g_now; }

static uint64_t now_ms(void *context)
{
    (void)context;
    return (uint64_t)g_now;
}

static agent_error_t mock_complete(void *context,
                                   const agent_model_request_t *request,
                                   const agent_model_sink_t *sink)
{
    int *calls = (int *)context;

    (*calls)++;
    if (*calls == 1) {
        char arguments[96];
        int length = snprintf(arguments, sizeof(arguments),
            "{\"room\":\"living_room\",\"device_id\":%d,"
            "\"on\":true,\"brightness\":72}", g_device_id);
        agent_tool_call_view_t call = {
            agent_string_view("call-light-1", 12),
            agent_string_view("set_light", 9),
            agent_string_view(arguments, (size_t)length)
        };
        CHECK(request->message_count == 1);
        CHECK(request->messages[0].role == AGENT_MESSAGE_ROLE_USER);
        return sink->tool_call(sink->context, &call);
    }

    if (request->message_count > 0) {
        const agent_message_view_t *last =
            &request->messages[request->message_count - 1u];
        char result[AGENT_MAX_TOOL_OUTPUT_BYTES + 1u];
        size_t size = last->content.size;
        if (size > AGENT_MAX_TOOL_OUTPUT_BYTES) size = AGENT_MAX_TOOL_OUTPUT_BYTES;
        if (last->content.data && size) memcpy(result, last->content.data, size);
        result[size] = '\0';
        printf("model turn 2: messages=%lu last_role=%d result=%s\n",
               (unsigned long)request->message_count, (int)last->role, result);
        if (last->role == AGENT_MESSAGE_ROLE_TOOL &&
            strstr(result, "\"source\":\"demo\"") &&
            strstr(result, "\"brightness\":72")) {
            g_tool_result_seen = 1;
        }
    }
    {
        static const char device_reply[] = "The living room light is on at 72%.";
        static const char plain_reply[] = "Hello.";
        const char *reply = request->message_count == 1u ? plain_reply : device_reply;
        return sink->text(sink->context, agent_string_view(reply, strlen(reply)));
    }
}

static agent_policy_decision_t allow_local(void *user_data,
                                            const agent_policy_request_t *request)
{
    (void)user_data;
    (void)request;
    return AGENT_POLICY_ALLOW;
}

static void observer(void *user_data, const agent_event_t *event)
{
    (void)user_data;
    if (event->type == AGENT_EVENT_TOOL_END &&
        event->tool_call.name.size == 9 &&
        memcmp(event->tool_call.name.data, "set_light", 9) == 0) {
        g_tool_events++;
    }
}

int main(void)
{
    agent_config_t config = agent_config_default();
    agent_model_ops_t ops = {.complete = mock_complete};
    agent_t *agent = NULL;
    agent_model_t *model = NULL;
    smart_home_state_t state;
    smart_home_device_service_t service;
    smart_home_device_t light;
    agent_request_t request = {0};
    agent_response_t response = {0};
    char output[128];
    int model_calls = 0;
    int device_id = 0;
    int ret;

    config.runtime.now_ms = now_ms;
    config.system_prompt = agent_string_view("Use tools for devices.", 22);
    CHECK(agent_init(&agent, &g_workspace, &config) == AGENT_OK);
    CHECK(agent_model_init(&model, &g_model_workspace, &ops, &model_calls) == AGENT_OK);
    CHECK(agent_set_model(agent, model) == AGENT_OK);
    CHECK(agent_set_event_callback(agent, observer, NULL) == AGENT_OK);
    CHECK(agent_set_policy_callback(agent, allow_local, NULL) == AGENT_OK);

    smart_home_device_init(&state);
    CHECK(smart_home_device_service_init(&service, &state) == AGENT_OK);
    CHECK(smart_home_device_service_add(&service, "living_room", "Main light",
                                        SMART_HOME_DEVICE_LIGHT, &device_id) == AGENT_OK);
    CHECK(device_id > 0);
    g_device_id = device_id;
    CHECK(smart_home_tools_register(agent, &service) == AGENT_OK);
    CHECK(agent_start(agent) == AGENT_OK);

    {
        static const char input[] = "Turn the living room light on to 72%.";
        request.input = agent_string_view(input, sizeof(input) - 1u);
    }
    response.output = output;
    response.output_size = sizeof(output);
    ret = agent_run(agent, &request, &response);
    CHECK(ret == AGENT_OK);
    CHECK(model_calls == 2);
    CHECK(g_tool_result_seen == 1);
    CHECK(g_tool_events == 1);
    CHECK(strcmp(output, "The living room light is on at 72%.") == 0);
    CHECK(smart_home_device_service_get_device(&service, device_id, &light) == AGENT_OK);
    CHECK(light.on == 1 && light.brightness == 72);

    /* A separate ordinary turn needs no tool and returns the final text directly. */
    model_calls = 1;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.input = agent_string_view("Say hello.", 10);
    response.output = output;
    response.output_size = sizeof(output);
    output[0] = '\0';
    CHECK(agent_run(agent, &request, &response) == AGENT_OK);
    CHECK(model_calls == 2);
    CHECK(g_tool_events == 1);
    CHECK(strcmp(output, "Hello.") == 0);

    smart_home_device_service_deinit(&service);
    agent_destroy(agent);
    agent_model_destroy(model);
    printf("test_agent_loop: tool result and local state verified\n");
    return 0;
}
