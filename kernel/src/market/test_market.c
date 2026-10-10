/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_market.c — host tests for kernel/src/market: every order state
 * transition (and every disallowed one), escrow conservation against an
 * independent mock ledger, refund exactness, fee exactness against an
 * independent reference, tax, inventory, bookings, digital delivery,
 * disputes, returns, reviews, ISF discovery through concord, agent
 * confirmation tokens, subscriptions without traps, B2B POs and invoices,
 * and a real ML-DSA-65 signed storefront. */
#include <stdio.h>
#include <string.h>
#include "mk_internal.h"
#include "mk_concord.h"
#include "mk_pq.h"
#include "pq_security.h"
#include "pay_assure.h"

_Static_assert(MK_PQ_PK_BYTES == PQ_MLDSA65_PK_BYTES, "pk size");
_Static_assert(MK_PQ_SK_BYTES == PQ_MLDSA65_SK_BYTES, "sk size");
_Static_assert(MK_PQ_SIG_BYTES == PQ_MLDSA65_SIG_BYTES, "sig size");

static int g_fail, g_pass;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

/* ===== independent assurance-fee reference (host-only __int128) ===== */
__extension__ typedef unsigned __int128 u128;
static uint64_t ref_fee(uint64_t a)
{
    return (uint64_t) ((u128) a * 8889u / 10000000u);
}

/* ===== mock identities and signatures =====
 * MOCK: sig = SHA3-256("mocksig" || signer || msg). It proves the plumbing,
 * not unforgeability; the real scheme is tested with ML-DSA-65 below. */
static mk_id_t ID(const char *name)
{
    mk_id_t id;
    pay_sha3_256((const uint8_t *) name, strlen(name), id.b);
    return id;
}
static void mock_sign(const mk_id_t *signer, const uint8_t *msg, uint32_t len, uint8_t sig[32])
{
    uint8_t buf[256];
    memcpy(buf, "mocksig", 7);
    memcpy(buf + 7, signer->b, 32);
    memcpy(buf + 39, msg, len);
    pay_sha3_256(buf, 39 + len, sig);
}

/* ===== mock ledger: an independent check of every settlement ===== */
#define NP 32
typedef struct {
    mk_id_t who[NP];
    int64_t bal[NP];
    uint32_t n;
    int64_t escrow, commons, tax;
    int64_t minted; /* initial balances */
    bool fail_next;
    uint32_t settles;
    mk_settle_kind_t last_kind;
    /* hooks state */
    con_commons_t *concord;
    mk_pq_keyring_t *ring;
    bool use_pq;
    uint32_t notes[16];
    uint32_t review_w;
    mk_id_t jurors[MK_MAX_JURORS];
    uint32_t n_jurors;
    mk_pain001_req_t last_pain;
    uint64_t last_pain_amount;
    bool kyc_block;
} ledger_t;
static ledger_t LG;

static int64_t *acct(const mk_id_t *id)
{
    for (uint32_t i = 0; i < LG.n; i++)
        if (!memcmp(LG.who[i].b, id->b, 32)) return &LG.bal[i];
    memcpy(LG.who[LG.n].b, id->b, 32);
    LG.bal[LG.n] = 0;
    return &LG.bal[LG.n++];
}
static void fund(const mk_id_t *id, int64_t v)
{
    *acct(id) += v;
    LG.minted += v;
}
static int64_t bal(const char *name)
{
    mk_id_t id = ID(name);
    return *acct(&id);
}
static bool ledger_total_ok(void)
{
    int64_t t = LG.escrow + LG.commons + LG.tax;
    for (uint32_t i = 0; i < LG.n; i++) t += LG.bal[i];
    return t == LG.minted;
}

static bool h_settle(void *ctx, const mk_settle_t *s)
{
    (void) ctx;
    LG.settles++;
    if (LG.fail_next) {
        LG.fail_next = false;
        return false;
    }
    LG.last_kind = s->kind;
    int64_t *b = acct(s->buyer), *sl = acct(s->seller);
    if (s->kind == MK_SETTLE_INVOICE) {
        /* direct: no escrow */
        *b -= (int64_t) s->from_buyer;
        *sl -= (int64_t) s->from_seller;
        *sl += (int64_t) s->to_seller;
        LG.commons += (int64_t) s->to_commons;
        LG.tax += (int64_t) s->to_tax;
        int64_t in = (int64_t) (s->from_buyer + s->from_seller);
        int64_t out = (int64_t) (s->to_seller + s->to_commons + s->to_tax + s->to_buyer);
        LG.escrow += in - out; /* must stay 0 for invoices */
        *b += (int64_t) s->to_buyer;
        return true;
    }
    *b -= (int64_t) s->from_buyer;
    *sl -= (int64_t) s->from_seller;
    LG.commons -= (int64_t) s->from_commons;
    LG.tax -= (int64_t) s->from_tax;
    LG.escrow += (int64_t) (s->from_buyer + s->from_seller + s->from_commons + s->from_tax);
    LG.escrow -= (int64_t) (s->to_seller + s->to_commons + s->to_tax + s->to_buyer);
    *sl += (int64_t) s->to_seller;
    *b += (int64_t) s->to_buyer;
    LG.commons += (int64_t) s->to_commons;
    LG.tax += (int64_t) s->to_tax;
    return LG.escrow >= 0;
}

static bool h_verify(void *ctx, const mk_id_t *signer, const uint8_t *msg, uint32_t len,
                     const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    if (LG.use_pq) return mk_pq_verify(LG.ring, signer, msg, len, sig, sig_len);
    uint8_t want[32];
    if (sig_len != 32 || len > 200) return false;
    mock_sign(signer, msg, len, want);
    return !memcmp(want, sig, 32);
}
static bool h_delivery(void *ctx, uint32_t order, const uint8_t *p, uint32_t n)
{
    (void) ctx;
    (void) order;
    return n == 8 && !memcmp(p, "TRACK-OK", 8);
}
static uint32_t h_isf(void *ctx, uint32_t a, uint32_t b)
{
    (void) ctx;
    return mk_concord_isf(LG.concord, a, b);
}
static bool h_see(void *ctx, uint32_t a, uint32_t b)
{
    (void) ctx;
    return mk_concord_can_see(LG.concord, a, b);
}
static uint32_t h_weight(void *ctx, const mk_id_t *r, uint16_t s)
{
    (void) ctx;
    (void) r;
    (void) s;
    return LG.review_w;
}
static uint32_t h_jurors(void *ctx, uint32_t order, mk_id_t *out, uint32_t max)
{
    (void) ctx;
    (void) order;
    uint32_t n = LG.n_jurors < max ? LG.n_jurors : max;
    memcpy(out, LG.jurors, n * sizeof(mk_id_t));
    return n;
}
static int32_t h_pain(void *ctx, const mk_pain001_req_t *rq, char *out, uint32_t cap)
{
    (void) ctx;
    memcpy(&LG.last_pain, rq, sizeof *rq);
    LG.last_pain_amount = rq->amount;
    int n = snprintf(out, cap, "<pain.001 ccy=%s amt=%llu e2e=%s/>", rq->ccy_alpha,
                     (unsigned long long) rq->amount, rq->end_to_end_id);
    return n < (int) cap ? n : -1;
}
static void h_note(void *ctx, mk_note_t k, uint32_t ref)
{
    (void) ctx;
    (void) ref;
    LG.notes[k]++;
}
static bool h_party(void *ctx, const mk_id_t *b, const mk_id_t *s, uint8_t c, uint64_t t)
{
    (void) ctx;
    (void) b;
    (void) s;
    (void) c;
    return !(LG.kyc_block && t > 100000);
}

static mk_market_t M; /* large: static */
static con_commons_t CC;

static void setup(void)
{
    memset(&LG, 0, sizeof LG);
    LG.review_w = MK_Q16;
    LG.concord = &CC;
    mk_hooks_t h;
    memset(&h, 0, sizeof h);
    h.verify = h_verify;
    h.settle = h_settle;
    h.verify_delivery = h_delivery;
    h.isf = h_isf;
    h.can_see = h_see;
    h.review_weight = h_weight;
    h.select_jurors = h_jurors;
    h.pain001 = h_pain;
    h.notify = h_note;
    h.party_ok = h_party;
    mk_init(&M, &h, NULL);
    mk_tick(&M, 1000000);
    con_init(&CC);
}

static bool all_ok(void)
{
    if (!mk_market_conserved(&M) || !ledger_total_ok()) return false;
    /* the mock escrow equals the sum of what orders say they hold */
    int64_t held = 0;
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++)
        if (M.order[i].used) held += (int64_t) (M.order[i].held_base + M.order[i].held_tax);
    return held == LG.escrow;
}

/* ----- fixtures ----- */
static uint16_t open_store(const char *owner, const char *name, uint32_t concord,
                           const char *arbiter)
{
    mk_store_spec_t s;
    memset(&s, 0, sizeof s);
    strcpy(s.name, name);
    s.owner = ID(owner);
    strcpy(s.region, "XA");
    s.concord_id = concord;
    if (arbiter) {
        s.has_arbiter = true;
        s.arbiter = ID(arbiter);
    }
    s.version = 1;
    uint8_t d[32], sig[32];
    mk_store_digest(&s, d);
    mock_sign(&s.owner, d, 32, sig);
    uint16_t h = 0;
    mk_status_t rc = mk_store_publish(&M, &s, sig, 32, &h);
    CHECK(rc == MK_OK);
    return h;
}

