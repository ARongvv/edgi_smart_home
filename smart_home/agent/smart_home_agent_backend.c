#include "port/sca_config.h"

#include "smart_home/agent/smart_home_agent_backend.h"
#include "smart_home/core/smart_home_memory.h"

#include <agent/model.h>
#include <agent_session_ram.h>
#include "smart_home/agent/smart_home_session.h"
#include "smart_home/agent/smart_home_agent_diag.h"
#include <agent_openai_model.h>
#include "agent_rtthread_runtime.h"
#include "agent_rtthread_transport.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    agent_openai_provider_t provider;
    char endpoint[288];
    char model[64];
    char authorization[264];
    char *request;
    char *response;
    char *decoded;
    size_t request_size;
    size_t response_size;
} smart_home_model_state_t;

struct smart_home_agent_backend {
    agent_workspace_t core_storage;
    agent_model_workspace_t model_storage;
    agent_t *agent;
    agent_model_t *model;
    agent_runtime_t runtime;
    agent_transport_t transport;
    agent_transport_t raw_transport;
    smart_home_agent_diag_t diag;
    agent_port_rtthread_transport_t transport_state;
    agent_limits_t limits;
    smart_home_model_state_t *active;
#if SCA_ENABLE_SESSION_PERSIST
    smart_home_session_t *persistent;
#else
    agent_session_ram_t session;
    agent_session_ram_turn_t turns[SCA_SESSION_TURNS_PROJECTED + 1u];
    agent_session_ram_message_t messages[(SCA_SESSION_TURNS_PROJECTED + 1u) * AGENT_MAX_PROJECTED_MESSAGES];
    agent_session_ram_call_t calls[(SCA_SESSION_TURNS_PROJECTED + 1u) * AGENT_MAX_PROJECTED_MESSAGES];
    agent_message_view_t read_messages[AGENT_MAX_PROJECTED_MESSAGES];
    agent_tool_call_view_t read_calls[AGENT_MAX_PROJECTED_MESSAGES];
    char session_payload[SCA_SESSION_BYTES];
#endif
    char url[512];
    char response_header_text[2048];
};


typedef struct {
    smart_home_agent_backend_t *backend;
    const agent_http_sink_t *downstream;
} diagnostic_exchange_t;

static agent_error_t diagnostic_headers(void *context, unsigned int status,
                                         const agent_http_header_t *headers, size_t count)
{
    diagnostic_exchange_t *exchange = context;
    printf("[agent_diag] turn=%lu stage=model.http status=%u\n",
           exchange->backend->diag.turn, status);
    return exchange->downstream->headers ? exchange->downstream->headers(
        exchange->downstream->context, status, headers, count) : AGENT_OK;
}

static agent_error_t diagnostic_body(void *context, const void *data, size_t size)
{
    diagnostic_exchange_t *exchange = context;
    smart_home_agent_backend_t *backend = exchange->backend;
    int ret;
    size_t before = backend->diag.response_bytes;
    if (size > SIZE_MAX - before) return AGENT_ERROR_CAPACITY;
    backend->diag.response_bytes += size;
    ret = exchange->downstream->body ? exchange->downstream->body(
        exchange->downstream->context, data, size) : AGENT_OK;
    if (ret != AGENT_OK) {
        printf("[agent_diag] turn=%lu stage=model.response code=%d used=%lu attempted=%lu capacity=%lu\n",
               backend->diag.turn, ret, (unsigned long)before,
               (unsigned long)backend->diag.response_bytes,
               (unsigned long)backend->active->response_size);
        smart_home_agent_diag_error(&backend->diag, "model.response", ret);
    }
    return (agent_error_t)ret;
}

static agent_error_t diagnostic_request(void *context, const agent_http_request_t *request,
                                         const agent_http_sink_t *sink)
{
    smart_home_agent_backend_t *backend = context;
    diagnostic_exchange_t exchange = {backend, sink};
    agent_http_sink_t traced_sink = {diagnostic_headers, diagnostic_body, &exchange};
    int ret;
    backend->diag.stage = "model.http";
    backend->diag.request_bytes = request->body_size;
    printf("[agent_diag] turn=%lu stage=model.request bytes=%lu capacity=%lu\n",
           backend->diag.turn, (unsigned long)request->body_size,
           (unsigned long)backend->active->request_size);
    ret = backend->raw_transport.ops->request(backend->raw_transport.context, request, &traced_sink);
    if (ret != AGENT_OK) smart_home_agent_diag_error(&backend->diag, "model.http", ret);
    else backend->diag.stage = "model.decode";
    return (agent_error_t)ret;
}

static const agent_transport_ops_t diagnostic_transport_ops = {diagnostic_request};

