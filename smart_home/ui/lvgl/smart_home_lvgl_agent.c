/**
 * smart_home LVGL agent bridge.
 */

#include "smart_home/ui/lvgl/smart_home_lvgl_internal.h"

#include "smart_home/config/smart_home_config.h"
#include "smart_home/core/smart_home_cpu_debug.h"
#include "smart_home/core/smart_home_memory.h"
#include "smart_home/ui/smart_home_ui_event.h"
#include "smart_home/agent/smart_home_agent_diag.h"
#if SCA_ENABLE_ASR
#include "smart_home/voice/smart_home_asr.h"
#include "smart_home/voice/smart_home_voice_capture.h"
#endif
#if SCA_ENABLE_TTS
#include "smart_home/voice/smart_home_voice_play.h"
#endif

#include "port/sca_os.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Temporary BOX-3 memory-pressure A/B test: match the successful console
 * smart_home task stack instead of reserving 64 KiB for each chat worker. */
#define SMART_HOME_AGENT_WORKER_STACK_SIZE 32768u

typedef struct {
    smart_home_lvgl_t *ui;
    int ret;
    char output[SMART_HOME_OUTPUT_SIZE];
} agent_done_t;

typedef struct {
    smart_home_lvgl_t *ui;
    agent_done_t *done;
    struct ui_pending_item *completion;
    char input[SMART_HOME_INPUT_SIZE];
} agent_job_t;

typedef smart_home_ui_event_t ui_event_payload_t;

typedef enum {
    UI_PENDING_EVENT = 0,
    UI_PENDING_DONE,
#if SCA_ENABLE_ASR
    UI_PENDING_ASR_TEXT,
#endif
#if SCA_ENABLE_KWS
    UI_PENDING_KWS_WAKE,
#endif
} ui_pending_type_t;

#if SCA_ENABLE_ASR
typedef struct {
    char text[SMART_HOME_INPUT_SIZE];
    smart_home_voice_result_t result;
} ui_asr_payload_t;
#endif

#if SCA_ENABLE_KWS
typedef struct {
    float wake_score;
} ui_kws_payload_t;
#endif

typedef struct ui_pending_item {
    struct ui_pending_item *next;
    ui_pending_type_t type;
    union {
        ui_event_payload_t event;
        agent_done_t *done;
#if SCA_ENABLE_ASR
        ui_asr_payload_t asr;
#endif
#if SCA_ENABLE_KWS
        ui_kws_payload_t kws;
#endif
    } data;
} ui_pending_item_t;

#if SCA_ENABLE_DEBUG_LOG
static void agent_worker_log_context(const char *stage,
                                     const smart_home_agent_app_t *app)
{
    struct sched_param sched_param;
    struct stackinfo_s stack_info;
    smart_home_network_status_t network;
#ifndef CONFIG_SMART_HOME_DEMO_OFFLINE_UI
    struct in_addr address;
    char address_text[INET_ADDRSTRLEN] = "none";
#else
    char address_text[] = "offline";
#endif
    int policy = -1;
    int priority = -1;
    int stack_ret;
    int probe_ret = SMART_HOME_NETWORK_ERR_NO_IP;

    memset(&sched_param, 0, sizeof(sched_param));
    if (0) {  /* RT-Thread 无 pthread_getschedparam，跳过调度调试日志 */
        priority = sched_param.sched_priority;
    }

    memset(&stack_info, 0, sizeof(stack_info));
    stack_ret = nxsched_get_stackinfo(0, &stack_info);
    printf("[agent_worker] %s tid=%d policy=%d priority=%d "
           "stack_capacity=%zu stack_query=%d\n",
           stage,
           (int)gettid(),
           policy,
           priority,
           stack_ret == 0 ? stack_info.adj_stack_size :
                            (size_t)SMART_HOME_AGENT_WORKER_STACK_SIZE,
           stack_ret);

    if (!app) {
        printf("[agent_worker] %s network app=none\n", stage);
        return;
    }

    network = app->system_status.network_status;
#ifndef CONFIG_SMART_HOME_DEMO_OFFLINE_UI
    memset(&address, 0, sizeof(address));
    if (network.ifname) {
        netlib_get_ipv4addr(network.ifname, &address);
        if (address.s_addr != 0) {
            inet_ntop(AF_INET, &address, address_text, sizeof(address_text));
        }

        probe_ret = smart_home_network_probe(&network);
    }
#else
    probe_ret = smart_home_network_probe(&network);
#endif

    printf("[agent_worker] %s network if=%s ip=%s probe=%d "
           "init=%d ip_status=%d dns_status=%d online=%d\n",
           stage,
           network.ifname ? network.ifname : "none",
           address_text,
           probe_ret,
           network.init_status,
           network.ip_status,
           network.dns_status,
           network.online ? 1 : 0);
}
#endif

