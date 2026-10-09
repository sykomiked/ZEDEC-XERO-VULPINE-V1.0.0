/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ehop.c — host tests for Endian Hopping (kernel/src/ehop).
 *
 * Involution of every AM depth and of whole schedules, exact depth outputs,
 * schedule determinism across nodes, key/rate/phase/hop/depth sensitivity,
 * the measured flip frequency, seal/open, tamper, replay, wrong-channel and
 * wrong-schedule rejection, tag routing, the band plan and QoS, the overlay
 * router and hook, private-network join/rotate/remove, and throughput.
 *
 * Includes pq_security.h next to ehop_internal.h so the repeated ML-DSA
 * prototypes are checked against the originals at compile time.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pq_security.h"
#include "ehop_internal.h"
#include "../mlkem/keccak.h"
#include "../tls/aead.h"

static int g_pass, g_fail;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
        }                                                                                          \
    } while (0)

static void fill(uint8_t *p, uint32_t n, uint32_t seed)
{
    uint8_t in[8];
    ehop_le32(in, seed);
    ehop_le32(in + 4, n);
    shake256(in, sizeof in, p, n);
}

static ehop_cfg_t cfg_make(uint8_t width, uint8_t band, uint8_t am, uint8_t pm_mode, uint16_t rate,
                           uint16_t hop, uint16_t phase, uint16_t step)
{
    ehop_cfg_t c;
    c.width = width;
    c.band = band;
    c.am_mask = am;
    c.pm_mode = pm_mode;
    c.fm_rate = rate;
    c.fm_hop = hop;
    c.pm_phase = phase;
    c.pm_step = step;
    return c;
}

static const uint8_t K1[32] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                               17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
static const uint8_t K2[32] = {0x99};

/* ---------------------------------------------------------------- band plan */
static void test_bands(void)
{
    ehop_cfg_t c;
    uint32_t b;
    for (b = 0; b < EHOP_BAND_COUNT; b++) {
        const ehop_band_info_t *i = ehop_band_info(b);
        CHECK(i && i->priority == b, "band priority equals band order");
        CHECK(i->max_frame <= EHOP_MAX_FRAME, "band max frame within limit");
        if (b + 1 < EHOP_BAND_COUNT)
            CHECK(i->fm_max < ehop_band_info(b + 1)->fm_min, "bands occupy disjoint FM ranges");
    }
    CHECK(ehop_band_info(EHOP_BAND_COUNT) == NULL, "no band past the plan");
    CHECK(ehop_band_for_class(EHOP_MSG_SETTLEMENT) == EHOP_BAND_SETTLE,
          "settlement -> settle band");
    CHECK(ehop_band_for_class(EHOP_MSG_PAYMENT) == EHOP_BAND_SETTLE, "payment -> settle band");
    CHECK(ehop_band_for_class(EHOP_MSG_ROUTING) == EHOP_BAND_CONTROL, "routing -> control band");
    CHECK(ehop_band_for_class(EHOP_MSG_VOICE) == EHOP_BAND_MEDIA, "voice -> media band");
    CHECK(ehop_band_for_class(EHOP_MSG_ALERT) == EHOP_BAND_CHAT, "alert -> chat band");
    CHECK(ehop_band_for_class(EHOP_MSG_FILE) == EHOP_BAND_BULK, "file -> bulk band");
    CHECK(ehop_band_for_class(999) == EHOP_BAND_BULK, "unknown -> bulk");

    c = cfg_make(4, EHOP_BAND_SETTLE, EHOP_AM_FULL, EHOP_PM_PER_FRAME, 16, 4, 3, 5);
    CHECK(ehop_cfg_check(&c) == EHOP_OK, "valid settle cfg");
    c.fm_rate = 100;
    CHECK(ehop_cfg_check(&c) == EHOP_EBAND, "FM outside band rejected");
    c = cfg_make(4, EHOP_BAND_SETTLE, EHOP_AM_NIBBLE, 0, 16, 0, 0, 0);
    CHECK(ehop_cfg_check(&c) == EHOP_EBAND, "AM outside band rejected");
    c = cfg_make(4, EHOP_BAND_MEDIA, EHOP_AM_PAIR, EHOP_PM_PER_EPOCH, 64, 0, 0, 1);
    CHECK(ehop_cfg_check(&c) == EHOP_EBAND, "PM mode outside band rejected");
    c = cfg_make(3, EHOP_BAND_CHAT, EHOP_AM_FULL, 0, 200, 0, 0, 0);
    CHECK(ehop_cfg_check(&c) == EHOP_EARG, "width 3 rejected");
    c = cfg_make(4, EHOP_BAND_CHAT, EHOP_AM_FULL, 0, 200, 200, 0, 0);
    CHECK(ehop_cfg_check(&c) == EHOP_EARG, "hop >= rate rejected");
    c = cfg_make(4, EHOP_BAND_CHAT, EHOP_AM_FULL, 0, 200, 0, 200, 0);
    CHECK(ehop_cfg_check(&c) == EHOP_EARG, "phase >= rate rejected");
    c = cfg_make(4, EHOP_BAND_CHAT, 0, 0, 200, 0, 0, 0);
    CHECK(ehop_cfg_check(&c) == EHOP_EARG, "empty AM rejected");
    c = cfg_make(8, EHOP_BAND_BULK, 0x10, 0, 600, 0, 0, 0);
    CHECK(ehop_cfg_check(&c) == EHOP_EARG, "unknown AM bit rejected");
    c = cfg_make(8, EHOP_BAND_BULK, EHOP_AM_ALL, 3, 600, 0, 0, 0);
    CHECK(ehop_cfg_check(&c) == EHOP_EARG, "unknown PM mode rejected");
}

/* ------------------------------------------------- exact AM depth outputs */
/* Find a frame whose first segment is "on" and covers the whole buffer. */
static void sched_on(ehop_schedule_t *s, const ehop_cfg_t *c)
{
    uint64_t seq;
    for (seq = 0; seq < 64; seq++) {
        ehop_schedule_init(s, K1, c, 7, 0, 0, seq);
        if (s->start_on && s->first_len >= 2) return;
    }
}