static void spec_init(mk_listing_spec_t *l, uint16_t store, mk_kind_t k, uint64_t price)
{
    memset(l, 0, sizeof *l);
    l->store = store;
    l->kind = k;
    strcpy(l->title, "thing");
    l->category = 3;
    l->ccy = 1; /* VFV */
    l->price = price;
    l->stock = 10;
    l->return_window = 30 * 86400;
    l->auto_release = 3 * 86400;
    l->version = 1;
    if (k == MK_KIND_DIGITAL)
        strcpy(l->cid, "bafybeigdyrzt5sfp7udm7hu76uh7y26nf3efuylqabf3oclgtqy55fbzdi");
    if (k == MK_KIND_SUBSCRIPTION) l->period = 30 * 86400;
    if (k == MK_KIND_BOOKING) {
        l->n_slots = 2;
        l->slot[0].start = M.now + 10 * 86400;
        l->slot[0].duration = 3600;
        l->slot[0].capacity = 3;
        l->slot[1].start = M.now + 20 * 86400;
        l->slot[1].duration = 3600;
        l->slot[1].capacity = 1;
        l->cancel_fee = price / 4;
        l->cancel_cutoff = 2 * 86400;
    }
}

static mk_status_t publish(const char *owner, const mk_listing_spec_t *l, uint16_t *h)
{
    uint8_t d[32], sig[32];
    mk_listing_digest(&M, l, d);
    mk_id_t o = ID(owner);
    mock_sign(&o, d, 32, sig);
    return mk_listing_publish(&M, l, sig, 32, h);
}

static uint16_t listing(const char *owner, uint16_t store, mk_kind_t k, uint64_t price)
{
    mk_listing_spec_t l;
    spec_init(&l, store, k, price);
    uint16_t h = 0;
    CHECK(publish(owner, &l, &h) == MK_OK);
    return h;
}

/* One-line order for `buyer`. */
static uint16_t order1(const char *buyer, uint16_t lst, uint32_t qty, uint8_t slot)
{
    mk_id_t b = ID(buyer);
    int32_t c = mk_cart_open(&M, &b);
    CHECK(c > 0);
    CHECK(mk_cart_add(&M, (uint16_t) c, lst, qty, slot) == MK_OK);
    mk_checkout_opts_t o;
    memset(&o, 0, sizeof o);
    strcpy(o.region, "XA");
    uint16_t out[4];
    int32_t n = mk_checkout(&M, (uint16_t) c, &o, out, 4);
    CHECK(n == 1);
    M.cart[c - 1].used = false;
    return n == 1 ? out[0] : 0;
}

static mk_order_state_t st(uint16_t o)
{
    return mk_order(&M, o)->state;
}

/* ===== 1. assurance fee ===== */
static void test_fee_reference(void)
{
    uint64_t v[] = {0,
                    1,
                    1124,
                    1125,
                    100,
                    123,
                    999,
                    12345,
                    1000000,
                    999999999999ull,
                    0xFFFFFFFFull,
                    0x0FFFFFFFFFFFFFFFull,
                    0xFFFFFFFFFFFFFFFFull};
    for (uint32_t i = 0; i < sizeof v / sizeof v[0]; i++)
        CHECK(pay_assure_fee(v[i]) == ref_fee(v[i]));
    uint64_t x = 88172645463325252ull;
    for (int i = 0; i < 2000; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        uint64_t a = x >> (i % 40 + 4);
        if (pay_assure_fee(a) != ref_fee(a)) {
            CHECK(0);
            break;
        }
    }
    CHECK(pay_assure_fee(10000) == 8 && pay_assure_fee(1124) == 0 && pay_assure_fee(1125) == 1);
}

/* ===== 2. the transition table against an independent list ===== */
static void test_table(void)
{
    struct {
        mk_order_state_t f;
        mk_event_t e;
        mk_order_state_t t;
    } edges[] = {
        {MK_ORD_PENDING_PAYMENT, MK_EV_PAY, MK_ORD_PAID},
        {MK_ORD_PENDING_PAYMENT, MK_EV_CANCEL, MK_ORD_CANCELLED},
        {MK_ORD_PENDING_PAYMENT, MK_EV_EXPIRE, MK_ORD_EXPIRED},
        {MK_ORD_PAID, MK_EV_CANCEL, MK_ORD_REFUNDED},
        {MK_ORD_PAID, MK_EV_FULFIL, MK_ORD_FULFILLED},
        {MK_ORD_PAID, MK_EV_CONFIRM, MK_ORD_COMPLETED},
        {MK_ORD_PAID, MK_EV_FULFIL_TIMEOUT, MK_ORD_REFUNDED},
        {MK_ORD_PAID, MK_EV_DISPUTE, MK_ORD_DISPUTED},
        {MK_ORD_PAID, MK_EV_REFUND_ALL, MK_ORD_REFUNDED},
        {MK_ORD_FULFILLED, MK_EV_CONFIRM, MK_ORD_COMPLETED},
        {MK_ORD_FULFILLED, MK_EV_AUTO_RELEASE, MK_ORD_COMPLETED},
        {MK_ORD_FULFILLED, MK_EV_DISPUTE, MK_ORD_DISPUTED},
        {MK_ORD_FULFILLED, MK_EV_REQUEST_RETURN, MK_ORD_RETURN_OPEN},
        {MK_ORD_FULFILLED, MK_EV_REFUND_ALL, MK_ORD_REFUNDED},
        {MK_ORD_COMPLETED, MK_EV_REQUEST_RETURN, MK_ORD_RETURN_OPEN},
        {MK_ORD_COMPLETED, MK_EV_REFUND_ALL, MK_ORD_REFUNDED},
        {MK_ORD_DISPUTED, MK_EV_RULING, MK_ORD_RESOLVED},
        {MK_ORD_RETURN_OPEN, MK_EV_RETURN_RECEIVED, MK_ORD_STATE_COUNT},
        {MK_ORD_RETURN_OPEN, MK_EV_RETURN_DECLINED, MK_ORD_STATE_COUNT},
        {MK_ORD_RETURN_OPEN, MK_EV_REFUND_ALL, MK_ORD_REFUNDED},
    };
    uint32_t ne = sizeof edges / sizeof edges[0], allowed = 0, ok = 1;
    for (uint32_t s = 0; s < MK_ORD_STATE_COUNT; s++)
        for (uint32_t e = 0; e < MK_EV_COUNT; e++) {
            mk_order_state_t want = MK_ORD_FREE;
            for (uint32_t i = 0; i < ne; i++)
                if (edges[i].f == (mk_order_state_t) s && edges[i].e == (mk_event_t) e)
                    want = edges[i].t;
            mk_order_state_t got = mk_order_next((mk_order_state_t) s, (mk_event_t) e);
            if (got != want) {
                ok = 0;
                printf("  table %s ev %u: got %s\n", mk_order_state_name((mk_order_state_t) s), e,
                       mk_order_state_name(got));
            }
            allowed += got != MK_ORD_FREE;
        }
    CHECK(ok);
    CHECK(allowed == ne);
    CHECK(mk_order_next(MK_ORD_STATE_COUNT, MK_EV_PAY) == MK_ORD_FREE);
    CHECK(mk_order_next(MK_ORD_PAID, MK_EV_COUNT) == MK_ORD_FREE);
}

/* Every API-reachable event NOT allowed in the order's current state must be
 * refused without changing the state or any money. */
static void probe_illegal(uint16_t o, const char *buyer, const char *seller)
{
    mk_id_t b = ID(buyer), s = ID(seller);
    mk_order_state_t s0 = st(o);
    mk_order_t snap;
    memcpy(&snap, mk_order(&M, o), sizeof snap);
    for (uint32_t e = 0; e < MK_EV_COUNT; e++) {
        if (mk_order_next(s0, (mk_event_t) e) != MK_ORD_FREE) continue;
        mk_status_t rc = MK_OK;
        switch ((mk_event_t) e) {
        case MK_EV_PAY:
            rc = mk_order_pay(&M, o, &b);
            break;
        case MK_EV_CANCEL:
            rc = mk_order_cancel(&M, o, &b);
            break;
        case MK_EV_FULFIL:
            rc = mk_order_fulfil(&M, o, &s, (const uint8_t *) "TRACK-OK", 8);
            break;
        case MK_EV_CONFIRM:
            rc = mk_order_confirm(&M, o, &b);
            break;
        case MK_EV_DISPUTE:
            rc = mk_dispute_open(&M, o, &b);
            break;
        case MK_EV_RULING:
            rc = mk_dispute_rule(&M, o, &s, 0, (const uint8_t *) "x", 1);
            break;
        case MK_EV_REQUEST_RETURN:
            rc = mk_return_request(&M, o, &b, 0, 1);
            break;
        case MK_EV_RETURN_RECEIVED:
            rc = mk_return_received(&M, o, &s, false);
            break;
        case MK_EV_RETURN_DECLINED:
            rc = mk_return_decline(&M, o, &s);
            break;
        case MK_EV_REFUND_ALL:
            rc = mk_order_refund_line(&M, o, &s, 0, 1);
            break;
        default:
            continue; /* timers: not callable */
        }
        if (rc != MK_ERR_STATE) {
            printf("  illegal ev %u in %s returned %d\n", e, mk_order_state_name(s0), rc);
            CHECK(0);
        }
    }
    CHECK(st(o) == s0);
    CHECK(!memcmp(&snap, mk_order(&M, o), sizeof snap));
    CHECK(all_ok());
}

