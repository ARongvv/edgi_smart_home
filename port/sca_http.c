/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small synchronous HTTP/HTTPS client with streaming response delivery.
 */
#include <rtthread.h>
#include <time.h>

#ifdef RT_USING_SAL
#include <sys/socket.h>
#include <sal_netdb.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#endif

#ifdef SCA_HTTP_USE_TLS
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#endif

#include "port/sca_http.h"
#include "port/sca_http_body.h"

#define DBG_TAG "sca.http"
#define SCA_HTTP_DEFAULT_TIMEOUT_MS  60000u
#define SCA_HTTP_READ_CHUNK          1024u
#define SCA_HTTP_DEFAULT_BODY_LIMIT  (1024u * 1024u)
#define SCA_IO_ERROR                 (-1)
#define SCA_IO_TIMEOUT               (-2)

#ifdef SCA_HTTP_USE_TLS
static rt_bool_t g_time_synced;

/* Public Amazon Root CA 1, used by the current api.deepseek.com chain.
 * SHA-256: 8E:CD:E6:88:4F:3D:87:B1:12:5B:A3:1A:C3:FC:B1:3D:
 *          70:16:DE:7F:57:CC:90:4F:E1:CB:97:C6:AE:98:19:6E */
static const char g_amazon_root_ca1[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
    "-----END CERTIFICATE-----\n";

/* Starfield Services Root CA G2, used by the cross-signed Amazon Root CA 1 that
 * AWS CDN 端点（api.deepseek.com）随握手一起下发。作用域是链顶端（depth=2），
 * 缺它时 verify 只剩 0x8（BADCERT_NOT_TRUSTED）。官方 demo 的 ca_file 同样
 * 会预置浏览器/系统根证书集合，这里等价地把两个根都放进信任链。
 * SHA-256: 14:65:FA:20:53:97:B8:76:FA:A6:F0:A9:95:8E:55:90:
 *          E4:0F:CC:7F:AA:4F:B7:C2:C8:67:75:21:FB:5F:B6:58 */
static const char g_starfield_services_root_ca_g2[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIID7zCCAtegAwIBAgIBADANBgkqhkiG9w0BAQsFADCBmDELMAkGA1UEBhMCVVMx\n"
    "EDAOBgNVBAgTB0FyaXpvbmExEzARBgNVBAcTClNjb3R0c2RhbGUxJTAjBgNVBAoT\n"
    "HFN0YXJmaWVsZCBUZWNobm9sb2dpZXMsIEluYy4xOzA5BgNVBAMTMlN0YXJmaWVs\n"
    "ZCBTZXJ2aWNlcyBSb290IENlcnRpZmljYXRlIEF1dGhvcml0eSAtIEcyMB4XDTA5\n"
    "MDkwMTAwMDAwMFoXDTM3MTIzMTIzNTk1OVowgZgxCzAJBgNVBAYTAlVTMRAwDgYD\n"
    "VQQIEwdBcml6b25hMRMwEQYDVQQHEwpTY290dHNkYWxlMSUwIwYDVQQKExxTdGFy\n"
    "ZmllbGQgVGVjaG5vbG9naWVzLCBJbmMuMTswOQYDVQQDEzJTdGFyZmllbGQgU2Vy\n"
    "dmljZXMgUm9vdCBDZXJ0aWZpY2F0ZSBBdXRob3JpdHkgLSBHMjCCASIwDQYJKoZI\n"
    "hvcNAQEBBQADggEPADCCAQoCggEBANUMOsQq+U7i9b4Zl1+OiFOxHz/Lz58gE20p\n"
    "OsgPfTz3a3Y4Y9k2YKibXlwAgLIvWX/2h/klQ4bnaRtSmpDhcePYLQ1Ob/bISdm2\n"
    "8xpWriu2dBTrz/sm4xq6HZYuajtYlIlHVv8loJNwU4PahHQUw2eeBGg6345AWh1K\n"
    "Ts9DkTvnVtYAcMtS7nt9rjrnvDH5RfbCYM8TWQIrgMw0R9+53pBlbQLPLJGmpufe\n"
    "hRhJfGZOozptqbXuNC66DQO4M99H67FrjSXZm86B0UVGMpZwh94CDklDhbZsc7tk\n"
    "6mFBrMnUVN+HL8cisibMn1lUaJ/8viovxFUcdUBgF4UCVTmLfwUCAwEAAaNCMEAw\n"
    "DwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFJxfAN+q\n"
    "AdcwKziIorhtSpzyEZGDMA0GCSqGSIb3DQEBCwUAA4IBAQBLNqaEd2ndOxmfZyMI\n"
    "bw5hyf2E3F/YNoHN2BtBLZ9g3ccaaNnRbobhiCPPE95Dz+I0swSdHynVv/heyNXB\n"
    "ve6SbzJ08pGCL72CQnqtKrcgfU28elUSwhXqvfdqlS5sdJ/PHLTyxQGjhdByPq1z\n"
    "qwubdQxtRbeOlKyWN7Wg0I8VRw7j6IPdj/3vQQF3zCepYoUz8jcI73HPdwbeyBkd\n"
    "iEDPfUYd/x7H4c7/I9vG+o1VTqkC50cRRj70/b17KSa7qWFiNiy2LSr2EIZkyXCn\n"
    "0q23KXB56jzaYyWf/Wi3MOxw+3WKt21gZ7IeyLnp2KhvAotnDU0mV3HaIPzBSlCN\n"
    "sSi6\n"
    "-----END CERTIFICATE-----\n";

/* DigiCert Global Root G2：api.deepseek.com 的另一组 CDN 边缘节点下发
 * leaf <- TrustAsia DV TLS RSA CA 2025 <- DigiCert Global Root G2，
 * 与 Amazon 链按节点不同而不同，因此两套根都要预置。
 * SHA-256: DF:3C:24:F9:BF:D6:66:76:1B:26:80:73:FE:06:D1:CC:
 *          8D:4F:82:A4:5B:3D:FB:37:38:9B:67:2B:89:8F:D0:65 */
static const char g_digicert_global_root_g2[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
    "MrY=\n"
    "-----END CERTIFICATE-----\n";
#endif

static int sca_strncasecmp(const char *a, const char *b, size_t n)
{
    while (n-- > 0)
    {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
        if (ca != cb) return (int)ca - (int)cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static const char *sca_header_value(const char *line, size_t name_len)
{
    const char *value = line + name_len;
    while (*value == ' ' || *value == '\t') value++;
    return value;
}

int sca_url_parse(const char *url, sca_url_t *out)
{
    const char *cursor;
    const char *host_start;
    const char *host_end;
    size_t host_len;
    size_t path_len;

    if (url == RT_NULL || out == RT_NULL) return SCA_ERR_PARAM;
    rt_memset(out, 0, sizeof(*out));
    if (rt_strncmp(url, "https://", 8) == 0)
    {
        out->tls = 1;
        out->port = 443;
        cursor = url + 8;
    }
    else if (rt_strncmp(url, "http://", 7) == 0)
    {
        out->tls = 0;
        out->port = 80;
        cursor = url + 7;
    }
    else return SCA_ERR_PARAM;

    host_start = cursor;
    while (*cursor && *cursor != ':' && *cursor != '/') cursor++;
    host_end = cursor;
    host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(out->host)) return SCA_ERR_PARAM;
    rt_memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    if (*cursor == ':')
    {
        uint32_t port = 0;
        cursor++;
        if (*cursor < '0' || *cursor > '9') return SCA_ERR_PARAM;
        while (*cursor >= '0' && *cursor <= '9')
        {
            port = port * 10u + (uint32_t)(*cursor - '0');
            if (port > 65535u) return SCA_ERR_PARAM;
            cursor++;
        }
        if (port == 0) return SCA_ERR_PARAM;
        out->port = (uint16_t)port;
    }
    if (*cursor == '\0') rt_strcpy(out->path, "/");
    else if (*cursor == '/')
    {
        path_len = rt_strlen(cursor);
        if (path_len >= sizeof(out->path)) return SCA_ERR_PARAM;
        rt_memcpy(out->path, cursor, path_len + 1);
    }
    else return SCA_ERR_PARAM;
    return SCA_OK;
}

int sca_http_init(void)
{
    return SCA_OK;
}

void sca_http_notify_time_sync(void)
{
#ifdef SCA_HTTP_USE_TLS
    if (time(RT_NULL) >= (time_t)1704067200) g_time_synced = RT_TRUE;
#endif
}

int sca_http_time_ready(void)
{
#ifdef SCA_HTTP_USE_TLS
    /* NTP 成功后 notify 会置 g_time_synced；若时钟已被其他途径
     * （手动 ntp_sync / 外部 RTC）校到 2024 年之后，也允许 HTTPS。 */
    return g_time_synced || time(RT_NULL) >= (time_t)1704067200;
#else
    return 0;
#endif
}

#ifdef RT_USING_SAL
static int sca_http_tcp_connect(const sca_url_t *url, uint32_t timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *res = RT_NULL;
    struct addrinfo *cursor;
    struct timeval tv;
    char port_text[8];
    int fd = -1;
    int ret;

    rt_memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    rt_snprintf(port_text, sizeof(port_text), "%u", (unsigned)url->port);
    ret = sal_getaddrinfo(url->host, port_text, &hints, &res);
    if (ret != 0 || res == RT_NULL)
    {
        SCA_LOGE(DBG_TAG, "DNS lookup failed for %s (%d)", url->host, ret);
        return -1;
    }
    for (cursor = res; cursor != RT_NULL; cursor = cursor->ai_next)
    {
        fd = socket(cursor->ai_family, cursor->ai_socktype, cursor->ai_protocol);
        if (fd < 0) continue;
        tv.tv_sec = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, cursor->ai_addr, cursor->ai_addrlen) == 0) break;
        closesocket(fd);
        fd = -1;
    }
    sal_freeaddrinfo(res);
    if (fd < 0) SCA_LOGE(DBG_TAG, "TCP connect failed %s:%u",
                         url->host, (unsigned)url->port);
    return fd;
}

typedef struct
{
#ifdef SCA_HTTP_USE_TLS
    mbedtls_ssl_context *ssl;
#endif
    int fd;
    rt_tick_t deadline;
} sca_io_t;

static int sca_deadline_expired(const sca_io_t *io)
{
    return (rt_int32_t)(rt_tick_get() - io->deadline) >= 0;
}

static int sca_socket_timeout(void)
{
    return errno == EAGAIN || errno == ETIMEDOUT;
}

#ifdef SCA_HTTP_USE_TLS
static int sca_ssl_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int ret = (int)send(fd, buf, len, 0);
    if (ret < 0)
    {
        if (sca_socket_timeout()) return MBEDTLS_ERR_SSL_TIMEOUT;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

static int sca_ssl_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int ret = (int)recv(fd, buf, len, 0);
    if (ret == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    if (ret < 0)
    {
        if (sca_socket_timeout()) return MBEDTLS_ERR_SSL_TIMEOUT;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}
#endif

static int sca_io_read(sca_io_t *io, void *buf, size_t len)
{
    if (sca_deadline_expired(io)) return SCA_IO_TIMEOUT;
#ifdef SCA_HTTP_USE_TLS
    if (io->ssl)
    {
        for (;;)
        {
            int ret = mbedtls_ssl_read(io->ssl, (unsigned char *)buf, len);
            if (ret > 0) return ret;
            if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
            if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
                return SCA_IO_TIMEOUT;
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
                return SCA_IO_ERROR;
            if (sca_deadline_expired(io)) return SCA_IO_TIMEOUT;
        }
    }
#endif
    {
        int ret = (int)recv(io->fd, buf, len, 0);
        if (ret < 0) return sca_socket_timeout() ? SCA_IO_TIMEOUT : SCA_IO_ERROR;
        return ret;
    }
}

static int sca_io_write_all(sca_io_t *io, const void *buf, size_t len)
{
    const unsigned char *cursor = (const unsigned char *)buf;
    while (len > 0)
    {
        int ret;
        if (sca_deadline_expired(io)) return SCA_IO_TIMEOUT;
#ifdef SCA_HTTP_USE_TLS
        if (io->ssl)
        {
            ret = mbedtls_ssl_write(io->ssl, cursor, len);
            if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
                return SCA_IO_TIMEOUT;
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;
        }
        else
#endif
        {
            ret = (int)send(io->fd, cursor, len, 0);
            if (ret < 0 && sca_socket_timeout()) return SCA_IO_TIMEOUT;
        }
        if (ret <= 0) return SCA_IO_ERROR;
        cursor += ret;
        len -= (size_t)ret;
    }
    return 0;
}

static int sca_io_read_line(sca_io_t *io, char *buf, size_t capacity)
{
    size_t used = 0;
    while (used + 1 < capacity)
    {
        char ch;
        int got = sca_io_read(io, &ch, 1);
        if (got < 0) return got;
        if (got == 0) return SCA_IO_ERROR;
        if (ch == '\n')
        {
            if (used > 0 && buf[used - 1] == '\r') used--;
            buf[used] = '\0';
            return (int)used;
        }
        buf[used++] = ch;
    }
    buf[capacity - 1] = '\0';
    return SCA_IO_ERROR;
}

static int sca_parse_content_length(const char *value, long *result)
{
    unsigned long length = 0;
    while (*value == ' ' || *value == '\t') value++;
    if (*value < '0' || *value > '9') return -1;
    while (*value >= '0' && *value <= '9')
    {
        unsigned int digit = (unsigned int)(*value - '0');
        if (length > (0x7ffffffful - digit) / 10ul) return -1;
        length = length * 10ul + digit;
        value++;
    }
    while (*value == ' ' || *value == '\t') value++;
    if (*value != '\0') return -1;
    *result = (long)length;
    return 0;
}

static int sca_is_chunked(const char *value, int *supported)
{
    *supported = 1;
    while (*value == ' ' || *value == '\t') value++;
    if (sca_strncasecmp(value, "chunked", 7) != 0) return 0;
    value += 7;
    while (*value == ' ' || *value == '\t') value++;
    return *value == '\0';
}
#endif /* RT_USING_SAL */

int sca_http_request(const sca_http_request_t *req, sca_http_response_t *rsp)
{
#ifdef RT_USING_SAL
    sca_url_t url;
    sca_io_t io;
    sca_http_body_parser_t body;
    sca_http_body_mode_t body_mode = SCA_HTTP_BODY_UNTIL_EOF;
    char line[256];
    char request_header[512];
    uint32_t timeout_ms;
    long content_length = -1;
    int chunked = 0;
    int ret = SCA_ERR;
#ifdef SCA_HTTP_USE_TLS
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt ca;
    rt_bool_t tls_ready = RT_FALSE;
#endif

    if (req == RT_NULL || rsp == RT_NULL || req->url == RT_NULL) return SCA_ERR_PARAM;
    rt_memset(rsp, 0, sizeof(*rsp));
    rsp->content_length = -1;
    if (req->response_header_buf && req->response_header_size > 0)
        req->response_header_buf[0] = '\0';
    if (sca_url_parse(req->url, &url) != SCA_OK) return SCA_ERR_PARAM;
#ifndef SCA_HTTP_USE_TLS
    if (url.tls)
    {
        SCA_LOGE(DBG_TAG, "HTTPS rejected: TLS is not compiled");
        return SCA_ERR_NOT_SUP;
    }
#else
    if (url.tls && !sca_http_time_ready())
    {
        SCA_LOGE(DBG_TAG, "HTTPS rejected: system time is not synchronized");
        return SCA_ERR_STATE;
    }
#endif

    timeout_ms = req->timeout_ms ? req->timeout_ms : SCA_HTTP_DEFAULT_TIMEOUT_MS;
    rt_memset(&io, 0, sizeof(io));
    io.fd = sca_http_tcp_connect(&url, timeout_ms);
    if (io.fd < 0) return SCA_ERR_IO;
    io.deadline = rt_tick_get() + rt_tick_from_millisecond(timeout_ms);

#ifdef SCA_HTTP_USE_TLS
    if (url.tls)
    {
        int tls_ret;
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&config);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_x509_crt_init(&ca);

        tls_ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                        (const unsigned char *)"sca", 3);
        if (tls_ret == 0)
            tls_ret = mbedtls_x509_crt_parse(&ca, (const unsigned char *)g_amazon_root_ca1,
                                             sizeof(g_amazon_root_ca1));
        if (tls_ret == 0)
            tls_ret = mbedtls_x509_crt_parse(&ca,
                                             (const unsigned char *)g_starfield_services_root_ca_g2,
                                             sizeof(g_starfield_services_root_ca_g2));
        if (tls_ret == 0)
            tls_ret = mbedtls_x509_crt_parse(&ca,
                                             (const unsigned char *)g_digicert_global_root_g2,
                                             sizeof(g_digicert_global_root_g2));
        if (tls_ret == 0)
            tls_ret = mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
                                                  MBEDTLS_SSL_TRANSPORT_STREAM,
                                                  MBEDTLS_SSL_PRESET_DEFAULT);
        if (tls_ret != 0)
        {
            SCA_LOGE(DBG_TAG, "TLS initialization failed -0x%04x", -tls_ret);
            ret = SCA_ERR;
            goto cleanup;
        }
        mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&config, &ca, RT_NULL);
        mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &ctr_drbg);
        /* 顺序与官方 examples/ssl_client2.c 一致：
         * setup -> set_hostname -> set_bio -> handshake。 */
        if (mbedtls_ssl_setup(&ssl, &config) != 0 ||
            mbedtls_ssl_set_hostname(&ssl, url.host) != 0)
        {
            SCA_LOGE(DBG_TAG, "TLS setup/hostname failed");
            ret = SCA_ERR;
            goto cleanup;
        }
        mbedtls_ssl_set_bio(&ssl, &io.fd, sca_ssl_send, sca_ssl_recv, RT_NULL);
        for (;;)
        {
            tls_ret = mbedtls_ssl_handshake(&ssl);
            if (tls_ret == 0) break;
            if (tls_ret == MBEDTLS_ERR_SSL_TIMEOUT ||
                sca_deadline_expired(&io))
            {
                ret = SCA_ERR_TIMEOUT;
                goto cleanup;
            }
            if (tls_ret != MBEDTLS_ERR_SSL_WANT_READ &&
                tls_ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
                char detail[160];
                mbedtls_strerror(tls_ret, detail, sizeof(detail));
                SCA_LOGE(DBG_TAG, "TLS handshake failed: %s, verify=0x%x",
                         detail, (unsigned)flags);
                ret = SCA_ERR_IO;
                goto cleanup;
            }
        }
        tls_ready = RT_TRUE;
        io.ssl = &ssl;
    }
