#include "port/sca_agent_profile.h"
#include "smart_home/agent/smart_home_agent_diag.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void smart_home_agent_diag_profile(void)
{
    printf("[agent_profile] core=%u scratch=%u context=%u tool_output=%u model_output=%u messages=%u tools=%u\n",
           AGENT_CORE_WORKSPACE_BYTES, AGENT_SCRATCH_BYTES,
           AGENT_MAX_CONTEXT_BYTES, AGENT_MAX_TOOL_OUTPUT_BYTES,
           AGENT_MAX_MODEL_OUTPUT_BYTES, AGENT_MAX_PROJECTED_MESSAGES,
           AGENT_MAX_TOOLS);
    printf("[agent_profile] request=%u response=%u decoded=%u json_tokens=%u session_line=%u session_decoded=%u file=%u history=%u retain=%u\n",
           CONFIG_SMART_HOME_MODEL_REQUEST_BUFFER_SIZE, CONFIG_SMART_HOME_MODEL_RESPONSE_BUFFER_SIZE,
           CONFIG_SMART_HOME_MODEL_DECODED_BUFFER_SIZE, CONFIG_SMART_HOME_MODEL_JSON_TOKENS,
           CONFIG_SMART_HOME_SESSION_LINE_BYTES, CONFIG_SMART_HOME_SESSION_PAYLOAD_BYTES,
           CONFIG_SMART_HOME_SESSION_FILE_BYTES, CONFIG_SMART_HOME_SESSION_HISTORY_TURNS,
           CONFIG_SMART_HOME_SESSION_RETAIN_TURNS);
}

void smart_home_agent_diag_begin(smart_home_agent_diag_t *diag)
{
    unsigned long turn;
    if (!diag) return;
    turn = diag->turn + 1u;
    memset(diag, 0, sizeof(*diag));
    diag->turn = turn;
    diag->stage = "session.prepare";
}

void smart_home_agent_diag_error(smart_home_agent_diag_t *diag, const char *stage, int code)
{
    if (diag && code != AGENT_OK) {
        if (diag->failure_stage) return;
        diag->failure_stage = stage;
        diag->stage = stage;
        printf("[agent_diag] turn=%lu stage=%s code=%d last_tool=%s last_call_id=%s\n",
               diag->turn, stage, code, diag->tool[0] ? diag->tool : "-",
               diag->call_id[0] ? diag->call_id : "-");
    }
}

static void copy_view(char *out, size_t capacity, agent_string_view_t value)
{
    size_t bytes = value.data ? value.size : 0u;
    if (bytes >= capacity) bytes = capacity - 1u;
    if (bytes) memcpy(out, value.data, bytes);
    out[bytes] = '\0';
}

void smart_home_agent_diag_observe(smart_home_agent_diag_t *diag, const agent_event_t *event)
{
    if (!diag || !event) return;
    switch (event->type) {
    case AGENT_EVENT_MODEL_BEGIN:
        diag->stage = "model.encode";
        diag->request_bytes = diag->response_bytes = 0u;
        break;
    case AGENT_EVENT_MODEL_END:
        if (event->status != AGENT_OK)
            smart_home_agent_diag_error(diag, diag->stage, event->status);
        break;
    case AGENT_EVENT_TOOL_BEGIN:
        copy_view(diag->tool, sizeof(diag->tool), event->tool_call.name);
        copy_view(diag->call_id, sizeof(diag->call_id), event->tool_call.id);
        diag->stage = "tool.execute";
        printf("[agent_diag] turn=%lu stage=tool.begin tool=%s call_id=%s arguments_bytes=%lu\n",
               diag->turn, diag->tool, diag->call_id,
               (unsigned long)event->tool_call.arguments_json.size);
        break;
    case AGENT_EVENT_TOOL_END:
        printf("[agent_diag] turn=%lu stage=tool.end tool=%s call_id=%s code=%d\n",
               diag->turn, diag->tool, diag->call_id, event->status);
        if (event->status == AGENT_ERROR_CAPACITY || event->status == AGENT_ERROR_LIMIT)
            smart_home_agent_diag_error(diag, "tool.execute", event->status);
        /* Other tool errors can be handled by a later Model step, so do not mark
         * a terminal failure here. The terminal result remains authoritative. */
        break;
    default:
        break;
    }
}

