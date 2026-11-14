/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Miloco 网关服务：独立 worker 轮询设备列表并执行电源控制。
 *
 * 数据流：
 *   GET /miot/device_list           → 设备 did/name/room/online（小 JSON）
 *   GET /miot/devices/{did}/spec    → 新设备一次性取 category（小 JSON）
 *   GET /miot/devices/{did}/status?iid=prop.2.1 → 电源态回读
 *   POST /miot/devices/{did}/control → set_property 开关
 *
 * 响应统一为 {code, message, data}；code==0 才视为成功。
 */

#include "smart_home/miloco/smart_home_miloco.h"
#include "smart_home/miloco/smart_home_miloco_client.h"
#include "smart_home/miloco/smart_home_miloco_specscan.h"
#include "smart_home/miloco/smart_home_miloco_protocol.h"

#include "smart_home/core/smart_home_memory.h"
#include "port/sca_json.h"
#include "smart_home/config/smart_home_secrets.h"

#include <agent/error.h>

#include "port/sca_os.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

/* 48 KiB：HTTP 路径（getaddrinfo + 非阻塞 connect + 收包解析 + 17KB
 * spec 的 cJSON 整树解析）的栈深余量。历史教训是 8 KiB 时溢出会踩坏
 * 相邻 PSRAM，症状延迟到后续堆操作（cJSON）才爆；24 KiB 在摄像机
 * spec 解析路径上仍出现过溢出，故提到 48 KiB。栈走 PSRAM bulk 堆，
 * 增量 24 KiB 对 32 MB PSRAM 无压力。 */
#define MILOCO_WORKER_STACK_SIZE   49152u

/* 轮询周期（毫秒）。与 SMART_HOME_MILOCO_POLL_INTERVAL_SEC 一致，
 * 但用毫秒表达以便 sca_sem_wait 直接使用（相对超时）。 */
#define MILOCO_POLL_WAIT_MS  (SMART_HOME_MILOCO_POLL_INTERVAL_SEC * 1000u)
#define MILOCO_CONTROL_QUEUE_DEPTH 8
/* 摄像机等复杂设备的 spec 达 17KB+（实测 17426B），16KB 缓冲会
 * 截断导致解析失败；32KB 走 PSRAM bulk 堆无压力。 */
#define MILOCO_RESPONSE_BYTES      (32u * 1024u)
#define MILOCO_STATUS_RESPONSE_BYTES 512u

typedef struct {
    char did[sizeof(((smart_home_miloco_device_t *)0)->did)];
    char iid[14];
    uint8_t is_action;                   /* 0=set_property 1=call_action */
    int32_t value;
    uint32_t request_id;
    uint32_t dispatch_observation;
} miloco_control_request_t;

typedef struct {
    miloco_control_request_t request;
    uint64_t deadline_ms;
    uint32_t generation;
    bool retained;
    bool complete;
    bool inflight;
    uint8_t control_type;
    bool write_only;
    char source[8];
    smart_home_miloco_control_report_t report;
} miloco_control_result_t;

struct smart_home_miloco {
    smart_home_miloco_client_config_t client_config;
    sca_thread_t *worker;
    bool worker_started;
    bool stop_requested;
    /* worker 栈（PSRAM）：保存指针以便 stop 时释放，避免泄漏。 */
    void *worker_stack;
    size_t worker_stack_bytes;
    /* reconfigure 待生效配置：worker 在循环顶部安全切换，UI 线程
     * 永不 join、永不重建线程。 */
    smart_home_miloco_client_config_t pending_config;
    bool config_dirty;
    /* 天气数据与拉取节奏控制。 */
    smart_home_miloco_weather_t weather;
    volatile uint32_t weather_poll_count;  /* 5s/轮，120轮=10分钟；kick置119 */
    /* 保存请求：worker 代写 secrets.json 后应用配置。 */
    smart_home_miloco_config_t save_config;
    bool save_pending;
    bool save_in_progress;
    bool save_completed;
    int save_result;

    sca_mutex_t lock;
    sca_sem_t wake;

    /* 以下字段由 lock 保护。 */
    miloco_control_request_t pending[MILOCO_CONTROL_QUEUE_DEPTH];
    size_t pending_count;
    uint32_t next_request_id;
    uint32_t control_generation;
    miloco_control_result_t results[MILOCO_CONTROL_QUEUE_DEPTH];
    unsigned result_next;
    smart_home_miloco_device_t devices[SMART_HOME_MILOCO_MAX_DEVICES];
    size_t device_count;
    uint32_t revision;

    /* worker 独占的轮询暂存：避免 19 KB × 3 落在 48 KB 工作线程栈上。
     * 只有 worker 线程访问，不需要加锁。
     */
    smart_home_miloco_device_t poll_parse[SMART_HOME_MILOCO_MAX_DEVICES];
    smart_home_miloco_device_t poll_fresh[SMART_HOME_MILOCO_MAX_DEVICES];
    smart_home_miloco_device_t poll_snapshot[SMART_HOME_MILOCO_MAX_DEVICES];
    bool reachable;
    bool xiaomi_bound;
};

static void lock_state(smart_home_miloco_t *service)
{
    sca_mutex_lock(&service->lock);
}

static void unlock_state(smart_home_miloco_t *service)
{
    sca_mutex_unlock(&service->lock);
}

bool smart_home_miloco_config_valid(const smart_home_miloco_config_t *config)
{
    const unsigned char *p;

    if (!config || config->port == 0) {
        return false;
    }
    /* 空 host 合法：worker 以"仅天气"模式运行，网关端点静默失败。 */
    if (config->host[0] == '\0') {
        return true;
    }
    for (p = (const unsigned char *)config->host; *p; p++) {
        if (*p < 0x20u || *p == 0x7fu) {
            return false;
        }
    }
    return true;
}

size_t smart_home_miloco_list(const smart_home_miloco_t *service,
                              smart_home_miloco_device_t *devices,
                              size_t capacity,
                              uint32_t *revision_out)
{
    size_t count;

    if (!service) {
        if (revision_out) {
            *revision_out = 0;
        }
        return 0;
    }
    lock_state((smart_home_miloco_t *)service);
    count = service->device_count;
    if (devices && capacity > 0) {
        if (count > capacity) {
            count = capacity;
        }
        memcpy(devices, service->devices,
               count * sizeof(devices[0]));
    }
    if (revision_out) {
        *revision_out = service->revision;
    }
    unlock_state((smart_home_miloco_t *)service);
    return count;
}

bool smart_home_miloco_get_config(const smart_home_miloco_t *service,
                                  smart_home_miloco_config_t *out)
{
    bool ok = false;

    if (service && out) {
        lock_state((smart_home_miloco_t *)service);
        snprintf(out->host, sizeof(out->host), "%s",
                 service->client_config.host);
        out->port = service->client_config.port;
        snprintf(out->token, sizeof(out->token), "%s",
                 service->client_config.token);
        ok = service->reachable || service->config_dirty ||
             service->client_config.host[0] != '\0';
        unlock_state((smart_home_miloco_t *)service);
    }
    return ok;
}

bool smart_home_miloco_bound(const smart_home_miloco_t *service)
{
    bool bound = false;

    if (service) {
        lock_state((smart_home_miloco_t *)service);
        bound = service->reachable && service->xiaomi_bound;
        unlock_state((smart_home_miloco_t *)service);
    }
    return bound;
}

bool smart_home_miloco_reachable(const smart_home_miloco_t *service)
{
    bool reachable = false;

    if (service) {
        lock_state((smart_home_miloco_t *)service);
        reachable = service->reachable;
        unlock_state((smart_home_miloco_t *)service);
    }
    return reachable;
}

