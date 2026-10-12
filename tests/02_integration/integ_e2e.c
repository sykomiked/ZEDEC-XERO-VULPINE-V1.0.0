/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* integ_e2e.c — Tier 2 integration (gap A6): tensor -> pay -> escrow timeout
 * -> rollback under a PQ reconnect, on a seeded lossy network.
 *
 * THREE NODES on netsim.h (jitter, loss, reordering, duplicates, partitions)
 *   A  buyer: wants tensor jobs done, pays for them
 *   B  provider: runs the job on kernel/src/tensor (zt_matvec + zt_softmax)
 *   C  clearing: the market (kernel/src/market) and the ledger of record
 *      (kernel/src/pay via mk_pay, the market's settle hook)
 * Every pair talks over a Vinea v3 session (kernel/src/vinea/vna_session:
 * ML-KEM-1024 + X25519 key agreement, ML-DSA-87 authentication,
 * ChaCha20-Poly1305 records with a 64-entry replay window). Tensors travel as
 * zt frames (T16, 21-octet frames with a check). The market acts only for the
 * identity the PQ session authenticated: the actor of every market call is
 * the session's peer NodeID, and listings are signed with the same ML-DSA-87
 * node key.
 *
 * ONE JOB: A orders and pays (capture into escrow), sends the input to B, B
 * computes, returns the result to A and fulfils at C, A checks the result
 * bit for bit and confirms, C releases escrow (assurance fee to commons).
 * A may confirm before B's fulfilment reaches C (the market lets a buyer
 * confirm receipt directly), so the escrow times out when a partition
 * between A and B holds up the job or its result past the store's window:
 * C's timer then refunds the escrow (the rollback of the sale), B's late
 * fulfilment is refused and B is paid nothing for that job.
 *
 * FAULTS, all drawn from the seed: datagram loss, duplication, reordering,
 * jitter, link partitions (one long enough to force the escrow timeout in
 * every other seed), session resets on either side (the initiator re-runs the
 * PQ handshake at once; a reset responder is found by the initiator's
 * heartbeat timeout), and compliance freezes on the buyer or escrow ledger
 * accounts that make the ledger refuse captures, releases and refunds.
 *
 * Odd jobs carry a second, differently taxed line; the operator's sales tax
 * is remitted by the facilitator in two seeds of three and by the seller in
 * the third (both kinds of release).
 *
 * PROPERTIES
 *   E1 conservation, on EVERY clearing cycle at C (once a simulated second):
 *      pay_ledger L2/L3 and the provenance chain; supply (holder DEBIT ==
 *      issuer CREDIT == what was minted); mk_market_conserved (M2, M3); the
 *      escrow account holds exactly the sum the orders say they hold; buyer,
 *      seller, commons + fee buckets and tax match books rebuilt from the
 *      market's order records; the fee buckets hold exactly
 *      pay_assure_split of every sweep.
 *   E2 rollback: a settlement the ledger refuses changes no ledger account,
 *      no journal entry and no order (the market rolls back too). Every
 *      settlement names its own order and moves exactly its money: a
 *      capture the order total, a release all of escrow, a refund one whole
 *      line. Every paid order's fulfilment deadline is the store's window,
 *      and a handle that names no order is refused (MK_ERR_NOT_FOUND).
 *   E3 escrow timeout: an order whose fulfilment is late is REFUNDED to the
 *      minor unit, the seller gets nothing for it, and the late fulfilment
 *      is refused.
 *   E4 the end state: every order is terminal, escrow is empty, every result
 *      that reached A equals the reference tensor bit for bit, and every
 *      fulfilled order's result reached A.
 *   E5 PQ sessions: no record sealed under an older session ever opens after
 *      a reconnect, and no duplicated datagram is accepted twice.
 *   E6 determinism: the same seed gives the same ledger chain head, order
 *      states and network counters.
 * The harness also proves it can fail: the conservation check rejects a
 * ledger with one minor unit added out of thin air.
 *
 *   integ_e2e [seeds]     default 8 seeds (each seed has its own profile)
 */
#include "tier.h"
#include "netsim.h"
#include "market.h"
#include "mk_pay.h"
#include "pay_ledger.h"
#include "pay_assure.h"
#include "vna_session.h"
#include "zt.h"

/* ---------------------------------------------------------------- setup -- */
enum { NA = 0, NB = 1, NC = 2 };
#define T0_S       1000000u /* market clock at simulated time 0 (s)     */
#define STEP_MS    5u
#define CYCLE_MS   1000u /* clearing cycle                          */
#define PING_MS    500u
#define DEAD_MS    2500u
#define HS_RTO_MS  1000u
#define RTO_MS     300u
#define RETRY_MS   400u
#define LAUNCH_MS  40000u
#define CAP_MS     400000u
#define FULFIL_S   20u
#define RELEASE_S  15u
#define PRICE      1234567u
#define PRICE2     345678u /* the add-on line some orders carry */
#define START      1000000000u
#define MAX_JOBS   12u
#define MAX_MSG    1024u
#define MSG_MAX    768u
#define ROWS       16u
#define NB_BLK     2u
#define BLK_WORDS  10u
#define X_WORDS    (NB_BLK * BLK_WORDS)
#define MAX_FRAMES 8u

static uint64_t g_rng;
static uint64_t g_now; /* simulated ms */ /* harness randomness (faults, keys, handshakes) */

/* ---------------------------------------------------- tensor reference -- */
static zt_q8_t W[ROWS * NB_BLK];

static void words_of_block(const zt_q8_t *b, uint32_t *w)
{
    w[0] = (uint32_t) b->scale;
    w[1] = (uint32_t) (uint8_t) b->phi_k | (uint32_t) b->shift << 8;
    for (uint32_t i = 0; i < ZT_BLOCK / 4; i++)
        w[2 + i] = (uint32_t) (uint8_t) b->q[4 * i] | (uint32_t) (uint8_t) b->q[4 * i + 1] << 8 |
                   (uint32_t) (uint8_t) b->q[4 * i + 2] << 16 |
                   (uint32_t) (uint8_t) b->q[4 * i + 3] << 24;
}

static void block_of_words(const uint32_t *w, zt_q8_t *b)
{
    b->scale = (int32_t) w[0];
    b->phi_k = (int8_t) (uint8_t) w[1];
    b->shift = (uint8_t) (w[1] >> 8);
    for (uint32_t i = 0; i < ZT_BLOCK; i++)
        b->q[i] = (int8_t) (uint8_t) (w[2 + i / 4] >> (8 * (i % 4)));
}

static void rand_fx(uint64_t *s, zt_fx *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) v[i] = (zt_fx) ns_below(s, 4 * ZT_ONE) - 2 * ZT_ONE;
}

/* the provider's job: y = softmax(W x) */
static void tensor_job(const zt_q8_t *x, zt_fx y[ROWS])
{
    zt_matvec(W, ROWS, x, NB_BLK, y);
    zt_softmax(y, ROWS);
}

