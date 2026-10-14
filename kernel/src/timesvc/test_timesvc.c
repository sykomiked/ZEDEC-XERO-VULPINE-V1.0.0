/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_timesvc.c — SNTP packet checks and combining servers.
 *
 * Replies are built here from known server and client times, so the offset
 * and round trip the parser computes can be checked exactly. Every refusal
 * reason has a reply that triggers it. */
#include <stdio.h>
#include <string.h>
#include "sntp.h"
#include "timesvc.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define S        1000000LL
#define FLOOR    1791763200LL /* 2026-10-12T00:00:00Z */
#define TRUE_NOW (1792000000LL * S)
#define NONCE    0x0123456789ABCDEFULL

/* Unix microseconds as an NTP timestamp; the fraction rounds up so that
 * converting back (which rounds down) gives the same microsecond. */
static uint64_t to_ntp(int64_t unix_us)
{
    int64_t secs = unix_us / S, us = unix_us % S;
    if (us < 0) secs--, us += S;
    uint64_t frac = (((uint64_t) us << 32) + (uint64_t) S - 1) / (uint64_t) S;
    return ((uint64_t) (uint32_t) (secs + NTP_UNIX_DELTA) << 32) | frac;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (56 - 8 * i));
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (24 - 8 * i));
}

/* A good stratum-2 reply; the server receives at t2 and sends at t3. */
static void reply(uint8_t pkt[48], uint64_t nonce, int64_t t2, int64_t t3)
{
    memset(pkt, 0, 48);
    pkt[0] = (0 << 6) | (4 << 3) | 4;
    pkt[1] = 2;
    put32(pkt + 4, 0x00000800); /* root delay 1/32 s */
    put32(pkt + 8, 0x00000400); /* root dispersion 1/64 s */
    memcpy(pkt + 12, "\x0a\x00\x00\x01", 4);
    put64(pkt + 16, to_ntp(t2 - 60 * S));
    put64(pkt + 24, nonce);
    put64(pkt + 32, to_ntp(t2));
    put64(pkt + 40, to_ntp(t3));
}

static void test_request(void)
{
    uint8_t q[48];
    CHECK(sntp_build_request(q, NONCE) == 48, "request is 48 bytes");
    CHECK(q[0] == 0x23, "LI 0, version 4, mode 3 (0x%02x)", q[0]);
    int zero = 1;
    for (int i = 1; i < 40; i++) zero &= q[i] == 0;
    CHECK(zero, "nothing about the local clock is sent");
    CHECK(q[40] == 0x01 && q[47] == 0xEF, "transmit timestamp carries the nonce");
}

static void test_reply(void)
{
    uint8_t p[48];
    sntp_sample_t s;
    /* local clock is 5000 s behind; 40 ms each way; 3 ms in the server */
    int64_t local_off = -5000 * S, t1 = TRUE_NOW + local_off;
    int64_t t2 = TRUE_NOW + 40000, t3 = t2 + 3000, t4 = t1 + 83000;
    reply(p, NONCE, t2, t3);
    CHECK(sntp_parse_reply(p, 48, NONCE, t1, t4, FLOOR, &s) == SNTP_OK, "good reply accepted");
    CHECK(s.offset_us == 5000 * S, "offset is exactly 5000 s (%lld)", (long long) s.offset_us);
    CHECK(s.delay_us == 80000, "round trip is 80 ms (%lld)", (long long) s.delay_us);
    CHECK(s.server_us == t3 && s.stratum == 2 && s.leap == 0, "server time and stratum");
    CHECK(s.root_dist_us == 15625 + 15625, "root distance 1/64 + 1/64 s (%u)", s.root_dist_us);

    uint8_t b[48];
#define REFUSE(mut, want, what)                                                                    \
    do {                                                                                           \
        memcpy(b, p, 48);                                                                          \
        mut;                                                                                       \
        sntp_status_t st_ = sntp_parse_reply(b, 48, NONCE, t1, t4, FLOOR, &s);                     \
        CHECK(st_ == (want), "%s (status %d)", what, st_);                                         \
    } while (0)
    REFUSE(b[31] ^= 1, SNTP_NOT_OURS, "wrong originate timestamp");
    REFUSE(b[0] = 0x23, SNTP_BAD, "mode 3 (a client) refused");
    REFUSE(b[0] = 0x25, SNTP_BAD, "mode 5 (broadcast) refused");
    REFUSE(b[0] = 0x14, SNTP_BAD, "version 2 refused");
    REFUSE(b[0] = 0x2C, SNTP_BAD, "version 5 refused");
    REFUSE(b[0] = 0xE4 | 0x20, SNTP_UNSYNCED, "leap indicator 3 refused");
    REFUSE(b[1] = 16, SNTP_UNSYNCED, "stratum 16 refused");
    REFUSE(memset(b + 32, 0, 8), SNTP_BAD, "zero receive timestamp refused");
    REFUSE(memset(b + 40, 0, 8), SNTP_BAD, "zero transmit timestamp refused");
    REFUSE(put64(b + 40, to_ntp(t2 - 1)), SNTP_BAD, "transmit before receive refused");
    REFUSE(put32(b + 8, 16u << 16), SNTP_UNSYNCED, "root dispersion of 16 s refused");
    REFUSE(put32(b + 4, 32u << 16), SNTP_UNSYNCED, "root delay of 32 s refused");
    memcpy(b, p, 48);
    b[1] = 0;
    memcpy(b + 12, "RATE", 4);
    CHECK(sntp_parse_reply(b, 48, NONCE, t1, t4, FLOOR, &s) == SNTP_KOD && !strcmp(s.kod, "RATE"),
          "kiss-o'-death RATE reported");
    CHECK(sntp_parse_reply(p, 47, NONCE, t1, t4, FLOOR, &s) == SNTP_BAD, "short reply refused");
    CHECK(sntp_parse_reply(p, 48, NONCE, t4, t1, FLOOR, &s) == SNTP_BAD,
          "reply before request refused");
    CHECK(sntp_parse_reply(0, 48, NONCE, t1, t4, FLOOR, &s) == SNTP_BAD, "null refused");

    /* a server too slow to measure clamps the round trip at zero */
    reply(p, NONCE, t2, t2 + 90000);
    CHECK(sntp_parse_reply(p, 48, NONCE, t1, t4, FLOOR, &s) == SNTP_OK && s.delay_us == 0,
          "negative round trip clamped to 0");
}

