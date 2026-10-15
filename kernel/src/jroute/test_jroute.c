/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_jroute.c — the jurisdictional routing matrix (jroute.h R1-R10): the
 * 128-bit helpers against __int128, the lot header and its corruptions,
 * point-in-polygon on squares, a shared border, overlaps, a sanction hole
 * and a triangle, the proof-of-zone verifier (stub, freshness, replay,
 * forged masks), the profile gate, every hierarchy level and its skips,
 * exact p/q vouchers, registries making BOTH, rulings (deliver, reroute,
 * refund), the receipt and escrow digests against an independent rebuild,
 * and a randomized conservation loop. The proof and arbiter hooks are
 * stand-ins (SHA-256 of the message); production passes a real verifier. */
#include <stdio.h>
#include <string.h>
#include "jroute.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

typedef unsigned __int128 u128;

/* ---- zones: x = lon, y = lat, microdegrees ---- */
#define M6 1000000
static const jr_pt_t SQ_A[4] = {{0, 0}, {0, 10 * M6}, {10 * M6, 10 * M6}, {10 * M6, 0}};
static const jr_pt_t SQ_B[4] = {{0, 10 * M6}, {0, 20 * M6}, {10 * M6, 20 * M6}, {10 * M6, 10 * M6}};
static const jr_pt_t SQ_C[4] = {{0, 20 * M6}, {0, 30 * M6}, {10 * M6, 30 * M6}, {10 * M6, 20 * M6}};
static const jr_pt_t SQ_D[4] = {{0, 30 * M6}, {0, 40 * M6}, {10 * M6, 40 * M6}, {10 * M6, 30 * M6}};
static const jr_pt_t SQ_S[4] = {{8 * M6, 8 * M6}, {8 * M6, 9 * M6}, {9 * M6, 9 * M6}, {9 * M6, 8 * M6}};
static const jr_pt_t SQ_E[4] = {{5 * M6, 0}, {5 * M6, 10 * M6}, {10 * M6, 10 * M6}, {10 * M6, 0}};
static const jr_pt_t TRI[3] = {{0, 50 * M6}, {10 * M6, 50 * M6}, {0, 60 * M6}};
static const jr_pt_t SQ_G[4] = {{0, 60 * M6}, {0, 70 * M6}, {10 * M6, 70 * M6}, {10 * M6, 60 * M6}};
static const jr_pt_t SQ_H[4] = {{5 * M6, 60 * M6}, {5 * M6, 70 * M6}, {15 * M6, 70 * M6}, {15 * M6, 60 * M6}};

static jr_zone_t Z[] = {
    {10, JR_ZONE, 0x0F, 1, 4, SQ_A},  {20, JR_ZONE, 0x06, 2, 4, SQ_B},
    {30, JR_ZONE, 0x04, 1, 4, SQ_C},  {40, JR_ZONE, 0x11, 1, 4, SQ_D},
    {99, JR_SANCTION, 0, 0, 4, SQ_S}, {5, JR_ZONE, 0x0F, 1, 4, SQ_E},
    {60, JR_ZONE, 0x02, 3, 3, TRI},   {70, JR_ZONE, 0x04, 1, 4, SQ_G},
    {71, JR_ZONE, 0x04, 4, 4, SQ_H},
};
#define NZ ((uint32_t) (sizeof Z / sizeof Z[0]))

static jr_pt_t P(int lat, int lon)
{
    jr_pt_t p = {lat, lon};
    return p;
}

/* ---- hooks ---- */
static int nburn, nrefund, ndeliver, nescrow, burn_fail, refund_fail;
static uint64_t burned, refunded;
static jr_receipt_t last_deliver;
static jr_escrow_t last_escrow;
static int burn(void *ctx, const uint8_t user[32], uint64_t v)
{
    (void) ctx;
    (void) user;
    if (burn_fail)
        return -1;
    nburn++;
    burned += v;
    return 0;
}
static int refund(void *ctx, const uint8_t user[32], uint64_t v)
{
    (void) ctx;
    (void) user;
    if (refund_fail)
        return -1;
    nrefund++;
    refunded += v;
    return 0;
}
static void deliver(void *ctx, const jr_receipt_t *r)
{
    (void) ctx;
    ndeliver++;
    memcpy(&last_deliver, r, sizeof *r);
}
static void escrow(void *ctx, const jr_escrow_t *e)
{
    (void) ctx;
    nescrow++;
    memcpy(&last_escrow, e, sizeof *e);
}
/* stand-in signature: sig = sha256(key byte || msg) */
static bool sig_ok(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig, uint32_t sl)
{
    uint8_t k = ctx ? *(const uint8_t *) ctx : 0, d[32];
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, &k, 1);
    sha256_update(&c, msg, len);
    sha256_final(&c, d);
    return sl == 32 && sig && memcmp(d, sig, 32) == 0;
}
static void sign(uint8_t key, const uint8_t *msg, uint32_t len, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, &key, 1);
    sha256_update(&c, msg, len);
    sha256_final(&c, out);
}
static uint8_t PROOF_KEY = 7, ARB_KEY = 9;

/* registries */
static lpres_state_t reg_ans[16];
static int nreg_calls;
static lpres_state_t reg(void *ctx, uint32_t cap, uint32_t zone, uint8_t ev[32])
{
    (void) ctx;
    nreg_calls++;
    ev[0] = (uint8_t) cap;
    ev[1] = (uint8_t) zone;
    return reg_ans[cap & 15u];
}

static jr_router_t R;
static jr_hooks_t H;
static const uint8_t U1[32] = {1, [31] = 0x71}, U2[32] = {2, [31] = 0x72}, U3[32] = {3, [31] = 0x73};

static void reset_counts(void)
{
    nburn = nrefund = ndeliver = nescrow = burn_fail = refund_fail = 0;
    burned = refunded = 0;
    nreg_calls = 0;
    for (int i = 0; i < 16; i++)
        reg_ans[i] = LPRES_STATE_NEITHER;
}

static void hooks(void)
{
    memset(&H, 0, sizeof H);
    H.burn = burn;
    H.refund = refund;
    H.deliver = deliver;
    H.escrow = escrow;
    H.proof_verify = sig_ok;
    H.proof_ctx = &PROOF_KEY;
    H.arbiter_verify = sig_ok;
    H.arbiter_ctx = &ARB_KEY;
}

static jr_lot_hdr_t lot(uint32_t wh, uint64_t serial)
{
    jr_lot_hdr_t h;
    memset(&h, 0, sizeof h);
    h.grade_kind = 1;
    h.warehouse_id = wh;
    h.volume = 200000;
    h.drum_serial = serial | (0x5Aull << 56);
    h.lot_number = serial * 10 | (0x3Bull << 56);
    h.grade = 800;
    h.haircut_bps = 250;
    h.vault_pubkey[0] = 0xAA;
    h.vault_pubkey[31] = 0xCC;
    h.custodian_receipt[0] = 0xDD;
    h.custodian_receipt[31] = 0xBB;
    h.depot_lat = 37500000;
    h.depot_lon = 15000000;
    return h;
}

static void add_lots(uint32_t asset, uint32_t wh, int n, uint64_t first)
{
    for (int i = 0; i < n; i++) {
        uint8_t b[JR_LOT_HDR_LEN];
        jr_lot_hdr_t h = lot(wh, first + (uint64_t) i);
        jr_lot_hdr_encode(&h, b);
        CHECK(jr_add_lot(&R, asset, b) == JR_OK, "add lot");
    }
}

/* Assets: 1 olive oil, 0.6 L per VINO, 200 L lots (1000 VINO per barrel);
 * 2 metal, 1/50 g per VINO, 31 g lots; 3 compute 16.16 octane per VINO,
 * operator only; 4 mutual 1:1 region 1; 5 local credit 3/7, region 9999 so
 * the hierarchy never picks it. */
static void setup(void)
{
    reset_counts();
    hooks();
    CHECK(jr_init(&R, &H, Z, NZ, 10) == JR_OK, "init");
    CHECK(jr_set_profile(&R, JR_PROFILE_INSTITUTION) == JR_OK, "institution");
    CHECK(jr_enable(&R, true) == JR_OK, "enable");
    jr_asset_t a1 = {1, JR_CAP_COMMODITY, 3, 5, 200, 0, false, false};
    jr_asset_t a2 = {2, JR_CAP_METALS, 1, 50, 31, 0, false, false};
    jr_asset_t a3 = {3, JR_CAP_COMPUTE, 65536, 1, 1, 0, true, false};
    jr_asset_t a4 = {4, JR_CAP_MUTUAL, 1, 1, 1, 1, false, false};
    jr_asset_t a5 = {5, JR_CAP_COMPUTE, 3, 7, 1, 9999, false, false};
    CHECK(jr_add_asset(&R, &a1) == JR_OK, "a1");
    CHECK(jr_add_asset(&R, &a2) == JR_OK, "a2");
    CHECK(jr_add_asset(&R, &a3) == JR_OK, "a3");
    CHECK(jr_add_asset(&R, &a4) == JR_OK, "a4");
    CHECK(jr_add_asset(&R, &a5) == JR_OK, "a5");
    CHECK(jr_set_n_asset(&R, 5) == JR_OK, "n asset");
    add_lots(1, 100, 10, 1000);
    add_lots(2, 200, 4, 5000);
    CHECK(jr_set_physical(&R, true) == JR_OK && R.physical_online, "physical layer on");
}

static jr_zone_ok_t zok(lpres_state_t v, uint32_t id, uint32_t caps, uint32_t region)
{
    jr_zone_ok_t z = {v, id, caps, region};
    return z;
}
static jr_zone_ok_t ZA(void)
{
    return zok(LPRES_STATE_TRUE, 10, 0x0F, 1);
}
static jr_request_t req(const uint8_t *u, uint64_t v, uint32_t want, bool acc, bool op)
{
    jr_request_t q;
    memset(&q, 0, sizeof q);
    memcpy(q.user, u, 32);
    q.vino = v;
    q.want_asset = want;
    q.accumulate = acc;
    q.operator_node = op;
    return q;
}

