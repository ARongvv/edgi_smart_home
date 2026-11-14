/**
 * RT-Thread 5.0.2 配置（Edgi-Talk PSE84 / Cortex-M55）
 *
 * ★ 这个文件通常由 menuconfig 生成，这里是手写的 ★
 *
 * 原因：本工程暂时没有 kconfig 前端（menuconfig 需要 Python 的
 * kconfiglib 与完整的 Kconfig 树）。手写一份最小可编译配置，先把
 * "内核 + port 抽象层 + 业务代码能真的编译过"这条链路打通；等真正接入
 * board 支持包时，用 menuconfig 重新生成即可覆盖本文件。
 *
 * 手写配置的原则：
 *   - 只开 sca_os / sca_fs / sca_http 真正用到的内核特性；
 *   - lwIP / DFS / 设备框架这些需要 BSP 配合的，先按"已存在"打开，
 *     具体驱动在板级 port 里实现；
 *   - 所有数值型参数都写清单位与依据。
 */

#ifndef __RTCONFIG_H__
#define __RTCONFIG_H__

#include "port/sca_agent_profile.h"

/* ── 内核基础 ───────────────────────────────────────────── */

#define RT_NAME_MAX                 16
#define RT_ALIGN_SIZE               8
#define RT_THREAD_PRIORITY_MAX      32
#define RT_TICK_PER_SECOND          1000
#define RT_VER_NUM                  0x50002

/* 空闲线程栈：内核在 idle 里做 defunct 清理，1024 字节在带
 * 文件系统回调时偏紧，取 2048。 */
#define IDLE_THREAD_STACK_SIZE      2048

#define RT_USING_OVERFLOW_CHECK

/* ── 调度器 ─────────────────────────────────────────────── */

/* M55 是单核（另一颗 M33 跑独立固件，不进同一个 OS 调度），
 * 因此不用 SMP 版本。 */
#define RT_USING_SCHEDULER_UP

/* ── 对象与 IPC ─────────────────────────────────────────── */

#define RT_USING_OBJECT_NAME
#define RT_USING_SEMAPHORE
#define RT_USING_MUTEX
#define RT_USING_EVENT
#define RT_USING_MAILBOX
#define RT_USING_MESSAGEQUEUE

/* sca_cond_t 的实现用信号量合成，不需要额外的 condvar 支持。 */

/* ── 内存 ───────────────────────────────────────────────── */

#define RT_USING_HEAP

/* 系统堆：HyperRAM 有 16MB，但内核堆只放线程栈与对象，
 * 大块缓冲（LVGL 绘制缓冲、字体、HTTP 响应）由 memheap 管理。
 * 这里给 64KB 作为内核堆起点，实际大小由链接脚本决定。 */
#define RT_USING_SMALL_MEM
#define RT_USING_MEMHEAP
#define RT_USING_MEMHEAP_AS_HEAP
#define RT_USING_MEMPOOL

/* ── 设备框架 ───────────────────────────────────────────── */

#define RT_USING_DEVICE
/* rt_console_get_device() 在 RT_USING_CONSOLE 未开时不存在，
 * 但 dfs_file.c 用它做 devfs 的 console 节点。 */
#define RT_USING_CONSOLE
#define RT_CONSOLE_DEVICE_NAME      "uart2"
/* shell.c 与 msh_file.c 用它做行缓冲。Kconfig 默认 128，
 * 本工程的 msh 命令只有两三个短命令，128 足够。 */
#define RT_CONSOLEBUF_SIZE          128
#define RT_USING_DEVICE_IPC
#define RT_USING_SERIAL
#define RT_USING_SERIAL_V1
#define RT_SERIAL_USING_DMA
#define RT_USING_PIN
#define RT_USING_ADC
#define RT_USING_AUDIO
/* 音频内存池参数：Kconfig 里 RT_USING_AUDIO 下的默认值。
 * 不开会报 RT_AUDIO_REPLAY_MP_BLOCK_COUNT 等未定义。 */
