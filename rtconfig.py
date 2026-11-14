"""GCC configuration for the Edgi-Talk non-secure Cortex-M55 image."""

import os
import shutil

SDK_ROOT = os.path.abspath(os.getenv(
    'EDGI_SDK_ROOT', 'D:/rt-thread-env/sdk-bsp-psoc_e84-edgi-talk'))
BSP_PROJECT = os.path.join(SDK_ROOT, 'projects', 'Edgi_Talk_M55_LVGL')
LINK_SCRIPT = os.path.join(BSP_PROJECT, 'board', 'linker_scripts', 'link.ld')

ARCH = 'arm'
CPU = 'cortex-m55'
CROSS_TOOL = PLATFORM = 'gcc'
BSP_LIBRARY_TYPE = 'Packages'
TARGET_EXT = 'elf'
PREFIX = 'arm-none-eabi-'
CC = AS = LINK = PREFIX + 'gcc'
CXX = PREFIX + 'g++'
AR = PREFIX + 'ar'
EXEC_PATH = os.getenv('RTT_EXEC_PATH') or os.path.dirname(shutil.which(CC) or '') or (
    'D:/rt-thread-env/toolchain/arm-gnu/'
    'arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin')

DEVICE = '-mcpu=cortex-m55 -mthumb -mfpu=auto -mfloat-abi=hard --specs=nano.specs'
CFLAGS = DEVICE + ' -Os -g -std=gnu99 -Wall -ffunction-sections -fdata-sections'
CFLAGS += ' -include port/sca_agent_profile.h'
CFLAGS += ' -D_POSIX_C_SOURCE=1 -D__RTTHREAD__ -DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE'
CFLAGS += ' -DCONFIG_SMART_HOME_DEMO_DATA_ROOT=\\"/littlefs\\"'
AFLAGS = DEVICE + ' -c -x assembler-with-cpp -Wa,-mimplicit-it=thumb'
CXXFLAGS = DEVICE + ' -Os -g -ffunction-sections -fdata-sections'
LFLAGS = DEVICE + ' -nostartfiles -Wl,--gc-sections,-Map=rtthread.map,-u,Reset_Handler'
LFLAGS += ' -T "' + LINK_SCRIPT.replace('\\', '/') + '"'
POST_ACTION = (
    PREFIX + 'objcopy -O ihex $TARGET rtthread.hex\n' +
    PREFIX + 'objcopy -O binary $TARGET rtthread.bin\n' +
    PREFIX + 'size $TARGET')
