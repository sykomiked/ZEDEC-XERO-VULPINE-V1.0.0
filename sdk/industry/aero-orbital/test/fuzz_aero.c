/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_aero.c — a deterministic operation fuzzer for aero-orbital against
 * an independent model. Two receivers (HOLD and MAJORITY policy, three
 * copies each) get random copies of a sliding window of packets: clean
 * copies, SEU variants with a valid CRC, and copies with a broken CRC; plus
 * early closes, analyst reviews (genuine accept or reject, forged, by an
 * unregistered key) and single-bit flips of the UBH-168 frames, which must
 * always be refused. Every status, state and decided value is compared with
 * the model. Geodesy is fuzzed alongside: random points round-trip within
 * 2 mm and random ECEF triples convert or are refused, never anything else.
 * Every 250 operations both decision chains are verified offline.
 *
 * One random draw per statement, so the stream (and the output) is
 * byte-identical under any C compiler.
 *
 *   fuzz_aero ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zaero.h"
#include "zind_test.h"

#define WIN 24

typedef struct {
    int good;
    int label; /* identical labels <=> identical bytes (good copies only) */
} mcopy_t;

typedef struct {
    int state; /* 0 = never seen */
    int n;
    mcopy_t c[ZAO_MAX_COPIES];
    int chosen;
} mpkt_t;

typedef struct {
    zao_rx *rx;
    int policy;
    mpkt_t m[4096];
} side_t;

static unsigned long g_ops[16];

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_aero: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    abort();
}

/* The bytes of copy `label` of packet seq (good), or with bit `flip` of the
 * readings broken after the CRC (bad). */
static size_t make_copy(uint32_t seq, int label, int bad, uint32_t flip, uint8_t *buf) {
    zao_tm tm;
    size_t len = 0;
    memset(&tm, 0, sizeof tm);
    tm.scid = 0x2A;
    tm.vcid = 3;
    tm.seq = seq;
    tm.time_ms = 1000ULL * seq;
    tm.n = (uint8_t) (1 + seq % 5);
    for (int i = 0; i < tm.n; i++) {
        tm.v[i].channel = (uint16_t) i;
        tm.v[i].value = (int64_t) seq * 100 + i + label * 1000003;
    }
    zao_tm_encode(&tm, buf, ZAO_TM_MAX_BYTES, &len);
    if (bad) buf[17 + flip % (len - 21)] ^= (uint8_t) (1u << (flip % 8));
    return len;
}

static int model_evaluate(side_t *sd, mpkt_t *m) {
    int bad = 0, distinct = 0, best = 0, best_i = -1;
    for (int i = 0; i < m->n; i++) {
        int same = 0, first = 1;
        if (!m->c[i].good) {
            bad++;
            continue;
        }
        for (int j = 0; j < m->n; j++)
            if (m->c[j].good && m->c[j].label == m->c[i].label) {
                if (j < i) first = 0;
                same++;
            }
        if (first) {
            distinct++;
            if (same > best) {
                best = same;
                best_i = i;
            }
        }
    }
    if (bad == 0 && distinct == 1) {
        m->state = ZAO_PK_RELEASED;
        m->chosen = 0;
        return ZXV_OK;
    }
    if (sd->policy == ZAO_POLICY_MAJORITY && best_i >= 0 && 2 * best > m->n) {
        m->state = ZAO_PK_CORRECTED;
        m->chosen = best_i;
        return ZAO_E_CORRECTED;
    }
    m->state = ZAO_PK_HELD;
    return ZAO_E_SEU;
}

static int model_submit(side_t *sd, mpkt_t *m, int good, int label) {
    if (m->state == 0) m->state = ZAO_PK_COLLECTING;
    if (m->state == ZAO_PK_REJECTED) return ZIND_E_FROZEN;
    if (m->state == ZAO_PK_RELEASED || m->state == ZAO_PK_CORRECTED ||
        m->state == ZAO_PK_ACCEPTED) {
        if (good && m->c[m->chosen].label == label) return ZIND_E_REPLAY;
        if (m->n < (int) ZAO_MAX_COPIES) {
            m->c[m->n].good = good;
            m->c[m->n].label = label;
            m->n++;
        }
        m->state = ZAO_PK_HELD;
        return ZAO_E_SEU;
    }
    if (m->n >= (int) ZAO_MAX_COPIES) return ZXV_E_CAPACITY;
    m->c[m->n].good = good;
    m->c[m->n].label = label;
    m->n++;
    if (m->state == ZAO_PK_HELD) return ZIND_E_HELD;
    if (m->n >= 3) return model_evaluate(sd, m);
    return ZXV_OK;
}

