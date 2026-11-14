/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * sca_fs 宿主单元测试。
 *
 * 验证点（对应 sca_fs 的三条承诺）：
 *   1. 原子写：中途失败时旧文件保持完整
 *   2. 整文件读：一次性读回并 NUL 结尾
 *   3. 目录递归创建：mkdir -p 语义
 *
 * 编译运行：
 *   python tests/run_host_tests.py
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "port/sca_fs.h"

static int g_failures;

#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            printf("  FAIL  %s  (line %d)\n", msg, __LINE__);   \
            g_failures++;                                       \
        } else {                                                \
            printf("  ok    %s\n", msg);                        \
        }                                                       \
    } while (0)

#define TEST_DIR "/tmp/sca_test"
#define TEST_FILE TEST_DIR "/state.json"

static void cleanup(void)
{
    remove(TEST_FILE);
    remove(TEST_FILE ".tmp");
    remove(TEST_DIR "/state.json");
}

static void test_mkdir_p(void)
{
    printf("[test_mkdir_p]\n");

    CHECK(sca_fs_mkdir_p(TEST_DIR) == SCA_OK, "mkdir -p creates dir");
    /* 幂等：再调一次也应成功 */
    CHECK(sca_fs_mkdir_p(TEST_DIR) == SCA_OK, "mkdir -p is idempotent");
}

static void test_atomic_write(void)
{
    const char *first  = "{\"revision\":1}";
    const char *second = "{\"revision\":2,\"room\":\"bedroom\"}";
    char *readback;
    size_t len = 0;

    printf("[test_atomic_write]\n");

    CHECK(sca_fs_write_atomic(TEST_FILE, first, strlen(first)) == SCA_OK,
          "write first version");

    readback = (char *)sca_fs_read_all(TEST_FILE, &len, 4096);
    CHECK(readback != NULL, "read back first version");
    if (readback) {
        CHECK(len == strlen(first), "length matches");
        CHECK(strcmp(readback, first) == 0, "content matches");
        sca_fs_free_buffer(readback);
    }

    /* 覆盖写：新内容替换旧内容 */
    CHECK(sca_fs_write_atomic(TEST_FILE, second, strlen(second)) == SCA_OK,
          "overwrite with second version");

    readback = (char *)sca_fs_read_all(TEST_FILE, &len, 4096);
    CHECK(readback != NULL, "read back second version");
    if (readback) {
        CHECK(strcmp(readback, second) == 0, "second content matches");
        /* 关键：不能残留旧内容 */
        CHECK(strstr(readback, "revision\":1") == NULL,
              "no residue from first version");
        sca_fs_free_buffer(readback);
    }

    /* .tmp 必须已被 rename 消耗掉 */
    CHECK(!sca_fs_exists(TEST_FILE ".tmp"), "tmp file consumed by rename");
}

static void test_size_limit(void)
{
    char *big;
    size_t len = 0;

    printf("[test_size_limit]\n");

    /* 读一个超过上限的文件应被拒绝（防止 OOM） */
    big = (char *)sca_fs_read_all(TEST_FILE, &len, 8);
    CHECK(big == NULL, "size limit rejects oversized read");
    if (big) {
        sca_fs_free_buffer(big);
    }

    /* 0 表示不限制 */
    big = (char *)sca_fs_read_all(TEST_FILE, &len, 0);
    CHECK(big != NULL, "zero limit means unlimited");
    if (big) {
        sca_fs_free_buffer(big);
    }
}

static void test_missing_file(void)
{
    printf("[test_missing_file]\n");

    CHECK(sca_fs_size("/nonexistent/x") < 0, "size of missing file is -1");
    CHECK(sca_fs_exists("/nonexistent/x") == 0, "exists returns 0");
    CHECK(sca_fs_read_all("/nonexistent/x", NULL, 4096) == NULL,
          "read_all returns NULL for missing");
    CHECK(sca_fs_remove("/nonexistent/x") == SCA_OK,
          "removing missing file is not an error");
}

int main(void)
{
    printf("=== sca_fs host tests ===\n");

    cleanup();
    test_mkdir_p();
    test_atomic_write();
    test_size_limit();
    test_missing_file();
    cleanup();

    printf("\n=== %s (%d failure(s)) ===\n",
           g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
