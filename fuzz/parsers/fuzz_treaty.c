/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_treaty.c — treaty Sutra text and COBOL records (treaty/zxt.c), the
 * runner (treaty/zxt_exec.c) and the COBOL entry ZXV_ENGINE (zxt_cobol.c).
 * src/treaty/fuzz_zxt.c is a grammar generator for the parser alone; this
 * harness takes raw hostile bytes and goes on to run what is accepted.
 *
 * Byte 0 picks the mode:
 *   0  Sutra text: the rest is handed to zxt_parse_sutra
 *   1  record: the rest, padded with spaces / cut to ZXT_REC_LEN, is a record
 *   2  built treaty: the rest builds a treaty that is always valid (1..16
 *      parties, weights that sum to 1, amounts from PIC and 2^59 edges)
 *   3  hand-built AST: the rest is copied raw over a zxt_treaty_t
 *
 * Invariants checked on every input:
 *   P1  a refused parse leaves the output treaty all zero and an err_off
 *       inside the input (zxt.h T2)
 *   P2  an accepted parse satisfies zxt_validate, and both forms re-emit
 *       byte for byte and parse back to an equal AST (T3, T4)
 *   P3  zxt_emit_* on a treaty zxt_validate refuses is refused too
 *   Then every valid treaty is run with zxt_exec:
 *   X1  a refusal gives a result that is all zero except the status (E1)
 *   X2  on success every number fits PIC S9(18) (zxt_exec.h) and the result
 *       record can be written (zxt_emit_result == ZXT_OK)
 *   X3  net[] sums to zero and equals the gross obligations recomputed here;
 *       the legs reproduce net[] exactly; at most n_party-1 legs
 *   X4  NET: balance == deposit, no fees, no dividend. SETL: sum(balance) ==
 *       sum(deposit) - sum(claim) - fees (value only leaves through the
 *       claims and the fee buckets), every balance >= 0, dividends >= 0 and
 *       sum to <= fees
 *   X5  deterministic: the same treaty and seed give the same result bytes
 *   X6  ZXV_ENGINE on the canonical record returns the same status and
 *       writes the ZXR1 result record whenever it reports success */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_in.h"
#include "zxt.h"
#include "zxt_exec.h"

static zxv_engine_t g_ws;
static zxt_result_t g_r1, g_r2;
static uint8_t g_rec[ZXT_REC_LEN], g_rec2[ZXT_REC_LEN];
static char g_txt[ZXT_SUTRA_MAX + 16u];
static char g_res[ZXT_RES_LEN];

static bool g_big; /* FZ_TREATY_TOLERATE_BIG_BALANCE only */

static bool all_zero(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *) p;
    for (size_t i = 0; i < n; i++)
        if (b[i]) return false;
    return true;
}

static void check_exec(const zxt_treaty_t *t, const uint8_t seed[32])
{
    g_big = false;
    int32_t st = zxt_exec(t, &g_ws, seed, &g_r1);
    if (st != g_r1.status) abort();
    int32_t st2 = zxt_exec(t, &g_ws, seed, &g_r2);
    if (st2 != st || memcmp(&g_r1, &g_r2, sizeof g_r1) != 0) abort(); /* X5 */
    if (st != ZXT_OK) {
        zxt_result_t z;
        memset(&z, 0, sizeof z);
        z.status = st;
        if (memcmp(&z, &g_r1, sizeof z) != 0) abort(); /* X1 */
        return;
    }
    const zxt_result_t *r = &g_r1;
    if (r->n_party != t->n_party || r->n_leg > ZXT_MAX_OWES || r->where != 0) abort();
    if (r->n_party > 1 && r->n_leg > r->n_party - 1u) abort();
    /* X2 */
    bool big = false;
    if (!zxt_pic_ok(r->fees) || r->fees < 0) abort();
    for (uint32_t i = 0; i < ZXT_MAX_PARTIES; i++) {
        if (!zxt_pic_ok(r->net[i]) || !zxt_pic_ok(r->dividend[i])) abort();
        if (!zxt_pic_ok(r->balance[i])) big = true;
    }
#ifdef FZ_TREATY_TOLERATE_BIG_BALANCE
    /* exploration builds only: step over the known balance > PIC S9(18)
     * defect so the rest of the state space keeps being searched */
    g_big = big;
#else
    if (big) abort();
#endif
    if (!big && zxt_emit_result(r, (uint8_t *) g_res, ZXT_RES_LEN) != ZXT_OK) abort();
    /* X3 */
    __int128 net[ZXT_MAX_PARTIES] = {0}, lnet[ZXT_MAX_PARTIES] = {0}, s = 0;
    for (uint32_t j = 0; j < t->n_owe; j++) {
        net[t->owe[j].from] -= t->owe[j].amount;
        net[t->owe[j].to] += t->owe[j].amount;
    }
    for (uint32_t k = 0; k < r->n_leg; k++) {
        const zxt_owe_t *l = &r->leg[k];
        if (l->from >= t->n_party || l->to >= t->n_party || l->from == l->to || l->amount <= 0)
            abort();
        lnet[l->from] -= l->amount;
        lnet[l->to] += l->amount;
    }
    for (uint32_t i = 0; i < t->n_party; i++) {
        if (net[i] != r->net[i] || lnet[i] != net[i]) abort();
        s += net[i];
    }
    if (s != 0) abort();
    /* X4 */
    __int128 dep = 0, clm = 0, bal = 0, div = 0;
    for (uint32_t i = 0; i < t->n_party; i++) {
        dep += t->party[i].deposit;
        clm += t->party[i].claim;
        bal += r->balance[i];
        div += r->dividend[i];
        if (r->balance[i] < 0 || r->dividend[i] < 0) abort();
        if (t->op == ZXT_OP_NET && r->balance[i] != t->party[i].deposit) abort();
    }
    if (t->op == ZXT_OP_NET && (r->fees != 0 || div != 0)) abort();
    if (t->op == ZXT_OP_SETL && (bal != dep - clm - r->fees || div > r->fees)) abort();
}