/* 危险动作黑名单与类别映射已迁入 smart_home_miloco_specscan.c
 * （随轻量解析一起移入本文件，仅 spec 端点使用）。 */

bool smart_home_miloco_copy_device(const smart_home_miloco_t *service,
                                    const char *did,
                                    smart_home_miloco_device_t *out)
{
    bool found = false;
    if (!service || !did || !out) return false;
    lock_state((smart_home_miloco_t *)service);
    for (size_t i = 0; i < service->device_count; i++) {
        if (!strcmp(service->devices[i].did, did)) {
            *out = service->devices[i];
            found = true;
            break;
        }
    }
    unlock_state((smart_home_miloco_t *)service);
    return found;
}

static miloco_control_result_t *control_result_locked(smart_home_miloco_t *s,
                                                       uint32_t id)
{
    if (!id) return NULL;
    for (unsigned i = 0; i < MILOCO_CONTROL_QUEUE_DEPTH; ++i)
        if (s->results[i].request.request_id == id) return &s->results[i];
    return NULL;
}

static void control_report(smart_home_miloco_control_report_t *r,
    smart_home_miloco_control_state_t state, int code, const char *reason)
{
    r->state = state;
    r->code = code;
    r->reason = reason;
    r->terminal = state != SMART_HOME_MILOCO_CONTROL_PENDING;
}

/* Caller holds the service lock: validate one coherent snapshot, without
 * copying a large device/control table onto either caller's stack. */
static int validate_control_locked(smart_home_miloco_t *s, const char *did,
    const char *iid, const char *operation, int32_t value,
    smart_home_miloco_control_t **control, const char **reason)
{
    *control = NULL;
    *reason = "invalid_arguments";
    if (!did || !iid || !operation || !did[0] || !iid[0] ||
        strlen(did) >= sizeof(s->pending[0].did) ||
        strlen(iid) >= sizeof(s->pending[0].iid) ||
        (strcmp(operation, "set") && strcmp(operation, "action")))
        return AGENT_ERROR_TOOL_ARGUMENT;
    if (s->stop_requested || s->config_dirty || s->save_pending || s->save_in_progress) {
        *reason = "gateway_reconfiguring_or_stopped";
        return AGENT_ERROR_STATE;
    }
    if (!s->reachable) {
        *reason = "gateway_unreachable";
        return AGENT_ERROR_IO;
    }
    for (size_t i = 0; i < s->device_count; ++i) {
        smart_home_miloco_device_t *device = &s->devices[i];
        if (strcmp(device->did, did)) continue;
        if (!device->online) {
            *reason = "device_offline";
            return AGENT_ERROR_IO;
        }
        for (unsigned k = 0; k < device->control_count &&
                             k < SMART_HOME_MILOCO_MAX_CONTROLS; ++k) {
            smart_home_miloco_control_t *c = &device->controls[k];
            if (strcmp(c->iid, iid)) continue;
            *reason = "unsupported_operation_or_value";
            if ((!strcmp(operation, "action")) !=
                (c->type == SMART_HOME_MILOCO_CTRL_ACTION) ||
                (strcmp(operation, "action") &&
                 !smart_home_miloco_value_allowed(c, value)))
                return AGENT_ERROR_TOOL_ARGUMENT;
            *control = c;
            *reason = "validated";
            return AGENT_OK;
        }
        *reason = "control_not_found";
        return AGENT_ERROR_NOT_FOUND;
    }
    *reason = "device_not_found";
    return AGENT_ERROR_NOT_FOUND;
}

static int enqueue_control(smart_home_miloco_t *s, const char *did,
    const char *iid, const char *operation, int32_t value, uint64_t deadline,
    const char *source, bool retained, uint32_t *id,
    smart_home_miloco_control_report_t *report)
{
    smart_home_miloco_control_t *control;
    miloco_control_result_t *slot = NULL;
    const char *reason = "gateway_unavailable";
    int ret = AGENT_ERROR_IO;
    if (id) *id = 0;
    memset(report, 0, sizeof(*report));
    if (!s) goto failed;
    lock_state(s);
    ret = validate_control_locked(s, did, iid, operation, value, &control, &reason);
    if (ret != AGENT_OK) goto unlock;
    if (deadline && sca_time_ms() >= deadline) {
        ret = AGENT_ERROR_TIMEOUT; reason = "deadline_before_submit"; goto unlock;
    }
    for (unsigned i = 0; i < MILOCO_CONTROL_QUEUE_DEPTH; ++i) {
        miloco_control_result_t *r = &s->results[i];
        if (r->request.request_id && (r->retained || !r->complete) &&
            !strcmp(r->request.did, did) && !strcmp(r->request.iid, iid)) {
            ret = AGENT_ERROR_BUSY; reason = "control_busy"; goto unlock;
        }
    }
    for (unsigned i = 0; i < MILOCO_CONTROL_QUEUE_DEPTH; ++i) {
        unsigned k = (s->result_next + i) % MILOCO_CONTROL_QUEUE_DEPTH;
        miloco_control_result_t *r = &s->results[k];
        if (!r->request.request_id || (!r->retained && r->complete && !r->inflight)) {
            slot = r; s->result_next = (k + 1) % MILOCO_CONTROL_QUEUE_DEPTH; break;
        }
    }
    if (!slot || s->pending_count >= MILOCO_CONTROL_QUEUE_DEPTH) {
        ret = AGENT_ERROR_LIMIT; reason = "control_queue_full"; goto unlock;
    }
    do { if (++s->next_request_id == 0) ++s->next_request_id; }
    while (control_result_locked(s, s->next_request_id));
    memset(slot, 0, sizeof(*slot));
    strcpy(slot->request.did, did);
    strcpy(slot->request.iid, iid);
    slot->request.is_action = !strcmp(operation, "action");
    slot->request.value = value;
    slot->request.request_id = s->next_request_id;
    slot->generation = s->control_generation;
    slot->control_type = control->type;
    slot->write_only = control->write_only;
    slot->deadline_ms = deadline;
    slot->retained = retained;
    snprintf(slot->source, sizeof(slot->source), "%.7s", source ? source : "legacy");
    control_report(&slot->report, SMART_HOME_MILOCO_CONTROL_PENDING,
                   AGENT_ERROR_BUSY, "queued");
    s->pending[s->pending_count++] = slot->request;
    if (id) *id = slot->request.request_id;
    *report = slot->report;
    SCA_LOGI("milo", "[control] stage=submit source=%s request=%lu did=%s iid=%s op=%s value=%ld",
        slot->source, (unsigned long)slot->request.request_id, did, iid, operation, (long)value);
unlock:
    unlock_state(s);
    if (ret == AGENT_OK) { sca_sem_post(&s->wake); return ret; }
failed:
    control_report(report, SMART_HOME_MILOCO_CONTROL_FAILED, ret, reason);
    SCA_LOGW("milo", "[control] stage=validate source=%.7s code=%d reason=%s",
             source ? source : "legacy", ret, reason);
    return ret;
}

int smart_home_miloco_control_begin(smart_home_miloco_t *s,
    const char *did, const char *iid, const char *operation, int32_t value,
    uint64_t deadline_ms, const char *source, uint32_t *id,
    smart_home_miloco_control_report_t *report)
{
    uint64_t now = sca_time_ms();
    uint64_t deadline = now > UINT64_MAX - SMART_HOME_MILOCO_CONTROL_WAIT_MS ?
        UINT64_MAX : now + SMART_HOME_MILOCO_CONTROL_WAIT_MS;
    if (!id || !report) return AGENT_ERROR_INVALID;
    if (deadline_ms && deadline_ms < deadline) deadline = deadline_ms;
    return enqueue_control(s, did, iid, operation, value, deadline, source, true, id, report);
}

