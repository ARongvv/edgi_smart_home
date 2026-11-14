/* PSE84 hardware entropy source for mbedTLS. */
#include <rtthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef SCA_HTTP_USE_TLS
#include <mbedtls/entropy.h>
#include <mbedtls/platform_time.h>
#include "cy_crypto_core.h"

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)((uint64_t)rt_tick_get() * 1000u /
                               RT_TICK_PER_SECOND);
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                          size_t *olen)
{
    size_t offset = 0;

    (void)data;
    if (!output || !olen)
    {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    *olen = 0;
    if (Cy_Crypto_Core_Enable(CRYPTO) != CY_CRYPTO_SUCCESS)
    {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    while (offset < len)
    {
        uint32_t word;
        size_t count = len - offset;
        if (Cy_Crypto_Core_Trng_Ext(CRYPTO, 32u, &word) != CY_CRYPTO_SUCCESS)
        {
            memset(output, 0, len);
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        }
        if (count > sizeof(word)) count = sizeof(word);
        memcpy(output + offset, &word, count);
        offset += count;
    }
    *olen = len;
    return 0;
}
#endif