/* ---- independent rebuild of R10 and the escrow digest ---- */
static void p8(sha256_ctx_t *c, uint8_t v)
{
    sha256_update(c, &v, 1);
}
static void p32(sha256_ctx_t *c, uint32_t v)
{
    uint8_t b[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
    sha256_update(c, b, 4);
}
static void p64(sha256_ctx_t *c, uint64_t v)
{
    p32(c, (uint32_t) v);
    p32(c, (uint32_t) (v >> 32));
}
static void ref_receipt(const uint8_t prev[32], const jr_receipt_t *o, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/jr-receipt/v1", 17);
    sha256_update(&c, prev, 32);
    p64(&c, o->receipt_id);
    sha256_update(&c, o->user, 32);
    p64(&c, o->vino_burned);
    p8(&c, o->outcome);
    p8(&c, o->event);
    p32(&c, o->asset_id);
    p32(&c, o->cap);
    p8(&c, o->transferable);
    p64(&c, o->lots);
    p32(&c, o->nlot_idx);
    for (uint32_t i = 0; i < o->nlot_idx; i++)
        p32(&c, o->lot_idx[i]);
    p64(&c, o->voucher_num);
    p64(&c, o->voucher_den);
    p8(&c, o->route);
    p64(&c, o->escrow_id);
    sha256_final(&c, out);
}
static void ref_escrow(const jr_escrow_t *e, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/jr-escrow/v1", 16);
    p64(&c, e->id);
    sha256_update(&c, e->user, 32);
    p64(&c, e->vino);
    p8(&c, e->reason);
    p32(&c, e->zone_id);
    p32(&c, e->caps);
    p32(&c, e->region);
    p8(&c, e->operator_node);
    p32(&c, e->asset_id);
    p64(&c, e->lots);
    p64(&c, e->voucher_after);
    p32(&c, e->nlot_idx);
    for (uint32_t i = 0; i < e->nlot_idx; i++)
        p32(&c, e->lot_idx[i]);
    p32(&c, e->nclaims);
    for (uint32_t i = 0; i < e->nclaims; i++) {
        p32(&c, e->claims[i].source);
        p8(&c, e->claims[i].verdict);
        sha256_update(&c, e->claims[i].evidence, 32);
    }
    sha256_final(&c, out);
}

static uint8_t head_before[32];
static void snap(void)
{
    memcpy(head_before, R.head, 32);
}
static void check_seal(const jr_receipt_t *o, const char *what)
{
    uint8_t d[32];
    ref_receipt(head_before, o, d);
    CHECK(memcmp(d, o->digest, 32) == 0, what);
    CHECK(memcmp(R.head, o->digest, 32) == 0, "head follows the receipt");
}

static jr_status_t settle(uint64_t id, uint8_t kind, uint32_t caps, uint32_t region,
                          jr_receipt_t *o)
{
    const jr_escrow_t *e = jr_escrow_get(&R, id);
    uint8_t msg[JR_RULING_MSG_LEN], s[32];
    if (!e)
        return jr_escrow_settle(&R, id, kind, caps, region, s, 32, o);
    jr_ruling_msg(e, kind, caps, region, msg);
    sign(ARB_KEY, msg, JR_RULING_MSG_LEN, s);
    return jr_escrow_settle(&R, id, kind, caps, region, s, 32, o);
}

/* ======================================================================= */

static void t_math(void)
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    int bad = 0;
    for (int i = 0; i < 4000; i++) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        uint64_t a = s, b = (s * 0x2545F4914F6CDD1Dull) >> (i % 64);
        if (i % 7 == 0)
            a = UINT64_MAX - (uint64_t) i;
        jr_u128_t m = jr_mul64(a, b);
        u128 w = (u128) a * b;
        if (m.lo != (uint64_t) w || m.hi != (uint64_t) (w >> 64))
            bad++;
        uint64_t d = (s >> (i % 61)) | 1u, q, r;
        jr_u128_t n = {(uint64_t) (w >> 64) % d, (uint64_t) w};
        u128 nn = ((u128) n.hi << 64) | n.lo;
        if (!jr_divmod(n, d, &q, &r) || q != (uint64_t) (nn / d) || r != (uint64_t) (nn % d))
            bad++;
    }
    CHECK(bad == 0, "mul64/divmod match __int128 on 4000 random cases");
    jr_u128_t n = {0, 0};
    uint64_t q = 5, r = 5;
    CHECK(!jr_divmod(n, 0, &q, &r), "divide by zero refused");
    n.hi = 7;
    CHECK(!jr_divmod(n, 7, &q, &r), "quotient past 64 bits refused (hi == d)");
    CHECK(jr_divmod(n, 8, &q, &r), "hi < d fits");
    CHECK(q == 0xE000000000000000ull && r == 0, "7*2^64 / 8");
    n.hi = 0;
    n.lo = UINT64_MAX;
    CHECK(jr_divmod(n, UINT64_MAX, &q, &r) && q == 1 && r == 0, "max / max");
    n.hi = UINT64_MAX - 1;
    n.lo = UINT64_MAX;
    CHECK(jr_divmod(n, UINT64_MAX, &q, &r) && q == UINT64_MAX && r == UINT64_MAX - 1,
          "carry path at the top bit");
    jr_u128_t m = jr_mul64(UINT64_MAX, UINT64_MAX);
    CHECK(m.hi == UINT64_MAX - 1 && m.lo == 1, "max * max");
}

static void t_lot_header(void)
{
    jr_lot_hdr_t h = lot(42, 77), g;
    uint8_t b[JR_LOT_HDR_LEN], c[JR_LOT_HDR_LEN];
    jr_lot_hdr_encode(&h, b);
    CHECK(b[0] == 'Z' && b[3] == 'T' && b[4] == 1 && b[5] == 0, "magic and version");
    CHECK(b[8] == 42 && b[16] == 77 && b[23] == 0x5A && b[24] == 770 % 256 && b[31] == 0x3B &&
              b[36] == 250 && b[71] == 0xCC && b[72] == 0xDD,
          "fixed offsets");
    {
        uint8_t d[32];
        sha256(b, 124, d);
        CHECK(memcmp(d, b + 124, 4) == 0, "check = first 4 bytes of sha256(bytes 0..123)");
    }
    CHECK(b[40] == 0xAA && b[103] == 0xBB, "vault key and receipt hash offsets");
    CHECK(jr_lot_hdr_decode(b, &g) == JR_OK, "decode");
    CHECK(memcmp(&g.vault_pubkey, &h.vault_pubkey, 32) == 0 &&
              memcmp(g.custodian_receipt, h.custodian_receipt, 32) == 0 &&
              g.warehouse_id == 42 && g.drum_serial == (77 | (0x5Aull << 56)) &&
              g.lot_number == (770 | (0x3Bull << 56)) &&
              g.volume == 200000 && g.grade == 800 && g.haircut_bps == 250 &&
              g.grade_kind == 1 && g.depot_lat == 37500000 && g.depot_lon == 15000000,
          "round trip");
    CHECK(jr_lot_hdr_decode(NULL, &g) == JR_E_ARG && jr_lot_hdr_decode(b, NULL) == JR_E_ARG,
          "null args");
    static const int pos[] = {0, 1, 2, 3, 4, 5, 38, 39, 112, 118, 123, 124, 127, 60};
    for (unsigned i = 0; i < sizeof pos / sizeof pos[0]; i++) {
        memcpy(c, b, sizeof b);
        c[pos[i]] ^= 0x01;
        CHECK(jr_lot_hdr_decode(c, &g) == JR_E_FORMAT, "any flipped byte refused");
    }
    struct {
        int field;
        int64_t v;
        jr_status_t want;
    } cases[] = {
        {0, 0, JR_E_FORMAT},          {0, 1, JR_OK},
        {1, 10000, JR_OK},            {1, 10001, JR_E_FORMAT},
        {2, 90000000, JR_OK},         {2, 90000001, JR_E_FORMAT},
        {2, -90000000, JR_OK},        {2, -90000001, JR_E_FORMAT},
        {3, 180000000, JR_OK},        {3, 180000001, JR_E_FORMAT},
        {3, -180000000, JR_OK},       {3, -180000001, JR_E_FORMAT},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        jr_lot_hdr_t t = lot(1, 1);
        if (cases[i].field == 0)
            t.volume = (uint32_t) cases[i].v;
        if (cases[i].field == 1)
            t.haircut_bps = (uint16_t) cases[i].v;
        if (cases[i].field == 2)
            t.depot_lat = (int32_t) cases[i].v;
        if (cases[i].field == 3)
            t.depot_lon = (int32_t) cases[i].v;
        jr_lot_hdr_encode(&t, c);
        CHECK(jr_lot_hdr_decode(c, &g) == cases[i].want, "field range");
    }
}

static void t_locate(void)
{
    uint32_t id, caps, reg;
    CHECK(jr_zone_locate(Z, NZ, P(2 * M6, 2 * M6), &id, &caps, &reg) == LPRES_STATE_TRUE &&
              id == 10 && caps == 0x0F && reg == 1,
          "inside A");
    CHECK(jr_zone_locate(Z, NZ, P(7 * M6, 2 * M6), &id, &caps, &reg) == LPRES_STATE_TRUE &&
              id == 5 && caps == 0x0F,
          "in A and E, which agree: lowest id");
    CHECK(jr_zone_locate(Z, NZ, P(8 * M6 + 5, 8 * M6 + 5), &id, &caps, &reg) ==
                  LPRES_STATE_FALSE &&
              id == 0 && caps == 0 && reg == 0,
          "sanction hole wins and hides the zone");
    CHECK(jr_zone_locate(Z, NZ, P(8 * M6, 8 * M6 + 3), &id, &caps, &reg) == LPRES_STATE_FALSE,
          "sanction edge is inside (closed)");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6, 10 * M6), &id, &caps, &reg) == LPRES_STATE_BOTH,
          "border A|B: masks differ");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6, 10 * M6 + 1), &id, &caps, &reg) == LPRES_STATE_TRUE &&
              id == 20 && caps == 0x06 && reg == 2,
          "just inside B");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6, 20 * M6), &id, &caps, &reg) == LPRES_STATE_BOTH,
          "border B|C");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6, 35 * M6), &id, &caps, &reg) == LPRES_STATE_BOTH &&
              id == 40,
          "reserved bit 0x10 is an unhandled flag");
    CHECK(jr_zone_locate(Z, NZ, P(2 * M6, 65 * M6), &id, &caps, &reg) == LPRES_STATE_TRUE &&
              id == 70,
          "G alone");
    CHECK(jr_zone_locate(Z, NZ, P(7 * M6, 65 * M6), &id, &caps, &reg) == LPRES_STATE_BOTH &&
              id == 70,
          "G and H: same mask, different region");
    CHECK(jr_zone_locate(Z, NZ, P(-1, 5 * M6), &id, &caps, &reg) == LPRES_STATE_NEITHER &&
              id == 0 && caps == 0,
          "outside everything");
    CHECK(jr_zone_locate(Z, NZ, P(0, 0), &id, &caps, &reg) == LPRES_STATE_TRUE && id == 10,
          "vertex is inside");
    /* triangle (lat 0..10, lon 50..60), hypotenuse lat + lon = 60 */
    CHECK(jr_zone_locate(Z, NZ, P(4 * M6, 55 * M6), &id, &caps, &reg) == LPRES_STATE_TRUE &&
              id == 60,
          "triangle inside");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6, 55 * M6), &id, &caps, &reg) == LPRES_STATE_TRUE,
          "triangle hypotenuse");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6 + 1, 55 * M6), &id, &caps, &reg) ==
              LPRES_STATE_NEITHER,
          "just past the hypotenuse");
    CHECK(jr_zone_locate(Z, NZ, P(10 * M6, 49 * M6), &id, &caps, &reg) == LPRES_STATE_NEITHER,
          "beyond the apex, ray passes the vertex");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6, 59 * M6), &id, &caps, &reg) == LPRES_STATE_NEITHER,
          "right of the triangle");
    CHECK(jr_zone_locate(Z, NZ, P(2 * M6, 2 * M6), NULL, NULL, NULL) == LPRES_STATE_TRUE,
          "outputs optional");
    jr_zone_t bad[2] = {Z[0], {11, JR_ZONE, 1, 1, 2, SQ_A}};
    CHECK(jr_zone_locate(bad, 2, P(2 * M6, 2 * M6), &id, &caps, &reg) == LPRES_STATE_NEITHER &&
              id == 0,
          "malformed table reads NEITHER");
    bad[1].npts = JR_MAX_ZONE_PTS + 1;
    CHECK(jr_zone_locate(bad, 2, P(2 * M6, 2 * M6), &id, &caps, &reg) == LPRES_STATE_NEITHER,
          "too many points");
    bad[1].npts = 4;
    bad[1].kind = 3;
    CHECK(jr_zone_locate(bad, 2, P(2 * M6, 2 * M6), &id, &caps, &reg) == LPRES_STATE_NEITHER,
          "bad kind");
    bad[1].kind = JR_ZONE;
    bad[1].pts = NULL;
    CHECK(jr_zone_locate(bad, 2, P(2 * M6, 2 * M6), &id, &caps, &reg) == LPRES_STATE_NEITHER,
          "null points");
    bad[1].pts = SQ_A;
    bad[1].npts = 3;
    CHECK(jr_zone_locate(bad, 2, P(2 * M6, 2 * M6), &id, &caps, &reg) == LPRES_STATE_BOTH,
          "three points is a valid polygon");
    CHECK(jr_zone_locate(NULL, 1, P(0, 0), &id, &caps, &reg) == LPRES_STATE_NEITHER,
          "null table");
    CHECK(jr_zone_locate(NULL, 0, P(0, 0), &id, &caps, &reg) == LPRES_STATE_NEITHER,
          "empty table");

    uint8_t h1[32], h2[32];
    CHECK(jr_table_hash(Z, NZ, h1) == JR_OK, "table hash");
    jr_zone_t cp[NZ];
    memcpy(cp, Z, sizeof cp);
    int changed = 0;
    for (int f = 0; f < 6; f++) {
        memcpy(cp, Z, sizeof cp);
        jr_pt_t moved[4];
        memcpy(moved, SQ_A, sizeof moved);
        if (f == 0)
            cp[0].id++;
        if (f == 1)
            cp[0].caps ^= 1;
        if (f == 2)
            cp[0].region++;
        if (f == 3)
            cp[0].kind = JR_SANCTION;
        if (f == 4) {
            moved[3].lat++;
            cp[0].pts = moved;
        }
        if (f == 5) {
            moved[3].lon++;
            cp[0].pts = moved;
        }
        jr_table_hash(cp, NZ, h2);
        changed += memcmp(h1, h2, 32) != 0;
    }
    CHECK(changed == 6, "every field is in the table hash");
    CHECK(jr_table_hash(Z, NZ - 1, h2) == JR_OK && memcmp(h1, h2, 32) != 0, "count in hash");
    CHECK(jr_table_hash(bad, 2, h2) == JR_OK, "3-point table hashes");
    bad[1].npts = 2;
    CHECK(jr_table_hash(bad, 2, h2) == JR_E_ARG, "malformed table refused");
    CHECK(jr_table_hash(NULL, 1, h2) == JR_E_ARG && jr_table_hash(Z, 1, NULL) == JR_E_ARG,
          "null args");
    CHECK(jr_table_hash(NULL, 0, h2) == JR_OK, "empty table hashes");
}