static void secure_clear(void *memory, size_t size)
{
    volatile unsigned char *p = memory;
    while (p && size-- > 0u) *p++ = 0u;
}

static void model_state_destroy(smart_home_model_state_t *state)
{
    if (!state) return;
    secure_clear(state->request, state->request_size);
    secure_clear(state->response, state->response_size);
    secure_clear(state->decoded, CONFIG_SMART_HOME_MODEL_DECODED_BUFFER_SIZE);
    smart_home_bulk_free(state->request);
    smart_home_bulk_free(state->response);
    smart_home_bulk_free(state->decoded);
    secure_clear(state, sizeof(*state));
    smart_home_bulk_free(state);
}

static int validate_config(const smart_home_model_config_t *config)
{
    const char *p;
    unsigned long port;

    if (!config || !memchr(config->host, '\0', sizeof(config->host)) ||
        !memchr(config->path, '\0', sizeof(config->path)) ||
        !memchr(config->port, '\0', sizeof(config->port)) ||
        !memchr(config->model, '\0', sizeof(config->model)) ||
        !memchr(config->api_key, '\0', sizeof(config->api_key)) ||
        !config->host[0] || config->path[0] != '/' || !config->port[0] ||
        !config->model[0] || config->timeout_ms < 5000 ||
        config->timeout_ms > 60000) return AGENT_ERROR_INVALID;

    for (p = config->host; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-')
            return AGENT_ERROR_INVALID;
    }
    for (p = config->port; *p; p++) {
        if (!isdigit((unsigned char)*p)) return AGENT_ERROR_INVALID;
    }
    port = strtoul(config->port, NULL, 10);
    if (port == 0u || port > 65535u) return AGENT_ERROR_INVALID;
    if (config->request_buffer_size == 0u || config->response_buffer_size == 0u ||
        config->request_buffer_size > SCA_MODEL_REQ_BYTES ||
        config->response_buffer_size > SCA_MODEL_RSP_BYTES)
        return AGENT_ERROR_CAPACITY;
    return AGENT_OK;
}

static int model_state_create(smart_home_agent_backend_t *backend,
                              const smart_home_model_config_t *config,
                              smart_home_model_state_t **out)
{
    smart_home_model_state_t *state;
    agent_openai_config_t provider = {0};
    int n;
    int ret = validate_config(config);

    *out = NULL;
    if (ret != AGENT_OK) return ret;
    state = smart_home_bulk_alloc(sizeof(*state));
    if (!state) return AGENT_ERROR_NOMEM;
    memset(state, 0, sizeof(*state));
    state->request_size = config->request_buffer_size;
    state->response_size = config->response_buffer_size;
    state->request = smart_home_bulk_alloc(state->request_size);
    state->response = smart_home_bulk_alloc(state->response_size);
    state->decoded = smart_home_bulk_alloc(CONFIG_SMART_HOME_MODEL_DECODED_BUFFER_SIZE);
    if (!state->request || !state->response || !state->decoded) {
        ret = AGENT_ERROR_NOMEM;
        goto fail;
    }
    n = snprintf(state->endpoint, sizeof(state->endpoint), "https://%s:%s%s",
                 config->host, config->port, config->path);
    if (n < 0 || (size_t)n >= sizeof(state->endpoint)) {
        ret = AGENT_ERROR_LIMIT;
        goto fail;
    }
    memcpy(state->model, config->model, strlen(config->model) + 1u);
    if (config->api_key[0]) {
        n = snprintf(state->authorization, sizeof(state->authorization),
                     "Bearer %s", config->api_key);
        if (n < 0 || (size_t)n >= sizeof(state->authorization)) {
            ret = AGENT_ERROR_LIMIT;
            goto fail;
        }
    }

    provider.transport = backend->transport;
    provider.runtime = backend->runtime;
    provider.endpoint = agent_string_view(state->endpoint, strlen(state->endpoint));
    provider.model = agent_string_view(state->model, strlen(state->model));
    provider.authorization = agent_string_view(state->authorization,
                                                strlen(state->authorization));
    provider.use_legacy_max_tokens = true;
    provider.request_buffer = state->request;
    provider.request_capacity = state->request_size;
    provider.response_buffer = state->response;
    provider.response_capacity = state->response_size;
    provider.decoded_buffer = state->decoded;
    provider.decoded_capacity = CONFIG_SMART_HOME_MODEL_DECODED_BUFFER_SIZE;
    provider.response_token_capacity = CONFIG_SMART_HOME_MODEL_JSON_TOKENS;
    provider.max_response_header_bytes = sizeof(backend->response_header_text);
    provider.max_json_depth = AGENT_MAX_JSON_DEPTH;
    ret = agent_openai_provider_init(&state->provider, &provider);
    if (ret != AGENT_OK) goto fail;
    *out = state;
    return AGENT_OK;

fail:
    model_state_destroy(state);
    return ret;
}

