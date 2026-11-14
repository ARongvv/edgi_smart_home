/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * JSON 抽象。
 *
 * cJSON 通过 config/cjson_compat.h 转发：
 *     #ifdef __NuttX__  → <netutils/cJSON.h>
 *     #else             → "cJSON.h"
 *
 * 那个 #ifdef 是平台判断，放在业务层违反本工程的约束。这里改成：
 * 业务层统一 include "port/sca_json.h"，由本文件决定 cJSON 的来源。
 */

#ifndef __SCA_JSON_H__
#define __SCA_JSON_H__

/*
 * cJSON.h 在 Windows 分支里把导出符号声明为
 *     __declspec(dllexport) type __stdcall
 * 这两个修饰符在 ARM GCC 目标上根本不存在（走的是非 Windows 分支）。
 * 但宿主语法检查（tcc / MinGW 定义了 _WIN32）会误入该分支。
 *
 * 处理：在包含 cJSON 之前把 __stdcall / __cdecl 定义成空宏、
 * 并关闭符号导出。宏是递归展开的，所以 cJSON 内部的
 * `#define CJSON_STDCALL __stdcall` 最终也展开为空。
 * 这不修改第三方源码，也不影响真实目标平台。
 */
#if defined(_WIN32) || defined(WIN32)
#undef  __stdcall
#undef  __cdecl
#define __stdcall
#define __cdecl
#define CJSON_HIDE_SYMBOLS
#endif

#include <cJSON.h>

#endif /* __SCA_JSON_H__ */