static void make_claim(jr_pt_t p, uint8_t secret_b, uint64_t epoch, jr_zone_claim_t *c,
                       uint8_t proof[32])
{
    uint8_t secret[32] = {secret_b}, msg[JR_CLAIM_MSG_LEN];
    CHECK(jr_zone_attest(Z, NZ, p, secret, epoch, c) == JR_OK, "attest");
    jr_zone_claim_msg(c, msg);
    sign(PROOF_KEY, msg, JR_CLAIM_MSG_LEN, proof);
}

static void t_attest_verify(void)
{
    setup();
    jr_zone_claim_t c, c2;
    uint8_t pf[32], msg[JR_CLAIM_MSG_LEN];
    make_claim(P(2 * M6, 2 * M6), 1, 100, &c, pf);
    CHECK(c.verdict == LPRES_STATE_TRUE && c.zone_id == 10 && c.caps == 0x0F && c.region == 1 &&
              c.epoch == 100 && memcmp(c.table_hash, R.table_hash, 32) == 0,
          "claim content");
    uint8_t want_nf[32], sec[32] = {1};
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) "zxv/jr-null/v1", 14);
    sha256_update(&h, sec, 32);
    p64(&h, 100);
    sha256_final(&h, want_nf);
    CHECK(memcmp(want_nf, c.nullifier, 32) == 0, "nullifier = sha256(dom || secret || epoch)");
    jr_zone_claim_msg(&c, msg);
    CHECK(memcmp(msg, "zxv/jr-zclaim/v1", 16) == 0 && msg[48] == 10 && msg[52] == 0x0F &&
              msg[56] == 1 && msg[60] == LPRES_STATE_TRUE && msg[61] == 100 &&
              memcmp(msg + 69, c.nullifier, 32) == 0 && memcmp(msg + 16, c.table_hash, 32) == 0,
          "claim message layout");
    CHECK(jr_zone_attest(Z, NZ, P(0, 0), NULL, 1, &c2) == JR_E_ARG &&
              jr_zone_attest(Z, NZ, P(0, 0), sec, 1, NULL) == JR_E_ARG,
          "attest null args");
    jr_zone_t bad = {1, JR_ZONE, 1, 1, 1, SQ_A};
    CHECK(jr_zone_attest(&bad, 1, P(0, 0), sec, 1, &c2) == JR_E_ARG, "attest bad table");

    jr_zone_ok_t z;
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_TRUE &&
              z.zone_id == 10 && z.caps == 0x0F && z.region == 1,
          "verified TRUE");
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_E_REPLAY, "replay refused");
    make_claim(P(2 * M6, 2 * M6), 2, 90, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_TRUE,
          "age == max_age is fresh");
    make_claim(P(2 * M6, 2 * M6), 3, 89, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_NEITHER &&
              z.zone_id == 0 && z.caps == 0,
          "stale reads NEITHER");
    make_claim(P(2 * M6, 2 * M6), 3, 101, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_NEITHER,
          "future reads NEITHER");
    make_claim(P(2 * M6, 2 * M6), 4, 100, &c, pf);
    pf[0] ^= 1;
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_E_PROOF, "bad proof refused");
    pf[0] ^= 1;
    CHECK(jr_zone_verify(&R, &c, pf, 31, 100, &z) == JR_E_PROOF, "short proof refused");
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK, "a refused proof records nothing");
    /* forged mask: re-sign a claim with a wider mask */
    make_claim(P(5 * M6, 15 * M6), 5, 100, &c, pf);
    CHECK(c.zone_id == 20, "in B");
    c.caps = 0x0F;
    jr_zone_claim_msg(&c, msg);
    sign(PROOF_KEY, msg, JR_CLAIM_MSG_LEN, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_E_CLAIM, "inflated mask refused");
    c.caps = 0x06;
    c.region = 1;
    jr_zone_claim_msg(&c, msg);
    sign(PROOF_KEY, msg, JR_CLAIM_MSG_LEN, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_E_CLAIM, "wrong region refused");
    c.region = 2;
    c.zone_id = 99;
    jr_zone_claim_msg(&c, msg);
    sign(PROOF_KEY, msg, JR_CLAIM_MSG_LEN, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_E_CLAIM, "a sanction id is no zone");
    c.zone_id = 12345;
    jr_zone_claim_msg(&c, msg);
    sign(PROOF_KEY, msg, JR_CLAIM_MSG_LEN, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_E_CLAIM, "unknown zone refused");
    c.verdict = 4;
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_E_CLAIM, "verdict out of range");
    make_claim(P(5 * M6, 35 * M6), 6, 100, &c, pf);
    CHECK(c.verdict == LPRES_STATE_BOTH, "D claim is BOTH");
    c.verdict = LPRES_STATE_TRUE;
    jr_zone_claim_msg(&c, msg);
    sign(PROOF_KEY, msg, JR_CLAIM_MSG_LEN, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_BOTH &&
              z.zone_id == 40,
          "TRUE claim with a reserved bit becomes BOTH");
    make_claim(P(5 * M6, 10 * M6), 7, 100, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_BOTH,
          "border claim BOTH");
    make_claim(P(8 * M6 + 5, 8 * M6 + 5), 8, 100, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_FALSE &&
              z.zone_id == 0,
          "sanctioned claim FALSE");
    make_claim(P(-5, -5), 9, 100, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_NEITHER,
          "nowhere claim NEITHER");
    make_claim(P(2 * M6, 2 * M6), 10, 100, &c, pf);
    c.table_hash[5] ^= 1;
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_NEITHER,
          "other map version NEITHER");
    CHECK(jr_zone_verify(NULL, &c, pf, 32, 100, &z) == JR_E_ARG &&
              jr_zone_verify(&R, NULL, pf, 32, 100, &z) == JR_E_ARG &&
              jr_zone_verify(&R, &c, pf, 32, 100, NULL) == JR_E_ARG,
          "verify null args");
    /* stub slot */
    R.hooks.proof_verify = NULL;
    make_claim(P(2 * M6, 2 * M6), 11, 100, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK && z.verdict == LPRES_STATE_NEITHER,
          "no proof backend: NEITHER");
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &z) == JR_OK, "stub records no nullifier");
    R.hooks.proof_verify = sig_ok;
    /* nullifier window */
    setup();
    int ok = 0;
    for (uint32_t i = 0; i < JR_MAX_NULLIFIERS; i++) {
        make_claim(P(2 * M6, 2 * M6), (uint8_t) i, 50 + (i % 2), &c, pf);
        ok += jr_zone_verify(&R, &c, pf, 32, 55, &z) == JR_OK;
    }
    CHECK(ok == (int) JR_MAX_NULLIFIERS && R.nnull == JR_MAX_NULLIFIERS, "window filled");
    make_claim(P(2 * M6, 2 * M6), 200, 55, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 55, &z) == JR_E_FULL, "full window refuses");
    CHECK(jr_zone_verify(&R, &c, pf, 32, 60, &z) == JR_E_FULL,
          "at 60 the epoch-50 entries are exactly max_age old: kept");
    CHECK(jr_zone_verify(&R, &c, pf, 32, 61, &z) == JR_OK && R.nnull == 65,
          "at 61 the epoch-50 entries are evicted");
    make_claim(P(2 * M6, 2 * M6), 201, 61, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 62, &z) == JR_OK && R.nnull == 2,
          "at 62 only the epoch-55 entry and the new one remain");
}

