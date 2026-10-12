/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_bearer_host.c — bearer_host.c over a real pseudo-terminal pair
 * (posix_openpt): raw mode really is raw (all 256 byte values, no echo, no
 * CR/LF translation, no ^C signal), COBS frames both ways, and the Hayes
 * driver talking to a scripted modem on the master side. */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "bearer_host.h"
#include "../../src/bearer/br_modem.h"
#include "../../src/bearer/br_serial.h"
#include "../../src/bearer/test_check.h"

static int g_master = -1, g_slave = -1;

static int open_pair(void)
{
    g_master = posix_openpt(O_RDWR | O_NOCTTY);
    if (g_master < 0 || grantpt(g_master) != 0 || unlockpt(g_master) != 0) return 0;
    const char *name = ptsname(g_master);
    if (!name) return 0;
    g_slave = bh_tty_open(name, 115200, 0);
    int fl = fcntl(g_master, F_GETFL);
    fcntl(g_master, F_SETFL, fl | O_NONBLOCK);
    return g_slave >= 0;
}

/* read exactly n bytes (or time out) */
static size_t read_n(int fd, uint8_t *p, size_t n, int ms)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = bh_tty_read(fd, p + got, n - got, ms);
        if (r <= 0) break;
        got += (size_t) r;
    }
    return got;
}

static void test_raw(void)
{
    CHECK("pty pair opened; slave opened raw at 115200", open_pair());
    struct termios t;
    tcgetattr(g_slave, &t);
    CHECK("raw: no ICANON, ECHO, ISIG, IEXTEN",
          !(t.c_lflag & (ICANON | ECHO | ISIG | IEXTEN)));
    CHECK("raw: no ICRNL, IXON, ISTRIP; no OPOST", !(t.c_iflag & (ICRNL | IXON | ISTRIP)) &&
                                                       !(t.c_oflag & OPOST));
    CHECK("raw: CS8, CREAD, CLOCAL, no parity", (t.c_cflag & CSIZE) == CS8 && (t.c_cflag & CREAD) &&
                                                    (t.c_cflag & CLOCAL) && !(t.c_cflag & PARENB));
    CHECK("raw: VMIN 0 VTIME 0", t.c_cc[VMIN] == 0 && t.c_cc[VTIME] == 0);
    CHECK("raw: speed 115200", cfgetospeed(&t) == B115200 && cfgetispeed(&t) == B115200);
    uint8_t all[256], back[256];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t) i;
    CHECK("master -> slave: write 256 byte values", bh_tty_write_all(g_master, all, 256, 1000) == 0);
    CHECK("slave reads all 256 values unchanged (^C, ^S, CR, NUL, 0xFF...)",
          read_n(g_slave, back, 256, 500) == 256 && !memcmp(back, all, 256));
    uint8_t e[8];
    CHECK("no echo back to the master", bh_tty_read(g_master, e, sizeof e, 100) == 0);
    CHECK("slave -> master: write 256 byte values", bh_tty_write_all(g_slave, all, 256, 1000) == 0);
    CHECK("master reads them unchanged (LF not turned into CR LF)",
          read_n(g_master, back, 256, 500) == 256 && !memcmp(back, all, 256));
    CHECK("read times out with 0 when idle", bh_tty_read(g_slave, back, 10, 50) == 0);
    CHECK("set_raw: 7 bit/s refused", bh_tty_set_raw(g_slave, 7, 0) == -EINVAL);
    CHECK("set_raw: unknown flag refused", bh_tty_set_raw(g_slave, 9600, 0x80) == -EINVAL);
    CHECK("set_raw: 9600 ok", bh_tty_set_raw(g_slave, 9600, 0) == 0 &&
                                  (tcgetattr(g_slave, &t), cfgetospeed(&t) == B9600));
    CHECK("open: non-tty refused", bh_tty_open("/dev/null", 9600, 0) == -ENOTTY);
    CHECK("open: missing device refused", bh_tty_open("/nonexistent/tty", 9600, 0) == -ENOENT);
    CHECK("open: bad speed refused before opening", bh_tty_open("/dev/null", 1234, 0) == -EINVAL);
}