static int agent_ui_enqueue(smart_home_lvgl_t *ui, ui_pending_item_t *item)
{
    int ret;

    if (!ui || !item) {
        return -1;
    }

    ret = sca_mutex_lock(&ui->pending_mutex);
    if (ret != 0) {
        return -ret;
    }

    item->next = NULL;
    if (ui->pending_tail) {
        ((ui_pending_item_t *)ui->pending_tail)->next = item;
    } else {
        ui->pending_head = item;
    }
    ui->pending_tail = item;
    sca_mutex_unlock(&ui->pending_mutex);
    return 0;
}

static const char *chat_request_error(int code, int has_operations)
{
    if (code == AGENT_ERROR_CAPACITY)
        return smart_home_agent_capacity_message(has_operations, 0);
    if (has_operations) {
        return "Chat incomplete. Some actions may have run - check devices.";
    }
    switch (code) {
    case AGENT_ERROR_TIMEOUT:
        return "Reply timed out, try again.";
    case AGENT_ERROR_IO:
        return "Cannot reach service. Check network and retry.";
    case AGENT_ERROR_MODEL_FAILED:
    case AGENT_ERROR_MODEL_PARSE:
    case AGENT_ERROR_MODEL_UNAVAILABLE:
        return "Assistant unavailable. Check model settings.";
    case AGENT_ERROR_AUTH:
        return "Auth failed. Check the API key.";
    case AGENT_ERROR_MODEL_RATE_LIMIT:
        return "Rate limited, try again later.";
    case AGENT_ERROR_CONTEXT_OVERFLOW:
        return "Too long. Start a new chat.";
    case AGENT_ERROR_POLICY_DENIED:
        return "Not permitted. Check tool permissions in Settings.";
    case AGENT_ERROR_CANCELLED:
        return "Request cancelled.";
    case AGENT_ERROR_NOMEM:
        return "Device busy, try again later.";
    default:
        return "Cannot complete, try again.";
    }
}

static void agent_done_apply(agent_done_t *done)
{
    smart_home_lvgl_t *ui = done ? done->ui : NULL;

    if (!done) {
        return;
    }

    if (ui) {
        if (ui->agent_worker_active) {
            sca_thread_join(ui->agent_worker, 10000);
        sca_thread_destroy(ui->agent_worker);
        ui->agent_worker = NULL;
            ui->agent_worker_active = 0;
        }
        ui->request_inflight = 0;
        if (ui->chat_send_btn) {
            lv_obj_remove_state(ui->chat_send_btn, LV_STATE_DISABLED);
        }
#if SCA_ENABLE_DEBUG_LOG
        printf("[agent] reply(%d): %.200s\n",
               done->ret,
               done->output[0] ? done->output : "(empty)");
#endif
        if (done->ret == AGENT_OK || done->ret == AGENT_ERROR_TRUNCATED) {
            smart_home_lvgl_finish_thinking(ui);
            smart_home_lvgl_append_msg_bubble(ui, done->output, 0);
            smart_home_lvgl_refresh_cards(ui);
            if (done->ret == AGENT_ERROR_TRUNCATED)
                smart_home_lvgl_append_error_bubble(ui, "Reply truncated to fit the display.");
#if SCA_ENABLE_TTS
            /* 语音播报在独立 worker 中合成与播放；开关关闭或服务
             * 未就绪时静默跳过，不影响气泡展示路径。 */
            if (done->ret != AGENT_OK) {
                printf("[voice] announce skipped (ret=%d)\n", done->ret);
            } else if (!voice_play_announce_enabled()) {
                /* 开关关闭时此前完全静默，排查时无从判断是"没触发"还是
                 * "触发了但无声"。这里必须留一行。 */
                printf("[voice] announce skipped (voice.json enabled=0)\n");
            } else {
                int enq = voice_play_text(done->output);
                printf("[voice] announce enqueue ret=%d text_len=%zu\n",
                       enq, strlen(done->output));
            }
#endif
        } else {
            smart_home_lvgl_finish_thinking(ui);
            if (done->output[0] && ui->app &&
                ui->app->system_status.session_status != AGENT_OK) {
                smart_home_lvgl_append_msg_bubble(ui, done->output, 0);
                smart_home_lvgl_append_error_bubble(ui,
                    done->ret == AGENT_ERROR_CAPACITY ?
                    "Reply shown, but conversation capacity was exceeded. Verify device state." :
                    "Shown, but history save failed. Verify device state.");
                smart_home_lvgl_refresh_cards(ui);
            } else {
                smart_home_lvgl_append_error_bubble(ui,
                    done->ret == AGENT_ERROR_CAPACITY ?
                    smart_home_agent_capacity_message(ui->chat_trace_tool_count > 0,
                        ui->app && ui->app->system_status.session_status == AGENT_ERROR_CAPACITY) :
                    done->output[0] ? done->output :
                    chat_request_error(done->ret, ui->chat_trace_tool_count > 0));
            }
        }
        /* Update status hint */
        if (ui->chat_status) {
            lv_obj_add_flag(ui->chat_status, LV_OBJ_FLAG_HIDDEN);
        }
        /* Force display refresh so the response appears immediately
         * without waiting for a user touch event. */
        lv_refr_now(NULL);
    }

    free(done);
}