/* ===== 3. every transition through the API ===== */
static void test_transitions(void)
{
    setup();
    mk_id_t B = ID("bea"), S = ID("sam"), X = ID("mallory");
    fund(&B, 100000000);
    uint16_t store = open_store("sam", "Sam's", 0, "arbiter");
    uint16_t L = listing("sam", store, MK_KIND_PHYSICAL, 1000);
    uint32_t transitions = 0;

    /* PENDING -> PAID, with guards */
    uint16_t o = order1("bea", L, 2, 0);
    CHECK(st(o) == MK_ORD_PENDING_PAYMENT);
    probe_illegal(o, "bea", "sam");
    CHECK(mk_order_pay(&M, o, &X) == MK_ERR_AUTH);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK && st(o) == MK_ORD_PAID);
    transitions++;
    CHECK(all_ok());
    probe_illegal(o, "bea", "sam");
    /* PAID -> FULFILLED -> COMPLETED (buyer confirm) */
    CHECK(mk_order_fulfil(&M, o, &B, (const uint8_t *) "TRACK-OK", 8) == MK_ERR_AUTH);
    CHECK(mk_order_fulfil(&M, o, &S, (const uint8_t *) "TRACK-OK", 8) == MK_OK);
    CHECK(st(o) == MK_ORD_FULFILLED && mk_order(&M, o)->proof_verified);
    transitions++;
    probe_illegal(o, "bea", "sam");
    CHECK(mk_order_confirm(&M, o, &S) == MK_ERR_AUTH);
    CHECK(mk_order_confirm(&M, o, &B) == MK_OK && st(o) == MK_ORD_COMPLETED);
    transitions++;
    CHECK(all_ok());
    probe_illegal(o, "bea", "sam");
    CHECK(mk_order(&M, o)->fee_charged == ref_fee(2000));
    CHECK(bal("sam") == 2000 - (int64_t) ref_fee(2000));
    CHECK(LG.notes[MK_NOTE_COMPLETED] == 1);

    /* COMPLETED -> RETURN_OPEN -> back (decline) -> RETURN_OPEN -> REFUNDED */
    CHECK(mk_return_request(&M, o, &B, 0, 1) == MK_OK && st(o) == MK_ORD_RETURN_OPEN);
    transitions++;
    probe_illegal(o, "bea", "sam");
    CHECK(mk_return_decline(&M, o, &S) == MK_OK && st(o) == MK_ORD_COMPLETED);
    transitions++;
    CHECK(mk_return_request(&M, o, &B, 0, 2) == MK_OK);
    CHECK(mk_return_shipped(&M, o, &B) == MK_OK);
    CHECK(mk_return_received(&M, o, &S, true) == MK_OK && st(o) == MK_ORD_REFUNDED);
    transitions++;
    CHECK(all_ok());
    CHECK(mk_order(&M, o)->fee_charged == 0);
    CHECK(bal("bea") == 100000000);
    CHECK(bal("sam") == 0 && LG.commons == 0);
    CHECK(mk_available(&M, L) == 10); /* restocked */
    probe_illegal(o, "bea", "sam");

    /* PENDING -> CANCELLED */
    o = order1("bea", L, 3, 0);
    CHECK(mk_available(&M, L) == 7);
    CHECK(mk_order_cancel(&M, o, &X) == MK_ERR_AUTH);
    CHECK(mk_order_cancel(&M, o, &B) == MK_OK && st(o) == MK_ORD_CANCELLED);
    transitions++;
    CHECK(mk_available(&M, L) == 10);
    probe_illegal(o, "bea", "sam");

    /* PENDING -> EXPIRED */
    o = order1("bea", L, 1, 0);
    mk_tick(&M, M.now + M.policy.payment_window);
    CHECK(st(o) == MK_ORD_EXPIRED);
    transitions++;
    CHECK(mk_available(&M, L) == 10);
    probe_illegal(o, "bea", "sam");

    /* PAID -> REFUNDED (cancel), stock restored */
    o = order1("bea", L, 4, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(M.listing[L - 1].spec.stock == 6);
    CHECK(mk_order_cancel(&M, o, &S) == MK_OK && st(o) == MK_ORD_REFUNDED);
    transitions++;
    CHECK(M.listing[L - 1].spec.stock == 10 && all_ok());
    CHECK(mk_order(&M, o)->fee_charged == 0);

    /* PAID -> COMPLETED (buyer confirms without a seller mark) */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_confirm(&M, o, &B) == MK_OK && st(o) == MK_ORD_COMPLETED);
    transitions++;

    /* PAID -> REFUNDED (fulfilment timeout) */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    mk_tick(&M, mk_order(&M, o)->fulfil_by);
    CHECK(st(o) == MK_ORD_REFUNDED && LG.notes[MK_NOTE_AUTO_CANCELLED] == 1);
    transitions++;
    CHECK(all_ok());

    /* PAID -> REFUNDED (seller refunds every line) */
    o = order1("bea", L, 2, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_refund_line(&M, o, &B, 0, 1) == MK_ERR_AUTH);
    CHECK(mk_order_refund_line(&M, o, &S, 0, 1) == MK_OK && st(o) == MK_ORD_PAID);
    CHECK(mk_order_refund_line(&M, o, &S, 0, 2) == MK_ERR_ARG);
    CHECK(mk_order_refund_line(&M, o, &S, 0, 1) == MK_OK && st(o) == MK_ORD_REFUNDED);
    transitions++;
    CHECK(all_ok());

    /* FULFILLED -> COMPLETED (auto-release), unverified proof doubles timer */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_fulfil(&M, o, &S, (const uint8_t *) "no-carrier", 10) == MK_OK);
    CHECK(!mk_order(&M, o)->proof_verified);
    CHECK(mk_order(&M, o)->release_at == M.now + 2u * 3u * 86400u);
    mk_tick(&M, M.now + 3 * 86400);
    CHECK(st(o) == MK_ORD_FULFILLED);
    mk_tick(&M, mk_order(&M, o)->release_at);
    CHECK(st(o) == MK_ORD_COMPLETED && LG.notes[MK_NOTE_AUTO_RELEASED] == 1);
    transitions++;
    CHECK(all_ok());

    /* FULFILLED -> RETURN_OPEN -> FULFILLED (partial, from escrow) -> REFUNDED */
    o = order1("bea", L, 3, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_fulfil(&M, o, &S, (const uint8_t *) "TRACK-OK", 8) == MK_OK);
    CHECK(mk_return_request(&M, o, &B, 0, 1) == MK_OK);
    CHECK(mk_return_shipped(&M, o, &B) == MK_OK);
    CHECK(mk_return_received(&M, o, &S, false) == MK_OK && st(o) == MK_ORD_FULFILLED);
    transitions++;
    CHECK(mk_order(&M, o)->held_base == 2000);
    CHECK(mk_order_refund_line(&M, o, &S, 0, 2) == MK_OK && st(o) == MK_ORD_REFUNDED);
    transitions++;
    CHECK(all_ok());

    /* RETURN_OPEN timeouts: seller silent -> refund; buyer never ships -> back */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_fulfil(&M, o, &S, (const uint8_t *) "TRACK-OK", 8) == MK_OK);
    CHECK(mk_return_request(&M, o, &B, 0, 1) == MK_OK);
    CHECK(mk_return_shipped(&M, o, &B) == MK_OK);
    mk_tick(&M, M.now + M.policy.return_accept);
    CHECK(st(o) == MK_ORD_REFUNDED);
    transitions++;
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_confirm(&M, o, &B) == MK_OK);
    CHECK(mk_return_request(&M, o, &B, 0, 1) == MK_OK);
    mk_tick(&M, M.now + M.policy.return_accept);
    CHECK(st(o) == MK_ORD_COMPLETED);
    transitions++;
    /* return window */
    mk_tick(&M, M.now + 31u * 86400u);
    CHECK(mk_return_request(&M, o, &B, 0, 1) == MK_ERR_WINDOW);
    CHECK(all_ok());

    /* post-release return where the seller cannot fund it: default recorded */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_confirm(&M, o, &B) == MK_OK);
    CHECK(mk_return_request(&M, o, &B, 0, 1) == MK_OK);
    CHECK(mk_return_shipped(&M, o, &B) == MK_OK);
    LG.fail_next = true;
    CHECK(mk_return_received(&M, o, &S, false) == MK_ERR_SETTLE);
    CHECK(st(o) == MK_ORD_COMPLETED && M.store[store - 1].seller_defaults == 1);
    CHECK(all_ok());

    /* FULFILLED -> DISPUTED -> RESOLVED (chosen arbiter) */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_fulfil(&M, o, &S, (const uint8_t *) "TRACK-OK", 8) == MK_OK);
    CHECK(mk_dispute_open(&M, o, &X) == MK_ERR_AUTH);
    CHECK(mk_dispute_open(&M, o, &B) == MK_OK && st(o) == MK_ORD_DISPUTED);
    transitions++;
    probe_illegal(o, "bea", "sam");
    uint8_t dg[32], ev[32] = {1}, sig[32];
    CHECK(mk_dispute_evidence(&M, o, &B, "bafyphoto", ev) == MK_OK);
    CHECK(mk_dispute_evidence(&M, o, &S, "bafyreceipt", ev) == MK_OK);
    CHECK(mk_dispute_evidence(&M, o, &X, "bafyspam", ev) == MK_ERR_AUTH);
    mk_tick(&M, M.now + M.policy.dispute_window + 1); /* no timer verdict */
    CHECK(st(o) == MK_ORD_DISPUTED);
    mk_id_t A = ID("arbiter");
    mk_ruling_digest(&M, o, 600, dg);
    mock_sign(&S, dg, 32, sig);
    CHECK(mk_dispute_rule(&M, o, &S, 600, sig, 32) == MK_ERR_AUTH);
    mock_sign(&A, dg, 32, sig);
    CHECK(mk_dispute_rule(&M, o, &A, 1001, sig, 32) == MK_ERR_ARG);
    CHECK(mk_dispute_rule(&M, o, &A, 500, sig, 32) == MK_ERR_AUTH); /* signed 600 */
    CHECK(mk_dispute_rule(&M, o, &A, 600, sig, 32) == MK_OK && st(o) == MK_ORD_RESOLVED);
    transitions++;
    CHECK(mk_order(&M, o)->to_buyer == 600);
    CHECK(mk_order(&M, o)->fee_charged == ref_fee(400));
    CHECK(all_ok());
    probe_illegal(o, "bea", "sam");

    /* PAID -> DISPUTED */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_dispute_open(&M, o, &S) == MK_OK && st(o) == MK_ORD_DISPUTED);
    transitions++;
    mk_ruling_digest(&M, o, 1000, dg);
    mock_sign(&A, dg, 32, sig);
    CHECK(mk_dispute_rule(&M, o, &A, 1000, sig, 32) == MK_OK);
    CHECK(mk_order(&M, o)->fee_charged == 0 && all_ok());

    CHECK(mk_restock(&M, &S, L, 20) == MK_OK);
    /* FULFILLED -> REFUNDED (seller refunds all, still in escrow) */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_fulfil(&M, o, &S, (const uint8_t *) "TRACK-OK", 8) == MK_OK);
    CHECK(mk_order_refund_line(&M, o, &S, 0, 1) == MK_OK && st(o) == MK_ORD_REFUNDED);
    transitions++;

    /* COMPLETED -> REFUNDED (goodwill refund of everything after release) */
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_order_confirm(&M, o, &B) == MK_OK);
    CHECK(mk_order_refund_line(&M, o, &S, 0, 1) == MK_OK && st(o) == MK_ORD_REFUNDED);
    transitions++;
    CHECK(mk_order(&M, o)->fee_charged == 0 && all_ok());

    /* atomicity: a refused settlement changes nothing */
    o = order1("bea", L, 1, 0);
    mk_order_t snap;
    memcpy(&snap, mk_order(&M, o), sizeof snap);
    LG.fail_next = true;
    CHECK(mk_order_pay(&M, o, &B) == MK_ERR_SETTLE);
    CHECK(!memcmp(&snap, mk_order(&M, o), sizeof snap));
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    LG.fail_next = true;
    CHECK(mk_order_confirm(&M, o, &B) == MK_ERR_SETTLE && st(o) == MK_ORD_PAID);
    CHECK(all_ok());

    /* self-dealing refused */
    mk_id_t Sm = ID("sam");
    int32_t c = mk_cart_open(&M, &Sm);
    CHECK(mk_cart_add(&M, (uint16_t) c, L, 1, 0) == MK_OK);
    mk_checkout_opts_t co;
    memset(&co, 0, sizeof co);
    uint16_t outs[2];
    CHECK(mk_checkout(&M, (uint16_t) c, &co, outs, 2) == MK_ERR_POLICY);
    M.cart[c - 1].used = false;
    CHECK(transitions == 22);
    printf("  transitions exercised through the API: %u\n", transitions);
}