static void test_am_depths(void)
{
    static const struct {
        uint8_t width, am;
        uint8_t in[8], out[8];
    } v[] = {
        {4, EHOP_AM_FULL, {1, 2, 3, 4}, {4, 3, 2, 1}},
        {4, EHOP_AM_HALF, {1, 2, 3, 4}, {3, 4, 1, 2}},
        {4, EHOP_AM_PAIR, {1, 2, 3, 4}, {2, 1, 4, 3}},
        {4, EHOP_AM_NIBBLE, {0x12, 0x34, 0x56, 0x78}, {0x21, 0x43, 0x65, 0x87}},
        {8, EHOP_AM_FULL, {1, 2, 3, 4, 5, 6, 7, 8}, {8, 7, 6, 5, 4, 3, 2, 1}},
        {8, EHOP_AM_HALF, {1, 2, 3, 4, 5, 6, 7, 8}, {5, 6, 7, 8, 1, 2, 3, 4}},
        {8, EHOP_AM_PAIR, {1, 2, 3, 4, 5, 6, 7, 8}, {2, 1, 4, 3, 6, 5, 8, 7}},
        {2, EHOP_AM_FULL, {1, 2}, {2, 1}},
        {2, EHOP_AM_HALF, {1, 2}, {2, 1}},
        {2, EHOP_AM_PAIR, {1, 2}, {2, 1}},
    };
    uint32_t i;
    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        ehop_cfg_t c = cfg_make(v[i].width, EHOP_BAND_CHAT, v[i].am, 0, 300, 0, 0, 0);
        ehop_schedule_t s;
        uint8_t b[8];
        memcpy(b, v[i].in, 8);
        sched_on(&s, &c);
        CHECK(s.start_on, "found a frame starting with carrier on");
        ehop_apply(&s, b, v[i].width);
        CHECK(memcmp(b, v[i].out, v[i].width) == 0, "AM depth exact output");
        ehop_apply(&s, b, v[i].width);
        CHECK(memcmp(b, v[i].in, v[i].width) == 0, "AM depth is an involution");
    }
    /* A short tail word (3 bytes of a 4-byte word), each depth. */
    {
        static const uint8_t am[4] = {EHOP_AM_FULL, EHOP_AM_HALF, EHOP_AM_PAIR, EHOP_AM_NIBBLE};
        static const uint8_t exp[4][3] = {{3, 2, 1}, {3, 2, 1}, {2, 1, 3}, {0x10, 0x20, 0x30}};
        for (i = 0; i < 4; i++) {
            ehop_cfg_t c = cfg_make(4, EHOP_BAND_CHAT, am[i], 0, 300, 0, 0, 0);
            ehop_schedule_t s;
            uint8_t b[3] = {1, 2, 3};
            if (am[i] == EHOP_AM_NIBBLE) {
                b[0] = 0x01;
                b[1] = 0x02;
                b[2] = 0x03;
            }
            sched_on(&s, &c);
            ehop_apply(&s, b, 3);
            CHECK(memcmp(b, exp[i], 3) == 0, "tail word reordered");
            ehop_apply(&s, b, 3);
            CHECK(am[i] == EHOP_AM_NIBBLE ? (b[0] == 1 && b[1] == 2 && b[2] == 3)
                                          : (b[0] == 1 && b[1] == 2 && b[2] == 3),
                  "tail word involution");
        }
    }
}

/* --------------------------------------------------- schedule involution */
static void test_involution(void)
{
    static uint8_t a[EHOP_MAX_PAYLOAD], b[EHOP_MAX_PAYLOAD];
    static const ehop_cfg_t cfgs[] = {
        {2, EHOP_BAND_CONTROL, EHOP_AM_FULL, EHOP_PM_STATIC, 1, 0, 0, 0},
        {4, EHOP_BAND_CONTROL, EHOP_AM_FULL | EHOP_AM_HALF, EHOP_PM_PER_FRAME, 5, 3, 1, 2},
        {8, EHOP_BAND_SETTLE, EHOP_AM_FULL | EHOP_AM_HALF | EHOP_AM_PAIR, EHOP_PM_PER_EPOCH, 9, 8,
         4, 7},
        {4, EHOP_BAND_MEDIA, EHOP_AM_NIBBLE | EHOP_AM_PAIR, EHOP_PM_PER_FRAME, 40, 30, 39, 11},
        {2, EHOP_BAND_CHAT, EHOP_AM_ALL, EHOP_PM_STATIC, 128, 100, 0, 0},
        {8, EHOP_BAND_BULK, EHOP_AM_ALL, EHOP_PM_PER_FRAME, 4096, 4095, 4095, 4095},
        {4, EHOP_BAND_BULK, EHOP_AM_NIBBLE, EHOP_PM_STATIC, 512, 0, 0, 0},
    };
    static const uint32_t lens[] = {0, 1, 2, 3, 7, 8, 9, 63, 64, 65, 511, 1000, 2047, 2048};
    uint32_t i, j, k, ok = 1, changed = 0;
    for (i = 0; i < sizeof cfgs / sizeof cfgs[0]; i++) {
        CHECK(ehop_cfg_check(&cfgs[i]) == EHOP_OK, "involution cfg valid");
        for (j = 0; j < sizeof lens / sizeof lens[0]; j++) {
            for (k = 0; k < 6; k++) {
                ehop_schedule_t s;
                uint32_t n = lens[j];
                fill(a, n, i * 1000 + j * 10 + k);
                memcpy(b, a, n);
                ehop_schedule_init(&s, K1, &cfgs[i], i, k, 3, (uint64_t) k * 0x100000001ull);
                ehop_apply(&s, b, n);
                if (n >= 64 && memcmp(a, b, n) != 0) changed++;
                ehop_apply(&s, b, n);
                if (memcmp(a, b, n) != 0) ok = 0;
            }
        }
    }
    CHECK(ok, "apply twice = identity: 7 configs x 14 lengths x 6 frames");
    CHECK(changed > 0, "apply once changes data");
    printf("  involution: %u schedule/length/frame cases, %u visibly reordered\n",
           (unsigned) (7 * 14 * 6), (unsigned) changed);
}