static void test_eras(void)
{
    CHECK(sntp_ntp_to_unix_us((uint64_t) NTP_UNIX_DELTA << 32, FLOOR) == 0, "NTP 1970 is Unix 0");
    /* 2040-01-01 is in NTP era 1 (after 2036-02-07) */
    int64_t y2040 = 2208988800LL * S;
    CHECK(sntp_ntp_to_unix_us(to_ntp(y2040), FLOOR) == y2040, "2040 read in era 1");
    CHECK(sntp_ntp_to_unix_us(to_ntp(y2040), 2200000000LL) == y2040, "2040 with a 2039 pivot");
    int64_t y2035 = 2051222400LL * S;
    CHECK(sntp_ntp_to_unix_us(to_ntp(y2035), 2200000000LL) == y2035, "2035 with a 2039 pivot");
    CHECK(sntp_ntp_to_unix_us(to_ntp(TRUE_NOW + 123456), FLOOR) == TRUE_NOW + 123456,
          "microseconds survive the round trip");
}

static sntp_sample_t sample(int64_t offset_s, int64_t delay_ms)
{
    sntp_sample_t s;
    memset(&s, 0, sizeof s);
    s.offset_us = offset_s * S;
    s.delay_us = delay_ms * 1000;
    s.root_dist_us = 20000;
    s.stratum = 2;
    return s;
}