static void agent_worker_main(void *arg)
{
    char stack_marker;
    agent_job_t *job = (agent_job_t *)arg;
    agent_done_t *done;
    ui_pending_item_t *item;

    if (!job) {
        return;
    }

    smart_home_cpu_debug_log("lvgl-agent-worker-start");
    smart_home_memory_log_region("lvgl-agent-worker-stack", &stack_marker);

#if SCA_ENABLE_DEBUG_LOG
    agent_worker_log_context("start", job->ui ? job->ui->app : NULL);
#endif

    done = job->done;
    smart_home_cpu_debug_log("lvgl-agent-before-agent");
    done->ret = smart_home_agent_run(job->ui->app,
                                     job->input,
                                     done->output,
                                     sizeof(done->output));
#if SCA_ENABLE_DEBUG_LOG
    agent_worker_log_context("done", job->ui ? job->ui->app : NULL);
#endif
    item = job->completion;
    item->type = UI_PENDING_DONE;
    item->data.done = done;
    if (agent_ui_enqueue(job->ui, item) == 0) {
        free(job);
        return;
    }
    free(item);

    free(done);
    free(job);
}

int smart_home_lvgl_submit_agent_job(smart_home_lvgl_t *ui, const char *text)
{
    agent_job_t *job;
    agent_done_t *done;
    int ret;

    if (!ui || !ui->app || !text || !text[0]) {
        return AGENT_ERROR_INVALID;
    }

    if (ui->request_inflight) {
        smart_home_lvgl_append_error_bubble(ui, "Still working on the last message.");
        return AGENT_ERROR_BUSY;
    }

    job = (agent_job_t *)calloc(1, sizeof(*job));
    if (!job) {
        smart_home_lvgl_append_error_bubble(ui, "Device busy, try again later.");
        return AGENT_ERROR_NOMEM;
    }

    done = (agent_done_t *)calloc(1, sizeof(*done));
    if (!done) {
        free(job);
        smart_home_lvgl_append_error_bubble(ui, "Device busy, try again later.");
        return AGENT_ERROR_NOMEM;
    }

    job->completion = calloc(1, sizeof(*job->completion));
    if (!job->completion) {
        free(done);
        free(job);
        return AGENT_ERROR_NOMEM;
    }
    job->ui = ui;
    job->done = done;
    strncpy(job->input, text, sizeof(job->input) - 1);
    done->ui = ui;
    ui->request_inflight = 1;

    /* 走 sca_os 线程抽象：栈由 sca_thread_create 内部从堆分配，
     * 不再需要 smart_home_bulk_alloc + 手工对齐那一套。
     * 线程栈由 sca_os 统一处理。 */
    {
        sca_thread_attr_t attr;

        memset(&attr, 0, sizeof(attr));
        attr.name        = "lvgl-agent";
        attr.fn          = agent_worker_main;
        attr.arg         = job;
        attr.stack_bytes = SMART_HOME_AGENT_WORKER_STACK_SIZE;
        attr.priority    = 8;
        attr.cpu         = 0xFF;

        ui->agent_worker = sca_thread_create(&attr);
        ret = ui->agent_worker ? 0 : -1;
    }
    if (ret != 0) {
        ui->request_inflight = 0;
        free(job->completion);
        free(done);
        free(job);
        smart_home_lvgl_append_error_bubble(ui, "Cannot send, try again.");
        return AGENT_ERROR;
    }

    ui->agent_worker_active = 1;
    return AGENT_OK;
}

