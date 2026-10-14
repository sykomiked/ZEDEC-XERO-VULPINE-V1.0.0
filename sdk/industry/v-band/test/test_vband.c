/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vband.c — v-band: the log (append-only, never overwrites what the
 * gateway has not acknowledged, chain head), redundant pairs (BOTH keeps
 * both values), the LoRaWAN payload table and batch sizes for every data
 * rate, batch round trips, every bit flip and truncation of a batch and of
 * its UBH-168 frames refused, non-canonical encodings refused, the gateway
 * (duplicates, out of order, unknown device, bad check, refusal callback,
 * resync), and a lossy end-to-end simulation. */
#include "zvband.h"
#include "zind_test.h"

#define VRC(expr, want)                                                                        \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zvb_status_name(rc_), (int) (want), zvb_status_name(want));                     \
    } while (0)

/* Recompute a batch's check value after editing it (the test's own copy of
 * the rule in zvband.h). */
static void reseal(uint8_t *buf, size_t len) {
    uint8_t tmp[12 + ZVB_BATCH_MAX], h[32];
    memcpy(tmp, "zvb/batch/v1", 12);
    memcpy(tmp + 12, buf, len - 4);
    zxv_sha3_256(tmp, 12 + len - 4, h);
    memcpy(buf + len - 4, h, 4);
}

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* The chain rule, independently. */
static void chain_ref(uint8_t chain[32], uint32_t device, const zvb_record *r) {
    uint8_t b[64];
    memcpy(b, "zvb/rec/v1", 10);
    memcpy(b + 10, chain, 32);
    put32(b + 42, device);
    put32(b + 46, r->seq);
    put32(b + 50, r->time_s);
    b[54] = r->sensor;
    b[55] = r->lp;
    put32(b + 56, (uint32_t) r->value);
    put32(b + 60, (uint32_t) r->value2);
    zxv_sha3_256(b, 64, chain);
}

typedef struct {
    int n;
    uint32_t device[64], batch[64];
    int32_t st[64];
} refusals;

static void on_refuse(void *user, uint32_t device, int32_t st, uint32_t batch) {
    refusals *r = (refusals *) user;
    if (r->n < 64) {
        r->device[r->n] = device;
        r->st[r->n] = st;
        r->batch[r->n] = batch;
    }
    r->n++;
}

static int rec_eq(const zvb_record *a, const zvb_record *b) {
    return a->seq == b->seq && a->time_s == b->time_s && a->sensor == b->sensor &&
           a->lp == b->lp && a->value == b->value && a->value2 == b->value2;
}

static void test_names(void) {
    const int32_t codes[] = {ZVB_E_FULL,   ZVB_E_DUPLICATE, ZVB_E_ORDER, ZVB_E_CHECK,
                             ZVB_E_DR,     ZVB_E_DEVICE,    ZVB_E_WIRE,  ZVB_E_EMPTY};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        const char *n = zvb_status_name(codes[i]);
        CHECK(n && strncmp(n, "ZVB_E_", 6) == 0, "name of %d: %s", (int) codes[i], n);
        CHECK(codes[i] <= -4000 && codes[i] > -4100, "code %d in the v-band block", (int) codes[i]);
        for (size_t j = 0; j < i; j++) CHECK(codes[i] != codes[j], "codes distinct");
    }
    CHECK(strcmp(zvb_status_name(ZXV_OK), zxv_status_name(ZXV_OK)) == 0, "falls back to libzxv");
}