static void test_timesvc(void)
{
    timesvc_t t;
    int64_t local = 1000 * S; /* local clock: 1000 s since power-on */
    int64_t off = 1792000000LL - 1000;
    timesvc_init(&t, 0);
    CHECK(t.floor_s == ZXV_BUILD_UNIX && t.min_agree == 2, "default floor and two servers");
    CHECK(timesvc_now_s(&t, local) == 0, "time unknown before any source: TLS gets 0");

    CHECK(!timesvc_set_rtc(&t, 946684800, local), "RTC reading 2000-01-01 ignored");
    CHECK(timesvc_now_s(&t, local) == 0, "still unknown");
    CHECK(timesvc_set_rtc(&t, 1792000000, local) && t.source == TIMESVC_SRC_RTC, "RTC accepted");
    CHECK(timesvc_now_s(&t, local + 5 * S) == 1792000005, "RTC time advances with the local clock");
    CHECK(!timesvc_set_rtc(&t, FLOOR + TIMESVC_MAX_AHEAD_S + 1, local),
          "RTC 100 years ahead ignored");

    sntp_sample_t a = sample(off, 30), b = sample(off + 0, 10), c = sample(off + 900, 5);
    timesvc_add_sample(&t, &a);
    CHECK(timesvc_commit(&t, local) == TIMESVC_TOO_FEW, "one server is not enough");
    CHECK(t.source == TIMESVC_SRC_RTC, "the RTC time stays");

    timesvc_add_sample(&t, &a);
    timesvc_add_sample(&t, &b);
    timesvc_add_sample(&t, &c);
    CHECK(timesvc_commit(&t, local) == TIMESVC_SYNCED && t.agreed == 2,
          "two of three agree; the liar is outvoted");
    CHECK(timesvc_now_s(&t, local) == 1792000000 && t.source == TIMESVC_SRC_NETWORK,
          "time set from the network");
    CHECK(!timesvc_set_rtc(&t, 1795000000, local), "RTC cannot override the network");

    timesvc_t u;
    timesvc_init(&u, FLOOR);
    sntp_sample_t d = sample(off + 1, 10), e = sample(off, 400);
    timesvc_add_sample(&u, &d);
    timesvc_add_sample(&u, &e);
    CHECK(timesvc_commit(&u, local) == TIMESVC_SYNCED && timesvc_now_s(&u, local) == 1792000001,
          "1 s apart with a 400 ms round trip agree; the faster server wins");

    timesvc_add_sample(&u, &a);
    timesvc_add_sample(&u, &c);
    CHECK(timesvc_commit(&u, local) == TIMESVC_DISAGREE, "two servers that disagree set nothing");
    CHECK(timesvc_now_s(&u, local) == 1792000001, "the earlier time stays");

    sntp_sample_t c2 = sample(off + 901, 5);
    timesvc_add_sample(&u, &a);
    timesvc_add_sample(&u, &b);
    timesvc_add_sample(&u, &c);
    timesvc_add_sample(&u, &c2);
    CHECK(timesvc_commit(&u, local) == TIMESVC_DISAGREE, "a 2-2 split is not a majority");

    sntp_sample_t old1 = sample(FLOOR - 86400 - 1000, 5), old2 = sample(FLOOR - 86400 - 1000, 6);
    timesvc_add_sample(&u, &old1);
    timesvc_add_sample(&u, &old2);
    CHECK(timesvc_commit(&u, local) == TIMESVC_BELOW_FLOOR,
          "agreeing servers cannot set a time before the build");
    sntp_sample_t far1 = sample(FLOOR + TIMESVC_MAX_AHEAD_S + 10, 5);
    timesvc_add_sample(&u, &far1);
    timesvc_add_sample(&u, &far1);
    CHECK(timesvc_commit(&u, local) == TIMESVC_TOO_FAR, "a time 100 years ahead is refused");

    timesvc_t v;
    timesvc_init(&v, FLOOR);
    timesvc_set_min_agree(&v, 1);
    timesvc_add_sample(&v, &a);
    CHECK(timesvc_commit(&v, local) == TIMESVC_SYNCED, "one trusted server when configured");
    int added = 0;
    for (int i = 0; i < 12; i++) added += timesvc_add_sample(&v, &a);
    CHECK(added == (int) TIMESVC_MAX_SAMPLES, "sample list is bounded (%d)", added);
    CHECK(timesvc_commit(&v, local) == TIMESVC_SYNCED && v.n_samples == 0, "commit clears it");
    CHECK(!strcmp(timesvc_status_str(TIMESVC_DISAGREE), "servers disagree"), "status text");
}

/* End to end: three servers answer the same request; one is 15 minutes off. */
static void test_end_to_end(void)
{
    timesvc_t t;
    timesvc_init(&t, FLOOR);
    int64_t t1 = 42 * S, now = TRUE_NOW;
    int64_t skew[3] = {0, 2000, 900 * S};
    for (int i = 0; i < 3; i++) {
        uint8_t q[48], p[48];
        uint64_t nonce = NONCE + (uint64_t) i;
        sntp_build_request(q, nonce);
        reply(p, nonce, now + 20000 + skew[i], now + 21000 + skew[i]);
        sntp_sample_t s;
        if (sntp_parse_reply(p, 48, nonce, t1, t1 + 41000, FLOOR, &s) == SNTP_OK)
            timesvc_add_sample(&t, &s);
    }
    CHECK(timesvc_commit(&t, t1) == TIMESVC_SYNCED, "three servers, one wrong: synced");
    int64_t err = timesvc_now_us(&t, t1 + 41000) - (now + 41000);
    CHECK(err >= -1000 && err <= 1000, "within 1 ms of the truth (%lld us)", (long long) err);
}

int main(void)
{
    test_request();
    test_reply();
    test_eras();
    test_timesvc();
    test_end_to_end();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
