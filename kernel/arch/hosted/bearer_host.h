/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bearer_host.h — POSIX glue for the serial and modem bearers
 * (kernel/src/bearer/br_serial.h, br_modem.h): open a serial port or
 * USB-serial adapter in raw mode, read and write with timeouts, and the
 * modem control lines.
 *
 *   H1  bh_tty_open() opens O_RDWR | O_NOCTTY | O_NONBLOCK and puts the
 *       line in raw 8N1 mode: no echo, no canonical line editing, no
 *       signals from ^C/^Z, no CR/LF translation in either direction, no
 *       XON/XOFF, receiver on, modem status lines ignored for open
 *       (CLOCAL). Every byte value 0..255 passes through unchanged, which
 *       COBS frames and AX.25/KISS need.
 *   H2  Speeds: 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200,
 *       230400 bit/s; anything else is -EINVAL (no silent fallback).
 *   H3  RTS/CTS hardware flow control only when asked (BH_TTY_RTSCTS) and
 *       only where the platform defines CRTSCTS; otherwise -ENOTSUP.
 *   H4  Reads and writes use poll() with a timeout in milliseconds; a
 *       write either sends everything or reports how it failed.
 *   H5  Return values: >= 0 on success, -errno on failure.
 *
 * HONEST LIMITS
 *   Tested with a pseudo-terminal pair (posix_openpt) in
 *   test_bearer_host.c, not with real UARTs, USB adapters or modems; DTR and
 *   DCD control (TIOCMGET/TIOCMBIS) does nothing on a pty. Windows COM ports
 *   are not covered (no termios); a Windows host needs its own glue.
 */
#ifndef ZXV_BEARER_HOST_H
#define ZXV_BEARER_HOST_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define BH_TTY_RTSCTS 0x01

int bh_tty_open(const char *path, uint32_t baud, int flags);
/* Apply H1/H2/H3 to an already open descriptor. */
int bh_tty_set_raw(int fd, uint32_t baud, int flags);
int bh_tty_write_all(int fd, const uint8_t *p, size_t n, int timeout_ms);
/* Bytes read (1..cap), 0 on timeout, -errno on error, -EPIPE on hang-up. */
ssize_t bh_tty_read(int fd, uint8_t *p, size_t cap, int timeout_ms);
int bh_tty_close(int fd);
/* Modem control lines: 1/0, or -errno (e.g. on a pty). */
int bh_tty_set_dtr(int fd, int on);
int bh_tty_dcd(int fd);

/* Callbacks with ctx = pointer to an int file descriptor. */
int bh_modem_write(void *ctx, const uint8_t *p, uint32_t n);        /* br_modem_ops_t.write */
int bh_serial_link_send(void *ctx, const uint8_t to[16], const uint8_t *d, uint32_t n); /* br_link_ops_t.send */

#endif /* ZXV_BEARER_HOST_H */
