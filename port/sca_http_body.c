/*
 * Copyright (c) 2026, Edgi-Talk SmartHome
 * SPDX-License-Identifier: Apache-2.0
 */
#include "port/sca_http_body.h"

#define SCA_HTTP_MAX_TRAILER_LINE 1024u

static int emit(sca_http_body_parser_t *p, const unsigned char *data, size_t len)
{
    if (len == 0)
    {
        return SCA_HTTP_BODY_OK;
    }
    if (len > p->limit - p->total)
    {
        p->state = SCA_HTTP_BODY_STATE_FAILED;
        return SCA_HTTP_BODY_NOSPC;
    }
    p->total += len;
    if (p->sink && p->sink(data, len, p->user) != 0)
    {
        p->state = SCA_HTTP_BODY_STATE_FAILED;
        return SCA_HTTP_BODY_CANCELLED;
    }
    return SCA_HTTP_BODY_OK;
}

static int hex_value(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int begin_chunk(sca_http_body_parser_t *p)
{
    if (p->size_digits == 0)
    {
        p->state = SCA_HTTP_BODY_STATE_FAILED;
        return SCA_HTTP_BODY_ERROR;
    }
    if (p->chunk_size > p->limit - p->total)
    {
        p->state = SCA_HTTP_BODY_STATE_FAILED;
        return SCA_HTTP_BODY_NOSPC;
    }
    p->remaining = p->chunk_size;
    p->chunk_size = 0;
    p->size_digits = 0;
    p->in_extension = 0;
    p->trailer_line = 0;
    p->state = p->remaining ? SCA_HTTP_BODY_STATE_DATA
                            : SCA_HTTP_BODY_STATE_TRAILER;
    return SCA_HTTP_BODY_OK;
}

int sca_http_body_init(sca_http_body_parser_t *p,
                       sca_http_body_mode_t mode, size_t content_length,
                       size_t limit, sca_http_body_sink_t sink, void *user)
{
    if (!p || (mode == SCA_HTTP_BODY_CONTENT_LENGTH && content_length > limit))
    {
        return SCA_HTTP_BODY_NOSPC;
    }
    p->mode = mode;
    p->sink = sink;
    p->user = user;
    p->remaining = content_length;
    p->chunk_size = 0;
    p->total = 0;
    p->limit = limit;
    p->trailer_line = 0;
    p->size_digits = 0;
    p->in_extension = 0;
    if (mode == SCA_HTTP_BODY_CHUNKED)
    {
        p->state = SCA_HTTP_BODY_STATE_SIZE;
    }
    else if (mode == SCA_HTTP_BODY_CONTENT_LENGTH && content_length == 0)
    {
        p->state = SCA_HTTP_BODY_STATE_DONE;
    }
    else
    {
        p->state = SCA_HTTP_BODY_STATE_DATA;
    }
    return SCA_HTTP_BODY_OK;
}

int sca_http_body_feed(sca_http_body_parser_t *p, const void *data, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t offset = 0;

    if (!p || (!data && len) || p->state == SCA_HTTP_BODY_STATE_FAILED)
    {
        return SCA_HTTP_BODY_ERROR;
    }
    if (p->state == SCA_HTTP_BODY_STATE_DONE)
    {
        return len == 0 ? SCA_HTTP_BODY_OK : SCA_HTTP_BODY_ERROR;
    }

    while (offset < len && p->state != SCA_HTTP_BODY_STATE_DONE)
    {
        unsigned char ch = bytes[offset];
        int ret = SCA_HTTP_BODY_OK;

        if (p->state == SCA_HTTP_BODY_STATE_DATA)
        {
            size_t available = len - offset;
            size_t count = available;
            if (p->mode != SCA_HTTP_BODY_UNTIL_EOF && count > p->remaining)
            {
                count = p->remaining;
            }
            ret = emit(p, bytes + offset, count);
            offset += count;
            if (ret != SCA_HTTP_BODY_OK) return ret;
            if (p->mode == SCA_HTTP_BODY_CONTENT_LENGTH)
            {
                p->remaining -= count;
                if (p->remaining == 0) p->state = SCA_HTTP_BODY_STATE_DONE;
            }
            else if (p->mode == SCA_HTTP_BODY_CHUNKED)
            {
                p->remaining -= count;
                if (p->remaining == 0) p->state = SCA_HTTP_BODY_STATE_DATA_CR;
            }
            continue;
        }

        offset++;
        switch (p->state)
        {
        case SCA_HTTP_BODY_STATE_SIZE:
        {
            int digit = hex_value(ch);
            if (!p->in_extension && digit >= 0)
            {
                if (p->size_digits >= sizeof(size_t) * 2u ||
                    p->chunk_size > ((size_t)-1 - (size_t)digit) / 16u)
                {
                    p->state = SCA_HTTP_BODY_STATE_FAILED;
                    return SCA_HTTP_BODY_NOSPC;
                }
                p->chunk_size = p->chunk_size * 16u + (size_t)digit;
                p->size_digits++;
            }
            else if (!p->in_extension && ch == ';' && p->size_digits > 0)
            {
                p->in_extension = 1;
            }
            else if (ch == '\r')
            {
                p->state = SCA_HTTP_BODY_STATE_SIZE_LF;
            }
            else if (ch == '\n')
            {
                ret = begin_chunk(p);
            }
            else if (p->in_extension && ch >= 0x20u && ch != 0x7fu)
            {
                /* Extension values are ignored, including quoted delimiters. */
            }
            else
            {
                p->state = SCA_HTTP_BODY_STATE_FAILED;
                return SCA_HTTP_BODY_ERROR;
            }
            break;
        }
        case SCA_HTTP_BODY_STATE_SIZE_LF:
            if (ch != '\n') ret = SCA_HTTP_BODY_ERROR;
            else ret = begin_chunk(p);
            break;
        case SCA_HTTP_BODY_STATE_DATA_CR:
            if (ch == '\r') p->state = SCA_HTTP_BODY_STATE_DATA_LF;
            else if (ch == '\n') p->state = SCA_HTTP_BODY_STATE_SIZE;
            else ret = SCA_HTTP_BODY_ERROR;
            break;
        case SCA_HTTP_BODY_STATE_DATA_LF:
            if (ch == '\n') p->state = SCA_HTTP_BODY_STATE_SIZE;
            else ret = SCA_HTTP_BODY_ERROR;
            break;
        case SCA_HTTP_BODY_STATE_TRAILER:
            if (ch == '\r')
            {
                p->state = SCA_HTTP_BODY_STATE_TRAILER_LF;
            }
            else if (ch == '\n')
            {
                if (p->trailer_line == 0) p->state = SCA_HTTP_BODY_STATE_DONE;
                else p->trailer_line = 0;
            }
            else if (++p->trailer_line > SCA_HTTP_MAX_TRAILER_LINE)
            {
                ret = SCA_HTTP_BODY_NOSPC;
            }
            break;
        case SCA_HTTP_BODY_STATE_TRAILER_LF:
            if (ch != '\n') ret = SCA_HTTP_BODY_ERROR;
            else if (p->trailer_line == 0) p->state = SCA_HTTP_BODY_STATE_DONE;
            else
            {
                p->trailer_line = 0;
                p->state = SCA_HTTP_BODY_STATE_TRAILER;
            }
            break;
        default:
            ret = SCA_HTTP_BODY_ERROR;
            break;
        }
        if (ret != SCA_HTTP_BODY_OK)
        {
            p->state = SCA_HTTP_BODY_STATE_FAILED;
            return ret;
        }
    }
    return SCA_HTTP_BODY_OK;
}

int sca_http_body_finish(sca_http_body_parser_t *p)
{
    if (!p || p->state == SCA_HTTP_BODY_STATE_FAILED)
    {
        return SCA_HTTP_BODY_ERROR;
    }
    if (p->state == SCA_HTTP_BODY_STATE_DONE ||
        p->mode == SCA_HTTP_BODY_UNTIL_EOF)
    {
        p->state = SCA_HTTP_BODY_STATE_DONE;
        return SCA_HTTP_BODY_OK;
    }
    p->state = SCA_HTTP_BODY_STATE_FAILED;
    return SCA_HTTP_BODY_ERROR;
}

int sca_http_body_done(const sca_http_body_parser_t *p)
{
    return p && p->state == SCA_HTTP_BODY_STATE_DONE;
}