/* Called under lock, including when worker has dequeued but not sent yet. */
static void abandon_control_locked(smart_home_miloco_t *s,
                                   miloco_control_result_t *r, bool cancel)
{
    if (r->report.terminal) return;
    for (size_t i = 0; i < s->pending_count; ++i) {
        if (s->pending[i].request_id != r->request.request_id) continue;
        memmove(&s->pending[i], &s->pending[i + 1],
                (s->pending_count - i - 1) * sizeof(s->pending[0]));
        --s->pending_count;
        r->complete = true;
        break;
    }
    control_report(&r->report, r->report.dispatched ?
        SMART_HOME_MILOCO_CONTROL_UNCONFIRMED : cancel ?
        SMART_HOME_MILOCO_CONTROL_CANCELLED : SMART_HOME_MILOCO_CONTROL_FAILED,
        cancel ? AGENT_ERROR_CANCELLED : AGENT_ERROR_TIMEOUT,
        r->report.dispatched ? "dispatched_result_unconfirmed" : cancel ?
        "cancelled_before_dispatch" : "deadline_before_dispatch");
}

static void resolve_control_locked(smart_home_miloco_t *s, miloco_control_result_t *r)
{
    if (r->report.terminal) return;
    if (r->complete) {
        smart_home_miloco_control_t *c = NULL;
        for (size_t i = 0; i < s->device_count; ++i) {
            smart_home_miloco_device_t *d = &s->devices[i];
            if (strcmp(d->did, r->request.did) || !d->online) continue;
            for (unsigned k = 0; k < d->control_count && k < SMART_HOME_MILOCO_MAX_CONTROLS; ++k)
                if (!strcmp(d->controls[k].iid, r->request.iid)) { c = &d->controls[k]; break; }
        }
        if (r->request.is_action && r->report.code == AGENT_OK) {
            control_report(&r->report, SMART_HOME_MILOCO_CONTROL_CONFIRMED,
                           AGENT_OK, "gateway_confirmed_action");
        } else if (!r->request.is_action && r->write_only && r->report.code == AGENT_OK) {
            control_report(&r->report, SMART_HOME_MILOCO_CONTROL_CONFIRMED,
                           AGENT_OK, "gateway_accepted_write");
        } else if (c && c->type == r->control_type && !r->request.is_action &&
                   !r->write_only && !c->write_only && c->value_valid &&
                   (int32_t)(c->observation - r->request.dispatch_observation) > 0 &&
                   c->value == r->request.value) {
            bool accepted = r->report.code == AGENT_OK;
            control_report(&r->report, SMART_HOME_MILOCO_CONTROL_CONFIRMED,
                AGENT_OK, accepted ? "device_state_confirmed" : "current_state_confirmed");
        } else if (r->report.code == SMART_HOME_MILOCO_RESULT_UNCONFIRMED &&
                   (r->request.is_action || r->write_only)) {
            control_report(&r->report, SMART_HOME_MILOCO_CONTROL_UNCONFIRMED,
                           AGENT_ERROR_TOOL_FAILED, "gateway_result_unconfirmed");
        }
    }
    if (!r->report.terminal && r->deadline_ms && sca_time_ms() >= r->deadline_ms)
        abandon_control_locked(s, r, false);
}

void smart_home_miloco_control_poll(smart_home_miloco_t *s, uint32_t id,
                                   smart_home_miloco_control_report_t *report)
{
    if (!report) return;
    memset(report, 0, sizeof(*report));
    control_report(report, SMART_HOME_MILOCO_CONTROL_FAILED,
                   AGENT_ERROR_NOT_FOUND, "request_not_found");
    if (!s) return;
    lock_state(s);
    miloco_control_result_t *r = control_result_locked(s, id);
    if (r) { resolve_control_locked(s, r); *report = r->report; }
    unlock_state(s);
}

void smart_home_miloco_control_cancel(smart_home_miloco_t *s, uint32_t id,
                                     smart_home_miloco_control_report_t *report)
{
    if (!s) { smart_home_miloco_control_poll(s, id, report); return; }
    lock_state(s);
    miloco_control_result_t *r = control_result_locked(s, id);
    if (r) { resolve_control_locked(s, r); abandon_control_locked(s, r, true); }
    unlock_state(s);
    smart_home_miloco_control_poll(s, id, report);
}

void smart_home_miloco_control_release(smart_home_miloco_t *s, uint32_t id)
{
    if (!s) return;
    lock_state(s);
    miloco_control_result_t *r = control_result_locked(s, id);
    if (r) {
        resolve_control_locked(s, r);
        abandon_control_locked(s, r, true);
        r->retained = false;
        SCA_LOGI("milo", "[control] stage=release request=%lu code=%d reason=%s sent=%u",
            (unsigned long)id, r->report.code, r->report.reason, r->report.dispatched);
    }
    unlock_state(s);
}

const char *smart_home_miloco_control_feedback(const smart_home_miloco_control_report_t *r)
{
    if (!r || !r->terminal) return "Sent, awaiting confirmation...";
    if (r->state == SMART_HOME_MILOCO_CONTROL_CONFIRMED) {
        if (!strcmp(r->reason, "gateway_confirmed_action")) return "Gateway confirmed action";
        if (!strcmp(r->reason, "gateway_accepted_write")) return "Write accepted, attribute not readable";
        if (!strcmp(r->reason, "current_state_confirmed")) return "Current state confirmed";
        return "Device state confirmed";
    }
    if (r->state == SMART_HOME_MILOCO_CONTROL_UNCONFIRMED)
        return "Execution unconfirmed; check device before retrying";
    if (r->state == SMART_HOME_MILOCO_CONTROL_CANCELLED) return "Cancelled before sending";
    if (!strcmp(r->reason, "control_busy")) return "Control busy, wait for previous request";
    if (!strcmp(r->reason, "gateway_unreachable")) return "Gateway unreachable, check connection";
    if (!strcmp(r->reason, "device_not_found")) return "Device no longer available, refresh devices";
    if (!strcmp(r->reason, "device_offline")) return "Device offline";
    if (!strcmp(r->reason, "control_not_found") ||
        !strcmp(r->reason, "control_changed_before_dispatch"))
        return "Device controls changed, refresh controls";
    if (!strcmp(r->reason, "unsupported_operation_or_value")) return "Unsupported operation or value";
    if (!strcmp(r->reason, "control_queue_full")) return "Control queue full, wait for pending requests";
    if (!strcmp(r->reason, "deadline_before_dispatch") ||
        !strcmp(r->reason, "deadline_before_submit")) return "Timed out before sending; command not sent";
    if (!strcmp(r->reason, "gateway_rejected")) return "Gateway rejected the command";
    return "Control failed; check gateway or device";
}

bool smart_home_miloco_control_result(const smart_home_miloco_t *s,
                                      uint32_t id, int *result)
{
    uint32_t observation;
    return smart_home_miloco_control_result_detail(s, id, result, &observation);
}

bool smart_home_miloco_control_result_detail(const smart_home_miloco_t *s,
    uint32_t id, int *result, uint32_t *dispatch_observation)
{
    bool found = false;
    if (!s || !id || !result || !dispatch_observation) return false;
    lock_state((smart_home_miloco_t *)s);
    miloco_control_result_t *r = control_result_locked((smart_home_miloco_t *)s, id);
    if (r && r->complete) {
        *result = r->report.code;
        *dispatch_observation = r->request.dispatch_observation;
        found = true;
    }
    unlock_state((smart_home_miloco_t *)s);
    return found;
}