static void test_log(void) {
    zvb_record st[8], r;
    zvb_log log;
    uint8_t chain[32] = {0};
    uint32_t released = 99;
    VRC(zvb_log_init(NULL, st, 8, 1), ZXV_E_ARG);
    VRC(zvb_log_init(&log, NULL, 8, 1), ZXV_E_ARG);
    VRC(zvb_log_init(&log, st, 0, 1), ZXV_E_ARG);
    VRC(zvb_log_init(&log, st, 4, 77), ZXV_OK);
    CHECK(log.next_seq == 1 && log.n == 0 && log.acked == 0 && log.sent == 0, "fresh log");

    /* states */
    VRC(zvb_log_append(&log, 1, 100, ZXV_LP_TRUE, 215), ZXV_OK);
    VRC(zvb_log_append(&log, 1, 100, ZXV_LP_FALSE, 7), ZXV_OK);
    VRC(zvb_log_append(&log, 2, 101, ZXV_LP_NEITHER, 0), ZXV_OK);
    VRC(zvb_log_append(&log, 2, 101, ZXV_LP_NEITHER, 5), ZXV_E_ARG);
    VRC(zvb_log_append(&log, 2, 101, ZXV_LP_BOTH, 5), ZXV_E_ARG);
    VRC(zvb_log_append(&log, 2, 101, 4, 5), ZXV_E_ARG);
    VRC(zvb_log_append(&log, 2, 101, -1, 5), ZXV_E_ARG);
    VRC(zvb_log_append(&log, 2, 99, ZXV_LP_TRUE, 5), ZXV_E_ARG); /* time went backwards */
    VRC(zvb_log_append(NULL, 2, 101, ZXV_LP_TRUE, 5), ZXV_E_ARG);
    CHECK(log.n == 3 && log.next_seq == 4, "refusals append nothing (n %u)", log.n);
    /* pair */
    VRC(zvb_log_append_pair(&log, 3, 102, 1000, 1002, 2), ZXV_OK);
    VRC(zvb_log_at(&log, 3, &r), ZXV_OK);
    CHECK(r.lp == ZXV_LP_TRUE && r.value == 1000 && r.value2 == 0 && r.seq == 4, "pair agrees");
    /* full: 4 kept, none acknowledged */
    VRC(zvb_log_append(&log, 1, 103, ZXV_LP_TRUE, 1), ZVB_E_FULL);
    VRC(zvb_log_append_pair(&log, 1, 103, 1, 900, 2), ZVB_E_FULL);
    CHECK(log.n == 4 && log.next_seq == 5, "full log unchanged");
    for (uint32_t i = 0; i < 4; i++) {
        VRC(zvb_log_at(&log, i, &r), ZXV_OK);
        CHECK(r.seq == i + 1, "record %u seq %u", i, r.seq);
        chain_ref(chain, 77, &r);
    }
    CHECK(memcmp(chain, log.chain, 32) == 0, "chain head matches the rule");
    VRC(zvb_log_at(&log, 4, &r), ZXV_E_NOT_FOUND);
    VRC(zvb_log_at(&log, 0, NULL), ZXV_E_ARG);

    /* ack rules: cannot acknowledge what was never sent */
    VRC(zvb_log_ack(&log, 2), ZXV_E_ARG);
    VRC(zvb_log_ack(&log, 1), ZXV_OK); /* nothing sent, nothing acknowledged */
    log.sent = log.sent_max = 3;       /* as if a batch carried seq 1..3 */
    VRC(zvb_log_ack(&log, 5), ZXV_E_ARG);
    VRC(zvb_log_ack(&log, 3), ZXV_OK);
    CHECK(log.acked == 2 && log.sent == 2, "seq 3 queued again (acked %u sent %u)", log.acked, log.sent);
    VRC(zvb_log_ack(&log, 2), ZXV_E_ARG); /* never goes back */
    VRC(zvb_log_trim(&log, &released), ZXV_OK);
    CHECK(released == 2 && log.n == 2, "two released (%u, n %u)", released, log.n);
    VRC(zvb_log_at(&log, 0, &r), ZXV_OK);
    CHECK(r.seq == 3 && r.lp == ZXV_LP_NEITHER, "oldest kept is seq 3");
    /* appends now wrap the ring */
    VRC(zvb_log_append_pair(&log, 9, 104, -50, 50, 99), ZXV_OK);
    VRC(zvb_log_at(&log, 2, &r), ZXV_OK);
    CHECK(r.seq == 5 && r.lp == ZXV_LP_BOTH && r.value == -50 && r.value2 == 50, "BOTH keeps both");
    chain_ref(chain, 77, &r);
    VRC(zvb_log_append(&log, 9, 104, ZXV_LP_TRUE, -1), ZXV_OK);
    VRC(zvb_log_at(&log, 3, &r), ZXV_OK);
    chain_ref(chain, 77, &r);
    CHECK(memcmp(chain, log.chain, 32) == 0, "chain covers released readings too");
    VRC(zvb_log_append(&log, 9, 105, ZXV_LP_TRUE, 3), ZVB_E_FULL);
    /* full with acknowledged readings: released automatically */
    log.sent = log.sent_max = 4;
    VRC(zvb_log_ack(&log, 5), ZXV_OK);
    VRC(zvb_log_append(&log, 9, 105, ZXV_LP_TRUE, 3), ZXV_OK);
    VRC(zvb_log_at(&log, 0, &r), ZXV_OK);
    CHECK(r.seq == 5 && log.n == 3, "auto release of acked (oldest %u)", r.seq);

    /* extreme pairs: difference beyond int32 */
    {
        zvb_record s2[4];
        zvb_log l2;
        VRC(zvb_log_init(&l2, s2, 4, 1), ZXV_OK);
        VRC(zvb_log_append_pair(&l2, 1, 0, INT32_MIN, INT32_MAX, UINT32_MAX - 1), ZXV_OK);
        VRC(zvb_log_at(&l2, 0, &r), ZXV_OK);
        CHECK(r.lp == ZXV_LP_BOTH && r.value == INT32_MIN && r.value2 == INT32_MAX, "extreme pair BOTH");
        VRC(zvb_log_append_pair(&l2, 1, 0, INT32_MIN, INT32_MAX, UINT32_MAX), ZXV_OK);
        VRC(zvb_log_at(&l2, 1, &r), ZXV_OK);
        CHECK(r.lp == ZXV_LP_TRUE && r.value == INT32_MIN, "tolerance covers everything");
        VRC(zvb_log_append_pair(&l2, 1, 0, 5, 5, 0), ZXV_OK);
        VRC(zvb_log_at(&l2, 2, &r), ZXV_OK);
        CHECK(r.lp == ZXV_LP_TRUE, "equal pair, zero tolerance");
        VRC(zvb_log_append_pair(&l2, 1, 0, 5, 6, 0), ZXV_OK);
        VRC(zvb_log_at(&l2, 3, &r), ZXV_OK);
        CHECK(r.lp == ZXV_LP_BOTH && r.value2 == 6, "off by one, zero tolerance");
        /* sequence exhaustion */
        l2.next_seq = UINT32_MAX;
        l2.acked = l2.sent = l2.sent_max = UINT32_MAX - 1;
        VRC(zvb_log_append(&l2, 1, 0, ZXV_LP_TRUE, 1), ZXV_E_RANGE);
    }
    /* chain: same readings, different device -> different head */
    {
        zvb_record a[2], b[2];
        zvb_log la, lb;
        zvb_log_init(&la, a, 2, 1);
        zvb_log_init(&lb, b, 2, 2);
        zvb_log_append(&la, 1, 5, ZXV_LP_TRUE, 1);
        zvb_log_append(&lb, 1, 5, ZXV_LP_TRUE, 1);
        CHECK(memcmp(la.chain, lb.chain, 32) != 0, "device bound into the chain");
    }
}

