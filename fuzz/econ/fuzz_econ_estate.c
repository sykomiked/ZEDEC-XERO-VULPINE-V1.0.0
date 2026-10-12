/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_estate.c — state-machine property harness for estate succession
 * (estate/estate.c, estate/faraid.c): phases, the exact manifest, contested
 * escrow, royalty streaming, forced heirship and faraid.
 *
 * Keys are 32-byte ids; the verify hook accepts sig == first 8 bytes of
 * SHA-256(key || msg). Every signed op is tried honest, with the wrong key,
 * with a flipped bit, or over a stale epoch / digest. Ops: REPORT, APPROVE,
 * CONFIRM, RULE, BEGIN, CLOSE, VETO, FRAUD, SET_MANIFEST (exact or broken
 * fractions), SET_FORCED (France or a registered row), SET_FARAID,
 * DISTRIBUTE, STREAM, CONTEST, RESOLVE, KEY_RELEASE.
 * Invariants after every op:
 *   E-CONS   everything handed to distribute + stream == everything paid
 *            out + escrow_total + est_stream_held; escrow_total == sum of
 *            the per-heir escrows (principal only: nothing grows)
 *   E-REF    a refused op changes nothing (memcmp of the whole est_t)
 *   E1       phase moves only along the spec; a signed op succeeds only with
 *            an honest signature of the right key over the current epoch
 *            and manifest; confirm needs k approvals and the wait; a veto
 *            refunds a held bond and bumps the epoch; a forfeited bond stays
 *            forfeited through a veto
 *   E2       the stored manifest sums to exactly 1 and equals the given
 *            fractions; distribute parts sum to the amount exactly, each part
 *            is floor or floor+1 of the exact share; stream pays cumulative
 *            floor(T * share) exactly, held < number of heirs
 *   E3       key release iff DISTRIBUTING/CLOSED and tick >= confirmed +
 *            release_ticks
 *   E5       forced shares: children equal, children together >= the
 *            reserved portion, no gift above the will's, the whole == 1
 *   F        faraid (exhaustive over the modelled heirs, once): class shares
 *            sum to exactly 1, a son takes twice a daughter, and
 *            est_set_faraid accepts every estate with no unallocated rest
 *   NO USURY: escrow and stream hold principal only; no amount depends on
 *            the tick. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "estate.h"
#include "faraid.h"
#include "../robin_debanks/sha256.h"
#include "fuzz_in.h"

typedef unsigned __int128 u128;

static est_t E;
static est_cfg_t C;
static est_forced_table_t T;
static unsigned long long g_ops, g_okc[20];
static u128 g_in, g_out, g_stream_T;

static void mksig(const uint8_t key[EST_KEY], const uint8_t *m, uint32_t n, uint8_t sig[8])
{
    uint8_t b[EST_KEY + EST_MSG + 8], h[32];
    memcpy(b, key, EST_KEY);
    memcpy(b + EST_KEY, m, n);
    sha256(b, EST_KEY + n, h);
    memcpy(sig, h, 8);
}

static bool verify(void *ctx, const uint8_t key[EST_KEY], const uint8_t *m, uint32_t n,
                   const uint8_t *sig, uint32_t sl)
{
    (void) ctx;
    uint8_t want[8];
    if (sl != 8 || n != EST_MSG) return false;
    mksig(key, m, n, want);
    return !memcmp(want, sig, 8);
}

static void key(uint8_t k[EST_KEY], uint8_t tag)
{
    for (uint32_t i = 0; i < EST_KEY; i++) k[i] = (uint8_t) (tag * 13u + i);
}

/* sign kind/value as `k`; mode: 0 honest, 1 wrong key, 2 flipped, 3 stale */
static bool sign_as(const uint8_t k[EST_KEY], uint8_t kind, uint64_t value, uint8_t mode,
                    uint8_t sig[8])
{
    est_t x = E;
    if (mode == 3) {
        if (x.epoch == 0) x.digest[0] ^= 1;
        else x.epoch--;
    }
    uint8_t m[EST_MSG];
    est_msg(&x, kind, value, m);
    uint8_t kk[EST_KEY];
    memcpy(kk, k, EST_KEY);
    if (mode == 1) kk[3] ^= 0x55;
    mksig(kk, m, sizeof m, sig);
    if (mode == 2) sig[4] ^= 0x20;
    return mode == 0;
}

static uint8_t sig_mode(fz_in *in)
{
    uint8_t b = fz_u8(in);
    return b < 208 ? 0 : (uint8_t) (1 + b % 3);
}