/* ------------------------------------------------------------- nodes ---- */
typedef struct {
    uint32_t id;
    uint32_t len;
    uint8_t b[MSG_MAX];
    bool acked;
    uint64_t last_tx; /* 0 = never sent */
} out_msg_t;

typedef struct {
    uint8_t peer;
    bool initiator;
    vna_hs_t hs;
    vna_sess_t sess;
    uint32_t gen;    /* generation of the established session   */
    uint32_t hs_gen; /* generation of the handshake in progress */
    uint64_t hs_at, last_heard, last_ping;
    out_msg_t out[MAX_MSG];
    uint32_t n_out;
    uint8_t seen[MAX_MSG + 1]; /* incoming message ids already delivered */
} link_t;

typedef struct {
    vna_identity_t idn;
    link_t link[NS_NODES];
} node_t;

static node_t N[NS_NODES];
static ns_t NET;
static uint32_t link_gen; /* every handshake gets a fresh generation */

typedef enum {
    J_IDLE = 0,
    J_ORDERED, /* ORDER sent                       */
    J_PAYING,  /* PAY sent                         */
    J_WORKING, /* JOB sent to B                    */
    J_CONFIRM, /* result checked, CONFIRM sent     */
    J_DONE
} job_state_t;

typedef struct {
    uint64_t start_ms, retry_at;
    uint32_t qty;
    uint16_t handle;
    job_state_t st;
    zt_q8_t x[NB_BLK];
    zt_fx ref[ROWS];
    bool result_ok, result_bad, fulfilled_by_b;
} job_t;

static job_t JOBS[MAX_JOBS];
static uint32_t n_jobs;

/* ------------------------------------------------------- market + pay ---- */
static mk_market_t M;
static pay_ledger_t PL;
static mk_pay_t MP;
static uint32_t acct_issuer, acct_a, acct_b, acct_escrow, acct_commons, acct_tax;
static uint32_t acct_bucket[PAY_ASSURE_BUCKETS];
static uint8_t vfv;
static uint16_t store_h, listing_h, listing2_h, cart_h;
static bool remit_tax; /* the facilitator remits sales tax (else the seller does) */
static uint16_t handle_of_job[MAX_JOBS];

typedef struct {
    /* faults injected and exercised */
    uint64_t resets, reconnect_dead, handshakes, partitions, freezes, refused, timeouts;
    uint64_t late_fulfil_refused, completed, refunded, expired;
    /* transport ground truth */
    uint64_t stale_tried, stale_accepted, dup_tried, dup_accepted, dup_suppressed;
    /* violations: must stay 0 */
    uint64_t cycles, conserve_fail, rollback_fail, result_bad, end_fail, settle_bad, probes_refused;
    /* fee sweeps */
    uint64_t swept, bucket_expect[PAY_ASSURE_BUCKETS];
} stats_t;
static stats_t S;

static uint64_t g_freeze_until[2];   /* [0] buyer, [1] escrow: simulated ms */
static uint8_t g_accepted[1u << 17]; /* by original datagram id, bitset (2^20 ids) */

static bool accepted_get(uint64_t id)
{
    uint64_t i = id % ((uint64_t) sizeof g_accepted * 8);
    return (g_accepted[i / 8] >> (i % 8)) & 1u;
}
static void accepted_set(uint64_t id)
{
    uint64_t i = id % ((uint64_t) sizeof g_accepted * 8);
    g_accepted[i / 8] |= (uint8_t) (1u << (i % 8));
}

static void id_of(uint8_t node, mk_id_t *out)
{
    memcpy(out->b, N[node].idn.id.b, 32);
}

static uint32_t acct_of(void *ctx, const mk_id_t *id, uint8_t ccy)
{
    (void) ctx;
    if (ccy != vfv) return MK_PAY_NONE;
    if (!memcmp(id->b, N[NA].idn.id.b, 32)) return acct_a;
    if (!memcmp(id->b, N[NB].idn.id.b, 32)) return acct_b;
    return MK_PAY_NONE;
}

/* A capture takes the order total; a release empties escrow; a refund (one
 * per line, M4) gives the buyer back what is left of one whole line. */
static bool settle_amount_ok(const mk_order_t *o, const mk_settle_t *s)
{
    uint64_t in = s->from_buyer + s->from_seller + s->from_commons + s->from_tax;
    uint64_t out = s->to_seller + s->to_commons + s->to_tax + s->to_buyer;
    if (s->kind == MK_SETTLE_CAPTURE) return in == o->total && out == 0;
    if (out < in) return false;
    if (s->kind == MK_SETTLE_RELEASE) return out - in == o->held_base + o->held_tax;
    if (s->kind != MK_SETTLE_REFUND || s->to_buyer != out - in) return false;
    for (uint32_t i = 0; i < o->n_lines; i++)
        if (o->line[i].refunded_qty < o->line[i].qty &&
            o->line[i].line_total - o->line[i].refunded_total == out - in)
            return true;
    return false;
}

/* E2: a refused settlement changes nothing on the ledger */
static uint16_t g_refused_ref[MK_MAX_ORDERS];
static uint32_t g_n_refused_ref;
static pay_account_t g_acct_before[PAY_MAX_ACCOUNTS];

static bool h_settle(void *ctx, const mk_settle_t *s)
{
    (void) ctx;
    uint64_t seq = PL.seq;
    uint8_t head[PAY_HASH_LEN];
    memcpy(head, PL.chain_head, sizeof head);
    memcpy(g_acct_before, PL.acct, sizeof g_acct_before);
    MP.tick = g_now;
    /* the instruction names the right order and moves exactly its money */
    const mk_order_t *o = mk_order(&M, (uint16_t) s->ref);
    if (!o || o->ccy != s->ccy || memcmp(s->buyer->b, o->buyer.b, 32) ||
        memcmp(s->seller->b, o->seller.b, 32) || !settle_amount_ok(o, s))
        S.settle_bad++;
    if (mk_pay_settle(&MP, s)) return true;
    S.refused++;
    if (PL.seq != seq || memcmp(head, PL.chain_head, sizeof head) ||
        memcmp(g_acct_before, PL.acct, sizeof g_acct_before))
        S.rollback_fail++;
    if (g_n_refused_ref < MK_MAX_ORDERS) g_refused_ref[g_n_refused_ref++] = (uint16_t) s->ref;
    return false;
}

/* signatures on storefronts and listings: ML-DSA-87 node keys */
#define MKT_CTX "zxv-market-v1"
static bool h_verify(void *ctx, const mk_id_t *signer, const uint8_t *msg, uint32_t len,
                     const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    for (uint32_t n = 0; n < NS_NODES; n++)
        if (!memcmp(signer->b, N[n].idn.id.b, 32))
            return sig_len == VNA_SIG_LEN && vna_verify(N[n].idn.pk, MKT_CTX, msg, len, sig);
    return false;
}

static void sign_digest(uint8_t node, const uint8_t d[32], uint8_t *sig)
{
    uint8_t rnd[32];
    for (int i = 0; i < 32; i++) rnd[i] = (uint8_t) ns_rand(&g_rng);
    vna_sign(N[node].idn.sk, MKT_CTX, d, 32, rnd, sig);
}

