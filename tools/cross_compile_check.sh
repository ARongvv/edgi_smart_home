#!/usr/bin/env bash
#
# 交叉编译验证（arm-none-eabi-gcc + 真实 RT-Thread 源码 + 真实 LVGL）
#
# 这不是完整构建：它不链接、不生成 bin，只做"每个 .c 能否编出目标文件"。
# 目的很明确 —— 在 scons 与 BSP 链接脚本就位之前，先把语法/类型/声明
# 层面的问题全部暴露出来。
#
# 用法：
#   tools/cross_compile_check.sh              # 全部
#   tools/cross_compile_check.sh port         # 只查某个目录
#
set -u

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
RTT="${RTT_ROOT:-/d/rt-thread-env/rt-thread}"
TC="${SCA_TOOLCHAIN:-/d/rt-thread-env/toolchain/arm-gnu/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin}"
CC="$TC/arm-none-eabi-gcc"

if [ ! -x "$CC.exe" ] && [ ! -x "$CC" ]; then
    echo "交叉编译器不存在: $CC"
    echo "设置 SCA_TOOLCHAIN 指向 arm-none-eabi-gcc 所在目录"
    exit 2
fi
CC="$CC.exe"
[ -x "$CC" ] || CC="${CC%.exe}"

# ── 目标参数 ──────────────────────────────────────────────
# M55 + FPv5 双精度 + hard float，与 board/board.h 的假定一致
CPU="-mcpu=cortex-m55 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16"
CFLAGS="$CPU -Os -std=gnu99 -Wall -Wno-unused-parameter -Wno-unused-variable"
CFLAGS="$CFLAGS -ffunction-sections -fdata-sections -fno-common"
# _POSIX_C_SOURCE=1 与 RT-Thread 的 newlib SConscript 一致。不定义时
# newlib 的 <sys/types.h> 不会 include <sys/select.h> → <sys/time.h>，
# 而那个头会在 clock_t 定义之前就被拉进来，导致 suseconds_t/clock_t
# 全未定义。这不是我们代码的问题，是 newlib 的头顺序要求。
CFLAGS="$CFLAGS -include $PROJ/port/sca_agent_profile.h"
CFLAGS="$CFLAGS -D_POSIX_C_SOURCE=1 -D__RTTHREAD__"
# LVGL：强制用 packages/lvgl/lv_conf.h。
#   LV_CONF_INCLUDE_SIMPLE —— 用 #include "lv_conf.h"（靠 -I 找到）
#   LV_KCONFIG_IGNORE      —— 跳过 lv_conf_kconfig.h 的 Kconfig 桥接
# 注意不要定义 LV_CONF_SKIP：lv_conf_internal.h 的判断是
#   #if !defined(LV_CONF_SKIP) || defined(LV_CONF_PATH)
# 只看"有没有定义"，写 -DLV_CONF_SKIP=0 反而会让整个 lv_conf.h 被跳过。
CFLAGS="$CFLAGS -DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE"

# ── 包含路径 ──────────────────────────────────────────────
INC=""
add_inc() { [ -d "$1" ] && INC="$INC -I$1"; }

add_inc "$PROJ"
add_inc "$RTT/include"
add_inc "$RTT/components/finsh"
add_inc "$RTT/components/drivers/include"
add_inc "$RTT/components/drivers/wlan"
add_inc "$RTT/components/drivers/audio"
add_inc "$RTT/components/drivers/tty/include"
add_inc "$RTT/components/libc/compilers/common/include"
# libc：RT-Thread 只在 GCC 下用 newlib 自己的类型定义，
# compilers/common/include 提供 POSIX 补充（unistd/dirent/posix）。
# 注意不要加 components/libc/compilers/common/extension ——
# 那是给 Keil/IAR 这类只有 ISO C 的工具链用的，里面的 sys/types.h
# 会与 newlib 的冲突（ssize_t 重复定义）。
add_inc "$RTT/components/libc/compilers/common/include"
add_inc "$RTT/components/utilities/ulog"
# DFS：v1 用 dfs_file.h，v2 才有 dfs_posix.h。两者都加，让源码
# 无论 include 哪一个都能找到。
add_inc "$RTT/components/dfs/dfs_v1/include"
add_inc "$RTT/components/dfs/dfs_v2/include"
add_inc "$RTT/components/net/sal/include"
add_inc "$RTT/components/net/sal/include/socket"
add_inc "$RTT/components/net/sal/include/socket/sys_socket"
# 不加 lwip 的 compat/posix：它也提供 <sys/socket.h>，会与 SAL 的
# 版本争同一个路径名，谁先命中取决于 -I 顺序，冲突难排查。
add_inc "$RTT/components/net/netdev/include"
add_inc "$RTT/components/net/lwip/port"
# lwIP 2.1.2：sal_socket.h 与 netdev_ipaddr.h 直接引用 lwip 的
# ip.h / err.h，缺这个路径所有 socket 相关头都编不过。
add_inc "$RTT/components/net/lwip/lwip-2.1.2/src/include"
add_inc "$RTT/components/net/lwip/lwip-2.1.2/src/include/lwip"
# netdev_ipaddr.h 在 NETDEV_IPV6 未定义时直接 typedef ip4_addr_t，
# 这两个类型来自 lwip 的 ip_addr.h。RT-Thread 正式构建里由
# RT_USING_LWIP 的 SConscript 把 lwip include 加进全局路径，
# 这里手工补上（见下）。