static void test_payload(void) {
    static const uint32_t eu[8] = {51, 51, 51, 115, 222, 222, 222, 222};
    static const uint32_t us[5] = {11, 53, 125, 242, 242};
    uint32_t m = 0;
    for (uint8_t dr = 0; dr < 8; dr++) {
        VRC(zvb_payload_max(ZVB_REGION_EU868, dr, &m), ZXV_OK);
        CHECK(m == eu[dr], "EU868 DR%u %u", dr, m);
    }
    for (uint8_t dr = 0; dr < 5; dr++) {
        VRC(zvb_payload_max(ZVB_REGION_US915, dr, &m), ZXV_OK);
        CHECK(m == us[dr], "US915 DR%u %u", dr, m);
    }
    VRC(zvb_payload_max(ZVB_REGION_EU868, 8, &m), ZVB_E_DR);
    VRC(zvb_payload_max(ZVB_REGION_US915, 5, &m), ZVB_E_DR);
    VRC(zvb_payload_max(ZVB_REGION_NBIOT, 200, &m), ZXV_OK);
    CHECK(m == 512, "NB-IoT %u", m);
    VRC(zvb_payload_max(0, 0, &m), ZVB_E_DR);
    VRC(zvb_payload_max(4, 0, &m), ZVB_E_DR);
    VRC(zvb_payload_max(ZVB_REGION_EU868, 0, NULL), ZXV_E_ARG);
}

/* a + d, or a when that would leave int32 */
static int32_t near(int32_t a, int32_t d) {
    int64_t x = (int64_t) a + d;
    return (x > INT32_MAX || x < INT32_MIN) ? a : (int32_t) x;
}

/* Fill a log with n mixed readings (deterministic). */
static void fill(zvb_log *log, uint32_t n, uint64_t *s) {
    uint32_t t = log->last_time;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t r = zt_rand(s);
        int k = (int) (r % 10);
        t += (uint32_t) ((r >> 8) % 4 == 0 ? (r >> 16) % 100000 : (r >> 16) % 300);
        if (k < 6) zvb_log_append(log, (uint8_t) (r >> 40), t, ZXV_LP_TRUE, (int32_t) (r >> 33) - (1 << 29));
        else if (k == 6) zvb_log_append(log, 3, t, ZXV_LP_FALSE, (int32_t) ((r >> 20) % 50));
        else if (k == 7) zvb_log_append(log, 4, t, ZXV_LP_NEITHER, 0);
        else zvb_log_append_pair(log, 5, t, (int32_t) (r >> 32), near((int32_t) (r >> 32), (int32_t) ((r >> 24) % 9) - 4), 2);
    }
}