/* Advance the market clock and run its timers. E2 for the market: an order
 * whose timer settlement was refused is exactly as it was. */
static mk_order_t g_ord_before[MK_MAX_ORDERS];
static void market_tick(void)
{
    memcpy(g_ord_before, M.order, sizeof g_ord_before);
    g_n_refused_ref = 0;
    uint64_t refunded_before = 0, refunded_after = 0;
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++)
        refunded_before += M.order[i].used && M.order[i].state == MK_ORD_REFUNDED;
    mk_tick(&M, T0_S + g_now / 1000u);
    for (uint32_t i = 0; i < g_n_refused_ref; i++) {
        uint16_t h = g_refused_ref[i];
        if (h == 0 || h > MK_MAX_ORDERS ||
            memcmp(&g_ord_before[h - 1], &M.order[h - 1], sizeof(mk_order_t)))
            S.rollback_fail++;
    }
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++)
        refunded_after += M.order[i].used && M.order[i].state == MK_ORD_REFUNDED;
    S.timeouts += refunded_after - refunded_before;
}

/* Around one market call on an order: E2 for the market. If the ledger
 * refused the call's settlement, the order must be exactly as it was. */
static mk_order_t g_call_before;
static void call_begin(uint16_t h)
{
    market_tick();
    const mk_order_t *o = mk_order(&M, h);
    if (o)
        memcpy(&g_call_before, o, sizeof g_call_before);
    else
        memset(&g_call_before, 0, sizeof g_call_before);
}
static mk_status_t call_end(uint16_t h, mk_status_t rc)
{
    const mk_order_t *o = mk_order(&M, h);
    if (rc == MK_ERR_SETTLE && (!o || memcmp(&g_call_before, o, sizeof g_call_before)))
        S.rollback_fail++;
    return rc;
}

static uint64_t g_sweeps;
static void post_sweep(uint64_t amount)
{
    uint64_t n = ++g_sweeps;
    pay_posting_req_t r;
    pay_receipt_t rc;
    uint64_t parts[PAY_ASSURE_BUCKETS];
    memset(&r, 0, sizeof r);
    uint8_t h[32] = {'S', 'W'};
    memcpy(h + 8, &n, sizeof n); /* inside both the idempotency key and the UETR */
    memcpy(r.idem_key, h, 32);
    h[31] = 0x55;
    pay_uetr_from_random(h + 8, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "SWEEP-%llu", (unsigned long long) n);
    r.initiator = r.attestor = 10;
    r.tick = g_now;
    r.kind = PAY_KIND_FEE;
    pay_assure_split(amount, parts);
    r.lines[r.n_lines++] = (pay_line_t){acct_commons, -(int64_t) amount, 0};
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++)
        if (parts[i]) r.lines[r.n_lines++] = (pay_line_t){acct_bucket[i], (int64_t) parts[i], 0};
    if (pay_ledger_post(&PL, &r, &rc) == PAY_OK) {
        S.swept += amount;
        for (int i = 0; i < PAY_ASSURE_BUCKETS; i++) S.bucket_expect[i] += parts[i];
    }
}

static uint64_t bal(uint32_t a)
{
    return pay_ledger_account(&PL, a)->debit;
}

/* E1: every conservation law at once; true when all hold */
static bool conserved(void)
{
    bool ok = pay_ledger_check(&PL) && pay_ledger_verify_chain(&PL) && mk_market_conserved(&M);
    uint64_t d = 0, c = 0;
    int64_t e = 0;
    pay_ledger_totals(&PL, PL.vfv_asset, PAY_CAP_FINANCIAL, &d, &c, &e);
    const pay_account_t *iss = pay_ledger_account(&PL, acct_issuer);
    ok = ok && d == c && e == 0 && iss->credit == START && iss->debit == 0;

    uint64_t held = 0, paid = 0, back = 0, seller = 0, fees = 0, tax = 0, charged = 0;
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++) {
        const mk_order_t *o = &M.order[i];
        if (!o->used) continue;
        if (o->paid_at && o->fulfil_by != o->paid_at + FULFIL_S)
            ok = false; /* the store's window */
        held += o->held_base + o->held_tax;
        paid += o->captured;
        back += o->to_buyer;
        seller += o->to_seller - o->seller_funded;
        fees += o->to_commons - o->commons_returned;
        tax += o->to_tax - o->tax_returned;
        charged += o->fee_charged;
    }
    uint64_t pools = bal(acct_commons);
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++) {
        pools += bal(acct_bucket[i]);
        ok = ok && bal(acct_bucket[i]) == S.bucket_expect[i];
    }
    ok = ok && bal(acct_escrow) == held && bal(acct_a) == START - paid + back &&
         bal(acct_b) == seller && pools == fees && fees == charged && bal(acct_tax) == tax &&
         bal(acct_a) + bal(acct_b) + bal(acct_escrow) + pools + bal(acct_tax) == START;
    return ok;
}

static void set_frozen(uint32_t a, bool on)
{
    uint8_t f = pay_ledger_account(&PL, a)->flags;
    pay_ledger_set_flags(&PL, a,
                         on ? (uint8_t) (f | PAY_ACCT_FROZEN) : (uint8_t) (f & ~PAY_ACCT_FROZEN));
}

static void clearing_cycle(bool faults)
{
    /* compliance freezes come and go */
    for (int k = 0; k < 2; k++) {
        uint32_t a = k ? acct_escrow : acct_a;
        if (g_freeze_until[k] && g_now >= g_freeze_until[k]) {
            g_freeze_until[k] = 0;
            set_frozen(a, false);
        } else if (faults && !g_freeze_until[k] && ns_chance(&g_rng, 60000)) {
            g_freeze_until[k] = g_now + 1000u + ns_below(&g_rng, 3000);
            set_frozen(a, true);
            S.freezes++;
        }
    }
    market_tick();
    uint64_t c = bal(acct_commons);
    if (c) post_sweep(c);
    S.cycles++;
    if (!conserved()) S.conserve_fail++;
}

/* ---------------------------------------------------------- transport ---- */
enum { PK_HS1 = 1, PK_HS2 = 2, PK_HS3 = 3, PK_REC = 4 };
enum { FR_DATA = 1, FR_ACK = 2, FR_PING = 3, FR_PONG = 4 };

static uint8_t g_buf[NS_MTU];

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static void rnd_bytes(uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) ns_rand(&g_rng);
}

static void send_raw(uint8_t from, uint8_t to, uint32_t gen, uint8_t kind, const uint8_t *b,
                     uint32_t len)
{
    g_buf[0] = kind;
    memcpy(g_buf + 1, b, len);
    ns_send(&NET, from, to, gen, g_buf, len + 1, g_now);
}

static void send_frame(uint8_t from, uint8_t to, uint8_t fr, uint32_t id, const uint8_t *b,
                       uint32_t len)
{
    link_t *l = &N[from].link[to];
    static uint8_t pt[MSG_MAX + 8], rec[MSG_MAX + 64];
    pt[0] = fr;
    put32(pt + 1, id);
    if (len) memcpy(pt + 5, b, len);
    int32_t n = vna_sess_seal(&l->sess, 1, pt, len + 5, rec, sizeof rec);
    if (n > 0) send_raw(from, to, l->gen, PK_REC, rec, (uint32_t) n);
}