/* ===== 4. community panel ===== */
static void test_community(void)
{
    setup();
    mk_id_t B = ID("bea"), S = ID("sam");
    fund(&B, 1000000);
    uint16_t store = open_store("sam", "Sam's", 0, NULL); /* no arbiter */
    uint16_t L = listing("sam", store, MK_KIND_PHYSICAL, 999);
    uint16_t o = order1("bea", L, 1, 0);
    CHECK(mk_order(&M, o)->community_mediation);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    LG.n_jurors = 0;
    CHECK(mk_dispute_open(&M, o, &B) == MK_ERR_POLICY);
    LG.jurors[0] = ID("j1");
    LG.jurors[1] = B; /* a party: must be excluded */
    LG.jurors[2] = ID("j2");
    LG.jurors[3] = ID("j3");
    LG.n_jurors = 4;
    CHECK(mk_dispute_open(&M, o, &B) == MK_OK);
    CHECK(mk_order(&M, o)->dispute.n_jurors == 2); /* policy 3 asked: j1, bea(x), j2 */
    mk_id_t j1 = ID("j1"), j2 = ID("j2"), j3 = ID("j3");
    uint8_t dg[32], sig[32];
    mk_ruling_digest(&M, o, 999, dg);
    mock_sign(&j1, dg, 32, sig);
    CHECK(mk_dispute_rule(&M, o, &j1, 999, sig, 32) == MK_OK && st(o) == MK_ORD_DISPUTED);
    CHECK(mk_dispute_rule(&M, o, &j1, 999, sig, 32) == MK_ERR_DUPLICATE);
    mock_sign(&j3, dg, 32, sig);
    CHECK(mk_dispute_rule(&M, o, &j3, 999, sig, 32) == MK_ERR_AUTH);
    mk_ruling_digest(&M, o, 300, dg);
    mock_sign(&j2, dg, 32, sig);
    CHECK(mk_dispute_rule(&M, o, &j2, 300, sig, 32) == MK_OK);
    CHECK(st(o) == MK_ORD_RESOLVED);
    CHECK(mk_order(&M, o)->dispute.buyer_award == 300); /* lower median of {300, 999} */
    CHECK(mk_order(&M, o)->fee_charged == ref_fee(699) && all_ok());

    /* deadline with a majority of a 3-panel */
    LG.jurors[1] = ID("j4");
    LG.n_jurors = 3;
    o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_dispute_open(&M, o, &S) == MK_OK && mk_order(&M, o)->dispute.n_jurors == 3);
    mk_ruling_digest(&M, o, 100, dg);
    mock_sign(&j1, dg, 32, sig);
    CHECK(mk_dispute_rule(&M, o, &j1, 100, sig, 32) == MK_OK);
    mk_tick(&M, mk_order(&M, o)->dispute.deadline);
    CHECK(st(o) == MK_ORD_DISPUTED); /* 1 of 3 is no majority */
    mk_ruling_digest(&M, o, 200, dg);
    mock_sign(&j2, dg, 32, sig);
    CHECK(mk_dispute_rule(&M, o, &j2, 200, sig, 32) == MK_OK);
    mk_tick(&M, M.now + 1);
    CHECK(st(o) == MK_ORD_RESOLVED && mk_order(&M, o)->dispute.buyer_award == 100);
    CHECK(all_ok());
}

/* ===== 5. refund exactness and fee exactness with tax ===== */
static void test_refund_exactness(void)
{
    uint32_t rates[][2] = {{20, 100}, {7, 100}, {19, 100}, {1, 3}, {0, 1}, {825, 10000}};
    uint64_t prices[] = {333, 1, 99, 1999, 123457, 7};
    uint32_t cases = 0, bad = 0;
    for (uint32_t ri = 0; ri < 6; ri++)
        for (uint32_t pi = 0; pi < 6; pi++)
            for (int incl = 0; incl < 2; incl++)
                for (int post = 0; post < 2; post++) {
                    setup();
                    M.policy.facilitator_remits_tax = (ri + pi) & 1;
                    mk_id_t B = ID("bea"), S = ID("sam");
                    fund(&B, 1000000000);
                    uint16_t store = open_store("sam", "S", 0, "arb");
                    mk_tax_rate_t tr;
                    memset(&tr, 0, sizeof tr);
                    strcpy(tr.region, "XA");
                    tr.tax_class = 0;
                    tr.num = rates[ri][0];
                    tr.den = rates[ri][1];
                    tr.inclusive = incl;
                    tr.tax_shipping = pi & 1;
                    tr.rounding = (mk_round_t) ((ri + pi) % 3);
                    CHECK(mk_tax_set(&M, &tr) > 0);
                    mk_listing_spec_t l;
                    spec_init(&l, store, MK_KIND_PHYSICAL, prices[pi]);
                    l.shipping = 17 * pi;
                    l.stock = 100;
                    uint16_t L = 0;
                    CHECK(publish("sam", &l, &L) == MK_OK);
                    uint16_t o = order1("bea", L, 7, 0);
                    const mk_order_t *od = mk_order(&M, o);
                    uint64_t lt = od->line[0].line_total, tx = od->line[0].tax;
                    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
                    if (post) CHECK(mk_order_confirm(&M, o, &B) == MK_OK);
                    uint32_t batches[] = {1, 2, 3, 1};
                    uint64_t sum = 0, sumtax = 0;
                    for (int k = 0; k < 4; k++) {
                        uint64_t r0 = od->line[0].refunded_total, t0 = od->line[0].refunded_tax;
                        if (mk_order_refund_line(&M, o, &S, 0, batches[k]) != MK_OK) bad++;
                        uint64_t R = od->line[0].refunded_total - r0;
                        uint64_t T = od->line[0].refunded_tax - t0;
                        if (T > R) bad++;
                        sum += R;
                        sumtax += T;
                        if (!all_ok()) bad++;
                        /* fee exactness after every step */
                        if (od->fee_charged != ref_fee(od->released_base - od->returned_base))
                            bad++;
                    }
                    if (sum != lt || sumtax != tx || od->state != MK_ORD_REFUNDED ||
                        od->fee_charged != 0 || bal("bea") != 1000000000 || bal("sam") != 0 ||
                        LG.commons != 0 || LG.tax != 0)
                        bad++;
                    cases++;
                }
    CHECK(bad == 0);
    printf("  refund-exactness cases: %u (4 batches each)\n", cases);
}