#define RT_AUDIO_REPLAY_MP_BLOCK_SIZE   4096
#define RT_AUDIO_REPLAY_MP_BLOCK_COUNT  2
#define RT_AUDIO_RECORD_PIPE_SIZE       2048
#define RT_USING_I2C
#define RT_USING_SPI
#define RT_USING_PWM
#define RT_USING_SYSTEM_WORKQUEUE
#define RT_SYSTEM_WORKQUEUE_STACKSIZE 2048
#define RT_SYSTEM_WORKQUEUE_PRIORITY 23
#define RT_USING_SDIO
#define RT_SDIO_STACK_SIZE          2048
#define RT_SDIO_THREAD_PRIORITY     0
#define RT_MMCSD_STACK_SIZE         2048
#define RT_MMCSD_THREAD_PREORITY    22
#define RT_MMCSD_MAX_PARTITION      16
#define RT_USING_WIFI
#define RT_WLAN_DEVICE_STA_NAME     "wlan0"
#define RT_WLAN_DEVICE_AP_NAME      "wlan1"
#define RT_WLAN_SSID_MAX_LENGTH     32
#define RT_WLAN_PASSWORD_MAX_LENGTH 64
#define RT_WLAN_DEV_EVENT_NUM       2
#define RT_WLAN_MANAGE_ENABLE
#define RT_WLAN_SCAN_WAIT_MS        10000
#define RT_WLAN_CONNECT_WAIT_MS     10000
#define RT_WLAN_SCAN_SORT
#define RT_WLAN_MSH_CMD_ENABLE
#define RT_WLAN_AUTO_CONNECT_ENABLE
#define AUTO_CONNECTION_PERIOD_MS   2000
#define RT_WLAN_CFG_ENABLE
#define RT_WLAN_CFG_INFO_MAX        3
#define RT_WLAN_PROT_ENABLE
#define RT_WLAN_PROT_NAME_LEN       8
#define RT_WLAN_PROT_MAX            2
#define RT_WLAN_DEFAULT_PROT        "lwip"
#define RT_WLAN_PROT_LWIP_ENABLE
#define RT_WLAN_PROT_LWIP_NAME      "lwip"
#define RT_WLAN_WORK_THREAD_ENABLE
#define RT_WLAN_WORKQUEUE_THREAD_NAME "wlan"
#define RT_WLAN_WORKQUEUE_THREAD_SIZE 2048
#define RT_WLAN_WORKQUEUE_THREAD_PRIO 11

/* ── 时钟与定时器 ───────────────────────────────────────── */

#define RT_USING_TIMER_SOFT
#define RT_TIMER_THREAD_STACK_SIZE  2048
#define RT_TIMER_THREAD_PRIO        4

/* ── 组件 ───────────────────────────────────────────────── */

#define RT_USING_FINSH
#define RT_USING_COMPONENTS_INIT
#define RT_USING_USER_MAIN
#define RT_MAIN_THREAD_STACK_SIZE   16384
#define RT_MAIN_THREAD_PRIORITY     10
#define FINSH_THREAD_NAME           "tshell"
#define FINSH_THREAD_PRIORITY       20
#define FINSH_THREAD_STACK_SIZE     4096
#define FINSH_USING_MSH
#define FINSH_USING_MSH_ONLY
#define FINSH_ARG_MAX               10
/* 这两个是 MSH_CMD_EXPORT 的前提：SYMTAB 决定导出表结构是否存在，
 * DESCRIPTION 决定是否把命令描述编进 .rodata.name 段。
 * 不开 SYMTAB 时 MSH_FUNCTION_EXPORT_CMD 完全没有定义，
 * 宏展开会留下裸参数，报 "expected ')' before string constant"。 */
#define FINSH_USING_SYMTAB
#define FINSH_USING_DESCRIPTION

