/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_aipi.c — AIPI (aipi/aipi.c): the wire decoders for function
 * descriptors, call records, claims and CID text, and the EXECUTE / submit /
 * escrow state machine driven by hostile claims, a lying peer and a
 * tampered local disk.
 *
 * The input is an op stream run against one requester node R, a provider
 * node P (R's peer, optionally lying) and three honest executors E0..E2.
 * Six functions are published: F0 isqrt CHECKER, F1 composite CERT, F2
 * smallest_factor QUORUM 3-of-2, F3 isqrt LOCAL, F4 isqrt private (LOCAL
 * privacy, R only), F5 isqrt CHECKER whose module and contract only P has.
 *
 * Invariants checked after every op:
 *   I1  decoders: anything aipi_fn_decode / aipi_call_decode /
 *       aipi_claim_decode / aipi_cid_from_text('z') accepts re-encodes to the
 *       same bytes (one function, one CID); aipi_fn_decode output passes
 *       aipi_fn_check; pointers returned stay inside the input
 *   I2  verified EXECUTE: every module/contract handed to the sandbox (run,
 *       check) hashes to the CID the descriptor names (A5); every byte
 *       aipi_get returns hashes to the CID asked for
 *   I3  A1: no call record is broadcast for a private function or a private
 *       input, and the peer is never asked for a CID R holds privately
 *   I4  escrow (A2): balance + escrow + sum(paid) == the starting balance;
 *       escrow == sum over live jobs of (bounty - paid - refunded); a job that
 *       is no longer PENDING has paid + refunded == bounty; a released job had
 *       nothing left in escrow
 *   I5  payment only for verified work: every executor paid holds an
 *       ACCEPTED slot of that job; for CHECKER/CERT jobs its output is the
 *       true answer; a job is paid at most once (no bounty double-claim)
 *   I6  a DONE job of a checked or local function (F0, F1, F3, F4, F5) holds
 *       the true answer, also when served from the memo cache
 *   I7  a refused submit / execute / release changes no job and no money
 *       (submit: the job up to its claim count; execute: every other job)
 *   I8  a replayed claim (same executor) is refused as DUP or STATE, and a
 *       claim for another call is refused
 *   I9  a PENDING job expires only once now >= now_at_execute + ttl (exact,
 *       saturating at UINT64_MAX: a huge ttl must not wrap into the past) */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aipi.h"
#include "fuzz_in.h"

#define JOBS  4u
#define MEMOS 4u
#define NFN   6u
#define NEXE  3u

/* ------------------------------------------------------------ fake sandbox */
static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static void wr64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static uint64_t isqrt64(uint64_t n)
{
    uint64_t r = 0;
    for (int b = 31; b >= 0; b--) {
        uint64_t t = r | ((uint64_t) 1 << b);
        if (t * t <= n) r = t;
    }
    return r;
}
static uint64_t least_factor(uint64_t n)
{
    if (n < 2) return n;
    for (uint64_t f = 2; f <= 2000u && f * f <= n; f++)
        if (n % f == 0) return f;
    return n; /* bounded: "prime enough" for a fuzz oracle */
}

/* The honest answer of the entry, or false when the fake sandbox fails. */
static bool truth(const char *entry, const uint8_t *in, uint32_t in_len, uint64_t *r)
{
    if (in_len != 8) return false;
    uint64_t n = rd64(in);
    if (!strcmp(entry, "isqrt"))
        *r = isqrt64(n);
    else if (!strcmp(entry, "smallest_factor"))
        *r = least_factor(n);
    else if (!strcmp(entry, "composite"))
        *r = (n > 3 && least_factor(n) != n);
    else
        return false;
    return true;
}

static void check_code(const aipi_code_t *code)
{
    ipfsn_cid_t c;
    if (!code || !code->fn || !code->module) abort();
    if (ipfsn_cid_sha256(IPFSN_MC_RAW, code->module, code->module_len, &c) != IPFSN_OK ||
        !ipfsn_cid_equal(&c, &code->fn->module))
        abort(); /* I2 */
    if (code->fn->has_contract) {
        if (!code->contract) abort();
        if (ipfsn_cid_sha256(IPFSN_MC_RAW, code->contract, code->contract_len, &c) != IPFSN_OK ||
            !ipfsn_cid_equal(&c, &code->fn->contract))
            abort();
    } else if (code->contract) {
        abort();
    }
}

static int fx_run(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                  uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    uint64_t r;
    (void) ctx;
    check_code(code);
    if (cap < 8 || !truth(code->fn->entry, in, in_len, &r)) return AIPI_ERR_EXEC;
    wr64(out, r);
    *out_len = 8;
    return AIPI_OK;
}

static int fx_check(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                    const uint8_t *out, uint32_t out_len, const uint8_t *cert, uint32_t cert_len)
{
    (void) ctx;
    check_code(code);
    if (in_len != 8 || out_len != 8) return 0;
    uint64_t n = rd64(in), r = rd64(out);
    if (!strcmp(code->fn->checker, "isqrt_ok"))
        return r < ((uint64_t) 1 << 32) && r * r <= n && (r + 1) * (r + 1) > n;
    if (!strcmp(code->fn->checker, "composite_ok")) {
        if (r != 1 || cert_len != 8) return 0;
        uint64_t f = rd64(cert);
        return f > 1 && f < n && n % f == 0;
    }
    return AIPI_ERR_EXEC;
}

static int fx_certify(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                      const uint8_t *out, uint32_t out_len, uint8_t *cert, uint32_t cap,
                      uint32_t *cert_len)
{
    (void) ctx;
    (void) out;
    (void) out_len;
    check_code(code);
    if (in_len != 8 || cap < 8) return AIPI_ERR_EXEC;
    wr64(cert, least_factor(rd64(in)));
    *cert_len = 8;
    return AIPI_OK;
}

/* ------------------------------------------------------------ nodes */
typedef struct node node_t;
struct node {
    ipfsn_memstore_t ms;
    ipfsn_storage_ops_t ops;
    ipfsn_bs_entry_t tab[64];
    ipfsn_bs_t bs;
    ipfsn_pin_t pins[32];
    ipfsn_node_t in;
    uint8_t *scratch, *net, *modbuf, *conbuf;
    aipi_job_t *jobs;
    aipi_memo_t *memo;
    aipi_ctx_t a;
    lpx_qrec_t qrec[8];
    lpx_qlog_t qlog;
    node_t *peer;
    int tamper;
};

static node_t *g_r, *g_p, *g_e[NEXE];

/* the requester's escrow ledger */
static struct {
    unsigned __int128 start, balance, escrow, paid;
    int npay;
    uint8_t pay_to[16][32];
    uint64_t pay_amt[16];
} g_lg;

/* broadcast mailbox */
static uint8_t g_call[AIPI_CALL_ENC_MAX];
static uint32_t g_call_len;
static bool g_cur_private; /* the input_private flag of the execute in flight */
static int g_fail_bcast, g_fail_reserve;

static const uint8_t MOD1[] = "fz-module-1: isqrt smallest_factor composite";
static const uint8_t MOD2[] = "fz-module-2: isqrt (only on the provider)";
static const uint8_t CON2[] = "fn isqrt(n: int) -> int;";
static ipfsn_cid_t g_mod1, g_mod2, g_con2;
static aipi_fn_t g_fn[NFN];
static ipfsn_cid_t g_fcid[NFN];

static int fn_index(const ipfsn_cid_t *cid)
{
    for (uint32_t i = 0; i < NFN; i++)
        if (ipfsn_cid_equal(cid, &g_fcid[i])) return (int) i;
    return -1;
}

static int nd_broadcast(void *ctx, const ipfsn_cid_t *call, const uint8_t *rec, uint32_t len,
                        uint64_t bounty, uint32_t want)
{
    ipfsn_cid_t f, again;
    const uint8_t *in;
    uint32_t in_len;
    (void) ctx;
    (void) bounty;
    (void) want;
    if (aipi_call_decode(rec, len, &f, &in, &in_len) != AIPI_OK) abort();
    if (aipi_call_cid(&f, in, in_len, &again) != AIPI_OK || !ipfsn_cid_equal(&again, call)) abort();
    int k = fn_index(&f);
    if (k < 0 || g_fn[k].privacy == AIPI_PRIV_LOCAL || g_fn[k].verify == AIPI_VERIFY_LOCAL ||
        g_cur_private)
        abort(); /* I3 */
    if (g_fail_bcast) return -1;
    memcpy(g_call, rec, len);
    g_call_len = len;
    return 0;
}

static int lg_reserve(void *ctx, uint64_t amt)
{
    (void) ctx;
    if (g_fail_reserve || amt > g_lg.balance) return -1;
    g_lg.balance -= amt;
    g_lg.escrow += amt;
    return 0;
}
static void lg_pay(void *ctx, const uint8_t ex[32], uint64_t amt)
{
    (void) ctx;
    if (amt > g_lg.escrow || g_lg.npay >= 16) abort();
    g_lg.escrow -= amt;
    g_lg.paid += amt;
    memcpy(g_lg.pay_to[g_lg.npay], ex, 32);
    g_lg.pay_amt[g_lg.npay++] = amt;
}
static void lg_refund(void *ctx, uint64_t amt)
{
    (void) ctx;
    if (amt > g_lg.escrow) abort();
    g_lg.escrow -= amt;
    g_lg.balance += amt;
}

static int nd_want(void *ctx, const uint8_t *cid_bin, uint32_t cid_len, uint8_t *buf, uint32_t cap,
                   uint32_t *len)
{
    node_t *n = (node_t *) ctx;
    ipfsn_cid_t cid;
    if (ipfsn_cid_decode_exact(cid_bin, cid_len, &cid) != IPFSN_OK) abort();
    if (ipfsn_is_private(&n->in, &cid)) abort(); /* I3 */
    if (!n->peer) return -1;
    if (ipfsn_serve_block(&n->peer->in, &cid, IPFSN_WIRE_PLAIN, buf, cap, len) != IPFSN_OK)
        return -1;
    if (n->tamper && *len) buf[(n->tamper - 1) % *len] ^= (uint8_t) (1u << (n->tamper & 7));
    return 0;
}

static node_t *node_alloc(void)
{
    node_t *n = (node_t *) calloc(1, sizeof *n);
    if (!n) abort();
    n->ms.cap = 1u << 20;
    n->ms.buf = (uint8_t *) malloc(n->ms.cap);
    n->scratch = (uint8_t *) malloc(IPFSN_SCRATCH_MIN);
    n->net = (uint8_t *) malloc(IPFSN_BLOCK_MAX + 256);
    n->modbuf = (uint8_t *) malloc(AIPI_MODULE_MAX);
    n->conbuf = (uint8_t *) malloc(AIPI_CONTRACT_MAX);
    n->jobs = (aipi_job_t *) malloc(JOBS * sizeof *n->jobs);
    n->memo = (aipi_memo_t *) malloc(MEMOS * sizeof *n->memo);
    if (!n->ms.buf || !n->scratch || !n->net || !n->modbuf || !n->conbuf || !n->jobs || !n->memo)
        abort();
    return n;
}

static void node_reset(node_t *n, uint8_t id, node_t *peer)
{
    uint8_t key[IPFSN_KEY_LEN];
    n->ms.len = 0;
    memset(n->tab, 0, sizeof n->tab);
    memset(n->pins, 0, sizeof n->pins);
    ipfsn_memstore_ops(&n->ms, &n->ops);
    if (ipfsn_bs_init(&n->bs, &n->ops, n->tab, 64, n->scratch, IPFSN_SCRATCH_MIN) != IPFSN_OK)
        abort();
    memset(key, id, sizeof key);
    ipfsn_bs_set_key(&n->bs, key);
    if (ipfsn_node_init(&n->in, &n->bs, n->pins, 32, n->net, IPFSN_BLOCK_MAX + 256) != IPFSN_OK)
        abort();
    n->peer = peer;
    n->tamper = 0;
    n->in.peer.ctx = n;
    n->in.peer.want = nd_want;
    n->in.peer.wire = IPFSN_WIRE_PLAIN;
    memset(n->jobs, 0xEE, JOBS * sizeof *n->jobs);
    memset(n->memo, 0xEE, MEMOS * sizeof *n->memo);
    if (aipi_init(&n->a, &n->in, n->jobs, JOBS, n->memo, MEMOS, n->modbuf, n->conbuf) != AIPI_OK)
        abort();
    memset(n->a.self, id, 32);
    n->a.online = true;
    n->a.exec.run = fx_run;
    n->a.exec.check = fx_check;
    n->a.exec.certify = fx_certify;
    lpx_qlog_init(&n->qlog, n->qrec, 8);
    n->a.qlog = &n->qlog;
}

static void make_fns(void)
{
    ipfsn_cid_sha256(IPFSN_MC_RAW, MOD1, sizeof MOD1, &g_mod1);
    ipfsn_cid_sha256(IPFSN_MC_RAW, MOD2, sizeof MOD2, &g_mod2);
    ipfsn_cid_sha256(IPFSN_MC_RAW, CON2, sizeof CON2, &g_con2);
    for (uint32_t i = 0; i < NFN; i++) {
        aipi_fn_t *f = &g_fn[i];
        memset(f, 0, sizeof *f);
        f->intent = (uint8_t) (i % AIPI_INTENT_COUNT);
        f->privacy = AIPI_PRIV_PUBLIC;
        f->determinism = AIPI_DET_DETERMINISTIC;
        f->fuel_max = 1000u + i;
        f->module = g_mod1;
        strcpy(f->entry, "isqrt");
        f->verify = AIPI_VERIFY_CHECKER;
        strcpy(f->checker, "isqrt_ok");
    }
    strcpy(g_fn[1].entry, "composite");
    g_fn[1].verify = AIPI_VERIFY_CERT;
    strcpy(g_fn[1].checker, "composite_ok");
    strcpy(g_fn[1].certifier, "factor_of");
    strcpy(g_fn[2].entry, "smallest_factor");
    g_fn[2].verify = AIPI_VERIFY_QUORUM;
    g_fn[2].checker[0] = 0;
    g_fn[2].quorum_n = 3;
    g_fn[2].quorum_k = 2;
    g_fn[3].verify = AIPI_VERIFY_LOCAL;
    g_fn[3].checker[0] = 0;
    g_fn[4].privacy = AIPI_PRIV_LOCAL;
    g_fn[5].module = g_mod2;
    g_fn[5].has_contract = true;
    g_fn[5].contract = g_con2;
    for (uint32_t i = 0; i < NFN; i++)
        if (aipi_fn_cid(&g_fn[i], &g_fcid[i]) != AIPI_OK) abort();
}

static void setup(void)
{
    static bool once;
    ipfsn_cid_t c;
    if (!once) {
        once = true;
        g_r = node_alloc();
        g_p = node_alloc();
        for (uint32_t i = 0; i < NEXE; i++) g_e[i] = node_alloc();
        make_fns();
    }
    node_reset(g_p, 0x50, NULL);
    node_reset(g_r, 0x52, g_p);
    for (uint32_t i = 0; i < NEXE; i++) node_reset(g_e[i], (uint8_t) (0xE0 + i), g_p);
    /* P holds every public blob and function */
    if (aipi_put_blob(&g_p->a, MOD1, sizeof MOD1, IPFSN_VIS_PUBLIC, &c) != AIPI_OK ||
        aipi_put_blob(&g_p->a, MOD2, sizeof MOD2, IPFSN_VIS_PUBLIC, &c) != AIPI_OK ||
        aipi_put_blob(&g_p->a, CON2, sizeof CON2, IPFSN_VIS_PUBLIC, &c) != AIPI_OK)
        abort();
    for (uint32_t i = 0; i < NFN; i++)
        if (i != 4 && aipi_publish_fn(&g_p->a, &g_fn[i], &c) != AIPI_OK) abort();
    /* R holds module 1 and F0..F2, F4 (private); it fetches F3, F5 from P */
    if (aipi_put_blob(&g_r->a, MOD1, sizeof MOD1, IPFSN_VIS_PUBLIC, &c) != AIPI_OK) abort();
    for (uint32_t i = 0; i < NFN; i++)
        if ((i <= 2 || i == 4) && aipi_publish_fn(&g_r->a, &g_fn[i], &c) != AIPI_OK) abort();
    if (!ipfsn_is_private(&g_r->in, &g_fcid[4])) abort();
    g_r->a.mesh.ctx = g_r;
    g_r->a.mesh.broadcast = nd_broadcast;
    g_r->a.mesh.reserve = lg_reserve;
    g_r->a.mesh.pay = lg_pay;
    g_r->a.mesh.refund = lg_refund;
    g_call_len = 0;
    g_fail_bcast = g_fail_reserve = 0;
    g_cur_private = false;
}

/* ------------------------------------------------------------ shadow state */
static int g_job_fn[JOBS]; /* which preset a live job runs, -1 if none */
static bool g_job_paid[JOBS];
static unsigned __int128 g_job_due[JOBS]; /* now + ttl at execute, exact */
static uint32_t g_last_job; /* the newest PENDING job: most claims go to it */

static void check_money(void)
{
    const aipi_ctx_t *a = &g_r->a;
    unsigned __int128 live = 0;
    if (g_lg.balance + g_lg.escrow + g_lg.paid != g_lg.start) abort(); /* I4 */
    for (uint32_t i = 0; i < JOBS; i++) {
        const aipi_job_t *j = &a->jobs[i];
        if (j->status == AIPI_JOB_FREE) continue;
        if ((unsigned __int128) j->paid + j->refunded > j->bounty) abort();
        uint64_t left = j->bounty - j->paid - j->refunded;
        if (j->status != AIPI_JOB_PENDING && left != 0) abort();
        live += left;
        if (j->naccepted + j->nrejected > j->nclaims || j->nclaims > AIPI_CLAIMS_MAX) abort();
    }
    if (live != g_lg.escrow) abort();
}

static void check_done(uint32_t idx)
{
    const aipi_job_t *j = &g_r->a.jobs[idx];
    int k = g_job_fn[idx];
    uint64_t r;
    if (j->status != AIPI_JOB_DONE || k < 0 || k == 2) return;
    if (!truth(g_fn[k].entry, j->in, j->in_len, &r)) abort(); /* only true answers are DONE */
    if (j->out_len != 8 || rd64(j->out) != r) abort();        /* I6 */
}

/* I5: everyone paid by the last op holds an accepted slot; true answers. */
static void check_payments(uint32_t idx, int npay_before)
{
    const aipi_job_t *j = &g_r->a.jobs[idx];
    if (g_lg.npay == npay_before) return;
    if (g_job_paid[idx]) abort(); /* paid twice */
    g_job_paid[idx] = true;
    for (int p = npay_before; p < g_lg.npay; p++) {
        bool ok = false;
        for (uint32_t s = 0; s < j->nclaims; s++)
            if (!memcmp(j->slot[s].executor, g_lg.pay_to[p], 32) &&
                j->slot[s].verdict == AIPI_V_ACCEPTED)
                ok = true;
        if (!ok) abort();
    }
    if (j->status != AIPI_JOB_DONE) abort();
    if (j->fn.verify == AIPI_VERIFY_QUORUM) {
        uint32_t w = 0;
        for (uint32_t s = 0; s < j->nclaims; s++)
            if (j->slot[s].verdict == AIPI_V_ACCEPTED) {
                w++;
                if (j->slot[s].out_len != j->out_len ||
                    memcmp(j->slot[s].out, j->out, j->out_len) != 0)
                    abort();
            }
        if (w < j->fn.quorum_k) abort();
    }
}

/* A job compared only on what is live: slots past nclaims are scratch. */
static bool job_same(const aipi_job_t *a, const aipi_job_t *b)
{
    size_t head = offsetof(aipi_job_t, slot);
    if (a->status == AIPI_JOB_FREE && b->status == AIPI_JOB_FREE) return true; /* contents unused */
    if (memcmp(a, b, head) != 0) return false;
    if (a->nclaims > AIPI_CLAIMS_MAX) abort();
    return memcmp(a->slot, b->slot, a->nclaims * sizeof a->slot[0]) == 0;
}

/* ------------------------------------------------------------ decoders (I1) */
static void fuzz_decoders(fz_in *in)
{
    uint8_t buf[AIPI_CLAIM_ENC_MAX + 8], again[AIPI_CLAIM_ENC_MAX + 8];
    uint8_t kind = fz_u8(in) & 3u;
    uint32_t n = fz_u16(in) % (uint32_t) sizeof buf;
    n = (uint32_t) fz_bytes(in, buf, n);
    uint8_t *b = fz_dup(buf, n);
    if (kind == 0) {
        aipi_fn_t f;
        if (aipi_fn_decode(b, n, &f) == AIPI_OK) {
            if (aipi_fn_check(&f) != AIPI_OK) abort();
            int m = aipi_fn_encode(&f, again, sizeof again);
            if (m < 0 || (uint32_t) m != n || memcmp(again, b, n) != 0) abort();
        }
    } else if (kind == 1) {
        ipfsn_cid_t f;
        const uint8_t *ip;
        uint32_t il;
        if (aipi_call_decode(b, n, &f, &ip, &il) == AIPI_OK) {
            if (ip < b || ip + il > b + n) abort();
            int m = aipi_call_encode(&f, ip, il, again, sizeof again);
            if (m < 0 || (uint32_t) m != n || memcmp(again, b, n) != 0) abort();
        }
    } else if (kind == 2) {
        aipi_claim_t c;
        if (aipi_claim_decode(b, n, &c) == AIPI_OK) {
            if (c.out < b || c.out + c.out_len > b + n || c.cert < b || c.cert + c.cert_len > b + n)
                abort();
            int m = aipi_claim_encode(&c.call, c.executor, c.out, c.out_len, c.cert, c.cert_len,
                                      again, sizeof again);
            if (m < 0 || (uint32_t) m != n || memcmp(again, b, n) != 0) abort();
        }
    } else {
        ipfsn_cid_t c;
        char *s = (char *) b;
        if (n > 0) s[0] = (s[0] & 1) ? 'z' : 'b';
        if (aipi_cid_from_text(s, n, &c) == AIPI_OK) {
            char t[AIPI_TEXT_MAX];
            if (c.version != 1) abort();
            if (s[0] == 'z') {
                int m = aipi_cid_to_text(&c, t, sizeof t);
                if (m < 0 || (uint32_t) m != n || memcmp(t, s, n) != 0) abort();
            }
            uint8_t key[32];
            (void) aipi_provider_key(&c, key);
        }
    }
    free(b);
}

/* ------------------------------------------------------------ claims */
static int make_claim(fz_in *in, uint32_t job, uint8_t *claim, uint32_t cap)
{
    const aipi_job_t *j = &g_r->a.jobs[job];
    uint8_t kind = fz_u8(in) % 8u;
    if (kind >= 6) kind = 0;
    if (kind == 0 && g_call_len) { /* honest executor answers the last broadcast */
        return aipi_serve(&g_e[fz_u8(in) % NEXE]->a, g_call, g_call_len, claim, cap);
    }
    uint8_t ex[32], out[16], cert[16];
    memset(ex, 0, 32);
    ex[0] = fz_u8(in) & 7u; /* a small id space: collisions = replays */
    ex[1] = 0xA5;
    uint32_t ol = fz_u8(in) & 1u ? 8u : fz_u8(in) % 17u;
    fz_bytes(in, out, ol);
    uint32_t cl = fz_u8(in) & 1u ? 8u : 0u;
    fz_bytes(in, cert, cl);
    if (kind == 1 && ol == 8 && j->in_len == 8) { /* the right answer, forged sender */
        uint64_t r = 0;
        if (g_job_fn[job] >= 0) (void) truth(g_fn[g_job_fn[job]].entry, j->in, 8, &r);
        wr64(out, r);
        if (cl) wr64(cert, least_factor(rd64(j->in)));
    }
    ipfsn_cid_t call = j->call_cid;
    if (kind == 2) call.digest[fz_u8(in) & 31u] ^= 1u; /* another call */
    if (call.codec != AIPI_MC_CALL) call.codec = AIPI_MC_CALL;
    int m = aipi_claim_encode(&call, ex, out, ol, cert, cl, claim, cap);
    if (m > 0 && kind == 3) claim[fz_u16(in) % (uint32_t) m] ^= (uint8_t) (1u + fz_u8(in)); /* tamper */
    if (m > 0 && kind == 4) m = (int) (fz_u16(in) % (uint32_t) (m + 1)); /* truncate */
    return m;
}

/* ------------------------------------------------------------ stats */
static unsigned long long g_st_ops, g_st_mesh, g_st_sub, g_st_acc, g_st_pay, g_st_cache, g_st_disp,
    g_st_forbid, g_st_serve;

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS"))
        fprintf(stderr,
                "fuzz_aipi: %llu ops, %llu mesh jobs, %llu claims counted, %llu accepted, %llu "
                "payments, %llu cache hits, %llu disputes, %llu forbidden blocks, %llu claims "
                "served\n",
                g_st_ops, g_st_mesh, g_st_sub, g_st_acc, g_st_pay, g_st_cache, g_st_disp,
                g_st_forbid, g_st_serve);
}