static void start_handshake(uint8_t me, uint8_t peer)
{
    link_t *l = &N[me].link[peer];
    static uint8_t m1[sizeof(vna_hs1_t) + 64];
    uint8_t rnd[128];
    rnd_bytes(rnd, sizeof rnd);
    vna_hs_clear(&l->hs);
    int32_t n = vna_hs_initiate(&l->hs, &N[me].idn, &N[peer].idn.id, 4, 0, rnd, m1, sizeof m1);
    l->hs_gen = ++link_gen;
    l->hs_at = g_now;
    if (n > 0) {
        S.handshakes++;
        send_raw(me, peer, l->hs_gen, PK_HS1, m1, (uint32_t) n);
    }
}

static void teardown(uint8_t me, uint8_t peer)
{
    link_t *l = &N[me].link[peer];
    vna_hs_clear(&l->hs);
    memset(&l->sess, 0, sizeof l->sess);
}

/* queue an application message; the transport retransmits until acked */
static void app_send(uint8_t from, uint8_t to, const uint8_t *b, uint32_t len)
{
    link_t *l = &N[from].link[to];
    if (l->n_out >= MAX_MSG || len > MSG_MAX) {
        S.end_fail++;
        return;
    }
    out_msg_t *m = &l->out[l->n_out];
    m->id = ++l->n_out;
    m->len = len;
    memcpy(m->b, b, len);
    m->acked = false;
    m->last_tx = 0;
}

static void app_deliver(uint8_t me, uint8_t from, const uint8_t *b, uint32_t len);

static void on_record(uint8_t me, const ns_pkt_t *p)
{
    link_t *l = &N[me].link[p->src];
    static uint8_t pt[MSG_MAX + 64];
    uint8_t type = 0;
    uint32_t n = 0;
    if (!l->sess.established) return;
    vna_status_t st = vna_sess_open(&l->sess, p->b + 1, p->len - 1, pt, sizeof pt, &type, &n);
    bool stale = p->gen != l->gen;
    bool dup = p->orig != p->id || accepted_get(p->orig);
    if (stale) S.stale_tried++;
    if (dup && !stale) S.dup_tried++;
    if (st != VNA_OK) return;
    if (stale) S.stale_accepted++;
    if (dup && accepted_get(p->orig)) S.dup_accepted++;
    accepted_set(p->orig);
    l->last_heard = g_now;
    if (n < 5) return;
    uint32_t id = get32(pt + 1);
    switch (pt[0]) {
    case FR_DATA:
        send_frame(me, p->src, FR_ACK, id, NULL, 0);
        if (id == 0 || id > MAX_MSG) return;
        if (l->seen[id]) {
            S.dup_suppressed++;
            return;
        }
        l->seen[id] = 1;
        app_deliver(me, p->src, pt + 5, n - 5);
        break;
    case FR_ACK:
        if (id >= 1 && id <= l->n_out) l->out[id - 1].acked = true;
        break;
    case FR_PING:
        send_frame(me, p->src, FR_PONG, id, NULL, 0);
        break;
    default:
        break;
    }
}

static void on_packet(const ns_pkt_t *p)
{
    uint8_t me = p->dst, peer = p->src;
    link_t *l = &N[me].link[peer];
    static uint8_t out[sizeof(vna_hs2_t) + 64];
    uint8_t rnd[128];
    switch (p->b[0]) {
    case PK_HS1: { /* responder: answer; the current session stays until M3 */
        vna_id_t who;
        if (l->hs.state == VNA_HS_SENT_M2 && p->gen == l->hs_gen) return; /* a duplicate */
        if (l->initiator || vna_hs_peek_initiator(p->b + 1, p->len - 1, &who) != VNA_OK ||
            !vna_id_eq(&who, &N[peer].idn.id))
            return;
        rnd_bytes(rnd, sizeof rnd);
        vna_hs_clear(&l->hs);
        int32_t n =
            vna_hs_respond(&l->hs, &N[me].idn, 4, 0, p->b + 1, p->len - 1, rnd, out, sizeof out);
        if (n > 0) {
            l->hs_gen = p->gen;
            send_raw(me, peer, p->gen, PK_HS2, out, (uint32_t) n);
        }
        break;
    }
    case PK_HS2: {
        if (!l->initiator || l->hs.state != VNA_HS_SENT_M1 || p->gen != l->hs_gen) return;
        rnd_bytes(rnd, 32);
        vna_sess_t s;
        int32_t n = vna_hs_initiator_finish(&l->hs, p->b + 1, p->len - 1, rnd, out, sizeof out, &s);
        if (n <= 0) return;
        send_raw(me, peer, p->gen, PK_HS3, out, (uint32_t) n);
        l->sess = s;
        l->gen = p->gen;
        l->last_heard = g_now;
        for (uint32_t i = 0; i < l->n_out; i++) l->out[i].last_tx = 0; /* resend at once */
        break;
    }
    case PK_HS3: {
        if (l->initiator || l->hs.state != VNA_HS_SENT_M2 || p->gen != l->hs_gen) return;
        vna_sess_t s;
        if (vna_hs_responder_finish(&l->hs, p->b + 1, p->len - 1, &s) != VNA_OK) return;
        l->sess = s;
        l->gen = p->gen;
        l->last_heard = g_now;
        for (uint32_t i = 0; i < l->n_out; i++) l->out[i].last_tx = 0;
        break;
    }
    case PK_REC:
        on_record(me, p);
        break;
    default:
        break;
    }
}

static void pump_link(uint8_t me, uint8_t peer)
{
    link_t *l = &N[me].link[peer];
    if (l->initiator) {
        if (!l->sess.established) {
            if (l->hs.state != VNA_HS_SENT_M1 || g_now - l->hs_at >= HS_RTO_MS)
                start_handshake(me, peer);
            return;
        }
        if (g_now - l->last_heard > DEAD_MS) { /* peer lost our session */
            S.reconnect_dead++;
            teardown(me, peer);
            start_handshake(me, peer);
            return;
        }
        if (g_now - l->last_ping >= PING_MS) {
            l->last_ping = g_now;
            send_frame(me, peer, FR_PING, 0, NULL, 0);
        }
    }
    if (!l->sess.established) return;
    for (uint32_t i = 0; i < l->n_out; i++) {
        out_msg_t *m = &l->out[i];
        if (m->acked || (m->last_tx && g_now - m->last_tx < RTO_MS)) continue;
        m->last_tx = g_now;
        send_frame(me, peer, FR_DATA, m->id, m->b, m->len);
    }
}

static bool all_acked(void)
{
    for (uint32_t a = 0; a < NS_NODES; a++)
        for (uint32_t b = 0; b < NS_NODES; b++)
            for (uint32_t i = 0; a != b && i < N[a].link[b].n_out; i++)
                if (!N[a].link[b].out[i].acked) return false;
    return true;
}

