/* Real Core + JSONL provider regression; only file IO and Model are in memory. */
#include "smart_home/agent/smart_home_agent_diag.h"
#include "json_internal.h"
#include <stdio.h>

/* Private JSONL provider entry point, normally declared by the vendor's
 * jsonl_internal.h. Both that header and agent_session_jsonl.h rely on
 * #pragma once alone, which tcc 0.9.27 does not honour when the same public
 * header is reached from two include chains. Including json_internal.h
 * directly keeps agent_session_jsonl.h single-instantiated. */
agent_error_t agent_jsonl_encode_message(agent_json_writer_t *writer,
                                         const agent_message_view_t *message);
#include <stdlib.h>
#include <string.h>

#define SV(s) ((agent_string_view_t)AGENT_SV_LITERAL(s))
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static char disk[2u * CONFIG_SMART_HOME_SESSION_LINE_BYTES];
static size_t disk_bytes;
static char write_line[CONFIG_SMART_HOME_SESSION_LINE_BYTES];
static char read_line[CONFIG_SMART_HOME_SESSION_LINE_BYTES];
static char decoded[CONFIG_SMART_HOME_SESSION_PAYLOAD_BYTES];
static int tokens[16384];
static agent_message_view_t read_messages[AGENT_MAX_PROJECTED_MESSAGES];
static agent_tool_call_view_t read_calls[AGENT_MAX_PROJECTED_MESSAGES];
static char session_id[8];
static agent_session_jsonl_t jsonl;
static smart_home_session_trace_t trace;
static smart_home_agent_diag_t diag;
static agent_session_storage_t storage;
static agent_workspace_t workspace;
static agent_model_workspace_t model_workspace;
static char payload[3000];
static int effects, model_calls, visited;

static agent_error_t file_size(void *ctx, agent_string_view_t id, uint64_t *bytes)
{ (void)ctx; (void)id; *bytes = disk_bytes; return AGENT_OK; }
static agent_error_t file_read(void *ctx, agent_string_view_t id, uint64_t offset, char *out, size_t bytes)
{
    (void)ctx; (void)id;
    if (offset > disk_bytes || bytes > disk_bytes - (size_t)offset) return AGENT_ERROR_IO;
    memcpy(out, disk + (size_t)offset, bytes); return AGENT_OK;
}
static agent_error_t file_append(void *ctx, agent_string_view_t id, const char *data, size_t bytes)
{
    (void)ctx; (void)id;
    if (bytes > sizeof(disk) - disk_bytes) return AGENT_ERROR_CAPACITY;
    memcpy(disk + disk_bytes, data, bytes); disk_bytes += bytes; return AGENT_OK;
}
static agent_error_t file_truncate(void *ctx, agent_string_view_t id, uint64_t bytes)
{ (void)ctx; (void)id; if (bytes > disk_bytes) return AGENT_ERROR_IO; disk_bytes = (size_t)bytes; return AGENT_OK; }
static agent_error_t file_sync(void *ctx, agent_string_view_t id)
{ (void)ctx; (void)id; return AGENT_OK; }
static agent_error_t file_clear(void *ctx, agent_string_view_t id)
{ (void)ctx; (void)id; disk_bytes = 0u; return AGENT_OK; }
static agent_error_t file_clear_all(void *ctx)
{ (void)ctx; disk_bytes = 0u; return AGENT_OK; }
static agent_error_t file_count(void *ctx, size_t *count)
{ (void)ctx; *count = disk_bytes ? 1u : 0u; return AGENT_OK; }

static void bind_storage(size_t line_capacity)
{
    agent_session_jsonl_config_t config = {0};
    config.files = (agent_session_jsonl_file_ops_t){file_size, file_read, file_append,
        file_truncate, file_sync, file_clear, file_clear_all, file_count};
    config.write_line = write_line; config.write_line_capacity = line_capacity;
    config.read_line = read_line; config.read_line_capacity = sizeof(read_line);
    config.decoded = decoded; config.decoded_capacity = sizeof(decoded);
    config.token_buffer = tokens; config.token_buffer_bytes = sizeof(tokens);
    config.read_messages = read_messages; config.read_message_capacity = AGENT_MAX_PROJECTED_MESSAGES;
    config.read_calls = read_calls; config.read_call_capacity = AGENT_MAX_PROJECTED_MESSAGES;
    config.session_id = session_id; config.session_id_capacity = sizeof(session_id);
    CHECK(agent_session_jsonl_init(&jsonl, &config) == AGENT_OK);
    CHECK(smart_home_session_trace_bind(&trace, &jsonl, &diag, &storage) == AGENT_OK);
}