/* ------------------------------------------------------------ the op stream */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static uint8_t claim[AIPI_CLAIM_ENC_MAX], last_claim[AIPI_CLAIM_ENC_MAX];
    static aipi_job_t snap[JOBS];
    static uint8_t gbuf[4096];
    uint32_t last_claim_len = 0, last_claim_job = 0;
    fz_in in;
    uint64_t now = 0;
    if (size > 4096) return 0;
    fz_init(&in, data, size);
    setup();
    memset(&g_lg, 0, sizeof g_lg);
    g_lg.start = g_lg.balance = fz_edge64(&in);
    g_last_job = 0;
    for (uint32_t i = 0; i < JOBS; i++) {
        g_job_fn[i] = -1;
        g_job_paid[i] = false;
    }

    for (int ops = 0; in.n > 0 && ops < 64; ops++) {
        static const uint8_t opmap[16] = {0, 0, 1, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 9, 1};
        uint8_t op = opmap[fz_u8(&in) & 15u];
        int npay = g_lg.npay;
        unsigned __int128 bal = g_lg.balance, esc = g_lg.escrow, paid = g_lg.paid;
        memcpy(snap, g_r->a.jobs, sizeof snap);
        switch (op) {
        case 0: { /* EXECUTE */
            uint32_t k = fz_u8(&in) % NFN, job = 99;
            uint8_t inb[16];
            uint32_t il = (fz_u8(&in) & 7u) ? 8u : fz_u8(&in) % 17u;
            uint8_t vs = fz_u8(&in);
            if (il == 8 && vs < 0xC0u)
                wr64(inb, (uint64_t) (vs & 7u) * 1000003u + 4u); /* a few repeated calls: memo */
            else
                fz_bytes(&in, inb, il);
            bool priv = (fz_u8(&in) & 7u) == 0;
            uint64_t bounty = fz_edge64(&in), ttl = fz_edge64(&in);
            ipfsn_cid_t fc = g_fcid[k];
            if ((fz_u8(&in) & 15u) == 0) fc.digest[fz_u8(&in) & 31u] ^= 0x10; /* unknown fn */
            g_cur_private = priv;
            int r = aipi_execute(&g_r->a, &fc, inb, il, priv, bounty, now, ttl, &job);
            g_cur_private = false;
            if (job != 99 && job >= JOBS) abort();
            if (r == AIPI_OK && g_r->a.jobs[job].status == AIPI_JOB_PENDING) g_last_job = job;
            for (uint32_t i = 0; i < JOBS; i++) {
                if (i == job) continue;
                if (snap[i].status == AIPI_JOB_FREE && g_r->a.jobs[i].status == AIPI_JOB_FREE)
                    continue;
                if (!job_same(&snap[i], &g_r->a.jobs[i])) abort(); /* I7 */
            }
            if (r == AIPI_OK || job != 99) {
                if (snap[job].status != AIPI_JOB_FREE) abort(); /* took a free slot */
                g_job_fn[job] = fn_index(&fc);
                g_job_due[job] = (unsigned __int128) now + ttl;
                if (g_job_due[job] > UINT64_MAX) g_job_due[job] = UINT64_MAX; /* saturates */
                g_job_paid[job] = false;
                if (g_r->a.jobs[job].status == AIPI_JOB_DONE) check_done(job);
                if (g_r->a.jobs[job].route == AIPI_ROUTE_MESH) g_st_mesh++;
                if (g_r->a.jobs[job].route == AIPI_ROUTE_CACHE) g_st_cache++;
            }
            if (r != AIPI_OK) {
                if (g_lg.balance != bal || g_lg.escrow != esc || g_lg.paid != paid) abort();
                if (job != 99 && g_r->a.jobs[job].status != AIPI_JOB_FAILED &&
                    g_r->a.jobs[job].status != AIPI_JOB_FREE)
                    abort();
            }
            break;
        }
        case 1:
        case 2: { /* SUBMIT a claim */
            uint8_t js = fz_u8(&in);
            uint32_t job = js < 0xC0u ? g_last_job : js % JOBS;
            int m = make_claim(&in, job, claim, sizeof claim);
            if (m < 0) break;
            uint8_t v = 0;
            int r = aipi_submit(&g_r->a, job, claim, (uint32_t) m, &v);
            if (r != AIPI_OK) {
                if (!job_same(&snap[job], &g_r->a.jobs[job]) || g_lg.npay != npay ||
                    g_lg.balance != bal || g_lg.escrow != esc)
                    abort(); /* I7 */
            } else {
                memcpy(last_claim, claim, (size_t) m);
                last_claim_len = (uint32_t) m;
                last_claim_job = job;
                g_st_sub++;
                g_st_acc += v == AIPI_V_ACCEPTED;
                g_st_disp += v == AIPI_V_DISPUTED;
                g_st_pay += (unsigned long long) (g_lg.npay - npay);
                check_payments(job, npay);
                check_done(job);
            }
            break;
        }
        case 3: { /* REPLAY the last accepted-for-counting claim (I8) */
            if (!last_claim_len) break;
            uint8_t v = 0;
            int r = aipi_submit(&g_r->a, last_claim_job, last_claim, last_claim_len, &v);
            aipi_claim_t lc;
            bool other = aipi_claim_decode(last_claim, last_claim_len, &lc) != AIPI_OK ||
                         !ipfsn_cid_equal(&lc.call, &g_r->a.jobs[last_claim_job].call_cid);
            /* the slot may have been released and reused by another call */
            if (r != AIPI_ERR_DUP && r != AIPI_ERR_STATE && !(other && r == AIPI_ERR_ARG)) abort();
            if (!job_same(&snap[last_claim_job], &g_r->a.jobs[last_claim_job]) ||
                g_lg.npay != npay)
                abort();
            break;
        }
        case 4: { /* TICK */
            uint64_t step = fz_edge64(&in);
            now = (fz_u8(&in) & 3u) ? now + (step & 0xFFFFu) : step;
            (void) aipi_tick(&g_r->a, now);
#ifndef FZ_AIPI_TOLERATE_TTL_WRAP /* exploration builds only: the known now + ttl wrap */
            for (uint32_t i = 0; i < JOBS; i++)
                if (snap[i].status == AIPI_JOB_PENDING &&
                    g_r->a.jobs[i].status == AIPI_JOB_EXPIRED && now < g_job_due[i])
                    abort(); /* expired before now + ttl */
#endif
            if (g_lg.npay != npay || g_lg.paid != paid) abort(); /* expiry never pays */
            break;
        }
        case 5: { /* RELEASE */
            uint32_t job = fz_u8(&in) % JOBS;
            const aipi_job_t *j = &g_r->a.jobs[job];
            bool pending = j->status == AIPI_JOB_PENDING;
            uint64_t left = j->status == AIPI_JOB_FREE ? 0 : j->bounty - j->paid - j->refunded;
            int r = aipi_release(&g_r->a, job);
            if (pending != (r == AIPI_ERR_STATE)) abort();
            if (r == AIPI_OK) {
                if (left != 0) abort(); /* I4: nothing left in escrow */
                g_job_fn[job] = -1;
                g_job_paid[job] = false;
            } else if (!job_same(&snap[job], &g_r->a.jobs[job])) {
                abort();
            }
            break;
        }
        case 6: { /* knobs */
            uint8_t b = fz_u8(&in);
            g_r->a.online = (b & 1u) == 0;
            g_r->a.prefer_local = (b & 2u) != 0;
            g_r->tamper = (b & 4u) ? 1 + fz_u8(&in) : 0;
            g_fail_bcast = (b & 8u) != 0;
            g_fail_reserve = (b & 16u) != 0;
            break;
        }
        case 7: { /* corrupt one byte of R's disk (A5) */
            if (!g_r->ms.len) break;
            uint32_t off = fz_u32(&in) % (uint32_t) g_r->ms.len;
            g_r->ms.buf[off] ^= (uint8_t) (1u + fz_u8(&in));
            break;
        }
        case 8: { /* GET a known or flipped CID (I2) */
            static const ipfsn_cid_t *pick[3] = {&g_mod1, &g_mod2, &g_con2};
            uint8_t s = fz_u8(&in);
            ipfsn_cid_t c = s < 128 ? *pick[s % 3] : g_fcid[s % NFN];
            if ((fz_u8(&in) & 7u) == 0) c.digest[0] ^= 1u;
            uint32_t len = 0;
            lpx_t tag;
            if (aipi_get(&g_r->a, &c, gbuf, sizeof gbuf, &len, &tag) == AIPI_OK) {
                ipfsn_cid_t h;
                if (len > sizeof gbuf) abort();
                if (ipfsn_cid_sha256(c.codec, gbuf, len, &h) != IPFSN_OK || !ipfsn_cid_equal(&h, &c))
                    abort();
            }
            break;
        }
        default: { /* hostile wire bytes, and a hostile call record to an executor */
            if (fz_u8(&in) & 1u) {
                fuzz_decoders(&in);
            } else {
                uint8_t rec[AIPI_CALL_ENC_MAX];
                uint32_t n = fz_u16(&in) % (uint32_t) sizeof rec;
                if (g_call_len && (fz_u8(&in) & 1u)) { /* mutate a real one */
                    memcpy(rec, g_call, g_call_len);
                    n = g_call_len;
                    rec[fz_u16(&in) % n] ^= (uint8_t) (1u + fz_u8(&in));
                } else {
                    n = (uint32_t) fz_bytes(&in, rec, n);
                }
                uint8_t *rb = fz_dup(rec, n);
                int m = aipi_serve(&g_e[0]->a, rb, n, claim, sizeof claim);
                if (m >= 0) {
                    aipi_claim_t c;
                    g_st_serve++;
                    ipfsn_cid_t f, call;
                    const uint8_t *ip;
                    uint32_t il;
                    if (aipi_claim_decode(claim, (uint32_t) m, &c) != AIPI_OK) abort();
                    if (aipi_call_decode(rb, n, &f, &ip, &il) != AIPI_OK) abort();
                    if (aipi_call_cid(&f, ip, il, &call) != AIPI_OK || !ipfsn_cid_equal(&call, &c.call))
                        abort();
                    int k = fn_index(&f);
                    if (k < 0 || g_fn[k].privacy == AIPI_PRIV_LOCAL || g_fn[k].verify == AIPI_VERIFY_LOCAL)
                        abort();
                }
                free(rb);
            }
            break;
        }
        }
        check_money();
        g_st_ops++;
    }
    g_st_forbid += g_r->a.forbidden;
    return 0;
}
