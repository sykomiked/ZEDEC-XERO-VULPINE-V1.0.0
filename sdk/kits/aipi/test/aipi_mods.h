/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aipi_mods.h — test and demo fixtures for the AIPI kit: Wasm modules
 * assembled in C (z-wasm's wasm_build.h), in-process nodes standing in for
 * the Vinea mesh, and a libzxv budget. Header-only, test and demo use. */
#ifndef AIPI_MODS_H
#define AIPI_MODS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aipi_zwasm.h"
#include "wasm_build.h"

/* ----- modules -------------------------------------------------------------
 * "halves":   half(n) -> n / 2                       (entry)
 *             half_ok(n, r) -> 2r == n || 2r + 1 == n (checker: the inverse)
 *             spin(n) -> loops for ever               (fuel test)
 * "factors":  smallest(n) -> least factor >= 2 (n if prime or < 4)
 *             is_composite(n) -> smallest(n) != n     (entry)
 *             factor_of(n, r) -> smallest(n)          (certifier)
 *             composite_ok(n, r, c) -> r == 1 && 1 < c < n && n % c == 0
 * "floaty":   half(n) via f64: refused by the Z-Wasm validator */
static inline void mod_halves(wb *m)
{
    static const uint8_t half[] = {0x20, 0x00, 0x42, 0x02, 0x7F};
    static const uint8_t half_ok[] = {0x20, 0x01, 0x42, 0x02, 0x7E, 0x20, 0x00, 0x51,
                                      0x20, 0x01, 0x42, 0x02, 0x7E, 0x42, 0x01, 0x7C,
                                      0x20, 0x00, 0x51, 0x72};
    static const uint8_t spin[] = {0x03, 0x40, 0x0C, 0x00, 0x0B, 0x42, 0x00};
    wt t;
    memset(&t, 0, sizeof t);
    t.types[0] = "I:I";
    t.types[1] = "II:i";
    t.ntypes = 2;
    t.funcs[0].type = 0;
    t.funcs[0].code = half;
    t.funcs[0].code_len = sizeof half;
    t.funcs[1].type = 1;
    t.funcs[1].code = half_ok;
    t.funcs[1].code_len = sizeof half_ok;
    t.funcs[2].type = 0;
    t.funcs[2].code = spin;
    t.funcs[2].code_len = sizeof spin;
    t.nfuncs = 3;
    t.exports[0] = "half";
    t.exports[1] = "half_ok";
    t.exports[2] = "spin";
    t.nexports = 3;
    t.datacount = -1;
    wb_reset(m);
    wt_build(m, &t);
}

static inline void mod_factors(wb *m)
{
    static const uint8_t smallest[] = {
        0x42, 0x02, 0x21, 0x01,                         /* f = 2                 */
        0x03, 0x40,                                     /* loop                  */
        0x20, 0x01, 0x20, 0x01, 0x7E, 0x20, 0x00, 0x55, /*   f*f > n             */
        0x04, 0x40, 0x20, 0x00, 0x0F, 0x0B,             /*   if: return n        */
        0x20, 0x00, 0x20, 0x01, 0x81, 0x50,             /*   n % f == 0          */
        0x04, 0x40, 0x20, 0x01, 0x0F, 0x0B,             /*   if: return f        */
        0x20, 0x01, 0x42, 0x01, 0x7C, 0x21, 0x01,       /*   f = f + 1           */
        0x0C, 0x00, 0x0B,                               /*   br 0; end           */
        0x42, 0x00};
    static const uint8_t is_composite[] = {0x20, 0x00, 0x10, 0x00, 0x20, 0x00, 0x52, 0xAD};
    static const uint8_t factor_of[] = {0x20, 0x00, 0x10, 0x00};
    static const uint8_t composite_ok[] = {
        0x20, 0x01, 0x42, 0x01, 0x51,       /* r == 1      */
        0x20, 0x02, 0x42, 0x01, 0x55, 0x71, /* && c > 1    */
        0x20, 0x02, 0x20, 0x00, 0x53, 0x71, /* && c < n    */
        0x20, 0x00, 0x20, 0x02, 0x81, 0x50, /* n % c == 0 (c == 0 traps: not accepted) */
        0x71};
    wt t;
    memset(&t, 0, sizeof t);
    t.types[0] = "I:I";
    t.types[1] = "II:I";
    t.types[2] = "III:i";
    t.ntypes = 3;
    t.funcs[0].type = 0;
    t.funcs[0].locals = "I";
    t.funcs[0].code = smallest;
    t.funcs[0].code_len = sizeof smallest;
    t.funcs[1].type = 0;
    t.funcs[1].code = is_composite;
    t.funcs[1].code_len = sizeof is_composite;
    t.funcs[2].type = 1;
    t.funcs[2].code = factor_of;
    t.funcs[2].code_len = sizeof factor_of;
    t.funcs[3].type = 2;
    t.funcs[3].code = composite_ok;
    t.funcs[3].code_len = sizeof composite_ok;
    t.nfuncs = 4;
    t.exports[0] = "smallest";
    t.exports[1] = "is_composite";
    t.exports[2] = "factor_of";
    t.exports[3] = "composite_ok";
    t.nexports = 4;
    t.datacount = -1;
    wb_reset(m);
    wt_build(m, &t);
}

static inline void mod_floaty(wb *m)
{
    static const uint8_t half[] = {0x20, 0x00, 0xB9, 0xB0};
    wt t;
    memset(&t, 0, sizeof t);
    t.types[0] = "I:I";
    t.ntypes = 1;
    t.funcs[0].type = 0;
    t.funcs[0].code = half;
    t.funcs[0].code_len = sizeof half;
    t.nfuncs = 1;
    t.exports[0] = "half";
    t.nexports = 1;
    t.datacount = -1;
    wb_reset(m);
    wt_build(m, &t);
}

static const char HALVES_ZC[] = "fn half(n: int[0..1000000]) -> int[0..500000];\n";

/* ----- libzxv budget -------------------------------------------------------- */
typedef struct {
    zxv_ctx *ctx;
    zxv_cap *root, *budget_cap;
    zxv_rmag *rm;
    uint32_t bud;
} zbudget_t;

static inline void zbudget_init(zbudget_t *b)
{
    if (zxv_ctx_create(&b->ctx) || zxv_cap_create(b->ctx, 1, NULL, ZXV_CAP_ALL, &b->root) ||
        zxv_cap_attenuate(b->root, ZXV_CAP_BUDGET, &b->budget_cap) ||
        zxv_rmag_create_cap(b->root, &b->rm) ||
        zxv_rmag_budget(b->root, b->rm, "aipi", ZXV_RES_COMPUTE_CYCLES, INT64_C(1) << 40, 1, 1,
                        &b->bud)) {
        fprintf(stderr, "libzxv setup failed\n");
        exit(2);
    }
}

static inline void zbudget_fini(zbudget_t *b)
{
    zxv_rmag_destroy(b->rm);
    zxv_cap_destroy(b->budget_cap);
    zxv_cap_destroy(b->root);
    zxv_ctx_destroy(b->ctx);
}

/* ----- a node --------------------------------------------------------------- */
#define TN_JOBS 4
typedef struct tnode tnode_t;
struct tnode {
    const char *name;
    ipfsn_memstore_t ms;
    ipfsn_storage_ops_t ops;
    ipfsn_bs_entry_t tab[128];
    ipfsn_bs_t bs;
    ipfsn_pin_t pins[32];
    ipfsn_node_t in;
    uint8_t *scratch, *net, *modbuf, *conbuf;
    aipi_job_t jobs[TN_JOBS];
    aipi_memo_t memo[4];
    aipi_ctx_t a;
    aipi_zw *zw;
    /* mesh: the last call broadcast, the peer blocks come from */
    uint8_t call[AIPI_CALL_ENC_MAX];
    uint32_t call_len, broadcasts;
    tnode_t *peer;
    /* economy */
    int64_t balance, escrow;
};

static int tn_want(void *ctx, const uint8_t *cid_bin, uint32_t cid_len, uint8_t *buf, uint32_t cap,
                   uint32_t *len)
{
    tnode_t *n = (tnode_t *) ctx;
    ipfsn_cid_t cid;
    if (!n->peer || ipfsn_cid_decode_exact(cid_bin, cid_len, &cid) != IPFSN_OK) return -1;
    return ipfsn_serve_block(&n->peer->in, &cid, IPFSN_WIRE_PLAIN, buf, cap, len) == IPFSN_OK ? 0
                                                                                              : -1;
}

static int tn_broadcast(void *ctx, const ipfsn_cid_t *call, const uint8_t *rec, uint32_t len,
                        uint64_t bounty, uint32_t want)
{
    tnode_t *n = (tnode_t *) ctx;
    (void) call;
    (void) bounty;
    (void) want;
    memcpy(n->call, rec, len);
    n->call_len = len;
    n->broadcasts++;
    return 0;
}

static int tn_reserve(void *ctx, uint64_t amt)
{
    tnode_t *n = (tnode_t *) ctx;
    if ((int64_t) amt > n->balance) return -1;
    n->balance -= (int64_t) amt;
    n->escrow += (int64_t) amt;
    return 0;
}

/* In a real host this is a Vinea PAY receipt to the executor (vna_econ). */
static int64_t tn_paid_total;
static void tn_pay(void *ctx, const uint8_t ex[32], uint64_t amt)
{
    tnode_t *n = (tnode_t *) ctx;
    (void) ex;
    n->escrow -= (int64_t) amt;
    tn_paid_total += (int64_t) amt;
}

static void tn_refund(void *ctx, uint64_t amt)
{
    tnode_t *n = (tnode_t *) ctx;
    n->escrow -= (int64_t) amt;
    n->balance += (int64_t) amt;
}

static inline tnode_t *tn_new(const char *name, uint8_t id, zbudget_t *b)
{
    tnode_t *n = calloc(1, sizeof *n);
    n->name = name;
    n->ms.cap = 4u << 20;
    n->ms.buf = malloc(n->ms.cap);
    n->scratch = malloc(IPFSN_SCRATCH_MIN);
    n->net = malloc(IPFSN_BLOCK_MAX + 256);
    n->modbuf = malloc(AIPI_MODULE_MAX);
    n->conbuf = malloc(AIPI_CONTRACT_MAX);
    ipfsn_memstore_ops(&n->ms, &n->ops);
    ipfsn_bs_init(&n->bs, &n->ops, n->tab, 128, n->scratch, IPFSN_SCRATCH_MIN);
    {
        uint8_t key[IPFSN_KEY_LEN];
        memset(key, id, sizeof key);
        ipfsn_bs_set_key(&n->bs, key);
    }
    ipfsn_node_init(&n->in, &n->bs, n->pins, 32, n->net, IPFSN_BLOCK_MAX + 256);
    n->in.peer.ctx = n;
    n->in.peer.want = tn_want;
    n->in.peer.wire = IPFSN_WIRE_PLAIN;
    aipi_init(&n->a, &n->in, n->jobs, TN_JOBS, n->memo, 4, n->modbuf, n->conbuf);
    memset(n->a.self, id, 32);
    n->a.online = true;
    if (aipi_zw_create(b->budget_cap, b->rm, b->bud, &n->zw) != AIPI_OK) {
        fprintf(stderr, "aipi_zw_create failed\n");
        exit(2);
    }
    aipi_zw_ops(n->zw, &n->a.exec);
    n->a.mesh.ctx = n;
    n->a.mesh.broadcast = tn_broadcast;
    n->a.mesh.reserve = tn_reserve;
    n->a.mesh.pay = tn_pay;
    n->a.mesh.refund = tn_refund;
    return n;
}

static inline void tn_free(tnode_t *n)
{
    aipi_zw_destroy(n->zw);
    free(n->ms.buf);
    free(n->scratch);
    free(n->net);
    free(n->modbuf);
    free(n->conbuf);
    free(n);
}

static inline const uint8_t *i64in(int64_t v, uint8_t buf[8])
{
    uint32_t len;
    aipi_zw_pack(&v, 1, buf, 8, &len);
    return buf;
}

static inline int64_t i64out(const uint8_t *p)
{
    int64_t v;
    uint32_t n;
    if (aipi_zw_unpack(p, 8, &v, &n) != AIPI_OK) return INT64_MIN;
    return v;
}

#endif /* AIPI_MODS_H */
