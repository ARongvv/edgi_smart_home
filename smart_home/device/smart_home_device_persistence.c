/* SPDX-License-Identifier: Apache-2.0 */
#include "smart_home/device/smart_home_device_persistence.h"
#include "smart_home/config/smart_home_config_store.h"

#include <string.h>

#define DEVICE_SAVE_DEBOUNCE_MS 300u
#define DBG_TAG "device-save"

static int save_state_json(const smart_home_state_t *state, void *user_data)
{
    cJSON *root;
    int ret;

    (void)user_data;
    root = smart_home_device_state_to_json(state);
    if (!root) return AGENT_ERROR_NOMEM;
    ret = smart_home_config_state_save(root);
    cJSON_Delete(root);
    return ret;
}

static int request_revision(smart_home_device_persistence_t *persistence,
                            uint32_t revision, uint32_t *generation)
{
    if (!persistence || !persistence->initialized) return AGENT_ERROR_INVALID;
    sca_mutex_lock(&persistence->mutex);
    if (persistence->stopping) {
        sca_mutex_unlock(&persistence->mutex);
        return AGENT_ERROR_STATE;
    }
    persistence->pending = 1;
    persistence->pending_revision = revision;
    persistence->request_generation++;
    if (generation) *generation = persistence->request_generation;
    sca_mutex_unlock(&persistence->mutex);
    sca_sem_post(&persistence->wake);
    return AGENT_OK;
}

static void device_changed(const smart_home_device_event_t *event, void *user_data)
{
    if (event) (void)request_revision(user_data, event->revision, NULL);
}

static int write_latest(smart_home_device_persistence_t *persistence)
{
    smart_home_state_t state;
    uint32_t revision;
    uint32_t generation;
    uint32_t current_revision;
    int ret;

    ret = smart_home_device_service_copy_state_with_revision(
        persistence->service, &state, &revision);
    if (ret != AGENT_OK) return ret;

    sca_mutex_lock(&persistence->mutex);
    generation = persistence->request_generation;
    persistence->saving = 1;
    sca_mutex_unlock(&persistence->mutex);

    ret = persistence->save(&state, persistence->save_user_data);
    current_revision = smart_home_device_service_revision(persistence->service);

    sca_mutex_lock(&persistence->mutex);
    persistence->saving = 0;
    persistence->attempted_revision = revision;
    persistence->last_result = ret;
    persistence->save_count++;
    persistence->completed_generation = generation;
    if (ret == AGENT_OK) persistence->saved_revision = revision;
    if (persistence->request_generation == generation) persistence->pending = 0;
    if (current_revision != revision) {
        persistence->pending = 1;
        persistence->pending_revision = current_revision;
    }
    sca_mutex_unlock(&persistence->mutex);
    sca_sem_post(&persistence->idle);
    if (ret != AGENT_OK)
        SCA_LOGW(DBG_TAG, "state save failed (%d); RAM state retained", ret);
    return ret;
}

static void persistence_worker(void *argument)
{
    smart_home_device_persistence_t *persistence = argument;

    for (;;) {
        int wait_result;
        int stopping;
        int pending;

        sca_sem_wait(&persistence->wake, 0);
        do {
            sca_mutex_lock(&persistence->mutex);
            stopping = persistence->stopping;
            sca_mutex_unlock(&persistence->mutex);
            if (stopping) break;
            wait_result = sca_sem_wait(&persistence->wake, DEVICE_SAVE_DEBOUNCE_MS);
        } while (wait_result != SCA_ERR_TIMEOUT);
        if (stopping) break;

        (void)write_latest(persistence);
        sca_mutex_lock(&persistence->mutex);
        pending = persistence->pending;
        sca_mutex_unlock(&persistence->mutex);
        if (pending) sca_sem_post(&persistence->wake);
    }
}