void smart_home_agent_diag_result(smart_home_agent_diag_t *diag,
                                  const agent_response_t *response, int code)
{
    const char *stage;
    if (!diag || !response) return;
    stage = diag->failure_stage ? diag->failure_stage : diag->stage;
    if (code == AGENT_OK) stage = "turn.complete";
    else if (code == AGENT_ERROR_CAPACITY && !diag->failure_stage &&
             response->session_status == AGENT_OK && response->status == code)
        stage = "core.capacity";
    printf("[agent_diag] turn=%lu stage=%s code=%d session=%d delivery=%d models=%u tools=%u scratch_peak=%lu scratch_capacity=%u output_bytes=%lu output_required=%lu\n",
           diag->turn, stage ? stage : "admission", code,
           response->session_status, response->delivery_status,
           (unsigned)response->summary.model_calls, (unsigned)response->summary.tool_calls,
           (unsigned long)response->summary.scratch_peak_bytes, CONFIG_AGENT_SCRATCH_BYTES,
           (unsigned long)response->output_written, (unsigned long)response->output_required);
}

static int add_bytes(size_t *total, size_t bytes)
{
    if (bytes > SIZE_MAX - *total) return AGENT_ERROR_CAPACITY;
    *total += bytes;
    return AGENT_OK;
}

static int string_bytes(size_t *total, agent_string_view_t text)
{
    size_t i;
    int ret;
    if (text.size && !text.data) return AGENT_ERROR_INVALID;
    if ((ret = add_bytes(total, 2u)) != AGENT_OK) return ret;
    for (i = 0u; i < text.size; ++i) {
        unsigned char c = (unsigned char)text.data[i];
        size_t width = c < 0x20u ? 6u : (c == '"' || c == '\\') ? 2u : 1u;
        if ((ret = add_bytes(total, width)) != AGENT_OK) return ret;
    }
    return AGENT_OK;
}

int smart_home_session_message_bytes(const agent_message_view_t *message, size_t *bytes)
{
    size_t total = 0u, i;
    int ret;
    if (!message || !bytes || message->role < AGENT_MESSAGE_ROLE_SYSTEM ||
        message->role > AGENT_MESSAGE_ROLE_TOOL ||
        (message->tool_call_count && !message->tool_calls)) return AGENT_ERROR_INVALID;
#define ADD(n) do { ret = add_bytes(&total, (n)); if (ret) return ret; } while (0)
#define STR(s) do { ret = string_bytes(&total, (s)); if (ret) return ret; } while (0)
    /* Version-1 roles are single decimal digits (1..4). Literal sizes mirror
     * the provider's v1 format; parity against its actual encoder is tested. */
    ADD(sizeof("{\"r\":0,\"c\":") - 1u);
    STR(message->content);
    ADD(sizeof(",\"tid\":") - 1u);
    STR(message->tool_call_id);
    ADD(sizeof(",\"tc\":[") - 1u);
    for (i = 0u; i < message->tool_call_count; ++i) {
        const agent_tool_call_view_t *call = &message->tool_calls[i];
        if (i) ADD(1u);
        ADD(sizeof("{\"id\":") - 1u); STR(call->id);
        ADD(sizeof(",\"name\":") - 1u); STR(call->name);
        ADD(sizeof(",\"a\":") - 1u); STR(call->arguments_json);
        ADD(1u);
    }
    ADD(2u);
#undef STR
#undef ADD
    *bytes = total;
    return AGENT_OK;
}

static agent_error_t traced_begin(void *context, agent_string_view_t id, void **transaction)
{
    smart_home_session_trace_t *trace = context;
    int ret = trace->raw.ops.begin(trace->raw.context, id, transaction);
    smart_home_agent_diag_error(trace->diag, "session.begin", ret);
    return (agent_error_t)ret;
}

static agent_error_t traced_append(void *context, void *transaction,
                                    const agent_message_view_t *message)
{
    smart_home_session_trace_t *trace = context;
    agent_session_jsonl_t *jsonl = trace->jsonl;
    size_t before = jsonl->write_length;
    size_t encoded = 0u, required = before;
    if (!message) return AGENT_ERROR_INVALID;
    int ret = smart_home_session_message_bytes(message, &encoded);
    if (ret == AGENT_OK) ret = add_bytes(&required, encoded);
    if (ret == AGENT_OK) ret = add_bytes(&required, jsonl->message_count ? 1u : 0u);
    /* Budget the complete record trailer AND its terminating NUL. */
    if (ret == AGENT_OK) ret = add_bytes(&required, sizeof("],\"o\":1}\n"));
    if (trace->diag) trace->diag->stage = "session.append";
    if (ret == AGENT_OK)
        ret = trace->raw.ops.append(trace->raw.context, transaction, message);
    printf("[agent_diag] turn=%lu stage=session.append role=%u tool=%s call_id=%.*s code=%d content_bytes=%lu encoded_bytes=%lu used_before=%lu used=%lu required=%lu capacity=%lu messages=%lu/%lu calls=%lu/%lu\n",
           trace->diag ? trace->diag->turn : 0ul, (unsigned)message->role,
           message->role == AGENT_MESSAGE_ROLE_TOOL && trace->diag &&
           message->tool_call_id.size == strlen(trace->diag->call_id) &&
           message->tool_call_id.data && !memcmp(message->tool_call_id.data,
               trace->diag->call_id, message->tool_call_id.size) ? trace->diag->tool : "-",
           (int)message->tool_call_id.size, message->tool_call_id.data ? message->tool_call_id.data : "",
           ret, (unsigned long)message->content.size, (unsigned long)encoded,
           (unsigned long)before, (unsigned long)jsonl->write_length, (unsigned long)required,
           (unsigned long)jsonl->config.write_line_capacity,
           (unsigned long)jsonl->message_count, (unsigned long)jsonl->config.read_message_capacity,
           (unsigned long)jsonl->call_count, (unsigned long)jsonl->config.read_call_capacity);
    smart_home_agent_diag_error(trace->diag, "session.append", ret);
    return (agent_error_t)ret;
}