#define RT_USING_MSH
#define RT_USING_POSIX
/* pthread_cond.c 调 rt_timespec_to_tick()，它的声明在
 * RT_USING_POSIX_CLOCK 里。sca_os 的 sca_cond_t 不需要 POSIX 时钟，
 * 但 RT_USING_PTHREADS 会编 pthread_cond.c，所以必须开。 */
#define RT_USING_POSIX_CLOCK
#define RT_USING_POSIX_DELAY
/* clock_gettime / clock_nanosleep 的实现在 ctime.c 里，
 * 外层是 #if defined(RT_USING_POSIX_CLOCK) && defined(RT_USING_KTIME)。
 * 不开 ktime 就只有声明没有实现 → 链接期 undefined reference。 */
#define RT_USING_KTIME
#define RT_USING_POSIX_FS
#define RT_USING_POSIX_DEVIO
#define RT_LIBC_DEFAULT_TIMEZONE    8

/* ── 文件系统 ───────────────────────────────────────────── */

/* board/ports/sca_storage_port.c 挂载 littlefs 到 /littlefs，
 * sca_fs 的所有路径都以它为根。 */
#define RT_USING_DFS
/* dfs.c 里 fd_slot_expand / fd_release 这些函数在 #ifdef DFS_USING_POSIX
 * 内定义，但调用点在外部。不开这个宏就会报 "implicit declaration"。
 * 它是 Kconfig 里 DFS 的默认项。 */
#define DFS_USING_POSIX
#define RT_USING_DFS_V1
#define RT_USING_DFS_DEVFS
#define RT_USING_DFS_ELMFAT
#define RT_USING_DFS_ROMFS
#define RT_USING_MTD_NOR
#define RT_USING_FAL
#define FAL_PART_HAS_TABLE_CFG
#define FAL_DEBUG                   0
#define DFS_FILESYSTEMS_MAX         4
#define DFS_FILESYSTEM_TYPES_MAX    4
#define DFS_FD_MAX                  16

/* ── 网络 ───────────────────────────────────────────────── */

/* sca_http 依赖 SAL + lwIP + mbedTLS。三者都由 BSP 提供，
 * 这里只打开开关，具体实现在 board/ports/sca_wifi_port.c。 */
#define RT_USING_SAL
#define SAL_USING_LWIP
/* netdev_ipaddr.h 用 NETDEV_IPV4 / NETDEV_IPV6 决定是否 typedef
 * ip4_addr_t / ip6_addr_t。不定义这两个宏，所有依赖它们的头
 * （sal_socket.h、netdev.h）都会报类型未定义。 */
#define NETDEV_IPV4                 1
#define NETDEV_IPV6                 0
#define SAL_USING_POSIX
#define RT_USING_LWIP
/* ★ 必须开：lwipopts.h 用它决定 LWIP_TCP。不开则 LWIP_TCP=0，
 * api_msg.h 里 struct sd 整个不编译，而 api_lib.c 的
 * #if LWIP_SO_SNDTIMEO 分支仍在引用 msg.sd → 编译失败。 */
#define RT_LWIP_TCP
/* 同理：ARP/ICMP/DHCP/DNS 都由 RT_LWIP_xxx 门控。 */
#define RT_LWIP_ARP
#define RT_LWIP_ICMP
#define RT_LWIP_DHCP
#define RT_LWIP_DNS
#define RT_LWIP_UDP
/* 选择 lwIP 2.1.2：它的 SConscript 用 RT_USING_LWIP212 作为开关，
 * 不开这个宏则 lwip-2.1.2 目录整个不进构建，但 port/ 仍然会编，
 * 于是 lwip/sys.h 找不到。 */
#define RT_USING_LWIP212
#define RT_USING_NETDEV
#define LWIP_USING_DHCP
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
/* 必须带值：lwip 的头里用 `#if LWIP_SO_RCVTIMEO`，
 * 空宏会让预处理器报 "#if with no expression"。 */
#define LWIP_SO_RCVTIMEO            1
#define LWIP_SO_SNDTIMEO            1

