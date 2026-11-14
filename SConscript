import os
import sys
from building import *

Import('SDK_ROOT', 'SDK_LIB', 'rtconfig', 'env')

objs = []

SUBDIRS = [
    'packages',
    'packages/lvgl',
    'board',
    'port',
    'smart_home',
    'resources',
    'applications',
]

for d in SUBDIRS:
    objs += SConscript(d + '/SConscript')

# Reuse the vendor startup, device configuration, HAL and peripheral drivers.
# Keep all object files in this project's build tree, including vendor sources.
bsp_modules = [
    'projects/libs',
    'libraries/HAL_Drivers',
    'libraries/components/Infineon_cmsis-latest',
    'libraries/components/Infineon_core-lib-latest',
    'libraries/components/mtb-device-support-pse8xxgp/hal',
    'libraries/components/mtb-device-support-pse8xxgp/pdl',
    'libraries/components/mtb-device-support-pse8xxgp/device-utils/syspm',
    'libraries/components/async-transfer',
    'libraries/components/serial-memory',
    'libraries/components/Infineon_retarget-io-latest',
    'libraries/components/ASRC',
    'libraries/components/mtb-srf',
    'libraries/components/mtb-ipc',
    'libraries/Common/board',
]
for module in bsp_modules:
    module_objs = SConscript(os.path.join(SDK_ROOT, module, 'SConscript'),
                             variant_dir='#build/bsp/' + module, duplicate=0)
    if module == 'libraries/HAL_Drivers' and GetDepend('BSP_USING_SDIO0'):
        module_objs = [node for node in module_objs if node.name != 'drv_sdio.c']
        for group in Projects:
            if group['name'] == 'Drivers':
                group['src'] = [node for node in group['src'] if node.name != 'drv_sdio.c']
        sdio_dir = os.path.join(SDK_ROOT, module)
        sdio_source = '#build/bsp/' + module + '/sca_drv_sdio.c'
        sdio_override = env.Command(sdio_source, [os.path.join(sdio_dir, 'drv_sdio.c'),
                                                 '#board/ports/patch_sdio.py'],
                                    '"${PYTHON}" "${SOURCES[1]}" "${SOURCES[0]}" "$TARGET"',
                                    PYTHON=sys.executable)
        module_objs += DefineGroup('SmartHome_SDIO', sdio_override, depend=['BSP_USING_SDIO'],
                                   CPPPATH=[sdio_dir, os.path.join(sdio_dir, 'config')])
    objs += module_objs

if GetDepend(['SCA_HTTP_USE_TLS']):
    # The default PDL group omits the direct Crypto/TRNG functions used by
    # mbedtls_hardware_poll. Keep these objects in this project's build tree.
    crypto_build = ('#build/bsp/libraries/components/'
                    'mtb-device-support-pse8xxgp/pdl/drivers/source')
    objs += DefineGroup('PSE84_TRNG', [
        crypto_build + '/cy_crypto_core_hw.c',
        crypto_build + '/cy_crypto_core_trng.c',
    ], depend=['SCA_HTTP_USE_TLS'])

