/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 平台配置收敛头。
 *
 * ★ 这是整个 port 层最重要的一个文件 ★
 *
 * 业务层不应直接依赖平台配置头。为了让业务层零平台依赖，这里做两件事：
 *
 *   1. 单向转发 rtconfig.h，让业务代码只需 include 本文件；
 *   2. 把 CONFIG_SMART_HOME_* 命名的宏，重映射成 SCA_* 命名。
 *
 * 为什么重命名而不是沿用原名：
 *   CONFIG_ 前缀在 RT-Thread 里由 rtconfig.h 独占，如果业务层继续写
 *   CONFIG_SMART_HOME_FOO，会和 menuconfig 生成的符号混在一起，无法
 *   区分"这个宏来自 Kconfig"还是"这个宏是应用自定的默认值"。
 *   统一改成 SCA_ 前缀后，凡是 SCA_ 开头的都可以安全地写 #ifndef 兜底。
 */

#ifndef __SCA_CONFIG_H__
#define __SCA_CONFIG_H__

/* ── 平台头（唯一允许 include rtconfig.h 的地方）───────────── */
#include <rtconfig.h>
#include "port/sca_agent_profile.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── 运行期路径 ────────────────────────────────────────── */

#ifndef SCA_ROOT_DIR
#define SCA_ROOT_DIR            "/littlefs"
#endif

#ifndef CONFIG_SMART_HOME_DEMO_DATA_ROOT
#define CONFIG_SMART_HOME_DEMO_DATA_ROOT SCA_ROOT_DIR
#endif

#ifndef SCA_RES_DIR
#define SCA_RES_DIR             SCA_ROOT_DIR "/res"
#endif

#define SCA_SKILL_DIR           SCA_RES_DIR "/skills"
#define SCA_FONT_DIR            SCA_RES_DIR "/fonts"
#define SCA_ICON_DIR            SCA_RES_DIR "/icons"

#define SCA_STATE_DIR           SCA_ROOT_DIR "/smart_home"
#define SCA_BACKENDS_PATH       SCA_STATE_DIR "/backends.json"
#define SCA_SETTINGS_PATH       SCA_STATE_DIR "/settings.json"
#define SCA_SECRETS_PATH        SCA_STATE_DIR "/secrets.json"
#define SCA_STATE_PATH          SCA_STATE_DIR "/state.json"
#define SCA_REMINDER_PATH       SCA_STATE_DIR "/reminders.json"
#define SCA_SESSION_DIR         SCA_STATE_DIR "/sessions"

/* ── 功能开关（由 smart_home/Kconfig 驱动）─────────────── */

#ifdef CONFIG_SCA_MILOCO
#define SCA_ENABLE_MILOCO       1
#else
#define SCA_ENABLE_MILOCO       0
#endif

#ifdef CONFIG_SCA_REMINDERS
#define SCA_ENABLE_REMINDERS    1
#else
#define SCA_ENABLE_REMINDERS    0
#endif

#ifdef CONFIG_SCA_USER_MEMORY
#define SCA_ENABLE_USER_MEMORY  1
#else
#define SCA_ENABLE_USER_MEMORY  0
#endif

#ifdef CONFIG_SCA_VOICE_TTS
#define SCA_ENABLE_TTS          1
#else
#define SCA_ENABLE_TTS          0
#endif

#ifdef CONFIG_SCA_VOICE_ASR
#define SCA_ENABLE_ASR          1
#else
#define SCA_ENABLE_ASR          0
#endif

#ifdef CONFIG_SCA_VAD
#define SCA_ENABLE_VAD          1
#else
#define SCA_ENABLE_VAD          0
#endif

#ifdef CONFIG_SCA_KWS
#define SCA_ENABLE_KWS          1
#else
#define SCA_ENABLE_KWS          0
#endif

#ifdef CONFIG_SCA_CAMERA
#define SCA_ENABLE_CAMERA       1
#else
#define SCA_ENABLE_CAMERA       0
#endif

#ifdef CONFIG_SCA_UI_LVGL
#define SCA_ENABLE_LVGL         1
#else
#define SCA_ENABLE_LVGL         0
#endif

#ifdef CONFIG_SCA_DEBUG_LOG
#define SCA_ENABLE_DEBUG_LOG    1
#else
#define SCA_ENABLE_DEBUG_LOG    0
#endif

