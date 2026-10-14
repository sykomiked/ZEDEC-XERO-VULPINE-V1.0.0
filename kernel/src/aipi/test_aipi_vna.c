/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aipi_vna.c — AIPI over Vinea (aipi_vna.h V1-V5): real Vinea nodes
 * (ML-DSA-87 signed messages) and real AIPI contexts on an in-memory
 * network. A caller asks executors it chose and providers it finds in the
 * DHT; honest claims are verified and paid, a liar's are refused and never
 * paid, strangers and relayed claims never reach AIPI, a quorum pays only
 * the agreeing executors, and escrow always balances. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aipi_vna.h"

static int failures = 0, passes = 0;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            passes++;                                                                              \
        else {                                                                                     \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
        }                                                                                          \
    } while (0)

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
static uint64_t isqrt(uint64_t n)
{
    uint64_t r = 0;
    for (int b = 31; b >= 0; b--) {
        uint64_t t = r | ((uint64_t) 1 << b);
        if (t * t <= n) r = t;
    }
    return r;
}

/* ===== a fake sandbox: isqrt (checker isqrt_ok) and sq (quorum) ========== */
typedef struct {
    int lie;
    int runs;
} fx_t;

static int fx_run(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                  uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    fx_t *x = (fx_t *) ctx;
    x->runs++;
    if (in_len != 8 || cap < 8) return AIPI_ERR_EXEC;
    uint64_t n = rd64(in), r;
    if (!strcmp(code->fn->entry, "isqrt"))
        r = isqrt(n);
    else if (!strcmp(code->fn->entry, "sq"))
        r = n * n;
    else
        return AIPI_ERR_EXEC;
    wr64(out, r + (uint64_t) x->lie);
    *out_len = 8;
    return AIPI_OK;
}

static int fx_check(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                    const uint8_t *out, uint32_t out_len, const uint8_t *cert, uint32_t cert_len)
{
    (void) ctx;
    (void) code;
    (void) cert;
    (void) cert_len;
    if (in_len != 8 || out_len != 8) return 0;
    uint64_t n = rd64(in), r = rd64(out);
    return r * r <= n && (r + 1) * (r + 1) > n;
}

/* ===== one peer: Vinea node + IPFS node + AIPI context + adapter ========= */
#define NP   5
#define POOL 64
typedef struct {
    vna_identity_t idn;
    vna_node_t node;
    vna_contact_t pool[POOL];
    vna_keycache_ent_t kc[16];
    vna_replay_ent_t rp[16];
    uint8_t dd[32][32];
    vna_store_slot_t store[8];
    vna_agreement_t ag;
    vna_usage_t us;
    vna_usage_ent_t ue[8];
    vna_lease_t le[4];
    ipfsn_memstore_t ms;
    ipfsn_storage_ops_t ops;
    ipfsn_bs_entry_t tab[64];
    ipfsn_bs_t bs;
    ipfsn_pin_t pins[16];
    ipfsn_node_t in;
    uint8_t *scratch, *net, *modbuf, *conbuf;
    aipi_job_t jobs[4];
    aipi_memo_t memo[4];
    aipi_ctx_t a;
    aipi_vna_t v;
    fx_t fx;
    /* economy */
    uint64_t balance, escrow, refunded, paid_total;
    int pays;
    vna_id_t paid_to[8];
    uint64_t paid_amt[8];
    int got_paid;
    vna_id_t paid_by;
    uint64_t paid_amount;
    ipfsn_cid_t paid_call;
} peer_t;

static peer_t P[NP];
static uint64_t g_now = 5000000;
static uint8_t *g_obuf;
static vna_outbox_t g_ob;
typedef struct {
    uint32_t from, to, len;
    uint8_t d[VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD];
} pkt_t;
static pkt_t Q[512];
static uint32_t qn;

static void addr_of(uint32_t i, uint8_t a[4])
{
    a[0] = (uint8_t) i;
    a[1] = 0xA1;
    a[2] = 0xF0;
    a[3] = 0x0D;
}

