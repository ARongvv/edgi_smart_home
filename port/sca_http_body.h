/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 * SPDX-License-Identifier: Apache-2.0
 *
 * Incremental HTTP response-body framing. No OS or dynamic allocation.
 */
#ifndef __SCA_HTTP_BODY_H__
#define __SCA_HTTP_BODY_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCA_HTTP_BODY_OK         0
#define SCA_HTTP_BODY_ERROR    (-1)
#define SCA_HTTP_BODY_NOSPC    (-2)
#define SCA_HTTP_BODY_CANCELLED (-3)

typedef enum
{
    SCA_HTTP_BODY_CONTENT_LENGTH = 0,
    SCA_HTTP_BODY_CHUNKED,
    SCA_HTTP_BODY_UNTIL_EOF,
} sca_http_body_mode_t;

typedef int (*sca_http_body_sink_t)(const void *data, size_t len, void *user);

typedef enum
{
    SCA_HTTP_BODY_STATE_SIZE = 0,
    SCA_HTTP_BODY_STATE_SIZE_LF,
    SCA_HTTP_BODY_STATE_DATA,
    SCA_HTTP_BODY_STATE_DATA_CR,
    SCA_HTTP_BODY_STATE_DATA_LF,
    SCA_HTTP_BODY_STATE_TRAILER,
    SCA_HTTP_BODY_STATE_TRAILER_LF,
    SCA_HTTP_BODY_STATE_DONE,
    SCA_HTTP_BODY_STATE_FAILED,
} sca_http_body_state_t;

typedef struct
{
    sca_http_body_mode_t mode;
    sca_http_body_state_t state;
    sca_http_body_sink_t sink;
    void *user;
    size_t remaining;
    size_t chunk_size;
    size_t total;
    size_t limit;
    size_t trailer_line;
    unsigned size_digits;
    int in_extension;
} sca_http_body_parser_t;

int sca_http_body_init(sca_http_body_parser_t *parser,
                       sca_http_body_mode_t mode, size_t content_length,
                       size_t limit, sca_http_body_sink_t sink, void *user);
int sca_http_body_feed(sca_http_body_parser_t *parser,
                       const void *data, size_t len);
int sca_http_body_finish(sca_http_body_parser_t *parser);
int sca_http_body_done(const sca_http_body_parser_t *parser);

#ifdef __cplusplus
}
#endif

#endif
