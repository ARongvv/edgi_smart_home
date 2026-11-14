#include <stdio.h>
#include <string.h>
#include "port/sca_http_body.h"

static int failures;
static char output[4096];
static size_t output_len;

static int collect(const void *data, size_t len, void *user)
{
    (void)user;
    if (output_len + len > sizeof(output)) return -1;
    memcpy(output + output_len, data, len);
    output_len += len;
    return 0;
}

static int reject(const void *data, size_t len, void *user)
{
    (void)data; (void)len; (void)user;
    return -1;
}

static void expect(int condition, const char *name)
{
    if (!condition)
    {
        printf("FAIL %s\n", name);
        failures++;
    }
}

static void fragmented(const char *wire, size_t step, const char *expected)
{
    sca_http_body_parser_t p;
    size_t len = strlen(wire);
    size_t offset = 0;
    int ret = SCA_HTTP_BODY_OK;
    output_len = 0;
    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CHUNKED, 0,
                              sizeof(output), collect, NULL) == 0, "chunk init");
    while (offset < len && ret == 0)
    {
        size_t count = len - offset;
        if (count > step) count = step;
        ret = sca_http_body_feed(&p, wire + offset, count);
        offset += count;
    }
    expect(ret == 0, "chunk feed");
    expect(sca_http_body_finish(&p) == 0, "chunk finish");
    expect(output_len == strlen(expected) &&
           memcmp(output, expected, output_len) == 0, "chunk payload");
}

int main(void)
{
    static const char chunked[] =
        "4;foo=bar\r\nWiki\r\n5;x=\";still extension\"\r\npedia\r\n"
        "0\r\nX-Checksum: abc\r\n\r\n";
    sca_http_body_parser_t p;
    size_t i;

    for (i = 1; i <= strlen(chunked); i++) fragmented(chunked, i, "Wikipedia");

    output_len = 0;
    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CONTENT_LENGTH, 5,
                              5, collect, NULL) == 0, "length init");
    expect(sca_http_body_feed(&p, "hello", 5) == 0, "length feed");
    expect(sca_http_body_finish(&p) == 0 && output_len == 5, "length done");
    expect(sca_http_body_feed(&p, "x", 1) != 0, "length overflow rejected");

    output_len = 0;
    expect(sca_http_body_init(&p, SCA_HTTP_BODY_UNTIL_EOF, 0,
                              32, collect, NULL) == 0, "eof init");
    expect(sca_http_body_feed(&p, "{\"error\":", 9) == 0, "401 fragment 1");
    expect(sca_http_body_feed(&p, "\"unauthorized\"}", 15) == 0, "401 fragment 2");
    expect(sca_http_body_finish(&p) == 0, "401 close-delimited finish");
    expect(output_len == 24 && memcmp(output, "{\"error\":\"unauthorized\"}", 24) == 0,
           "401 body preserved");

    output_len = 0;
    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CONTENT_LENGTH, 10,
                              16, collect, NULL) == 0, "truncated init");
    expect(sca_http_body_feed(&p, "short", 5) == 0, "truncated feed");
    expect(sca_http_body_finish(&p) != 0, "truncated content-length rejected");

    output_len = 0;
    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CHUNKED, 0,
                              16, collect, NULL) == 0, "chunk truncated init");
    expect(sca_http_body_feed(&p, "5\r\nabc", 6) == 0, "chunk truncated feed");
    expect(sca_http_body_finish(&p) != 0, "truncated chunk rejected");

    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CONTENT_LENGTH, 9,
                              8, collect, NULL) == SCA_HTTP_BODY_NOSPC,
           "content-length capacity precheck");
    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CHUNKED, 0,
                              3, collect, NULL) == 0, "small capacity init");
    expect(sca_http_body_feed(&p, "4\r\nWiki\r\n0\r\n\r\n", 16) == SCA_HTTP_BODY_NOSPC,
           "chunk capacity enforced");

    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CHUNKED, 0,
                              32, reject, NULL) == 0, "cancel init");
    expect(sca_http_body_feed(&p, "1\r\nx\r\n0\r\n\r\n", 13) == SCA_HTTP_BODY_CANCELLED,
           "sink cancellation propagated");

    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CHUNKED, 0,
                              32, collect, NULL) == 0, "bad chunk init");
    expect(sca_http_body_feed(&p, "Z\r\n", 3) != 0, "invalid chunk size rejected");
    expect(sca_http_body_init(&p, SCA_HTTP_BODY_CHUNKED, 0,
                              32, collect, NULL) == 0, "timeout partial init");
    expect(sca_http_body_feed(&p, "1\r\nx\r\n0\r\n", 10) == 0, "timeout partial feed");
    expect(sca_http_body_finish(&p) != 0, "missing terminal trailer rejected");

    if (failures == 0) printf("HTTP body parser tests passed\n");
    return failures ? 1 : 0;
}
