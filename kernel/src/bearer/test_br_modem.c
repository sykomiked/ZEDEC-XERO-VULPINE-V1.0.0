/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_br_modem.c — the Hayes AT driver against a scripted fake modem that
 * implements echo, verbose result codes, dialling outcomes, data mode, the
 * "+++" escape with guard time on both sides, and ATH0. COBS frames are
 * carried across the data connection end to end. */
#include <string.h>

#include "br_modem.h"
#include "br_serial.h"
#include "test_check.h"

/* ===== the fake modem ===== */
typedef struct {
    int mode; /* 0 command, 1 data */
    int echo, silent, ignore_escape;
    char line[256];
    int ln;
    uint8_t out[8192]; /* bytes for the driver */
    int on;
    uint8_t data[8192]; /* data received in data mode */
    int dn;
    const char *dial_result;
    uint64_t last_rx;
    int plus;
    int hung_up;
    char cmds[32][64];
    int ncmds;
} fake_t;

static fake_t F;
static uint64_t g_now;

static void fq(const char *s)
{
    size_t n = strlen(s);
    memcpy(F.out + F.on, s, n);
    F.on += (int) n;
}

static void fm_cmd(void)
{
    F.line[F.ln] = 0;
    if (F.ncmds < 32) {
        snprintf(F.cmds[F.ncmds], sizeof F.cmds[F.ncmds], "%.63s", F.line);
        F.ncmds++;
    }
    if (F.silent) return;
    if (!strcmp(F.line, "ATZ")) {
        F.echo = 1;
        fq("\r\nOK\r\n");
    } else if (!strcmp(F.line, "ATE0V1Q0X4")) {
        F.echo = 0;
        fq("\r\nOK\r\n");
    } else if (!strncmp(F.line, "ATD", 3) || !strcmp(F.line, "ATA")) {
        if (!F.dial_result) return; /* never answers */
        fq("\r\n");
        fq(F.dial_result);
        fq("\r\n");
        if (!strncmp(F.dial_result, "CONNECT", 7)) F.mode = 1;
    } else if (!strcmp(F.line, "ATH0")) {
        F.hung_up = 1;
        fq("\r\nOK\r\n");
    } else if (F.ln == 0) {
        /* bare CR */
    } else {
        fq("\r\nERROR\r\n");
    }
}

static int fm_write(void *ctx, const uint8_t *p, uint32_t n)
{
    (void) ctx;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b = p[i];
        if (F.mode == 1) {
            if (b == '+' && (F.plus > 0 || g_now >= F.last_rx + 1000)) {
                F.plus++;
            } else {
                for (; F.plus > 0; F.plus--) F.data[F.dn++] = '+';
                F.data[F.dn++] = b;
            }
            F.last_rx = g_now;
            continue;
        }
        if (F.echo) F.out[F.on++] = b;
        if (b == '\r') {
            fm_cmd();
            F.ln = 0;
        } else if (F.ln < 255) {
            F.line[F.ln++] = (char) b;
        }
    }
    return 0;
}

static void fm_tick(void)
{
    if (F.mode == 1 && F.plus == 3 && g_now >= F.last_rx + 1000 && !F.ignore_escape) {
        F.mode = 0;
        F.plus = 0;
        fq("\r\nOK\r\n");
    }
}

/* ===== driver side ===== */
static int g_ev[64], g_arg[64], g_nev;
static uint8_t g_rx[8192];
static int g_rxn;

static void on_event(void *ctx, int ev, uint32_t arg)
{
    (void) ctx;
    if (g_nev < 64) {
        g_ev[g_nev] = ev;
        g_arg[g_nev] = (int) arg;
        g_nev++;
    }
}

static void on_data(void *ctx, const uint8_t *p, uint32_t n)
{
    (void) ctx;
    memcpy(g_rx + g_rxn, p, n);
    g_rxn += (int) n;
}

static br_modem_t M;

static void pump(void)
{
    fm_tick();
    if (F.on) {
        uint8_t tmp[8192];
        int n = F.on;
        memcpy(tmp, F.out, (size_t) n);
        F.on = 0;
        br_modem_rx(&M, tmp, (uint32_t) n, g_now);
    }
    br_modem_tick(&M, g_now);
    fm_tick();
    if (F.on) {
        uint8_t tmp[8192];
        int n = F.on;
        memcpy(tmp, F.out, (size_t) n);
        F.on = 0;
        br_modem_rx(&M, tmp, (uint32_t) n, g_now);
    }
}