/* X6: the COBOL entry on the canonical record. */
static void check_cobol(const zxt_treaty_t *t)
{
    if (zxt_emit_record(t, g_rec, sizeof g_rec) != ZXT_OK) abort();
    uint8_t seed[32];
    memcpy(seed, g_rec + 4, 32);
    int32_t want = zxt_exec(t, &g_ws, seed, &g_r1);
    memset(g_res, 'Q', sizeof g_res);
    bool big = want == ZXT_OK && g_big;
    int rc = ZXV_ENGINE((const char *) g_rec, g_res);
    if (rc != want) abort();
    if (!big && memcmp(g_res, ZXT_RES_MAGIC, 4) != 0) abort(); /* result record always written */
}

/* P2 plus the run, for a treaty that zxt_validate accepts. */
static void check_valid(const zxt_treaty_t *t, const uint8_t seed[32])
{
    zxt_treaty_t b;
    uint32_t n = 0, off = 1;
    if (zxt_validate(t) != ZXT_OK) abort();
    if (zxt_emit_sutra(t, g_txt, sizeof g_txt, &n) != ZXT_OK || n > ZXT_SUTRA_MAX) abort();
    if (zxt_parse_sutra(g_txt, n, &b, &off) != ZXT_OK || off != 0 || !zxt_equal(t, &b)) abort();
    if (zxt_emit_record(t, g_rec, sizeof g_rec) != ZXT_OK) abort();
    if (zxt_parse_record(g_rec, ZXT_REC_LEN, &b, &off) != ZXT_OK || !zxt_equal(t, &b)) abort();
    if (zxt_emit_record(&b, g_rec2, sizeof g_rec2) != ZXT_OK ||
        memcmp(g_rec, g_rec2, ZXT_REC_LEN) != 0)
        abort();
    /* a too-small sutra buffer is refused with nothing claimed written */
    if (n > 0) {
        uint32_t m = 7;
        if (zxt_emit_sutra(t, g_txt, n - 1u, &m) != ZXT_E_CAP || m != 0) abort();
    }
    check_exec(t, seed);
    check_cobol(t);
}

static int64_t pick_amount(fz_in *in, bool allow_zero)
{
    static const int64_t e[] = {1,
                                2,
                                999,
                                1000000,
                                (int64_t) 1 << 32,
                                ((int64_t) 1 << 59) - 1,
                                (int64_t) 1 << 59,
                                ((int64_t) 1 << 59) + 1,
                                500000000000000000LL,
                                ZXT_PIC18_MAX - 1,
                                ZXT_PIC18_MAX};
    uint8_t sel = fz_u8(in);
    int64_t v;
    if (sel < 0x60u)
        v = e[sel % (sizeof e / sizeof e[0])];
    else if (sel < 0xA0u)
        v = fz_u16(in);
    else
        v = (int64_t) (fz_edge64(in) % (uint64_t) (ZXT_PIC18_MAX + 1));
    if (!allow_zero && v == 0) v = 1;
    return v;
}