static void flush(uint32_t from)
{
    for (uint32_t i = 0; i < g_ob.n; i++) {
        const vna_out_ent_t *e = &g_ob.e[i];
        if (e->addr_len != 4 || e->addr[1] != 0xA1 || e->addr[0] >= NP || qn >= 512 ||
            e->len > sizeof Q[0].d)
            abort(); /* the harness never drops a datagram silently */
        Q[qn].from = from;
        Q[qn].to = e->addr[0];
        Q[qn].len = e->len;
        memcpy(Q[qn].d, g_ob.buf + e->off, e->len);
        qn++;
    }
    vna_outbox_clear(&g_ob);
}

static void run(void)
{
    static pkt_t b[512];
    for (int guard = 0; qn && guard < 64; guard++) {
        uint32_t k = qn;
        memcpy(b, Q, sizeof b[0] * k);
        qn = 0;
        for (uint32_t i = 0; i < k; i++) {
            uint8_t a[4];
            addr_of(b[i].from, a);
            g_now++;
            (void) vna_node_handle(&P[b[i].to].node, b[i].d, b[i].len, a, 4, g_now, &g_ob);
            flush(b[i].to);
        }
    }
}

static void tick(uint64_t dt)
{
    g_now += dt;
    for (uint32_t i = 0; i < NP; i++) {
        vna_node_tick(&P[i].node, g_now, &g_ob);
        aipi_vna_tick(&P[i].v, g_now);
        flush(i);
    }
    run();
}

static int ec_reserve(void *ctx, uint64_t amt)
{
    peer_t *p = (peer_t *) ctx;
    if (amt > p->balance) return -1;
    p->balance -= amt;
    p->escrow += amt;
    return 0;
}
static int ec_pay(void *ctx, const vna_id_t *ex, uint64_t amt, const ipfsn_cid_t *call)
{
    peer_t *p = (peer_t *) ctx;
    (void) call;
    if (p->pays >= 8) return -1;
    p->paid_to[p->pays] = *ex;
    p->paid_amt[p->pays++] = amt;
    p->escrow -= amt;
    p->paid_total += amt;
    return 0;
}
static void ec_refund(void *ctx, uint64_t amt)
{
    peer_t *p = (peer_t *) ctx;
    p->escrow -= amt;
    p->balance += amt;
    p->refunded += amt;
}
static void ec_paid(void *ctx, const vna_id_t *caller, uint64_t amt, const ipfsn_cid_t *call)
{
    peer_t *p = (peer_t *) ctx;
    p->got_paid++;
    p->paid_by = *caller;
    p->paid_amount = amt;
    p->paid_call = *call;
}

static void peer_up(uint32_t i, bool with_econ)
{
    peer_t *p = &P[i];
    uint8_t seed[32] = {0};
    seed[0] = (uint8_t) (i + 1);
    seed[1] = 0xA1;
    if (vna_identity_create(&p->idn, seed, 0, 1) != VNA_OK) abort();
    vna_node_cfg_t cfg;
    vna_node_cfg_default(&cfg);
    cfg.degree = VNA_DEG_STORE; /* hold DHT records (provider records) for others */
    vna_node_mem_t mem = {p->pool, POOL, p->kc, 16, p->rp, 16, p->dd, 32, p->store, 8};
    seed[2] = 0x77;
    if (vna_node_init(&p->node, &p->idn, &cfg, &mem, seed) != VNA_OK) abort();
    /* every peer shares compute and a few records with everyone */
    vna_agree_default(&p->ag, &p->idn.id);
    p->ag.degree = VNA_DEG_COMPUTE;
    p->ag.audience = VNA_AUD_EVERYONE;
    p->ag.compute_per_cycle = p->ag.q_compute = 1000;
    p->ag.q_records = 8;
    p->ag.quota_period_ms = 3600000;
    p->ag.expires_ms = g_now + 24ull * 3600000u;
    vna_usage_init(&p->us, p->ue, 8, p->le, 4);
    vna_node_set_agreement(&p->node, &p->ag, &p->us, 0, 0);

    p->ms.cap = 4u << 20;
    p->ms.buf = malloc(p->ms.cap);
    p->scratch = malloc(IPFSN_SCRATCH_MIN);
    p->net = malloc(IPFSN_BLOCK_MAX + 256);
    p->modbuf = malloc(AIPI_MODULE_MAX);
    p->conbuf = malloc(AIPI_CONTRACT_MAX);
    ipfsn_memstore_ops(&p->ms, &p->ops);
    if (ipfsn_bs_init(&p->bs, &p->ops, p->tab, 64, p->scratch, IPFSN_SCRATCH_MIN) != IPFSN_OK)
        abort();
    uint8_t key[IPFSN_KEY_LEN];
    memset(key, (int) i + 1, sizeof key);
    ipfsn_bs_set_key(&p->bs, key);
    if (ipfsn_node_init(&p->in, &p->bs, p->pins, 16, p->net, IPFSN_BLOCK_MAX + 256) != IPFSN_OK)
        abort();
    if (aipi_init(&p->a, &p->in, p->jobs, 4, p->memo, 4, p->modbuf, p->conbuf) != AIPI_OK) abort();
    p->a.online = true;
    p->a.exec.ctx = &p->fx;
    p->a.exec.run = fx_run;
    p->a.exec.check = fx_check;
    aipi_vna_econ_t ec = {p, ec_reserve, ec_pay, ec_refund, ec_paid};
    CHECK(aipi_vna_init(&p->v, &p->a, &p->node, &g_ob, with_econ ? &ec : NULL) == AIPI_OK);
    CHECK(!memcmp(p->a.self, p->idn.id.b, 32) && p->a.mesh.ctx == &p->v);
    p->balance = 10000;
}