static int last_ev(void)
{
    return g_nev ? g_ev[g_nev - 1] : 0;
}
static int last_arg(void)
{
    return g_nev ? g_arg[g_nev - 1] : -1;
}

static void reset(const char *dial_result)
{
    memset(&F, 0, sizeof F);
    F.echo = 1;
    F.dial_result = dial_result;
    g_nev = 0;
    g_rxn = 0;
    g_now = 100000;
    br_modem_ops_t ops = {NULL, fm_write, on_data, on_event};
    br_modem_init(&M, &ops);
}

static int start_ready(const char *dial_result)
{
    reset(dial_result);
    if (br_modem_start(&M, g_now) != BR_OK) return 0;
    for (int i = 0; i < 4; i++) pump();
    return M.state == BR_MDM_READY && last_ev() == BR_MDM_EV_READY;
}

static void test_parse(void)
{
    uint32_t r = 7;
    CHECK("parse OK", br_modem_parse_result("OK", 2, &r) == BR_MDM_R_OK);
    CHECK("parse CONNECT without rate",
          br_modem_parse_result("CONNECT", 7, &r) == BR_MDM_R_CONNECT && r == 0);
    CHECK("parse CONNECT 33600",
          br_modem_parse_result("CONNECT 33600", 13, &r) == BR_MDM_R_CONNECT && r == 33600);
    CHECK("parse CONNECT 115200/ARQ",
          br_modem_parse_result("CONNECT 115200/ARQ", 18, &r) == BR_MDM_R_CONNECT && r == 115200);
    CHECK("CONNECTED is not a result", br_modem_parse_result("CONNECTED", 9, &r) == BR_MDM_R_NONE);
    CHECK("CONNECT x is not a result", br_modem_parse_result("CONNECT x", 9, &r) == BR_MDM_R_NONE);
    CHECK("CONNECT 96k is not a result",
          br_modem_parse_result("CONNECT 96k", 11, &r) == BR_MDM_R_NONE);
    CHECK("'OK ' (trailing space) is not a result",
          br_modem_parse_result("OK ", 3, &r) == BR_MDM_R_NONE);
    CHECK("parse NO DIAL TONE spelling",
          br_modem_parse_result("NO DIAL TONE", 12, &r) == BR_MDM_R_NO_DIALTONE);
    CHECK("parse BUSY / NO ANSWER / ERROR / RING / NO CARRIER",
          br_modem_parse_result("BUSY", 4, &r) == BR_MDM_R_BUSY &&
              br_modem_parse_result("NO ANSWER", 9, &r) == BR_MDM_R_NO_ANSWER &&
              br_modem_parse_result("ERROR", 5, &r) == BR_MDM_R_ERROR &&
              br_modem_parse_result("RING", 4, &r) == BR_MDM_R_RING &&
              br_modem_parse_result("NO CARRIER", 10, &r) == BR_MDM_R_NO_CARRIER);
    CHECK("dial string 555-1234 refused ('-')", !br_modem_dial_ok("555-1234"));
    CHECK("dial string T5551234,,*70# ok", br_modem_dial_ok("T5551234,,*70#"));
    CHECK("dial string with ';' refused (no command smuggling)", !br_modem_dial_ok("5551234;H"));
    CHECK("dial string ending in ';' (stay in command mode) refused",
          !br_modem_dial_ok("5551234;"));
    CHECK("dial string with CR refused", !br_modem_dial_ok("555\rATH"));
    CHECK("empty dial string refused", !br_modem_dial_ok(""));
    CHECK("40-digit dial string ok", br_modem_dial_ok("1234567890123456789012345678901234567890"));
    CHECK("41-digit dial string refused",
          !br_modem_dial_ok("12345678901234567890123456789012345678901"));
    CHECK("lower-case 't' refused", !br_modem_dial_ok("t555"));
}

