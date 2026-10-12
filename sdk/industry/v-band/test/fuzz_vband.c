/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_vband.c — a deterministic operation fuzzer for v-band against an
 * independent model. Three devices with small logs; random readings
 * (TRUE, FALSE, NEITHER, agreeing and disagreeing pairs, invalid ones);
 * batches at random regions and data rates carried over UBH-168 with
 * random bit flips; replies delivered, lost or forged; captured batches
 * replayed late or edited; trims. After every operation the status equals
 * the model's, the log's counters equal the model's, every accepted batch
 * holds exactly the next readings the device logged, every batch fits its
 * data rate and holds as many readings as fit, and every corrupted or
 * edited batch is refused.
 *
 * One random draw per statement, so the stream (and the output) is
 * byte-identical under any C compiler.
 *
 *   fuzz_vband ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zvband.h"
#include "zind_test.h"

#define NDEV 3
#define CAP  16

typedef struct {
    zvb_log log;
    zvb_record st[CAP];
    zvb_record *truth; /* every reading ever logged, by seq - 1 */
    uint32_t next_seq, acked, sent, sent_max, n, last_time, next_batch;
    uint8_t chain[32];
    /* gateway's model of this device */
    uint32_t gw_next, gw_last_batch;
    /* a captured batch for replay */
    uint8_t cap_buf[ZVB_BATCH_MAX];
    size_t cap_len;
} mdev;

static void put32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t vlen(uint32_t v) {
    uint32_t n = 1;
    while (v >= 128) {
        v >>= 7;
        n++;
    }
    return n;
}

static uint32_t zz(int32_t v) {
    int64_t x = v;
    return (uint32_t) (((uint64_t) x << 1) ^ (uint64_t) (x < 0 ? -1 : 0));
}

static uint32_t msize(const zvb_record *r, uint32_t prev) {
    uint32_t n = 2 + vlen(r->time_s - prev) + vlen(zz(r->value));
    if (r->lp == ZXV_LP_BOTH) n += vlen(zz(r->value2));
    return n;
}