static void test_batches(void) {
    static zvb_record st[600], got[300];
    zvb_log log;
    zvb_batch_info info;
    uint8_t buf[ZVB_BATCH_MAX];
    size_t len = 0;
    uint32_t count = 0, m = 0, total = 0;
    uint64_t s = 12345;
    VRC(zvb_log_init(&log, st, 600, 0xC0FFEE), ZXV_OK);
    VRC(zvb_batch_make(&log, 51, buf, sizeof buf, &len, &count), ZVB_E_EMPTY);
    fill(&log, 600, &s);
    CHECK(log.n == 600, "filled %u", log.n);
    VRC(zvb_batch_make(&log, ZVB_BATCH_MIN - 1, buf, sizeof buf, &len, &count), ZVB_E_DR);
    VRC(zvb_batch_make(&log, ZVB_BATCH_MAX + 1, buf, sizeof buf, &len, &count), ZXV_E_ARG);
    VRC(zvb_batch_make(NULL, 51, buf, sizeof buf, &len, &count), ZXV_E_ARG);
    CHECK(log.sent == 0 && log.next_batch == 1, "refused makes change nothing");

    /* every region and data rate: payloads within limit, round trip exact */
    for (int region = 1; region <= 3; region++) {
        for (uint8_t dr = 0; dr < 8; dr++) {
            uint32_t before = log.sent, bno = log.next_batch;
            if (zvb_payload_max(region, dr, &m) != ZXV_OK) continue;
            if (log.sent == 600) {
                log.sent = 0; /* rewind for the next size: as a resend */
                log.acked = 0;
                before = 0;
            }
            if (m < ZVB_BATCH_MIN) {
                VRC(zvb_batch_make(&log, m, buf, sizeof buf, &len, &count), ZVB_E_DR);
                CHECK(region == ZVB_REGION_US915 && dr == 0, "only US915 DR0 is too small (%d/%u)", region, dr);
                continue;
            }
            VRC(zvb_batch_make(&log, m, buf, sizeof buf, &len, &count), ZXV_OK);
            CHECK(len <= m && len >= ZVB_BATCH_MIN, "region %d DR%u: %zu <= %u", region, dr, len, m);
            CHECK(count >= 1 && log.sent == before + count && log.next_batch == bno + 1, "progress");
            VRC(zvb_batch_decode(buf, len, got, 300, &info), ZXV_OK);
            CHECK(info.device == 0xC0FFEE && info.batch == bno && info.first_seq == before + 1 &&
                      info.count == count, "header");
            CHECK(info.n_lp[0] + info.n_lp[1] + info.n_lp[2] + info.n_lp[3] == count, "lp counts");
            for (uint32_t i = 0; i < count; i++) {
                zvb_record r;
                zvb_log_at(&log, before + i, &r);
                CHECK(rec_eq(&r, &got[i]), "region %d DR%u record %u", region, dr, i);
            }
            {
                uint8_t base = 9;
                zxv_hex_unpack(info.tag, &base, NULL, NULL);
                if (info.n_lp[ZXV_LP_BOTH]) CHECK(base == ZXV_HEX_BASE_BOTH, "tag BOTH");
                else if (info.n_lp[ZXV_LP_FALSE]) CHECK(base == ZXV_HEX_BASE_FALSE, "tag FALSE");
                else if (info.n_lp[ZXV_LP_NEITHER]) CHECK(base == ZXV_HEX_BASE_NEITHER, "tag NEITHER");
                else CHECK(base == ZXV_HEX_BASE_TRUE, "tag TRUE");
            }
            total += count;
        }
    }
    CHECK(total > 40, "readings carried %u", total);

    /* drain everything at DR5 then EMPTY */
    log.sent = log.acked = 0;
    while (zvb_batch_make(&log, 222, buf, sizeof buf, &len, &count) == ZXV_OK) CHECK(len <= 222, "DR5 batch");
    CHECK(log.sent == 600, "all sent (%u)", log.sent);
    VRC(zvb_batch_make(&log, 222, buf, sizeof buf, &len, &count), ZVB_E_EMPTY);
    /* the most readings a 512-byte NB-IoT payload holds: 122 of 4 bytes */
    {
        zvb_log l2;
        static zvb_record s2[300];
        zvb_log_init(&l2, s2, 300, 5);
        for (uint32_t i = 0; i < 300; i++) zvb_log_append(&l2, 0, 1, ZXV_LP_NEITHER, 0);
        VRC(zvb_batch_make(&l2, 512, buf, sizeof buf, &len, &count), ZXV_OK);
        CHECK(count == 122 && len == ZVB_BATCH_HEADER + 122 * 4 + 4, "smallest readings (%u, %zu)", count, len);
        VRC(zvb_batch_decode(buf, len, got, 300, &info), ZXV_OK);
        CHECK(info.n_lp[ZXV_LP_NEITHER] == 122, "NEITHER counted");
        VRC(zvb_batch_decode(buf, len, got, 121, &info), ZXV_E_BUFFER);
        VRC(zvb_batch_decode(buf, len, NULL, 0, &info), ZXV_OK); /* count only */
        /* a caller buffer smaller than the payload limit bounds the batch */
        VRC(zvb_batch_make(&l2, 512, buf, 40, &len, &count), ZXV_OK);
        CHECK(len <= 40 && count == (40 - 22) / 4, "bounded by cap (%zu, %u)", len, count);
    }
    /* extreme values and time deltas round trip */
    {
        zvb_log l3;
        zvb_record s3[8];
        zvb_log_init(&l3, s3, 8, UINT32_MAX);
        zvb_log_append(&l3, 255, 0, ZXV_LP_TRUE, INT32_MIN);
        zvb_log_append(&l3, 255, UINT32_MAX, ZXV_LP_TRUE, INT32_MAX);
        zvb_log_append_pair(&l3, 0, UINT32_MAX, INT32_MAX, INT32_MIN, 0);
        zvb_log_append(&l3, 0, UINT32_MAX, ZXV_LP_FALSE, -1);
        VRC(zvb_batch_make(&l3, 115, buf, sizeof buf, &len, &count), ZXV_OK);
        CHECK(count == 4 && len == 18 + (2 + 1 + 5) + (2 + 5 + 5) + (2 + 1 + 5 + 5) + (2 + 1 + 1) + 4,
              "extreme sizes (%u, %zu)", count, len);
        VRC(zvb_batch_decode(buf, len, got, 8, &info), ZXV_OK);
        for (uint32_t i = 0; i < 4; i++) CHECK(rec_eq(&s3[i], &got[i]), "extreme record %u", i);
    }
}

