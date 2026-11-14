import os
import sys

import rtconfig
from SCons.Subst import quote_spaces

SDK_ROOT = rtconfig.SDK_ROOT
RTT_ROOT = os.path.join(SDK_ROOT, 'rt-thread')
SDK_LIB = os.path.join(SDK_ROOT, 'libraries', 'components')
if not os.path.isfile(rtconfig.LINK_SCRIPT) or not os.path.isfile(
        os.path.join(RTT_ROOT, 'tools', 'building.py')):
    Exit('Set EDGI_SDK_ROOT to the Edgi-Talk BSP checkout.')

os.environ['RTT_ROOT'] = RTT_ROOT
sys.path.append(os.path.join(RTT_ROOT, 'tools'))
from building import PrepareBuilding, DoBuilding, DefineGroup, GetDepend, Projects

DefaultEnvironment(tools=[])
env = Environment(tools=['mingw'],
                  AS=rtconfig.AS, ASFLAGS=rtconfig.AFLAGS,
                  CC=rtconfig.CC, CCFLAGS=rtconfig.CFLAGS,
                  AR=rtconfig.AR, ARFLAGS='-rc',
                  CXX=rtconfig.CXX, CXXFLAGS=rtconfig.CXXFLAGS,
                  LINK=rtconfig.LINK, LINKFLAGS=rtconfig.LFLAGS,
                  LIBS=['m'])
env.PrependENVPath('PATH', rtconfig.EXEC_PATH)
Export('RTT_ROOT', 'rtconfig', 'SDK_ROOT', 'SDK_LIB', 'env')

objs = PrepareBuilding(env, RTT_ROOT, has_libcpu=False)
if GetDepend('RT_USING_LWIP212'):
    # Arm GNU 13.3 with -Os on M55 can compile the checksum's trailing-byte
    # check as an unconditional read. Keep the regular branches in this file.
    checksum_dir = os.path.join(RTT_ROOT, 'components', 'net', 'lwip',
                                'lwip-2.1.2', 'src', 'core')
    def is_lwip_checksum(node):
        return node.name == 'inet_chksum.c' and os.path.normcase(
            os.path.dirname(node.rfile().abspath)) == os.path.normcase(checksum_dir)

    checksum_sources = [node for node in objs if is_lwip_checksum(node)]
    objs = [node for node in objs if not is_lwip_checksum(node)]
    for group in Projects:
        group['src'] = [node for node in group['src'] if not is_lwip_checksum(node)]
    objs += DefineGroup('SmartHome_lwIP_Checksum', checksum_sources,
                        depend=['RT_USING_LWIP212'],
                        LOCAL_CCFLAGS=' -fno-branch-count-reg')

if os.name == 'nt':
    # The BSP link line exceeds Windows' command-line limit. GCC's response
    # file parser treats backslashes as escapes.
    env['TEMPFILEARGESCFUNC'] = lambda arg: quote_spaces(str(arg).replace('\\', '/'))
    env['LINKCOM'] = "${TEMPFILE('" + env['LINKCOM'] + "', '$LINKCOMSTR')}"

# The M55 port reuses sources from its sibling M33 directory by absolute path.
# Map those objects into this project's build tree as well.
cpu_dir = os.path.join(RTT_ROOT, 'libcpu', 'arm', 'cortex-m33')
cpu_variant = '#build/kernel/libcpu/arm/cortex-m33'
env.VariantDir(cpu_variant, cpu_dir, duplicate=0)
objs = [env.File(cpu_variant + '/' + node.name)
        if os.path.normcase(os.path.dirname(node.abspath)) == os.path.normcase(cpu_dir)
        else node for node in objs]
target = 'rtthread.elf'
DoBuilding(target, objs)
env.Depends(target, rtconfig.LINK_SCRIPT)