static void test_session(void)
{
    CHECK("init: ATZ then ATE0V1Q0X4, READY", start_ready("CONNECT 33600") && F.ncmds == 2 &&
                                                  !strcmp(F.cmds[0], "ATZ") &&
                                                  !strcmp(F.cmds[1], "ATE0V1Q0X4"));
    CHECK("echo of ATZ was ignored (no ERROR, no extra event)", g_nev == 1);
    CHECK("dial with bad characters refused before anything is sent",
          br_modem_dial(&M, "555;ATH", g_now) == BR_ERR_ARG && F.ncmds == 2);
    /* dial, CONNECT and data in the same burst */
    CHECK("dial", br_modem_dial(&M, "T5551234", g_now) == BR_OK && M.state == BR_MDM_DIALING);
    CHECK("modem saw ATDT5551234", F.ncmds == 3 && !strcmp(F.cmds[2], "ATDT5551234"));
    fq("hello"); /* remote data right behind CONNECT\r\n */
    pump();
    CHECK("CONNECT 33600: ONLINE, event with the rate",
          M.state == BR_MDM_ONLINE && last_ev() == BR_MDM_EV_CONNECTED && last_arg() == 33600 &&
              M.connect_rate == 33600);
    CHECK("data after CONNECT\\r\\n delivered exactly (LF skipped)",
          g_rxn == 5 && !memcmp(g_rx, "hello", 5));
    CHECK("dial while online refused", br_modem_dial(&M, "1", g_now) == BR_ERR_STATE);
    /* a COBS frame across the call */
    uint8_t fr[64];
    int32_t fl = br_serial_frame((const uint8_t *) "settle#42", 9, fr, sizeof fr);
    CHECK("send a COBS frame in data mode", br_modem_send(&M, fr, (uint32_t) fl, g_now) == BR_OK);
    br_serial_rx_t srx;
    uint8_t raw[128], out[128];
    br_serial_rx_init(&srx, raw, sizeof raw);
    int got = 0;
    uint32_t ol = 0;
    for (int i = 0; i < F.dn; i++)
        if (br_serial_rx_byte(&srx, F.data[i], out, sizeof out, &ol) == 1) got++;
    CHECK("remote end decodes the frame", got == 1 && ol == 9 && !memcmp(out, "settle#42", 9));
    /* escape and hang up */
    g_now += 10;
    br_modem_send(&M, (const uint8_t *) "x", 1, g_now);
    uint64_t t0 = g_now;
    CHECK("hangup starts the escape",
          br_modem_hangup(&M, g_now) == BR_OK && M.state == BR_MDM_ESC_PRE);
    CHECK("send refused during the escape (guard silence)",
          br_modem_send(&M, (const uint8_t *) "y", 1, g_now) == BR_ERR_STATE);
    g_now = t0 + 999;
    pump();
    CHECK("no '+++' before 1 s of silence", F.plus == 0 && M.state == BR_MDM_ESC_PRE);
    g_now = t0 + 1000;
    pump();
    CHECK("'+++' written after exactly the guard time", F.plus == 3 && M.state == BR_MDM_ESC_POST);
    g_now = t0 + 1999;
    pump();
    CHECK("modem waits its own guard time after '+++'", F.mode == 1 && M.state == BR_MDM_ESC_POST);
    g_now = t0 + 2000;
    pump();
    CHECK("OK after the guard, ATH0 sent, hung up",
          F.hung_up && !strcmp(F.cmds[F.ncmds - 1], "ATH0") && M.state == BR_MDM_READY);
    CHECK("disconnected event: local hang-up",
          last_ev() == BR_MDM_EV_DISCONNECTED && last_arg() == BR_MDM_R_LOCAL);
    CHECK("the '+++' never reached the data stream", F.dn > 0 && F.data[F.dn - 1] == 'x');
}

static void test_dial_failures(void)
{
    static const struct {
        const char *res;
        int code;
    } cases[] = {{"BUSY", BR_MDM_R_BUSY},
                 {"NO CARRIER", BR_MDM_R_NO_CARRIER},
                 {"NO DIALTONE", BR_MDM_R_NO_DIALTONE},
                 {"NO ANSWER", BR_MDM_R_NO_ANSWER},
                 {"ERROR", BR_MDM_R_ERROR}};
    for (int i = 0; i < 5; i++) {
        int ok = start_ready(cases[i].res);
        br_modem_dial(&M, "5551234", g_now);
        pump();
        ok = ok && M.state == BR_MDM_READY && last_ev() == BR_MDM_EV_DIAL_FAILED &&
             last_arg() == cases[i].code;
        char name[80];
        snprintf(name, sizeof name, "dial result %s -> DIAL_FAILED, back to READY", cases[i].res);
        CHECK(name, ok);
    }
    start_ready(NULL);
    br_modem_dial(&M, "5551234", g_now);
    uint64_t t0 = g_now;
    g_now = t0 + 59999;
    pump();
    CHECK("no answer to ATD: still dialling at 59.999 s", M.state == BR_MDM_DIALING);
    g_now = t0 + 60000;
    pump();
    CHECK("dial timeout at 60 s: abort, DIAL_FAILED(TIMEOUT)",
          M.state == BR_MDM_READY && last_ev() == BR_MDM_EV_DIAL_FAILED &&
              last_arg() == BR_MDM_R_TIMEOUT);
}