static const uint8_t MODULE[] = "fake-module: isqrt sq";

static void make_fn(aipi_fn_t *fn, const ipfsn_cid_t *mod, const char *entry, int verify)
{
    memset(fn, 0, sizeof *fn);
    fn->intent = AIPI_INTENT_FN;
    fn->verify = (uint8_t) verify;
    fn->privacy = AIPI_PRIV_PUBLIC;
    fn->determinism = AIPI_DET_DETERMINISTIC;
    fn->fuel_max = 100000;
    fn->module = *mod;
    strcpy(fn->entry, entry);
    if (verify == AIPI_VERIFY_CHECKER) strcpy(fn->checker, "isqrt_ok");
    if (verify == AIPI_VERIFY_QUORUM) {
        fn->quorum_n = 3;
        fn->quorum_k = 2;
    }
}

static void hold(uint32_t i, const aipi_fn_t *fn, ipfsn_cid_t *f)
{
    ipfsn_cid_t mod;
    CHECK(aipi_put_blob(&P[i].a, MODULE, sizeof MODULE, IPFSN_VIS_PUBLIC, &mod) == AIPI_OK);
    CHECK(aipi_publish_fn(&P[i].a, fn, f) == AIPI_OK);
}

static uint8_t in8[8];
static const uint8_t *u64in(uint64_t v)
{
    wr64(in8, v);
    return in8;
}

/* aipi_vna_execute from peer `i`, then put what it queued on the wire */
static int ex(uint32_t i, aipi_vna_t *v, const ipfsn_cid_t *f, const uint8_t *in, bool priv,
              uint64_t bounty, uint64_t ttl, uint32_t *job)
{
    int r = aipi_vna_execute(v, f, in, 8, priv, bounty, g_now, ttl, job);
    flush(i);
    return r;
}

static bool balanced(const peer_t *p)
{
    return p->balance + p->escrow + p->paid_total == 10000;
}