static void t_setup_rules(void)
{
    reset_counts();
    hooks();
    jr_hooks_t nb = H;
    nb.burn = NULL;
    CHECK(jr_init(&R, &nb, Z, NZ, 10) == JR_E_ARG, "burn hook required");
    CHECK(jr_init(NULL, &H, Z, NZ, 10) == JR_E_ARG && jr_init(&R, NULL, Z, NZ, 10) == JR_E_ARG,
          "init null args");
    jr_zone_t bad = {1, JR_ZONE, 1, 1, 2, SQ_A};
    CHECK(jr_init(&R, &H, &bad, 1, 10) == JR_E_ARG, "init bad table");
    CHECK(jr_init(&R, &H, Z, NZ, 10) == JR_OK, "init");
    CHECK(R.profile == JR_PROFILE_COMMUNITY && !R.enabled && R.next_receipt == 1,
          "starts community, off");
    jr_request_t q = req(U1, 1000, 0, false, false);
    jr_zone_ok_t z = ZA();
    jr_receipt_t o;
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_E_PROFILE && nburn == 0, "community refuses");
    CHECK(jr_enable(&R, true) == JR_E_PROFILE && !R.enabled, "cannot enable in community");
    CHECK(jr_enable(&R, false) == JR_OK, "disable is always allowed");
    CHECK(jr_set_profile(&R, JR_PROFILE_INSTITUTION) == JR_OK, "institution");
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_E_PROFILE, "institution but not enabled");
    CHECK(jr_enable(&R, true) == JR_OK && R.enabled, "enable");
    CHECK(jr_set_profile(&R, JR_PROFILE_COMMUNITY) == JR_OK && !R.enabled,
          "back to community disables");
    CHECK(jr_set_profile(&R, (jr_profile_t) 2) == JR_E_ARG && jr_set_profile(NULL, 1) == JR_E_ARG,
          "bad profile");
    CHECK(jr_enable(NULL, true) == JR_E_ARG, "enable null");

    jr_asset_t a = {1, JR_CAP_COMMODITY, 1, 1, 1, 0, false, false};
    CHECK(jr_add_asset(&R, &a) == JR_OK && R.assets[0].active, "add, forced active");
    CHECK(jr_add_asset(&R, &a) == JR_E_ARG, "duplicate id");
    jr_asset_t b = a;
    b.id = 0;
    CHECK(jr_add_asset(&R, &b) == JR_E_ARG, "id 0");
    b = a;
    b.id = 2;
    b.cap = 0x03;
    CHECK(jr_add_asset(&R, &b) == JR_E_ARG, "two bits");
    b.cap = 0x10;
    CHECK(jr_add_asset(&R, &b) == JR_E_ARG, "reserved bit");
    b.cap = 0;
    CHECK(jr_add_asset(&R, &b) == JR_E_ARG, "no bit");
    b.cap = JR_CAP_METALS;
    b.num = 0;
    CHECK(jr_add_asset(&R, &b) == JR_E_ARG, "num 0");
    b.num = 1;
    b.den = 0;
    CHECK(jr_add_asset(&R, &b) == JR_E_ARG, "den 0");
    b.den = 1;
    b.lot = 0;
    CHECK(jr_add_asset(&R, &b) == JR_E_ARG, "lot 0");
    b.lot = 2;
    b.den = (uint64_t) 1 << 63;
    CHECK(jr_add_asset(&R, &b) == JR_E_RANGE, "den*lot = 2^64 refused");
    b.lot = 1;
    b.den = UINT64_MAX;
    CHECK(jr_add_asset(&R, &b) == JR_OK, "den*lot = 2^64-1 fits");
    CHECK(jr_add_asset(NULL, &b) == JR_E_ARG && jr_add_asset(&R, NULL) == JR_E_ARG, "null");
    for (uint32_t i = 3; i <= JR_MAX_ASSETS; i++) {
        b.id = i;
        CHECK(jr_add_asset(&R, &b) == JR_OK, "fill");
    }
    b.id = 99;
    CHECK(jr_add_asset(&R, &b) == JR_E_FULL, "asset table full");
    CHECK(jr_retire_asset(&R, 77) == JR_E_NOTFOUND && jr_retire_asset(NULL, 1) == JR_E_NOTFOUND,
          "retire unknown");
    CHECK(jr_retire_asset(&R, 2) == JR_OK && !R.assets[1].active, "retire");
    CHECK(jr_set_n_asset(&R, 1) == JR_E_ARG, "N asset must be compute");
    CHECK(jr_set_n_asset(&R, 555) == JR_E_NOTFOUND, "N asset unknown");
    CHECK(jr_set_n_asset(&R, 0) == JR_OK && R.n_asset == 0, "N asset cleared");
    CHECK(jr_set_n_asset(NULL, 0) == JR_E_ARG, "N null");
    CHECK(jr_add_registry(&R, 0, reg, NULL) == JR_E_ARG, "registry id 0");
    CHECK(jr_add_registry(&R, 1, NULL, NULL) == JR_E_ARG, "registry fn null");
    CHECK(jr_add_registry(NULL, 1, reg, NULL) == JR_E_ARG, "registry null router");
    for (uint32_t i = 1; i <= JR_MAX_REGISTRIES; i++)
        CHECK(jr_add_registry(&R, i, reg, NULL) == JR_OK, "registry");
    CHECK(jr_add_registry(&R, 9, reg, NULL) == JR_E_FULL, "registries full");

    uint8_t hb[JR_LOT_HDR_LEN];
    jr_lot_hdr_t lh = lot(1, 1);
    jr_lot_hdr_encode(&lh, hb);
    CHECK(jr_add_lot(&R, 77, hb) == JR_E_NOTFOUND, "lot for unknown asset");
    CHECK(jr_add_lot(&R, 3, hb) == JR_OK, "metals lot");
    CHECK(jr_add_lot(&R, 1, hb) == JR_E_ARG, "same warehouse+serial refused across assets");
    lh.warehouse_id = 2;
    jr_lot_hdr_encode(&lh, hb);
    CHECK(jr_add_lot(&R, 1, hb) == JR_OK, "same serial, other warehouse");
    hb[0] = 0;
    CHECK(jr_add_lot(&R, 1, hb) == JR_E_FORMAT, "bad header");
    CHECK(jr_add_lot(NULL, 1, hb) == JR_E_ARG && jr_add_lot(&R, 1, NULL) == JR_E_ARG, "null");
    CHECK(jr_free_lots(&R, 1) == 1 && jr_free_lots(&R, 3) == 1 && jr_free_lots(NULL, 1) == 0,
          "free lots");
    jr_asset_t comp = {50, JR_CAP_COMPUTE, 1, 1, 1, 0, false, false};
    (void) comp;
    R.assets[2].cap = JR_CAP_COMPUTE;
    lh.warehouse_id = 3;
    jr_lot_hdr_encode(&lh, hb);
    CHECK(jr_add_lot(&R, 3, hb) == JR_E_ARG, "no lots for a compute asset");
    R.assets[2].cap = JR_CAP_MUTUAL;
    CHECK(jr_add_lot(&R, 3, hb) == JR_E_ARG, "no lots for a mutual asset");
    R.assets[2].cap = JR_CAP_COMMODITY;
    for (uint32_t i = R.nlots; i < JR_MAX_LOTS; i++) {
        lh.warehouse_id = 1000 + i;
        jr_lot_hdr_encode(&lh, hb);
        CHECK(jr_add_lot(&R, 3, hb) == JR_OK, "fill lots");
    }
    lh.warehouse_id = 99999;
    jr_lot_hdr_encode(&lh, hb);
    CHECK(jr_add_lot(&R, 3, hb) == JR_E_FULL, "lot table full");
}

static void t_hierarchy(void)
{
    jr_receipt_t o;
    jr_zone_ok_t z = ZA();
    jr_request_t q;
    uint64_t p, qq;

    /* level 1, exact p/q */
    setup();
    q = req(U1, 2500, 1, false, false);
    snap();
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK, "olive oil");
    CHECK(o.outcome == JR_OUT_ALLOTTED && o.asset_id == 1 && o.cap == JR_CAP_COMMODITY &&
              o.lots == 7 && o.nlot_idx == 7 && o.voucher_num == 500 && o.voucher_den == 3 &&
              o.route == LPRES_STATE_TRUE && o.transferable && o.escrow_id == 0 &&
              o.vino_burned == 2500 && o.receipt_id == 1,
          "7 barrels, 500/3 VINO voucher");
    CHECK(o.lot_idx[0] == 0 && o.lot_idx[6] == 6 && R.lots[6].state == 2 && R.lots[7].state == 0,
          "first free lots, delivered");
    check_seal(&o, "receipt digest");
    CHECK(o.event == JR_EVENT_DISCRETIONARY_ALLOTMENT, "allotment event");
    CHECK(nburn == 1 && burned == 2500 && R.burned_total == 2500 && ndeliver == 1 &&
              last_deliver.lots == 7 && memcmp(last_deliver.digest, o.digest, 32) == 0,
          "burn then deliver hook");
    CHECK(jr_free_lots(&R, 1) == 3, "3 barrels left");
    CHECK(jr_voucher(&R, U1, 1, &p, &qq) == JR_OK && p == 500 && qq == 3, "voucher 500/3");
    CHECK(jr_voucher(&R, U2, 1, &p, &qq) == JR_OK && p == 0 && qq == 1, "no voucher 0/1");
    CHECK(jr_voucher(&R, U1, 77, &p, &qq) == JR_E_NOTFOUND, "voucher unknown asset");
    CHECK(jr_voucher(NULL, U1, 1, &p, &qq) == JR_E_ARG && jr_voucher(&R, NULL, 1, &p, &qq) ==
              JR_E_ARG && jr_voucher(&R, U1, 1, NULL, &qq) == JR_E_ARG &&
              jr_voucher(&R, U1, 1, &p, NULL) == JR_E_ARG,
          "voucher null args");

    /* accumulate: zero lots, voucher grows */
    q = req(U1, 100, 1, true, false);
    snap();
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_VOUCHER_ONLY &&
              o.lots == 0 && o.nlot_idx == 0 && o.voucher_num == 800 && o.voucher_den == 3 &&
              o.receipt_id == 2,
          "accumulate to 800/3");
    check_seal(&o, "voucher-only digest");
    CHECK(o.event == JR_EVENT_DISCRETIONARY_ALLOTMENT, "voucher-only event");
    CHECK(strcmp(JR_UI_ACTION, "Accept Discretionary System Allotment") == 0 &&
              strstr(JR_UI_NOTICE, "without contractual consideration") != NULL,
          "UI wording");
    /* 67 more VINO: 201 + 800 = 1001 -> one barrel, 1/3 left */
    q = req(U1, 67, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1 && o.lots == 1 &&
              o.voucher_num == 1 && o.voucher_den == 3,
          "voucher completes a barrel");
    /* exactly: 1/3 + 333 VINO... 999+1 = 1000 -> 1 barrel, voucher cleared */
    q = req(U1, 333, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.lots == 1 && o.voucher_num == 0 &&
              o.voucher_den == 1 && R.nvouchers == 0,
          "voucher spent to zero is removed");
    CHECK(jr_free_lots(&R, 1) == 1, "one barrel left");

    /* dust without accumulate falls through: metals dust, compute needs an
     * operator, mutual delivers */
    q = req(U2, 100, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 4 && o.lots == 100 &&
              o.cap == JR_CAP_MUTUAL,
          "falls to level 3");
    q = req(U2, 100, 1, false, true);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 3 && o.lots == 6553600,
          "operator node gets compute (16.16 scaling)");
    /* inventory short: 2 barrels wanted, 1 left -> metals (2000 VINO -> 40 g -> 1 lot of 31 g) */
    q = req(U2, 2000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 2 && o.lots == 1 &&
              o.voucher_num == 450 && o.voucher_den == 1 && o.lot_idx[0] == 10,
          "not enough barrels: metals");
    /* no preference: level order commodity first */
    q = req(U3, 334, 0, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1 && o.lots == 1 &&
              o.voucher_num == 2 && o.voucher_den == 3,
          "no preference: commodity first");
    q = req(U3, 2000, 0, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 2,
          "barrels gone: metals before compute");
    q = req(U3, 1000, 4, false, true);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 4,
          "asked-for mutual wins over level order");
    q = req(U3, 2000, 77, false, true);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 2, "unknown preference ignored");
    q = req(U3, 1000, 0, false, true);
    jr_zone_ok_t zc = zok(LPRES_STATE_TRUE, 30, 0x04, 1);
    CHECK(jr_accept_allotment(&R, &q, &zc, &o) == JR_OK && o.asset_id == 4, "zone C: mutual only");
    jr_zone_ok_t zb = zok(LPRES_STATE_TRUE, 20, 0x06, 2);
    q = req(U3, 1000, 1, false, true);
    CHECK(jr_accept_allotment(&R, &q, &zb, &o) == JR_OK && o.asset_id == 3, "zone B: compute");
    q = req(U3, 1000, 4, false, false);
    int before = nescrow;
    CHECK(jr_accept_allotment(&R, &q, &zb, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              o.asset_id == 0 && o.escrow_id == o.receipt_id && nescrow == before + 1 &&
              last_escrow.reason == JR_ESC_NO_ROUTE && last_escrow.nclaims == 1 &&
              last_escrow.claims[0].verdict == LPRES_STATE_TRUE && last_escrow.zone_id == 20,
          "zone B, no operator, mutual cross-border: level 4 escrow");
    CHECK(o.voucher_num == 0 && o.voucher_den == 1, "no-plan escrow voucher 0/1");
    /* overflow never wraps: 2^63 VINO of compute is past 64 bits */
    q = req(U3, (uint64_t) 1 << 63, 0, false, true);
    CHECK(jr_accept_allotment(&R, &q, &zb, &o) == JR_OK && o.outcome == JR_OUT_ESCROW,
          "compute overflow: no candidate, escrow");
    q = req(U3, ((uint64_t) 1 << 48) - 1, 0, false, true);
    CHECK(jr_accept_allotment(&R, &q, &zb, &o) == JR_OK && o.asset_id == 3 &&
              o.lots == (((uint64_t) 1 << 48) - 1) << 16,
          "largest compute that fits");
    /* dust everywhere permitted */
    jr_zone_ok_t zm = zok(LPRES_STATE_TRUE, 10, JR_CAP_METALS, 1);
    q = req(U3, 10, 0, false, false);
    before = nburn;
    CHECK(jr_accept_allotment(&R, &q, &zm, &o) == JR_E_DUST && nburn == before, "dust: nothing burned");
    /* accumulate only applies to the asked-for asset */
    static const uint8_t U9[32] = {9};
    q = req(U9, 10, 2, true, false);
    CHECK(jr_accept_allotment(&R, &q, &zm, &o) == JR_OK && o.outcome == JR_OUT_VOUCHER_ONLY &&
              o.voucher_num == 10 && o.voucher_den == 1 && o.nlot_idx == 0,
          "accumulate metals");
    q = req(U3, 10, 0, true, false);
    CHECK(jr_accept_allotment(&R, &q, &zm, &o) == JR_E_DUST, "accumulate without a preference is dust");
    /* retired asset is skipped */
    CHECK(jr_retire_asset(&R, 4) == JR_OK, "retire mutual");
    q = req(U3, 1000, 4, false, false);
    CHECK(jr_accept_allotment(&R, &q, &zc, &o) == JR_OK && o.outcome == JR_OUT_ESCROW,
          "retired mutual: escrow");
    /* zero and null */
    q = req(U3, 0, 0, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_E_ARG, "zero VINO");
    q = req(U3, 1, 0, false, false);
    CHECK(jr_accept_allotment(NULL, &q, &z, &o) == JR_E_ARG && jr_accept_allotment(&R, NULL, &z, &o) == JR_E_ARG &&
              jr_accept_allotment(&R, &q, NULL, &o) == JR_E_ARG && jr_accept_allotment(&R, &q, &z, NULL) == JR_E_ARG,
          "null args");
    /* more than 32 lots at once is no candidate */
    setup();
    add_lots(1, 300, 30, 1);
    q = req(U1, 11000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 4, "33 barrels: falls through");
    q = req(U1, 10667, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1 && o.lots == 32 &&
              o.nlot_idx == 32,
          "32 barrels at once");
    /* region-bound asset matches its region only */
    setup();
    jr_zone_ok_t z9 = zok(LPRES_STATE_TRUE, 10, JR_CAP_COMPUTE, 9999);
    q = req(U1, 7, 0, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z9, &o) == JR_OK && o.asset_id == 5 && o.lots == 3 &&
              o.voucher_num == 0 && o.transferable,
          "region 9999 gets its own credit, transferable in hierarchy");
}