#ifdef CONFIG_SCA_SESSION_PERSIST
#define SCA_ENABLE_SESSION_PERSIST 1
#else
#define SCA_ENABLE_SESSION_PERSIST 0
#endif

/* ── 容量参数（带安全默认值，允许 Kconfig 覆盖）────────── */

#ifndef CONFIG_SCA_STACK_MAIN
#define SCA_STACK_MAIN          (16 * 1024)
#endif

#ifndef CONFIG_SCA_PRIO_MAIN
#define SCA_PRIO_MAIN           10
#endif

#ifndef CONFIG_SCA_STACK_AGENT
#define SCA_STACK_AGENT         (16 * 1024)
#endif

#ifndef CONFIG_SCA_STACK_MILOCO
#define SCA_STACK_MILOCO        (12 * 1024)
#endif

#ifndef CONFIG_SCA_STACK_VOICE
#define SCA_STACK_VOICE         (8 * 1024)
#endif

#ifndef CONFIG_SCA_STACK_CAMERA
#define SCA_STACK_CAMERA        (6 * 1024)
#endif

#define SCA_MODEL_REQ_BYTES     CONFIG_SMART_HOME_MODEL_REQUEST_BUFFER_SIZE

#define SCA_MODEL_RSP_BYTES     CONFIG_SMART_HOME_MODEL_RESPONSE_BUFFER_SIZE

#define SCA_SESSION_TURNS       CONFIG_SMART_HOME_SESSION_RETAIN_TURNS

#define SCA_SESSION_BYTES       CONFIG_SMART_HOME_SESSION_PAYLOAD_BYTES

#define SCA_SESSION_TURNS_PROJECTED  CONFIG_SMART_HOME_SESSION_HISTORY_TURNS

#define SCA_SESSION_LINE_BYTES  CONFIG_SMART_HOME_SESSION_LINE_BYTES

#define SCA_OUTPUT_BYTES        (CONFIG_AGENT_MAX_MODEL_OUTPUT_BYTES + 1u)

#ifndef CONFIG_SCA_MAX_DEVICES
#define SCA_MAX_DEVICES         16
#endif

/* 路径长度上限：兼容旧的 SMART_HOME_CONFIG_PATH_MAX 命名。
 * 板上最长路径形如 /littlefs/smart_home/sessions/xxx.jsonl ≈ 48 字节，
 * 128 留足余量。 */
#ifndef SCA_PATH_MAX
#define SCA_PATH_MAX            128
#endif

#ifndef SCA_NAME_MAX
#define SCA_NAME_MAX            64
#endif

/* 兼容旧命名，便于平移代码时少改一行。
 * 这些别名让"搬文件 + 改 include"两步即可完成接入，
 * 不必逐个替换常量名。新代码应直接用 SCA_ 前缀。 */
#define SMART_HOME_CONFIG_PATH_MAX  SCA_PATH_MAX
#define SMART_HOME_CONFIG_DIR       SCA_STATE_DIR
#define SMART_HOME_SKILL_DIR        SCA_SKILL_DIR
#define SMART_HOME_RES_DIR          SCA_RES_DIR

#ifndef SMART_HOME_CONFIG_BACKENDS_PATH
#define SMART_HOME_CONFIG_BACKENDS_PATH    SCA_BACKENDS_PATH
#endif
#ifndef SMART_HOME_CONFIG_SETTINGS_PATH
#define SMART_HOME_CONFIG_SETTINGS_PATH    SCA_SETTINGS_PATH
#endif
#ifndef SMART_HOME_CONFIG_STATE_PATH
#define SMART_HOME_CONFIG_STATE_PATH       SCA_STATE_PATH
#endif
#ifndef SMART_HOME_CONFIG_SECRETS_PATH
#define SMART_HOME_CONFIG_SECRETS_PATH     SCA_SECRETS_PATH
#endif

/* ── 平台惯用宏兼容 ────────────────────────────────────
 * 业务代码中的 FAR / CODE 等修饰宏在这里定义为空即可，
 * 避免逐个文件删（会引入大量无意义 diff）。 */
#ifndef FAR
#define FAR
#endif
#ifndef CODE
#define CODE
#endif

/* ── 编译期断言：32 位平台假设 ─────────────────────────── */
#if UINTPTR_MAX != 0xFFFFFFFFu
#warning "This port targets 32-bit ARM (Cortex-M). Pointer size assumption changed."
#endif

#endif /* __SCA_CONFIG_H__ */