int smart_home_miloco_submit_control(smart_home_miloco_t *s,
    const char *did, const char *iid, const char *operation, int32_t value)
{
    return smart_home_miloco_submit_control_tracked(s, did, iid, operation, value, NULL);
}

int smart_home_miloco_submit_control_tracked(smart_home_miloco_t *s,
    const char *did, const char *iid, const char *operation, int32_t value, uint32_t *id)
{
    smart_home_miloco_control_report_t report;
    return enqueue_control(s, did, iid, operation, value, 0, "legacy", false, id, &report);
}

int smart_home_miloco_submit_power(smart_home_miloco_t *service,
                                   const char *did,
                                   bool on)
{
    smart_home_miloco_device_t device;
    int index;
    if (!smart_home_miloco_copy_device(service, did, &device)) return AGENT_ERROR_NOT_FOUND;
    index = smart_home_miloco_power_index(&device);
    if (index < 0) return AGENT_ERROR_INVALID;
    return smart_home_miloco_submit_control(service, did, device.controls[index].iid,
                                            "set", on ? 1 : 0);
}

/*
 * 原子切换网关目标配置：worker 在下一个循环边界应用并立即轮询。
 * 与 stop+start 的区别：不销毁线程（无 join 阻塞、无栈重分配），
 * LVGL 线程调用安全。
 */
int smart_home_miloco_reconfigure(smart_home_miloco_t *service,
                                  const smart_home_miloco_config_t *config)
{
    smart_home_miloco_client_config_t client_config;
    int ret = AGENT_OK;

    if (!service || !smart_home_miloco_config_valid(config)) {
        return AGENT_ERROR_INVALID;
    }
    snprintf(client_config.host, sizeof(client_config.host), "%s",
             config->host);
    client_config.port = config->port;
    snprintf(client_config.token, sizeof(client_config.token), "%s",
             config->token);

    lock_state(service);
    if (service->stop_requested) {
        ret = AGENT_ERROR_INVALID;
    } else {
        service->pending_config = client_config;
        service->config_dirty = true;
    }
    unlock_state(service);
    if (ret == AGENT_OK) {
        sca_sem_post(&service->wake);
    }
    return ret;
}

int smart_home_miloco_request_save(smart_home_miloco_t *service,
                                   const smart_home_miloco_config_t *config)
{
    int ret = AGENT_OK;

    if (!service || !smart_home_miloco_config_valid(config)) {
        return AGENT_ERROR_INVALID;
    }
    lock_state(service);
    if (service->stop_requested) {
        ret = AGENT_ERROR_INVALID;
    } else if (service->save_pending || service->save_in_progress) {
        ret = AGENT_ERROR_BUSY;
    } else {
        service->save_config = *config;
        service->save_completed = false;
        service->save_pending = true;
    }
    unlock_state(service);
    if (ret == AGENT_OK) {
        sca_sem_post(&service->wake);
    }
    return ret;
}

int smart_home_miloco_save_status(const smart_home_miloco_t *service)
{
    int result;
    if (!service) return AGENT_ERROR_INVALID;
    lock_state((smart_home_miloco_t *)service);
    result = service->save_pending || service->save_in_progress ? AGENT_ERROR_BUSY :
             service->save_completed ? service->save_result : AGENT_ERROR_NOT_FOUND;
    unlock_state((smart_home_miloco_t *)service);
    return result;
}

/* ── JSON 解析 ─────────────────────────────────────────────── */

static void copy_text(char *dst, size_t dst_size, const cJSON *value)
{
    const char *text = cJSON_IsString(value) ? value->valuestring : "";
    size_t n = strlen(text);

    if (n > dst_size - 1)
      {
        /* UTF-8 字符边界回退：字节截断会产生非法序列（同
         * specscan copy_bounded 的教训——半个汉字曾让 agent 请求
         * 被 LLM 服务端以 invalid unicode 拒绝）。 */
        n = dst_size - 1;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80)
          {
            n--;
          }
      }

    memcpy(dst, text, n);
    dst[n] = '\0';
}

/* 解析 data: [{did,name,online,room_name,...}, ...]（device_list 响应）。 */
static int parse_device_list(smart_home_miloco_t *service, const char *body,
                             smart_home_miloco_device_t *fresh_categories,
                             size_t fresh_capacity,
                             size_t *fresh_count)
{
    cJSON *root = cJSON_Parse(body);
    cJSON *data;
    cJSON *item;
    smart_home_miloco_device_t *parsed = service->poll_parse;
    size_t count = 0;

    *fresh_count = 0;
    if (!root) {
        return AGENT_ERROR_PARSE;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return AGENT_ERROR_PARSE;
    }

    cJSON_ArrayForEach(item, data) {
        const cJSON *did = cJSON_GetObjectItemCaseSensitive(item, "did");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *online = cJSON_GetObjectItemCaseSensitive(item, "online");
        const cJSON *room = cJSON_GetObjectItemCaseSensitive(item, "room_name");
        smart_home_miloco_device_t *slot;

        if (!cJSON_IsString(did) || !did->valuestring[0] ||
            count >= SMART_HOME_MILOCO_MAX_DEVICES) {
            continue;
        }
        slot = &parsed[count++];
        memset(slot, 0, sizeof(*slot));
        copy_text(slot->did, sizeof(slot->did), did);
        copy_text(slot->name, sizeof(slot->name), name);
        copy_text(slot->room, sizeof(slot->room), room);
        slot->online = cJSON_IsTrue(online);
    }
    cJSON_Delete(root);

    /* 保留旧条目的 category/controllable/power_on；新 did 记入 fresh 列表
     * 供 worker 后续拉取 spec。 */
    {
        size_t i;
        size_t j;

        for (i = 0; i < count; i++) {
            const smart_home_miloco_device_t *old = NULL;

            for (j = 0; j < service->device_count; j++) {
                if (strcmp(service->devices[j].did, parsed[i].did) == 0) {
                    old = &service->devices[j];
                    break;
                }
            }
            if (old) {
                parsed[i].category = old->category;
                parsed[i].controllable = old->controllable;
                parsed[i].power_on = old->power_on;
                parsed[i].power_valid = old->power_valid;
                parsed[i].control_count = old->control_count;
                memcpy(parsed[i].controls, old->controls,
                       sizeof(parsed[i].controls));
            } else if (fresh_categories && *fresh_count < fresh_capacity) {
                strcpy(fresh_categories[*fresh_count].did, parsed[i].did);
                (*fresh_count)++;
            }
        }
    }

    /* 真变才 revision++：列表内容不变时（绝大多数轮询）不打扰 UI——
     * 原先每 5 秒无条件 bump，触发全网格 lv_obj_clean 重建（白闪、
     * 滚动位置重置）并配合 UI 刷新顺序杀掉打开中的控制抽屉。
     * controls 内容与状态值的变化由 parse_device_spec /
     * refresh_device_status 各自 bump，此处只比列表级字段。 */
    {
        size_t i;
        int changed = service->device_count != count || !service->reachable;

        if (!changed)
          {
            for (i = 0; i < count; i++)
              {
                const smart_home_miloco_device_t *old =
                    &service->devices[i];

                if (strcmp(old->did, parsed[i].did) != 0 ||
                    strcmp(old->name, parsed[i].name) != 0 ||
                    strcmp(old->room, parsed[i].room) != 0 ||
                    old->online != parsed[i].online ||
                    old->category != parsed[i].category ||
                    old->controllable != parsed[i].controllable ||
                    old->power_on != parsed[i].power_on ||
                    old->control_count != parsed[i].control_count)
                  {
                    changed = 1;
                    break;
                  }
              }
          }

        lock_state(service);
        memcpy(service->devices, parsed, count * sizeof(parsed[0]));
        service->device_count = count;
        service->reachable = true;
        if (changed)
          {
            service->revision++;
          }
        unlock_state(service);
    }
    return AGENT_OK;
}