static void test_tamper(void) {
    zvb_record st[64], got[64];
    zvb_log log;
    zvb_batch_info info;
    uint8_t buf[ZVB_BATCH_MAX], t[ZVB_BATCH_MAX];
    size_t len = 0, flips = 0, refused = 0;
    uint32_t count = 0;
    uint64_t s = 99;
    zvb_log_init(&log, st, 64, 4242);
    fill(&log, 40, &s);
    VRC(zvb_batch_make(&log, 115, buf, sizeof buf, &len, &count), ZXV_OK);
    /* every single-bit flip */
    for (size_t i = 0; i < len; i++)
        for (int b = 0; b < 8; b++) {
            int32_t rc;
            memcpy(t, buf, len);
            t[i] ^= (uint8_t) (1u << b);
            rc = zvb_batch_decode(t, len, got, 64, &info);
            flips++;
            if (rc == ZVB_E_CHECK || rc == ZXV_E_FORMAT) refused++;
            CHECK(rc == ZVB_E_CHECK || (i == 0 && rc == ZXV_E_FORMAT), "flip %zu.%d -> %d", i, b, (int) rc);
        }
    CHECK(refused == flips && flips == len * 8, "all %zu flips refused", flips);
    /* every truncation and an extension */
    for (size_t l = 0; l < len; l++) {
        int32_t rc = zvb_batch_decode(buf, l, got, 64, &info);
        CHECK(rc == ZVB_E_CHECK || rc == ZXV_E_FORMAT, "truncated to %zu -> %d", l, (int) rc);
    }
    memcpy(t, buf, len);
    t[len] = 0;
    VRC(zvb_batch_decode(t, len + 1, got, 64, &info), ZVB_E_CHECK);
    VRC(zvb_batch_decode(buf, len, NULL, 0, NULL), ZXV_E_ARG);
    VRC(zvb_batch_decode(buf, ZVB_BATCH_MAX + 1, got, 64, &info), ZXV_E_FORMAT);

    /* structurally bad batches with a correct check value */
    {
        /* hand-built: one record, sensor 1, TRUE, dt 0, value 5 */
        uint8_t b[64];
        size_t n = 0;
        memset(b, 0, sizeof b);
        b[0] = 1;
        put32(b + 1, 7);
        put32(b + 5, 1);
        put32(b + 9, 1);
        b[13] = 1;
        put32(b + 14, 1000);
        n = 18;
        b[n++] = 1;
        b[n++] = ZXV_LP_TRUE;
        b[n++] = 0;
        b[n++] = 10;
        n += 4;
        reseal(b, n);
        VRC(zvb_batch_decode(b, n, got, 4, &info), ZXV_OK);
        CHECK(got[0].value == 5 && got[0].time_s == 1000 && got[0].seq == 1, "hand-built decodes");
#define BAD(edit, want)                                                                        \
    do {                                                                                       \
        uint8_t c[64];                                                                         \
        size_t cn = n;                                                                         \
        memcpy(c, b, sizeof c);                                                                \
        edit;                                                                                  \
        reseal(c, cn);                                                                         \
        VRC(zvb_batch_decode(c, cn, got, 4, &info), want);                                     \
    } while (0)
        BAD(c[0] = 2, ZXV_E_FORMAT);                         /* version */
        BAD(c[13] = 0, ZXV_E_FORMAT);                        /* count 0 */
        BAD(c[13] = 2, ZXV_E_FORMAT);                        /* count beyond the records */
        BAD(put32(c + 9, 0), ZXV_E_FORMAT);                  /* seq 0 */
        BAD(put32(c + 5, 0), ZXV_E_FORMAT);                  /* batch 0 */
        BAD(put32(c + 9, UINT32_MAX), ZXV_E_FORMAT);         /* seq overflow */
        BAD(c[19] = 8 | ZXV_LP_TRUE, ZXV_E_FORMAT);          /* unknown flag */
        BAD(c[19] = 4 | ZXV_LP_TRUE, ZXV_E_FORMAT);          /* value2 flag without BOTH */
        BAD(c[19] = ZXV_LP_BOTH, ZXV_E_FORMAT);              /* BOTH without value2 */
        BAD(c[19] = ZXV_LP_NEITHER, ZXV_E_FORMAT);           /* NEITHER with a value */
        BAD(c[20] = 1, ZXV_E_FORMAT);                        /* first delta not 0 */
        BAD((c[21] = 0x8A, c[22] = 0x00, c[26] = 0, cn = 27), ZXV_E_FORMAT); /* overlong varint */
        BAD((c[21] = 0x8A, cn = 26), ZXV_E_FORMAT);          /* varint runs into the check */
        BAD((c[22] = 0, cn = 27), ZXV_E_FORMAT);             /* trailing byte */
        BAD((c[21] = 0xFF, c[22] = 0xFF, c[23] = 0xFF, c[24] = 0xFF, c[25] = 0x1F, cn = 30), ZXV_E_FORMAT); /* > 32 bits */
        BAD((c[21] = 0xFF, c[22] = 0xFF, c[23] = 0xFF, c[24] = 0xFF, c[25] = 0x0F, cn = 30), ZXV_OK); /* exactly 32 bits */
        BAD((c[21] = 0x80, c[22] = 0x80, c[23] = 0x80, c[24] = 0x80, c[25] = 0x80, c[26] = 0x01, cn = 31), ZXV_E_FORMAT); /* 6 bytes */
        /* time overflow: two records, base time near the top */
        BAD((put32(c + 14, UINT32_MAX), c[13] = 2, c[22] = 1, c[23] = 1, c[24] = 1, c[25] = 2, cn = 30),
            ZXV_E_FORMAT);
        BAD((put32(c + 14, UINT32_MAX - 1), c[13] = 2, c[22] = 1, c[23] = 1, c[24] = 1, c[25] = 2, cn = 30),
            ZXV_OK);
#undef BAD
    }
}