static void test_carrier(void)
{
    start_ready("CONNECT 9600");
    br_modem_dial(&M, "1", g_now);
    pump();
    fq("abc\r\nNO CARR");
    pump();
    CHECK("partial NO CARRIER does not hang up", M.state == BR_MDM_ONLINE);
    fq("Y\r\ndef");
    pump();
    CHECK("all bytes still delivered as data",
          g_rxn == 3 + 9 + 6 && !memcmp(g_rx + 12, "Y\r\ndef", 6));
    fq("\r\nNO CARRIER\r\nzzz");
    pump();
    CHECK("NO CARRIER in the stream ends the call", M.state == BR_MDM_READY &&
                                                        last_ev() == BR_MDM_EV_DISCONNECTED &&
                                                        last_arg() == BR_MDM_R_NO_CARRIER);
    start_ready("CONNECT");
    br_modem_dial(&M, "1", g_now);
    pump();
    CHECK("CONNECT without a rate", M.state == BR_MDM_ONLINE && M.connect_rate == 0);
    br_modem_carrier_lost(&M, g_now);
    CHECK("DCD drop ends the call", M.state == BR_MDM_READY && last_arg() == BR_MDM_R_DCD);
    /* incoming call */
    start_ready("CONNECT 2400");
    fq("\r\nRING\r\n");
    pump();
    CHECK("RING reported", last_ev() == BR_MDM_EV_RING);
    CHECK("answer sends ATA and connects",
          br_modem_answer(&M, g_now) == BR_OK && (pump(), M.state == BR_MDM_ONLINE) &&
              !strcmp(F.cmds[F.ncmds - 1], "ATA") && M.connect_rate == 2400);
    CHECK("answer when not READY refused", br_modem_answer(&M, g_now) == BR_ERR_STATE);
}

static void test_timeouts(void)
{
    reset("CONNECT");
    F.silent = 1;
    br_modem_start(&M, g_now);
    uint64_t t0 = g_now;
    g_now = t0 + 4999;
    pump();
    CHECK("silent modem: still INIT at 4.999 s", M.state == BR_MDM_INIT);
    g_now = t0 + 5000;
    pump();
    CHECK("silent modem: FAILED at 5 s",
          M.state == BR_MDM_FAILED && last_ev() == BR_MDM_EV_FAILED && last_arg() == BR_MDM_INIT);
    CHECK("dial in FAILED refused", br_modem_dial(&M, "1", g_now) == BR_ERR_STATE);
    /* modem that ignores the escape */
    start_ready("CONNECT 1200");
    br_modem_dial(&M, "1", g_now);
    pump();
    F.ignore_escape = 1;
    t0 = g_now;
    br_modem_hangup(&M, g_now);
    g_now = t0 + 1000;
    pump();
    g_now = t0 + 1000 + 1000 + 4999;
    pump();
    CHECK("escape not answered: still waiting at guard + 5 s - 1 ms", M.state == BR_MDM_ESC_POST);
    g_now += 1;
    pump();
    CHECK("escape not answered: FAILED (host drops DTR)",
          M.state == BR_MDM_FAILED && last_arg() == BR_MDM_ESC_POST);
    /* init answered with ERROR */
    reset(NULL);
    br_modem_start(&M, g_now);
    F.on = 0;
    fq("\r\nERROR\r\n");
    pump();
    CHECK("ERROR to ATZ: FAILED", M.state == BR_MDM_FAILED);
    /* over-long line is discarded, the OK after it still counts */
    reset(NULL);
    F.silent = 1;
    br_modem_start(&M, g_now);
    char longl[200];
    memset(longl, 'Q', sizeof longl);
    memcpy(F.out, longl, sizeof longl);
    F.on = (int) sizeof longl;
    fq("OK\r\n"); /* glued to the long line: one 202-char line, discarded */
    pump();
    CHECK("over-long line (even ending in OK) ignored", M.state == BR_MDM_INIT && M.init_step == 0);
    fq("\r\nOK\r\n");
    pump();
    CHECK("next proper OK advances init", M.init_step == 1);
    CHECK("init refuses NULL write",
          br_modem_init(&M, &(br_modem_ops_t){NULL, NULL, NULL, NULL}) == BR_ERR_ARG);
}

int main(void)
{
    test_parse();
    test_session();
    test_dial_failures();
    test_carrier();
    test_timeouts();
    return check_summary();
}