static void t_four_valued(void)
{
    jr_receipt_t o;
    jr_request_t q;
    jr_zone_ok_t z;
    setup();
    z = zok(LPRES_STATE_FALSE, 0, 0, 0);
    q = req(U1, 1000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_E_SANCTIONED && nburn == 0, "sanctioned");
    z = zok(LPRES_STATE_BOTH, 20, 0x06, 2);
    snap();
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              o.route == LPRES_STATE_BOTH && nburn == 1 && last_escrow.reason ==
              JR_ESC_ZONE_CONFLICT && last_escrow.asset_id == 0 &&
              last_escrow.claims[0].verdict == LPRES_STATE_BOTH &&
              last_escrow.claims[0].evidence[0] == 20,
          "zone BOTH: escrow");
    check_seal(&o, "escrow receipt digest");
    CHECK(o.event == JR_EVENT_CUSTODIAL_HOLD, "custodial hold event");
    uint8_t d[32];
    ref_escrow(jr_escrow_get(&R, o.escrow_id), d);
    CHECK(memcmp(d, jr_escrow_get(&R, o.escrow_id)->digest, 32) == 0, "escrow digest");
    z = zok(LPRES_STATE_TRUE, 40, 0x11, 1);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              o.route == LPRES_STATE_BOTH && last_escrow.reason == JR_ESC_ZONE_CONFLICT,
          "reserved bit under TRUE: escrow");
    z = zok(LPRES_STATE_TRUE, 40, 0x80000001u, 1);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW, "top bit too");
    /* NEITHER: local non-transferable credit */
    z = zok(LPRES_STATE_NEITHER, 0, 0, 0);
    q = req(U1, 10, 1, false, false);
    snap();
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_LOCAL_CREDIT &&
              o.asset_id == 5 && !o.transferable && o.lots == 4 && o.voucher_num == 2 &&
              o.voucher_den == 3 && o.route == LPRES_STATE_NEITHER,
          "NEITHER: 30/7 -> 4 units, 2/3 VINO voucher");
    check_seal(&o, "local credit digest");
    q = req(U1, 2, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.lots == 1 && o.voucher_num == 1 &&
              o.voucher_den == 3,
          "6+2 = 8 -> 1 unit, 1/3 left");
    q = req(U1, 1, 1, false, false);
    int b = nburn;
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_E_DUST && nburn == b, "NEITHER dust");
    CHECK(jr_retire_asset(&R, 5) == JR_OK, "retire N asset");
    q = req(U1, 100, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              last_escrow.reason == JR_ESC_NO_ROUTE &&
              last_escrow.claims[0].verdict == LPRES_STATE_NEITHER,
          "retired N asset: escrow");
    CHECK(jr_set_n_asset(&R, 0) == JR_OK, "clear N");
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW, "no N asset: escrow");
    setup();
    z = zok(LPRES_STATE_NEITHER, 0, 0, 0);
    q = req(U1, (uint64_t) 1 << 62, 0, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_LOCAL_CREDIT,
          "large N credit fits");

    /* registries */
    setup();
    CHECK(jr_add_registry(&R, 500, reg, NULL) == JR_OK, "registry 500");
    CHECK(jr_add_registry(&R, 501, reg, NULL) == JR_OK, "registry 501");
    z = ZA();
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_TRUE;
    q = req(U1, 1000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ALLOTTED &&
              o.route == LPRES_STATE_TRUE && nreg_calls == 2,
          "registries TRUE: deliver");
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_NEITHER;
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ALLOTTED,
          "registries NEITHER add nothing");
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_FALSE;
    q = req(U1, 1100, 1, false, false);
    snap();
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              o.route == LPRES_STATE_BOTH && o.asset_id == 1 && o.lots == 3 &&
              o.nlot_idx == 3 && o.voucher_num == 0,
          "transit flag: BOTH, plan frozen");
    check_seal(&o, "class escrow digest");
    uint64_t eid = o.escrow_id;
    const jr_escrow_t *e = jr_escrow_get(&R, eid);
    CHECK(e && e->reason == JR_ESC_CLASS_CONFLICT && e->nclaims == 3 &&
              e->claims[0].source == 0 && e->claims[0].verdict == LPRES_STATE_TRUE &&
              e->claims[1].source == 500 && e->claims[1].verdict == LPRES_STATE_FALSE &&
              e->claims[2].source == 501 && e->claims[1].evidence[0] == JR_CAP_COMMODITY &&
              e->claims[1].evidence[1] == 10 && e->lots == 3 && e->voucher_after == 300 &&
              e->vino == 1100 && R.lots[e->lot_idx[0]].state == 1,
          "claims pinned side by side, lots escrowed");
    ref_escrow(e, (uint8_t *) head_before);
    CHECK(memcmp(head_before, e->digest, 32) == 0, "class escrow digest");
    CHECK(jr_free_lots(&R, 1) == 1, "escrowed lots are not free");
    /* frozen voucher: U1 cannot route to olive while escrow open */
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_TRUE;
    q = req(U1, 2000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 2, "frozen asset skipped");
    q = req(U2, 334, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1, "other holders unaffected");
    reg_ans[JR_CAP_METALS] = 9;
    q = req(U2, 2000, 2, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              last_escrow.claims[1].verdict == LPRES_STATE_BOTH,
          "an out-of-range answer is BOTH");
}

static void t_rulings(void)
{
    jr_receipt_t o, o2;
    jr_request_t q;
    jr_zone_ok_t z = ZA();
    setup();
    add_lots(1, 300, 30, 1);
    CHECK(jr_add_registry(&R, 500, reg, NULL) == JR_OK, "registry");
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_FALSE;
    q = req(U1, 2500, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW, "class escrow");
    uint64_t eid = o.escrow_id;
    uint8_t s[32] = {0};
    CHECK(jr_escrow_settle(&R, eid, JR_RULE_DELIVER, 0, 0, s, 32, &o2) == JR_E_SIG, "bad sig");
    CHECK(settle(eid, 9, 0, 0, &o2) == JR_E_RULING && settle(eid, 0, 0, 0, &o2) == JR_E_RULING,
          "bad kind");
    CHECK(settle(4242, JR_RULE_DELIVER, 0, 0, &o2) == JR_E_NOTFOUND, "unknown escrow");
    CHECK(settle(0, JR_RULE_DELIVER, 0, 0, &o2) == JR_E_NOTFOUND, "escrow id 0");
    CHECK(jr_escrow_settle(NULL, eid, 1, 0, 0, s, 32, &o2) == JR_E_ARG &&
              jr_escrow_settle(&R, eid, 1, 0, 0, s, 32, NULL) == JR_E_ARG,
          "settle null");
    R.hooks.arbiter_verify = NULL;
    CHECK(settle(eid, JR_RULE_DELIVER, 0, 0, &o2) == JR_E_SIG, "no arbiter hook");
    R.hooks.arbiter_verify = sig_ok;
    /* a ruling signed for other caps is refused */
    uint8_t msg[JR_RULING_MSG_LEN];
    jr_ruling_msg(jr_escrow_get(&R, eid), JR_RULE_REROUTE, 4, 1, msg);
    CHECK(memcmp(msg, "zxv/jr-ruling/v1", 16) == 0 && msg[16] == (uint8_t) eid &&
              msg[24] == JR_RULE_REROUTE && msg[25] == 4 && msg[29] == 1 &&
              memcmp(msg + 33, jr_escrow_get(&R, eid)->digest, 32) == 0,
          "ruling message layout");
    sign(ARB_KEY, msg, JR_RULING_MSG_LEN, s);
    CHECK(jr_escrow_settle(&R, eid, JR_RULE_REROUTE, 6, 1, s, 32, &o2) == JR_E_SIG,
          "caps are signed");
    int burns = nburn;
    snap();
    CHECK(settle(eid, JR_RULE_DELIVER, 0, 0, &o2) == JR_OK && o2.outcome == JR_OUT_ALLOTTED &&
              o2.lots == 7 && o2.asset_id == 1 && o2.voucher_num == 500 && o2.voucher_den == 3 &&
              o2.route == LPRES_STATE_TRUE && nburn == burns && R.lots[o.lot_idx[0]].state == 2,
          "DELIVER commits the frozen plan without a second burn");
    check_seal(&o2, "deliver digest");
    CHECK(o2.event == JR_EVENT_CUSTODIAL_IN_KIND_DISBURSEMENT, "release is an in-kind disbursement");
    CHECK(R.burned_total == 2500, "burned once");
    CHECK(settle(eid, JR_RULE_DELIVER, 0, 0, &o2) == JR_E_SETTLED, "settled once");
    CHECK(jr_escrow_get(&R, eid)->settled && jr_escrow_get(&R, eid)->ruling == JR_RULE_DELIVER,
          "ruling recorded");
    q = req(U1, 500, 1, false, false);
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_TRUE;
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1 && o.lots == 2 &&
              o.voucher_num == 0,
          "voucher unfrozen and used: 1500 + 500 = 2000 -> 2 barrels");
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_FALSE;

    /* REFUND */
    q = req(U2, 3000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW, "escrow 2");
    eid = o.escrow_id;
    uint32_t free_before = jr_free_lots(&R, 1);
    refund_fail = 1;
    CHECK(settle(eid, JR_RULE_REFUND, 0, 0, &o2) == JR_E_REFUND &&
              !jr_escrow_get(&R, eid)->settled && jr_free_lots(&R, 1) == free_before,
          "failed refund changes nothing");
    refund_fail = 0;
    R.hooks.refund = NULL;
    CHECK(settle(eid, JR_RULE_REFUND, 0, 0, &o2) == JR_E_REFUND, "no refund hook");
    R.hooks.refund = refund;
    CHECK(settle(eid, JR_RULE_REFUND, 0, 0, &o2) == JR_OK && o2.outcome == 0 && nrefund == 1 &&
              refunded == 3000 && R.refunded_total == 3000 &&
              jr_free_lots(&R, 1) == free_before + 9,
          "refund returns VINO and frees lots");

    /* REROUTE */
    q = req(U3, 2000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW, "escrow 3");
    eid = o.escrow_id;
    burns = nburn;
    int calls = nreg_calls;
    snap();
    CHECK(settle(eid, JR_RULE_REROUTE, JR_CAP_COMMODITY, 1, &o2) == JR_OK &&
              o2.outcome == JR_OUT_ALLOTTED && o2.asset_id == 1 && o2.lots == 6 &&
              nburn == burns && nreg_calls == calls,
          "REROUTE with the arbiter's mask skips registries");
    check_seal(&o2, "reroute digest");
    CHECK(o2.event == JR_EVENT_CUSTODIAL_IN_KIND_DISBURSEMENT, "reroute release event");
    q = req(U3, 2000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW, "escrow 4");
    eid = o.escrow_id;
    CHECK(settle(eid, JR_RULE_REROUTE, JR_CAP_MUTUAL, 1, &o2) == JR_OK && o2.asset_id == 4 &&
              o2.lots == 2000,
          "REROUTE to mutual credit");
    q = req(U3, 2000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK, "escrow 5");
    eid = o.escrow_id;
    CHECK(settle(eid, JR_RULE_REROUTE, 0, 1, &o2) == JR_OK && o2.outcome == JR_OUT_ESCROW &&
              o2.escrow_id != eid && jr_escrow_get(&R, eid)->settled &&
              jr_escrow_get(&R, o2.escrow_id)->reason == JR_ESC_NO_ROUTE &&
              jr_escrow_get(&R, o2.escrow_id)->vino == 2000,
          "REROUTE with no permission parks in a new escrow");
    uint64_t parked = o2.escrow_id;
    CHECK(settle(parked, JR_RULE_DELIVER, 0, 0, &o2) == JR_E_RULING, "no plan to deliver");
    CHECK(settle(parked, JR_RULE_REROUTE, JR_CAP_METALS, 1, &o2) == JR_OK && o2.asset_id == 2,
          "parked escrow rerouted to metals");
    /* reroute dust: 10 VINO with only metals permitted */
    q = req(U3, 10, 2, true, false);
    reg_ans[JR_CAP_METALS] = LPRES_STATE_FALSE;
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              o.asset_id == 2 && o.lots == 0,
          "accumulate escrowed");
    eid = o.escrow_id;
    CHECK(settle(eid, JR_RULE_REROUTE, JR_CAP_METALS, 1, &o2) == JR_OK &&
              o2.outcome == JR_OUT_ESCROW,
          "reroute dust is parked, never lost");
    CHECK(settle(eid, JR_RULE_DELIVER, 0, 0, &o2) == JR_E_SETTLED, "old escrow settled");
    /* reroute into a ZONE conflict (reserved bit in the ruling) */
    q = req(U3, 1000, 1, false, false);
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_FALSE;
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK, "escrow 6");
    eid = o.escrow_id;
    CHECK(settle(eid, JR_RULE_REROUTE, 0x20, 1, &o2) == JR_OK && o2.outcome == JR_OUT_ESCROW &&
              jr_escrow_get(&R, o2.escrow_id)->reason == JR_ESC_ZONE_CONFLICT,
          "a ruling with a reserved bit escrows again");
    CHECK(R.burned_total == burned && R.refunded_total == refunded, "totals match the hooks");
}