if GetDepend(['RT_USING_WIFI_HOST_DRIVER']):
    # Mirror the SDK's WHD source selection, but replace its weak .incbin
    # resource source (whose paths depend on the reference project location).
    whd_module = 'libraries/components/wifi-host-driver'
    whd_root = os.path.join(SDK_ROOT, whd_module)
    whd_build = '#build/bsp/' + whd_module
    VariantDir(whd_build, whd_root, duplicate=0)

    wifi_standard = 'COMPONENT_WIFI6' if GetDepend(['WHD_USING_WIFI6']) else 'COMPONENT_WIFI5'
    whd_rel = 'wifi-host-driver/WHD/' + wifi_standard
    whd_src = whd_build + '/' + whd_rel
    whd_inc = os.path.join(whd_root, whd_rel)
    whd_sources = Glob(whd_src + '/src/bus_protocols/whd_bus_common.c')
    whd_sources += Glob(whd_src + '/src/bus_protocols/whd_bus_sdio_protocol.c')
    whd_sources += Glob(whd_src + '/src/bus_protocols/whd_bus.c')
    whd_sources += Glob(whd_src + '/src/*.c')
    whd_sources += Glob(whd_build + '/porting/src/bsp/*.c')
    whd_sources += Glob(whd_build + '/porting/src/wlan/*.c')
    whd_sources += [node for node in Glob(whd_build + '/porting/src/resources/*.c')
                    if node.name != 'whd_project_resources.c']
    whd_paths = [
        whd_inc + '/src', whd_inc + '/inc', whd_inc + '/src/include',
        whd_inc + '/src/bus_protocols', whd_inc + '/resources/resource_imp',
    ]
    if GetDepend(['WHD_PORTING_HAL']):
        whd_sources += Glob(whd_build + '/porting/src/hal/*.c')
        whd_paths += [whd_root + '/porting/inc/hal']
    if GetDepend(['WHD_PORTING_RTOS']):
        whd_sources += Glob(whd_build + '/porting/src/rtos/*.c')
        whd_paths += [whd_root + '/porting/inc/rtos']
    if GetDepend(['WHD_PORTING_BSP']):
        whd_paths += [whd_root + '/porting/inc/bsp']

    if GetDepend(['WHD_RESOURCES_IN_MEMORY', 'WHD_USING_CHIP_CYW55500']):
        resource_root = os.path.join(SDK_ROOT, 'resources')
        resource_files = [
            os.path.join(resource_root, '55500A1.trxcse'),
            os.path.join(resource_root, '55500A1.clm_blob'),
            os.path.join(resource_root, 'cyw55513modpse84som_rev3.txt'),
        ]
        for item in resource_files:
            if not os.path.isfile(item):
                raise RuntimeError('Missing WHD resource file: ' + item)

        def whd_array(data):
            return ',\n'.join('    ' + ', '.join(str(b) for b in bytearray(data[i:i + 16]))
                              for i in range(0, len(data), 16))

        def generate_whd_resources(target, source, env):
            firmware, clm, nvram = [open(str(node), 'rb').read() for node in source]
            nvram = nvram.replace(b'\r\n', b'\n').replace(b'\r', b'\n').replace(b'\n', b'\x00')
            if not nvram or nvram[-1] != 0:
                nvram += b'\x00'
            output = '''#include <stdint.h>
#include "wiced_resource.h"
const unsigned char wifi_firmware_image_data[%d] = {
%s
};
const resource_hnd_t wifi_firmware_image = { RESOURCE_IN_MEMORY, %d, {.mem = {(const char *)wifi_firmware_image_data}} };
const unsigned char wifi_firmware_clm_blob_data[%d] = {
%s
};
const resource_hnd_t wifi_firmware_clm_blob = { RESOURCE_IN_MEMORY, %d, {.mem = {(const char *)wifi_firmware_clm_blob_data}} };
const char wifi_nvram_image[%d] = {
%s
};
const uint32_t wifi_nvram_image_size = %d;
''' % (len(firmware), whd_array(firmware), len(firmware),
       len(clm), whd_array(clm), len(clm),
       len(nvram), whd_array(nvram), len(nvram))
            with open(str(target[0]), 'w') as output_file:
                output_file.write(output)

        whd_sources += env.Command(whd_build + '/whd_project_resources.c',
                                   resource_files, generate_whd_resources)

    objs += DefineGroup('whd', whd_sources, depend=['RT_USING_WIFI_HOST_DRIVER'],
                        CPPPATH=whd_paths)

vendor_board = os.path.join(rtconfig.BSP_PROJECT, 'board')
objs += SConscript(os.path.join(vendor_board, 'SConscript'),
                   variant_dir='#build/bsp/board', duplicate=0)

# The SDK's littlefs SConscript can remove files in its source tree.
littlefs = os.path.join(SDK_LIB, 'littlefs')
VariantDir('#build/bsp/littlefs', littlefs, duplicate=0)
objs += DefineGroup('LittleFS', Glob('#build/bsp/littlefs/*.c'),
                    depend=['BSP_USING_LITTLEFS'], CPPPATH=[littlefs],
                    CPPDEFINES=['LFS_CONFIG=lfs_config.h'])

Return('objs')