/* ===== 6. tax arithmetic and currencies ===== */
static void test_tax_ccy(void)
{
    setup();
    int32_t usd = mk_ccy_register(&M, "USD", 840, 2, false);
    int32_t jpy = mk_ccy_register(&M, "JPY", 392, 0, false);
    int32_t bhd = mk_ccy_register(&M, "BHD", 48, 3, false);
    CHECK(usd > 0 && jpy > 0 && bhd > 0);
    CHECK(mk_ccy_register(&M, "USD", 840, 2, false) == MK_ERR_DUPLICATE);
    CHECK(mk_ccy_register(&M, "us", 1, 2, false) == MK_ERR_ARG);
    CHECK(mk_ccy_register(&M, "XYZ", 1, 9, false) == MK_ERR_ARG);
    CHECK(mk_ccy_find(&M, "VFV") == 1 && M.ccy[0].numeric == 555 && M.ccy[0].is_vfv);
    mk_tax_rate_t t;
    memset(&t, 0, sizeof t);
    strcpy(t.region, "GB");
    t.num = 20;
    t.den = 100;
    t.inclusive = true;
    CHECK(mk_tax_set(&M, &t) > 0);
    uint64_t lt, tx;
    bool un = false;
    CHECK(mk__line_tax(&M, "GB", 0, 1200, 0, &lt, &tx, &un) == MK_OK);
    CHECK(lt == 1200 && tx == 200 && !un);
    t.inclusive = false;
    t.rounding = MK_ROUND_HALF_UP;
    strcpy(t.region, "US-CA");
    t.num = 725;
    t.den = 10000;
    CHECK(mk_tax_set(&M, &t) > 0);
    CHECK(mk__line_tax(&M, "US-CA", 0, 1000, 500, &lt, &tx, &un) == MK_OK);
    CHECK(tx == 73 && lt == 1573); /* 72.5 -> 73, shipping untaxed */
    t.rounding = MK_ROUND_FLOOR;
    CHECK(mk_tax_set(&M, &t) > 0);
    CHECK(mk__line_tax(&M, "US-CA", 0, 1000, 0, &lt, &tx, &un) == MK_OK && tx == 72);
    t.rounding = MK_ROUND_CEIL;
    t.tax_shipping = true;
    CHECK(mk_tax_set(&M, &t) > 0);
    CHECK(mk__line_tax(&M, "US-CA", 0, 1000, 1, &lt, &tx, &un) == MK_OK && tx == 73);
    CHECK(mk__line_tax(&M, "ZZ", 0, 1000, 0, &lt, &tx, &un) == MK_OK && tx == 0 && un);
    t.den = 0;
    CHECK(mk_tax_set(&M, &t) == MK_ERR_ARG);

    /* a cart across two stores and two currencies splits into three orders */
    mk_id_t B = ID("bea");
    fund(&B, 10000000);
    uint16_t s1 = open_store("sam", "S", 0, NULL), s2 = open_store("ann", "A", 0, NULL);
    mk_listing_spec_t l;
    uint16_t a = listing("sam", s1, MK_KIND_PHYSICAL, 100);
    spec_init(&l, s1, MK_KIND_PHYSICAL, 5000);
    l.ccy = (uint8_t) jpy;
    uint16_t b = 0;
    CHECK(publish("sam", &l, &b) == MK_OK);
    uint16_t c2 = listing("ann", s2, MK_KIND_PHYSICAL, 250);
    int32_t c = mk_cart_open(&M, &B);
    CHECK(mk_cart_add(&M, (uint16_t) c, a, 1, 0) == MK_OK);
    CHECK(mk_cart_add(&M, (uint16_t) c, b, 2, 0) == MK_OK);
    CHECK(mk_cart_add(&M, (uint16_t) c, c2, 1, 0) == MK_OK);
    CHECK(mk_cart_add(&M, (uint16_t) c, a, 2, 0) == MK_OK); /* merges */
    CHECK(mk_cart_remove(&M, (uint16_t) c, 999) == MK_ERR_NOT_FOUND);
    mk_checkout_opts_t co;
    memset(&co, 0, sizeof co);
    strcpy(co.region, "ZZ");
    uint16_t out[4];
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == MK_ERR_FULL);
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 4) == 3);
    CHECK(mk_order(&M, out[0])->total == 300 && mk_order(&M, out[0])->tax_unconfigured);
    CHECK(mk_order(&M, out[1])->total == 10000 && mk_order(&M, out[1])->ccy == jpy);
    CHECK(M.cart[c - 1].n == 0);
    /* oversell refused atomically */
    CHECK(mk_cart_add(&M, (uint16_t) c, a, 8, 0) == MK_OK);
    CHECK(mk_cart_add(&M, (uint16_t) c, c2, 1, 0) == MK_OK);
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 4) == MK_ERR_STOCK);
    CHECK(mk_available(&M, c2) == 9 && mk_available(&M, a) == 7);
    /* KYC/screening gate */
    mk_cart_clear(&M, (uint16_t) c);
    LG.kyc_block = true;
    CHECK(mk_cart_add(&M, (uint16_t) c, b, 7, 0) == MK_OK); /* 35000 JPY: ok */
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 4) == 1);
    CHECK(mk_cart_add(&M, (uint16_t) c, a, 2, 0) == MK_OK);
    M.listing[a - 1].spec.price = 100000;
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 4) == MK_ERR_POLICY);
    CHECK(all_ok());
}

/* ===== 7. signed objects ===== */
static void test_signed(void)
{
    setup();
    mk_store_spec_t s;
    memset(&s, 0, sizeof s);
    strcpy(s.name, "Coop Bakery");
    s.owner = ID("baker");
    s.version = 1;
    s.institution = true;
    uint8_t d[32], sig[32];
    mk_store_digest(&s, d);
    mk_id_t other = ID("other");
    mock_sign(&other, d, 32, sig);
    uint16_t h = 0;
    CHECK(mk_store_publish(&M, &s, sig, 32, &h) == MK_ERR_AUTH);
    mock_sign(&s.owner, d, 32, sig);
    CHECK(mk_store_publish(&M, &s, sig, 32, &h) == MK_OK && h == 1);
    CHECK(mk_store_publish(&M, &s, sig, 32, &h) == MK_ERR_DUPLICATE); /* replay */
    s.version = 2;
    strcpy(s.name, "Coop Bakery & Mill");
    mk_store_digest(&s, d);
    mock_sign(&s.owner, d, 32, sig);
    CHECK(mk_store_publish(&M, &s, sig, 32, &h) == MK_OK);
    CHECK(!strcmp(M.store[0].spec.name, "Coop Bakery & Mill"));
    s.has_arbiter = true;
    s.arbiter = s.owner;
    s.version = 3;
    CHECK(mk_store_publish(&M, &s, sig, 32, &h) == MK_ERR_ARG); /* own arbiter */

    mk_listing_spec_t l;
    spec_init(&l, h, MK_KIND_PHYSICAL, 450);
    uint16_t lh = 0;
    CHECK(publish("other", &l, &lh) == MK_ERR_AUTH);
    CHECK(publish("baker", &l, &lh) == MK_OK);
    CHECK(publish("baker", &l, &lh) == MK_ERR_DUPLICATE);
    l.version = 2;
    l.price = 500;
    CHECK(publish("baker", &l, &lh) == MK_OK && M.listing[lh - 1].spec.price == 500);
    spec_init(&l, h, MK_KIND_DIGITAL, 10);
    l.cid[0] = 0;
    uint16_t z = 0;
    CHECK(publish("baker", &l, &z) == MK_ERR_ARG);
    spec_init(&l, h, MK_KIND_PHYSICAL, 10);
    l.ccy = 9;
    CHECK(publish("baker", &l, &z) == MK_ERR_CCY);
    spec_init(&l, h, MK_KIND_BOOKING, 100);
    l.cancel_fee = 101;
    CHECK(publish("baker", &l, &z) == MK_ERR_POLICY);
    mk_id_t baker = ID("baker");
    CHECK(mk_listing_withdraw(&M, &other, lh) == MK_ERR_AUTH);
    CHECK(mk_listing_withdraw(&M, &baker, lh) == MK_OK && !M.listing[lh - 1].active);
    CHECK(mk_restock(&M, &other, lh, 5) == MK_ERR_AUTH);
    CHECK(mk_restock(&M, &baker, lh, 5) == MK_OK && M.listing[lh - 1].spec.stock == 15);
    CHECK(mk_store_close(&M, &baker, h) == MK_OK);
    spec_init(&l, h, MK_KIND_PHYSICAL, 10);
    z = 0;
    CHECK(publish("baker", &l, &z) == MK_ERR_STATE);
}

/* ===== 8. real ML-DSA-65 storefront ===== */
static mk_pq_keyring_t RING;
static uint8_t PK[PQ_MLDSA65_PK_BYTES], SK[PQ_MLDSA65_SK_BYTES], SIG[PQ_MLDSA65_SIG_BYTES];
static void test_mldsa(void)
{
    setup();
    memset(&RING, 0, sizeof RING);
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (i * 7 + 1);
    pq_mldsa65_keygen(seed, PK, SK);
    mk_id_t id;
    CHECK(mk_pq_add(&RING, PK, &id));
    LG.ring = &RING;
    LG.use_pq = true;
    mk_store_spec_t s;
    memset(&s, 0, sizeof s);
    strcpy(s.name, "Institutional Supply Co");
    s.owner = id;
    s.institution = true;
    s.version = 1;
    uint8_t d[32];
    mk_store_digest(&s, d);
    mk_pq_sign(SK, d, NULL, SIG);
    uint16_t h = 0;
    s.version = 7; /* tampered after signing */
    CHECK(mk_store_publish(&M, &s, SIG, sizeof SIG, &h) == MK_ERR_AUTH);
    s.version = 1;
    CHECK(mk_store_publish(&M, &s, SIG, sizeof SIG, &h) == MK_OK);
    SIG[100] ^= 1;
    s.version = 2;
    mk_store_digest(&s, d);
    CHECK(mk_store_publish(&M, &s, SIG, sizeof SIG, &h) == MK_ERR_AUTH);
    LG.use_pq = false;
}