/* ------------------------------------------------- determinism + diversity */
static void sched_out(const uint8_t *key, const ehop_cfg_t *c, uint32_t chan, uint32_t epoch,
                      uint32_t sender, uint64_t seq, uint8_t *out, uint32_t n)
{
    ehop_schedule_t s;
    uint32_t i;
    for (i = 0; i < n; i++) out[i] = (uint8_t) i ^ (uint8_t) (i >> 8) ^ (uint8_t) (i * 37u);
    ehop_schedule_init(&s, key, c, chan, epoch, sender, seq);
    ehop_apply(&s, out, n);
}

static void test_determinism(void)
{
    static uint8_t x[2048], y[2048];
    ehop_cfg_t c =
        cfg_make(4, EHOP_BAND_SETTLE, EHOP_AM_FULL | EHOP_AM_PAIR, EHOP_PM_PER_FRAME, 16, 8, 3, 5);
    ehop_cfg_t d;
    ehop_channel_t na, nb;
    uint8_t hdr[EHOP_HDR_BYTES] = {0x5A, 0x48, 1, EHOP_BAND_SETTLE}, ta[8], tb[8];

    sched_out(K1, &c, 7, 2, 1, 42, x, sizeof x);
    sched_out(K1, &c, 7, 2, 1, 42, y, sizeof y);
    CHECK(memcmp(x, y, sizeof x) == 0, "same inputs, same schedule (two nodes)");

    CHECK(ehop_channel_init(&na, K1, 7, 2, &c, 1) == EHOP_OK, "node A channel");
    CHECK(ehop_channel_init(&nb, K1, 7, 2, &c, 2) == EHOP_OK, "node B channel");
    CHECK(memcmp(na.aead_key, nb.aead_key, 32) == 0 && memcmp(na.sched_key, nb.sched_key, 32) == 0,
          "both nodes derive identical channel keys");
    ehop_channel_tag(&na, hdr, ta);
    ehop_channel_tag(&nb, hdr, tb);
    CHECK(memcmp(ta, tb, 8) == 0, "both nodes compute the same tag");

#define DIFFERS(expr_desc, KEY, CFG, CH, EP, SND, SEQ)                                             \
    do {                                                                                           \
        sched_out(KEY, CFG, CH, EP, SND, SEQ, y, sizeof y);                                        \
        CHECK(memcmp(x, y, sizeof x) != 0, expr_desc);                                             \
    } while (0)
    DIFFERS("different key -> different schedule", K2, &c, 7, 2, 1, 42);
    DIFFERS("different channel id -> different schedule", K1, &c, 8, 2, 1, 42);
    DIFFERS("different epoch -> different schedule", K1, &c, 7, 3, 1, 42);
    DIFFERS("different sender -> different schedule", K1, &c, 7, 2, 9, 42);
    DIFFERS("different seq -> different schedule", K1, &c, 7, 2, 1, 43);
    d = c;
    d.fm_rate = 17;
    DIFFERS("different FM rate -> different schedule", K1, &d, 7, 2, 1, 42);
    d = c;
    d.fm_hop = 7;
    DIFFERS("different FM hop -> different schedule", K1, &d, 7, 2, 1, 42);
    d = c;
    d.pm_phase = 4;
    DIFFERS("different PM phase -> different schedule", K1, &d, 7, 2, 1, 42);
    d = c;
    d.am_mask = EHOP_AM_FULL;
    DIFFERS("different AM depth -> different schedule", K1, &d, 7, 2, 1, 42);
    d = c;
    d.width = 8;
    DIFFERS("different width -> different schedule", K1, &d, 7, 2, 1, 42);
#undef DIFFERS
    ehop_channel_init(&nb, K1, 7, 2, &c, 1);
    d = c;
    d.fm_rate = 17;
    ehop_channel_init(&nb, K1, 7, 2, &d, 1);
    ehop_channel_tag(&nb, hdr, tb);
    CHECK(memcmp(ta, tb, 8) != 0, "different FM rate -> different tag key");
}

/* ------------------------------------------------ measured flip frequency */
static void test_frequency(void)
{
    /* Width 2, FULL depth, distinct non-palindromic words: a word changed
     * exactly when its segment is "on", so segment boundaries are visible. */
    static uint8_t buf[EHOP_MAX_PAYLOAD], ref[EHOP_MAX_PAYLOAD];
    ehop_cfg_t c = cfg_make(2, EHOP_BAND_SETTLE, EHOP_AM_FULL, EHOP_PM_STATIC, 16, 6, 0, 0);
    uint32_t frames, w, segs = 0, words = 0, inrange = 1, hist_min = 9999, hist_max = 0;
    for (frames = 0; frames < 64; frames++) {
        ehop_schedule_t s;
        uint32_t prev = 2, run = 0, first = 1;
        for (w = 0; w < EHOP_MAX_PAYLOAD / 2; w++) {
            ref[2 * w] = (uint8_t) (w & 0x7F);
            ref[2 * w + 1] = (uint8_t) (0x80 | (w & 0x7F));
        }
        memcpy(buf, ref, sizeof buf);
        ehop_schedule_init(&s, K1, &c, 1, 0, 0, frames);
        ehop_apply(&s, buf, sizeof buf);
        for (w = 0; w < EHOP_MAX_PAYLOAD / 2; w++) {
            uint32_t on = buf[2 * w] != ref[2 * w];
            if (on != prev && prev != 2) {
                if (!first) { /* first segment is PM-shortened */
                    segs++;
                    words += run;
                    if (run < 10u || run > 22u) inrange = 0;
                    if (run < hist_min) hist_min = run;
                    if (run > hist_max) hist_max = run;
                }
                first = 0;
                run = 0;
            }
            prev = on;
            run++;
        }
    }
    CHECK(inrange, "every hopped segment inside [rate-hop, rate+hop]");
    CHECK(segs > 0 && words * 100u / segs >= 1500u && words * 100u / segs <= 1700u,
          "mean words per flip within 1 of the FM rate (16)");
    CHECK(hist_min < hist_max, "segment lengths actually hop");
    printf("  FM rate 16, hop 6: %u segments, mean %u.%02u words/flip, range %u..%u\n",
           (unsigned) segs, (unsigned) (words / segs), (unsigned) ((words * 100u / segs) % 100u),
           (unsigned) hist_min, (unsigned) hist_max);
}

