#include "port/sca_config.h"
#include "smart_home/agent/smart_home_session.h"
#include "smart_home/agent/smart_home_agent_diag.h"

#if SCA_ENABLE_SESSION_PERSIST
#include "port/sca_fs.h"
#include "smart_home/config/smart_home_config.h"
#include "smart_home/core/smart_home_memory.h"
#include "agent_posix_file_store.h"
#include <agent_session_jsonl_files.h>
#include <stdint.h>
#include <string.h>

struct smart_home_session {
    agent_session_jsonl_t jsonl;
    smart_home_session_trace_t trace;
    agent_session_storage_t storage;
    agent_posix_file_store_t port;
    agent_file_store_t store;
    agent_session_jsonl_files_t files;
    char paths[512];
    char name[32];
    char id[8];
    char write_line[SCA_SESSION_LINE_BYTES];
    char read_line[SCA_SESSION_LINE_BYTES];
    char decoded[SCA_SESSION_BYTES];
    int tokens[16384];
    agent_message_view_t messages[AGENT_MAX_PROJECTED_MESSAGES];
    agent_tool_call_view_t calls[AGENT_MAX_PROJECTED_MESSAGES];
};

static agent_string_view_t session_id(void)
{
    return agent_string_view(SMART_HOME_SESSION_ID, sizeof(SMART_HOME_SESSION_ID) - 1u);
}

/* cagent 的 posix_size() 靠 errno==ENOENT 判断文件缺失，而 DFS v1 的
 * stat() 把错误码写进 RT-Thread 线程 errno、与 newlib errno 不共享，
 * 结果“文件不存在”被误报成 AGENT_ERROR_IO。这里用 stat 返回值判定，
 * 并在首轮先建空文件，供 turn_open 的 size 查询返回 0。 */
static rt_bool_t session_file_exists(void)
{
    return sca_fs_exists(SMART_HOME_SESSION_DIR "/" SMART_HOME_SESSION_FILE) ? RT_TRUE
                                                                             : RT_FALSE;
}

static int session_file_touch(void)
{
    return sca_fs_write_atomic(SMART_HOME_SESSION_DIR "/" SMART_HOME_SESSION_FILE, "", 0) ==
                   SCA_OK
               ? AGENT_OK
               : AGENT_ERROR_IO;
}

int smart_home_session_open(smart_home_session_t **out, agent_session_storage_t *storage,
                             smart_home_agent_diag_t *diag)
{
    smart_home_session_t *session;
    agent_session_jsonl_config_t jsonl = {0};
    int ret;
    if (!out || !storage) return AGENT_ERROR_INVALID;
    *out = NULL;
    if (CONFIG_SMART_HOME_SESSION_RETAIN_TURNS < 2u ||
        CONFIG_SMART_HOME_SESSION_FILE_BYTES <= SCA_SESSION_LINE_BYTES)
        return AGENT_ERROR_INVALID;
    if (sca_fs_mkdir_p(SMART_HOME_SESSION_DIR) != SCA_OK) return AGENT_ERROR_IO;
    session = smart_home_bulk_alloc(sizeof(*session));
    if (!session) return AGENT_ERROR_NOMEM;
    memset(session, 0, sizeof(*session));
    {
        const agent_posix_file_store_config_t config = {
            SMART_HOME_SESSION_DIR, session->paths, sizeof(session->paths), false, false
        };
        ret = agent_posix_file_store_init(&session->port, &config, &session->store);
    }
    if (ret != AGENT_OK) goto fail;
    ret = agent_session_jsonl_files_init(&session->files, &session->store,
                                        session->name, sizeof(session->name), &jsonl);
    if (ret != AGENT_OK) goto fail;
    jsonl.write_line = session->write_line;
    jsonl.write_line_capacity = sizeof(session->write_line);
    jsonl.read_line = session->read_line;
    jsonl.read_line_capacity = sizeof(session->read_line);
    jsonl.decoded = session->decoded;
    jsonl.decoded_capacity = sizeof(session->decoded);
    jsonl.token_buffer = session->tokens;
    jsonl.token_buffer_bytes = sizeof(session->tokens);
    jsonl.read_messages = session->messages;
    jsonl.read_message_capacity = AGENT_MAX_PROJECTED_MESSAGES;
    jsonl.read_calls = session->calls;
    jsonl.read_call_capacity = AGENT_MAX_PROJECTED_MESSAGES;
    jsonl.session_id = session->id;
    jsonl.session_id_capacity = sizeof(session->id);
    ret = agent_session_jsonl_init(&session->jsonl, &jsonl);
    if (ret == AGENT_OK) ret = smart_home_session_trace_bind(&session->trace, &session->jsonl, diag, &session->storage);
    if (ret != AGENT_OK) goto fail;
    *storage = session->storage;
    *out = session;
    return AGENT_OK;
fail:
    smart_home_bulk_free(session);
    return ret;
}