static u128 gcd128(u128 a, u128 b)
{
    while (b) {
        u128 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* ---- F: exhaustive faraid check ---- */
static void faraid_all(void)
{
    for (int hu = 0; hu < 2; hu++)
        for (uint32_t w = 0; w <= 4; w++) {
            if (hu && w) continue;
            for (int fa = 0; fa < 2; fa++)
                for (int mo = 0; mo < 2; mo++)
                    for (uint32_t so = 0; so <= 8; so++)
                        for (uint32_t da = 0; da <= 8; da++) {
                            est_faraid_heirs_t h = {hu != 0, w, fa != 0, mo != 0, so, da, false};
                            uint32_t n = (uint32_t) hu + w + (uint32_t) fa + (uint32_t) mo + so + da;
                            est_faraid_t f;
                            est_status_t st = est_faraid(&h, &f);
                            if (n == 0) {
                                if (st == EST_OK) abort();
                                continue;
                            }
                            if (st != EST_OK) abort();
                            /* the class shares sum to exactly 1 */
                            u128 a = 0, b = 1;
                            for (int c = 0; c < EST_FA_N; c++) {
                                const est_frac_t *s = &f.share[c];
                                if (s->den == 0 || s->num > s->den) abort();
                                if (gcd128(s->num, s->den) != 1 && s->num) abort(); /* lowest */
                                u128 l = b / gcd128(b, s->den) * s->den;
                                a = a * (l / b) + (u128) s->num * (l / s->den);
                                b = l;
                            }
                            if (a != b) abort();
                            /* a son takes twice a daughter */
                            if (so && da) {
                                const est_frac_t *S = &f.share[EST_FA_SONS],
                                                 *D = &f.share[EST_FA_DAUGHTERS];
                                if ((u128) S->num * D->den * da != (u128) 2 * D->num * S->den * so)
                                    abort();
                            }
                            /* the manifest builder accepts every estate with no rest */
                            est_t x;
                            est_cfg_t c = C;
                            if (est_init(&x, &c, verify, 0) != EST_OK) abort();
                            uint32_t id[64];
                            for (uint32_t i = 0; i < 64; i++) id[i] = 1000 + i;
                            est_status_t ss = est_set_faraid(&x, &h, id);
                            if (f.share[EST_FA_REST].num) {
                                if (ss == EST_OK) abort();
                            } else if (n <= EST_MAX_HEIRS) {
                                if (ss != EST_OK) abort();
                            } else if (ss == EST_OK) {
                                abort();
                            }
                        }
        }
}

static void setup(void)
{
    memset(&C, 0, sizeof C);
    key(C.owner, 1);
    for (uint32_t i = 0; i < 3; i++) key(C.guardian[i], (uint8_t) (10 + i));
    C.n_guardians = 3;
    C.k = 2;
    key(C.attestor[0], 20);
    key(C.attestor[1], 21);
    C.n_attestors = 2;
    key(C.court, 30);
    key(C.gateway, 31);
    C.wait_ticks = 10;
    C.release_ticks = 20;
    if (est_init(&E, &C, verify, 0) != EST_OK) abort();
    est_forced_init(&T);
    g_in = g_out = g_stream_T = 0;
}

static bool phase_ok(est_phase_t from, est_phase_t to, uint8_t op)
{
    if (from == to) return true;
    switch (op) {
    case 0: return from == EST_ALIVE && to == EST_REPORTED;
    case 2: return from == EST_REPORTED && to == EST_CONFIRMED;
    case 3: return from == EST_CONFIRMED && to == EST_RULED;
    case 4: return (from == EST_RULED || from == EST_CONFIRMED) && to == EST_DISTRIBUTING;
    case 5: return from == EST_DISTRIBUTING && to == EST_CLOSED;
    case 6: return (from == EST_REPORTED || from == EST_CONFIRMED) && to == EST_ALIVE;
    default: return false;
    }
}

/* the stored manifest equals the given fractions and sums to exactly 1 */
static void check_manifest(uint32_t n, const uint64_t num[], const uint64_t den[])
{
    if (E.n != n || E.den == 0) abort();
    u128 s = 0;
    for (uint32_t i = 0; i < n; i++) {
        s += E.num[i];
        if ((u128) E.num[i] * den[i] != (u128) num[i] * E.den) abort();
    }
    if (s != E.den) abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static bool once;
    if (!once) {
        once = true;
        setup();
        faraid_all();
    }
    if (size > 4096) return 0;
    fz_in in;
    fz_init(&in, data, size);
    setup();
    static est_t snap;
    uint64_t tick = 0;

    while (in.n) {
        uint8_t ob = fz_u8(&in), op = ob % 16u;
        if (op == 6 && ob < 224) op = 11; /* a veto is rare */
        if (op == 7 && ob < 200) op = 12; /* so is a fraud finding */
        g_ops++;
        tick += fz_u8(&in);
        snap = E;
        int st = 0;
        uint8_t sig[8];

        if (op == 0) { /* REPORT */
            uint32_t a = fz_u8(&in) % 3;
            uint64_t bond = fz_edge64(&in);
            uint8_t md = sig_mode(&in);
            bool ok = sign_as(C.attestor[a % 2], EST_MSG_REPORT, bond, md, sig);
            st = est_report(&E, a, bond, tick, sig, 8);
            if (st == EST_OK && (!ok || a >= 2 || bond == 0 || snap.phase != EST_ALIVE ||
                                 E.bond_state != EST_BOND_HELD || E.reported != tick))
                abort();
        } else if (op == 1) { /* APPROVE */
            uint32_t g = fz_u8(&in) % 4;
            uint8_t md = sig_mode(&in);
            bool ok = sign_as(C.guardian[g % 3], EST_MSG_APPROVE, 0, md, sig);
            st = est_approve(&E, g, sig, 8);
            if (st == EST_OK && (!ok || g >= 3 || snap.phase != EST_REPORTED)) abort();
        } else if (op == 2) { /* CONFIRM */
            st = est_confirm(&E, tick);
            if (st == EST_OK) {
                if (__builtin_popcount(snap.approved) < (int) C.k) abort();
                if (tick < snap.reported + C.wait_ticks || snap.reported + C.wait_ticks < snap.reported)
                    abort();
            }
        } else if (op == 3) { /* RULE */
            uint8_t md = sig_mode(&in);
            bool ok = sign_as(C.gateway, EST_MSG_RULING, 0, md, sig);
            st = est_rule(&E, sig, 8);
            if (st == EST_OK && (!ok || snap.enclave == EST_COMMON || snap.enclave == EST_NONE))
                abort();
        } else if (op == 4) { /* BEGIN */
            st = est_begin(&E);
            if (st == EST_OK && snap.phase == EST_CONFIRMED && snap.enclave != EST_COMMON) abort();
        } else if (op == 5) { /* CLOSE */
            st = est_close(&E);
            if (st == EST_OK && snap.bond_state == EST_BOND_HELD &&
                E.bond_state != EST_BOND_REFUNDED)
                abort();
        } else if (op == 6) { /* VETO */
            uint8_t md = sig_mode(&in);
            bool ok = sign_as(C.owner, EST_MSG_VETO, 0, md, sig);
            st = est_veto(&E, sig, 8);
            if (st == EST_OK) {
                if (!ok || E.epoch != snap.epoch + 1 || E.approved) abort();
                if (snap.bond_state == EST_BOND_HELD && E.bond_state != EST_BOND_REFUNDED) abort();
                if (snap.bond_state == EST_BOND_FORFEITED && E.bond_state != EST_BOND_FORFEITED)
                    abort();
            }
        } else if (op == 7) { /* FRAUD */
            uint8_t w = fz_u8(&in);
            uint32_t who = (w & 1) ? EST_COURT : (uint32_t) (w >> 1) % 4;
            uint8_t md = sig_mode(&in);
            bool ok = sign_as(who == EST_COURT ? C.court : C.guardian[who % 3], EST_MSG_FRAUD, 0,
                              md, sig);
            st = est_fraud_finding(&E, who, sig, 8);
            if (st == EST_OK && (!ok || (who != EST_COURT && who >= 3) ||
                                 snap.bond_state != EST_BOND_HELD ||
                                 E.bond_state != EST_BOND_FORFEITED))
                abort();
        } else if (op == 8) { /* SET_MANIFEST */
            uint32_t n = 1 + fz_u8(&in) % EST_MAX_HEIRS;
            uint8_t f = fz_u8(&in);
            est_enclave_t enc = (est_enclave_t) (1 + f % 3);
            uint64_t num[EST_MAX_HEIRS], den[EST_MAX_HEIRS];
            uint32_t id[EST_MAX_HEIRS];
            /* exact by construction: parts of a common D, each over a
             * multiple-of-D denominator (lowest terms or not) */
            uint64_t D = 1 + fz_u8(&in), left = D;
            for (uint32_t i = 0; i < n; i++) {
                uint64_t p = i + 1 == n ? left : (left ? fz_u8(&in) % (left + 1) : 0);
                left -= p;
                uint64_t k = 1 + fz_u8(&in) % 4;
                num[i] = p * k;
                den[i] = D * k;
                id[i] = 500 + i;
            }
            if ((f & 0xF0) == 0xF0) num[fz_u8(&in) % n] += 1; /* broken sum */
            if ((f & 0xF0) == 0xE0) {                       /* edge fractions */
                uint32_t i = fz_u8(&in) % n;
                num[i] = fz_edge64(&in);
                den[i] = fz_edge64(&in);
            }
            st = est_set_manifest(&E, enc, n, id, num, den);
            if (st == EST_OK) {
                if (enc == EST_COMMON ? snap.phase != EST_ALIVE : snap.phase > EST_CONFIRMED)
                    abort();
                check_manifest(n, num, den);
                for (uint32_t i = 0; i < n; i++)
                    if (E.contested[i]) abort();
            }
        } else if (op == 9) { /* SET_FORCED */
            uint8_t f = fz_u8(&in);
            if (f & 1) { /* register a row */
                est_forced_row_t row;
                memset(&row, 0, sizeof row);
                row.jur = EST_JUR('A', 'A' + (f >> 1) % 8);
                row.n_steps = 1 + fz_u8(&in) % EST_FORCED_STEPS;
                for (uint32_t i = 0; i < row.n_steps; i++) {
                    uint8_t b = fz_u8(&in);
                    row.reserve[i] = b >= 0xF8 ? (est_frac_t){fz_edge64(&in), fz_edge64(&in)}
                                               : (est_frac_t){b % 9, 1 + (b >> 3) % 9};
                }
                (void) est_forced_register(&T, &row);
            }
            uint16_t jur = (f & 2) ? EST_JUR('F', 'R') : EST_JUR('A', 'A' + (f >> 2) % 8);
            uint32_t nc = fz_u8(&in) % 6, ng = fz_u8(&in) % 6;
            uint64_t W = (fz_u8(&in) & 1) ? (uint64_t) 1 + fz_u8(&in) : fz_edge64(&in);
            uint64_t gift[EST_MAX_HEIRS], A = 0;
            uint32_t cid[EST_MAX_HEIRS], gid[EST_MAX_HEIRS];
            for (uint32_t j = 0; j < ng; j++) {
                uint64_t room = W > A ? W - A : 0;
                uint64_t rnd = fz_u64(&in);
                gift[j] = (j + 1 == ng && nc == 0) ? room
                          : room == UINT64_MAX     ? rnd
                          : room                   ? rnd % (room + 1)
                                                   : 0;
                A += gift[j];
                gid[j] = 700 + j;
            }
            for (uint32_t i = 0; i < nc; i++) cid[i] = 600 + i;
            const est_forced_row_t *row = est_forced_find(&T, jur);
            st = est_set_forced(&E, &T, jur, nc, cid, ng, gid, gift, W);
            if (st == EST_OK) {
                if (!row || E.enclave != EST_FORCED || E.n != nc + ng) abort();
                u128 cs = 0;
                for (uint32_t i = 0; i < nc; i++) {
                    if (E.num[i] != E.num[0]) abort(); /* children equal */
                    cs += E.num[i];
                }
                if (nc) {
                    const est_frac_t *r = &row->reserve[nc - 1 < row->n_steps ? nc - 1
                                                                              : row->n_steps - 1];
                    if (cs * r->den < (u128) r->num * E.den) abort(); /* reserve honoured */
                }
                for (uint32_t j = 0; j < ng; j++) /* no gift above the will's */
                    if ((u128) E.num[nc + j] * W > (u128) gift[j] * E.den) abort();
                u128 s = 0;
                for (uint32_t i = 0; i < E.n; i++) s += E.num[i];
                if (s != E.den) abort();
            }
        } else if (op == 10) { /* SET_FARAID */
            uint8_t f = fz_u8(&in), g = fz_u8(&in);
            est_faraid_heirs_t h = {(f & 1) != 0, (f >> 1) % 6,       (f & 0x10) != 0,
                                    (f & 0x20) != 0, g % 8,            (g >> 3) % 8,
                                    (f & 0xC0) == 0xC0};
            uint32_t id[EST_MAX_HEIRS];
            for (uint32_t i = 0; i < EST_MAX_HEIRS; i++) id[i] = 800 + i;
            st = est_set_faraid(&E, &h, id);
            if (st == EST_OK) {
                if (h.others || (h.husband && h.wives) || snap.phase > EST_CONFIRMED) abort();
                u128 s = 0;
                for (uint32_t i = 0; i < E.n; i++) s += E.num[i];
                if (s != E.den || E.enclave != EST_FARAID) abort();
            }
        } else if (op == 11 || op == 12) { /* DISTRIBUTE / STREAM */
            uint64_t amt = (fz_u8(&in) & 1) ? fz_u32(&in) : fz_edge64(&in);
            uint64_t out[EST_MAX_HEIRS] = {0}, esc = 0;
            st = op == 11 ? est_distribute(&E, amt, out, &esc) : est_stream(&E, amt, out, &esc);
            if (st == EST_OK) {
                u128 so = 0;
                for (uint32_t i = 0; i < E.n; i++) so += out[i];
                if (so + esc != (op == 11 ? (u128) amt : so + esc)) abort();
                for (uint32_t i = 0; i < E.n; i++) {
                    uint64_t part = E.contested[i] ? E.escrow[i] - snap.escrow[i] : out[i];
                    if (E.contested[i] && out[i]) abort();
                    if (op == 11) {
                        u128 fl = (u128) amt * E.num[i] / E.den;
                        if (part != fl && part != fl + 1) abort();
                    } else {
                        u128 tgt = (g_stream_T + amt) * E.num[i] / E.den;
                        if (E.paid[i] != (uint64_t) tgt || part != E.paid[i] - snap.paid[i]) abort();
                    }
                }
                g_in += amt;
                g_out += so;
                if (op == 12) {
                    g_stream_T += amt;
                    if (E.stream_total != g_stream_T) abort();
                    if (est_stream_held(&E) >= (E.n ? E.n : 1)) abort();
                }
                if (op == 11 && snap.phase != EST_DISTRIBUTING) abort();
            }
        } else if (op == 13) { /* CONTEST */
            uint32_t h = fz_u8(&in) % (EST_MAX_HEIRS + 1);
            uint8_t w = fz_u8(&in);
            uint32_t who = (w & 1) ? EST_COURT : (uint32_t) (w >> 1) % 4;
            uint8_t md = sig_mode(&in);
            bool ok = sign_as(who == EST_COURT ? C.court : C.guardian[who % 3], EST_MSG_CONTEST,
                              est_contest_value(&E, h), md, sig);
            st = est_contest(&E, h, who, sig, 8);
            if (st == EST_OK && (!ok || h >= snap.n || snap.contested[h] ||
                                 snap.phase == EST_CLOSED || (who != EST_COURT && who >= 3)))
                abort();
        } else if (op == 14) { /* RESOLVE */
            uint32_t h = fz_u8(&in) % (EST_MAX_HEIRS + 1);
            uint8_t md = sig_mode(&in);
            bool ok = sign_as(C.court, EST_MSG_RESOLVE, est_contest_value(&E, h), md, sig);
            uint64_t rel = 0;
            st = est_resolve(&E, h, sig, 8, &rel);
            if (st == EST_OK) {
                if (!ok || !snap.contested[h] || rel != snap.escrow[h] || E.escrow[h]) abort();
                g_out += rel;
            }
        } else { /* KEY_RELEASE (pure) */
            uint64_t t = (fz_u8(&in) & 1) ? tick : fz_edge64(&in);
            bool r = est_key_release_ok(&E, t);
            bool want = (E.phase == EST_DISTRIBUTING || E.phase == EST_CLOSED) &&
                        E.confirmed + C.release_ticks >= E.confirmed &&
                        t >= E.confirmed + C.release_ticks;
            if (r != want) abort();
            st = EST_OK;
        }

        if (st == EST_OK) g_okc[op]++;
        if (getenv("FZ_TRACE")) fprintf(stderr, "op %u st %d phase %d\n", op, st, E.phase);
        if (st != EST_OK && memcmp(&snap, &E, sizeof E)) abort(); /* E-REF (EST_DUP too) */
        if (!phase_ok(snap.phase, E.phase, op)) abort();          /* E1 */
        u128 esc = 0;
        for (uint32_t i = 0; i < EST_MAX_HEIRS; i++) esc += E.escrow[i];
        if (esc != E.escrow_total) abort();                               /* E-CONS */
        if (g_in != g_out + E.escrow_total + est_stream_held(&E)) abort(); /* E-CONS */
        if (E.enclave != EST_NONE) {
            u128 s = 0;
            for (uint32_t i = 0; i < E.n; i++) s += E.num[i];
            if (s != E.den) abort(); /* E2: always exactly 1 */
        }
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) {
        fprintf(stderr, "fuzz_econ_estate: %llu ops checked\n", g_ops);
        for (int i = 0; i < 20; i++)
            if (g_okc[i]) fprintf(stderr, "  op %d ok %llu\n", i, g_okc[i]);
    }
}