/* ===== 9. bookings, digital, services ===== */
static void test_kinds(void)
{
    setup();
    mk_id_t B = ID("bea"), S = ID("sam");
    fund(&B, 10000000);
    uint16_t store = open_store("sam", "S", 0, "arb");
    uint16_t bk = listing("sam", store, MK_KIND_BOOKING, 8000);
    uint16_t o1 = order1("bea", bk, 2, 0);
    CHECK(M.listing[bk - 1].spec.slot[0].booked == 2);
    mk_id_t C = ID("cat");
    fund(&C, 10000000);
    int32_t c = mk_cart_open(&M, &C);
    CHECK(mk_cart_add(&M, (uint16_t) c, bk, 2, 0) == MK_OK);
    mk_checkout_opts_t co;
    memset(&co, 0, sizeof co);
    uint16_t out[2];
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == MK_ERR_STOCK);
    CHECK(mk_cart_add(&M, (uint16_t) c, bk, 1, 5) == MK_ERR_ARG);
    CHECK(mk_order_pay(&M, o1, &B) == MK_OK);
    /* free cancel before the cutoff */
    CHECK(mk_order_cancel(&M, o1, &B) == MK_OK && st(o1) == MK_ORD_REFUNDED);
    CHECK(M.listing[bk - 1].spec.slot[0].booked == 0 && bal("bea") == 10000000);
    /* late cancel: flat disclosed fee, assurance fee once */
    o1 = order1("bea", bk, 2, 0);
    CHECK(mk_order_pay(&M, o1, &B) == MK_OK);
    mk_tick(&M, M.listing[bk - 1].spec.slot[0].start - 86400);
    CHECK(mk_order_cancel(&M, o1, &B) == MK_OK && st(o1) == MK_ORD_REFUNDED);
    CHECK(mk_order(&M, o1)->cancel_fee == 4000);
    CHECK(bal("bea") == 10000000 - 4000);
    CHECK(bal("sam") == 4000 - (int64_t) ref_fee(4000));
    CHECK(LG.commons == (int64_t) ref_fee(4000) && all_ok());
    /* seller cancelling never charges the buyer */
    uint16_t o2 = order1("bea", bk, 1, 1);
    CHECK(mk_order_pay(&M, o2, &B) == MK_OK);
    CHECK(mk_order_cancel(&M, o2, &S) == MK_OK && mk_order(&M, o2)->cancel_fee == 0);
    /* past slot cannot be booked */
    mk_tick(&M, M.listing[bk - 1].spec.slot[0].start + 1);
    mk_id_t D = ID("dan");
    c = mk_cart_open(&M, &D);
    CHECK(mk_cart_add(&M, (uint16_t) c, bk, 1, 0) == MK_OK);
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == MK_ERR_WINDOW);

    /* digital: delivered by CID, which must match */
    uint16_t dg = listing("sam", store, MK_KIND_DIGITAL, 1500);
    CHECK(mk_available(&M, dg) == MK_UNLIMITED);
    uint16_t o3 = order1("bea", dg, 1, 0);
    CHECK(mk_order_pay(&M, o3, &B) == MK_OK);
    CHECK(mk_order_fulfil(&M, o3, &S, (const uint8_t *) "bafywrong", 9) == MK_ERR_AUTH);
    const char *cid = M.listing[dg - 1].spec.cid;
    CHECK(mk_order_fulfil(&M, o3, &S, (const uint8_t *) cid, (uint32_t) strlen(cid)) == MK_OK);
    CHECK(st(o3) == MK_ORD_FULFILLED);
    CHECK(mk_return_request(&M, o3, &B, 0, 1) == MK_OK); /* window set in fixture */
    CHECK(mk_return_decline(&M, o3, &S) == MK_OK && st(o3) == MK_ORD_FULFILLED);
    mk_tick(&M, mk_order(&M, o3)->release_at);
    CHECK(st(o3) == MK_ORD_COMPLETED && all_ok());

    /* service with a no-return policy */
    mk_listing_spec_t l;
    spec_init(&l, store, MK_KIND_SERVICE, 20000);
    l.return_window = 0;
    l.stock = MK_UNLIMITED;
    uint16_t sv = 0;
    CHECK(publish("sam", &l, &sv) == MK_OK);
    uint16_t o4 = order1("bea", sv, 1, 0);
    CHECK(mk_order_pay(&M, o4, &B) == MK_OK);
    CHECK(mk_order_fulfil(&M, o4, &S, (const uint8_t *) "done", 4) == MK_OK);
    CHECK(mk_return_request(&M, o4, &B, 0, 1) == MK_ERR_POLICY);
    CHECK(mk_order_confirm(&M, o4, &B) == MK_OK && all_ok());
}

/* ===== 10. reviews ===== */
static void test_reviews(void)
{
    setup();
    mk_id_t B = ID("bea"), S = ID("sam"), C = ID("cat");
    fund(&B, 10000000);
    fund(&C, 10000000);
    uint16_t store = open_store("sam", "S", 0, "arb");
    uint16_t L = listing("sam", store, MK_KIND_PHYSICAL, 100);
    CHECK(mk_store_quality(&M, store) == MK_Q16 / 2); /* neutral prior */
    uint16_t o = order1("bea", L, 1, 0);
    CHECK(mk_review(&M, o, &B, 5, "bafytext") == MK_ERR_STATE);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(mk_review(&M, o, &B, 5, NULL) == MK_ERR_STATE);
    CHECK(mk_order_confirm(&M, o, &B) == MK_OK);
    CHECK(mk_review(&M, o, &S, 5, NULL) == MK_ERR_AUTH);
    CHECK(mk_review(&M, o, &B, 6, NULL) == MK_ERR_ARG);
    CHECK(mk_review(&M, o, &B, 5, "bafytext") == MK_OK);
    CHECK(mk_review(&M, o, &B, 5, NULL) == MK_ERR_DUPLICATE);
    /* (4 * 65536 * 16384 + 32768 * 131072) / (65536 + 131072) = 2/3 */
    CHECK(mk_store_quality(&M, store) == 43690);
    /* a sybil-suspect reviewer (weight 0) moves nothing */
    uint16_t o2 = order1("cat", L, 1, 0);
    CHECK(mk_order_pay(&M, o2, &C) == MK_OK && mk_order_confirm(&M, o2, &C) == MK_OK);
    LG.review_w = 0;
    CHECK(mk_review(&M, o2, &C, 1, NULL) == MK_OK);
    CHECK(mk_store_quality(&M, store) == 43690 && M.store[store - 1].n_reviews == 2);
    /* a refunded-before-release order never earns a review */
    uint16_t o3 = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o3, &B) == MK_OK && mk_order_cancel(&M, o3, &B) == MK_OK);
    CHECK(mk_review(&M, o3, &B, 1, NULL) == MK_ERR_STATE);
}

/* ===== 11. ISF discovery through concord ===== */
static void test_discovery(void)
{
    setup();
    surplus_real_t e0[CON_DIM] = {0}, e1[CON_DIM] = {0}, mix[CON_DIM] = {0};
    e0[0] = SR_ONE;
    e1[1] = SR_ONE;
    mix[0] = SR_ONE;
    mix[1] = SR_ONE;
    CHECK(con_join(&CC, 1, e0, 200) >= 0); /* buyer */
    CHECK(con_join(&CC, 2, e1, 200) >= 0); /* complementary */
    CHECK(con_join(&CC, 3, e0, 200) >= 0); /* identical: echo chamber */
    CHECK(con_join(&CC, 4, mix, 200) >= 0);
    CHECK(con_join(&CC, 5, e1, 200) >= 0); /* will be divided */
    uint16_t sA = open_store("ann", "A", 2, NULL), sB = open_store("bob", "B", 3, NULL);
    uint16_t sC = open_store("cy", "C", 4, NULL), sD = open_store("di", "D", 5, NULL);
    uint16_t la = listing("ann", sA, MK_KIND_PHYSICAL, 10);
    uint16_t la2 = listing("ann", sA, MK_KIND_PHYSICAL, 11);
    uint16_t la3 = listing("ann", sA, MK_KIND_PHYSICAL, 12);
    uint16_t lb = listing("bob", sB, MK_KIND_PHYSICAL, 10);
    uint16_t lc = listing("cy", sC, MK_KIND_PHYSICAL, 10);
    uint16_t ld = listing("di", sD, MK_KIND_PHYSICAL, 10);
    (void) la2;
    (void) la3;
    CHECK(con_report_boundary(&CC, 1, 5, 1000));
    mk_id_t buyer = ID("bea");
    mk_query_t q = {&buyer, 1, -1, -1, -1, 0};
    mk_hit_t hits[16];
    uint32_t n = mk_discover(&M, &q, hits, 16);
    /* A x2 (per-store cap 2), C, B; D hidden by the mutual divide */
    CHECK(n == 4);
    CHECK(mk__listing_c(&M, hits[0].listing)->spec.store == sA);
    CHECK(mk__listing_c(&M, hits[1].listing)->spec.store == sA);
    CHECK(hits[2].listing == lc && hits[3].listing == lb);
    CHECK(hits[0].isf_q16 > 65000 && hits[3].isf_q16 < 100);
    CHECK(hits[2].isf_q16 > 32000 && hits[2].isf_q16 < 33500);
    bool sawd = false;
    for (uint32_t i = 0; i < n; i++) sawd |= hits[i].listing == ld;
    CHECK(!sawd);
    q.per_store_cap = 1;
    CHECK(mk_discover(&M, &q, hits, 16) == 3);
    /* out of stock disappears; the seller's own shop is not shown to itself */
    M.listing[lc - 1].spec.stock = 0;
    mk_id_t ann = ID("ann");
    mk_query_t q2 = {&ann, 2, -1, -1, -1, 0};
    n = mk_discover(&M, &q2, hits, 16);
    for (uint32_t i = 0; i < n; i++)
        CHECK(hits[i].listing != lc && mk__listing_c(&M, hits[i].listing)->spec.store != sA);
    q.category = 9;
    CHECK(mk_discover(&M, &q, hits, 16) == 0);
    (void) la;
}

