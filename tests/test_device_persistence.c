/* Host test: event coalescing, latest snapshots, failures and demo reset. */
#include "smart_home/device/smart_home_device_persistence.h"
#include <agent/context.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static smart_home_state_t g_saved;
static int g_save_count;
static int g_save_result;

int sca_mutex_init(sca_mutex_t *m, const char *name)
{ (void)name; memset(m, 0, sizeof(*m)); m->magic = 1; return SCA_OK; }
void sca_mutex_destroy(sca_mutex_t *m) { memset(m, 0, sizeof(*m)); }
int sca_mutex_lock(sca_mutex_t *m) { (void)m; return SCA_OK; }
int sca_mutex_trylock(sca_mutex_t *m) { (void)m; return SCA_OK; }
void sca_mutex_unlock(sca_mutex_t *m) { (void)m; }
int sca_mutex_ready(const sca_mutex_t *m) { return m && m->magic != 0; }
uint64_t sca_time_ms(void) { static uint64_t now; return ++now; }
int sca_sem_init(sca_sem_t *s, const char *name, uint32_t initial)
{ (void)name; memset(s, 0, sizeof(*s)); s->magic = initial + 1; return SCA_OK; }
void sca_sem_destroy(sca_sem_t *s) { memset(s, 0, sizeof(*s)); }
int sca_sem_wait(sca_sem_t *s, uint32_t timeout_ms)
{ (void)s; return timeout_ms ? SCA_ERR_TIMEOUT : SCA_OK; }
int sca_sem_trywait(sca_sem_t *s) { (void)s; return SCA_ERR_TIMEOUT; }
void sca_sem_post(sca_sem_t *s) { (void)s; }
struct sca_thread { int unused; };
sca_thread_t *sca_thread_create(const sca_thread_attr_t *attr)
{ (void)attr; return NULL; }
int sca_thread_join(sca_thread_t *t, uint32_t timeout_ms)
{ (void)t; (void)timeout_ms; return SCA_OK; }
void sca_thread_destroy(sca_thread_t *t) { (void)t; }
void sca_log(int level, const char *tag, const char *fmt, ...)
{ (void)level; (void)tag; (void)fmt; }
int smart_home_config_state_save(const cJSON *root)
{ (void)root; return AGENT_ERROR_STATE; }
agent_error_t agent_register_context(agent_t *agent,
                                     const agent_context_provider_t *provider)
{ (void)agent; (void)provider; return AGENT_OK; }

static int fake_save(const smart_home_state_t *state, void *user_data)
{
    (void)user_data;
    g_saved = *state;
    g_save_count++;
    return g_save_result;
}

int main(void)
{
    smart_home_state_t state;
    smart_home_device_service_t service;
    smart_home_device_persistence_t persistence;
    smart_home_device_persistence_status_t status;
    smart_home_device_t light;
    uint32_t revision;

    smart_home_device_init(&state);
    CHECK(smart_home_device_service_init(&service, &state) == AGENT_OK);
    CHECK(smart_home_device_persistence_init(&persistence, &service,
                                             fake_save, NULL) == AGENT_OK);
    CHECK(smart_home_device_service_set_light(&service, "living_room", 1, 30) == AGENT_OK);
    CHECK(smart_home_device_service_set_light(&service, "living_room", 1, 72) == AGENT_OK);
    CHECK(smart_home_device_service_set_environment(&service, 24, 50, 200) == AGENT_OK);
    CHECK(g_save_count == 0);
    revision = smart_home_device_service_revision(&service);
    CHECK(revision == 3u);

    CHECK(smart_home_device_persistence_flush(&persistence, 0) == AGENT_OK);
    CHECK(g_save_count == 1);
    CHECK(smart_home_device_find_first(&g_saved, "living_room",
                                       SMART_HOME_DEVICE_LIGHT) != NULL);
    CHECK(smart_home_device_find_first(&g_saved, "living_room",
                                       SMART_HOME_DEVICE_LIGHT)->brightness == 72);
    CHECK(g_saved.env_temperature == 24 && g_saved.env_humidity == 50 &&
          g_saved.env_light == 200);
    smart_home_device_persistence_status(&persistence, &status);
    CHECK(!status.busy && status.saved_revision == revision && status.save_count == 1u);

    g_save_result = AGENT_ERROR_IO;
    CHECK(smart_home_device_persistence_request_save(&persistence) == AGENT_OK);
    CHECK(smart_home_device_persistence_flush(&persistence, 0) == AGENT_ERROR_IO);
    CHECK(g_save_count == 2);
    CHECK(smart_home_device_service_find_first(&service, "living_room",
                                               SMART_HOME_DEVICE_LIGHT, &light) == AGENT_OK);
    CHECK(light.on && light.brightness == 72);
    smart_home_device_persistence_status(&persistence, &status);
    CHECK(!status.busy && status.last_result == AGENT_ERROR_IO);

    g_save_result = AGENT_OK;
    CHECK(smart_home_device_persistence_request_save(&persistence) == AGENT_OK);
    CHECK(smart_home_device_persistence_flush(&persistence, 0) == AGENT_OK);
    CHECK(g_save_count == 3);

    CHECK(smart_home_device_service_reset(&service) == AGENT_OK);
    CHECK(smart_home_device_persistence_flush(&persistence, 0) == AGENT_OK);
    CHECK(g_save_count == 4);
    CHECK(smart_home_device_find_first(&g_saved, "living_room",
                                       SMART_HOME_DEVICE_LIGHT) != NULL);
    CHECK(!smart_home_device_find_first(&g_saved, "living_room",
                                       SMART_HOME_DEVICE_LIGHT)->on);
    CHECK(smart_home_device_find_first(&g_saved, "living_room",
                                       SMART_HOME_DEVICE_LIGHT)->brightness == 0);
    CHECK(g_saved.env_temperature == 26 && g_saved.env_humidity == 45 &&
          g_saved.env_light == 300);

    smart_home_device_persistence_stop(&persistence);
    smart_home_device_service_deinit(&service);
    printf("test_device_persistence: coalescing, latest state, failure and reset passed\n");
    return 0;
}