/* R11: software first; the physical layer is off until lots exist. */
static void t_software_first(void)
{
    jr_receipt_t o;
    jr_request_t q;
    jr_zone_ok_t z = ZA();
    reset_counts();
    hooks();
    CHECK(jr_init(&R, &H, Z, NZ, 10) == JR_OK && !R.physical_online, "cold node: physical off");
    CHECK(jr_set_profile(&R, JR_PROFILE_INSTITUTION) == JR_OK && jr_enable(&R, true) == JR_OK,
          "on");
    jr_asset_t a1 = {1, JR_CAP_COMMODITY, 3, 5, 200, 0, false, false};
    jr_asset_t a3 = {3, JR_CAP_COMPUTE, JR_TPC_TICKS_PER_VINO, 1, 1, 0, false, false};
    jr_asset_t a4 = {4, JR_CAP_MUTUAL, 1, 1, 1, 1, false, false};
    CHECK(jr_add_asset(&R, &a1) == JR_OK && jr_add_asset(&R, &a3) == JR_OK &&
              jr_add_asset(&R, &a4) == JR_OK,
          "assets");
    CHECK(jr_set_physical(&R, true) == JR_E_NOTFOUND && !R.physical_online,
          "no lots: the physical layer cannot come on");
    CHECK(jr_set_physical(NULL, true) == JR_E_ARG, "null");
    q = req(U1, 3, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 3 && o.lots == 196608,
          "olive oil asked, compute allotted: 3 VINO = 3 * 2^16 ticks");
    add_lots(1, 100, 2, 1);
    q = req(U1, 334, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 3,
          "lots present but the layer is off: still compute");
    CHECK(jr_free_physical(&R) == 2 && jr_free_physical(NULL) == 0, "free physical");
    CHECK(jr_set_physical(&R, true) == JR_OK, "layer on");
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1 && o.lots == 1,
          "now olive oil");
    uint8_t b[JR_LOT_HDR_LEN];
    jr_lot_hdr_t h = lot(500, 1);
    jr_lot_hdr_encode(&h, b);
    CHECK(jr_add_lot_src(&R, 1, b, 7) == JR_OK && R.lots[2].source == 7, "sourced lot");
    CHECK(jr_withdraw_source(&R, 0) == 0 && jr_withdraw_source(NULL, 7) == 0, "bad withdraw");
    CHECK(jr_withdraw_source(&R, 7) == 1 && R.lots[2].state == 3 && R.physical_online,
          "withdrawn; one direct lot still free");
    CHECK(jr_free_lots(&R, 1) == 1, "withdrawn lots are not free");
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1, "last barrel");
    CHECK(R.physical_online, "a release does not switch the layer off by itself");
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 3,
          "no barrels left: nothing hangs, compute");
    CHECK(jr_withdraw_source(&R, 7) == 0 && !R.physical_online, "empty: layer off");
    CHECK(jr_set_physical(&R, false) == JR_OK, "off is always allowed");
}


/* Re-seal a header after editing it so only the semantic checks can refuse it. */
static void reseal(uint8_t b[JR_LOT_HDR_LEN])
{
    uint8_t d[32];
    sha256(b, 124, d);
    memcpy(b + 124, d, 4);
}

static void t_extras(void)
{
    jr_receipt_t o, o2;
    jr_request_t q;
    jr_zone_ok_t z = ZA();
    jr_lot_hdr_t g;
    uint8_t b[JR_LOT_HDR_LEN], c[JR_LOT_HDR_LEN];
    jr_lot_hdr_t h = lot(1, 1);
    jr_lot_hdr_encode(&h, b);
    static const int pos[] = {0, 1, 2, 3, 4, 5, 38, 39, 112, 117, 123};
    for (unsigned i = 0; i < sizeof pos / sizeof pos[0]; i++) {
        memcpy(c, b, sizeof b);
        c[pos[i]] ^= 0x40;
        reseal(c);
        CHECK(jr_lot_hdr_decode(c, &g) == JR_E_FORMAT, "resealed magic/version/flags/reserved");
    }
    memcpy(c, b, sizeof b);
    c[60] ^= 1;
    reseal(c);
    CHECK(jr_lot_hdr_decode(c, &g) == JR_OK && g.vault_pubkey[20] == 1, "resealed key byte ok");

    /* users differing only in byte 31 are different holders */
    setup();
    uint8_t ua[32] = {9, [31] = 1}, ub[32] = {9, [31] = 2};
    q = req(ua, 100, 1, true, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.user[31] == 1, "ua accumulates");
    uint64_t p, qq;
    CHECK(jr_voucher(&R, ub, 1, &p, &qq) == JR_OK && p == 0, "ub has no voucher");
    CHECK(jr_voucher(&R, ua, 1, &p, &qq) == JR_OK && p == 100 && qq == 1, "ua has 300/3 = 100");
    q = req(ub, 100, 1, true, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && R.nvouchers == 2 &&
              R.vouchers[1].user[31] == 2,
          "ub gets its own line");
    /* removing a voucher that is not last keeps the other */
    q = req(ua, 234, 1, false, false); /* 702 + 300 = 1002 -> 1 lot, 2 left */
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.lots == 1 && o.voucher_num == 2 &&
              o.voucher_den == 3,
          "ua spends");
    q = req(ua, 666, 1, false, false); /* 1998 + 2 = 2000 -> 2 lots, 0 */
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.lots == 2 && R.nvouchers == 1 &&
              R.vouchers[0].user[31] == 2 && R.vouchers[0].r == 300,
          "first voucher removed, second moved down intact");
    /* metals with lot 0 untouched: commit marks only the planned lots */
    CHECK(R.lots[0].state == 2 && R.lots[3].state == 0, "lots 0-2 used");
    q = req(U2, 2000, 2, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 2 && o.nlot_idx == 1 &&
              R.lots[3].state == 0 && R.lots[11].state == 0,
          "one metals lot, nothing else marked");

    /* sublevel and level order with both deliverable */
    setup();
    q = req(U1, 2000, 0, false, true);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1,
          "commodity before metals when both deliver");
    jr_zone_ok_t zm = zok(LPRES_STATE_TRUE, 10, JR_CAP_METALS | JR_CAP_COMPUTE | JR_CAP_MUTUAL, 1);
    CHECK(jr_accept_allotment(&R, &q, &zm, &o) == JR_OK && o.asset_id == 2,
          "metals before compute");
    jr_zone_ok_t zcm = zok(LPRES_STATE_TRUE, 10, JR_CAP_COMPUTE | JR_CAP_MUTUAL, 1);
    CHECK(jr_accept_allotment(&R, &q, &zcm, &o) == JR_OK && o.asset_id == 3,
          "compute before mutual");

    /* add64 carry: num = 2^64-1, den*lot = 2^64-2 */
    setup();
    jr_asset_t big = {9, JR_CAP_MUTUAL, UINT64_MAX, ((uint64_t) 1 << 63) - 1, 2, 0, false, false};
    CHECK(jr_add_asset(&R, &big) == JR_OK, "big asset");
    q = req(U1, 1, 9, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 9 && o.lots == 1 &&
              R.vouchers[0].r == 1,
          "1 lot, r = 1");
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.lots == 1 && R.vouchers[0].r == 2,
          "carry into the high word: 1 lot, r = 2");
    /* gcd reduction */
    jr_asset_t g24 = {10, JR_CAP_MUTUAL, 24, 1000, 1, 0, false, false};
    CHECK(jr_add_asset(&R, &g24) == JR_OK, "g24");
    q = req(U2, 42, 10, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.voucher_num == 1 && o.voucher_den == 3,
          "8/24 = 1/3");
    q = req(U2, 43, 10, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.voucher_num == 5 && o.voucher_den == 3,
          "40/24 = 5/3");
    jr_asset_t g8 = {11, JR_CAP_MUTUAL, 8, 3, 7, 0, false, false};
    CHECK(jr_add_asset(&R, &g8) == JR_OK, "g8");
    q = req(U3, 5, 11, false, false); /* 40 = 1*21 + 19 */
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.lots == 1 && o.voucher_num == 19 &&
              o.voucher_den == 8,
          "19/8 stays");
    q = req(U3, 3, 11, false, false); /* 24 + 19 = 43 = 2*21 + 1 */
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.lots == 2 && o.voucher_num == 1 &&
              o.voucher_den == 8,
          "1/8");
    q = req(U3, 5, 11, false, false); /* 40 + 1 = 41 = 1*21 + 20 */
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.voucher_num == 5 && o.voucher_den == 2,
          "20/8 = 5/2");

    /* hooks are optional */
    setup();
    R.hooks.deliver = NULL;
    R.hooks.escrow = NULL;
    q = req(U1, 1000, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK, "no deliver hook");
    jr_zone_ok_t zb = zok(LPRES_STATE_BOTH, 1, 0, 0);
    CHECK(jr_accept_allotment(&R, &q, &zb, &o) == JR_OK && o.outcome == JR_OUT_ESCROW,
          "no escrow hook");
    CHECK(ndeliver == 0 && nescrow == 0, "nothing called");
    /* re-selecting INSTITUTION keeps it enabled */
    CHECK(jr_set_profile(&R, JR_PROFILE_INSTITUTION) == JR_OK && R.enabled, "stays enabled");

    /* frozen voucher with the escrow at index 0; escrow receipt shows the voucher */
    setup();
    CHECK(jr_add_registry(&R, 500, reg, NULL) == JR_OK, "registry");
    q = req(U1, 100, 1, true, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.voucher_num == 100, "voucher 300/3");
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_FALSE;
    q = req(U1, 334, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              R.nescrows == 1 && o.voucher_num == 100 && o.voucher_den == 1,
          "class escrow at index 0 reports the frozen voucher");
    uint64_t eid = o.escrow_id;
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_TRUE;
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 4,
          "frozen: olive oil skipped although barrels are free");
    q = req(U2, 334, 1, false, false);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.asset_id == 1, "U2 not frozen");
    /* DELIVER refused when the voucher table is full */
    for (uint32_t i = R.nvouchers; i < JR_MAX_VOUCHERS; i++) {
        uint8_t u[32] = {0x77, (uint8_t) i};
        q = req(u, 3, 4, true, false);
        jr_zone_ok_t zmu = zok(LPRES_STATE_TRUE, 10, JR_CAP_METALS, 1);
        q.want_asset = 2;
        CHECK(jr_accept_allotment(&R, &q, &zmu, &o) == JR_OK, "fill");
    }
    /* U1's voucher line exists, so DELIVER can reuse it */
    CHECK(settle(eid, JR_RULE_DELIVER, 0, 0, &o2) == JR_OK && o2.lots == 1 &&
              o2.voucher_num == 302 && o2.voucher_den == 3 && o2.user[31] == 0x71,
          "existing line reused even when full: 1002 + 300 -> 1 lot, 302/3 left");
    q = req(U3, 335, 1, false, false); /* 1005 -> r 5 */
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_FALSE;
    R.nvouchers = JR_MAX_VOUCHERS; /* still full */
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_E_FULL, "full before burn");
}