static agent_error_t tool_execute(void *ctx, const agent_tool_context_t *call, const agent_text_sink_t *sink)
{
    (void)ctx; (void)call; ++effects;
    return sink->write(sink->context, agent_string_view(payload, sizeof(payload)));
}

static agent_error_t model_complete(void *ctx, const agent_model_request_t *request,
                                     const agent_model_sink_t *sink)
{
    (void)ctx; ++model_calls;
    if (model_calls == 1) {
        agent_tool_call_view_t first = {SV("call-1"), SV("large_result"), SV("{}")};
        agent_tool_call_view_t second = {SV("call-2"), SV("large_result"), SV("{}")};
        CHECK(sink->tool_call(sink->context, &first) == AGENT_OK);
        return sink->tool_call(sink->context, &second);
    }
    CHECK(request->message_count == 4u);
    CHECK(request->messages[2].content.size == sizeof(payload));
    CHECK(request->messages[3].content.size == sizeof(payload));
    return sink->text(sink->context, SV("Completed."));
}

static uint64_t now_ms(void *ctx) { (void)ctx; return 1000u; }
static agent_policy_decision_t allow(void *ctx, const agent_policy_request_t *request)
{ (void)ctx; (void)request; return AGENT_POLICY_ALLOW; }

static void observe(void *ctx, const agent_event_t *event)
{ smart_home_agent_diag_observe(ctx, event); }

static agent_response_t run_turn(size_t capacity, int expected)
{
    static char output[128];
    agent_t *agent = NULL;
    agent_model_t *model = NULL;
    agent_config_t config = agent_config_default();
    agent_model_ops_t model_ops = {model_complete};
    agent_tool_t tool = {0};
    agent_request_t request = {0};
    agent_response_t response = {0};
    config.runtime.now_ms = now_ms;
    effects = model_calls = 0;
    bind_storage(capacity);
    smart_home_agent_diag_begin(&diag);
    CHECK(agent_init(&agent, &workspace, &config) == AGENT_OK);
    CHECK(agent_model_init(&model, &model_workspace, &model_ops, NULL) == AGENT_OK);
    CHECK(agent_set_model(agent, model) == AGENT_OK);
    CHECK(agent_set_policy_callback(agent, allow, NULL) == AGENT_OK);
    CHECK(agent_set_session_storage(agent, &storage) == AGENT_OK);
    CHECK(agent_set_event_callback(agent, observe, &diag) == AGENT_OK);
    tool.name = SV("large_result");
    tool.description = SV("Large escaped result");
    tool.input_schema_json = SV("{\"type\":\"object\"}");
    tool.execute = tool_execute;
    CHECK(agent_register_tool(agent, &tool) == AGENT_OK);
    CHECK(agent_start(agent) == AGENT_OK);
    request.session_id = SV("home"); request.input = SV("Run both tools.");
    response.output = output; response.output_size = sizeof(output);
    CHECK(agent_run(agent, &request, &response) == expected);
    smart_home_agent_diag_result(&diag, &response, expected);
    agent_destroy(agent); agent_model_destroy(model);
    return response;
}

static agent_error_t visit(void *ctx, const agent_session_group_view_t *group)
{
    (void)ctx; ++visited;
    CHECK(group->message_count == 5u);
    CHECK(group->messages[2].content.size == sizeof(payload));
    CHECK(!memcmp(group->messages[2].content.data, payload, sizeof(payload)));
    return AGENT_OK;
}

static void test_size_parity(void)
{
    static char encoded[32768];
    const char text[] = "Unicode 中\n\t\r\"\\\001";
    agent_tool_call_view_t calls[2] = {
        {SV("a"), SV("tool"), SV("{\"name\":\"中\\n\"}")},
        {SV("b"), SV("other"), SV("{}")}};
    int role;
    for (role = AGENT_MESSAGE_ROLE_SYSTEM; role <= AGENT_MESSAGE_ROLE_TOOL; ++role) {
        agent_message_view_t message = {0};
        agent_json_writer_t writer;
        size_t measured = 0u;
        message.role = (agent_message_role_t)role;
        message.content = agent_string_view(text, sizeof(text) - 1u);
        message.tool_call_id = SV("id\"\\");
        if (role == AGENT_MESSAGE_ROLE_ASSISTANT) { message.tool_calls = calls; message.tool_call_count = 2u; }
        agent_json_writer_init(&writer, encoded, sizeof(encoded));
        CHECK(agent_jsonl_encode_message(&writer, &message) == AGENT_OK);
        CHECK(smart_home_session_message_bytes(&message, &measured) == AGENT_OK);
        CHECK(measured == writer.length);
    }
}