#endif

    {
        const char *method_text = (req->method == SCA_HTTP_POST) ? "POST" :
                                  (req->method == SCA_HTTP_PUT) ? "PUT" : "GET";
        ret = rt_snprintf(request_header, sizeof(request_header),
                          "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n",
                          method_text, url.path, url.host);
        if (ret <= 0 || ret >= (int)sizeof(request_header) ||
            sca_io_write_all(&io, request_header, (size_t)ret) != 0)
        {
            ret = SCA_ERR_IO;
            goto cleanup;
        }
        for (size_t i = 0; i < req->header_count; i++)
        {
            ret = rt_snprintf(request_header, sizeof(request_header), "%s: %s\r\n",
                              req->headers[i].name, req->headers[i].value);
            if (ret <= 0 || ret >= (int)sizeof(request_header) ||
                sca_io_write_all(&io, request_header, (size_t)ret) != 0)
            {
                ret = SCA_ERR_IO;
                goto cleanup;
            }
        }
        if (req->body && req->body_len > 0)
            ret = rt_snprintf(request_header, sizeof(request_header),
                              "Content-Length: %u\r\n\r\n", (unsigned)req->body_len);
        else ret = rt_snprintf(request_header, sizeof(request_header), "\r\n");
        if (ret <= 0 || ret >= (int)sizeof(request_header) ||
            sca_io_write_all(&io, request_header, (size_t)ret) != 0 ||
            (req->body && req->body_len > 0 &&
             sca_io_write_all(&io, req->body, req->body_len) != 0))
        {
            ret = SCA_ERR_IO;
            goto cleanup;
        }
    }

    ret = sca_io_read_line(&io, line, sizeof(line));
    if (ret < 0)
    {
        ret = ret == SCA_IO_TIMEOUT ? SCA_ERR_TIMEOUT : SCA_ERR_IO;
        goto cleanup;
    }
    {
        const char *p = line;
        int status = 0;
        if (rt_strncmp(p, "HTTP/", 5) != 0)
        {
            ret = SCA_ERR_IO;
            goto cleanup;
        }
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (*p < '0' || *p > '9')
        {
            ret = SCA_ERR_IO;
            goto cleanup;
        }
        while (*p >= '0' && *p <= '9')
        {
            status = status * 10 + (*p - '0');
            p++;
        }
        if (status < 100 || status > 999)
        {
            ret = SCA_ERR_IO;
            goto cleanup;
        }
        rsp->status = status;
    }

    for (;;)
    {
        int len = sca_io_read_line(&io, line, sizeof(line));
        if (len < 0)
        {
            ret = len == SCA_IO_TIMEOUT ? SCA_ERR_TIMEOUT : SCA_ERR_IO;
            goto cleanup;
        }
        if (len == 0) break;
        if (sca_strncasecmp(line, "Content-Length:", 15) == 0)
        {
            long parsed;
            if (sca_parse_content_length(line + 15, &parsed) != 0 ||
                (content_length >= 0 && parsed != content_length))
            {
                ret = SCA_ERR_IO;
                goto cleanup;
            }
            content_length = parsed;
        }
        else if (sca_strncasecmp(line, "Transfer-Encoding:", 18) == 0)
        {
            int supported;
            chunked = sca_is_chunked(line + 18, &supported);
            if (!chunked || !supported)
            {
                ret = SCA_ERR_NOT_SUP;
                goto cleanup;
            }
        }
        else if (sca_strncasecmp(line, "Content-Type:", 13) == 0)
        {
            const char *v = sca_header_value(line, 13);
            size_t copy = rt_strlen(v);
            if (copy >= sizeof(rsp->content_type)) copy = sizeof(rsp->content_type) - 1;
            rt_memcpy(rsp->content_type, v, copy);
            rsp->content_type[copy] = '\0';
        }
        if (req->response_header_buf && req->response_header_size > 0)
        {
            size_t used = rt_strlen(req->response_header_buf);
            if (used + (size_t)len + 2 < req->response_header_size)
                rt_snprintf(req->response_header_buf + used,
                            req->response_header_size - used, "%s\n", line);
        }
    }
    if (chunked && content_length >= 0)
    {
        ret = SCA_ERR_IO;
        goto cleanup;
    }
    rsp->content_length = chunked ? -1 : content_length;
    if (req->headers_ready && req->headers_ready(rsp->status, req->sink_user) != 0)
    {
        ret = SCA_ERR_CANCELLED;
        goto cleanup;
    }

    if ((rsp->status >= 100 && rsp->status < 200) || rsp->status == 204 || rsp->status == 304)
    {
        body_mode = SCA_HTTP_BODY_CONTENT_LENGTH;
        content_length = 0;
    }
    else if (chunked) body_mode = SCA_HTTP_BODY_CHUNKED;
    else if (content_length >= 0) body_mode = SCA_HTTP_BODY_CONTENT_LENGTH;

    ret = sca_http_body_init(&body, body_mode,
                             content_length >= 0 ? (size_t)content_length : 0,
                             req->max_body_bytes ? req->max_body_bytes
                                                 : SCA_HTTP_DEFAULT_BODY_LIMIT,
                             req->sink, req->sink_user);
    if (ret != SCA_HTTP_BODY_OK)
    {
        ret = SCA_ERR_NOSPC;
        goto cleanup;
    }
    ret = SCA_OK;
    while (!sca_http_body_done(&body))
    {
        char data[SCA_HTTP_READ_CHUNK];
        int got = sca_io_read(&io, data, sizeof(data));
        if (got < 0)
        {
            ret = got == SCA_IO_TIMEOUT ? SCA_ERR_TIMEOUT : SCA_ERR_IO;
            break;
        }
        if (got == 0) break;
        ret = sca_http_body_feed(&body, data, (size_t)got);
        if (ret != SCA_HTTP_BODY_OK)
        {
            ret = ret == SCA_HTTP_BODY_NOSPC ? SCA_ERR_NOSPC :
                  ret == SCA_HTTP_BODY_CANCELLED ? SCA_ERR_CANCELLED : SCA_ERR_IO;
            break;
        }
    }
    if (ret == SCA_OK && sca_http_body_finish(&body) != SCA_HTTP_BODY_OK)
        ret = SCA_ERR_IO;
    rsp->body_bytes = body.total;

cleanup:
#ifdef SCA_HTTP_USE_TLS
    if (url.tls)
    {
        if (tls_ready) mbedtls_ssl_close_notify(&ssl);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&config);
        mbedtls_x509_crt_free(&ca);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
#endif
    closesocket(io.fd);
    return ret;
#else
    (void)req;
    (void)rsp;
    return SCA_ERR_NOT_SUP;
#endif
}