int smart_home_device_persistence_init(smart_home_device_persistence_t *persistence,
                                       smart_home_device_service_t *service,
                                       smart_home_device_save_fn save,
                                       void *user_data)
{
    if (!persistence || !service || !service->mutex_initialized)
        return AGENT_ERROR_INVALID;
    memset(persistence, 0, sizeof(*persistence));
    persistence->service = service;
    persistence->save = save ? save : save_state_json;
    persistence->save_user_data = user_data;
    persistence->last_result = AGENT_OK;
    if (sca_mutex_init(&persistence->mutex, "devsave") != SCA_OK)
        return AGENT_ERROR;
    if (sca_sem_init(&persistence->wake, "devsave", 0) != SCA_OK) {
        sca_mutex_destroy(&persistence->mutex);
        memset(persistence, 0, sizeof(*persistence));
        return AGENT_ERROR;
    }
    if (sca_sem_init(&persistence->idle, "devsave", 0) != SCA_OK) {
        sca_sem_destroy(&persistence->wake);
        sca_mutex_destroy(&persistence->mutex);
        memset(persistence, 0, sizeof(*persistence));
        return AGENT_ERROR;
    }
    persistence->initialized = 1;
    smart_home_device_service_set_event_listener(service, device_changed, persistence);
    return AGENT_OK;
}

int smart_home_device_persistence_start(smart_home_device_persistence_t *persistence)
{
    sca_thread_attr_t attr;

    if (!persistence || !persistence->initialized) return AGENT_ERROR_INVALID;
    if (persistence->worker) return AGENT_OK;
    memset(&attr, 0, sizeof(attr));
    attr.name = "device-save";
    attr.fn = persistence_worker;
    attr.arg = persistence;
    attr.stack_bytes = 16384;
    attr.priority = 12;
    attr.cpu = 0xFF;
    persistence->worker = sca_thread_create(&attr);
    return persistence->worker ? AGENT_OK : AGENT_ERROR;
}

int smart_home_device_persistence_request_save(
    smart_home_device_persistence_t *persistence)
{
    if (!persistence || !persistence->service) return AGENT_ERROR_INVALID;
    return request_revision(persistence,
        smart_home_device_service_revision(persistence->service), NULL);
}

int smart_home_device_persistence_flush(smart_home_device_persistence_t *persistence,
                                        uint32_t timeout_ms)
{
    uint32_t generation;
    uint32_t revision;
    uint64_t deadline;
    int ret;

    if (!persistence || !persistence->initialized) return AGENT_ERROR_INVALID;
    revision = smart_home_device_service_revision(persistence->service);
    ret = request_revision(persistence, revision, &generation);
    if (ret != AGENT_OK) return ret;
    if (!persistence->worker) return write_latest(persistence);

    deadline = sca_time_ms() + timeout_ms;
    for (;;) {
        int completed;
        int saving;
        int result;
        uint32_t remaining = 0;

        sca_mutex_lock(&persistence->mutex);
        completed = (int32_t)(persistence->completed_generation - generation) >= 0;
        saving = persistence->saving;
        result = persistence->last_result;
        sca_mutex_unlock(&persistence->mutex);
        if (completed && !saving) return result;
        if (timeout_ms != UINT32_MAX) {
            uint64_t now = sca_time_ms();
            if (now >= deadline) return AGENT_ERROR_TIMEOUT;
            remaining = (uint32_t)(deadline - now);
            if (remaining > 100u) remaining = 100u;
        }
        sca_sem_wait(&persistence->idle, remaining);
    }
}

void smart_home_device_persistence_status(
    smart_home_device_persistence_t *persistence,
    smart_home_device_persistence_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    status->last_result = AGENT_ERROR_STATE;
    if (!persistence || !persistence->initialized) return;
    sca_mutex_lock(&persistence->mutex);
    status->pending_revision = persistence->pending_revision;
    status->saved_revision = persistence->saved_revision;
    status->save_count = persistence->save_count;
    status->last_result = persistence->last_result;
    status->busy = persistence->pending || persistence->saving;
    sca_mutex_unlock(&persistence->mutex);
}

void smart_home_device_persistence_stop(smart_home_device_persistence_t *persistence)
{
    if (!persistence || !persistence->initialized) return;
    smart_home_device_service_set_event_listener(persistence->service, NULL, NULL);
    (void)smart_home_device_persistence_flush(persistence, 3000);
    sca_mutex_lock(&persistence->mutex);
    persistence->stopping = 1;
    sca_mutex_unlock(&persistence->mutex);
    sca_sem_post(&persistence->wake);
    if (persistence->worker) {
        (void)sca_thread_join(persistence->worker, 0);
        sca_thread_destroy(persistence->worker);
    }
    sca_sem_destroy(&persistence->idle);
    sca_sem_destroy(&persistence->wake);
    sca_mutex_destroy(&persistence->mutex);
    memset(persistence, 0, sizeof(*persistence));
}
