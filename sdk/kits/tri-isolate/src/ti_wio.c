/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_wio.c — blocking frame I/O for the worker side. */
#include "ti_wio.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "zxv_api.h"

int ti_write_all(int fd, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *) p;
    while (n) {
        ssize_t k = write(fd, b, n);
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        b += (size_t) k;
        n -= (size_t) k;
    }
    return 0;
}

/* 0 all read, 1 end of stream before any byte, -1 error or short. */
static int read_all(int fd, uint8_t *b, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t k = read(fd, b + got, n - got);
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (k == 0) return got == 0 ? 1 : -1;
        got += (size_t) k;
    }
    return 0;
}

int ti_read_frame(int fd, uint32_t max_len, uint32_t *out_type, uint8_t **out_body, size_t *out_len) {
    uint8_t hdr[5];
    uint32_t len = 0;
    int rc = read_all(fd, hdr, 4);
    *out_body = NULL;
    *out_len = 0;
    if (rc != 0) return rc;
    if (ti_frame_header(hdr, max_len, &len) != ZXV_OK) return -1;
    if (read_all(fd, hdr + 4, 1) != 0) return -1;
    *out_type = hdr[4];
    if (len > 1u) {
        uint8_t *b = (uint8_t *) malloc(len - 1u);
        if (!b) return -1;
        if (read_all(fd, b, len - 1u) != 0) {
            free(b);
            return -1;
        }
        *out_body = b;
        *out_len = len - 1u;
    }
    return 0;
}

int ti_send_buf(int fd, ti_buf *b) {
    int rc = ti_write_all(fd, b->b, b->n);
    b->n = 0;
    return rc;
}