static void check_pkt(side_t *sd, uint32_t seq, unsigned long it) {
    zao_key k = {0x2A, 3, seq};
    zao_pkt_info info;
    zao_tm tm;
    const mpkt_t *m = &sd->m[seq];
    int32_t rc = zao_rx_info(sd->rx, k, &info);
    if (m->state == 0) {
        if (rc != ZAO_E_PACKET) die("an unseen packet exists", it);
        return;
    }
    if (rc != ZXV_OK || info.state != m->state || info.copies != m->n)
        die("packet state differs from the model", it);
    rc = zao_rx_value(sd->rx, k, &tm);
    if (m->state == ZAO_PK_RELEASED || m->state == ZAO_PK_CORRECTED ||
        m->state == ZAO_PK_ACCEPTED) {
        uint8_t want[ZAO_TM_MAX_BYTES], got[ZAO_TM_MAX_BYTES];
        size_t wl = make_copy(seq, m->c[m->chosen].label, 0, 0, want), gl = 0;
        if (rc != ZXV_OK || zao_tm_encode(&tm, got, sizeof got, &gl) != ZXV_OK || gl != wl ||
            memcmp(got, want, wl) != 0)
            die("the decided value differs from the model", it);
    } else if (rc == ZXV_OK) {
        die("an undecided packet gave a value", it);
    }
}

static void check_chain(const zxv_cap *cap, const side_t *sd, const zind_signer *op,
                        const uint8_t *const *an, unsigned long it) {
    size_t len = 0;
    uint8_t *buf;
    uint32_t nr = 0, nc = 0, bad = 0;
    if (zao_decisions_export(sd->rx, NULL, 0, &len) != ZXV_E_BUFFER) die("export size", it);
    buf = (uint8_t *) malloc(len);
    if (!buf || zao_decisions_export(sd->rx, buf, len, &len) != ZXV_OK) die("export", it);
    if (zao_decisions_verify(cap, buf, len, an, 1, zind_signer_pk(op), &nr, &nc, &bad) != ZXV_OK)
        die("the decision chain does not verify offline", it);
    free(buf);
}

/* one review of held packet `seq`: genuine (accept a good copy, or reject),
 * forged, or by an unregistered key */
static void review(const zxv_cap *cap, side_t *sd, uint32_t seq, uint64_t *s,
                   const zind_signer *an, const zind_signer *rogue, unsigned long it) {
    zao_key k = {0x2A, 3, seq};
    mpkt_t *m = &sd->m[seq];
    uint8_t msg[64], sig[ZIND_SIG_BYTES];
    size_t ml = 0;
    uint64_t r;
    int how, copy = -1;
    int32_t dec, rc;
    for (int i = 0; i < m->n && copy < 0; i++)
        if (m->c[i].good) copy = i;
    r = zt_rand(s);
    dec = copy >= 0 && r % 3 != 0 ? ZAO_ACCEPT : ZAO_REJECT;
    if (dec == ZAO_REJECT) copy = 0;
    r = zt_rand(s);
    how = (int) (r % 5);
    if (zao_review_message(sd->rx, k, dec, (uint32_t) copy, msg, sizeof msg, &ml) != ZXV_OK)
        die("review message", it);
    if (zind_sign(cap, how == 0 ? rogue : an, msg, ml, ZAO_REVIEW_CTX, sig) != ZXV_OK)
        die("sign", it);
    rc = zao_review_resolve(sd->rx, k, zind_signer_pk(how == 1 ? rogue : an), dec,
                            (uint32_t) copy, sig);
    if (how == 0) {
        if (rc != ZXV_E_VERIFY) die("a forged review was accepted", it);
        g_ops[8]++;
    } else if (how == 1) {
        if (rc != ZAO_E_ANALYST) die("an unregistered analyst was accepted", it);
        g_ops[8]++;
    } else {
        if (rc != ZXV_OK) die("a genuine review was refused", it);
        if (dec == ZAO_ACCEPT) {
            m->state = ZAO_PK_ACCEPTED;
            m->chosen = copy;
        } else {
            m->state = ZAO_PK_REJECTED;
        }
        g_ops[9]++;
    }
}