static void test_wire(void) {
    uint8_t bytes[ZVB_BATCH_MAX], back[ZVB_BATCH_MAX];
    uint8_t fr[ZVB_WIRE_MAX_FRAMES * ZXV_WIRE_FRAME_BYTES], t[sizeof fr];
    size_t nf = 0, len = 0, flips = 0;
    uint64_t s = 7;
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (uint8_t) zt_rand(&s);
    CHECK(zvb_wire_frames(1) == 2 && zvb_wire_frames(16) == 2 && zvb_wire_frames(17) == 3 &&
              zvb_wire_frames(512) == 33, "frame counts");
    CHECK(ZVB_WIRE_MAX_FRAMES == 33, "max frames");
    /* every length */
    for (size_t l = 1; l <= ZVB_BATCH_MAX; l += (l < 40 ? 1 : 37)) {
        VRC(zvb_wire_encode(l % 10, bytes, l, fr, sizeof fr, &nf), ZXV_OK);
        CHECK(nf == zvb_wire_frames(l), "nframes %zu", nf);
        VRC(zvb_wire_decode(fr, nf, back, sizeof back, &len), ZXV_OK);
        CHECK(len == l && memcmp(back, bytes, l) == 0, "wire round trip %zu", l);
    }
    VRC(zvb_wire_encode(0, bytes, ZVB_BATCH_MAX, fr, sizeof fr, &nf), ZXV_OK);
    VRC(zvb_wire_encode(10, bytes, 20, fr, sizeof fr, &nf), ZXV_E_ARG);
    VRC(zvb_wire_encode(0, bytes, 0, fr, sizeof fr, &nf), ZXV_E_ARG);
    VRC(zvb_wire_encode(0, bytes, ZVB_BATCH_MAX + 1, fr, sizeof fr, &nf), ZXV_E_ARG);
    VRC(zvb_wire_encode(0, bytes, 40, fr, 2 * ZXV_WIRE_FRAME_BYTES, &nf), ZXV_E_BUFFER);
    CHECK(nf == 4, "needed frames reported (%zu)", nf);

    /* every bit of every frame of a 60-byte payload */
    VRC(zvb_wire_encode(3, bytes, 60, fr, sizeof fr, &nf), ZXV_OK);
    CHECK(nf == 5, "60 bytes -> 5 frames");
    for (size_t i = 0; i < nf * ZXV_WIRE_FRAME_BYTES; i++)
        for (int b = 0; b < 8; b++) {
            int32_t rc;
            memcpy(t, fr, nf * ZXV_WIRE_FRAME_BYTES);
            t[i] ^= (uint8_t) (1u << b);
            rc = zvb_wire_decode(t, nf, back, sizeof back, &len);
            CHECK(rc != ZXV_OK, "wire flip %zu.%d accepted", i, b);
            flips++;
        }
    CHECK(flips == 5 * 21 * 8, "wire flips %zu", flips);
    /* frames reordered, missing, extra, wrong magic, small buffer */
    memcpy(t, fr, nf * ZXV_WIRE_FRAME_BYTES);
    memcpy(t + 21, fr + 42, 21);
    memcpy(t + 42, fr + 21, 21);
    VRC(zvb_wire_decode(t, nf, back, sizeof back, &len), ZVB_E_WIRE);
    VRC(zvb_wire_decode(fr, nf - 1, back, sizeof back, &len), ZVB_E_WIRE);
    VRC(zvb_wire_decode(fr, nf + 1, back, sizeof back, &len), ZVB_E_WIRE);
    VRC(zvb_wire_decode(fr, 1, back, sizeof back, &len), ZVB_E_WIRE);
    VRC(zvb_wire_decode(fr + 21, nf - 1, back, sizeof back, &len), ZVB_E_WIRE);
    VRC(zvb_wire_decode(fr, nf, back, 59, &len), ZXV_E_BUFFER);
    CHECK(len == 60, "needed length reported");
    VRC(zvb_wire_decode(NULL, nf, back, 60, &len), ZXV_E_ARG);
    {
        uint8_t idle[21];
        zxv_wire_idle(3, idle);
        memcpy(t, idle, 21);
        memcpy(t + 21, fr + 21, (nf - 1) * 21);
        VRC(zvb_wire_decode(t, nf, back, sizeof back, &len), ZVB_E_WIRE);
    }
    /* the same payload on another trunk is a different frame set */
    {
        uint8_t f2[sizeof fr];
        size_t n2 = 0;
        zvb_wire_encode(4, bytes, 60, f2, sizeof f2, &n2);
        CHECK(n2 == nf && memcmp(f2, fr, nf * 21) != 0, "trunk in the tags");
        memcpy(t, fr, 21);
        memcpy(t + 21, f2 + 21, (nf - 1) * 21);
        VRC(zvb_wire_decode(t, nf, back, sizeof back, &len), ZVB_E_WIRE); /* mixed trunks */
    }
}

