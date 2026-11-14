/* Exercise the real service queue/dispatch and registered Agent adapter.
 * Include the service to drive its private worker boundary deterministically;
 * only OS scheduling and HTTP are mocked, never control decisions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <agent.h>

/* Declared before the service source so tcc does not imply int strtok_r. */
int settimeofday(const struct timeval *tv, const void *tz);
char *strtok_r(char *str, const char *delim, char **save);

#include "smart_home/agent/smart_home_agent.h"
#include "smart_home/miloco/smart_home_miloco_service.c"

/* tcc ships no <assert.h>; keep the same fail-fast behaviour locally. */
#define CHECK(x) do { if (!(x)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static smart_home_miloco_t service;
static smart_home_agent_app_t app;
static agent_tool_t control_tool;
static uint64_t now_ms;
static unsigned posts, sleeps;
static int post_mode, get_mode, pump_mode;
static bool cancelled;
static smart_home_miloco_control_report_t during_post;
static char last_body[128], output[1024];
static uint32_t last_id;

int sca_mutex_lock(sca_mutex_t *m) { CHECK(!m->magic); m->magic = 1; return 0; }
void sca_mutex_unlock(sca_mutex_t *m) { CHECK(m->magic); m->magic = 0; }
void sca_sem_post(sca_sem_t *s) { (void)s; }
uint64_t sca_time_ms(void) { return now_ms; }
void sca_log(int level, const char *tag, const char *fmt, ...)
{ (void)level; (void)tag; (void)fmt; }
void *smart_home_bulk_alloc(size_t size) { return malloc(size); }
void smart_home_bulk_free(void *p) { free(p); }
bool agent_cancel_token_is_set(const agent_cancel_token_t *token)
{ (void)token; return cancelled; }
agent_error_t agent_register_tool(agent_t *a, const agent_tool_t *t)
{ (void)a; if (t->name.size == 19 && !memcmp(t->name.data, "miot_device_control", 19)) control_tool = *t; return AGENT_OK; }
/* The service compiles start/stop even though this test drives its queue
 * directly; these symbols must resolve without spawning scheduler state. */
sca_thread_t *sca_thread_create(const sca_thread_attr_t *attr)
{ (void)attr; return NULL; }
int sca_thread_join(sca_thread_t *t, uint32_t timeout_ms) { (void)t; (void)timeout_ms; return 0; }
void sca_thread_destroy(sca_thread_t *t) { (void)t; }
void sca_sem_destroy(sca_sem_t *s) { (void)s; }
int sca_sem_init(sca_sem_t *s, const char *name, uint32_t initial)
{ (void)name; (void)initial; s->magic = 1; return 0; }
int sca_sem_wait(sca_sem_t *s, uint32_t timeout_ms) { (void)s; (void)timeout_ms; return 0; }
int sca_mutex_init(sca_mutex_t *m, const char *name) { (void)name; m->magic = 0; return 0; }
void sca_mutex_destroy(sca_mutex_t *m) { (void)m; }
int settimeofday(const struct timeval *tv, const void *tz) { (void)tv; (void)tz; return 0; }
int gettimeofday(struct timeval *tv, void *tz)
{ (void)tz; if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; } return 0; }
int smart_home_miloco_http_get_date(const smart_home_miloco_client_config_t *config,
    const char *path, char *response, size_t response_size,
    int *http_status_out, time_t *date_out)
{
    (void)config; (void)path; (void)response; (void)response_size;
    (void)http_status_out; (void)date_out;
    return -1;
}
int smart_home_secrets_set_miloco(const char *host, uint16_t port, const char *token)
{ (void)host; (void)port; (void)token; return 0; }
/* msvcrt has no strtok_r; parse-only helper for the weather reply. */
char *strtok_r(char *str, const char *delim, char **save)
{
    char *start;
    if (!str) str = *save;
    if (!str) return NULL;
    str += strspn(str, delim);
    if (!*str) { *save = str; return NULL; }
    start = str;
    str += strcspn(str, delim);
    if (*str) { *str = '\0'; *save = str + 1; } else *save = str;
    return start;
}
/* msvcrt has no C99 trunc(); the test only feeds int32 JSON numbers. */
double trunc(double value) { return (double)(long long)value; }

static void dispatch(void)
{
    miloco_control_request_t request;
    char body[2048];
    lock_state(&service);
    bool found = take_control_locked(&service, &request);
    unlock_state(&service);
    if (found) { last_id = request.request_id; process_control(&service, &request, body, sizeof(body)); }
}

