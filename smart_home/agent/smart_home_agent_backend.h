#pragma once

#include "port/sca_agent_profile.h"

#include <agent.h>
#include <stdint.h>

typedef struct {
    char backend_id[32];
    char host[128];
    char path[128];
    char port[8];
    char api_key[256];
    char model[64];
    int timeout_ms;
    uint32_t request_buffer_size;
    uint32_t response_buffer_size;
    uint32_t max_output_tokens;
} smart_home_model_config_t;

typedef struct smart_home_agent_backend smart_home_agent_backend_t;

/* Creates a CONFIGURING Agent; register contributions before agent_start(). */
int smart_home_agent_backend_create(smart_home_agent_backend_t **out,
                                    agent_t **agent,
                                    const char *system_prompt,
                                    const smart_home_model_config_t *config);
/* Caller serializes configuration, agent_run and destruction. */
int smart_home_agent_backend_apply(smart_home_agent_backend_t *backend,
                                   const smart_home_model_config_t *config);
void smart_home_agent_backend_destroy(smart_home_agent_backend_t **backend);

int smart_home_agent_backend_prepare_turn(smart_home_agent_backend_t *backend);

int smart_home_agent_backend_visit_history(smart_home_agent_backend_t *backend,
                                            agent_session_visit_fn visit, void *context);

/* Diagnostics stay on the serialized Agent worker. */
void smart_home_agent_backend_begin_turn(smart_home_agent_backend_t *backend);
void smart_home_agent_backend_observe(smart_home_agent_backend_t *backend,
                                      const agent_event_t *event);
void smart_home_agent_backend_log_result(smart_home_agent_backend_t *backend,
                                         const agent_response_t *response, int code);
