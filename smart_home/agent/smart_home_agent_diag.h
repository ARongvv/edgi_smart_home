#pragma once

#include <agent.h>
#include <agent_session_jsonl.h>

typedef struct smart_home_agent_diag {
    unsigned long turn;
    const char *stage;
    const char *failure_stage;
    char tool[AGENT_MAX_NAME_BYTES + 1u];
    char call_id[AGENT_MAX_IDENTIFIER_BYTES + 1u];
    size_t request_bytes;
    size_t response_bytes;
} smart_home_agent_diag_t;

typedef struct {
    agent_session_jsonl_t *jsonl;
    agent_session_storage_t raw;
    smart_home_agent_diag_t *diag;
} smart_home_session_trace_t;

void smart_home_agent_diag_profile(void);
void smart_home_agent_diag_begin(smart_home_agent_diag_t *diag);
void smart_home_agent_diag_observe(smart_home_agent_diag_t *diag, const agent_event_t *event);
void smart_home_agent_diag_error(smart_home_agent_diag_t *diag, const char *stage, int code);
void smart_home_agent_diag_result(smart_home_agent_diag_t *diag,
                                  const agent_response_t *response, int code);

/* Exact v1 JSONL message length, after JSON escaping, excluding its terminator.
 * Size arithmetic only: never invokes tools, allocates, or writes user data. */
int smart_home_session_message_bytes(const agent_message_view_t *message, size_t *bytes);
int smart_home_session_trace_bind(smart_home_session_trace_t *trace,
                                  agent_session_jsonl_t *jsonl,
                                  smart_home_agent_diag_t *diag,
                                  agent_session_storage_t *storage);

/* UI-safe classification; shared with host tests. */
const char *smart_home_agent_capacity_message(int has_operations, int session_failed);