/* 解析 data: {did, name, category, spec:{iid:{...}}}。轻量扫描见
 * smart_home_miloco_specscan.c——17KB 摄像机 spec 的 cJSON 整树解析
 * （上千次 malloc/free 风暴，约 130KB 瞬时树落 SRAM 堆区）曾在真机
 * 引发分配器冻死；现为零堆分配的线性扫描，本函数只剩结果应用。 */
static int parse_device_spec(smart_home_miloco_t *service, const char *body)
{
    smart_home_miloco_category_t mapped;
    char target_did[24];
    size_t dev_index = SMART_HOME_MILOCO_MAX_DEVICES;
    smart_home_miloco_control_t parsed[SMART_HOME_MILOCO_MAX_CONTROLS];
    uint8_t parsed_count = 0;
    bool power_ctrl = false;
    size_t i;

    if (smart_home_miloco_specscan(body, target_did, sizeof(target_did),
                                   &mapped, parsed,
                                   SMART_HOME_MILOCO_MAX_CONTROLS,
                                   &parsed_count, &power_ctrl) != 0)
      {
        return AGENT_ERROR_PARSE;
      }

    lock_state(service);
    for (i = 0; i < service->device_count; i++) {
        if (strcmp(service->devices[i].did, target_did) == 0) {
            dev_index = i;
            break;
        }
    }
    if (dev_index != SMART_HOME_MILOCO_MAX_DEVICES) {
        smart_home_miloco_device_t *dev = &service->devices[dev_index];

        dev->category = mapped;
        memcpy(dev->controls, parsed, parsed_count * sizeof(parsed[0]));
        dev->control_count = parsed_count;
        dev->controllable = power_ctrl;
        service->revision++;
    }
    unlock_state(service);
    return AGENT_OK;
}

/* 新版 data.properties 和旧版 data 数组共用类型及错误码校验。 */
static int refresh_device_status(smart_home_miloco_t *service,
                                 const char *did_text, const char *body)
{
    int ret = AGENT_ERROR_NOT_FOUND;
    lock_state(service);
    for (size_t i = 0; i < service->device_count; i++) {
        smart_home_miloco_device_t *dev = &service->devices[i];
        bool changed = false;
        if (strcmp(dev->did, did_text)) continue;
        ret = smart_home_miloco_status_apply(dev, body, &changed);
        if (changed) service->revision++;
        break;
    }
    unlock_state(service);
    return ret;
}

/* 拉取设备全部 prop 控制项的当前值（一次合并请求）。 */
static int refresh_device_values(smart_home_miloco_t *service,
                                 const char *did,
                                 char *body, size_t body_size)
{
    char path[80 + SMART_HOME_MILOCO_MAX_CONTROLS * 14];
    char iids[SMART_HOME_MILOCO_MAX_CONTROLS * 14];
    size_t used = 0;
    int http_status = 0;
    int ret;
    size_t i;
    uint8_t k;

    iids[0] = '\0';
    lock_state(service);
    for (i = 0; i < service->device_count; i++) {
        const smart_home_miloco_device_t *dev = &service->devices[i];

        if (strcmp(dev->did, did) != 0) {
            continue;
        }
        for (k = 0; k < dev->control_count; k++) {
            const smart_home_miloco_control_t *ctrl = &dev->controls[k];

            if (ctrl->type == SMART_HOME_MILOCO_CTRL_ACTION || ctrl->write_only) {
                continue;
            }
            if (used + strlen(ctrl->iid) + 2 >= sizeof(iids)) {
                break;
            }
            used += (size_t)snprintf(iids + used, sizeof(iids) - used,
                                     "%s%s", used ? "," : "", ctrl->iid);
        }
        break;
    }
    unlock_state(service);
    if (!used) {
        return AGENT_OK;
    }
    snprintf(path, sizeof(path), "/api/miot/devices/%.23s/status?iid=%s",
             did, iids);
    ret = smart_home_miloco_http_get(&service->client_config, path,
                                     body, body_size, &http_status);
    if (ret < 0 || http_status != 200) {
        (void)refresh_device_status(service, did, NULL);
        return ret < 0 ? ret : -EIO;
    }
    return refresh_device_status(service, did, body);
}

/* ── worker ────────────────────────────────────────────────── */

/* spec 拉取重试记账（service 本地，避免改动 miloco.h 共享结构体触发
 * 全量重编）。原设计"新设备一次性拉 spec 且忽略返回值"：首次失败
 * （网关刚连上时网络/DNS 尚未稳定的高危时刻）即永久空 controls，
 * agent 与 UI 都无法控制。现改为 control_count 仍为 0 的在线设备
 * 每轮重试，超过上限放弃，防止 spec 恒空时打爆 backend。 */
#define MILOCO_SPEC_RETRY_MAX 12

static struct
{
    char did[24];
    uint8_t attempts;
} g_spec_attempts[SMART_HOME_MILOCO_MAX_DEVICES];

/* 记一次尝试；返回 false 表示已达上限或表满，本轮不再拉。 */
static bool spec_attempt_take(const char *did)
{
    int slot = -1;
    int i;

    if (!did || !did[0])
      {
        return false;
      }

    for (i = 0; i < SMART_HOME_MILOCO_MAX_DEVICES; i++)
      {
        if (g_spec_attempts[i].did[0] &&
            strcmp(g_spec_attempts[i].did, did) == 0)
          {
            slot = i;
            break;
          }

        if (slot < 0 && g_spec_attempts[i].did[0] == '\0')
          {
            slot = i;
          }
      }

    if (slot < 0)
      {
        return false;
      }

    if (g_spec_attempts[slot].did[0] == '\0')
      {
        snprintf(g_spec_attempts[slot].did,
                 sizeof(g_spec_attempts[slot].did), "%.23s", did);
        g_spec_attempts[slot].attempts = 0;
      }

    if (g_spec_attempts[slot].attempts >= MILOCO_SPEC_RETRY_MAX)
      {
        return false;
      }

    g_spec_attempts[slot].attempts++;
    return true;
}

static int fetch_spec_for(smart_home_miloco_t *service, const char *did,
                          char *body, size_t body_size)
{
    char path[80];
    int http_status = 0;
    int ret;

    snprintf(path, sizeof(path), "/api/miot/devices/%.23s/spec", did);
    ret = smart_home_miloco_http_get(&service->client_config, path,
                                     body, body_size, &http_status);
    if (ret < 0) {
        return ret;
    }
    if (http_status != 200) {
        return -EIO;
    }
    return parse_device_spec(service, body);
}

/* GET /api/miot/status 解析 data.is_bound。绑定状态翻转时递增 revision
 * 触发 UI 刷新；未绑定时设备列表清空。 */