void sca_sleep_ms(uint32_t ms)
{
    now_ms += ms; ++sleeps;
    if (pump_mode == 1 && sleeps == 2) dispatch();
    if (pump_mode == 2) cancelled = true;
}

int smart_home_miloco_http_post(const smart_home_miloco_client_config_t *config,
    const char *path, const char *body, char *out, size_t cap, int *status)
{
    (void)config;
    CHECK(!service.lock.magic);
    CHECK(strstr(path, "/cam/control"));
    snprintf(last_body, sizeof(last_body), "%s", body);
    ++posts;
    /* Before HTTP finishes, neither an old cached value nor an in-flight
     * operation is sufficient to return tool success. */
    smart_home_miloco_control_poll(&service, last_id, &during_post);
    CHECK(!during_post.terminal);
    if (post_mode == 1) return -5;
    *status = post_mode == 4 ? 401 : 200;
    if (post_mode == 6) now_ms += SMART_HOME_MILOCO_CONTROL_WAIT_MS;
    if (post_mode == 5) {
        now_ms += SMART_HOME_MILOCO_CONTROL_WAIT_MS;
        smart_home_miloco_control_poll(&service, last_id, &during_post);
        CHECK(during_post.terminal && during_post.state == SMART_HOME_MILOCO_CONTROL_UNCONFIRMED);
    }
    if (post_mode == 2) snprintf(out, cap, "{\"code\":0,\"data\":{}}");
    else if (strstr(body, "call_action"))
        snprintf(out, cap, "{\"code\":0,\"data\":{\"result\":{\"did\":\"cam\",\"iid\":\"action.2.1\",\"code\":%d}}}", post_mode == 3 ? -123 : 0);
    else snprintf(out, cap, "{\"code\":0,\"data\":{\"results\":[{\"did\":\"cam\",\"iid\":\"prop.2.1\",\"code\":0}]}}");
    return 0;
}

int smart_home_miloco_http_get(const smart_home_miloco_client_config_t *config,
    const char *path, char *out, size_t cap, int *status)
{
    (void)config; CHECK(strstr(path, "/status?iid="));
    if (get_mode == 1) return -5;
    *status = 200;
    snprintf(out, cap, "{\"code\":0,\"data\":{\"properties\":[{\"iid\":\"prop.2.1\",\"code\":0,\"value\":true}]}}");
    return 0;
}

static void reset(void)
{
    memset(&service, 0, sizeof(service)); memset(&app, 0, sizeof(app));
    service.reachable = true; service.device_count = 1;
    smart_home_miloco_device_t *d = &service.devices[0];
    strcpy(d->did, "cam"); d->online = true; d->control_count = 2;
    strcpy(d->controls[0].iid, "action.2.1"); d->controls[0].type = SMART_HOME_MILOCO_CTRL_ACTION;
    strcpy(d->controls[1].iid, "prop.2.1"); d->controls[1].type = SMART_HOME_MILOCO_CTRL_BOOL;
    strcpy(d->controls[1].type_name, "on");
    d->controls[1].value_valid = true; d->controls[1].value = 1; d->controls[1].observation = 7;
    app.miloco = &service;
    posts = sleeps = 0; post_mode = get_mode = pump_mode = 0; cancelled = false;
    now_ms = 1000; output[0] = last_body[0] = 0;
    CHECK(smart_home_miloco_tools_register((agent_t *)&app, &app) == AGENT_OK);
    CHECK(control_tool.execute);
}

static uint32_t begin(const char *iid, const char *op, int value, uint64_t deadline)
{
    uint32_t id; smart_home_miloco_control_report_t r;
    CHECK(smart_home_miloco_control_begin(&service, "cam", iid, op, value,
        deadline, "card", &id, &r) == AGENT_OK);
    CHECK(id && !r.terminal && !r.dispatched);
    return id;
}

static smart_home_miloco_control_report_t poll_id(uint32_t id)
{ smart_home_miloco_control_report_t r; smart_home_miloco_control_poll(&service, id, &r); return r; }