/* HTTPS 握手对缓冲区大小敏感：历史上 IOB 过小会导致握手失败
 * （IOB 36 → 256）。RT-Thread 侧的对应参数是 TCP 窗口与
 * recvmbox 大小，这里按 8KB 起步。 */
#define LWIP_TCP_WND                 8192
#define RT_LWIP_TCP_MSS              1460
#define RT_LWIP_TCP_RECVMBOX_SIZE    32
#define RT_LWIP_TCP_SND_BUF          16384
#define RT_LWIP_TCPTHREAD_PRIORITY   10
#define RT_LWIP_TCPTHREAD_MBOX_SIZE  16
#define RT_LWIP_TCPTHREAD_STACKSIZE  2048
#define RT_LWIP_ETHTHREAD_PRIORITY   11
#define RT_LWIP_ETHTHREAD_MBOX_SIZE  16
#define RT_LWIP_ETHTHREAD_STACKSIZE  2048

#define RT_USING_SNTP
#define RT_USING_RTC
#define RT_USING_SOFT_RTC

/* CYW55500 / WHD：资源直接编入 M55 镜像，无需额外 FAL 烧写。
 * 注意：SDK 内嵌的 55500A1.clm_blob 只包含 AU/US 法规表，
 * 设置为 CN 会导致 whd_wifi_on 在 set country 阶段失败。
 * 量产需替换为包含 CN 的完整 CLM blob；当前使用 AU（支持 1-13 信道）。 */
#define WHD_SET_COUNTRY_FROM_HOST
#define WHD_COUNTRY_CODE            "AU"
#define WHD_COUNTRY_CODE_REVISION   0
#define CY_WIFI_USING_THREAD_INIT
#define CY_WIFI_INIT_THREAD_PRIORITY 10
#define CY_WIFI_INIT_THREAD_STACK_SIZE 2048
#define CY_WIFI_WHD_THREAD_PRIORITY 6
#define CY_WIFI_WHD_THREAD_STACK_SIZE 5120
#define WHD_RESOURCES_IN_MEMORY
#define WHD_RESOURCES_BLOCK_SIZE    1024
#define WHD_USING_CHIP_CYW55500
#define WHD_USING_WIFI6
#define CYBSP_USING_PIN_NUMBER
#define CYBSP_REG_ON_PIN            94
#define CYBSP_HOST_WAKE_IRQ_PIN     -1
#define CYBSP_HOST_WAKE_IRQ_EVENT_FALL
#define CYBSP_OOB_INTR_PRIORITY     2
#define WHD_PORTING_HAL
#define WHD_PORTING_RTOS
#define RT_USING_WIFI_HOST_DRIVER
#define RT_WHD_LOG_LEVEL_ERROR

/* ── TLS ────────────────────────────────────────────────── */

/* sca_http 直接在 SAL socket 上使用官方 mbedTLS 3.6.5 LTS；
 * 不启用 SAL_USING_TLS，避免再引入一套重复的 TLS 会话封装。
 * CA、SNI、主机名和证书有效期均强制校验，NTP 校时前拒绝 HTTPS。 */
#define RT_USING_MBEDTLS
#define SCA_HTTP_USE_TLS
/* #define SAL_USING_TLS */

/* ── 调试 ───────────────────────────────────────────────── */

#define RT_USING_ULOG
#define ULOG_OUTPUT_LVL             LOG_LVL_INFO
#define ULOG_BUFFER_SIZE            1024
#define ULOG_ASYNC_OUTPUT

#define RT_DEBUG
#define RT_DEBUG_INIT               0
#define RT_DEBUG_THREAD             0

/* ── POSIX 兼容 ─────────────────────────────────────────── */

#define RT_USING_PTHREADS
#define PTHREAD_NUM_MAX             8

/* ── 架构相关（M55）─────────────────────────────────────── */

#define ARCH_ARM_CORTEX_M55
#define ARCH_ARM_CORTEX_M
#define RT_ALIGN(size, align)       (((size) + (align) - 1) & ~((align) - 1))