/* ===== 12. agent shopping ===== */
static void test_agent(void)
{
    setup();
    mk_id_t U = ID("user"), AG = ID("assistant"), X = ID("x");
    fund(&U, 10000000);
    uint16_t store = open_store("sam", "S", 0, "arb");
    uint16_t L = listing("sam", store, MK_KIND_PHYSICAL, 1000);
    mk_listing_spec_t l;
    spec_init(&l, store, MK_KIND_PHYSICAL, 50);
    l.category = 40;
    uint16_t L2 = 0;
    CHECK(publish("sam", &l, &L2) == MK_OK);
    CHECK(mk_mandate_grant(&M, &U, &U, 1, 10, 10, 1, M.now + 99) == MK_ERR_ARG);
    int32_t md = mk_mandate_grant(&M, &U, &AG, 1, 3000, 5000, 1ull << 3, M.now + 30 * 86400);
    CHECK(md > 0);
    int32_t c = mk_cart_open(&M, &U);
    mk_checkout_opts_t co;
    memset(&co, 0, sizeof co);
    co.agent = &AG;
    co.mandate = (uint16_t) md;
    uint16_t out[2];
    CHECK(mk_cart_add(&M, (uint16_t) c, L, 4, 0) == MK_OK); /* 4000 > 3000 per order */
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == MK_ERR_MANDATE);
    CHECK(mk_available(&M, L) == 10); /* rolled back */
    mk_cart_clear(&M, (uint16_t) c);
    CHECK(mk_cart_add(&M, (uint16_t) c, L2, 1, 0) == MK_OK); /* category 40 not allowed */
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == MK_ERR_MANDATE);
    mk_cart_clear(&M, (uint16_t) c);
    CHECK(mk_cart_add(&M, (uint16_t) c, L, 2, 0) == MK_OK);
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == 1);
    uint16_t o = out[0];
    CHECK(LG.notes[MK_NOTE_CONFIRM_NEEDED] == 1);
    CHECK(mk_order_pay(&M, o, &U) == MK_ERR_CONFIRM); /* no token, no purchase */
    CHECK(mk_cart_add(&M, (uint16_t) c, L, 2, 0) == MK_OK);
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == 1);
    uint16_t o2 = out[0];
    uint8_t msg[32], tok[32], msg2[32];
    mk_confirm_message(&M, o, msg);
    mk_confirm_message(&M, o2, msg2);
    CHECK(memcmp(msg, msg2, 32) != 0);
    mock_sign(&AG, msg, 32, tok); /* the agent cannot confirm for the user */
    CHECK(mk_order_user_confirm(&M, o, tok, 32) == MK_ERR_AUTH);
    mock_sign(&U, msg, 32, tok);
    CHECK(mk_order_user_confirm(&M, o2, tok, 32) == MK_ERR_AUTH); /* bound to one order */
    CHECK(mk_order_user_confirm(&M, o, tok, 32) == MK_OK);
    CHECK(mk_order_user_confirm(&M, o, tok, 32) == MK_ERR_STATE); /* single use */
    CHECK(M.mandate[md - 1].committed == 2000);
    CHECK(mk_order_pay(&M, o, &AG) == MK_ERR_AUTH);
    CHECK(mk_order_pay(&M, o, &U) == MK_OK);
    /* second order: 2000 + 2000 <= 5000, then a third would exceed */
    mk_confirm_message(&M, o2, msg2);
    mock_sign(&U, msg2, 32, tok);
    CHECK(mk_order_user_confirm(&M, o2, tok, 32) == MK_OK);
    CHECK(mk_cart_add(&M, (uint16_t) c, L, 2, 0) == MK_OK);
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == MK_ERR_MANDATE);
    /* unconfirmed agent order expires at the confirm window */
    mk_cart_clear(&M, (uint16_t) c);
    CHECK(mk_cart_add(&M, (uint16_t) c, L, 1, 0) == MK_OK);
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == 1);
    uint16_t o3 = out[0];
    mk_tick(&M, mk_order(&M, o3)->confirm_by);
    CHECK(st(o3) == MK_ORD_EXPIRED);
    /* revocation */
    CHECK(mk_mandate_revoke(&M, (uint16_t) md, &X) == MK_ERR_AUTH);
    CHECK(mk_mandate_revoke(&M, (uint16_t) md, &U) == MK_OK);
    CHECK(mk_cart_add(&M, (uint16_t) c, L, 1, 0) == MK_OK);
    CHECK(mk_checkout(&M, (uint16_t) c, &co, out, 2) == MK_ERR_MANDATE);
    CHECK(all_ok());
}

/* ===== 13. subscriptions without traps ===== */
static void test_subs(void)
{
    setup();
    mk_id_t B = ID("bea"), X = ID("x");
    fund(&B, 10000000);
    uint16_t store = open_store("sam", "S", 0, "arb");
    uint16_t L = listing("sam", store, MK_KIND_SUBSCRIPTION, 999);
    uint16_t o = order1("bea", L, 1, 0);
    CHECK(mk_order_pay(&M, o, &B) == MK_OK);
    CHECK(st(o) == MK_ORD_FULFILLED);
    int32_t sh = mk_sub_start(&M, o);
    CHECK(sh > 0 && !M.sub[sh - 1].auto_renew);
    uint64_t end = M.sub[sh - 1].period_end;
    mk_tick(&M, end + 86400);
    uint32_t used = 0;
    for (uint32_t i = 0; i < MK_MAX_ORDERS; i++) used += M.order[i].used;
    CHECK(used == 1); /* nothing renewed by default */
    CHECK(st(o) == MK_ORD_COMPLETED);
    CHECK(mk_sub_opt_in(&M, (uint16_t) sh, &X, 2) == MK_ERR_AUTH);
    CHECK(mk_sub_opt_in(&M, (uint16_t) sh, &B, 2) == MK_OK);
    M.sub[sh - 1].period_end = M.now + 10 * 86400;
    mk_tick(&M, M.now + 6 * 86400);
    CHECK(LG.notes[MK_NOTE_RENEWAL_REMINDER] == 0);
    mk_tick(&M, M.now + 1 * 86400); /* 3 days before the end */
    CHECK(LG.notes[MK_NOTE_RENEWAL_REMINDER] == 1 && LG.notes[MK_NOTE_RENEWED] == 0);
    mk_tick(&M, M.sub[sh - 1].period_end);
    CHECK(LG.notes[MK_NOTE_RENEWED] == 1 && M.sub[sh - 1].renewals == 1);
    uint16_t o2 = M.sub[sh - 1].current_order;
    CHECK(o2 != o && st(o2) == MK_ORD_FULFILLED && mk_order(&M, o2)->total == 999);
    /* a price rise needs fresh consent */
    M.listing[L - 1].spec.price = 1299;
    mk_tick(&M, M.sub[sh - 1].period_end);
    CHECK(LG.notes[MK_NOTE_RENEWAL_PRICE_CHANGED] == 1 && !M.sub[sh - 1].auto_renew);
    CHECK(M.sub[sh - 1].renewals == 1);
    /* opt in again, then one-call cancel */
    CHECK(mk_sub_opt_in(&M, (uint16_t) sh, &B, 1) == MK_OK);
    CHECK(M.sub[sh - 1].price_lock == 1299);
    CHECK(mk_sub_cancel(&M, (uint16_t) sh, &B) == MK_OK);
    mk_tick(&M, M.sub[sh - 1].period_end + 10 * 86400);
    CHECK(M.sub[sh - 1].renewals == 1);
    CHECK(mk_sub_opt_in(&M, (uint16_t) sh, &B, 1) == MK_ERR_STATE);
    CHECK(all_ok());
}