int main(void)
{
    size_t obcap = (size_t) (VNA_OUTBOX_MAX + 1u) * (VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD);
    g_obuf = malloc(obcap);
    vna_outbox_init(&g_ob, g_obuf, (uint32_t) obcap);
    /* 0 caller, 1 and 2 honest executors, 3 a liar, 4 a stranger */
    for (uint32_t i = 0; i < NP; i++) peer_up(i, true);
    P[3].fx.lie = 1;
    CHECK(aipi_vna_init(NULL, &P[0].a, &P[0].node, &g_ob, NULL) == AIPI_ERR_ARG);
    /* everyone meets everyone through peer 0 */
    for (uint32_t i = 1; i < NP; i++) {
        uint8_t a[4];
        addr_of(0, a);
        CHECK(vna_node_bootstrap(&P[i].node, a, 4, g_now, &g_ob) == VNA_OK);
        flush(i);
        run();
    }
    tick(10);
    for (uint32_t i = 1; i < NP; i++) CHECK(vna_rt_find(&P[0].node.rt, &P[i].idn.id) != 0);

    ipfsn_cid_t mod, f, fq;
    aipi_fn_t fn, fnq;
    ipfsn_cid_sha256(IPFSN_MC_RAW, MODULE, sizeof MODULE, &mod);
    make_fn(&fn, &mod, "isqrt", AIPI_VERIFY_CHECKER);
    make_fn(&fnq, &mod, "sq", AIPI_VERIFY_QUORUM);
    for (uint32_t i = 0; i < 4; i++) {
        hold(i, &fn, &f);
        hold(i, &fnq, &fq);
    }
    uint32_t job;
    uint8_t out[16];
    uint32_t len;

    printf("[1] the owner's executor answers; verified, then paid over Vinea\n");
    CHECK(aipi_vna_add_executor(&P[0].v, &P[1].idn.id) == AIPI_OK);
    CHECK(aipi_vna_add_executor(&P[0].v, &P[1].idn.id) == AIPI_OK && P[0].v.nexec == 1);
    {
        vna_id_t z;
        memset(&z, 0, sizeof z);
        CHECK(aipi_vna_add_executor(&P[0].v, &z) == AIPI_ERR_ARG);
    }
    /* executors are not serving yet: BUSY */
    CHECK(ex(0, &P[0].v, &f, u64in(1000), false, 100, 5000, &job) == AIPI_OK);
    CHECK(aipi_job(&P[0].a, job)->route == AIPI_ROUTE_MESH && P[0].v.calls_out == 1);
    run();
    CHECK(P[1].v.serve_refused == 1 && P[0].v.busy_in == 1);
    CHECK(aipi_job(&P[0].a, job)->status == AIPI_JOB_PENDING);
    tick(6000); /* expiry refunds */
    CHECK(aipi_job(&P[0].a, job)->status == AIPI_JOB_EXPIRED && P[0].refunded == 100);
    CHECK(balanced(&P[0]) && aipi_vna_pending(&P[0].v) == 0);
    aipi_release(&P[0].a, job);

    for (uint32_t i = 1; i < 4; i++) aipi_vna_serve(&P[i].v, true, 10);
    CHECK(ex(0, &P[0].v, &f, u64in(1000), false, 100, 5000, &job) == AIPI_OK);
    run();
    const aipi_job_t *j = aipi_job(&P[0].a, job);
    CHECK(j->status == AIPI_JOB_DONE && P[1].v.served == 1 && P[0].v.claims_in == 1);
    CHECK(aipi_result(&P[0].a, job, out, sizeof out, &len) == AIPI_OK && len == 8 &&
          rd64(out) == 31);
    CHECK(P[0].pays == 1 && vna_id_eq(&P[0].paid_to[0], &P[1].idn.id) && P[0].paid_amt[0] == 100);
    CHECK(P[0].v.paid_out == 1 && P[1].got_paid == 1 && P[1].paid_amount == 100 &&
          vna_id_eq(&P[1].paid_by, &P[0].idn.id) && ipfsn_cid_equal(&P[1].paid_call, &j->call_cid));
    CHECK(balanced(&P[0]) && P[0].escrow == 0);
    tick(10);
    CHECK(aipi_vna_pending(&P[0].v) == 0);
    aipi_release(&P[0].a, job);

    printf("[2] a liar is refused and never paid; the bounty comes back\n");
    aipi_vna_clear_executors(&P[0].v);
    CHECK(aipi_vna_add_executor(&P[0].v, &P[3].idn.id) == AIPI_OK);
    CHECK(ex(0, &P[0].v, &f, u64in(2000), false, 50, 5000, &job) == AIPI_OK);
    run();
    j = aipi_job(&P[0].a, job);
    CHECK(P[3].v.served == 1 && j->status == AIPI_JOB_PENDING && j->nrejected == 1);
    CHECK(P[0].pays == 1 && P[3].got_paid == 0);
    tick(6000);
    CHECK(j->status == AIPI_JOB_EXPIRED && P[0].refunded == 150 && balanced(&P[0]));
    aipi_release(&P[0].a, job);

    printf("[3] V2: strangers and relayed claims never reach AIPI\n");
    CHECK(ex(0, &P[0].v, &f, u64in(81), false, 40, 5000, &job) == AIPI_OK);
    /* the liar's answer is dropped on the floor before it arrives */
    qn = 0;
    j = aipi_job(&P[0].a, job);
    {
        uint8_t claim[AIPI_CLAIM_ENC_MAX], o8[8];
        wr64(o8, 9);
        /* a stranger (peer 4, never asked) sends a correct claim in its own name */
        int cl = aipi_claim_encode(&j->call_cid, P[4].idn.id.b, o8, 8, 0, 0, claim, sizeof claim);
        CHECK(cl > 0 && vna_node_send_aipi(&P[4].node, &P[0].idn.id, VNA_AIPI_CLAIM, 0, claim,
                                           (uint32_t) cl, g_now, &g_ob) == VNA_OK);
        flush(4);
        run();
        CHECK(P[0].v.claims_refused == 1 && j->status == AIPI_JOB_PENDING && j->nclaims == 0);
        /* the stranger relays a claim naming the asked executor (peer 3) */
        cl = aipi_claim_encode(&j->call_cid, P[3].idn.id.b, o8, 8, 0, 0, claim, sizeof claim);
        CHECK(vna_node_send_aipi(&P[4].node, &P[0].idn.id, VNA_AIPI_CLAIM, 0, claim, (uint32_t) cl,
                                 g_now, &g_ob) == VNA_OK);
        flush(4);
        run();
        CHECK(P[0].v.claims_refused == 2 && j->nclaims == 0);
        /* garbage claim bytes */
        CHECK(vna_node_send_aipi(&P[4].node, &P[0].idn.id, VNA_AIPI_CLAIM, 0, o8, 8, g_now,
                                 &g_ob) == VNA_OK);
        flush(4);
        run();
        CHECK(P[0].v.claims_refused == 3);
        /* the asked executor itself: accepted and paid */
        cl = aipi_claim_encode(&j->call_cid, P[3].idn.id.b, o8, 8, 0, 0, claim, sizeof claim);
        CHECK(vna_node_send_aipi(&P[3].node, &P[0].idn.id, VNA_AIPI_CLAIM, 0, claim, (uint32_t) cl,
                                 g_now, &g_ob) == VNA_OK);
        flush(3);
        run();
        CHECK(j->status == AIPI_JOB_DONE && P[0].v.claims_in == 3 && P[3].got_paid == 1);
        /* a late claim for a finished job */
        CHECK(vna_node_send_aipi(&P[3].node, &P[0].idn.id, VNA_AIPI_CLAIM, 0, claim, (uint32_t) cl,
                                 g_now, &g_ob) == VNA_OK);
        flush(3);
        run();
        CHECK(P[0].v.claims_refused == 4);
    }
    CHECK(balanced(&P[0]));
    tick(10);
    aipi_release(&P[0].a, job);

    printf("[4] quorum 2 of 3: the two honest executors are paid, the liar is not\n");
    aipi_vna_clear_executors(&P[0].v);
    for (uint32_t i = 1; i < 4; i++) CHECK(aipi_vna_add_executor(&P[0].v, &P[i].idn.id) == AIPI_OK);
    int pays0 = P[0].pays;
    CHECK(ex(0, &P[0].v, &fq, u64in(12), false, 101, 5000, &job) == AIPI_OK);
    run();
    j = aipi_job(&P[0].a, job);
    CHECK(j->status == AIPI_JOB_DONE && aipi_result(&P[0].a, job, out, sizeof out, &len) == 0 &&
          rd64(out) == 144);
    CHECK(P[0].pays - pays0 == 2 && P[0].paid_amt[pays0] + P[0].paid_amt[pays0 + 1] == 101);
    CHECK(P[1].got_paid == 2 && P[2].got_paid == 1 && P[3].got_paid == 1);
    CHECK(balanced(&P[0]) && P[0].escrow == 0);
    tick(10);
    aipi_release(&P[0].a, job);

    printf("[5] the bounty floor, and no economy\n");
    aipi_vna_clear_executors(&P[0].v);
    CHECK(aipi_vna_add_executor(&P[0].v, &P[2].idn.id) == AIPI_OK);
    uint32_t sr = P[2].v.serve_refused;
    CHECK(ex(0, &P[0].v, &f, u64in(50), false, 5, 5000, &job) == AIPI_OK);
    run();
    CHECK(P[2].v.serve_refused == sr + 1 && aipi_job(&P[0].a, job)->status == AIPI_JOB_PENDING);
    tick(6000);
    aipi_release(&P[0].a, job);
    CHECK(balanced(&P[0]));
    {
        aipi_vna_t *v = &P[4].v; /* re-bind peer 4 with no economy */
        CHECK(aipi_vna_init(v, &P[4].a, &P[4].node, &g_ob, NULL) == AIPI_OK);
        ipfsn_cid_t f4;
        hold(4, &fn, &f4);
        CHECK(aipi_vna_add_executor(v, &P[2].idn.id) == AIPI_OK);
        CHECK(ex(4, v, &f4, u64in(50), false, 5, 5000, &job) == AIPI_ERR_FUNDS);
        aipi_vna_serve(&P[2].v, true, 0);
        CHECK(ex(4, v, &f4, u64in(50), false, 0, 5000, &job) == AIPI_OK);
        run();
        CHECK(aipi_job(&P[4].a, job)->status == AIPI_JOB_DONE && v->paid_out == 0);
        aipi_vna_serve(&P[2].v, true, 10);
        aipi_release(&P[4].a, job);
    }

    printf("[6] private work never leaves the node; no route at all is refused\n");
    {
        uint32_t c0 = P[0].v.calls_out;
        CHECK(ex(0, &P[0].v, &f, u64in(64), true, 10, 5000, &job) == AIPI_OK);
        CHECK(aipi_job(&P[0].a, job)->route == AIPI_ROUTE_LOCAL && P[0].v.calls_out == c0);
        aipi_release(&P[0].a, job);
        /* a node that knows nobody: no executor, no lookup possible */
        peer_t *lone = &P[4];
        aipi_vna_clear_executors(&lone->v);
        static vna_contact_t keep[POOL];
        uint32_t nk = vna_node_contacts(&lone->node, keep, POOL);
        for (uint32_t i = 0; i < nk; i++) vna_rt_remove(&lone->node.rt, &keep[i].id);
        ipfsn_cid_t f4;
        hold(4, &fnq, &f4);
        uint32_t pend = aipi_vna_pending(&lone->v);
        CHECK(ex(4, &lone->v, &f4, u64in(3), false, 0, 5000, &job) == AIPI_ERR_TRANSPORT);
        CHECK(aipi_vna_pending(&lone->v) == pend);
    }

    printf("[7] V1: a provider found in the DHT is asked\n");
    aipi_vna_clear_executors(&P[0].v);
    CHECK(aipi_vna_announce(&P[2].v, &f, NULL, 0, g_now, 3600000) >= 0);
    flush(2);
    run();
    tick(2500);
    tick(2500);
    {
        uint8_t key[32];
        vna_id_t k;
        CHECK(aipi_provider_key(&f, key) == AIPI_OK);
        memcpy(k.b, key, 32);
        uint32_t holders = 0;
        for (uint32_t i = 0; i < NP; i++) holders += vna_node_store_find(&P[i].node, &k) != 0;
        CHECK(holders >= 2);
    }
    uint32_t s2 = P[2].v.served;
    CHECK(ex(0, &P[0].v, &f, u64in(4096), false, 20, 20000, &job) == AIPI_OK);
    CHECK(P[0].v.lookups >= 1);
    run();
    for (int r = 0; r < 4 && aipi_job(&P[0].a, job)->status == AIPI_JOB_PENDING; r++) tick(100);
    j = aipi_job(&P[0].a, job);
    CHECK(P[0].v.provider_sends == 1 && P[2].v.served == s2 + 1);
    CHECK(j->status == AIPI_JOB_DONE && aipi_result(&P[0].a, job, out, sizeof out, &len) == 0 &&
          rd64(out) == 64);
    CHECK(balanced(&P[0]));
    CHECK(aipi_vna_announce(&P[2].v, &f, NULL, VNA_ADDR_MAX + 1, g_now, 1000) == VNA_ERR_ARG);

    printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