/* ---------------------------------------------------- seal / open / tamper */
static void test_seal_open(void)
{
    static uint8_t pt[1024], fr[EHOP_MAX_FRAME], fr2[EHOP_MAX_FRAME], out[EHOP_MAX_FRAME];
    ehop_cfg_t c =
        cfg_make(4, EHOP_BAND_SETTLE, EHOP_AM_FULL | EHOP_AM_HALF, EHOP_PM_PER_FRAME, 12, 5, 2, 3);
    ehop_cfg_t c2 = c;
    ehop_channel_t tx, rx, other, rx2;
    uint32_t fl = 0, ol = 0, i, ok;
    int rc;
    fill(pt, sizeof pt, 77);
    ehop_channel_init(&tx, K1, 3, 5, &c, 10);
    ehop_channel_init(&rx, K1, 3, 5, &c, 20);
    CHECK(ehop_seal(&tx, pt, 1000, fr, sizeof fr, &fl) == EHOP_OK, "seal ok");
    CHECK(fl == 1000 + EHOP_OVERHEAD, "frame length = payload + 48");
    CHECK(tx.next_seq == 1, "seal advanced the sequence number");
    CHECK(memcmp(fr + 32, pt, 1000) != 0, "payload not in clear");
    CHECK(ehop_open(&rx, fr, fl, out, sizeof out, &ol) == EHOP_OK && ol == 1000 &&
              memcmp(out, pt, 1000) == 0,
          "open recovers plaintext");
    CHECK(ehop_open(&rx, fr, fl, out, sizeof out, &ol) == EHOP_EREPLAY, "replayed frame rejected");
    /* Out-of-order inside the window is accepted once. */
    {
        uint8_t f1[200], f2[200];
        uint32_t l1, l2;
        ehop_seal(&tx, pt, 100, f1, sizeof f1, &l1);
        ehop_seal(&tx, pt, 100, f2, sizeof f2, &l2);
        CHECK(ehop_open(&rx, f2, l2, out, sizeof out, &ol) == EHOP_OK, "seq 2 first");
        CHECK(ehop_open(&rx, f1, l1, out, sizeof out, &ol) == EHOP_OK, "late seq 1 still accepted");
        CHECK(ehop_open(&rx, f1, l1, out, sizeof out, &ol) == EHOP_EREPLAY,
              "late seq 1 not accepted twice");
    }
    CHECK(ehop_channel_set_seq(&tx, 1) == EHOP_EARG, "counter cannot move backwards");
    CHECK(ehop_channel_set_seq(&tx, 3) == EHOP_OK && tx.next_seq == 3,
          "counter can be restored forwards");
    {
        /* A seventeenth sender on one channel finds the table full. */
        ehop_channel_t snd, rcv;
        uint8_t f[100];
        uint32_t l, k, okn = 1;
        ehop_channel_init(&rcv, K1, 3, 5, &c, 0);
        for (k = 1; k <= EHOP_REPLAY_SLOTS + 1; k++) {
            ehop_channel_init(&snd, K1, 3, 5, &c, k);
            ehop_seal(&snd, pt, 10, f, sizeof f, &l);
            if (ehop_open(&rcv, f, l, out, sizeof out, &ol) !=
                (k <= EHOP_REPLAY_SLOTS ? EHOP_OK : EHOP_EFULL))
                okn = 0;
        }
        CHECK(okn, "16 senders tracked, the 17th refused rather than unchecked");
    }
    CHECK(ehop_seal(&tx, pt, 0, fr2, sizeof fr2, &fl) == EHOP_OK &&
              ehop_open(&rx, fr2, fl, out, sizeof out, &ol) == EHOP_OK && ol == 0,
          "empty payload round trip");

    /* Tamper: every byte position class. */
    ehop_seal(&tx, pt, 1000, fr, sizeof fr, &fl);
    ok = 1;
    for (i = 0; i < fl; i += (i < 40 ? 1 : 97)) {
        memcpy(fr2, fr, fl);
        fr2[i] ^= 0x01;
        rc = ehop_open(&rx, fr2, fl, out, sizeof out, &ol);
        if (rc == EHOP_OK) ok = 0;
        if (rc == EHOP_EAUTH)
            for (uint32_t j = 0; j < 1000; j++)
                if (out[j]) ok = 0;
    }
    memcpy(fr2, fr, fl);
    fr2[fl - 1] ^= 0x80;
    CHECK(ehop_open(&rx, fr2, fl, out, sizeof out, &ol) == EHOP_EAUTH, "flipped MAC bit -> EAUTH");
    memcpy(fr2, fr, fl);
    fr2[40] ^= 0x04;
    CHECK(ehop_open(&rx, fr2, fl, out, sizeof out, &ol) == EHOP_EAUTH,
          "flipped ciphertext bit -> EAUTH");
    memcpy(fr2, fr, fl);
    fr2[26] ^= 0x01;
    CHECK(ehop_open(&rx, fr2, fl, out, sizeof out, &ol) == EHOP_ECHANNEL,
          "flipped tag bit -> not this channel");
    memcpy(fr2, fr, fl);
    fr2[14] ^= 0x01;
    CHECK(ehop_open(&rx, fr2, fl, out, sizeof out, &ol) == EHOP_ECHANNEL,
          "flipped seq bit -> tag no longer matches");
    CHECK(ok, "no tampered frame opens and failed opens leave no plaintext");
    CHECK(ehop_open(&rx, fr, fl - 1, out, sizeof out, &ol) == EHOP_EFORMAT,
          "truncated frame -> EFORMAT");
    CHECK(ehop_open(&rx, fr, fl, out, 10, &ol) == EHOP_ESIZE, "small out -> ESIZE");
    CHECK(ehop_open(&rx, fr, fl, out, sizeof out, &ol) == EHOP_OK,
          "untampered frame still opens after the tamper attempts");

    /* Wrong channel: different FM rate on the same key and id. */
    c2.fm_rate = 13;
    ehop_channel_init(&other, K1, 3, 5, &c2, 30);
    ehop_seal(&tx, pt, 500, fr, sizeof fr, &fl);
    CHECK(ehop_open(&other, fr, fl, out, sizeof out, &ol) == EHOP_ECHANNEL,
          "wrong-FM channel: tag does not match");
    /* Forge the other channel's tag onto the frame: now only the AEAD stands
     * between the wrong schedule and the data, and it must say no. */
    ehop_channel_tag(&other, fr, fr + EHOP_HDR_BYTES);
    CHECK(ehop_open(&other, fr, fl, out, sizeof out, &ol) == EHOP_EAUTH,
          "wrong schedule with a forged-valid tag fails the MAC");
    c2 = c;
    c2.am_mask = EHOP_AM_FULL;
    ehop_channel_init(&other, K1, 3, 5, &c2, 30);
    ehop_seal(&tx, pt, 500, fr, sizeof fr, &fl);
    ehop_channel_tag(&other, fr, fr + EHOP_HDR_BYTES);
    CHECK(ehop_open(&other, fr, fl, out, sizeof out, &ol) == EHOP_EAUTH,
          "wrong AM depth with a forged-valid tag fails the MAC");
    c2 = c;
    c2.pm_phase = 3;
    ehop_channel_init(&other, K1, 3, 5, &c2, 30);
    ehop_seal(&tx, pt, 500, fr, sizeof fr, &fl);
    ehop_channel_tag(&other, fr, fr + EHOP_HDR_BYTES);
    CHECK(ehop_open(&other, fr, fl, out, sizeof out, &ol) == EHOP_EAUTH,
          "wrong PM phase with a forged-valid tag fails the MAC");
    ehop_channel_init(&other, K2, 3, 5, &c, 30);
    ehop_seal(&tx, pt, 500, fr, sizeof fr, &fl);
    ehop_channel_tag(&other, fr, fr + EHOP_HDR_BYTES);
    CHECK(ehop_open(&other, fr, fl, out, sizeof out, &ol) == EHOP_EAUTH,
          "wrong key with a forged-valid tag fails the MAC");
    ehop_channel_init(&rx2, K1, 3, 6, &c, 20);
    ehop_seal(&tx, pt, 500, fr, sizeof fr, &fl);
    CHECK(ehop_open(&rx2, fr, fl, out, sizeof out, &ol) == EHOP_EEPOCH, "other epoch -> EEPOCH");
    CHECK(ehop_seal(&tx, pt, 1025, fr, sizeof fr, &fl) == EHOP_ESIZE,
          "frame over the settle band limit refused");
    CHECK(ehop_seal(&tx, pt, 100, fr, 100, &fl) == EHOP_ESIZE, "small cap refused");
}