static agent_error_t model_complete(void *context,
                                    const agent_model_request_t *request,
                                    const agent_model_sink_t *sink)
{
    smart_home_agent_backend_t *backend = context;
    if (!backend->active || !backend->active->authorization[0])
        return AGENT_ERROR_AUTH;
    int ret;
    backend->diag.stage = "model.encode";
    backend->active->request[0] = '\0';
    ret = agent_openai_model_ops()->complete(&backend->active->provider, request, sink);
    if (ret != AGENT_OK) {
        /* The provider does not expose its failed encoder/decoder's required
         * size. Report observed bytes and each real bound, never a made-up
         * exact requirement for an incomplete response. */
        printf("[agent_diag] turn=%lu stage=%s code=%d request_bytes=%lu request_capacity=%lu response_bytes=%lu response_capacity=%lu decoded_capacity=%u token_capacity=%u required=unknown\n",
               backend->diag.turn, backend->diag.stage, ret,
               (unsigned long)backend->diag.request_bytes,
               (unsigned long)backend->active->request_size,
               (unsigned long)backend->diag.response_bytes,
               (unsigned long)backend->active->response_size,
               CONFIG_SMART_HOME_MODEL_DECODED_BUFFER_SIZE, CONFIG_SMART_HOME_MODEL_JSON_TOKENS);
        smart_home_agent_diag_error(&backend->diag, backend->diag.stage, ret);
    }
    return ret;
}

int smart_home_agent_backend_apply(smart_home_agent_backend_t *backend,
                                   const smart_home_model_config_t *config)
{
    smart_home_model_state_t *replacement;
    smart_home_model_state_t *previous;
    agent_limits_t limits;
    int ret;

    if (!backend || !backend->agent) return AGENT_ERROR_INVALID;
    ret = model_state_create(backend, config, &replacement);
    if (ret != AGENT_OK) return ret;
    limits = backend->limits;
    limits.timeout_ms = (uint32_t)config->timeout_ms;
    limits.per_model_timeout_ms = (uint32_t)config->timeout_ms;
    if (config->max_output_tokens) limits.max_output_tokens = config->max_output_tokens;
    ret = agent_set_limits(backend->agent, &limits);
    if (ret != AGENT_OK) {
        model_state_destroy(replacement);
        return ret;
    }
    previous = backend->active;
    backend->active = replacement;
    backend->limits = limits;
    backend->transport_state.config.request_timeout_ms = (uint32_t)config->timeout_ms;
    model_state_destroy(previous);
    return AGENT_OK;
}

int smart_home_agent_backend_create(smart_home_agent_backend_t **out,
                                    agent_t **agent,
                                    const char *system_prompt,
                                    const smart_home_model_config_t *config)
{
    smart_home_agent_backend_t *backend = NULL;
    agent_port_rtthread_transport_config_t transport = {0};
    agent_config_t core = agent_config_default();
    const agent_model_ops_t model = {.complete = model_complete};
    const char *stage = "config";
    int ret;

    if (!out || !agent || !system_prompt) return AGENT_ERROR_INVALID;
    *out = NULL;
    *agent = NULL;
    ret = validate_config(config);
    if (ret != AGENT_OK) goto fail;
    smart_home_agent_diag_profile();
    stage = "allocation";
    backend = smart_home_bulk_alloc(sizeof(*backend));
    if (!backend) {
        ret = AGENT_ERROR_NOMEM;
        goto fail;
    }
    memset(backend, 0, sizeof(*backend));
    core.limits.max_history_turns = SCA_SESSION_TURNS_PROJECTED;
    backend->limits = core.limits;
    stage = "runtime";
    ret = agent_port_rtthread_runtime_init(&backend->runtime);
    if (ret != AGENT_OK) goto fail;
    /* TLS 由 port/sca_http 内部处理，本层不再持有 tls 上下文。
     * 只需 runtime、超时与 URL 复用缓冲。 */
    transport.runtime               = backend->runtime;
    transport.request_timeout_ms    = (uint32_t)config->timeout_ms;
    transport.url_buffer            = backend->url;
    transport.url_buffer_size       = sizeof(backend->url);
    transport.response_header_buffer      = backend->response_header_text;
    transport.response_header_buffer_size = sizeof(backend->response_header_text);
    stage = "transport";
    ret = agent_port_rtthread_transport_init(&backend->raw_transport,
                                              &backend->transport_state, &transport);
    if (ret != AGENT_OK) goto fail;
    backend->transport.ops = &diagnostic_transport_ops;
    backend->transport.context = backend;
    core.runtime = backend->runtime;
    core.system_prompt = agent_string_view(system_prompt, strlen(system_prompt));
    stage = "core";
    ret = agent_init(&backend->agent, &backend->core_storage, &core);
    if (ret != AGENT_OK) goto fail;
    stage = "session";
    {
        agent_session_storage_t storage;
#if SCA_ENABLE_SESSION_PERSIST
        ret = smart_home_session_open(&backend->persistent, &storage, &backend->diag);
#else
        const agent_session_ram_config_t ram = {
            backend->turns, SCA_SESSION_TURNS_PROJECTED + 1u,
            backend->messages, sizeof(backend->messages) / sizeof(backend->messages[0]),
            backend->calls, sizeof(backend->calls) / sizeof(backend->calls[0]),
            backend->session_payload, sizeof(backend->session_payload),
            backend->read_messages, AGENT_MAX_PROJECTED_MESSAGES,
            backend->read_calls, AGENT_MAX_PROJECTED_MESSAGES
        };
        ret = agent_session_ram_init(&backend->session, &ram);
        if (ret == AGENT_OK) ret = agent_session_ram_bind(&backend->session, &storage);
#endif
        if (ret == AGENT_OK) ret = agent_set_session_storage(backend->agent, &storage);
        if (ret != AGENT_OK) goto fail;
    }
    stage = "model-bind";
    ret = agent_model_init(&backend->model, &backend->model_storage,
                           &model, backend);
    if (ret != AGENT_OK) goto fail;
    ret = agent_set_model(backend->agent, backend->model);
    if (ret != AGENT_OK) goto fail;
    stage = "model-config";
    ret = smart_home_agent_backend_apply(backend, config);
    if (ret != AGENT_OK) goto fail;
    *agent = backend->agent;
    *out = backend;
    return AGENT_OK;

fail:
    printf("[smart_home_init] stage=backend-%s ret=%d (%s)\n",
           stage, ret, agent_error_str((agent_error_t)ret));
    smart_home_agent_backend_destroy(&backend);
    return ret;
}