add_inc "$PROJ/packages/lvgl"
add_inc "$PROJ/packages/lvgl/src"
add_inc "$PROJ/packages/lvgl/src"
add_inc "$PROJ/packages/cjson"
add_inc "$PROJ/packages/qrcodegen"
add_inc "$PROJ/packages/cagent/include"
add_inc "$PROJ/packages/cagent/src"
add_inc "$PROJ/packages/cagent/codecs/json"
add_inc "$PROJ/packages/cagent/codecs/json/vendor/jsmn"
add_inc "$PROJ/packages/cagent/providers/model/openai/include"
add_inc "$PROJ/packages/cagent/providers/storage/ram/include"
add_inc "$PROJ/packages/cagent/providers/storage/files/include"
add_inc "$PROJ/packages/cagent/providers/storage/jsonl/include"
add_inc "$PROJ/packages/cagent/ports/rtthread"
add_inc "$PROJ/packages/cagent/ports/rtthread/storage/include"

# LVGL 内部头文件用相对路径互相引用（"../misc/lv_assert.h"），
# 因此只需要根目录与 src/ 两个入口，不能把子目录全加进来 ——
# src/libs/thorvg/rapidjson/msinttypes/ 里有一份 Windows 专用的
# stdint.h，一旦进 -I 路径就会把系统的挤掉，整个工程都编不过。

# ── 目标文件列表 ──────────────────────────────────────────
TARGET="${1:-all}"
case "$TARGET" in
    all)
        # camera 与 voice 默认不参与构建（rtconfig.h 里 CONFIG_SCA_CAMERA /
        # CONFIG_SCA_VOICE_* 都注释掉了），它们尚未完成 RT-Thread 适配，
        # 仍使用 V4L2 / ALSA / KWS 模型。等阶段 7/8 真正做适配时再加入。
        FILES=$(find "$PROJ/port" "$PROJ/smart_home" "$PROJ/board" \
                     "$PROJ/applications" \
                     -name '*.c' \
                     -not -path '*/camera/*' \
                     -not -path '*/voice/*' | sort)
        ;;
    *)
        FILES=$(find "$PROJ/$TARGET" -name '*.c' 2>/dev/null | sort)
        ;;
esac

if [ -z "$FILES" ]; then
    echo "没有找到 .c 文件"
    exit 1
fi

OUT="${TMPDIR:-/tmp}/sca_xbuild"
rm -rf "$OUT"; mkdir -p "$OUT"

ok=0; fail=0
FAILED=""
for f in $FILES; do
    obj="$OUT/$(echo "${f#$PROJ/}" | tr '/' '_' | sed 's/\.c$/.o/')"
    if $CC $CFLAGS $INC -c "$f" -o "$obj" 2>"$OUT/err.txt"; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        FAILED="$FAILED\n  ${f#$PROJ/}"
        echo "── FAIL  ${f#$PROJ/}"
        grep -E "error:|Error:" "$OUT/err.txt" | head -4
    fi
done

echo ""
echo "════════════════════════════════════════"
echo "  通过 $ok / 失败 $fail"
if [ "$fail" -gt 0 ]; then
    echo -e "  失败清单:$FAILED"
fi
echo "════════════════════════════════════════"

[ "$fail" -eq 0 ]