/* --------------------------------------------- routing, QoS, router, hook */
static void test_routing(void)
{
    static uint8_t pt[1500], fr[EHOP_MAX_FRAME], out[EHOP_MAX_FRAME];
    static ehop_router_t ra, rb;
    static const ehop_cfg_t plan[5] = {
        {2, EHOP_BAND_CONTROL, EHOP_AM_FULL, EHOP_PM_PER_FRAME, 3, 1, 0, 1},
        {4, EHOP_BAND_SETTLE, EHOP_AM_FULL | EHOP_AM_PAIR, EHOP_PM_PER_FRAME, 16, 4, 2, 3},
        {4, EHOP_BAND_MEDIA, EHOP_AM_NIBBLE | EHOP_AM_PAIR, EHOP_PM_PER_FRAME, 64, 16, 0, 5},
        {8, EHOP_BAND_CHAT, EHOP_AM_ALL, EHOP_PM_PER_EPOCH, 200, 50, 7, 1},
        {8, EHOP_BAND_BULK, EHOP_AM_ALL, EHOP_PM_STATIC, 1024, 512, 0, 0},
    };
    ehop_channel_t chans[5], ch;
    ehop_hook_t hook;
    uint8_t ida[32] = {'A'}, idb[32] = {'B'}, idc[32] = {'C'};
    uint32_t i, fl, ol, band, prio, chan;
    int pa, pb, ok = 1;
    fill(pt, sizeof pt, 5);

    for (i = 0; i < 5; i++) ehop_channel_init(&chans[i], K1, 100 + i, 0, &plan[i], 1);
    for (i = 0; i < 5; i++) {
        ehop_channel_t s;
        ehop_channel_copy(&s, &chans[i]);
        ehop_seal(&s, pt, 200, fr, sizeof fr, &fl);
        if (ehop_route(chans, 5, fr, fl) != (int) i) ok = 0;
        ehop_frame_qos(fr, fl, &band, &prio);
        if (band != plan[i].band || prio != plan[i].band) ok = 0;
    }
    CHECK(ok, "tag routing picks the right one of 5 channels on a link");
    {
        ehop_channel_t s;
        ehop_channel_init(&s, K2, 100, 0, &plan[0], 1);
        ehop_seal(&s, pt, 200, fr, sizeof fr, &fl);
        CHECK(ehop_route(chans, 5, fr, fl) == -1, "foreign frame routes nowhere");
        CHECK(ehop_frame_qos(fr, fl, &band, &prio) == EHOP_OK && prio == 0,
              "QoS readable without keys");
    }

    /* Two overlay nodes, A and B, each with the other in its table. */
    ehop_router_init(&ra);
    ehop_router_init(&rb);
    pa = ehop_router_add_peer(&ra, idb, 1);
    pb = ehop_router_add_peer(&rb, ida, 100); /* A is a high-capacity peer */
    CHECK(pa == 0 && pb == 0, "peers added");
    CHECK(ehop_router_add_peer(&ra, idb, 5) == 0 && ra.peers[0].capacity == 5,
          "re-adding a peer updates it");
    CHECK(ehop_router_find_peer(&ra, idc) == EHOP_EMEMBER, "unknown peer not found");
    for (i = 0; i < 4; i++) { /* no bulk channel on this link */
        ehop_channel_init(&ch, K1, 200 + i, 0, &plan[i], 0xA);
        CHECK(ehop_router_add_channel(&ra, 0, &ch) == (int) i, "A adds channel");
        ehop_channel_init(&ch, K1, 200 + i, 0, &plan[i], 0xB);
        ehop_router_add_channel(&rb, 0, &ch);
    }
    CHECK(ehop_router_pick(&ra, 0, EHOP_MSG_SETTLEMENT, 900, &band) == 1 &&
              band == EHOP_BAND_SETTLE,
          "settlement picks the settle channel");
    CHECK(ehop_router_pick(&ra, 0, EHOP_MSG_ROUTING, 100, &band) == 0 && band == EHOP_BAND_CONTROL,
          "routing picks the control channel");
    CHECK(ehop_router_pick(&ra, 0, EHOP_MSG_CONTROL, 900, &band) == 1 && band == EHOP_BAND_SETTLE,
          "oversize control falls back to the next lower-priority band");
    CHECK(ehop_router_pick(&ra, 0, EHOP_MSG_VOICE, 1500, &band) == 3 && band == EHOP_BAND_CHAT,
          "oversize voice falls back to chat");
    CHECK(ehop_router_pick(&ra, 0, EHOP_MSG_FILE, 100, &band) == EHOP_EBAND,
          "bulk is never promoted into a higher band");

    ehop_router_hook(&ra, &hook);
    CHECK(hook.seal(hook.ctx, 0, EHOP_MSG_PAYMENT, pt, 700, fr, sizeof fr, &fl) == EHOP_OK,
          "hook seal");
    CHECK(hook.qos(fr, fl, &band, &prio) == EHOP_OK && band == EHOP_BAND_SETTLE && prio == 1,
          "hook QoS: settle band, priority 1");
    CHECK(ehop_router_recv(&rb, 0, fr, fl, out, sizeof out, &ol, &chan) == EHOP_OK && chan == 1 &&
              ol == 700 && memcmp(out, pt, 700) == 0,
          "B receives A's payment on the settle channel");
    CHECK(ehop_router_recv(&rb, 0, fr, fl, out, sizeof out, &ol, &chan) == EHOP_EREPLAY,
          "router replay rejected");
    ehop_router_hook(&rb, &hook);
    CHECK(hook.seal(hook.ctx, 0, EHOP_MSG_ALERT, pt, 1500, fr, sizeof fr, &fl) == EHOP_OK &&
              ehop_router_recv(&ra, 0, fr, fl, out, sizeof out, &ol, &chan) == EHOP_OK &&
              chan == 3 && memcmp(out, pt, 1500) == 0,
          "B -> A alert on chat through the hook");
    CHECK(hook.open(hook.ctx, 1, fr, fl, out, sizeof out, &ol, &chan) == EHOP_EARG,
          "unknown peer slot rejected");
}