#if SCA_ENABLE_ASR
/* VAD 关闭时保持原有"录满上限"行为。 */
#if SCA_ENABLE_VAD
#  define SMART_HOME_ASR_AUTOSTOP_MS CONFIG_SMART_HOME_VAD_AUTOSTOP_SILENCE_MS
#else
#  define SMART_HOME_ASR_AUTOSTOP_MS 0
#endif

/* ASR worker：录音 + MiMo 云端识别，结果经 pending 队列回投 LVGL 线程。
 * 本函数绝不调用任何 LVGL API。 */
static void *asr_worker_main(void *arg)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)arg;
    ui_pending_item_t *item;
    uint8_t *pcm = NULL;
    size_t bytes = 0;
    char text[SMART_HOME_INPUT_SIZE];
    smart_home_voice_result_t result = {AGENT_ERROR, 0, SMART_HOME_VOICE_CAPTURE};
    int ret;

    text[0] = '\0';

    ret = voice_capture_record_vad(CONFIG_SMART_HOME_ASR_AUDIO_MAX_SECONDS,
                                   &ui->asr_abort,
                                   SMART_HOME_ASR_AUTOSTOP_MS,
                                   &pcm, &bytes);
    if (ret == 0 && bytes >= 3200)
      {
        /* 至少 0.1 s（3200 字节）才值得上传识别。 */
        ret = smart_home_asr_recognize_ex(pcm, bytes, text, sizeof(text), &result);
      }
    else if (ret == 0)
      {
        /* 录音成功但不足 0.1 s：不是云端失败，按采集阶段报错。 */
        ret = AGENT_ERROR_INVALID;
        result.code = AGENT_ERROR_INVALID;
        result.stage = SMART_HOME_VOICE_CAPTURE;
      }

    /* asr_abort 的语义是"再点一次结束录音并发送"，不是取消：
     * 录音循环已经提前退出，这里照常上传已录到的内容。 */

    if (pcm != NULL)
      {
        smart_home_bulk_free(pcm);
      }

    item = (ui_pending_item_t *)calloc(1, sizeof(*item));
    if (item != NULL)
      {
        item->type = UI_PENDING_ASR_TEXT;
        item->data.asr.result = result;
        strncpy(item->data.asr.text, text, sizeof(item->data.asr.text) - 1);
        item->data.asr.text[sizeof(item->data.asr.text) - 1] = '\0';
        if (agent_ui_enqueue(ui, item) != 0)
          {
            free(item);
          }
      }

    return NULL;
}

int smart_home_lvgl_submit_asr_job(smart_home_lvgl_t *ui)
{
    sca_thread_attr_t attr;
    int ret;

    if (!ui) return AGENT_ERROR_INVALID;

    if (ui->asr_worker_active) {
        sca_thread_join(ui->asr_worker, 10000);
        sca_thread_destroy(ui->asr_worker);
        ui->asr_worker = NULL;
        ui->asr_worker_active = 0;
    }

    memset(&attr, 0, sizeof(attr));
    attr.name        = "lvgl-asr";
    attr.fn          = asr_worker_main;
    attr.arg         = ui;
    attr.stack_bytes = CONFIG_SCA_STACK_VOICE;
    attr.priority    = 9;
    attr.cpu         = 0xFF;

    ui->asr_worker = sca_thread_create(&attr);
    if (!ui->asr_worker) return AGENT_ERROR;

    ui->asr_worker_active = 1;
    return AGENT_OK;
}
#endif

#if SCA_ENABLE_KWS
/* KWS worker 线程调用：投递唤醒事件，不做任何 UI 操作。 */
int smart_home_lvgl_post_kws_wake(smart_home_lvgl_t *ui, float score)
{
    ui_pending_item_t *item;

    if (!ui) {
        return AGENT_ERROR_INVALID;
    }

    item = (ui_pending_item_t *)calloc(1, sizeof(*item));
    if (item == NULL) {
        return AGENT_ERROR_NOMEM;
    }

    item->type = UI_PENDING_KWS_WAKE;
    item->data.kws.wake_score = score;
    if (agent_ui_enqueue(ui, item) != 0) {
        free(item);
        return AGENT_ERROR;
    }

    return AGENT_OK;
}
#endif

