/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SMART_HOME_DEVICE_PERSISTENCE_H
#define SMART_HOME_DEVICE_PERSISTENCE_H

#include "smart_home/device/smart_home_device_service.h"

typedef int (*smart_home_device_save_fn)(const smart_home_state_t *state,
                                         void *user_data);

typedef struct {
    smart_home_device_service_t *service;
    smart_home_device_save_fn save;
    void *save_user_data;
    sca_mutex_t mutex;
    sca_sem_t wake;
    sca_sem_t idle;
    sca_thread_t *worker;
    uint32_t pending_revision;
    uint32_t attempted_revision;
    uint32_t saved_revision;
    uint32_t request_generation;
    uint32_t completed_generation;
    uint32_t save_count;
    int last_result;
    int pending;
    int saving;
    int stopping;
    int initialized;
} smart_home_device_persistence_t;

typedef struct {
    uint32_t pending_revision;
    uint32_t saved_revision;
    uint32_t save_count;
    int last_result;
    int busy;
} smart_home_device_persistence_status_t;

/* A NULL save callback selects the normal state.json store. */
int smart_home_device_persistence_init(smart_home_device_persistence_t *persistence,
                                       smart_home_device_service_t *service,
                                       smart_home_device_save_fn save,
                                       void *user_data);
int smart_home_device_persistence_start(smart_home_device_persistence_t *persistence);
void smart_home_device_persistence_stop(smart_home_device_persistence_t *persistence);

/* Coalesce the newest revision and wake the single writer. */
int smart_home_device_persistence_request_save(
    smart_home_device_persistence_t *persistence);
int smart_home_device_persistence_flush(smart_home_device_persistence_t *persistence,
                                        uint32_t timeout_ms);
void smart_home_device_persistence_status(
    smart_home_device_persistence_t *persistence,
    smart_home_device_persistence_status_t *status);

#endif /* SMART_HOME_DEVICE_PERSISTENCE_H */