static int poll_bind_status(smart_home_miloco_t *service,
                            char *body, size_t body_size)
{
    cJSON *root;
    cJSON *data;
    const cJSON *is_bound;
    bool bound = false;
    int http_status = 0;
    int ret;

    ret = smart_home_miloco_http_get(&service->client_config,
                                     "/api/miot/status", body, body_size,
                                     &http_status);
    if (ret < 0) {
        lock_state(service);
        if (service->reachable) {
            service->reachable = false;
            service->revision++;
        }
        unlock_state(service);
        return ret;
    }
    if (http_status != 200) {
        return -EIO;
    }
    root = cJSON_Parse(body);
    if (!root) {
        return AGENT_ERROR_PARSE;
    }
    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    is_bound = cJSON_IsObject(data)
        ? cJSON_GetObjectItemCaseSensitive(data, "is_bound") : NULL;
    bound = cJSON_IsTrue(is_bound);
    cJSON_Delete(root);

    lock_state(service);
    if (!service->reachable || service->xiaomi_bound != bound) {
        service->revision++;
    }
    service->reachable = true;
    service->xiaomi_bound = bound;
    if (!bound && service->device_count > 0) {
        service->device_count = 0;
        service->revision++;
    }
    unlock_state(service);
    return bound ? AGENT_OK : AGENT_ERROR_NOT_FOUND;
}

static int poll_device_list(smart_home_miloco_t *service,
                            char *body, size_t body_size)
{
    smart_home_miloco_device_t *fresh = service->poll_fresh;
    size_t fresh_count = 0;
    int http_status = 0;
    int ret;
    size_t i;

    ret = smart_home_miloco_http_get(&service->client_config,
                                     "/api/miot/device_list", body, body_size,
                                     &http_status);
    if (ret < 0) {
        /* 设备列表传输失败不降级网关在线状态：健康判定只属于
         * /api/miot/status（poll_bind_status）。保留上次设备快照，
         * 下轮轮询自然重试；绑定后服务端重编排期间曾出现短暂
         * 连接超时，属可恢复现象。 */
        return ret;
    }
    if (http_status != 200) {
        return -EIO;
    }
    ret = parse_device_list(service, body, fresh,
                            SMART_HOME_MILOCO_MAX_DEVICES, &fresh_count);
    if (ret != AGENT_OK) {
        return ret;
    }

    /* 新设备补 spec 类别；可控设备回读电源态。复用响应缓冲。 */
    for (i = 0; i < fresh_count; i++) {
        fetch_spec_for(service, fresh[i].did, body, body_size);
        spec_attempt_take(fresh[i].did);
    }
    {
        smart_home_miloco_device_t *snapshot = service->poll_snapshot;
        size_t count;

        count = smart_home_miloco_list(service, snapshot,
                                       SMART_HOME_MILOCO_MAX_DEVICES, NULL);
        for (i = 0; i < count; i++) {
            /* controls 仍为空的在线设备重试 spec（带上限）：
             * 修复首次拉取失败一次即永久无 controls 的结构性缺口。 */
            if (snapshot[i].online && snapshot[i].control_count == 0 &&
                spec_attempt_take(snapshot[i].did)) {
                SCA_LOGI("milo", "[milo] spec retry did=%.23s",
                       snapshot[i].did);
                fetch_spec_for(service, snapshot[i].did, body, body_size);
            }
    if (snapshot[i].online && snapshot[i].control_count > 0) {
                refresh_device_values(service, snapshot[i].did,
                                      body, body_size);
            }
        }
    }
    return AGENT_OK;
}

/* ── 天气（wttr.in 文本格式） ───────────────────────────────── */

/* 天气条件直接沿用 wttr.in 的英文原文（"Clear" / "Light rain"）。
 * 不再另存中文映射（condition_cn）：UI 字体为纯英文 Montserrat，
 * 中文映射不会被渲染。 */

/* 拉取 wttr.in/{city}?format=%C|%t|%h|%w 并解析管道分隔响应。
 * 文本格式约 40 字节，远小于 JSON 25KB，复用现有响应缓冲。 */
static void poll_weather(smart_home_miloco_t *service,
                         char *body, size_t body_size)
{
    smart_home_miloco_client_config_t cfg;
    char city[sizeof(service->weather.city)];
    char path[64];
    char *fields[4];
    time_t server_date = (time_t)-1;
    char *saveptr;
    int http_status = 0;
    int field_count;
    int i;
    int ret;

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.host, sizeof(cfg.host), "wttr.in");
    cfg.port = 80;
    cfg.token[0] = '\0';

    /* 城市可能被 UI 线程改写，拷贝后再拼 URL。 */
    lock_state(service);
    snprintf(city, sizeof(city), "%s", service->weather.city);
    unlock_state(service);
    snprintf(path, sizeof(path), "/%.20s?format=%%C|%%t|%%h|%%w", city);
    ret = smart_home_miloco_http_get_date(&cfg, path, body, body_size,
                                          &http_status, &server_date);
    if (ret < 0 || http_status != 200) {
        SCA_LOGW("milo", "[milo] weather fetch failed ret=%d status=%d",
               ret, http_status);
        return;
    }

    SCA_LOGI("milo", "[milo] weather http date=%ld cur=%ld",
           (long)server_date, (long)time(NULL));

    /* 响应头 Date 即时间源（砍掉独立 SNTP 后的对时途径）：仅当
     * 本地时钟明显落后时前拨，避免正常走时被反复回写。 */
    if (server_date > (time_t)1000000000L) {
        struct timeval tv;

        gettimeofday(&tv, NULL);
        if (tv.tv_sec < server_date - 30L) {
            tv.tv_sec = server_date;
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);
            SCA_LOGI("milo", "[milo] time set from HTTP Date: %ld",
                   (long)server_date);
        }
    }

    /* 响应形如 "Clear|+29°C|72%|→13km/h"，按管道切四段。 */
    field_count = 0;
    for (i = 0; i < 4; i++) {
        fields[i] = i == 0 ? strtok_r(body, "|", &saveptr) :
                             strtok_r(NULL, "|", &saveptr);
        if (!fields[i]) {
            break;
        }
        field_count++;
    }
    if (field_count < 4) {
        SCA_LOGW("milo", "[milo] weather parse: got %d fields",
               field_count);
        return;
    }

    lock_state(service);
    snprintf(service->weather.condition_en,
             sizeof(service->weather.condition_en), "%s", fields[0]);
    service->weather.temperature = atoi(fields[1]);
    service->weather.humidity = atoi(fields[2]);
    service->weather.wind_kmph = atoi(fields[3]);
    service->weather.valid = true;
    service->revision++;    /* 触发 UI 刷新 */
    unlock_state(service);
    SCA_LOGI("milo", "[milo] weather %s: %s %dC %d%% %dkm/h",
           service->weather.city, fields[0],
           service->weather.temperature, service->weather.humidity,
           service->weather.wind_kmph);
}



bool smart_home_miloco_get_weather(const smart_home_miloco_t *service,
                                   smart_home_miloco_weather_t *out)
{
    bool valid;

    if (!service || !out) {
        return false;
    }
    lock_state((smart_home_miloco_t *)service);
    *out = service->weather;
    valid = service->weather.valid;
    unlock_state((smart_home_miloco_t *)service);
    return valid;
}

int smart_home_miloco_set_weather_city(smart_home_miloco_t *service,
                                       const char *city)
{
    if (!service || !city || !city[0] || strlen(city) >= 24) {
        return -EINVAL;
    }
    lock_state(service);
    if (strcmp(service->weather.city, city) != 0) {
        snprintf(service->weather.city, sizeof(service->weather.city),
                 "%s", city);
        service->weather.valid = false;      /* 旧城市数据作废 */
    }
    /* 下一轮 worker 迭代计数自增到 120，立即拉取新城市天气。 */
    service->weather_poll_count = 119;
    unlock_state(service);
    SCA_LOGI("milo", "[milo] weather city set to %s", city);
    return 0;
}