static uint64_t gcd(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

/* Mode 2: a valid treaty from the bytes. */
static void build(fz_in *in, zxt_treaty_t *t)
{
    zxt_clear(t);
    memcpy(t->name, "FZ-TREATY", 9);
    memcpy(t->asset, "USD", 3);
    t->op = (fz_u8(in) & 1u) ? ZXT_OP_SETL : ZXT_OP_NET;
    t->n_party = (uint8_t) (1u + fz_u8(in) % ZXT_MAX_PARTIES);
    uint32_t den = 1u + fz_u32(in) % ZXT_WEIGHT_MAX, left = den;
    if (den < t->n_party) den = left = t->n_party;
    for (uint32_t i = 0; i < t->n_party; i++) {
        zxt_party_t *p = &t->party[i];
        p->id[0] = 'P';
        p->id[1] = (char) ('A' + i);
        p->deposit = pick_amount(in, true);
        /* the engine funds at most 2^59 per deposit: keep most runs inside it */
        if (p->deposit > ((int64_t) 1 << 59) && (fz_u8(in) & 3u)) p->deposit = (int64_t) 1 << 59;
        uint8_t c = fz_u8(in);
        p->claim = c < 0x40u ? 0 : c < 0x80u ? p->deposit : (int64_t) ((uint64_t) p->deposit * c / 255u);
        uint32_t num = i + 1u == t->n_party ? left : (left ? fz_u32(in) % (left + 1u) : 0u);
        left -= num;
        uint64_t g = gcd(num, den);
        p->w_num = (uint32_t) (num / g);
        p->w_den = (uint32_t) (den / g);
    }
    if (t->n_party > 1) {
        t->n_owe = (uint8_t) (fz_u8(in) % (ZXT_MAX_OWES + 1u));
        for (uint32_t j = 0; j < t->n_owe; j++) {
            uint8_t f = (uint8_t) (fz_u8(in) % t->n_party);
            uint8_t d = (uint8_t) ((f + 1u + fz_u8(in) % (t->n_party - 1u)) % t->n_party);
            t->owe[j].from = f;
            t->owe[j].to = d;
            t->owe[j].amount = pick_amount(in, false);
            if (t->owe[j].amount > ((int64_t) 1 << 58) && (fz_u8(in) & 3u))
                t->owe[j].amount = (int64_t) 1 << 58;
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > ZXT_SUTRA_MAX + 64u) return 0;
    uint8_t mode = data[0] & 3u;
    const uint8_t *p = data + 1;
    size_t n = size - 1;
    uint8_t seed[32] = {0};
    zxt_treaty_t t;
    uint32_t off = 0;

    if (mode == 0) {
        char *txt = (char *) fz_dup(p, n);
        zxt_status_t st = zxt_parse_sutra(txt, (uint32_t) n, &t, &off);
        if (st == ZXT_OK) {
            uint32_t m = 0;
            if (zxt_emit_sutra(&t, g_txt, sizeof g_txt, &m) != ZXT_OK || m != n ||
                memcmp(g_txt, txt, n) != 0)
                abort(); /* T3 one reading */
            check_valid(&t, seed);
        } else {
            if (st >= 0 || !all_zero(&t, sizeof t) || off > n) abort(); /* P1 */
        }
        free(txt);
    } else if (mode == 1) {
        uint8_t *rec = (uint8_t *) malloc(ZXT_REC_LEN);
        if (!rec) abort();
        memset(rec, ' ', ZXT_REC_LEN);
        memcpy(rec, p, n < ZXT_REC_LEN ? n : ZXT_REC_LEN);
        zxt_status_t st = zxt_parse_record(rec, ZXT_REC_LEN, &t, &off);
        if (st == ZXT_OK) {
            if (zxt_emit_record(&t, g_rec, sizeof g_rec) != ZXT_OK ||
                memcmp(g_rec, rec, ZXT_REC_LEN) != 0)
                abort(); /* T3 one reading */
            memcpy(seed, rec + 4, 32);
            check_valid(&t, seed);
        } else {
            if (st >= 0 || !all_zero(&t, sizeof t) || off > ZXT_REC_LEN) abort(); /* P1 */
        }
        /* the COBOL entry never trusts a record: a refusal is reported in the result */
        memset(g_res, 'Q', sizeof g_res);
        int rc = ZXV_ENGINE((const char *) rec, g_res);
        if (st != ZXT_OK && rc != st) abort();
        if (!(st == ZXT_OK && g_big) && memcmp(g_res, ZXT_RES_MAGIC, 4) != 0) abort();
        free(rec);
    } else if (mode == 2) {
        fz_in in;
        fz_init(&in, p, n);
        fz_bytes(&in, seed, 8);
        build(&in, &t);
        check_valid(&t, seed);
    } else {
        memset(&t, 0, sizeof t);
        memcpy(&t, p, n < sizeof t ? n : sizeof t);
        zxt_status_t vs = zxt_validate(&t);
        uint32_t m = 9;
        if (vs != ZXT_OK) {
            if (zxt_emit_sutra(&t, g_txt, sizeof g_txt, &m) == ZXT_OK || m != 0) abort(); /* P3 */
            if (zxt_emit_record(&t, g_rec, sizeof g_rec) == ZXT_OK) abort();
            if (zxt_exec(&t, &g_ws, seed, &g_r1) != vs) abort();
            zxt_result_t z;
            memset(&z, 0, sizeof z);
            z.status = vs;
            if (memcmp(&z, &g_r1, sizeof z) != 0) abort();
        } else {
            check_valid(&t, seed);
        }
    }
    return 0;
}