static void t_extras2(void)
{
    jr_receipt_t o, o2;
    jr_request_t q;
    jr_zone_ok_t z = ZA();
    /* DELIVER and REROUTE failing on a full voucher table restore the escrow */
    setup();
    CHECK(jr_add_registry(&R, 500, reg, NULL) == JR_OK, "registry");
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_FALSE;
    q = req(U3, 335, 1, false, false); /* 1005 -> 1 lot, r 5: needs a new line */
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK && o.outcome == JR_OUT_ESCROW, "escrow");
    uint64_t eid = o.escrow_id;
    uint16_t li = jr_escrow_get(&R, eid)->lot_idx[0];
    CHECK(li == 0 && R.lots[0].state == 1, "lot 0 escrowed");
    for (uint32_t i = 0; i < JR_MAX_VOUCHERS; i++) {
        uint8_t u[32] = {0x66, (uint8_t) i};
        q = req(u, 3, 2, true, false);
        jr_zone_ok_t zmu = zok(LPRES_STATE_TRUE, 10, JR_CAP_METALS, 1);
        CHECK(jr_accept_allotment(&R, &q, &zmu, &o) == JR_OK, "fill");
    }
    CHECK(settle(eid, JR_RULE_DELIVER, 0, 0, &o2) == JR_E_FULL && !jr_escrow_get(&R, eid)->settled,
          "DELIVER: full voucher table refused");
    CHECK(settle(eid, JR_RULE_REROUTE, JR_CAP_COMMODITY, 1, &o2) == JR_E_FULL &&
              !jr_escrow_get(&R, eid)->settled && R.lots[0].state == 1,
          "REROUTE: refused, escrow and its lot restored");
    CHECK(settle(eid, JR_RULE_REROUTE, JR_CAP_MUTUAL, 1, &o2) == JR_OK && o2.asset_id == 4 &&
              o2.user[31] == 0x73 && R.lots[0].state == 0,
          "REROUTE to mutual (no voucher) works and frees the lot");

    /* NEITHER path: N asset overflow escrows; claim evidence is zone 0 */
    setup();
    jr_asset_t huge = {12, JR_CAP_COMPUTE, (uint64_t) 1 << 40, 1, 1, 9999, false, false};
    CHECK(jr_add_asset(&R, &huge) == JR_OK && jr_set_n_asset(&R, 12) == JR_OK, "huge N");
    jr_zone_ok_t zn = zok(LPRES_STATE_NEITHER, 77, 0, 0);
    q = req(U1, (uint64_t) 1 << 40, 0, false, false);
    CHECK(jr_accept_allotment(&R, &q, &zn, &o) == JR_OK && o.outcome == JR_OUT_ESCROW &&
              last_escrow.claims[0].evidence[0] == 0 && last_escrow.claims[0].evidence[31] == 0,
          "N overflow: escrow, evidence zone 0");
    /* registry evidence: bytes it does not write are zero */
    setup();
    CHECK(jr_add_registry(&R, 500, reg, NULL) == JR_OK, "registry");
    reg_ans[JR_CAP_COMMODITY] = LPRES_STATE_FALSE;
    q = req(U1, 334, 1, false, true);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_OK, "escrow");
    const jr_escrow_t *e = jr_escrow_get(&R, o.escrow_id);
    CHECK(e->claims[1].evidence[2] == 0 && e->claims[1].evidence[31] == 0 &&
              e->claims[0].evidence[0] == 10 && e->claims[0].evidence[31] == 0 && e->operator_node,
          "evidence bytes");
    uint8_t d[32];
    ref_escrow(e, d);
    CHECK(memcmp(d, e->digest, 32) == 0, "operator node is in the escrow digest");

    /* escrow slot reuse finds a settled slot that is not slot 0 */
    setup();
    jr_zone_ok_t zb = zok(LPRES_STATE_BOTH, 1, 0, 0);
    uint64_t ids[JR_MAX_ESCROWS];
    for (uint32_t i = 0; i < JR_MAX_ESCROWS; i++) {
        q = req(U1, 5, 0, false, false);
        CHECK(jr_accept_allotment(&R, &q, &zb, &o) == JR_OK, "escrow");
        ids[i] = o.escrow_id;
    }
    CHECK(settle(ids[5], JR_RULE_REFUND, 0, 0, &o2) == JR_OK, "settle slot 5");
    CHECK(jr_accept_allotment(&R, &q, &zb, &o) == JR_OK && R.escrows[5].id == o.escrow_id &&
              jr_escrow_get(&R, ids[0]) != NULL,
          "slot 5 reused, slot 0 kept");

    /* direct lots are source 0; withdrawing source 1 leaves them */
    setup();
    CHECK(jr_withdraw_source(&R, 1) == 0 && R.lots[0].state == 0, "direct lots untouched");
    jr_router_t *rr = &R;
    reset_counts();
    hooks();
    CHECK(jr_init(rr, &H, Z, NZ, 10) == JR_OK, "init");
    jr_asset_t a1 = {1, JR_CAP_COMMODITY, 3, 5, 200, 0, false, false};
    CHECK(jr_add_asset(rr, &a1) == JR_OK, "a1");
    uint8_t b[JR_LOT_HDR_LEN];
    jr_lot_hdr_t h = lot(1, 1);
    jr_lot_hdr_encode(&h, b);
    CHECK(jr_add_lot_src(rr, 1, b, 3) == JR_OK, "lot 0 from source 3");
    h = lot(1, 2);
    jr_lot_hdr_encode(&h, b);
    CHECK(jr_add_lot(rr, 1, b) == JR_OK, "lot 1 direct");
    h = lot(1, 1);
    jr_lot_hdr_encode(&h, b);
    CHECK(jr_add_lot(rr, 1, b) == JR_E_ARG, "duplicate of lot 0");
    h = lot(1, 2);
    jr_lot_hdr_encode(&h, b);
    CHECK(jr_add_lot(rr, 1, b) == JR_E_ARG, "duplicate of lot 1");
    CHECK(jr_set_physical(rr, true) == JR_OK, "on");
    CHECK(jr_withdraw_source(rr, 3) == 1 && R.lots[0].state == 3 && R.physical_online,
          "source at index 0 withdrawn; lot 1 keeps it on");

    /* zone polygons: every edge midpoint and a 64-point polygon */
    uint32_t id, caps, rg;
    CHECK(jr_zone_locate(Z, NZ, P(0, 5 * M6), &id, &caps, &rg) == LPRES_STATE_TRUE && id == 10,
          "bottom edge of A");
    CHECK(jr_zone_locate(Z, NZ, P(2 * M6, 0), &id, &caps, &rg) == LPRES_STATE_TRUE && id == 10,
          "left edge of A");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6, 50 * M6), &id, &caps, &rg) == LPRES_STATE_TRUE &&
              id == 60,
          "triangle vertical edge");
    CHECK(jr_zone_locate(Z, NZ, P(0, 55 * M6), &id, &caps, &rg) == LPRES_STATE_TRUE && id == 60,
          "triangle bottom edge");
    CHECK(jr_zone_locate(Z, NZ, P(-1, 55 * M6), &id, &caps, &rg) == LPRES_STATE_NEITHER,
          "just below the triangle");
    CHECK(jr_zone_locate(Z, NZ, P(10 * M6 + 1, 5 * M6), &id, &caps, &rg) == LPRES_STATE_NEITHER,
          "just above A");
    CHECK(jr_zone_locate(Z, NZ, P(5 * M6, -1), &id, &caps, &rg) == LPRES_STATE_NEITHER,
          "just left of A");
    static jr_pt_t poly[JR_MAX_ZONE_PTS];
    for (uint32_t i = 0; i < JR_MAX_ZONE_PTS; i++) {
        /* a 64-gon on a 16x16 square perimeter, counter-clockwise */
        int32_t s = (int32_t) (i % 16) * M6, side = (int32_t) (i / 16);
        poly[i].lat = side == 0 ? 0 : side == 1 ? s : side == 2 ? 16 * M6 : 16 * M6 - s;
        poly[i].lon = side == 0 ? s : side == 1 ? 16 * M6 : side == 2 ? 16 * M6 - s : 0;
    }
    jr_zone_t pz = {1, JR_ZONE, 1, 1, JR_MAX_ZONE_PTS, poly};
    CHECK(jr_zone_locate(&pz, 1, P(8 * M6, 8 * M6), &id, &caps, &rg) == LPRES_STATE_TRUE,
          "64 points is valid");
    uint8_t h1[32], h2[32];
    jr_pt_t m3[4];
    memcpy(m3, SQ_A, sizeof m3);
    jr_zone_t zz = Z[0];
    jr_table_hash(&zz, 1, h1);
    m3[0].lat++;
    zz.pts = m3;
    jr_table_hash(&zz, 1, h2);
    CHECK(memcmp(h1, h2, 32) != 0, "first point is in the hash");

    /* verifier: out reset, replay of a later nullifier, FALSE claim with an id */
    setup();
    jr_zone_claim_t c;
    uint8_t pf[32], msg[JR_CLAIM_MSG_LEN];
    jr_zone_ok_t zo = {LPRES_STATE_TRUE, 5, 5, 5};
    make_claim(P(2 * M6, 2 * M6), 1, 100, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 200, &zo) == JR_OK && zo.verdict ==
              LPRES_STATE_NEITHER && zo.zone_id == 0 && zo.caps == 0 && zo.region == 0,
          "stale resets every output");
    make_claim(P(2 * M6, 2 * M6), 2, 100, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &zo) == JR_OK, "first");
    jr_zone_claim_t c2;
    uint8_t pf2[32];
    make_claim(P(2 * M6, 2 * M6), 3, 100, &c2, pf2);
    CHECK(jr_zone_verify(&R, &c2, pf2, 32, 100, &zo) == JR_OK, "second");
    CHECK(jr_zone_verify(&R, &c2, pf2, 32, 100, &zo) == JR_E_REPLAY, "replay of the second");
    make_claim(P(8 * M6 + 5, 8 * M6 + 5), 4, 100, &c, pf);
    c.zone_id = 10;
    c.caps = 0x0F;
    jr_zone_claim_msg(&c, msg);
    sign(PROOF_KEY, msg, JR_CLAIM_MSG_LEN, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &zo) == JR_OK && zo.verdict == LPRES_STATE_FALSE &&
              zo.zone_id == 0 && zo.caps == 0,
          "FALSE never carries a zone");
    make_claim(P(2 * M6, 2 * M6), 5, 100, &c, pf);
    c.table_hash[31] ^= 1;
    CHECK(jr_zone_verify(&R, &c, pf, 32, 100, &zo) == JR_OK && zo.verdict == LPRES_STATE_NEITHER,
          "last table-hash byte compared");
    /* eviction keeps the survivors findable */
    setup();
    for (uint32_t i = 0; i < 4; i++) {
        make_claim(P(2 * M6, 2 * M6), (uint8_t) (10 + i), i < 2 ? 50 : 55, &c, pf);
        CHECK(jr_zone_verify(&R, &c, pf, 32, 55, &zo) == JR_OK, "add");
    }
    make_claim(P(2 * M6, 2 * M6), 20, 61, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 61, &zo) == JR_OK && R.nnull == 3, "two evicted");
    make_claim(P(2 * M6, 2 * M6), 12, 55, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 61, &zo) == JR_E_REPLAY, "moved entry still found");
    make_claim(P(2 * M6, 2 * M6), 13, 55, &c, pf);
    CHECK(jr_zone_verify(&R, &c, pf, 32, 61, &zo) == JR_E_REPLAY, "second moved entry found");
}