static int execute_control(smart_home_miloco_t *service,
                           miloco_control_request_t *request,
                           char *body, size_t body_size)
{
    char path[80];
    char request_body[128];
    int http_status = 0;
    int ret;
    bool is_bool = false;
    const char *reason;
    smart_home_miloco_control_t *ctrl;
    lock_state(service);
    miloco_control_result_t *r = control_result_locked(service, request->request_id);
    if (!r) { unlock_state(service); return AGENT_ERROR_NOT_FOUND; }
    if (r->deadline_ms && sca_time_ms() >= r->deadline_ms)
        abandon_control_locked(service, r, false);
    if (r->report.terminal) { ret = r->report.code; unlock_state(service); return ret; }
    ret = validate_control_locked(service, request->did, request->iid,
        request->is_action ? "action" : "set", request->value, &ctrl, &reason);
    if (ret == AGENT_OK && r->generation != service->control_generation) {
        ret = AGENT_ERROR_STATE;
        reason = "gateway_changed_before_dispatch";
    }
    if (ret == AGENT_OK && (ctrl->type != r->control_type || ctrl->write_only != r->write_only)) {
        ret = AGENT_ERROR_TOOL_ARGUMENT;
        reason = "control_changed_before_dispatch";
    }
    if (ret != AGENT_OK) {
        control_report(&r->report, SMART_HOME_MILOCO_CONTROL_FAILED, ret, reason);
        unlock_state(service);
        return ret;
    }
    is_bool = ctrl->type == SMART_HOME_MILOCO_CTRL_BOOL;
    request->dispatch_observation = ctrl->observation;
    r->request.dispatch_observation = ctrl->observation;
    if (!request->is_action) {
        ctrl->value_valid = false;
        for (size_t i = 0; i < service->device_count; ++i) {
            smart_home_miloco_device_t *d = &service->devices[i];
            if (!strcmp(d->did, request->did) && smart_home_miloco_power_index(d) >= 0 &&
                ctrl == &d->controls[smart_home_miloco_power_index(d)]) d->power_valid = false;
        }
        service->revision++;
    }
    r->report.dispatched = true;
    r->report.reason = "awaiting_gateway";
    unlock_state(service);

    snprintf(path, sizeof(path), "/api/miot/devices/%.23s/control",
             request->did);
    if (request->is_action) {
        snprintf(request_body, sizeof(request_body),
                 "{\"type\":\"call_action\",\"iid\":\"%.13s\"}",
                 request->iid);
    } else if (is_bool) {
        snprintf(request_body, sizeof(request_body),
                 "{\"type\":\"set_property\",\"iid\":\"%.13s\","
                 "\"value\":%s}",
                 request->iid, request->value ? "true" : "false");
    } else {
        snprintf(request_body, sizeof(request_body),
                 "{\"type\":\"set_property\",\"iid\":\"%.13s\","
                 "\"value\":%d}",
                 request->iid, (int)request->value);
    }
    ret = smart_home_miloco_http_post(&service->client_config, path,
                                      request_body, body, body_size,
                                      &http_status);
    char received[24];
    if (ret >= 0) snprintf(received, sizeof(received), "%lu", (unsigned long)strlen(body));
    else snprintf(received, sizeof(received), "unknown");
    SCA_LOGI("milo", "[control] stage=http request=%lu did=%s iid=%s op=%s transport=%d http=%d bytes=%s capacity=%lu",
        (unsigned long)request->request_id, request->did, request->iid,
        request->is_action ? "action" : "set", ret, http_status,
        received, (unsigned long)body_size);
    if (ret < 0) {
        SCA_LOGW("milo", "[miloco] control response: transport error=%d", ret);
        ret = SMART_HOME_MILOCO_RESULT_UNCONFIRMED;
    } else if (http_status != 200) {
        SCA_LOGW("milo", "[miloco] control response: http=%d", http_status);
        ret = SMART_HOME_MILOCO_RESULT_UNCONFIRMED;
    } else {
        ret = smart_home_miloco_control_reply(body, request->did, request->iid,
                                              request->is_action != 0);
        if (ret == AGENT_ERROR_PARSE) {
            SCA_LOGW("milo", "[miloco] control response: invalid result");
            ret = SMART_HOME_MILOCO_RESULT_UNCONFIRMED;
        } else if (ret != AGENT_OK) {
            SCA_LOGW("milo", "[miloco] control response: rejected result=%d", ret);
        }
    }
    if (!request->is_action &&
        (ret == AGENT_OK || ret == SMART_HOME_MILOCO_RESULT_UNCONFIRMED)) {
        int read_ret = refresh_device_values(service, request->did, body, body_size);
        if (read_ret != AGENT_OK)
            SCA_LOGW("milo", "[miloco] control readback: error=%d", read_ret);
    }
    return ret;
}

/* Inflight slots cannot be reused, even if their consumer has timed out. */
static bool take_control_locked(smart_home_miloco_t *s, miloco_control_request_t *out)
{
    if (!s->pending_count) return false;
    *out = s->pending[0];
    memmove(&s->pending[0], &s->pending[1], (--s->pending_count) * sizeof(s->pending[0]));
    miloco_control_result_t *r = control_result_locked(s, out->request_id);
    if (r) r->inflight = true;
    return true;
}

static void process_control(smart_home_miloco_t *s, miloco_control_request_t *request,
                            char *body, size_t capacity)
{
    int result = execute_control(s, request, body, capacity);
    lock_state(s);
    miloco_control_result_t *r = control_result_locked(s, request->request_id);
    if (r) {
        r->complete = true;
        r->inflight = false;
        if (!r->report.terminal && r->deadline_ms && sca_time_ms() >= r->deadline_ms)
            abandon_control_locked(s, r, false);
        if (!r->report.terminal) {
            r->report.code = result;
            if (result != AGENT_OK && result != SMART_HOME_MILOCO_RESULT_UNCONFIRMED)
                control_report(&r->report, SMART_HOME_MILOCO_CONTROL_FAILED,
                               result, "gateway_rejected");
            resolve_control_locked(s, r);
        }
        SCA_LOGI("milo", "[control] stage=complete request=%lu raw_code=%d code=%d reason=%s sent=%u",
            (unsigned long)request->request_id, result, r->report.code,
            r->report.reason, r->report.dispatched);
    }
    unlock_state(s);
}

