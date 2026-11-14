#pragma once

#include "port/sca_agent_profile.h"

#include <agent/session.h>

#ifndef CONFIG_SMART_HOME_DEMO_DATA_ROOT
#define CONFIG_SMART_HOME_DEMO_DATA_ROOT "/data"
#endif
#define SMART_HOME_SESSION_DIR CONFIG_SMART_HOME_DEMO_DATA_ROOT "/smart_home/sessions"
#define SMART_HOME_SESSION_FILE "session-686f6d65.jsonl"

typedef struct smart_home_session smart_home_session_t;

struct smart_home_agent_diag;
int smart_home_session_open(smart_home_session_t **out, agent_session_storage_t *storage,
                             struct smart_home_agent_diag *diag);
void smart_home_session_close(smart_home_session_t **session);
int smart_home_session_prepare(smart_home_session_t *session);
/* Borrowed complete groups, newest first; no reentry or retained pointers. */
int smart_home_session_visit(smart_home_session_t *session,
                             agent_session_visit_fn visit, void *context);