/* -------------------------------------------------------- application ---- */
enum {
    OP_ORDER = 'O',
    OP_ORDER_R = 'o',
    OP_PAY = 'P',
    OP_PAY_R = 'p',
    OP_JOB = 'J',
    OP_RESULT = 'R',
    OP_FULFIL = 'F',
    OP_FULFIL_R = 'f',
    OP_CONFIRM = 'C',
    OP_CONFIRM_R = 'c'
};

/* op, job, handle, a, b (each 32-bit) then an optional tail */
static void msg(uint8_t from, uint8_t to, uint8_t op, uint32_t job, uint32_t h, uint32_t a,
                uint32_t b, const uint8_t *tail, uint32_t tlen)
{
    uint8_t m[MSG_MAX];
    m[0] = op;
    put32(m + 1, job);
    put32(m + 5, h);
    put32(m + 9, a);
    put32(m + 13, b);
    if (tlen) memcpy(m + 17, tail, tlen);
    app_send(from, to, m, 17 + tlen);
}

static bool terminal(mk_order_state_t s)
{
    return s == MK_ORD_COMPLETED || s == MK_ORD_REFUNDED || s == MK_ORD_EXPIRED ||
           s == MK_ORD_CANCELLED;
}

static mk_order_state_t state_of(uint16_t h)
{
    const mk_order_t *o = mk_order(&M, h);
    return o ? o->state : MK_ORD_FREE;
}

/* An authenticated peer naming an order that does not exist is refused. */
static void probe_check(uint16_t h, mk_status_t rc)
{
    if (h != 0 && h <= MK_MAX_ORDERS && M.order[h - 1].used) return;
    if (rc == MK_ERR_NOT_FOUND)
        S.probes_refused++;
    else
        S.end_fail++;
}

/* C: the clearing node; the actor is the PQ-authenticated peer */
static void at_c(uint8_t from, uint8_t op, uint32_t job, uint32_t h16, const uint8_t *tail,
                 uint32_t tlen)
{
    mk_id_t actor;
    memcpy(actor.b, N[NC].link[from].sess.peer.b, 32);
    uint16_t h = (uint16_t) h16;
    mk_status_t rc;
    switch (op) {
    case OP_ORDER: {
        if (from != NA || job >= MAX_JOBS) return;
        if (!handle_of_job[job]) { /* idempotent per job */
            uint16_t out = 0;
            mk_cart_clear(&M, cart_h);
            market_tick();
            if (mk_cart_add(&M, cart_h, listing_h, JOBS[job].qty, 0) == MK_OK &&
                (job % 2 == 0 || mk_cart_add(&M, cart_h, listing2_h, 1, 0) == MK_OK)) {
                mk_checkout_opts_t o;
                memset(&o, 0, sizeof o);
                strcpy(o.region, "XA");
                if (mk_checkout(&M, cart_h, &o, &out, 1) == 1) handle_of_job[job] = out;
            }
        }
        msg(NC, from, OP_ORDER_R, job, handle_of_job[job], 0, 0, NULL, 0);
        break;
    }
    case OP_PAY:
        call_begin(h);
        rc = call_end(h, mk_order_pay(&M, h, &actor));
        probe_check(h, rc);
        msg(NC, from, OP_PAY_R, job, h, state_of(h), (uint32_t) rc, NULL, 0);
        break;
    case OP_FULFIL:
        if (tlen != 32) return;
        call_begin(h);
        rc = call_end(h, mk_order_fulfil(&M, h, &actor, tail, 32));
        if (rc == MK_ERR_STATE && state_of(h) == MK_ORD_REFUNDED) S.late_fulfil_refused++;
        msg(NC, from, OP_FULFIL_R, job, h, state_of(h), (uint32_t) rc, NULL, 0);
        break;
    case OP_CONFIRM:
        call_begin(h);
        rc = call_end(h, mk_order_confirm(&M, h, &actor));
        probe_check(h, rc);
        msg(NC, from, OP_CONFIRM_R, job, h, state_of(h), (uint32_t) rc, NULL, 0);
        break;
    default:
        break;
    }
}

/* B: the provider computes whatever it is sent and fulfils at C */
static void at_b(uint8_t from, uint8_t op, uint32_t job, uint32_t h, const uint8_t *tail,
                 uint32_t tlen)
{
    if (op != OP_JOB || from != NA || job >= MAX_JOBS || tlen % ZT_FRAME_OCTETS) return;
    uint32_t words[MAX_FRAMES * ZT_FRAME_WORDS];
    if (tlen / ZT_FRAME_OCTETS > MAX_FRAMES ||
        zt_frame_unpack(tail, tlen / ZT_FRAME_OCTETS, words) != (int32_t) X_WORDS)
        return;
    zt_q8_t x[NB_BLK];
    for (uint32_t i = 0; i < NB_BLK; i++) block_of_words(words + i * BLK_WORDS, &x[i]);
    zt_fx y[ROWS];
    tensor_job(x, y);
    uint8_t fr[MAX_FRAMES * ZT_FRAME_OCTETS], d[32];
    uint32_t nf = zt_frame_pack((const uint32_t *) y, ROWS, 0, fr);
    msg(NB, NA, OP_RESULT, job, h, 0, 0, fr, nf * ZT_FRAME_OCTETS);
    sha3_256((const uint8_t *) y, sizeof y, d);
    msg(NB, NC, OP_FULFIL, job, h, 0, 0, d, 32);
    JOBS[job].fulfilled_by_b = true;
}

static void a_pay(uint32_t k)
{
    JOBS[k].st = J_PAYING;
    msg(NA, NC, OP_PAY, k, JOBS[k].handle, 0, 0, NULL, 0);
}

static void a_confirm(uint32_t k)
{
    JOBS[k].st = J_CONFIRM;
    msg(NA, NC, OP_CONFIRM, k, JOBS[k].handle, 0, 0, NULL, 0);
}

