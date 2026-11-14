#ifndef __RTCONFIG_H__
#define __RTCONFIG_H__
#define RT_TICK_PER_SECOND 1000
#define RT_USING_SAL 1
#define RT_USING_LWIP 1
#define RT_USING_ULOG 1
#define SCA_HTTP_USE_TLS 1

/* tcc 不认 `type __stdcall f(...)` 这种写法。
 * cJSON.h 在 Windows 分支里用 CJSON_PUBLIC(type) = __declspec(dllexport) type __stdcall，
 * 这里把两个修饰都清空，让声明退化成普通原型。仅影响宿主语法检查。 */
#define CJSON_STDCALL
#define CJSON_HIDE_SYMBOLS
#endif