static void test_exact_boundary(void)
{
    agent_message_view_t user = {0}, reply = {0};
    void *transaction = NULL;
    size_t a, b, exact;
    user.role = AGENT_MESSAGE_ROLE_USER; user.content = SV("Test.");
    reply.role = AGENT_MESSAGE_ROLE_ASSISTANT;
    reply.content = SV("A reply with \"quotes\", a newline\n and 中.");
    CHECK(smart_home_session_message_bytes(&user, &a) == AGENT_OK);
    CHECK(smart_home_session_message_bytes(&reply, &b) == AGENT_OK);
    exact = strlen("{\"v\":1,\"sid\":\"home\",\"m\":[") + a + 1u + b + sizeof("],\"o\":1}\n");
    disk_bytes = 0u;
    bind_storage(exact);
    CHECK(storage.ops.begin(storage.context, SV("home"), &transaction) == AGENT_OK);
    CHECK(storage.ops.append(storage.context, transaction, &user) == AGENT_OK);
    CHECK(storage.ops.append(storage.context, transaction, &reply) == AGENT_OK);
    CHECK(storage.ops.finish(storage.context, transaction, AGENT_SESSION_TURN_COMPLETE) == AGENT_OK);
    CHECK(disk_bytes + 1u == exact);
    disk_bytes = 0u;
    bind_storage(exact - 1u);
    CHECK(storage.ops.begin(storage.context, SV("home"), &transaction) == AGENT_OK);
    CHECK(storage.ops.append(storage.context, transaction, &user) == AGENT_OK);
    CHECK(storage.ops.append(storage.context, transaction, &reply) == AGENT_ERROR_CAPACITY);
    CHECK(storage.ops.finish(storage.context, transaction, AGENT_SESSION_TURN_ABORTED) == AGENT_OK);
    CHECK(disk_bytes == 0u);
}

int main(void)
{
    agent_response_t response;
    size_t saved;
    static char saved_disk[sizeof(disk)];
    memset(payload, '"', sizeof(payload));
    payload[1] = '\n'; payload[2] = '\\';
    CHECK(AGENT_SCRATCH_BYTES == 65536u && sizeof(workspace) >= 98304u);
    test_size_parity();
    test_exact_boundary();
    response = run_turn(CONFIG_SMART_HOME_SESSION_LINE_BYTES, AGENT_OK);
    CHECK(effects == 2 && model_calls == 2);
    CHECK(response.session_status == AGENT_OK && !strcmp(response.output, "Completed."));
    CHECK(disk_bytes > 4096u && disk_bytes < CONFIG_SMART_HOME_SESSION_LINE_BYTES);
    saved = disk_bytes; memcpy(saved_disk, disk, saved);
    /* Rebinding emulates restart: decode the committed group, without tools. */
    bind_storage(CONFIG_SMART_HOME_SESSION_LINE_BYTES);
    visited = 0;
    CHECK(storage.ops.recent(storage.context, SV("home"), 20u, visit, NULL) == AGENT_OK);
    CHECK(visited == 1 && effects == 2);
    /* Old 4 KiB limit reproduces Done -> failed session append. */
    response = run_turn(4096u, AGENT_ERROR_CAPACITY);
    CHECK(effects == 1 && model_calls == 1);
    CHECK(response.session_status == AGENT_ERROR_CAPACITY && response.output_written == 0u);
    CHECK(diag.failure_stage && !strcmp(diag.failure_stage, "session.append"));
    CHECK(!strcmp(diag.tool, "large_result") && !strcmp(diag.call_id, "call-1"));
    CHECK(disk_bytes == saved && !memcmp(disk, saved_disk, saved));
    CHECK(strstr(smart_home_agent_capacity_message(1, 1), "conversation capacity"));
    CHECK(strstr(smart_home_agent_capacity_message(1, 1), "may have run"));
    CHECK(!strstr(smart_home_agent_capacity_message(0, 0), "may have run"));
    puts("test_agent_capacity: new profile, encoder parity, old-limit reproduction, history preservation and no replay passed");
    return 0;
}