static void test_gateway(void) {
    zvb_gw gw;
    zvb_gw_dev devs[3];
    refusals rf;
    zvb_record st[64], got[64];
    zvb_log log;
    zvb_batch_info info;
    uint8_t b1[ZVB_BATCH_MAX], b2[ZVB_BATCH_MAX], b3[ZVB_BATCH_MAX];
    size_t l1 = 0, l2 = 0, l3 = 0;
    uint32_t c1 = 0, c2 = 0, c3 = 0, next = 0;
    uint64_t s = 5;
    memset(&rf, 0, sizeof rf);
    VRC(zvb_gw_init(&gw, NULL, 3, on_refuse, &rf), ZXV_E_ARG);
    VRC(zvb_gw_init(&gw, devs, 0, on_refuse, &rf), ZXV_E_ARG);
    VRC(zvb_gw_init(&gw, devs, 3, on_refuse, &rf), ZXV_OK);
    VRC(zvb_gw_add(&gw, 11), ZXV_OK);
    VRC(zvb_gw_add(&gw, 11), ZXV_E_DUPLICATE);
    VRC(zvb_gw_add(&gw, 12), ZXV_OK);
    VRC(zvb_gw_add(&gw, 13), ZXV_OK);
    VRC(zvb_gw_add(&gw, 14), ZXV_E_CAPACITY);

    zvb_log_init(&log, st, 64, 11);
    fill(&log, 30, &s);
    VRC(zvb_batch_make(&log, 51, b1, sizeof b1, &l1, &c1), ZXV_OK);
    VRC(zvb_batch_make(&log, 51, b2, sizeof b2, &l2, &c2), ZXV_OK);
    VRC(zvb_batch_make(&log, 51, b3, sizeof b3, &l3, &c3), ZXV_OK);

    /* batch 2 before batch 1: skips readings */
    next = 0;
    VRC(zvb_gw_accept(&gw, b2, l2, got, 64, &info, &next), ZVB_E_ORDER);
    CHECK(next == 1 && rf.n == 1 && rf.st[0] == ZVB_E_ORDER && rf.device[0] == 11 && rf.batch[0] == 2,
          "order refusal reported (next %u)", next);
    VRC(zvb_gw_accept(&gw, b1, l1, got, 64, &info, &next), ZXV_OK);
    CHECK(next == 1 + c1 && info.count == c1, "batch 1 accepted (next %u)", next);
    for (uint32_t i = 0; i < c1; i++) {
        zvb_record r;
        zvb_log_at(&log, i, &r);
        CHECK(rec_eq(&r, &got[i]), "gateway record %u", i);
    }
    VRC(zvb_gw_accept(&gw, b1, l1, got, 64, &info, &next), ZVB_E_DUPLICATE);
    CHECK(rf.n == 2 && rf.st[1] == ZVB_E_DUPLICATE && next == 1 + c1, "duplicate reported");
    /* small output buffer: nothing changes */
    VRC(zvb_gw_accept(&gw, b2, l2, got, c2 - 1, &info, &next), ZXV_E_BUFFER);
    VRC(zvb_gw_accept(&gw, b2, l2, NULL, 0, &info, &next), ZXV_E_BUFFER);
    VRC(zvb_gw_accept(&gw, b2, l2, got, 64, &info, &next), ZXV_OK);
    CHECK(next == 1 + c1 + c2, "batch 2 accepted");
    /* tampered batch 3: bad check, reported with the device */
    b3[20] ^= 1;
    VRC(zvb_gw_accept(&gw, b3, l3, got, 64, &info, &next), ZVB_E_CHECK);
    CHECK(rf.n == 3 && rf.st[2] == ZVB_E_CHECK && rf.device[2] == 11 && next == 1 + c1 + c2,
          "check refusal reported");
    b3[20] ^= 1;
    /* the device resends from next: ack, rebuild -> batch 4 carrying c3+ readings */
    VRC(zvb_log_ack(&log, next), ZXV_OK);
    {
        uint8_t b4[ZVB_BATCH_MAX];
        size_t l4 = 0;
        uint32_t c4 = 0;
        VRC(zvb_batch_make(&log, 51, b4, sizeof b4, &l4, &c4), ZXV_OK);
        VRC(zvb_gw_accept(&gw, b4, l4, got, 64, &info, &next), ZXV_OK);
        CHECK(info.batch == 4 && info.first_seq == 1 + c1 + c2, "resend as batch 4");
        /* the old batch 3, arriving late: duplicate readings */
        VRC(zvb_gw_accept(&gw, b3, l3, got, 64, &info, &next), ZVB_E_DUPLICATE);
        /* a forged batch 2 again with new number but old readings */
        VRC(zvb_gw_accept(&gw, b4, l4, got, 64, &info, &next), ZVB_E_DUPLICATE);
        VRC(zvb_log_ack(&log, next), ZXV_OK);
    }
    /* a batch numbered below the last but carrying the next readings */
    {
        uint8_t b5[ZVB_BATCH_MAX];
        size_t l5 = 0;
        uint32_t c5 = 0;
        VRC(zvb_batch_make(&log, 51, b5, sizeof b5, &l5, &c5), ZXV_OK);
        put32(b5 + 5, 3);
        reseal(b5, l5);
        VRC(zvb_gw_accept(&gw, b5, l5, got, 64, &info, &next), ZVB_E_ORDER);
        put32(b5 + 5, 5);
        reseal(b5, l5);
        VRC(zvb_gw_accept(&gw, b5, l5, got, 64, &info, &next), ZXV_OK);
    }
    /* unknown device */
    {
        zvb_log l9;
        zvb_record s9[4];
        uint8_t b9[64];
        size_t l = 0;
        uint32_t c = 0;
        int before = rf.n;
        zvb_log_init(&l9, s9, 4, 99);
        zvb_log_append(&l9, 0, 0, ZXV_LP_TRUE, 1);
        zvb_batch_make(&l9, 51, b9, sizeof b9, &l, &c);
        next = 777;
        VRC(zvb_gw_accept(&gw, b9, l, got, 64, &info, &next), ZVB_E_DEVICE);
        CHECK(rf.n == before + 1 && rf.device[before] == 99 && rf.st[before] == ZVB_E_DEVICE && next == 777,
              "unknown device reported, next untouched");
        /* garbage too short to name anything */
        VRC(zvb_gw_accept(&gw, b9, 3, got, 64, &info, &next), ZXV_E_FORMAT);
        CHECK(rf.n == before + 2 && rf.device[before + 1] == 0, "short garbage reported as device 0");
    }
    /* per-device counters */
    CHECK(devs[0].accepted == 4 && devs[0].refused == 6, "dev 11: %u accepted, %u refused",
          devs[0].accepted, devs[0].refused);
    CHECK(devs[1].accepted == 0 && devs[1].next_seq == 1, "dev 12 untouched");
    VRC(zvb_gw_accept(NULL, b1, l1, got, 64, &info, &next), ZXV_E_ARG);
    VRC(zvb_gw_accept(&gw, NULL, l1, got, 64, &info, &next), ZXV_E_ARG);
    /* no callback: still refused */
    gw.on_refuse = NULL;
    VRC(zvb_gw_accept(&gw, b1, l1, got, 64, NULL, NULL), ZVB_E_DUPLICATE);
}

/* Two devices, a lossy link (uplinks and acks lost, batches corrupted,
 * duplicated and delayed), random data rates; at the end the gateway holds
 * exactly every reading each device logged, in order. */