/* FPU：M55 的 fpv5-d16，hard 浮点。
 * RT-Thread 的上下文切换需要保存 s16-s31，cortex-m33/fpu port 已实现。 */
#define ARCH_ARM_FPU
#define ARCH_ARM_FPU_LAZY_STACKING

/* ── 工程级开关（由 SConstruct 读取）────────────────────── */

/* ★ 两套名字，必须都写 ★
 *
 *   SCA_xxx        —— SConscript 的 GetDepend() 读这个。
 *                     RT-Thread 的 Kconfig 生成 rtconfig.h 时用
 *                     "去掉 CONFIG_ 前缀" 的裸名，GetDepend 也只认裸名。
 *   CONFIG_SCA_xxx —— port/sca_config.h 用 #ifdef 读这个，
 *                     翻成代码里的 SCA_ENABLE_xxx。
 *
 * 只写 CONFIG_ 形式会导致所有 SConscript 的 if 判断为假 →
 * 文件不进构建 → 链接期一堆 undefined reference。 */
#define SCA_ENABLE_APP
#define SCA_UI_LVGL
#define SCA_MILOCO
#define SCA_REMINDERS
#define SCA_USER_MEMORY
#define SCA_SESSION_PERSIST
/* 摄像头/语音默认关闭：需要 UVC 摄像头与 KWS 模型，板级 port 未实现。 */
/* #define SCA_CAMERA */
/* #define SCA_VAD */
/* #define SCA_VOICE_ASR */
/* #define SCA_VOICE_TTS */
/* #define SCA_KWS */

#define CONFIG_SCA_ENABLE_APP       1
#define CONFIG_SCA_UI_LVGL          1
#define RT_USING_CACHE
#define RT_USING_TOUCH
#define RT_TOUCH_PIN_IRQ
#define RT_USING_LCD
#define RT_USING_I2C_BITOPS
#define SOC_SERIES_IFX_PSOCE84
#define KIT_PROCE84_EVK_M55
#define BSP_USING_GPIO
#define BSP_USING_UART
#define BSP_USING_UART2
#define BSP_USING_I2C
#define BSP_USING_HW_I2C0
#define BSP_USING_SOFT_I2C1
#define BSP_SOFT_I2C1_SCL_PIN       105
#define BSP_SOFT_I2C1_SDA_PIN       106
#define BSP_USING_PWM
#define BSP_USING_PWM18
#define BSP_USING_LCD
#define COMPONENT_MTB_DISPLAY_tl043wvv02
#define BSP_LCD_ROTATION_DEGREES    90
#define BSP_USING_FLASH
#define BSP_USING_LITTLEFS
#define BSP_USING_SDIO
#define BSP_USING_SDIO0
#define BSP_USING_SDIO1
#define CONFIG_SCA_MILOCO           1
#define CONFIG_SCA_REMINDERS        1
#define CONFIG_SCA_USER_MEMORY      1
#define CONFIG_SCA_SESSION_PERSIST  1
/* 摄像头与语音默认关闭：需要 UVC 摄像头与 KWS 模型文件，
 * 板级 port 尚未实现，打开会在链接期缺符号。
 * 注意这些必须整行注释掉，不能写成带行尾注释的 define：
 * 那种写法仍然算"已定义"，而 sca_config.h 用 #ifdef 判断。 */
/* #define CONFIG_SCA_CAMERA */
/* #define CONFIG_SCA_VAD */
/* #define CONFIG_SCA_VOICE_ASR */
/* #define CONFIG_SCA_VOICE_TTS */
/* #define CONFIG_SCA_KWS */
/* #define CONFIG_SCA_DEBUG_LOG */

/* 容量参数不在 rtconfig.h 里定义 —— 它们由 port/sca_config.h 提供
 * 带 #ifndef 保护的默认值。两边都定义会触发 redefined 警告。 */

#endif /* __RTCONFIG_H__ */