static void miloco_worker(void *argument)
{
    smart_home_miloco_t *service = argument;
    char *body;
    bool poll_due = true;

    SCA_LOGI("milo", "[milo] worker: entry");
    body = smart_home_bulk_alloc(MILOCO_RESPONSE_BYTES);
    if (!body) {
        SCA_LOGE("milo", "ERROR: [miloco] response buffer alloc failed");
        return;
    }

    while (1) {
        bool stop;
        bool has_control = false;
        bool configured = service->client_config.host[0] != '\0';
        miloco_control_request_t control;
        lock_state(service);
        bool control_due = service->pending_count != 0;
        unlock_state(service);

        /* 天气拉取：首次立即，此后每 120 轮（10 分钟）一次；
         * 拉取失败（WiFi 未就绪/外网不通）时每 12 轮（1 分钟）重试，
         * 开机联网后最迟 1 分钟内即有天气和时间（HTTP Date 同步）。
         * 天气独立于米家网关配置：未绑定米家也能获取。 */
        service->weather_poll_count++;
        if (!control_due && (service->weather_poll_count >= 120 ||
            (!service->weather.valid &&
             (service->weather_poll_count % 12) == 0))) {
            poll_weather(service, body, MILOCO_RESPONSE_BYTES);
            service->weather_poll_count = 0;
        }

        if (!control_due && poll_due && configured) {
            SCA_LOGI("milo", "[milo] worker: poll begin");
            if (poll_bind_status(service, body, MILOCO_RESPONSE_BYTES)
                    == AGENT_OK) {
                /* 已绑定才轮询设备；未绑定时绑定页只需要 is_bound。 */
                poll_device_list(service, body, MILOCO_RESPONSE_BYTES);
            }
            poll_due = false;
        }

        lock_state(service);
        stop = service->stop_requested;
        if (service->save_pending) {
            smart_home_miloco_config_t save = service->save_config;
            smart_home_miloco_client_config_t client_config;
            int save_ret;

            service->save_pending = false;
            service->save_in_progress = true;
            memset(&service->save_config, 0, sizeof(service->save_config));
            snprintf(client_config.host, sizeof(client_config.host), "%s",
                     save.host);
            client_config.port = save.port;
            snprintf(client_config.token, sizeof(client_config.token), "%s",
                     save.token);
            unlock_state(service);
#ifdef CONFIG_SMART_HOME_MILOCO_VOLATILE_CONFIG
            /* 挥发型配置：跳过 secrets.json 写入（flash 写挂死的临时
             * 规避），仅在工作线程内应用并立即轮询。 */
            save_ret = AGENT_OK;
            SCA_LOGI("milo", "[milo] worker: volatile config applied");
#else
            SCA_LOGI("milo", "[milo] worker: saving secrets");
            save_ret = smart_home_secrets_set_miloco(save.host, save.port,
                                                     save.token);
            SCA_LOGI("milo", "[milo] worker: secrets save ret=%d",
                   save_ret);
#endif
            memset(&save, 0, sizeof(save));
            lock_state(service);
            service->save_result = save_ret;
            service->save_in_progress = false;
            service->save_completed = true;
            if (save_ret == AGENT_OK) {
                service->client_config = client_config;
                service->control_generation++;
                service->reachable = false;
                service->revision++;
            }
            poll_due = true;
        }
        if (service->config_dirty) {
            service->client_config = service->pending_config;
            service->control_generation++;
            service->reachable = false;
            service->revision++;
            service->config_dirty = false;
            poll_due = true;
        }
        if (!stop) has_control = take_control_locked(service, &control);
        unlock_state(service);

        if (stop) {
            break;
        }
        if (has_control) {
            process_control(service, &control, body, MILOCO_RESPONSE_BYTES);
            continue; /* 立即处理后续排队的控制请求。 */
        }

        /* 等控制请求或到点轮询。
         * 不使用 sem_timedwait(CLOCK_REALTIME)，改用 sca_sem_wait
         * 的相对超时 —— 语义等价且不依赖墙上时钟（板上无 RTC 时
         * CLOCK_REALTIME 不可靠）。 */
        if (sca_sem_wait(&service->wake, MILOCO_POLL_WAIT_MS) == SCA_OK) {
            continue;               /* 被控制请求或停机唤醒 */
        }
        poll_due = true;            /* 超时：到点轮询 */
    }

    smart_home_bulk_free(body);
    return;
}

void smart_home_miloco_kick_weather(smart_home_miloco_t *service)
{
    if (service) {
        /* 下一轮 worker 迭代 weather_poll_count++ 变 120，
         * 触发 >= 120 条件立即拉取天气 + HTTP Date 时间同步。 */
        service->weather_poll_count = 119;
    }
}

int smart_home_miloco_start(smart_home_miloco_t **service_out,
                            const smart_home_miloco_config_t *config)
{
    smart_home_miloco_t *service;
    int ret;

    if (!service_out || !config ||
        (config->host[0] && !smart_home_miloco_config_valid(config)) ||
        (!config->host[0] && config->port == 0)) {
        return AGENT_ERROR_INVALID;
    }
    if (*service_out) {
        return AGENT_ERROR_INVALID;
    }

    SCA_LOGI("milo", "[milo] start: enter host=%s", config->host);
    service = smart_home_bulk_alloc(sizeof(*service));
    if (!service) {
        return AGENT_ERROR_NOMEM;
    }
    memset(service, 0, sizeof(*service));
    snprintf(service->client_config.host,
             sizeof(service->client_config.host), "%s", config->host);
    service->client_config.port = config->port;
    snprintf(service->client_config.token,
             sizeof(service->client_config.token), "%s", config->token);

    ret = sca_mutex_init(&service->lock, NULL);
    if (ret != 0) {
        smart_home_bulk_free(service);
        return AGENT_ERROR;
    }
    if (sca_sem_init(&service->wake, "milo-wake", 0) != 0) {
        sca_mutex_destroy(&service->lock);
        smart_home_bulk_free(service);
        return AGENT_ERROR;
    }

    /* worker 线程走 sca_os：栈由创建方分配、绑核由 attr 描述。
     *
     * ★ 绑核策略 ★ 米家轮询绑到 CPU1，理由是 LVGL 主循环、
     *   DSI 扫描与触摸采样都在 CPU0，而米洛轮询每 5 秒要走 DNS + TCP 收包，
     *   放在 CPU0 会和 UI 抢核，表现为点击延迟。
     *   这里保留同样的决策：attr.cpu = 1。
     *   （ESP32-P4 是 SMP 双核；Edgi-Talk 的 M55 是单核，sca_os 会忽略该字段。）
     */
    {
        sca_thread_attr_t attr;

        memset(&attr, 0, sizeof(attr));
        attr.name        = "miloco";
        attr.fn          = miloco_worker;
        attr.arg         = service;
        attr.stack_bytes = MILOCO_WORKER_STACK_SIZE;
        attr.priority    = 9;
        attr.cpu         = 1;      /* 与 LVGL 主循环分核 */

        service->worker = sca_thread_create(&attr);
        ret = service->worker ? 0 : -1;
    }
    SCA_LOGI("milo", "[milo] start: thread create ret=%d", ret);
    if (ret != 0) {
        sca_sem_destroy(&service->wake);
        sca_mutex_destroy(&service->lock);
        smart_home_bulk_free(service);
        return AGENT_ERROR;
    }
    service->worker_started = true;
    snprintf(service->weather.city, sizeof(service->weather.city),
             "Shenzhen");
    service->weather_poll_count = 119;  /* 首轮立即拉取天气+SNTP */
    SCA_LOGI("milo", "INFO: [miloco] gateway worker started host=%s port=%u",
           config->host, (unsigned)config->port);
    *service_out = service;
    return AGENT_OK;
}

void smart_home_miloco_stop(smart_home_miloco_t **service_ptr)
{
    smart_home_miloco_t *service;

    if (!service_ptr || !*service_ptr) {
        return;
    }
    service = *service_ptr;
    *service_ptr = NULL;

    lock_state(service);
    service->stop_requested = true;
    unlock_state(service);
    sca_sem_post(&service->wake);

    if (service->worker_started && service->worker) {
        /* sca_thread_join 等线程函数返回；随后 destroy 回收栈与句柄 */
        if (sca_thread_join(service->worker, 5000) != SCA_OK) {
            SCA_LOGW("milo", "worker did not exit in 5s, forcing");
        }
        sca_thread_destroy(service->worker);
        service->worker = NULL;
    }
    sca_sem_destroy(&service->wake);
    sca_mutex_destroy(&service->lock);
    smart_home_bulk_free(service);
    SCA_LOGI("milo", "INFO: [miloco] gateway worker stopped");
}