/* A: the buyer */
static void at_a(uint8_t from, uint8_t op, uint32_t k, uint32_t h, uint32_t a, uint32_t b,
                 const uint8_t *tail, uint32_t tlen)
{
    if (k >= n_jobs) return;
    job_t *j = &JOBS[k];
    switch (op) {
    case OP_ORDER_R:
        if (from != NC || j->st != J_ORDERED) return;
        if (!h) { /* checkout failed: try again later */
            j->retry_at = g_now + RETRY_MS;
            return;
        }
        j->handle = (uint16_t) h;
        a_pay(k);
        break;
    case OP_PAY_R:
        if (from != NC || j->st != J_PAYING) return;
        if (a == MK_ORD_PAID || a == MK_ORD_FULFILLED) {
            uint32_t words[X_WORDS];
            uint8_t fr[MAX_FRAMES * ZT_FRAME_OCTETS];
            for (uint32_t i = 0; i < NB_BLK; i++) words_of_block(&j->x[i], words + i * BLK_WORDS);
            uint32_t nf = zt_frame_pack(words, X_WORDS, 0, fr);
            j->st = J_WORKING;
            msg(NA, NB, OP_JOB, k, j->handle, 0, 0, fr, nf * ZT_FRAME_OCTETS);
        } else if (a == MK_ORD_PENDING_PAYMENT) {
            j->retry_at = g_now + RETRY_MS; /* the ledger refused (a freeze): pay again */
        } else {
            j->st = J_DONE;
        }
        break;
    case OP_RESULT: {
        if (from != NB || j->st != J_WORKING) return;
        uint32_t y[MAX_FRAMES * ZT_FRAME_WORDS];
        bool ok = tlen % ZT_FRAME_OCTETS == 0 && tlen / ZT_FRAME_OCTETS <= MAX_FRAMES &&
                  zt_frame_unpack(tail, tlen / ZT_FRAME_OCTETS, y) == (int32_t) ROWS &&
                  !memcmp(y, j->ref, sizeof j->ref);
        if (!ok) {
            j->result_bad = true;
            S.result_bad++;
            j->st = J_DONE;
            return;
        }
        j->result_ok = true;
        a_confirm(k);
        break;
    }
    case OP_CONFIRM_R:
        if (from != NC || j->st != J_CONFIRM) return;
        if (a == MK_ORD_PAID || a == MK_ORD_FULFILLED) /* not fulfilled yet, or refused */
            j->retry_at = g_now + RETRY_MS;
        else
            j->st = J_DONE;
        break;
    default:
        (void) b;
        break;
    }
}

static void app_deliver(uint8_t me, uint8_t from, const uint8_t *b, uint32_t len)
{
    if (len < 17) return;
    uint32_t job = get32(b + 1), h = get32(b + 5), a = get32(b + 9), c = get32(b + 13);
    if (me == NC) at_c(from, b[0], job, h, b + 17, len - 17);
    if (me == NB) at_b(from, b[0], job, h, b + 17, len - 17);
    if (me == NA) at_a(from, b[0], job, h, a, c, b + 17, len - 17);
}

static bool g_probed;
static void a_step(void)
{
    if (!g_probed && g_now >= 100) { /* handles that name no order */
        g_probed = true;
        msg(NA, NC, OP_PAY, MAX_JOBS, 0, 0, 0, NULL, 0);
        msg(NA, NC, OP_CONFIRM, MAX_JOBS, MK_MAX_ORDERS + 1, 0, 0, NULL, 0);
    }
    for (uint32_t k = 0; k < n_jobs; k++) {
        job_t *j = &JOBS[k];
        if (j->st == J_IDLE && g_now >= j->start_ms) {
            j->st = J_ORDERED;
            msg(NA, NC, OP_ORDER, k, 0, j->qty, 0, NULL, 0);
        }
        if (j->retry_at && g_now >= j->retry_at) {
            j->retry_at = 0;
            if (j->st == J_ORDERED) msg(NA, NC, OP_ORDER, k, 0, j->qty, 0, NULL, 0);
            if (j->st == J_PAYING) a_pay(k);
            if (j->st == J_CONFIRM) a_confirm(k);
        }
    }
}

/* ---------------------------------------------------------------- run ---- */
typedef struct {
    uint64_t at, len;
    uint8_t a, b;
} fault_t;

typedef struct {
    uint8_t chain[PAY_HASH_LEN];
    uint8_t states[MAX_JOBS];
    uint64_t net[5];
    uint64_t bal[3];
} outcome_t;

static void setup_world(uint64_t seed)
{
    memset(N, 0, sizeof N);
    memset(JOBS, 0, sizeof JOBS);
    memset(&S, 0, sizeof S);
    memset(handle_of_job, 0, sizeof handle_of_job);
    memset(g_freeze_until, 0, sizeof g_freeze_until);
    memset(g_accepted, 0, sizeof g_accepted);
    link_gen = 0;
    g_sweeps = 0;
    g_probed = false;
    g_now = 0;
    g_rng = seed * 0x2545F4914F6CDD1Dull + 1;

    for (uint8_t n = 0; n < NS_NODES; n++) {
        uint8_t s[32];
        rnd_bytes(s, 32);
        vna_identity_create(&N[n].idn, s, 4, 1u << 16);
        for (uint8_t p = 0; p < NS_NODES; p++) {
            N[n].link[p].peer = p;
            N[n].link[p].initiator = n < p;
        }
    }

    /* the ledger: VFV minted once to the buyer; pools start empty */
    pay_ledger_init(&PL, NULL);
    pay_ledger_open(&PL, 100, PL.vfv_asset, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &acct_issuer);
    pay_ledger_open(&PL, 1, PL.vfv_asset, PAY_CAP_FINANCIAL, 0, &acct_a);
    pay_ledger_open(&PL, 2, PL.vfv_asset, PAY_CAP_FINANCIAL, 0, &acct_b);
    pay_ledger_open(&PL, 3, PL.vfv_asset, PAY_CAP_FINANCIAL, 0, &acct_escrow);
    pay_ledger_open(&PL, 4, PL.vfv_asset, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS, &acct_commons);
    pay_ledger_open(&PL, 5, PL.vfv_asset, PAY_CAP_FINANCIAL, 0, &acct_tax);
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++)
        pay_ledger_open(&PL, 6 + (uint32_t) i, PL.vfv_asset, PAY_CAP_FINANCIAL, 0, &acct_bucket[i]);
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    uint8_t u[16] = {'M', 'I', 'N', 'T'};
    pay_uetr_from_random(u, r.uetr);
    strcpy(r.e2e, "MINT-1");
    r.idem_key[0] = 'M';
    pay_ledger_issue(&PL, &r, acct_issuer, acct_a, START, &rc);

    /* the market */
    mk_hooks_t h;
    memset(&h, 0, sizeof h);
    h.settle = h_settle;
    h.verify = h_verify;
    mk_policy_t pol;
    mk_policy_default(&pol);
    pol.min_auto_release = 10;
    pol.max_auto_release = 3600;
    remit_tax = seed % 3 != 0;
    pol.facilitator_remits_tax = remit_tax;
    mk_init(&M, &h, &pol);
    mk_tick(&M, T0_S);
    vfv = (uint8_t) mk_ccy_find(&M, "VFV");
    uint8_t pseed[32];
    rnd_bytes(pseed, 32);
    mk_pay_init(&MP, &PL, acct_of, NULL, pseed, 10);
    mk_pay_bind(&MP, vfv, acct_escrow, acct_commons, acct_tax);

    static uint8_t sig[VNA_SIG_LEN];
    uint8_t d[32];
    mk_store_spec_t st;
    memset(&st, 0, sizeof st);
    strcpy(st.name, "B tensor service");
    id_of(NB, &st.owner);
    strcpy(st.region, "XA");
    st.fulfil_within = FULFIL_S;
    st.version = 1;
    mk_store_digest(&st, d);
    sign_digest(NB, d, sig);
    store_h = 0;
    mk_store_publish(&M, &st, sig, VNA_SIG_LEN, &store_h);
    mk_listing_spec_t ls;
    memset(&ls, 0, sizeof ls);
    ls.store = store_h;
    ls.kind = MK_KIND_SERVICE;
    strcpy(ls.title, "matvec+softmax 16x64");
    ls.ccy = vfv;
    ls.price = PRICE;
    ls.stock = MK_UNLIMITED;
    ls.auto_release = RELEASE_S;
    ls.version = 1;
    mk_listing_digest(&M, &ls, d);
    sign_digest(NB, d, sig);
    listing_h = 0;
    mk_listing_publish(&M, &ls, sig, VNA_SIG_LEN, &listing_h);
    strcpy(ls.title, "priority add-on");
    ls.price = PRICE2;
    ls.tax_class = 1;
    mk_listing_digest(&M, &ls, d);
    sign_digest(NB, d, sig);
    listing2_h = 0;
    mk_listing_publish(&M, &ls, sig, VNA_SIG_LEN, &listing2_h);
    /* operator-supplied sales tax on both lines, at different rates */
    mk_tax_rate_t tr;
    memset(&tr, 0, sizeof tr);
    strcpy(tr.region, "XA");
    tr.num = 7;
    tr.den = 100;
    mk_tax_set(&M, &tr);
    tr.tax_class = 1;
    tr.num = 1;
    tr.den = 8;
    tr.rounding = MK_ROUND_CEIL;
    mk_tax_set(&M, &tr);
    mk_id_t a;
    id_of(NA, &a);
    cart_h = (uint16_t) mk_cart_open(&M, &a);

    /* the provider's weights and the buyer's jobs with their references */
    zt_fx v[ROWS * NB_BLK * ZT_BLOCK];
    rand_fx(&g_rng, v, ROWS * NB_BLK * ZT_BLOCK);
    for (uint32_t r2 = 0; r2 < ROWS; r2++)
        zt_quantize(v + r2 * NB_BLK * ZT_BLOCK, NB_BLK * ZT_BLOCK, W + r2 * NB_BLK, false);
    n_jobs = 8 + ns_below(&g_rng, MAX_JOBS - 8 + 1);
    for (uint32_t k = 0; k < n_jobs; k++) {
        job_t *j = &JOBS[k];
        j->start_ms = 200 + ns_below(&g_rng, LAUNCH_MS);
        j->qty = 1 + ns_below(&g_rng, 3);
        rand_fx(&g_rng, v, NB_BLK * ZT_BLOCK);
        zt_quantize(v, NB_BLK * ZT_BLOCK, j->x, false);
        tensor_job(j->x, j->ref);
    }
}

