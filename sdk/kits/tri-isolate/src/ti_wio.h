/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_wio.h — blocking frame I/O for the worker side (the worker program and
 * the test workers). The host side has its own non-blocking, deadline-
 * bounded I/O in ti_host.c. */
#ifndef TI_WIO_H
#define TI_WIO_H

#include <stddef.h>
#include <stdint.h>

#include "ti_frame.h"

/* The worker's end of the socket. */
#define TI_WORKER_FD 3

/* Write all bytes (retrying on EINTR). 0 on success, -1 on error. */
int ti_write_all(int fd, const void *p, size_t n);
/* Read one frame of at most `max_len` bytes: *out_type and a malloc'ed
 * body (*out_body, free it; NULL when the body is empty). 0 on success,
 * 1 on a clean end of stream before a frame, -1 on error or a bad header. */
int ti_read_frame(int fd, uint32_t max_len, uint32_t *out_type, uint8_t **out_body, size_t *out_len);
/* Send `b`'s bytes and empty it. 0 / -1. */
int ti_send_buf(int fd, ti_buf *b);

#endif /* TI_WIO_H */