static void fuzz_geo(uint64_t *s, unsigned long it) {
    zao_geo g, back;
    zao_ecef e, e2;
    uint64_t r;
    int32_t st;
    r = zt_rand(s);
    g.lat_nd = (int64_t) (r % 180000000001ULL) - ZAO_LAT_MAX;
    r = zt_rand(s);
    g.lon_nd = (int64_t) (r % 360000000001ULL) - ZAO_LON_MAX;
    r = zt_rand(s);
    g.h_mm = (int64_t) (r % (uint64_t) (ZAO_H_MAX - ZAO_H_MIN + 1)) + ZAO_H_MIN;
    if (zao_geo_to_ecef(&g, &e) != ZXV_OK) die("an in-range point was refused", it);
    if (zao_ecef_to_geo(&e, &back) != ZXV_OK) die("its ECEF did not convert back", it);
    if (zao_geo_to_ecef(&back, &e2) != ZXV_OK) die("the round trip was refused", it);
    if (e2.x_mm - e.x_mm > 2 || e.x_mm - e2.x_mm > 2 || e2.y_mm - e.y_mm > 2 ||
        e.y_mm - e2.y_mm > 2 || e2.z_mm - e.z_mm > 2 || e.z_mm - e2.z_mm > 2 ||
        back.h_mm - g.h_mm > 2 || g.h_mm - back.h_mm > 2)
        die("round trip beyond 2 mm", it);
    /* an arbitrary triple: converts, or is refused for range */
    r = zt_rand(s);
    e.x_mm = (int64_t) (r % 260000000001ULL) - 130000000000;
    r = zt_rand(s);
    e.y_mm = (int64_t) (r % 260000000001ULL) - 130000000000;
    r = zt_rand(s);
    e.z_mm = (int64_t) (r % 260000000001ULL) - 130000000000;
    st = zao_ecef_to_geo(&e, &back);
    if (st == ZXV_OK) {
        g_ops[11]++;
        if (zao_geo_to_ecef(&back, &e2) != ZXV_OK) die("a converted point did not go back", it);
        if (e2.x_mm - e.x_mm > 2 || e.x_mm - e2.x_mm > 2 || e2.z_mm - e.z_mm > 2 ||
            e.z_mm - e2.z_mm > 2)
            die("ECEF round trip beyond 2 mm", it);
    } else if (st == ZAO_E_RANGE) {
        g_ops[12]++;
    } else {
        die("ECEF conversion returned something other than OK or RANGE", it);
    }
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0xAE70ULL), s = seed;
    zt_env e;
    zind_signer *op, *an, *rogue;
    zxv_hexlog *qlog = NULL;
    static side_t sides[2];
    const uint8_t *an_pk[1];
    uint32_t base = 0;
    if (zt_env_open(&e, NULL) != 0) return 2;
    op = zt_signer(e.cap, 1);
    an = zt_signer(e.cap, 2);
    rogue = zt_signer(e.cap, 3);
    if (!op || !an || !rogue || zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK) return 2;
    for (int i = 0; i < 2; i++) {
        zao_rx_cfg cfg = {3, (uint8_t) i, 4096};
        sides[i].policy = i;
        if (zao_rx_create(e.cap, op, qlog, &cfg, &sides[i].rx) != ZXV_OK ||
            zao_analyst_add(sides[i].rx, zind_signer_pk(an)) != ZXV_OK)
            return 2;
    }
    an_pk[0] = zind_signer_pk(an);

    for (unsigned long it = 0; it < iters; it++) {
        uint64_t r = zt_rand(&s);
        side_t *sd = &sides[r & 1];
        int op_ = (int) ((r >> 8) % 100);
        uint32_t seq;
        mpkt_t *m;
        r = zt_rand(&s);
        seq = base + (uint32_t) (r % WIN);
        m = &sd->m[seq];
        if (it % 100 == 99 && base + WIN + 8 < 4096) base += 8;
        if (op_ < 70) {
            /* a copy: clean, an SEU variant with a good CRC, or a broken CRC */
            uint8_t buf[ZAO_TM_MAX_BYTES], fr[40 * 21];
            size_t len, nf = 0;
            int kind, label = 0, good = 1;
            uint32_t flip = 0;
            int32_t want, rc;
            r = zt_rand(&s);
            kind = (int) (r % 20);
            if (kind >= 17) {
                good = 0;
                r = zt_rand(&s);
                flip = (uint32_t) r;
                label = -1;
            } else if (kind >= 14) {
                label = 1 + kind % 2;
            }
            len = make_copy(seq, label < 0 ? 0 : label, !good, flip, buf);
            if (zao_wire_encode((uint32_t) (seq % 10), buf, len, fr, sizeof fr, &nf) != ZXV_OK)
                die("wire encode", it);
            r = zt_rand(&s);
            if (r % 10 == 0) {
                /* a transport error: refused, nothing recorded */
                r = zt_rand(&s);
                fr[(r >> 3) % (nf * 21)] ^= (uint8_t) (1u << (r % 8));
                if (zao_rx_submit(sd->rx, fr, nf, NULL) != ZAO_E_WIRE)
                    die("a flipped frame bit was accepted", it);
                g_ops[0]++;
            } else {
                want = model_submit(sd, m, good, label);
                rc = zao_rx_submit(sd->rx, fr, nf, NULL);
                if (rc != want) {
                    fprintf(stderr, "seq %u good %d label %d -> %s, model %s\n", seq, good, label,
                            zao_status_name(rc), zao_status_name(want));
                    die("submit status differs from the model", it);
                }
                g_ops[1 + (rc == ZXV_OK ? 0 : rc == ZAO_E_SEU ? 1 : rc == ZAO_E_CORRECTED ? 2
                                                  : rc == ZIND_E_REPLAY ? 3 : rc == ZIND_E_HELD ? 4
                                                                                               : 5)]++;
            }
        } else if (op_ < 78) {
            /* close early */
            zao_key k = {0x2A, 3, seq};
            int32_t want, rc = zao_rx_close(sd->rx, k);
            if (m->state == 0) want = ZAO_E_PACKET;
            else if (m->state != ZAO_PK_COLLECTING) want = ZXV_E_STATE;
            else want = model_evaluate(sd, m);
            if (rc != want) die("close status differs from the model", it);
            g_ops[7]++;
        } else if (op_ < 95) {
            /* a review of a held packet in the window */
            for (int q = 0; q < WIN && m->state != ZAO_PK_HELD; q++) {
                seq = base + (uint32_t) q;
                m = &sd->m[seq];
            }
            if (m->state == ZAO_PK_HELD) review(e.cap, sd, seq, &s, an, rogue, it);
        } else {
            fuzz_geo(&s, it);
            g_ops[10]++;
        }
        check_pkt(sd, seq, it);
        if (it % 250 == 249) {
            check_chain(e.cap, &sides[0], op, an_pk, it);
            check_chain(e.cap, &sides[1], op, an_pk, it);
        }
    }
    for (int i = 0; i < 2; i++) {
        for (uint32_t q = 0; q < base + WIN; q++) check_pkt(&sides[i], q, iters);
        check_chain(e.cap, &sides[i], op, an_pk, iters);
    }
    if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    {
        uint32_t nq = 0;
        zxv_hexlog_count(qlog, &nq);
        printf("fuzz_aero: %lu ops (copies: ok %lu, held %lu, corrected %lu, replays %lu, "
               "evidence %lu, other %lu; transport flips refused %lu; closes %lu; reviews %lu, "
               "forged %lu; geodesy %lu, ECEF converted %lu, refused %lu; log entries %u), seed "
               "%llu, 0 invariant failures\n",
               iters, g_ops[1], g_ops[2], g_ops[3], g_ops[4], g_ops[5], g_ops[6], g_ops[0],
               g_ops[7], g_ops[9], g_ops[8], g_ops[10], g_ops[11], g_ops[12], nq,
               (unsigned long long) seed);
    }
    for (int i = 0; i < 2; i++) zao_rx_destroy(sides[i].rx);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(op);
    zind_signer_destroy(an);
    zind_signer_destroy(rogue);
    zt_env_close(&e);
    return 0;
}