/* every other seed gets one partition long enough to force the escrow
 * timeout; the rest are short */
static uint32_t make_faults(uint64_t seed, fault_t *part, uint32_t *n_part, fault_t *reset)
{
    uint32_t np = 0;
    static const uint8_t links[3][2] = {{NA, NB}, {NA, NC}, {NB, NC}};
    if (seed % 2 == 1) {
        const uint8_t *l = links[0]; /* A-B: the job or its result is held up */
        part[np++] = (fault_t){5000 + ns_below(&g_rng, 15000), (FULFIL_S + 8) * 1000u, l[0], l[1]};
    }
    uint32_t k = 2 + ns_below(&g_rng, 2);
    for (uint32_t i = 0; i < k; i++) {
        const uint8_t *l = links[ns_below(&g_rng, 3)];
        part[np++] =
            (fault_t){2000 + ns_below(&g_rng, 50000), 500 + ns_below(&g_rng, 6000), l[0], l[1]};
    }
    *n_part = np;
    uint32_t nr = 3 + ns_below(&g_rng, 3);
    for (uint32_t i = 0; i < nr; i++) {
        const uint8_t *l = links[ns_below(&g_rng, 3)];
        bool flip = ns_below(&g_rng, 2);
        reset[i] =
            (fault_t){1500 + ns_below(&g_rng, 60000), 0, flip ? l[1] : l[0], flip ? l[0] : l[1]};
    }
    return nr;
}

static bool finished(void)
{
    if (g_now < LAUNCH_MS + 1000u || !all_acked()) return false;
    for (uint32_t k = 0; k < n_jobs; k++)
        if (!handle_of_job[k] || !terminal(state_of(handle_of_job[k])) || JOBS[k].st != J_DONE)
            return false;
    return true;
}

static outcome_t run_seed(uint64_t seed, bool faults)
{
    setup_world(seed);
    ns_profile_t prof = {0};
    if (faults) {
        prof.drop_ppm = ns_below(&g_rng, 150000);
        prof.dup_ppm = 5000 + ns_below(&g_rng, 30000);
        prof.reorder_ppm = 50000 + ns_below(&g_rng, 150000);
        prof.base_ms = 5 + ns_below(&g_rng, 40);
        prof.jitter_ms = ns_below(&g_rng, 60);
        prof.reorder_extra_ms = 50 + ns_below(&g_rng, 250);
    } else {
        prof.base_ms = 10;
    }
    ns_init(&NET, seed, &prof);
    fault_t part[8], reset[8];
    uint32_t n_part = 0, n_reset = 0;
    if (faults) n_reset = make_faults(seed, part, &n_part, reset);

    uint64_t next_cycle = CYCLE_MS;
    for (g_now = 0; g_now < CAP_MS; g_now += STEP_MS) {
        for (uint32_t i = 0; i < n_part; i++)
            if (g_now == part[i].at - part[i].at % STEP_MS) {
                ns_partition(&NET, part[i].a, part[i].b, g_now + part[i].len);
                S.partitions++;
            }
        for (uint32_t i = 0; i < n_reset; i++)
            if (g_now == reset[i].at - reset[i].at % STEP_MS) {
                teardown(reset[i].a, reset[i].b);
                S.resets++;
            }
        ns_pkt_t *p;
        while ((p = ns_poll(&NET, g_now)) != NULL) {
            on_packet(p);
            ns_done(p);
        }
        a_step();
        for (uint8_t a = 0; a < NS_NODES; a++)
            for (uint8_t b = 0; b < NS_NODES; b++)
                if (a != b) pump_link(a, b);
        if (g_now >= next_cycle) {
            next_cycle += CYCLE_MS;
            clearing_cycle(faults && g_now < LAUNCH_MS + 30000u);
            if (finished()) break;
        }
    }

    /* E4: the end state */
    outcome_t o;
    memset(&o, 0, sizeof o);
    if (!finished() || NET.next_id >= (uint64_t) sizeof g_accepted * 8) S.end_fail++;
    for (uint32_t k = 0; k < n_jobs; k++) {
        const mk_order_t *ord = mk_order(&M, handle_of_job[k]);
        const job_t *j = &JOBS[k];
        if (!ord) {
            S.end_fail++;
            continue;
        }
        o.states[k] = (uint8_t) ord->state;
        if (ord->held_base + ord->held_tax) S.end_fail++;
        if (j->fulfilled_by_b && !j->result_ok) S.end_fail++; /* a result never reached A */
        if (ord->state == MK_ORD_COMPLETED) {
            S.completed++;
            if (!j->fulfilled_by_b || ord->to_seller == 0) S.end_fail++;
        }
        if (ord->state == MK_ORD_REFUNDED) {
            S.refunded++;
            if (ord->to_seller || ord->to_buyer != ord->captured || ord->fee_charged) S.end_fail++;
        }
        if (ord->state == MK_ORD_EXPIRED) S.expired++;
    }
    if (bal(acct_escrow) != 0 || bal(acct_commons) != 0) S.end_fail++;
    if (!conserved()) S.conserve_fail++;
    memcpy(o.chain, PL.chain_head, sizeof o.chain);
    o.net[0] = NET.sent;
    o.net[1] = NET.dropped;
    o.net[2] = NET.duplicated;
    o.net[3] = NET.reordered;
    o.net[4] = S.handshakes;
    o.bal[0] = bal(acct_a);
    o.bal[1] = bal(acct_b);
    o.bal[2] = S.swept;
    return o;
}

