"""Apply the SmartHome SDIO0 clock limit to the SDK driver in the build tree."""

import sys
from pathlib import Path


source, target = map(Path, sys.argv[1:])
driver = source.read_text(encoding='utf-8')

old = '    sdio->host->freq_max = CY_SDIO_MAX_FREQ_HZ;'
new = '''    sdio->host->freq_max = CY_SDIO_MAX_FREQ_HZ;
#ifdef BSP_USING_SDIO0
    /* SDHC0 is wired to the CYW55500. Keep it in SDIO default-speed mode. */
    if (hw_desc == &sdhc0_hw_desc)
    {
        sdio->host->freq_max = CY_SDIO_DEFAULT_FREQ_HZ;
        LOG_I("Wi-Fi SDIO clock limited to %u Hz", (unsigned)sdio->host->freq_max);
    }
#endif'''
if driver.count(old) != 1:
    raise SystemExit(f'{source}: expected exactly one SDIO clock initialization')
driver = driver.replace(old, new)

old = '    /* DMA constraints */'
new = '''    if (sdio->host->freq_max <= CY_SDIO_DEFAULT_FREQ_HZ)
    {
        sdio->host->flags &= ~MMCSD_SUP_HIGHSPEED;
    }

    /* DMA constraints */'''
if driver.count(old) != 1:
    raise SystemExit(f'{source}: expected exactly one SDIO host capability block')
target.write_text(driver.replace(old, new), encoding='utf-8')