/* ------------------------------------------------------- private networks */
static void test_private_net(void)
{
    static ehop_net_t admin, m1, m2, m3, eve;
    static uint8_t ek1[MLKEM768_EK_BYTES], dk1[MLKEM768_DK_BYTES];
    static uint8_t ek2[MLKEM768_EK_BYTES], dk2[MLKEM768_DK_BYTES];
    static uint8_t ek3[MLKEM768_EK_BYTES], dk3[MLKEM768_DK_BYTES];
    static uint8_t w1[EHOP_WELCOME_BYTES], w2[EHOP_WELCOME_BYTES], w3[EHOP_WELCOME_BYTES];
    static uint8_t wx[EHOP_WELCOME_BYTES];
    static uint8_t rk[EHOP_NET_MAX_MEMBERS][EHOP_WELCOME_BYTES];
    static uint8_t fr[EHOP_MAX_FRAME], out[EHOP_MAX_FRAME];
    static ehop_net_t fake;
    uint8_t net_id[16] = "ZXV-private-net", seed[32], ent[32], m[32];
    uint8_t old_key[32];
    ehop_cfg_t c =
        cfg_make(4, EHOP_BAND_SETTLE, EHOP_AM_FULL | EHOP_AM_HALF, EHOP_PM_PER_EPOCH, 20, 6, 1, 3);
    ehop_channel_t ca, c1, c2, c3, ce;
    uint32_t fl, ol, cnt, i;
    const char *msg = "settlement batch 0x2a: 1,000,000 units, final";
    uint32_t ml = (uint32_t) strlen(msg);

    fill(seed, 32, 1);
    fill(ent, 32, 2);
    for (i = 0; i < 32; i++) m[i] = (uint8_t) (i * 3);
    mlkem768_keygen(seed, ent, ek1, dk1);
    fill(seed, 32, 3);
    mlkem768_keygen(seed, ent, ek2, dk2);
    fill(seed, 32, 4);
    mlkem768_keygen(seed, ent, ek3, dk3);

    fill(seed, 32, 10);
    fill(ent, 32, 11);
    CHECK(ehop_net_create(&admin, net_id, 1, seed, ent) == EHOP_OK, "network created");
    ehop_net_member_init(&m1, net_id, 11, admin.admin_pk);
    ehop_net_member_init(&m2, net_id, 12, admin.admin_pk);
    ehop_net_member_init(&m3, net_id, 13, admin.admin_pk);
    CHECK(ehop_net_invite(&admin, 11, ek1, m, NULL, w1) == EHOP_OK, "invite m1");
    m[0] ^= 1;
    CHECK(ehop_net_invite(&admin, 12, ek2, m, NULL, w2) == EHOP_OK, "invite m2");
    m[0] ^= 2;
    CHECK(ehop_net_invite(&admin, 13, ek3, m, NULL, w3) == EHOP_OK, "invite m3");

    /* Bad welcomes first. */
    memcpy(wx, w1, sizeof wx);
    wx[EHOP_WELCOME_BODY + 10] ^= 1;
    CHECK(ehop_net_join(&m1, dk1, wx) == EHOP_ESIG, "corrupted signature rejected");
    memcpy(wx, w1, sizeof wx);
    wx[1116] ^= 1;
    CHECK(ehop_net_join(&m1, dk1, wx) == EHOP_ESIG, "altered wrapped key rejected");
    CHECK(ehop_net_join(&m1, dk1, w2) == EHOP_EMEMBER, "someone else's welcome rejected");
    CHECK(ehop_net_join(&m1, dk2, w1) == EHOP_EAUTH,
          "wrong decapsulation key: commitment check refuses the share");
    CHECK(!m1.has_key, "nothing installed after failures");
    /* A welcome signed by an impostor admin. */
    fill(seed, 32, 66);
    ehop_net_create(&fake, net_id, 1, seed, ent);
    ehop_net_invite(&fake, 11, ek1, m, NULL, wx);
    CHECK(ehop_net_join(&m1, dk1, wx) == EHOP_ESIG, "impostor admin rejected");

    CHECK(ehop_net_join(&m1, dk1, w1) == EHOP_OK, "m1 joins");
    CHECK(ehop_net_join(&m2, dk2, w2) == EHOP_OK, "m2 joins");
    CHECK(ehop_net_join(&m3, dk3, w3) == EHOP_OK, "m3 joins");
    CHECK(memcmp(m1.key, admin.key, 32) == 0 && memcmp(m3.key, admin.key, 32) == 0,
          "members hold the network key");
    CHECK(ehop_net_join(&m1, dk1, w1) == EHOP_EEPOCH, "welcome replay rejected");

    /* Channels: everyone derives the same channel; outsiders cannot. */
    ehop_net_channel(&admin, 1, &c, 1, &ca);
    ehop_net_channel(&m1, 1, &c, 11, &c1);
    ehop_net_channel(&m3, 1, &c, 13, &c3);
    memset(&eve, 0, sizeof eve);
    ehop_net_member_init(&eve, net_id, 99, admin.admin_pk);
    eve.has_key = 1; /* an outsider guessing at a key */
    memset(eve.key, 0x42, 32);
    ehop_net_channel(&eve, 1, &c, 99, &ce);
    CHECK(ehop_seal(&ca, (const uint8_t *) msg, ml, fr, sizeof fr, &fl) == EHOP_OK,
          "admin seals on network channel");
    CHECK(ehop_open(&c1, fr, fl, out, sizeof out, &ol) == EHOP_OK && ol == ml &&
              memcmp(out, msg, ml) == 0,
          "member 1 reads it");
    CHECK(ehop_open(&c3, fr, fl, out, sizeof out, &ol) == EHOP_OK, "member 3 reads it");
    CHECK(ehop_route(&ce, 1, fr, fl) == -1, "non-member cannot route the frame");
    ehop_channel_tag(&ce, fr, fr + EHOP_HDR_BYTES);
    CHECK(ehop_open(&ce, fr, fl, out, sizeof out, &ol) == EHOP_EAUTH,
          "non-member cannot read it even with a matching tag");

    /* Rotate: everyone ratchets; old-epoch frames are no longer accepted. */
    memcpy(old_key, admin.key, 32);
    ehop_seal(&ca, (const uint8_t *) msg, ml, fr, sizeof fr, &fl);
    ehop_net_rotate(&admin);
    ehop_net_rotate(&m1);
    ehop_net_rotate(&m2);
    ehop_net_rotate(&m3);
    CHECK(admin.epoch == 1 && memcmp(admin.key, old_key, 32) != 0, "key ratcheted");
    CHECK(memcmp(admin.key, m2.key, 32) == 0, "members ratchet to the same key");
    ehop_net_channel(&m1, 1, &c, 11, &c1);
    CHECK(ehop_open(&c1, fr, fl, out, sizeof out, &ol) == EHOP_EEPOCH,
          "previous-epoch frame refused after rotation");
    ehop_net_channel(&admin, 1, &c, 1, &ca);
    ehop_seal(&ca, (const uint8_t *) msg, ml, fr, sizeof fr, &fl);
    CHECK(ehop_open(&c1, fr, fl, out, sizeof out, &ol) == EHOP_OK, "epoch-1 frame reads");
    {
        /* Holding only the epoch-1 key, recomputing epoch 0 is not possible
         * through any API: the only derivation runs forward. Show that the
         * forward step from the old key reproduces the new key and that the
         * new key differs from every byte of the old one's role. */
        ehop_net_t t;
        memcpy(&t, &m2, sizeof t);
        memcpy(t.key, old_key, 32);
        t.epoch = 0;
        ehop_net_rotate(&t);
        CHECK(memcmp(t.key, m2.key, 32) == 0, "ratchet is deterministic forward");
    }

    /* Remove m3: re-key from fresh entropy, re-send to m1 and m2 only. */
    fill(ent, 32, 12345);
    CHECK(ehop_net_remove(&admin, 13, ent, NULL, rk, &cnt) == EHOP_OK && cnt == 2,
          "remove m3 -> 2 re-key welcomes");
    {
        uint32_t got1 = 0, got2 = 0, got3 = 0;
        uint32_t cnt2 = 7;
        CHECK(ehop_net_remove(&admin, 13, ent, NULL, rk + 2, &cnt2) == EHOP_EMEMBER && cnt2 == 0 &&
                  admin.epoch == 2,
              "removing twice fails and changes nothing");
        for (i = 0; i < 2; i++) {
            if (ehop_net_join(&m1, dk1, rk[i]) == EHOP_OK) got1++;
            if (ehop_net_join(&m2, dk2, rk[i]) == EHOP_OK) got2++;
            if (ehop_net_join(&m3, dk3, rk[i]) == EHOP_OK) got3++;
        }
        CHECK(got1 == 1 && got2 == 1, "remaining members accept their re-key");
        CHECK(got3 == 0, "removed member accepts no re-key welcome");
        CHECK(m1.epoch == 2 && memcmp(m1.key, admin.key, 32) == 0, "m1 at epoch 2");
        /* m3 tries the best it can: ratchet its own old key forward. */
        ehop_net_rotate(&m3);
        CHECK(m3.epoch == 2 && memcmp(m3.key, admin.key, 32) != 0,
              "removed member's ratcheted key is not the new key");
        ehop_net_channel(&admin, 1, &c, 1, &ca);
        ehop_net_channel(&m1, 1, &c, 11, &c1);
        ehop_net_channel(&m2, 1, &c, 12, &c2);
        ehop_net_channel(&m3, 1, &c, 13, &c3);
        ehop_seal(&ca, (const uint8_t *) msg, ml, fr, sizeof fr, &fl);
        CHECK(ehop_open(&c1, fr, fl, out, sizeof out, &ol) == EHOP_OK &&
                  ehop_open(&c2, fr, fl, out, sizeof out, &ol) == EHOP_OK,
              "remaining members read epoch-2 frames");
        CHECK(ehop_route(&c3, 1, fr, fl) == -1, "removed member cannot route epoch-2 frame");
        ehop_channel_tag(&c3, fr, fr + EHOP_HDR_BYTES);
        CHECK(ehop_open(&c3, fr, fl, out, sizeof out, &ol) == EHOP_EAUTH,
              "removed member cannot read epoch-2 frame");
    }
    CHECK(ehop_net_join(&m1, dk1, w1) == EHOP_EEPOCH, "old invite cannot roll m1 back");
    /* m3 ratcheted locally to epoch 2, so a re-invite at epoch 2 is refused
     * (no rollback) until m3 resets its state and joins afresh. */
    CHECK(ehop_net_invite(&admin, 13, ek3, m, NULL, w3) == EHOP_OK &&
              ehop_net_join(&m3, dk3, w3) == EHOP_EEPOCH,
          "stale local state refuses a same-epoch welcome");
    ehop_net_member_init(&m3, net_id, 13, admin.admin_pk);
    CHECK(ehop_net_join(&m3, dk3, w3) == EHOP_OK && memcmp(m3.key, admin.key, 32) == 0,
          "re-invited member rejoins after resetting");
    ehop_net_wipe(&admin);
    CHECK(admin.has_key == 0 && admin.admin_sk[0] == 0, "wipe clears the network");
}