int main(int argc, char **argv)
{
    tier_begin("tier2/integ_e2e", NULL, 0);
    uint32_t seeds = argc > 1 ? (uint32_t) strtoul(argv[1], NULL, 10) : 8u;
    if (seeds < 2) seeds = 2;

    /* a clean network first: every job completes and is paid exactly once */
    run_seed(1000, false);
    CHECK(S.completed == n_jobs && S.refunded == 0 && S.end_fail == 0 && S.conserve_fail == 0 &&
              S.result_bad == 0 && S.cycles > 0,
          "clean network: %llu/%u jobs completed, books conserved",
          (unsigned long long) S.completed, n_jobs);
    uint64_t want_fee = 0, want_seller = 0, want_tax = 0;
    for (uint32_t k = 0; k < n_jobs; k++) {
        const mk_order_t *o = mk_order(&M, handle_of_job[k]);
        uint64_t net = (uint64_t) PRICE * JOBS[k].qty + (k % 2 ? PRICE2 : 0);
        uint64_t tax =
            ((uint64_t) PRICE * JOBS[k].qty * 7 + 50) / 100 + (k % 2 ? (PRICE2 + 7) / 8 : 0);
        want_fee += pay_assure_fee(net);
        want_seller += net - pay_assure_fee(net) + (remit_tax ? 0 : tax);
        want_tax += remit_tax ? tax : 0;
        if (!o || o->total != net + tax || o->n_lines != (k % 2 ? 2 : 1)) want_tax = UINT64_MAX;
    }
    CHECK(bal(acct_b) == want_seller && S.swept == want_fee && bal(acct_tax) == want_tax &&
              S.settle_bad == 0,
          "clean network: seller holds price - 0.08889%% fee, buckets hold the fee, tax %s",
          remit_tax ? "remitted by the facilitator" : "kept by the seller");

    /* the harness can fail: one minor unit out of thin air breaks E1 */
    PL.acct[acct_b].debit++;
    PL.acct[acct_b].equity++;
    CHECK(!conserved(), "a one-unit leak is caught by the conservation check");
    PL.acct[acct_b].debit--;
    PL.acct[acct_b].equity--;

    stats_t tot;
    memset(&tot, 0, sizeof tot);
    outcome_t first;
    memset(&first, 0, sizeof first);
    uint32_t jobs_total = 0;
    for (uint32_t i = 0; i < seeds; i++) {
        uint64_t seed = i + 1;
        outcome_t o = run_seed(seed, true);
        if (i == 0) first = o;
        jobs_total += n_jobs;
        bool ok = S.conserve_fail == 0 && S.rollback_fail == 0 && S.result_bad == 0 &&
                  S.settle_bad == 0 && S.probes_refused == 2 && S.end_fail == 0 &&
                  S.stale_accepted == 0 && S.dup_accepted == 0;
        CHECK(ok,
              "seed %llu: %llu cycles conserved, %llu/%llu/%llu completed/refunded/expired, "
              "net sent %llu lost %llu dup %llu reord %llu, %llu handshakes, %llu resets, "
              "%llu refused settlements rolled back (fails: cons %llu rb %llu res %llu end %llu "
              "stale %llu dup %llu)",
              (unsigned long long) seed, (unsigned long long) S.cycles,
              (unsigned long long) S.completed, (unsigned long long) S.refunded,
              (unsigned long long) S.expired, (unsigned long long) NET.sent,
              (unsigned long long) NET.dropped, (unsigned long long) NET.duplicated,
              (unsigned long long) NET.reordered, (unsigned long long) S.handshakes,
              (unsigned long long) S.resets, (unsigned long long) S.refused,
              (unsigned long long) S.conserve_fail, (unsigned long long) S.rollback_fail,
              (unsigned long long) S.result_bad, (unsigned long long) S.end_fail,
              (unsigned long long) S.stale_accepted, (unsigned long long) S.dup_accepted);
        if (seed % 2 == 1)
            CHECK(S.refunded > 0 && S.timeouts >= S.refunded,
                  "seed %llu: the long partition forced %llu escrow timeouts",
                  (unsigned long long) seed, (unsigned long long) S.timeouts);
        tot.cycles += S.cycles;
        tot.completed += S.completed;
        tot.refunded += S.refunded;
        tot.refused += S.refused;
        tot.freezes += S.freezes;
        tot.resets += S.resets;
        tot.reconnect_dead += S.reconnect_dead;
        tot.stale_tried += S.stale_tried;
        tot.dup_tried += S.dup_tried;
        tot.dup_suppressed += S.dup_suppressed;
        tot.late_fulfil_refused += S.late_fulfil_refused;
        tot.partitions += S.partitions;
    }
    printf("  %u seeds, %u jobs: %llu completed, %llu refunded, %llu clearing cycles\n", seeds,
           jobs_total, (unsigned long long) tot.completed, (unsigned long long) tot.refunded,
           (unsigned long long) tot.cycles);
    printf("  faults exercised: %llu partitions, %llu session resets (%llu found by heartbeat), "
           "%llu freezes, %llu settlements refused and rolled back, %llu stale-session records "
           "and %llu duplicate datagrams refused, %llu duplicate messages suppressed, "
           "%llu late fulfilments refused\n",
           (unsigned long long) tot.partitions, (unsigned long long) tot.resets,
           (unsigned long long) tot.reconnect_dead, (unsigned long long) tot.freezes,
           (unsigned long long) tot.refused, (unsigned long long) tot.stale_tried,
           (unsigned long long) tot.dup_tried, (unsigned long long) tot.dup_suppressed,
           (unsigned long long) tot.late_fulfil_refused);
    CHECK(tot.completed > 0 && tot.refunded > 0, "both outcomes occur: completed and refunded");
    CHECK(tot.stale_tried > 0 && tot.resets > 0 && tot.reconnect_dead > 0,
          "PQ reconnects happened and stale-session records were tried");
    CHECK(tot.refused > 0 && tot.freezes > 0,
          "the ledger refused settlements (rollback exercised)");
    CHECK(tot.dup_tried > 0 && tot.dup_suppressed > 0, "duplicates reached both layers");

    /* E6: the same seed gives the same run */
    outcome_t again = run_seed(1, true);
    CHECK(memcmp(&first, &again, sizeof first) == 0, "seed 1 replays bit for bit");
    return tier_end();
}