void smart_home_session_close(smart_home_session_t **session)
{
    if (session && *session) {
        memset(*session, 0, sizeof(**session));
        smart_home_bulk_free(*session);
        *session = NULL;
    }
}

int smart_home_session_prepare(smart_home_session_t *session)
{
    const agent_string_view_t name = agent_string_view(SMART_HOME_SESSION_FILE,
                                                       sizeof(SMART_HOME_SESSION_FILE) - 1u);
    uint64_t bytes = 0u;
    size_t end, start, count = 0u;
    char *data;
    bool published;
    int ret;
    rt_bool_t exists;
    if (!session) return AGENT_ERROR_INVALID;
    if (session->jsonl.active) return AGENT_ERROR_BUSY;
    exists = session_file_exists();
    if (!exists)
    {
        ret = session_file_touch();
        return ret;
    }
    ret = agent_file_size(&session->store, name, &bytes);
    if (ret == AGENT_ERROR_NOT_FOUND) return AGENT_OK;
    if (ret != AGENT_OK || !bytes) return ret;
    if (bytes > CONFIG_SMART_HOME_SESSION_FILE_BYTES) {
        printf("[agent_diag] stage=session.file required=%llu capacity=%u code=%d\n",
               (unsigned long long)bytes, CONFIG_SMART_HOME_SESSION_FILE_BYTES, AGENT_ERROR_CAPACITY);
        smart_home_agent_diag_error(session->trace.diag, "session.file", AGENT_ERROR_CAPACITY);
        return AGENT_ERROR_CAPACITY;
    }
    data = smart_home_bulk_alloc((size_t)bytes);
    if (!data) return AGENT_ERROR_NOMEM;
    ret = agent_file_read_exact(&session->store, name, 0u, data, (size_t)bytes);
    if (ret != AGENT_OK) goto done;
    end = (size_t)bytes;
    while (end && data[end - 1u] != '\n') --end;
    start = end;
    while (start && count < CONFIG_SMART_HOME_SESSION_RETAIN_TURNS - 1u) {
        size_t candidate = start - 1u;
        while (candidate && data[candidate - 1u] != '\n') --candidate;
        if (end - candidate > CONFIG_SMART_HOME_SESSION_FILE_BYTES -
                              SCA_SESSION_LINE_BYTES) break;
        start = candidate;
        ++count;
    }
    if (start || end != bytes)
        ret = agent_file_replace(&session->store, name, data + start, end - start, &published);
done:
    smart_home_bulk_free(data);
    return ret;
}

int smart_home_session_visit(smart_home_session_t *session,
                             agent_session_visit_fn visit, void *context)
{
    agent_session_storage_t storage;
    uint64_t bytes;
    int ret;
    if (!session || !visit) return AGENT_ERROR_INVALID;
    if (session->jsonl.active) return AGENT_ERROR_BUSY;
    ret = agent_file_size(&session->store,
                         agent_string_view(SMART_HOME_SESSION_FILE,
                                           sizeof(SMART_HOME_SESSION_FILE) - 1u), &bytes);
    if (ret == AGENT_ERROR_NOT_FOUND) return AGENT_OK;
    if (ret != AGENT_OK) return ret;
    if (bytes > CONFIG_SMART_HOME_SESSION_FILE_BYTES) {
        printf("[agent_diag] stage=session.file required=%llu capacity=%u code=%d\n",
               (unsigned long long)bytes, CONFIG_SMART_HOME_SESSION_FILE_BYTES, AGENT_ERROR_CAPACITY);
        smart_home_agent_diag_error(session->trace.diag, "session.file", AGENT_ERROR_CAPACITY);
        return AGENT_ERROR_CAPACITY;
    }
    storage = session->storage;
    return storage.ops.recent(storage.context, session_id(),
                              CONFIG_SMART_HOME_SESSION_RETAIN_TURNS, visit, context);
}
#endif