/* ------------------------------------------------------------- throughput */
static double now_s(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double) t.tv_sec + (double) t.tv_nsec * 1e-9;
}

static void bench(void)
{
    static uint8_t buf[65536];
    static uint8_t fr[EHOP_MAX_FRAME], out[EHOP_MAX_FRAME];
    static const struct {
        const char *name;
        ehop_cfg_t c;
    } cases[] = {
        {"w2 FULL rate1 (worst: flip every word)",
         {2, EHOP_BAND_CONTROL, EHOP_AM_FULL, 0, 1, 0, 0, 0}},
        {"w4 FULL|HALF rate5 hop3",
         {4, EHOP_BAND_CONTROL, EHOP_AM_FULL | EHOP_AM_HALF, 2, 5, 3, 0, 1}},
        {"w8 FULL|HALF|PAIR rate16 hop8",
         {8, EHOP_BAND_SETTLE, EHOP_AM_FULL | EHOP_AM_HALF | EHOP_AM_PAIR, 2, 16, 8, 0, 1}},
        {"w4 NIBBLE|PAIR rate64 hop16",
         {4, EHOP_BAND_MEDIA, EHOP_AM_NIBBLE | EHOP_AM_PAIR, 2, 64, 16, 0, 1}},
        {"w8 ALL rate1024 hop512", {8, EHOP_BAND_BULK, EHOP_AM_ALL, 0, 1024, 512, 0, 0}},
    };
    uint32_t i, iters;
    double t0, t1, mbs;
    fill(buf, sizeof buf, 9);
    printf("  throughput (transform alone, 64 KiB buffer, this machine):\n");
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        ehop_schedule_t s;
        ehop_schedule_init(&s, K1, &cases[i].c, 1, 0, 0, 0);
        iters = 0;
        t0 = now_s();
        do {
            ehop_apply(&s, buf, sizeof buf);
            iters++;
            t1 = now_s();
        } while (t1 - t0 < 0.25);
        mbs = (double) iters * sizeof buf / (t1 - t0) / 1e6;
        printf("    %-42s %8.0f MB/s\n", cases[i].name, mbs);
    }
    {
        ehop_schedule_t s;
        iters = 0;
        t0 = now_s();
        do {
            ehop_schedule_init(&s, K1, &cases[2].c, 1, 0, 0, iters);
            iters++;
            t1 = now_s();
        } while (t1 - t0 < 0.25);
        printf("    schedule derivation per frame (one SHAKE256): %.2f us\n",
               (t1 - t0) / iters * 1e6);
    }
    {
        ehop_channel_t tx, rx;
        uint32_t fl, ol;
        ehop_channel_init(&tx, K1, 1, 0, &cases[4].c, 1);
        ehop_channel_init(&rx, K1, 1, 0, &cases[4].c, 2);
        iters = 0;
        t0 = now_s();
        do {
            ehop_seal(&tx, buf, 2048, fr, sizeof fr, &fl);
            iters++;
            t1 = now_s();
        } while (t1 - t0 < 0.25);
        {
            uint8_t nonce[12] = {0}, tag[16];
            uint32_t n2 = 0;
            double a0 = now_s(), a1;
            do {
                aead_seal(tx.aead_key, nonce, fr, 32, buf, out, 2048, tag);
                n2++;
                a1 = now_s();
            } while (a1 - a0 < 0.25);
            printf("    reference: bare ChaCha20-Poly1305 aead_seal, 2048 B: %.0f MB/s\n",
                   (double) n2 * 2048 / (a1 - a0) / 1e6);
        }
        printf("    full ehop_seal, 2048-byte frames (schedule+AEAD): %.0f MB/s\n",
               (double) iters * 2048 / (t1 - t0) / 1e6);
        iters = 0;
        t0 = now_s();
        do {
            ehop_seal(&tx, buf, 2048, fr, sizeof fr, &fl);
            ehop_open(&rx, fr, fl, out, sizeof out, &ol);
            iters++;
            t1 = now_s();
        } while (t1 - t0 < 0.25);
        printf("    seal+open round trip, 2048-byte frames: %.0f MB/s\n",
               (double) iters * 2048 / (t1 - t0) / 1e6);
    }
    CHECK(1, "throughput measured");
}

int main(void)
{
    printf("=== ehop: keyed alternating-endianness channels (AM/FM/PM) ===\n");
    test_bands();
    test_am_depths();
    test_involution();
    test_determinism();
    test_frequency();
    test_seal_open();
    test_routing();
    test_private_net();
    bench();
    printf("ehop: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