static void mchain(uint8_t chain[32], uint32_t device, const zvb_record *r) {
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

static int same_rec(const zvb_record *a, const zvb_record *b) {
    return a->seq == b->seq && a->time_s == b->time_s && a->sensor == b->sensor &&
           a->lp == b->lp && a->value == b->value && a->value2 == b->value2;
}

static uint32_t g_refusals;
static void on_refuse(void *user, uint32_t device, int32_t st, uint32_t batch) {
    (void) user;
    (void) device;
    (void) st;
    (void) batch;
    g_refusals++;
}

static int32_t mvalue(uint64_t r) {
    switch ((r >> 20) % 4) {
    case 0: return (int32_t) ((r >> 24) % 100);
    case 1: return (int32_t) ((r >> 24) % 20000) - 10000;
    case 2: return (int32_t) (uint32_t) (r >> 32);
    default: return ((r >> 24) & 1) ? INT32_MAX : INT32_MIN;
    }
}

int main(int argc, char **argv) {
    long iters = argc > 1 ? atol(argv[1]) : 2000;
    uint64_t seed = zt_seed_from_env(0x7642616E64ULL), s = seed;
    mdev *dv = calloc(NDEV, sizeof *dv);
    zvb_gw gw;
    zvb_gw_dev gdev[NDEV];
    uint32_t c_append = 0, c_full = 0, c_bad = 0, c_pairs_both = 0, c_batches = 0, c_empty = 0,
             c_small = 0, c_ok = 0, c_dup = 0, c_order = 0, c_wire = 0, c_check = 0,
             c_replay = 0, c_edit = 0, c_lost = 0, c_forged_ack = 0, c_trim = 0, c_readings = 0;
    if (!dv) return 2;
    zvb_gw_init(&gw, gdev, NDEV, on_refuse, NULL);
    for (int d = 0; d < NDEV; d++) {
        dv[d].truth = calloc((size_t) iters + 8, sizeof(zvb_record));
        if (!dv[d].truth) return 2;
        zvb_log_init(&dv[d].log, dv[d].st, CAP, (uint32_t) (500 + d));
        zvb_gw_add(&gw, (uint32_t) (500 + d));
        dv[d].next_seq = 1;
        dv[d].next_batch = 1;
        dv[d].gw_next = 1;
    }

    for (long it = 0; it < iters; it++) {
        uint64_t r = zt_rand(&s);
        int d = (int) (r % NDEV);
        int op = (int) ((r >> 4) % 16);
        mdev *m = &dv[d];
        zvb_log *log = &m->log;
        int32_t rc, want;
        uint64_t q = zt_rand(&s);

        if (op < 8) { /* a reading */
            zvb_record nr;
            int kind = (int) (q % 7);
            uint32_t dt = (uint32_t) ((q >> 3) % 50);
            uint32_t t = ((q >> 9) % 23 == 0 && m->last_time > 0) ? m->last_time - 1 : m->last_time + dt;
            int32_t v = mvalue(q);
            int32_t v2 = (v > INT32_MAX - 4 || v < INT32_MIN + 4) ? v : v + (int32_t) ((q >> 40) % 9) - 4;
            uint32_t tol = (uint32_t) ((q >> 50) % 4);
            int valid = 1;
            memset(&nr, 0, sizeof nr);
            nr.sensor = (uint8_t) (q >> 56);
            nr.time_s = t;
            nr.value = v;
            if (kind <= 2) nr.lp = ZXV_LP_TRUE;
            else if (kind == 3) nr.lp = ZXV_LP_FALSE;
            else if (kind == 4) {
                nr.lp = ZXV_LP_NEITHER;
                nr.value = ((q >> 30) % 5 == 0) ? 1 : 0;
                valid = nr.value == 0;
            } else {
                uint32_t diff = v >= v2 ? (uint32_t) v - (uint32_t) v2 : (uint32_t) v2 - (uint32_t) v;
                nr.lp = diff <= tol ? ZXV_LP_TRUE : ZXV_LP_BOTH;
                nr.value2 = diff <= tol ? 0 : v2;
            }
            if (t < m->last_time) valid = 0;
            /* model of the room left */
            if (!valid) want = ZXV_E_ARG;
            else if (m->n == CAP && m->acked < m->next_seq - m->n) want = ZVB_E_FULL;
            else want = ZXV_OK;
            if (kind >= 5) rc = zvb_log_append_pair(log, nr.sensor, t, v, v2, tol);
            else rc = zvb_log_append(log, nr.sensor, t, nr.lp, nr.value);
            CHECK(rc == want, "it %ld: append -> %d want %d", it, (int) rc, (int) want);
            if (rc == ZXV_OK) {
                if (m->n == CAP) {
                    uint32_t first = m->next_seq - m->n;
                    while (m->n > 0 && first <= m->acked) {
                        first++;
                        m->n--;
                    }
                }
                nr.seq = m->next_seq;
                m->truth[nr.seq - 1] = nr;
                mchain(m->chain, (uint32_t) (500 + d), &nr);
                m->next_seq++;
                m->n++;
                m->last_time = t;
                c_append++;
                c_pairs_both += nr.lp == ZXV_LP_BOTH;
            } else if (rc == ZVB_E_FULL) c_full++;
            else c_bad++;
        } else if (op < 13) { /* a batch over the mesh to the gateway */
            uint8_t buf[ZVB_BATCH_MAX], fr[ZVB_WIRE_MAX_FRAMES * 21], rx[ZVB_BATCH_MAX];
            zvb_record got[ZVB_BATCH_MAX / 4];
            zvb_batch_info info;
            size_t len = 0, nf = 0, rl = 0;
            uint32_t count = 0, max = 0, next = 0, first = m->sent + 1, fit = 0, size = ZVB_BATCH_HEADER + 4;
            int region = (int) (q % 3) + 1;
            uint8_t dr = (uint8_t) ((q >> 2) % 8);
            int fate = (int) ((q >> 5) % 8);
            if (zvb_payload_max(region, dr, &max) != ZXV_OK) max = 51;
            /* the model's count: as many as fit */
            for (uint32_t k = first; k < m->next_seq && fit < ZVB_BATCH_RECORDS; k++) {
                uint32_t sz = msize(&m->truth[k - 1], k == first ? m->truth[k - 1].time_s : m->truth[k - 2].time_s);
                if (size + sz > max) break;
                size += sz;
                fit++;
            }
            want = first >= m->next_seq ? ZVB_E_EMPTY : fit == 0 ? ZVB_E_DR : ZXV_OK;
            rc = zvb_batch_make(log, max, buf, sizeof buf, &len, &count);
            CHECK(rc == want, "it %ld: make -> %d want %d", it, (int) rc, (int) want);
            if (rc == ZVB_E_EMPTY) {
                c_empty++;
                if (m->acked < m->sent) { /* waited long enough for a reply: resend */
                    CHECK(zvb_log_ack(log, m->acked + 1) == ZXV_OK, "timeout ack");
                    m->sent = m->acked;
                }
            }
            if (rc == ZVB_E_DR) c_small++;
            if (rc != ZXV_OK) goto after;
            CHECK(count == fit && len == size && len <= max, "it %ld: batch %u/%u readings, %zu/%u bytes",
                  it, count, fit, len, size);
            m->sent += count;
            if (m->sent > m->sent_max) m->sent_max = m->sent;
            m->next_batch++;
            c_batches++;
            if (fate == 0) { /* captured for later, lost now */
                memcpy(m->cap_buf, buf, len);
                m->cap_len = len;
                c_lost++;
                CHECK(zvb_log_ack(log, m->acked + 1) == ZXV_OK, "timeout ack");
                m->sent = m->acked;
                goto after;
            }
            if (fate == 4) { /* delayed in the mesh: the device sends on */
                memcpy(m->cap_buf, buf, len);
                m->cap_len = len;
                goto after;
            }
            rc = zvb_wire_encode((uint32_t) d, buf, len, fr, sizeof fr, &nf);
            CHECK(rc == ZXV_OK && nf == zvb_wire_frames(len), "it %ld: wire encode", it);
            if (fate == 1) {
                size_t at = (size_t) ((q >> 8) % (nf * 21));
                fr[at] ^= (uint8_t) (1u << ((q >> 20) % 8));
            }
            rc = zvb_wire_decode(fr, nf, rx, sizeof rx, &rl);
            if (fate == 1) {
                CHECK(rc != ZXV_OK, "it %ld: flipped frame accepted", it);
                c_wire++;
                CHECK(zvb_log_ack(log, m->acked + 1) == ZXV_OK, "timeout ack");
                m->sent = m->acked;
                goto after;
            }
            CHECK(rc == ZXV_OK && rl == len && memcmp(rx, buf, len) == 0, "it %ld: wire round trip", it);
            if (fate == 2) { /* corrupted between the mesh and the gateway */
                size_t at = (size_t) ((q >> 8) % len);
                rx[at] ^= (uint8_t) (1u << ((q >> 20) % 8));
                rc = zvb_gw_accept(&gw, rx, rl, got, ZVB_BATCH_MAX / 4, &info, &next);
                CHECK(rc == ZVB_E_CHECK || (at == 0 && rc == ZXV_E_FORMAT), "it %ld: corrupt -> %d", it, (int) rc);
                c_check++;
                CHECK(zvb_log_ack(log, m->acked + 1) == ZXV_OK, "timeout ack");
                m->sent = m->acked;
                goto after;
            }
            {
                uint32_t bno = m->next_batch - 1;
                if (bno == m->gw_last_batch || first < m->gw_next) want = ZVB_E_DUPLICATE;
                else if (bno < m->gw_last_batch || first > m->gw_next) want = ZVB_E_ORDER;
                else want = ZXV_OK;
                rc = zvb_gw_accept(&gw, rx, rl, got, ZVB_BATCH_MAX / 4, &info, &next);
                CHECK(rc == want, "it %ld: gateway -> %d want %d", it, (int) rc, (int) want);
                if (rc == ZXV_OK) {
                    for (uint32_t i = 0; i < count; i++)
                        CHECK(same_rec(&got[i], &m->truth[first - 1 + i]), "it %ld: reading %u", it, first + i);
                    m->gw_next += count;
                    m->gw_last_batch = bno;
                    c_ok++;
                    c_readings += count;
                } else if (rc == ZVB_E_DUPLICATE) c_dup++;
                else c_order++;
                CHECK(next == m->gw_next, "it %ld: reply next %u want %u", it, next, m->gw_next);
            }
            if (fate == 3) { /* reply lost */
                c_lost++;
                CHECK(zvb_log_ack(log, m->acked + 1) == ZXV_OK, "timeout ack");
                m->sent = m->acked;
            } else {
                rc = zvb_log_ack(log, next);
                want = (next < m->acked + 1 || next > m->sent_max + 1) ? ZXV_E_ARG : ZXV_OK;
                CHECK(rc == want, "it %ld: ack %u -> %d", it, next, (int) rc);
                if (rc == ZXV_OK) {
                    m->acked = next - 1;
                    m->sent = m->acked;
                }
            }
        } else if (op == 13) { /* replay or edit a captured batch */
            zvb_record got[ZVB_BATCH_MAX / 4];
            zvb_batch_info info;
            uint8_t t[ZVB_BATCH_MAX];
            uint32_t next = 0;
            uint32_t bno, first;
            if (!m->cap_len) goto after;
            memcpy(t, m->cap_buf, m->cap_len);
            bno = (uint32_t) t[5] | (uint32_t) t[6] << 8 | (uint32_t) t[7] << 16 | (uint32_t) t[8] << 24;
            first = (uint32_t) t[9] | (uint32_t) t[10] << 8 | (uint32_t) t[11] << 16 | (uint32_t) t[12] << 24;
            if (q % 3 == 0) {
                t[(q >> 8) % m->cap_len] ^= (uint8_t) (1u << ((q >> 20) % 8));
                rc = zvb_gw_accept(&gw, t, m->cap_len, got, ZVB_BATCH_MAX / 4, &info, &next);
                CHECK(rc == ZVB_E_CHECK || rc == ZXV_E_FORMAT, "it %ld: edited replay -> %d", it, (int) rc);
                c_edit++;
            } else {
                if (bno == m->gw_last_batch || first < m->gw_next) want = ZVB_E_DUPLICATE;
                else if (bno < m->gw_last_batch || first > m->gw_next) want = ZVB_E_ORDER;
                else want = ZXV_OK;
                rc = zvb_gw_accept(&gw, t, m->cap_len, got, ZVB_BATCH_MAX / 4, &info, &next);
                CHECK(rc == want, "it %ld: replay -> %d want %d", it, (int) rc, (int) want);
                if (rc == ZXV_OK) { /* the device learns at its next reply */
                    for (uint32_t i = 0; i < info.count; i++)
                        CHECK(same_rec(&got[i], &m->truth[first - 1 + i]), "it %ld: late reading", it);
                    m->gw_next += info.count;
                    m->gw_last_batch = bno;
                    c_readings += info.count;
                    c_ok++;
                } else c_replay++;
            }
            m->cap_len = 0;
        } else if (op == 14) { /* a forged or garbled reply */
            uint32_t nx = (uint32_t) (q % (m->next_seq + 3));
            /* an in-range reply claiming readings the gateway lacks would
             * make the device drop them: the LoRaWAN MIC is what stops
             * that (README "Limits"), so here such a reply is garbled
             * beyond the range instead */
            if (nx > m->gw_next && nx >= m->acked + 1 && nx <= m->sent_max + 1) nx = m->sent_max + 2;
            want = (nx < m->acked + 1 || nx > m->sent_max + 1) ? ZXV_E_ARG : ZXV_OK;
            rc = zvb_log_ack(log, nx);
            CHECK(rc == want, "it %ld: forged ack %u -> %d want %d", it, nx, (int) rc, (int) want);
            if (rc == ZXV_OK) {
                m->acked = nx - 1;
                m->sent = m->acked;
            } else c_forged_ack++;
        } else { /* trim */
            uint32_t rel = 0, wantrel = 0, first = m->next_seq - m->n;
            while (wantrel < m->n && first + wantrel <= m->acked) wantrel++;
            rc = zvb_log_trim(log, &rel);
            CHECK(rc == ZXV_OK && rel == wantrel, "it %ld: trim %u want %u", it, rel, wantrel);
            m->n -= wantrel;
            c_trim++;
        }
    after:
        CHECK(log->next_seq == m->next_seq && log->acked == m->acked && log->sent == m->sent &&
                  log->sent_max == m->sent_max && log->n == m->n && log->next_batch == m->next_batch &&
                  memcmp(log->chain, m->chain, 32) == 0,
              "it %ld: device %d counters", it, d);
        CHECK(gdev[d].next_seq == m->gw_next && gdev[d].last_batch == m->gw_last_batch,
              "it %ld: gateway %d", it, d);
        if (g_fail > 20) break;
    }
    printf("readings: logged %u (%u disagreeing pairs), full %u, invalid %u, trims %u\n", c_append,
           c_pairs_both, c_full, c_bad, c_trim);
    printf("batches %u, empty %u, too small %u; accepted %u carrying %u readings; duplicates %u, "
           "out of order %u, late replays refused %u; wire flips %u, bad checks %u, edited replays %u, lost %u, "
           "forged replies %u; refusals reported %u, seed %llu\n",
           c_batches, c_empty, c_small, c_ok, c_readings, c_dup, c_order, c_replay, c_wire, c_check, c_edit,
           c_lost, c_forged_ack, g_refusals, (unsigned long long) (seed % 1000003));
    for (int d = 0; d < NDEV; d++) free(dv[d].truth);
    free(dv);
    return zt_report("fuzz_vband");
}