static void test_simulation(void) {
    enum { DEV = 2, N = 400 };
    static zvb_record st[DEV][12], held[DEV][N + 8];
    static uint8_t late[DEV][ZVB_BATCH_MAX];
    size_t late_len[DEV] = {0, 0};
    zvb_log log[DEV];
    zvb_gw gw;
    zvb_gw_dev devs[DEV];
    refusals rf;
    uint32_t nheld[DEV] = {0, 0}, appended[DEV] = {0, 0}, full[DEV] = {0, 0};
    uint64_t s = 2026, s2[DEV] = {11, 22};
    uint32_t lost = 0, acks_lost = 0, corrupt = 0;
    static zvb_record truth[DEV][N + 8];
    memset(&rf, 0, sizeof rf);
    zvb_gw_init(&gw, devs, DEV, on_refuse, &rf);
    for (int d = 0; d < DEV; d++) {
        zvb_log_init(&log[d], st[d], 12, (uint32_t) (100 + d));
        zvb_gw_add(&gw, (uint32_t) (100 + d));
    }
    for (int round = 0; round < 4000 && (appended[0] < N || appended[1] < N || nheld[0] < N || nheld[1] < N);
         round++) {
        uint64_t r = zt_rand(&s);
        int d = (int) (r & 1);
        int act = (int) ((r >> 1) % 8);
        uint8_t buf[ZVB_BATCH_MAX], wire[ZVB_WIRE_MAX_FRAMES * 21], rx[ZVB_BATCH_MAX];
        size_t len = 0, nf = 0, rl = 0;
        uint32_t count = 0, max = 0, next = 0;
        int32_t rc;
        if (act < 4) {
            if (appended[d] >= N) continue;
            {
                uint32_t before = log[d].next_seq;
                fill(&log[d], 1, &s2[d]);
                if (log[d].next_seq == before) {
                    full[d]++;
                    continue;
                }
                zvb_log_at(&log[d], log[d].n - 1, &truth[d][appended[d]]);
                appended[d]++;
            }
            continue;
        }
        zvb_payload_max(ZVB_REGION_EU868, (uint8_t) ((r >> 9) % 8), &max);
        rc = zvb_batch_make(&log[d], max, buf, sizeof buf, &len, &count);
        if (rc == ZVB_E_EMPTY) continue;
        CHECK(rc == ZXV_OK && len <= max, "sim make %d", (int) rc);
        if (rc != ZXV_OK) continue;
        if (act == 4) { /* uplink lost: no reply; device times out and resends */
            lost++;
            memcpy(late[d], buf, len);
            late_len[d] = len;
            zvb_log_ack(&log[d], log[d].acked + 1);
            continue;
        }
        zvb_wire_encode((uint32_t) d, buf, len, wire, sizeof wire, &nf);
        if (act == 5) { /* corrupted in the mesh: wire refuses */
            wire[21 + ((r >> 12) % ((nf - 1) * 21))] ^= (uint8_t) (1u << ((r >> 20) % 8));
            corrupt++;
            CHECK(zvb_wire_decode(wire, nf, rx, sizeof rx, &rl) != ZXV_OK, "corruption caught");
            zvb_log_ack(&log[d], log[d].acked + 1);
            continue;
        }
        VRC(zvb_wire_decode(wire, nf, rx, sizeof rx, &rl), ZXV_OK);
        rc = zvb_gw_accept(&gw, rx, rl, &held[d][nheld[d]], N + 8 - nheld[d], NULL, &next);
        CHECK(rc == ZXV_OK || rc == ZVB_E_DUPLICATE || rc == ZVB_E_ORDER, "sim accept %d", (int) rc);
        if (rc == ZXV_OK) nheld[d] += count;
        if (act == 6) { /* the reply is lost */
            acks_lost++;
            zvb_log_ack(&log[d], log[d].acked + 1);
        } else {
            VRC(zvb_log_ack(&log[d], next), ZXV_OK);
        }
        /* now and then a stale batch arrives late */
        if (late_len[d] && ((r >> 30) % 5) == 0) {
            rc = zvb_gw_accept(&gw, late[d], late_len[d], held[d] + nheld[d], N + 8 - nheld[d], NULL, &next);
            CHECK(rc == ZVB_E_DUPLICATE || rc == ZVB_E_ORDER || rc == ZXV_OK, "late %d", (int) rc);
            if (rc == ZXV_OK) {
                zvb_batch_info bi;
                zvb_batch_decode(late[d], late_len[d], NULL, 0, &bi);
                nheld[d] += bi.count;
                /* the device learns at its next reply */
            }
            late_len[d] = 0;
        }
    }
    for (int d = 0; d < DEV; d++) {
        CHECK(appended[d] == N && nheld[d] == N, "dev %d: %u logged, %u held at the gateway", d, appended[d],
              nheld[d]);
        for (uint32_t i = 0; i < nheld[d] && i < appended[d]; i++)
            CHECK(rec_eq(&truth[d][i], &held[d][i]), "dev %d reading %u", d, i);
        CHECK(devs[d].next_seq == N + 1, "dev %d next %u", d, devs[d].next_seq);
    }
    CHECK(lost > 10 && acks_lost > 10 && corrupt > 10 && rf.n > 10 && full[0] + full[1] > 0,
          "simulation exercised loss %u, lost replies %u, corruption %u, refusals %d, full %u", lost,
          acks_lost, corrupt, rf.n, full[0] + full[1]);
    printf("simulation: %u+%u readings delivered in order; %u uplinks lost, %u replies lost, "
           "%u corrupted, %d gateway refusals, %u full-log refusals\n",
           nheld[0], nheld[1], lost, acks_lost, corrupt, rf.n, full[0] + full[1]);
}

int main(void) {
    test_names();
    test_log();
    test_payload();
    test_batches();
    test_tamper();
    test_wire();
    test_gateway();
    test_simulation();
    return zt_report("test_vband");
}