static void ui_event_apply(smart_home_lvgl_t *ui,
                           const ui_event_payload_t *payload)
{
    if (!ui || !payload) {
        return;
    }

    switch (payload->type) {
    case AGENT_EVENT_MODEL_BEGIN:
        smart_home_lvgl_show_thinking(ui);
        break;
    case AGENT_EVENT_TOOL_BEGIN:
        smart_home_lvgl_trace_tool(ui,
                                   payload->tool_name,
                                   payload->tool_call_id,
                                   -1);
        break;
    case AGENT_EVENT_TOOL_END:
        if (strcmp(payload->tool_name, "set_light") == 0 ||
            strcmp(payload->tool_name, "set_ac") == 0 ||
            strcmp(payload->tool_name, "run_scene") == 0) {
            smart_home_lvgl_refresh_cards(ui);
        }
        smart_home_lvgl_trace_tool(ui,
                                   payload->tool_name,
                                   payload->tool_call_id,
                                   payload->status == AGENT_OK ? 1 : 0);
        break;
    case AGENT_EVENT_TURN_END:
        /* Completion owns the final bubble; terminal events only end activity. */
        smart_home_lvgl_finish_thinking(ui);
        break;
    default:
        break;
    }

}

void smart_home_lvgl_event_cb(void *user_data, const agent_event_t *event)
{
    smart_home_lvgl_t *ui = (smart_home_lvgl_t *)user_data;
    ui_pending_item_t *item;

    if (!ui || !event) {
        return;
    }

    item = (ui_pending_item_t *)calloc(1, sizeof(*item));
    if (!item) {
        return;
    }

    item->type = UI_PENDING_EVENT;
    smart_home_ui_event_copy(&item->data.event, event);

    if (agent_ui_enqueue(ui, item) != 0) {
        free(item);
    }
}

void smart_home_lvgl_process_pending(smart_home_lvgl_t *ui)
{
    ui_pending_item_t *item;

    if (!ui) {
        return;
    }

#if SCA_ENABLE_TTS
    {
        smart_home_voice_result_t result;
        if (voice_play_take_error(&result))
            smart_home_lvgl_append_error_bubble(ui, smart_home_voice_result_text(&result));
    }
#endif

    for (;;) {
        if (sca_mutex_lock(&ui->pending_mutex) != 0) {
            return;
        }

        item = (ui_pending_item_t *)ui->pending_head;
        if (item) {
            ui->pending_head = item->next;
            if (!ui->pending_head) {
                ui->pending_tail = NULL;
            }
        }
        sca_mutex_unlock(&ui->pending_mutex);

        if (!item) {
            return;
        }

        if (item->type == UI_PENDING_DONE) {
            agent_done_apply(item->data.done);
        }
#if SCA_ENABLE_ASR
        else if (item->type == UI_PENDING_ASR_TEXT) {
            if (ui->asr_worker_active) {
                sca_thread_join(ui->asr_worker, 10000);
        sca_thread_destroy(ui->asr_worker);
        ui->asr_worker = NULL;
                ui->asr_worker_active = 0;
            }

            if (item->data.asr.result.code == AGENT_OK && item->data.asr.text[0]) {
                smart_home_lvgl_chat_send_text(ui, item->data.asr.text);
            } else {
                smart_home_lvgl_append_error_bubble(
                    ui, smart_home_voice_result_text(&item->data.asr.result));
            }
            smart_home_lvgl_chat_asr_finish(ui);
        }
#endif
#if SCA_ENABLE_KWS
        else if (item->type == UI_PENDING_KWS_WAKE) {
            smart_home_voice_session_on_wake(ui, item->data.kws.wake_score);
        }
#endif
        else {
            ui_event_apply(ui, &item->data.event);
        }
        free(item);
    }
}

void smart_home_lvgl_discard_pending(smart_home_lvgl_t *ui)
{
    ui_pending_item_t *item;
    ui_pending_item_t *next;

    if (!ui || sca_mutex_lock(&ui->pending_mutex) != 0) {
        return;
    }

    item = (ui_pending_item_t *)ui->pending_head;
    ui->pending_head = NULL;
    ui->pending_tail = NULL;
    sca_mutex_unlock(&ui->pending_mutex);

    while (item) {
        next = item->next;
        if (item->type == UI_PENDING_DONE) {
            free(item->data.done);
        }
        free(item);
        item = next;
    }
}