void smart_home_agent_backend_destroy(smart_home_agent_backend_t **ptr)
{
    smart_home_agent_backend_t *backend;
    if (!ptr || !(backend = *ptr)) return;
    agent_destroy(backend->agent);
    agent_model_destroy(backend->model);
    model_state_destroy(backend->active);
#if SCA_ENABLE_SESSION_PERSIST
    smart_home_session_close(&backend->persistent);
#endif
    secure_clear(backend, sizeof(*backend));
    smart_home_bulk_free(backend);
    *ptr = NULL;
}

int smart_home_agent_backend_prepare_turn(smart_home_agent_backend_t *backend)
{
    if (!backend) return AGENT_ERROR_INVALID;
    backend->diag.stage = "session.prepare";
#if SCA_ENABLE_SESSION_PERSIST
    int ret = smart_home_session_prepare(backend->persistent);
    smart_home_agent_diag_error(&backend->diag, "session.prepare", ret);
    return ret;
#else
    size_t keep = SCA_SESSION_TURNS_PROJECTED;
    int ret;
    if (!backend) return AGENT_ERROR_INVALID;
    ret = agent_session_ram_trim(&backend->session, keep);
    while (ret == AGENT_OK && keep &&
           backend->session.config.payload_capacity - backend->session.payload_used <
           SCA_SESSION_BYTES / 4u) {
        ret = agent_session_ram_trim(&backend->session, --keep);
    }
    return ret;
#endif
}

int smart_home_agent_backend_visit_history(smart_home_agent_backend_t *backend,
                                            agent_session_visit_fn visit, void *context)
{
    if (!backend || !visit) return AGENT_ERROR_INVALID;
#if SCA_ENABLE_SESSION_PERSIST
    return smart_home_session_visit(backend->persistent, visit, context);
#else
    agent_session_storage_t storage;
    int ret = agent_session_ram_bind(&backend->session, &storage);
    return ret == AGENT_OK ? storage.ops.recent(storage.context,
        agent_string_view("home", 4u), SCA_SESSION_TURNS, visit, context) : ret;
#endif
}

void smart_home_agent_backend_begin_turn(smart_home_agent_backend_t *backend)
{
    if (!backend) return;
    smart_home_agent_diag_begin(&backend->diag);
    backend->diag.stage = "inventory.prepare";
}

void smart_home_agent_backend_observe(smart_home_agent_backend_t *backend,
                                      const agent_event_t *event)
{
    if (backend) smart_home_agent_diag_observe(&backend->diag, event);
}

void smart_home_agent_backend_log_result(smart_home_agent_backend_t *backend,
                                         const agent_response_t *response, int code)
{
    if (backend) smart_home_agent_diag_result(&backend->diag, response, code);
}
