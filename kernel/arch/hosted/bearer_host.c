/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bearer_host.c — POSIX termios glue for the serial and modem bearers
 * (rules H1..H5 in bearer_host.h). Host-only: not part of the freestanding
 * kernel build. */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE    700
#define _DARWIN_C_SOURCE /* macOS hides B57600 and up under _XOPEN_SOURCE */
#include "bearer_host.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "../../src/bearer/br_serial.h"

static int speed_of(uint32_t baud, speed_t *s)
{
    switch (baud) {
    case 1200:
        *s = B1200;
        return 0;
    case 2400:
        *s = B2400;
        return 0;
    case 4800:
        *s = B4800;
        return 0;
    case 9600:
        *s = B9600;
        return 0;
    case 19200:
        *s = B19200;
        return 0;
    case 38400:
        *s = B38400;
        return 0;
    case 57600:
        *s = B57600;
        return 0;
    case 115200:
        *s = B115200;
        return 0;
    case 230400:
        *s = B230400;
        return 0;
    default:
        return -EINVAL;
    }
}

int bh_tty_set_raw(int fd, uint32_t baud, int flags)
{
    speed_t sp;
    if (fd < 0 || (flags & ~BH_TTY_RTSCTS)) return -EINVAL;
    if (speed_of(baud, &sp) != 0) return -EINVAL;
    struct termios t;
    if (tcgetattr(fd, &t) != 0) return -errno;
    t.c_iflag &= (tcflag_t) ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON |
                              IXOFF | IXANY | INPCK);
    t.c_oflag &= (tcflag_t) ~OPOST;
    t.c_lflag &= (tcflag_t) ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= (tcflag_t) ~(CSIZE | PARENB | CSTOPB);
    t.c_cflag |= (tcflag_t) (CS8 | CREAD | CLOCAL);
#ifdef CRTSCTS
    if (flags & BH_TTY_RTSCTS)
        t.c_cflag |= CRTSCTS;
    else
        t.c_cflag &= (tcflag_t) ~CRTSCTS;
#else
    if (flags & BH_TTY_RTSCTS) return -ENOTSUP;
#endif
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (cfsetispeed(&t, sp) != 0 || cfsetospeed(&t, sp) != 0) return -errno;
    if (tcsetattr(fd, TCSANOW, &t) != 0) return -errno;
    return 0;
}

int bh_tty_open(const char *path, uint32_t baud, int flags)
{
    speed_t sp;
    if (!path || speed_of(baud, &sp) != 0) return -EINVAL;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -errno;
    if (!isatty(fd)) {
        close(fd);
        return -ENOTTY;
    }
    int rc = bh_tty_set_raw(fd, baud, flags);
    if (rc != 0) {
        close(fd);
        return rc;
    }
    (void) tcflush(fd, TCIOFLUSH);
    return fd;
}

int bh_tty_write_all(int fd, const uint8_t *p, size_t n, int timeout_ms)
{
    if (fd < 0 || (!p && n)) return -EINVAL;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w > 0) {
            off += (size_t) w;
            continue;
        }
        if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -errno;
        struct pollfd pf = {fd, POLLOUT, 0};
        int r = poll(&pf, 1, timeout_ms);
        if (r == 0) return -ETIMEDOUT;
        if (r < 0 && errno != EINTR) return -errno;
        if (r > 0 && (pf.revents & (POLLERR | POLLHUP | POLLNVAL))) return -EPIPE;
    }
    return 0;
}

ssize_t bh_tty_read(int fd, uint8_t *p, size_t cap, int timeout_ms)
{
    if (fd < 0 || !p || cap == 0) return -EINVAL;
    struct pollfd pf = {fd, POLLIN, 0};
    int r = poll(&pf, 1, timeout_ms);
    if (r == 0) return 0;
    if (r < 0) return errno == EINTR ? 0 : -errno;
    if (pf.revents & POLLIN) {
        ssize_t n = read(fd, p, cap);
        if (n > 0) return n;
        if (n == 0) return -EPIPE;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        return errno == EIO ? -EPIPE : -errno;
    }
    if (pf.revents & (POLLHUP | POLLERR | POLLNVAL)) return -EPIPE;
    return 0;
}

int bh_tty_close(int fd)
{
    if (fd < 0) return -EINVAL;
    return close(fd) == 0 ? 0 : -errno;
}

int bh_tty_set_dtr(int fd, int on)
{
#if defined(TIOCMBIS) && defined(TIOCMBIC) && defined(TIOCM_DTR)
    int bits = TIOCM_DTR;
    if (ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &bits) != 0) return -errno;
    return 0;
#else
    (void) fd;
    (void) on;
    return -ENOTSUP;
#endif
}

int bh_tty_dcd(int fd)
{
#if defined(TIOCMGET) && defined(TIOCM_CAR)
    int bits = 0;
    if (ioctl(fd, TIOCMGET, &bits) != 0) return -errno;
    return (bits & TIOCM_CAR) ? 1 : 0;
#else
    (void) fd;
    return -ENOTSUP;
#endif
}

int bh_modem_write(void *ctx, const uint8_t *p, uint32_t n)
{
    if (!ctx) return -EINVAL;
    return bh_tty_write_all(*(int *) ctx, p, n, 2000);
}

int bh_serial_link_send(void *ctx, const uint8_t to[16], const uint8_t *d, uint32_t n)
{
    (void) to;
    if (!ctx || !d || n == 0 || n > BR_SERIAL_MAX_PAYLOAD) return BR_ERR_ARG;
    size_t cap = BR_SERIAL_FRAME_MAX(n);
    uint8_t *f = (uint8_t *) malloc(cap);
    if (!f) return BR_ERR_FULL;
    int32_t k = br_serial_frame(d, n, f, (uint32_t) cap);
    int rc = k > 0 ? bh_tty_write_all(*(int *) ctx, f, (size_t) k, 2000) : -1;
    free(f);
    if (k <= 0) return BR_ERR_ARG;
    return rc == 0 ? BR_OK : BR_ERR_SEND;
}