static void test_frames(void)
{
    br_serial_rx_t rx;
    static uint8_t raw[BR_SERIAL_FRAME_MAX(4096)], out[4100];
    br_serial_rx_init(&rx, raw, sizeof raw);
    static uint8_t msg[3000];
    for (int i = 0; i < 3000; i++) msg[i] = (uint8_t) (i * 7 % 256);
    int fd = g_slave;
    CHECK("serial link send (COBS + CRC-32) over the pty", bh_serial_link_send(&fd, NULL, msg, 3000) == BR_OK);
    uint8_t buf[512];
    int got = 0;
    uint32_t ol = 0;
    for (int it = 0; it < 200 && !got; it++) {
        ssize_t n = bh_tty_read(g_master, buf, sizeof buf, 200);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i++)
            if (br_serial_rx_byte(&rx, buf[i], out, sizeof out, &ol) == 1) got = 1;
    }
    CHECK("master decodes the 3000-byte frame exactly", got && ol == 3000 && !memcmp(out, msg, 3000));
    CHECK("serial link send refuses empty", bh_serial_link_send(&fd, NULL, msg, 0) == BR_ERR_ARG);
    /* garbage then a frame, master -> slave */
    uint8_t s[64];
    memcpy(s, "\x03\x7f junk", 7);
    int32_t k = br_serial_frame((const uint8_t *) "pty ok", 6, s + 7, 50);
    bh_tty_write_all(g_master, s, (size_t) (7 + k), 1000);
    br_serial_rx_init(&rx, raw, sizeof raw);
    got = 0;
    ssize_t n = bh_tty_read(g_slave, buf, sizeof buf, 300);
    for (ssize_t i = 0; i < n; i++)
        if (br_serial_rx_byte(&rx, buf[i], out, sizeof out, &ol) == 1) got = 1;
    CHECK("slave resyncs after garbage and decodes the frame", got && ol == 6 && !memcmp(out, "pty ok", 6));
}

/* scripted modem on the master side */
static int g_events[16], g_nev;
static void on_ev(void *ctx, int ev, uint32_t arg)
{
    (void) ctx;
    (void) arg;
    if (g_nev < 16) g_events[g_nev++] = ev;
}
static uint8_t g_data[256];
static int g_dn;
static void on_data(void *ctx, const uint8_t *p, uint32_t n)
{
    (void) ctx;
    memcpy(g_data + g_dn, p, n);
    g_dn += (int) n;
}

static int expect_cmd(const char *cmd)
{
    char b[64];
    size_t want = strlen(cmd);
    size_t got = read_n(g_master, (uint8_t *) b, want, 500);
    return got == want && !memcmp(b, cmd, want);
}

static void feed(br_modem_t *m, uint64_t now)
{
    uint8_t b[256];
    ssize_t n;
    while ((n = bh_tty_read(g_slave, b, sizeof b, 100)) > 0) br_modem_rx(m, b, (uint32_t) n, now);
}

static void test_modem(void)
{
    int fd = g_slave;
    br_modem_t m;
    br_modem_ops_t ops = {&fd, bh_modem_write, on_data, on_ev};
    br_modem_init(&m, &ops);
    uint64_t now = 1000;
    br_modem_start(&m, now);
    CHECK("pty modem: driver wrote ATZ\\r", expect_cmd("ATZ\r"));
    bh_tty_write_all(g_master, (const uint8_t *) "ATZ\r\r\nOK\r\n", 10, 500);
    feed(&m, now);
    CHECK("pty modem: then ATE0V1Q0X4\\r", expect_cmd("ATE0V1Q0X4\r"));
    bh_tty_write_all(g_master, (const uint8_t *) "\r\nOK\r\n", 6, 500);
    feed(&m, now);
    CHECK("pty modem: READY", m.state == BR_MDM_READY && g_nev == 1 && g_events[0] == BR_MDM_EV_READY);
    br_modem_dial(&m, "5551234", now);
    CHECK("pty modem: ATD5551234\\r", expect_cmd("ATD5551234\r"));
    bh_tty_write_all(g_master, (const uint8_t *) "\r\nCONNECT 9600\r\nremote", 22, 500);
    feed(&m, now);
    CHECK("pty modem: CONNECT and the first data", m.state == BR_MDM_ONLINE && g_dn == 6 &&
                                                       !memcmp(g_data, "remote", 6));
    br_modem_send(&m, (const uint8_t *) "local", 5, now);
    CHECK("pty modem: data to the remote end", expect_cmd("local"));
    br_modem_hangup(&m, now);
    uint8_t b[8];
    CHECK("pty modem: nothing during the guard time", bh_tty_read(g_master, b, sizeof b, 100) == 0);
    now += 1000;
    br_modem_tick(&m, now);
    CHECK("pty modem: +++ after the guard time", expect_cmd("+++"));
    now += 1000;
    bh_tty_write_all(g_master, (const uint8_t *) "\r\nOK\r\n", 6, 500);
    feed(&m, now);
    CHECK("pty modem: ATH0\\r", expect_cmd("ATH0\r"));
    bh_tty_write_all(g_master, (const uint8_t *) "\r\nOK\r\n", 6, 500);
    feed(&m, now);
    CHECK("pty modem: hung up, READY", m.state == BR_MDM_READY &&
                                           g_events[g_nev - 1] == BR_MDM_EV_DISCONNECTED);
}

int main(void)
{
    test_raw();
    if (g_slave >= 0) {
        test_frames();
        test_modem();
    }
    CHECK("close slave", bh_tty_close(g_slave) == 0);
    CHECK("close twice refused", bh_tty_close(g_slave) == -EBADF);
    close(g_master);
    return check_summary();
}