static agent_error_t sink_write(void *ctx, agent_string_view_t text)
{
    (void)ctx; CHECK(text.size < sizeof(output));
    memcpy(output, text.data, text.size); output[text.size] = 0; return AGENT_OK;
}
static int agent_action(uint64_t deadline)
{
    const char *args = "{\"device_key\":\"milo:cam\",\"did\":\"cam\",\"iid\":\"action.2.1\",\"operation\":\"action\"}";
    agent_tool_context_t ctx = {0};
    ctx.call.arguments_json = agent_string_view(args, strlen(args));
    ctx.call.id = agent_string_view("test-call", 9); ctx.deadline_ms = deadline;
    agent_text_sink_t sink = {sink_write, NULL};
    return control_tool.execute(control_tool.user_data, &ctx, &sink);
}

int main(void)
{
    uint32_t id, other; smart_home_miloco_control_report_t r;
    reset();
    id = begin("action.2.1", "action", 0, 0);
    CHECK(smart_home_miloco_control_begin(&service, "cam", "action.2.1", "action", 0,
        0, "agent", &other, &r) == AGENT_ERROR_BUSY && other == 0);
    CHECK(!poll_id(id).terminal && posts == 0);
    dispatch(); r = poll_id(id);
    CHECK(posts == 1 && r.terminal && r.code == AGENT_OK);
    CHECK(!strcmp(r.reason, "gateway_confirmed_action"));
    smart_home_miloco_control_release(&service, id);
    /* Card and registered Agent tool produce exactly the same HTTP body. */
    char card_body[128]; strcpy(card_body, last_body);
    pump_mode = 1;
    CHECK(agent_action(0) == AGENT_OK && sleeps >= 2 && posts == 2);
    CHECK(!strcmp(card_body, last_body));
    CHECK(strstr(output, "\"state\":\"confirmed\"") && strstr(output, "gateway_confirmed_action"));
    CHECK(!service.results[1].retained);

    reset(); post_mode = 1; pump_mode = 1;
    CHECK(agent_action(0) == AGENT_ERROR_TOOL_FAILED && posts == 1);
    CHECK(strstr(output, "\"ok\":false") && strstr(output, "unconfirmed"));
    reset(); post_mode = 2; pump_mode = 1;
    CHECK(agent_action(0) == AGENT_ERROR_TOOL_FAILED && posts == 1);
    reset(); post_mode = 3; pump_mode = 1;
    CHECK(agent_action(0) == AGENT_ERROR_TOOL_FAILED && strstr(output, "gateway_rejected"));
    reset(); post_mode = 4; pump_mode = 1;
    CHECK(agent_action(0) == AGENT_ERROR_TOOL_FAILED && posts == 1);

    /* Queued deadlines and cancellation remove commands: no late actuation. */
    reset(); CHECK(agent_action(now_ms + 35) == AGENT_ERROR_TIMEOUT);
    CHECK(now_ms == 1035 && service.pending_count == 0); dispatch(); CHECK(!posts);
    reset(); CHECK(agent_action(now_ms - 1) == AGENT_ERROR_TIMEOUT && !service.pending_count);
    reset(); pump_mode = 2;
    CHECK(agent_action(0) == AGENT_ERROR_CANCELLED && !service.pending_count); dispatch(); CHECK(!posts);
    reset(); cancelled = true;
    CHECK(agent_action(0) == AGENT_ERROR_CANCELLED && !service.pending_count);
    reset(); id = begin("action.2.1", "action", 0, 0);
    smart_home_miloco_control_release(&service, id); dispatch(); CHECK(!posts);

    /* Cancellation after dequeue, before POST, also prevents late sending. */
    reset(); id = begin("action.2.1", "action", 0, 0);
    miloco_control_request_t req;
    lock_state(&service); CHECK(take_control_locked(&service, &req)); unlock_state(&service);
    smart_home_miloco_control_cancel(&service, id, &r);
    char body[2048]; process_control(&service, &req, body, sizeof(body)); CHECK(!posts);
    smart_home_miloco_control_release(&service, id);

    /* Timeout during HTTP remains unknown even when a late success arrives. */
    reset(); post_mode = 5; id = begin("action.2.1", "action", 0, 0); dispatch(); r = poll_id(id);
    CHECK(posts == 1 && r.state == SMART_HOME_MILOCO_CONTROL_UNCONFIRMED && r.code != 0);
    smart_home_miloco_control_release(&service, id);

    reset(); post_mode = 6; id = begin("action.2.1", "action", 0, 0); dispatch();
    CHECK(poll_id(id).state == SMART_HOME_MILOCO_CONTROL_UNCONFIRMED);
    reset(); id = begin("action.2.1", "action", 0, now_ms + 10);
    now_ms += 10; dispatch();
    CHECK(!posts && poll_id(id).code == AGENT_ERROR_TIMEOUT);
    reset(); id = begin("action.2.1", "action", 0, 0); ++service.control_generation;
    dispatch(); r = poll_id(id);
    CHECK(!posts && !strcmp(r.reason, "gateway_changed_before_dispatch"));

    /* Cached target must not confirm a write without a new readback. */
    reset(); get_mode = 1; id = begin("prop.2.1", "set", 1, 0); dispatch();
    CHECK(!poll_id(id).terminal && !service.devices[0].controls[1].value_valid);
    now_ms += SMART_HOME_MILOCO_CONTROL_WAIT_MS; r = poll_id(id);
    CHECK(r.state == SMART_HOME_MILOCO_CONTROL_UNCONFIRMED);
    smart_home_miloco_control_release(&service, id);
    reset(); id = begin("prop.2.1", "set", 1, 0); dispatch(); r = poll_id(id);
    CHECK(r.code == 0 && !strcmp(r.reason, "device_state_confirmed"));
    reset(); post_mode = 1; id = begin("prop.2.1", "set", 1, 0); dispatch(); r = poll_id(id);
    CHECK(r.code == 0 && !strcmp(r.reason, "current_state_confirmed"));
    reset(); service.devices[0].controls[1].write_only = true;
    id = begin("prop.2.1", "set", 1, 0); dispatch(); CHECK(!strcmp(poll_id(id).reason, "gateway_accepted_write"));

    /* Local admission and dispatch revalidation share the same reasons. */
    reset(); service.reachable = false;
    CHECK(agent_action(0) == AGENT_ERROR_IO && strstr(output, "gateway_unreachable") && !posts);
    reset(); service.devices[0].online = false;
    CHECK(agent_action(0) == AGENT_ERROR_IO && strstr(output, "device_offline"));
    reset(); service.device_count = 0;
    CHECK(agent_action(0) == AGENT_ERROR_NOT_FOUND && strstr(output, "device_not_found"));
    reset(); id = begin("action.2.1", "action", 0, 0);
    service.devices[0].controls[0].type = SMART_HOME_MILOCO_CTRL_BOOL;
    dispatch(); r = poll_id(id);
    CHECK(!posts && r.code == AGENT_ERROR_TOOL_ARGUMENT && !strcmp(r.reason, "unsupported_operation_or_value"));
    reset();
    CHECK(smart_home_miloco_control_begin(&service, "cam", "prop.2.1", "set", 2, 0,
        "card", &id, &r) == AGENT_ERROR_TOOL_ARGUMENT && !service.pending_count);

    /* Retained results survive churn of legacy consumers; admission cannot
     * overwrite a queued, in-flight or retained request. */
    reset(); id = begin("action.2.1", "action", 0, 0); dispatch();
    for (unsigned i = 0; i < 20; ++i) {
        CHECK(smart_home_miloco_submit_control(&service, "cam", "prop.2.1", "set", 1) == 0);
        dispatch(); CHECK(poll_id(id).code == 0);
    }
    smart_home_miloco_control_release(&service, id);
    reset();
    service.devices[0].control_count = 8;
    uint32_t ids[8];
    for (unsigned i = 0; i < 8; ++i) {
        snprintf(service.devices[0].controls[i].iid, 14, "action.2.%u", i + 1);
        service.devices[0].controls[i].type = SMART_HOME_MILOCO_CTRL_ACTION;
        ids[i] = begin(service.devices[0].controls[i].iid, "action", 0, 0);
    }
    CHECK(service.pending_count == 8);
    service.devices[0].control_count = 9;
    strcpy(service.devices[0].controls[8].iid, "action.2.9");
    service.devices[0].controls[8].type = SMART_HOME_MILOCO_CTRL_ACTION;
    CHECK(smart_home_miloco_control_begin(&service, "cam", "action.2.9", "action", 0,
        0, "card", &other, &r) == AGENT_ERROR_LIMIT && other == 0);
    for (unsigned i = 0; i < 8; ++i) smart_home_miloco_control_release(&service, ids[i]);
    CHECK(!service.pending_count); dispatch(); CHECK(!posts);
    puts("test_miloco_control: card/Agent parity, real dispatch, failure receipts, deadlines, cancellation, readback and retained results passed");
    return 0;
}