static agent_error_t traced_finish(void *context, void *transaction,
                                    agent_session_turn_outcome_t outcome)
{
    smart_home_session_trace_t *trace = context;
    int ret;
    if (outcome == AGENT_SESSION_TURN_ABORTED) {
        if (transaction != trace->jsonl || !trace->jsonl->active) return AGENT_ERROR_INVALID;
        /* Preserve the app's complete-turn-only persistence contract. */
        trace->jsonl->active = false;
        return AGENT_OK;
    }
    ret = trace->raw.ops.finish(trace->raw.context, transaction, outcome);
    smart_home_agent_diag_error(trace->diag, "session.finish", ret);
    return (agent_error_t)ret;
}

static agent_error_t traced_recent(void *context, agent_string_view_t id,
                                    size_t candidates, agent_session_visit_fn visit, void *user)
{
    smart_home_session_trace_t *trace = context;
    int ret = trace->raw.ops.recent(trace->raw.context, id, candidates, visit, user);
    if (ret != AGENT_OK) {
        printf("[agent_diag] stage=session.history code=%d line_capacity=%lu decoded_capacity=%lu token_bytes=%lu\n",
               ret, (unsigned long)trace->jsonl->config.read_line_capacity,
               (unsigned long)trace->jsonl->config.decoded_capacity,
               (unsigned long)trace->jsonl->config.token_buffer_bytes);
        smart_home_agent_diag_error(trace->diag, "session.history", ret);
    }
    return (agent_error_t)ret;
}

#define FORWARD_ID(name) \
static agent_error_t traced_##name(void *context, agent_string_view_t id) { \
    smart_home_session_trace_t *trace = context; \
    return trace->raw.ops.name(trace->raw.context, id); \
}
FORWARD_ID(clear)
FORWARD_ID(remove)
#undef FORWARD_ID
static agent_error_t traced_clear_all(void *context)
{
    smart_home_session_trace_t *trace = context;
    return trace->raw.ops.clear_all(trace->raw.context);
}
static agent_error_t traced_count(void *context, size_t *count)
{
    smart_home_session_trace_t *trace = context;
    return trace->raw.ops.count(trace->raw.context, count);
}

int smart_home_session_trace_bind(smart_home_session_trace_t *trace,
                                  agent_session_jsonl_t *jsonl,
                                  smart_home_agent_diag_t *diag,
                                  agent_session_storage_t *storage)
{
    int ret;
    if (!trace || !jsonl || !storage) return AGENT_ERROR_INVALID;
    memset(trace, 0, sizeof(*trace));
    trace->jsonl = jsonl;
    trace->diag = diag;
    ret = agent_session_jsonl_bind(jsonl, &trace->raw);
    if (ret != AGENT_OK) return ret;
    storage->context = trace;
    storage->ops = (agent_session_storage_ops_t){traced_begin, traced_append, traced_finish,
        traced_recent, traced_clear, traced_clear_all, traced_remove, traced_count};
    return AGENT_OK;
}

const char *smart_home_agent_capacity_message(int has_operations, int session_failed)
{
    if (has_operations)
        return session_failed ?
            "Chat stopped: conversation capacity exceeded. Earlier tools may have run; verify the device before retrying." :
            "Chat stopped: agent capacity exceeded. Earlier tools may have run; verify the device before retrying.";
    return session_failed ? "Conversation capacity exceeded. Start a new chat; see serial diagnostics." :
        "Agent capacity exceeded. See serial diagnostics.";
}