/* ===== 14. B2B ===== */
static void test_b2b(void)
{
    setup();
    int32_t eur = mk_ccy_register(&M, "EUR", 978, 2, false);
    mk_id_t ORG = ID("hospital"), SUP = ID("supplier"), X = ID("x");
    fund(&ORG, 1000000000);
    uint16_t store = open_store("supplier", "Medical Supply Coop", 0, "arb");
    mk_listing_spec_t l;
    spec_init(&l, store, MK_KIND_PHYSICAL, 2500);
    l.ccy = (uint8_t) eur;
    l.stock = 1000;
    uint16_t L = 0;
    CHECK(publish("supplier", &l, &L) == MK_OK);
    mk_tax_rate_t t;
    memset(&t, 0, sizeof t);
    strcpy(t.region, "DE");
    t.num = 19;
    t.den = 100;
    CHECK(mk_tax_set(&M, &t) > 0);
    mk_po_spec_t p;
    memset(&p, 0, sizeof p);
    strcpy(p.po_number, "PO-2026-0042");
    p.buyer_org = ORG;
    p.store = store;
    p.ccy = (uint8_t) eur;
    strcpy(p.region, "DE");
    p.n_lines = 1;
    p.line[0].listing = L;
    p.line[0].qty = 100;
    p.line[0].unit_price = 2300; /* negotiated */
    p.net_days = 30;
    p.late_fee = 2500;
    p.service_fee = 500;
    mk_po_spec_t bad;
    memcpy(&bad, &p, sizeof p);
    bad.late_fee = M.policy.max_flat_fee + 1;
    CHECK(mk_po_draft(&M, &bad) == MK_ERR_POLICY);
    int32_t po = mk_po_draft(&M, &p);
    CHECK(po > 0 && M.po[po - 1].state == MK_PO_DRAFT);
    uint8_t sig[32];
    mock_sign(&X, M.po[po - 1].digest, 32, sig);
    CHECK(mk_po_submit(&M, (uint16_t) po, sig, 32) == MK_ERR_AUTH);
    CHECK(mk_po_accept(&M, (uint16_t) po, &SUP) == MK_ERR_STATE);
    mock_sign(&ORG, M.po[po - 1].digest, 32, sig);
    CHECK(mk_po_submit(&M, (uint16_t) po, sig, 32) == MK_OK);
    CHECK(mk_po_accept(&M, (uint16_t) po, &X) == MK_ERR_AUTH);
    CHECK(mk_po_accept(&M, (uint16_t) po, &SUP) == MK_OK);
    CHECK(mk_po_cancel(&M, (uint16_t) po, &ORG) == MK_ERR_STATE);
    CHECK(mk_invoice_issue(&M, (uint16_t) po, &ORG, "INV-1") == MK_ERR_AUTH);
    int32_t inv = mk_invoice_issue(&M, (uint16_t) po, &SUP, "INV-1");
    CHECK(inv > 0 && M.po[po - 1].state == MK_PO_INVOICED);
    CHECK(M.listing[L - 1].spec.stock == 900);
    const mk_invoice_t *v = &M.invoice[inv - 1];
    CHECK(v->base == 230000 && v->tax == 43700);
    CHECK(mk_invoice_amount_due(&M, (uint16_t) inv) == 230000 + 43700 + 500);
    /* overdue: the flat late fee applies once, and never grows */
    mk_tick(&M, v->due_at + 1);
    CHECK(v->state == MK_INV_OVERDUE && LG.notes[MK_NOTE_INVOICE_OVERDUE] == 1);
    uint64_t due = mk_invoice_amount_due(&M, (uint16_t) inv);
    CHECK(due == 230000 + 43700 + 500 + 2500);
    for (int d = 0; d < 400; d++) mk_tick(&M, M.now + 86400);
    CHECK(mk_invoice_amount_due(&M, (uint16_t) inv) == due);
    /* pain.001 via the callback */
    char xml[256];
    int32_t n = mk_invoice_pain001(&M, (uint16_t) inv, xml, sizeof xml);
    CHECK(n > 0 && LG.last_pain_amount == due);
    CHECK(!strcmp(LG.last_pain.ccy_alpha, "EUR") && LG.last_pain.minor == 2);
    CHECK(strstr(xml, "ZXV-INV-INV-1") != NULL);
    CHECK(mk_invoice_settle(&M, (uint16_t) inv, due - 1, false) == MK_ERR_ARG);
    CHECK(mk_invoice_settle(&M, (uint16_t) inv, due, false) == MK_OK);
    CHECK(v->state == MK_INV_PAID && M.po[po - 1].state == MK_PO_CLOSED);
    CHECK(v->assure_fee == ref_fee(230000 + 500 + 2500));
    CHECK(LG.commons == (int64_t) v->assure_fee && bal("supplier") == -(int64_t) v->assure_fee);
    CHECK(LG.escrow == 0 && ledger_total_ok());

    /* paid on time (initiated before due): no late fee; VFV is internal only */
    memset(&p.line[0], 0, sizeof p.line[0]);
    p.line[0].listing = L;
    p.line[0].qty = 1;
    p.line[0].unit_price = 2500;
    strcpy(p.po_number, "PO-2");
    po = mk_po_draft(&M, &p);
    mock_sign(&ORG, M.po[po - 1].digest, 32, sig);
    CHECK(mk_po_submit(&M, (uint16_t) po, sig, 32) == MK_OK);
    CHECK(mk_po_accept(&M, (uint16_t) po, &SUP) == MK_OK);
    inv = mk_invoice_issue(&M, (uint16_t) po, &SUP, "INV-2");
    CHECK(mk_invoice_issue(&M, (uint16_t) po, &SUP, "INV-1") == MK_ERR_STATE);
    CHECK(mk_invoice_pain001(&M, (uint16_t) inv, xml, sizeof xml) > 0);
    CHECK(M.invoice[inv - 1].state == MK_INV_INITIATED);
    mk_tick(&M, M.invoice[inv - 1].due_at + 86400);
    CHECK(M.invoice[inv - 1].state == MK_INV_INITIATED);
    CHECK(mk_invoice_amount_due(&M, (uint16_t) inv) == 2500 + 475 + 500);
    CHECK(mk_invoice_void(&M, (uint16_t) inv, &SUP) == MK_ERR_STATE);
    CHECK(mk_invoice_settle(&M, (uint16_t) inv, 3475, true) == MK_OK);
    CHECK(LG.escrow == 0 && ledger_total_ok());

    /* VFV invoice: pain.001 refused; settles internally; void; reject; cancel */
    p.ccy = 1;
    mk_listing_spec_t lv;
    spec_init(&lv, store, MK_KIND_PHYSICAL, 100);
    uint16_t LV = 0;
    CHECK(publish("supplier", &lv, &LV) == MK_OK);
    p.line[0].listing = LV;
    strcpy(p.po_number, "PO-3");
    po = mk_po_draft(&M, &p);
    mock_sign(&ORG, M.po[po - 1].digest, 32, sig);
    CHECK(mk_po_submit(&M, (uint16_t) po, sig, 32) == MK_OK);
    CHECK(mk_po_accept(&M, (uint16_t) po, &SUP) == MK_OK);
    inv = mk_invoice_issue(&M, (uint16_t) po, &SUP, "INV-3");
    CHECK(mk_invoice_pain001(&M, (uint16_t) inv, xml, sizeof xml) == MK_ERR_CCY);
    CHECK(mk_invoice_void(&M, (uint16_t) inv, &X) == MK_ERR_AUTH);
    CHECK(mk_invoice_void(&M, (uint16_t) inv, &SUP) == MK_OK);
    CHECK(mk_invoice_settle(&M, (uint16_t) inv, 1, true) == MK_ERR_STATE);
    strcpy(p.po_number, "PO-4");
    po = mk_po_draft(&M, &p);
    mock_sign(&ORG, M.po[po - 1].digest, 32, sig);
    CHECK(mk_po_submit(&M, (uint16_t) po, sig, 32) == MK_OK);
    CHECK(mk_po_reject(&M, (uint16_t) po, &SUP) == MK_OK && M.po[po - 1].state == MK_PO_REJECTED);
    strcpy(p.po_number, "PO-5");
    po = mk_po_draft(&M, &p);
    CHECK(mk_po_cancel(&M, (uint16_t) po, &SUP) == MK_ERR_AUTH);
    CHECK(mk_po_cancel(&M, (uint16_t) po, &ORG) == MK_OK && M.po[po - 1].state == MK_PO_CANCELLED);
}

/* ===== 15. randomized escrow conservation ===== */
static void test_random_conservation(void)
{
    setup();
    const char *buyers[] = {"b0", "b1", "b2", "b3"};
    for (int i = 0; i < 4; i++) {
        mk_id_t b = ID(buyers[i]);
        fund(&b, 1000000000);
    }
    uint16_t s = open_store("sam", "S", 0, "arb");
    mk_tax_rate_t t;
    memset(&t, 0, sizeof t);
    strcpy(t.region, "XA");
    t.num = 17;
    t.den = 100;
    CHECK(mk_tax_set(&M, &t) > 0);
    uint16_t L = listing("sam", s, MK_KIND_PHYSICAL, 777);
    mk_id_t S = ID("sam"), A = ID("arb");
    M.listing[L - 1].spec.stock = 100000;
    uint64_t x = 0x9E3779B97F4A7C15ull;
    uint32_t steps = 0, bad = 0;
    for (int it = 0; it < 3000; it++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        uint32_t r = (uint32_t) x;
        /* recycle terminal slots so the table never fills */
        for (uint32_t i = 0; i < MK_MAX_ORDERS; i++) {
            mk_order_state_t q = M.order[i].state;
            if (M.order[i].used &&
                (q == MK_ORD_CANCELLED || q == MK_ORD_EXPIRED || q == MK_ORD_RESOLVED ||
                 q == MK_ORD_REFUNDED) &&
                (r & 7) == 0) {
                M.order[i].used = false;
                M.order[i].state = MK_ORD_FREE;
            }
        }
        uint16_t o = (uint16_t) (1 + (r >> 8) % MK_MAX_ORDERS);
        mk_id_t b;
        const mk_order_t *od = mk_order(&M, o);
        switch (r % 11) {
        case 0:
        case 1: {
            uint16_t n = order1(buyers[(r >> 4) & 3], L, 1 + ((r >> 12) % 7), 0);
            (void) n;
            break;
        }
        default:
            if (!od) break;
            b = od->buyer;
            switch (r % 11) {
            case 2:
                (void) mk_order_pay(&M, o, &b);
                break;
            case 3:
                (void) mk_order_fulfil(&M, o, &S, (const uint8_t *) "TRACK-OK", 8);
                break;
            case 4:
                (void) mk_order_confirm(&M, o, &b);
                break;
            case 5:
                (void) mk_order_refund_line(&M, o, &S, 0, 1 + ((r >> 20) & 1));
                break;
            case 6:
                (void) mk_dispute_open(&M, o, &b);
                break;
            case 7: {
                uint8_t dg[32], sig[32];
                uint64_t H = od->held_base + od->held_tax;
                uint64_t aw = H ? (r >> 9) % (H + 1) : 0;
                mk_ruling_digest(&M, o, aw, dg);
                mock_sign(&A, dg, 32, sig);
                (void) mk_dispute_rule(&M, o, &A, aw, sig, 32);
                break;
            }
            case 8:
                (void) mk_return_request(&M, o, &b, 0, 1);
                break;
            case 9:
                (void) mk_return_received(&M, o, &S, (r >> 3) & 1);
                break;
            case 10:
                (void) mk_order_cancel(&M, o, &b);
                break;
            }
        }
        if ((r & 15) == 0) mk_tick(&M, M.now + ((r >> 16) % 5) * 86400);
        if ((r & 63) == 1) LG.fail_next = true;
        steps++;
        if (!all_ok()) {
            bad++;
            break;
        }
    }
    LG.fail_next = false;
    CHECK(bad == 0);
    printf("  randomized conservation steps: %u\n", steps);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_fee_reference();
    test_table();
    test_transitions();
    test_community();
    test_refund_exactness();
    test_tax_ccy();
    test_signed();
    test_mldsa();
    test_kinds();
    test_reviews();
    test_discovery();
    test_agent();
    test_subs();
    test_b2b();
    test_random_conservation();
    printf("test_market: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