static void t_atomic_and_full(void)
{
    jr_receipt_t o;
    jr_request_t q;
    jr_zone_ok_t z = ZA();
    setup();
    burn_fail = 1;
    q = req(U1, 2500, 1, false, false);
    uint8_t h0[32];
    memcpy(h0, R.head, 32);
    CHECK(jr_accept_allotment(&R, &q, &z, &o) == JR_E_BURN && R.next_receipt == 1 &&
              jr_free_lots(&R, 1) == 10 && R.nvouchers == 0 && R.burned_total == 0 &&
              memcmp(h0, R.head, 32) == 0 && ndeliver == 0,
          "refused burn changes nothing");
    jr_zone_ok_t zb = zok(LPRES_STATE_BOTH, 1, 0, 0);
    CHECK(jr_accept_allotment(&R, &q, &zb, &o) == JR_E_BURN && R.nescrows == 0, "no escrow either");
    burn_fail = 0;
    /* voucher table full */
    int ok = 0;
    for (uint32_t i = 0; i < JR_MAX_VOUCHERS; i++) {
        uint8_t u[32] = {(uint8_t) i, (uint8_t) (i >> 8), 0x55};
        q = req(u, 3, 4, false, false);
        jr_zone_ok_t zm = zok(LPRES_STATE_TRUE, 10, JR_CAP_METALS, 1);
        q.want_asset = 2;
        q.accumulate = true;
        ok += jr_accept_allotment(&R, &q, &zm, &o) == JR_OK;
    }
    CHECK(ok == (int) JR_MAX_VOUCHERS && R.nvouchers == JR_MAX_VOUCHERS, "vouchers filled");
    {
        uint8_t u[32] = {0xEE};
        jr_zone_ok_t zm = zok(LPRES_STATE_TRUE, 10, JR_CAP_METALS, 1);
        q = req(u, 3, 2, true, false);
        int b = nburn;
        CHECK(jr_accept_allotment(&R, &q, &zm, &o) == JR_E_FULL && nburn == b, "voucher table full");
        q = req(u, 1550, 2, false, false);
        CHECK(jr_accept_allotment(&R, &q, &zm, &o) == JR_OK && o.voucher_num == 0,
              "an exact lot needs no voucher slot");
        jr_zone_ok_t zn = zok(LPRES_STATE_NEITHER, 0, 0, 0);
        q = req(u, 8, 2, false, false);
        CHECK(jr_accept_allotment(&R, &q, &zn, &o) == JR_E_FULL, "full on the N path too");
        uint8_t u0[32] = {0, 0, 0x55};
        q = req(u0, 3, 2, true, false);
        CHECK(jr_accept_allotment(&R, &q, &zm, &o) == JR_OK && o.voucher_num == 6,
              "an existing voucher still grows");
    }
    /* escrow table full, then slot reuse */
    setup();
    jr_zone_ok_t zb2 = zok(LPRES_STATE_BOTH, 1, 0, 0);
    uint64_t first = 0;
    for (uint32_t i = 0; i < JR_MAX_ESCROWS; i++) {
        q = req(U1, 5, 0, false, false);
        CHECK(jr_accept_allotment(&R, &q, &zb2, &o) == JR_OK, "escrow");
        if (i == 0)
            first = o.escrow_id;
    }
    int b = nburn;
    CHECK(jr_accept_allotment(&R, &q, &zb2, &o) == JR_E_FULL && nburn == b, "escrow table full");
    jr_receipt_t o2;
    CHECK(settle(first, JR_RULE_REFUND, 0, 0, &o2) == JR_OK, "settle first");
    CHECK(jr_accept_allotment(&R, &q, &zb2, &o) == JR_OK && jr_escrow_get(&R, first) == NULL &&
              jr_escrow_get(&R, o.escrow_id) != NULL && R.nescrows == JR_MAX_ESCROWS,
          "settled slot reused");
    CHECK(jr_escrow_get(NULL, 1) == NULL, "get null");
}

/* Randomized: every delivery conserves vino*num + r == lots*den*lot + r'. */
static void t_random(void)
{
    setup();
    CHECK(jr_add_registry(&R, 7, reg, NULL) == JR_OK, "registry");
    add_lots(1, 400, 200, 1);
    uint64_t s = 12345;
    int bad = 0, deliveries = 0, escrows = 0, refusals = 0;
    uint64_t sum = 0;
    for (int i = 0; i < 3000; i++) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        uint8_t u[32] = {(uint8_t) (s >> 33) % 8u};
        uint64_t v = 1 + ((s >> 20) % 5000);
        uint32_t want = (uint32_t) ((s >> 40) % 6);
        static const jr_zone_ok_t zs[] = {
            {LPRES_STATE_TRUE, 10, 0x0F, 1}, {LPRES_STATE_TRUE, 20, 0x06, 2},
            {LPRES_STATE_TRUE, 30, 0x04, 1}, {LPRES_STATE_NEITHER, 0, 0, 0},
            {LPRES_STATE_BOTH, 1, 0, 0},     {LPRES_STATE_TRUE, 10, 0x09, 1},
        };
        jr_zone_ok_t z = zs[(s >> 50) % 6];
        reg_ans[JR_CAP_COMMODITY] = (s >> 60) == 0 ? LPRES_STATE_FALSE : LPRES_STATE_TRUE;
        jr_request_t q = req(u, v, want, (s >> 44) & 1, (s >> 45) & 1);
        uint64_t rprev = 0;
        jr_voucher_t *w = NULL;
        jr_receipt_t o;
        jr_status_t st;
        /* read the previous voucher of whatever asset gets chosen: snapshot all */
        uint64_t prev[8] = {0};
        for (uint32_t a = 1; a <= 5; a++)
            for (uint32_t k = 0; k < R.nvouchers; k++)
                if (R.vouchers[k].asset_id == a && memcmp(R.vouchers[k].user, u, 32) == 0)
                    prev[a] = R.vouchers[k].r;
        uint8_t h0[32];
        memcpy(h0, R.head, 32);
        st = jr_accept_allotment(&R, &q, &z, &o);
        (void) w;
        if (st != JR_OK) {
            refusals++;
            if (memcmp(h0, R.head, 32) != 0)
                bad++;
            continue;
        }
        sum += v;
        uint8_t d[32];
        ref_receipt(h0, &o, d);
        if (memcmp(d, o.digest, 32) != 0)
            bad++;
        if (o.outcome == JR_OUT_ESCROW) {
            escrows++;
            jr_receipt_t o2;
            uint8_t kind = (s >> 30) & 1 ? JR_RULE_REFUND : JR_RULE_REROUTE;
            if (settle(o.escrow_id, kind, JR_CAP_MUTUAL, 1, &o2) != JR_OK)
                bad++;
            continue;
        }
        deliveries++;
        const jr_asset_t *a = NULL;
        for (uint32_t k = 0; k < R.nassets; k++)
            if (R.assets[k].id == o.asset_id)
                a = &R.assets[k];
        rprev = prev[o.asset_id];
        uint64_t rnew = 0;
        for (uint32_t k = 0; k < R.nvouchers; k++)
            if (R.vouchers[k].asset_id == o.asset_id && memcmp(R.vouchers[k].user, u, 32) == 0)
                rnew = R.vouchers[k].r;
        u128 lhs = (u128) v * a->num + rprev;
        u128 rhs = (u128) o.lots * a->den * a->lot + rnew;
        if (lhs != rhs || rnew >= a->den * a->lot)
            bad++;
        if (o.voucher_num * a->num != rnew * o.voucher_den)
            bad++;
        {
            uint64_t x = o.voucher_num, y = o.voucher_den;
            while (y) {
                uint64_t t2 = x % y;
                x = y;
                y = t2;
            }
            if (x != 1 && !(o.voucher_num == 0 && o.voucher_den == 1))
                bad++;
        }
        if ((z.caps & a->cap) == 0 && o.outcome != JR_OUT_LOCAL_CREDIT)
            bad++;
    }
    CHECK(bad == 0, "conservation, digests and masks hold over 3000 random allotments");
    printf("random: %d deliveries, %d escrows, %d refusals\n", deliveries, escrows, refusals);
    CHECK(deliveries > 500 && escrows > 100 && refusals > 10, "every path exercised");
    CHECK(R.burned_total == sum && burned == sum, "burn total");
    uint32_t delivered = 0;
    for (uint32_t i = 0; i < R.nlots; i++)
        delivered += R.lots[i].state != 0;
    CHECK(delivered + jr_free_lots(&R, 1) + jr_free_lots(&R, 2) == R.nlots, "lot states");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    t_math();
    t_lot_header();
    t_locate();
    t_attest_verify();
    t_setup_rules();
    t_hierarchy();
    t_four_valued();
    t_rulings();
    t_software_first();
    t_atomic_and_full();
    t_extras();
    t_extras2();
    t_random();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
