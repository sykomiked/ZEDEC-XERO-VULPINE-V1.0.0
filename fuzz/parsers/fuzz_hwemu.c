/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_hwemu.c — hwemu: ZHP-1 profile decoding (hwp.c), signed hardware
 * update images and contract packages (hwr.c, hwr_pq.c) and the two-slot
 * region state machine with its audit log (hwa.c) and the matrix (hwm.c).
 *
 * Byte 0 picks the mode (low 3 bits):
 *   0    profile: the rest is a ZHP-1 blob for hwp_decode
 *   4    profile edits: the reference machine R1's encoding with byte
 *        edits (position, xor), a length change and a node-count change
 *        from the rest, then as mode 0
 *   1    image: the rest is a ZXHR image for hwr_parse / hwr_vectors_next
 *   2,5,6 region: an op stream driving one region linked to a matrix, with
 *        a fast stand-in signature scheme (below) so thousands of images
 *        can be signed per second
 *   3,7  real PQ: one genuinely dual-signed image (ML-DSA-87 +
 *        SLH-DSA-SHAKE-256s, made once at start) is mutated (byte flips,
 *        truncation, extension, half swaps) and staged on a dual region
 *
 * The stand-in scheme: a key is 2656 bytes; the "ML-DSA half" of a
 * signature over msg is SHA-256(pk[0..32) || msg) followed by zeros, the
 * "SLH half" SHA-256(pk[2592..2624) || msg) followed by zeros. A dual
 * signature is valid only if both halves are, by the same key.
 *
 * Invariants:
 *   H1  hwp_decode accepts only canonical blobs: re-encoding gives the same
 *       bytes, the digest is SHA-256 of those bytes, decode(encode(p)) == p;
 *       hwp_match / hwp_present are deterministic and never crash
 *   H2  hwr_parse OK => len == 96 + payload_len + sig_len exactly and every
 *       pointer lies inside the image; hwr_vectors_next stays in bounds and
 *       terminates
 *   H3  a stage that returns OK was signed by a trusted key with the full
 *       suite (for DUAL both halves) and was not modified after signing
 *       (forged, truncated, extended, flipped images are refused)
 *   H4  a refusal before the signature check (FORMAT, TARGET, KIND,
 *       FUNCTION, BLOCKED, STATE, DEVICE) changes no region state and
 *       writes no record; VERIFY / ROLLBACK add exactly one forbidden
 *       REJECTED record and change nothing else (nothing is loaded)
 *   H5  R3: committed versions strictly increase over the region's life and
 *       the floor never falls; nothing below the floor is ever staged
 *   H6  R6: a payload that was quarantined (read-back, activation,
 *       conformance) never stages again
 *   H7  state consistency: ACTIVE has a valid active slot served by the
 *       device, PROBATION serves the staged slot, EMPTY has no active slot
 *       and a linked provider is not attested TRUE there
 *   H8  R7: the audit chain always verifies, never shrinks, and every state
 *       change is accompanied by a record (or the region is HALTED)
 *   H9  a contract package verifies iff authentic and of kind VECTORS
 *   H10 real PQ: the mutated image stages iff it is byte-identical to the
 *       signed one */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_in.h"
#include "hwp.h"
#include "hwr.h"
#include "hwr_pq.h"
#include "hwa.h"
#include "hwm.h"
#include "sha256.h"
#include "pq_matrix_algs.h"

/* ======================= H1: profiles ======================= */
static hwp_profile_t g_p, g_p2, g_pres;
static uint8_t g_enc[HWP_HEADER + HWP_MAX_NODES * (16u + HWP_MAX_CAPS * 10u + HWP_MAX_STATE + HWP_MAX_EXT)];

static void mode_profile(const uint8_t *p, size_t n)
{
    if (n > sizeof g_enc) return;
    uint8_t *b = fz_dup(p, n);
    int rc = hwp_decode(b, (uint32_t) n, &g_p);
    if (rc == HWP_OK) {
        uint32_t el = 0;
        if (hwp_encoded_len(&g_p) != n) abort();
        if (hwp_encode(&g_p, g_enc, sizeof g_enc, &el) != HWP_OK || el != n) abort();
        if (memcmp(g_enc, b, n)) abort(); /* one machine, one encoding */
        uint8_t d1[32], d2[32];
        hwp_digest(&g_p, d1);
        sha256(b, n, d2);
        if (memcmp(d1, d2, 32)) abort();
        if (hwp_decode(g_enc, el, &g_p2) != HWP_OK) abort();
        if (hwp_encode(&g_p2, g_enc, sizeof g_enc, &el) != HWP_OK || memcmp(g_enc, b, n)) abort();
        /* matching both ways, twice: deterministic */
        static hwp_match_t m1, m2;
        const hwp_profile_t *r1 = hwp_reference_r1();
        int a = hwp_match(&g_p, r1, &m1), c = hwp_match(&g_p, r1, &m2);
        if (a != c || (a == HWP_OK && memcmp(&m1, &m2, sizeof m1))) abort();
        if (a == HWP_OK && m1.native + m1.emulated > m1.n) abort();
        (void) hwp_match(r1, &g_p, &m1);
        (void) hwp_match(&g_p, &g_p, &m1);
        if (hwp_present(&g_p, r1, &g_pres) == HWP_OK) {
            if (hwp_encode(&g_pres, g_enc, sizeof g_enc, &el) != HWP_OK) abort();
            if (hwp_decode(g_enc, el, &g_p2) != HWP_OK) abort(); /* stays canonical */
        }
        for (uint16_t i = 0; i < g_p.n; i++) (void) hwp_class_name(g_p.node[i].cls);
    }
    free(b);
}

static uint8_t g_mut[sizeof g_enc];
static void mode_profile_mut(fz_in *in)
{
    uint32_t n = 0;
    if (hwp_encode(hwp_reference_r1(), g_mut, sizeof g_mut, &n) != HWP_OK) abort();
    uint8_t k = fz_u8(in) % 9u;
    for (uint8_t i = 0; i < k; i++) {
        uint16_t pos = fz_u16(in);
        g_mut[pos % n] ^= fz_u8(in);
    }
    uint8_t t = fz_u8(in);
    if (t & 1) n -= (uint32_t) (fz_u8(in) % 32u) % n;           /* truncate */
    if (t & 2) { uint32_t add = fz_u8(in) % 32u; fz_bytes(in, g_mut + n, add); n += add; }
    if (t & 4) { g_mut[12] = (uint8_t) n; g_mut[13] = (uint8_t) (n >> 8); } /* fix up length */
    mode_profile(g_mut, n);
}

/* ======================= H2: images ======================= */
static void mode_image(const uint8_t *p, size_t n)
{
    uint8_t *b = fz_dup(p, n);
    hwr_image_t im;
    if (hwr_parse(b, (uint32_t) n, &im) == HWR_OK) {
        if ((uint64_t) n != (uint64_t) HWR_HEADER + im.payload_len + im.sig_len) abort();
        if (im.header != b || im.payload != b + HWR_HEADER ||
            im.sig != b + HWR_HEADER + im.payload_len || im.sig_len != hwr_sig_len(im.alg))
            abort();
        uint8_t h[HWR_HEADER];
        hwr_build_header(&im, h);
        if (memcmp(h, b, HWR_HEADER)) abort(); /* the header round-trips */
        uint32_t off = 0, op, il, ol, cnt = 0;
        const uint8_t *vi, *vo;
        int rc;
        while ((rc = hwr_vectors_next(im.payload, im.payload_len, &off, &op, &vi, &il, &vo, &ol)) ==
               HWR_OK) {
            if (vi < im.payload || vi + il > im.payload + im.payload_len) abort();
            if (vo < im.payload || vo + ol > im.payload + im.payload_len || ol > 512u) abort();
            if (++cnt > im.payload_len) abort(); /* must terminate */
        }
        if (rc != 1 && rc != HWR_E_FORMAT) abort();
    }
    /* the vector walker on the raw bytes too */
    uint32_t off = 0, op, il, ol, cnt = 0;
    const uint8_t *vi, *vo;
    while (hwr_vectors_next(b, (uint32_t) n, &off, &op, &vi, &il, &vo, &ol) == HWR_OK) {
        if (vi + il > b + n || vo + ol > b + n) abort();
        if (++cnt > n) abort();
    }
    free(b);
}

/* ======================= the stand-in scheme ======================= */
#define KEYS 4 /* 0, 1 trusted; 2 untrusted; 3 trusted only for MLDSA87 test */
static uint8_t g_key[KEYS][HWR_DUAL_PK];

static void half(const uint8_t *pk32, const uint8_t *msg, uint32_t mlen, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, pk32, 32);
    sha256_update(&c, msg, mlen);
    sha256_final(&c, out);
}

static bool half_ok(const uint8_t *pk32, const uint8_t *msg, uint32_t mlen, const uint8_t *sig,
                    uint32_t slen)
{
    uint8_t h[32];
    half(pk32, msg, mlen, h);
    if (memcmp(h, sig, 32)) return false;
    for (uint32_t i = 32; i < slen; i++)
        if (sig[i]) return false;
    return true;
}

static unsigned g_verifies;
static bool mock_verify(void *ctx, uint16_t alg, const uint8_t *pk, uint32_t pk_len,
                        const uint8_t *msg, uint32_t msg_len, const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    g_verifies++;
    if (pk_len != hwr_pk_len(alg) || sig_len != hwr_sig_len(alg) || msg_len != HWR_HEADER)
        abort(); /* the region must hand over exactly the suite's sizes */
    if (alg == HWR_ALG_MLDSA87) return half_ok(pk, msg, msg_len, sig, sig_len);
    if (alg == HWR_ALG_DUAL) {
        bool a = half_ok(pk, msg, msg_len, sig, HWR_MLDSA87_SIG);
        bool b = half_ok(pk + HWR_MLDSA87_PK, msg, msg_len, sig + HWR_MLDSA87_SIG, HWR_SLH256S_SIG);
        return a && b;
    }
    return false;
}

/* ======================= the simulated region ======================= */
typedef struct {
    uint8_t slot[2][256];
    uint32_t len[2];
    int active;
    int corrupt, refuse, refuse_load;
    uint32_t loads;
} sim_t;
static sim_t g_sim;

static int sim_load(void *d, uint32_t s, const uint8_t *p, uint32_t n)
{
    sim_t *x = d;
    if (s > 1) abort();
    if (n > 256 || x->refuse_load) return -1;
    memcpy(x->slot[s], p, n);
    x->len[s] = n;
    if (x->corrupt) x->slot[s][0] ^= 1;
    x->loads++;
    return 0;
}
static int sim_readback(void *d, uint32_t s, uint8_t dg[32])
{
    sim_t *x = d;
    if (s > 1) abort();
    sha256(x->slot[s], x->len[s], dg);
    return 0;
}
static int sim_activate(void *d, uint32_t s)
{
    sim_t *x = d;
    if (s > 1) abort();
    if (x->refuse) return -1;
    x->active = (int) s;
    return 0;
}
static const hwr_ops_t SIM_OPS = {sim_load, sim_readback, sim_activate};

static int sim_call(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                    uint32_t cap, uint32_t *out_len)
{
    (void) m;
    (void) op;
    sim_t *x = inst;
    if (x->active < 0 || n > cap) return -1;
    for (uint32_t i = 0; i < n; i++)
        out[i] = in[i] < x->len[x->active] ? x->slot[x->active][in[i]] : 0;
    *out_len = n;
    return 0;
}
static int soft_inc(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out,
                    uint32_t cap, uint32_t *out_len)
{
    (void) inst;
    (void) m;
    (void) op;
    if (n > cap) return -1;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (in[i] + 1);
    *out_len = n;
    return 0;
}

static uint8_t T_INC[256], T_XOR[256], T_VIN[256];
#define TCLASS 0x00D0F00Du

static hwm_t g_m;
static uint32_t g_finc, g_fxor, g_preg;
static hwa_rec_t g_rec[96];
static hwa_log_t g_log;
static hwr_region_t g_r;
static uint8_t g_tdig[32];

/* ---- the model ---- */
static uint8_t g_blocked[64][32];
static uint32_t g_nblocked;
static uint32_t g_max_committed;
static bool g_any_committed;
static uint32_t g_floor_seen;

static bool model_blocked(const uint8_t d[32])
{
    for (uint32_t i = 0; i < g_nblocked; i++)
        if (!memcmp(g_blocked[i], d, 32)) return true;
    return false;
}

/* the parts of the region that mean something (an inactive, unused slot's
 * flags are scratch) */
typedef struct {
    hwr_state_t state;
    uint32_t active, staged, floor, nbad, probation_left;
    hwr_slot_t act, stg;
    uint8_t bad[HWR_MAX_BAD][32];
    uint8_t staged_header[HWR_HEADER];
    uint32_t n_staged, n_committed, n_rolled_back, n_quarantined;
    uint32_t log_n;
    uint32_t loads;
} view_t;

static void view(view_t *v)
{
    memset(v, 0, sizeof *v);
    v->state = g_r.state;
    v->active = g_r.active;
    v->staged = g_r.staged;
    v->floor = g_r.floor;
    v->nbad = g_r.nbad;
    v->probation_left = g_r.probation_left;
    if (g_r.active != HWR_NO_SLOT) v->act = g_r.slot[g_r.active];
    if (g_r.staged != HWR_NO_SLOT) v->stg = g_r.slot[g_r.staged];
    memcpy(v->bad, g_r.bad, sizeof v->bad);
    if (g_r.staged != HWR_NO_SLOT) memcpy(v->staged_header, g_r.staged_header, HWR_HEADER);
    v->n_staged = g_r.n_staged;
    v->n_committed = g_r.n_committed;
    v->n_rolled_back = g_r.n_rolled_back;
    v->n_quarantined = g_r.n_quarantined;
    v->log_n = g_log.n;
    v->loads = g_sim.loads;
}

static void check_consistent(void)
{
    if (!hwa_verify(&g_log)) abort(); /* H8 */
    if (g_r.floor < g_floor_seen) abort(); /* H5 */
    g_floor_seen = g_r.floor;
    if (g_r.active != HWR_NO_SLOT && g_r.active > 1) abort();
    if (g_r.staged != HWR_NO_SLOT && g_r.staged > 1) abort();
    switch (g_r.state) {
    case HWR_ST_ACTIVE:
        if (g_r.active == HWR_NO_SLOT || !g_r.slot[g_r.active].valid) abort();
        if (g_r.staged != HWR_NO_SLOT) abort();
        if (g_sim.active != (int) g_r.active) abort(); /* the device serves it */
        break;
    case HWR_ST_STAGED:
        if (g_r.staged == HWR_NO_SLOT || !g_r.slot[g_r.staged].valid || g_r.staged == g_r.active)
            abort();
        break;
    case HWR_ST_PROBATION:
        if (g_r.staged == HWR_NO_SLOT || !g_r.slot[g_r.staged].valid) abort();
        if (g_sim.active != (int) g_r.staged) abort();
        if (g_r.probation_left == 0) abort();
        break;
    case HWR_ST_EMPTY:
        if (g_r.active != HWR_NO_SLOT || g_r.staged != HWR_NO_SLOT) abort();
        if (g_m.pv[g_preg].attest == LPRES_STATE_TRUE && g_r.n_staged) abort(); /* H7: S0 */
        break;
    case HWR_ST_HALTED:
        break;
    default:
        abort();
    }
}

static void region_fresh(uint16_t alg)
{
    memset(&g_sim, 0, sizeof g_sim);
    g_sim.active = -1;
    hwm_init(&g_m);
    uint32_t rcpu, rfab, psoft;
    if (hwm_func_add(&g_m, 0x200, "inc", &g_finc) || hwm_func_add(&g_m, 0x201, "xor5", &g_fxor) ||
        hwm_resource_add(&g_m, 1, &rcpu) || hwm_resource_add(&g_m, 2, &rfab) ||
        hwm_vector_add(&g_m, g_finc, 0, T_VIN, 256, T_INC, 256) ||
        hwm_vector_add(&g_m, g_fxor, 0, T_VIN, 256, T_XOR, 256) ||
        hwm_provider_add(&g_m, g_finc, rcpu, HWM_KIND_SOFT, 50, soft_inc, 0, &psoft) ||
        hwm_provider_add(&g_m, g_finc, rfab, HWM_KIND_FABRIC, 1, sim_call, &g_sim, &g_preg))
        abort();
    (void) hwm_attest(&g_m, psoft, 0);
    hwa_init(&g_log, g_rec, sizeof g_rec / sizeof g_rec[0]);
    if (hwr_region_init(&g_r, 9, TCLASS, g_tdig, alg, mock_verify, 0, &SIM_OPS, &g_sim, &g_log) !=
            HWR_OK ||
        hwr_trust_key(&g_r, g_key[0]) || hwr_trust_key(&g_r, g_key[1]) ||
        hwr_link(&g_r, &g_m, rfab, g_preg))
        abort();
    g_nblocked = 0;
    g_max_committed = 0;
    g_any_committed = false;
    g_floor_seen = 0;
}

/* ---- building images ---- */
static uint8_t g_img[HWR_HEADER + 256 + HWR_DUAL_SIG + 8];

/* sign mode: 0 key0 full, 1 key1 full, 2 untrusted key2, 3 ML half only,
 * 4 SLH half only, 5 ML half key0 + SLH half key1 (mixed), 6 garbage,
 * 7 unsigned (zeros) */
static uint32_t build(fz_in *in, uint16_t alg, bool *authentic, uint8_t pdig[32],
                      uint32_t *ver_out, uint32_t *svn_out, uint32_t *kind_out)
{
    hwr_image_t im;
    memset(&im, 0, sizeof im);
    uint8_t f = fz_u8(in);
    im.alg = (f & 0x80) ? (alg == HWR_ALG_DUAL ? HWR_ALG_MLDSA87 : HWR_ALG_DUAL) : alg;
    static const uint32_t kinds[] = {HWR_KIND_PARTIAL, HWR_KIND_BITSTREAM, HWR_KIND_MICROCODE,
                                     HWR_KIND_FIRMWARE, HWR_KIND_VECTORS, 0, 6, HWR_KIND_PARTIAL};
    im.kind = kinds[(f >> 4) & 7u];
    im.target_class = (f & 8) ? TCLASS ^ 1u : TCLASS;
    uint8_t g = fz_u8(in);
    if ((g & 3) == 1) memcpy(im.target_digest, g_tdig, 32);
    if ((g & 3) == 2) memset(im.target_digest, 0xCD, 32);
    im.fn_id = (g & 4) ? 0x201 : ((g & 8) ? 0x999 : 0x200);
    /* versions near the region's current one, or edges */
    uint32_t cur = g_r.active != HWR_NO_SLOT ? g_r.slot[g_r.active].version : 0;
    uint8_t vs = fz_u8(in);
    switch (vs & 7) {
    case 0: im.version = cur + 1; break;
    case 1: im.version = cur; break;
    case 2: im.version = cur ? cur - 1 : 0; break;
    case 3: im.version = (uint32_t) fz_edge64(in); break;
    default: im.version = cur + 1 + (vs >> 3); break;
    }
    uint8_t ss = fz_u8(in);
    switch (ss & 3) {
    case 0: im.svn = g_r.floor; break;
    case 1: im.svn = g_r.floor + 1; break;
    case 2: im.svn = g_r.floor ? g_r.floor - 1 : 0; break;
    default: im.svn = (uint32_t) fz_edge64(in); break;
    }
    /* payload: inc, xor5, inc with one wrong gate, random, or short */
    uint8_t pl[256];
    uint32_t plen = 256;
    uint8_t ps = fz_u8(in);
    switch (ps & 7) {
    case 0: case 1: memcpy(pl, T_INC, 256); break;
    case 2: memcpy(pl, T_XOR, 256); break;
    case 3: { /* one wrong gate (two reads, in a fixed order) */
        uint8_t at = fz_u8(in), x = (uint8_t) (1u + fz_u8(in) % 255u);
        memcpy(pl, T_INC, 256);
        pl[at] ^= x;
        break;
    }
    case 4: fz_bytes(in, pl, 256); break;
    case 5: plen = 1u + fz_u8(in); fz_bytes(in, pl, plen); break;
    default: memcpy(pl, T_INC, 256); pl[ps >> 3] ^= 0x40; break;
    }
    im.payload_len = plen;
    sha256(pl, plen, im.payload_sha256);
    sha256(pl, plen, pdig);
    hwr_build_header(&im, g_img);
    memcpy(g_img + HWR_HEADER, pl, plen);
    uint32_t sl = hwr_sig_len(im.alg);
    uint8_t *sig = g_img + HWR_HEADER + plen;
    if (sl) memset(sig, 0, sl);
    uint8_t sm = fz_u8(in);
    uint32_t mode = sm & 7u;
    bool auth = false;
    if (sl) {
        uint32_t mlh = im.alg == HWR_ALG_DUAL ? HWR_MLDSA87_SIG : sl;
        const uint8_t *k_ml = g_key[mode == 1 ? 1 : (mode == 2 ? 2 : 0)];
        const uint8_t *k_slh = g_key[mode == 1 ? 1 : (mode == 2 ? 2 : (mode == 5 ? 1 : 0))];
        if (mode <= 3 || mode == 5) half(k_ml, g_img, HWR_HEADER, sig);
        if (im.alg == HWR_ALG_DUAL && (mode <= 2 || mode == 4 || mode == 5))
            half(k_slh + HWR_MLDSA87_PK, g_img, HWR_HEADER, sig + mlh);
        if (mode == 6) fz_bytes(in, sig, 64);
        /* ML-DSA-only suite: the ML half by a trusted key is the whole
         * signature (modes 0, 1, 3, 5); dual: both halves by one key */
        auth = mode <= 1 || (im.alg == HWR_ALG_MLDSA87 && (mode == 3 || mode == 5));
    }
    uint32_t len = HWR_HEADER + plen + sl;
    /* post-signing damage */
    uint8_t dm = fz_u8(in);
    switch (dm & 7) {
    case 1: len -= 1u + (fz_u8(in) % 8u); auth = false; break;               /* truncated */
    case 2: g_img[len] = 0; len++; auth = false; break;                       /* extended */
    case 3: g_img[fz_u8(in) % HWR_HEADER] ^= (uint8_t) (1u << (dm >> 5)); auth = false; break;
    case 4: g_img[HWR_HEADER + fz_u8(in) % plen] ^= 1; auth = false; break;   /* payload */
    case 5:
        if (sl) {
            g_img[HWR_HEADER + plen + fz_u16(in) % sl] ^= (uint8_t) (1u << (dm >> 5));
            auth = false;
        }
        break;
    default: break;
    }
    *authentic = auth && im.alg == alg;
    *ver_out = im.version;
    *svn_out = im.svn;
    *kind_out = im.kind;
    return len;
}

static void expect_unchanged(const view_t *a)
{
    view_t b;
    view(&b);
    if (memcmp(a, &b, sizeof b)) abort();
}

static void op_stage(fz_in *in)
{
    bool authentic;
    uint8_t pd[32];
    uint32_t ver, svn, kind;
    uint32_t len = build(in, g_r.alg, &authentic, pd, &ver, &svn, &kind);
    view_t before;
    view(&before);
    uint32_t cur = g_r.active != HWR_NO_SLOT ? g_r.slot[g_r.active].version : 0;
    bool had_active = g_r.active != HWR_NO_SLOT;
    uint8_t *img = fz_dup(g_img, len);
    int rc = hwr_stage(&g_r, img, len);
    free(img);
    switch (rc) {
    case HWR_OK:
        if (!authentic) abort();                       /* H3: forged accepted */
        if (kind == HWR_KIND_VECTORS) abort();
        if (svn < before.floor) abort();                /* H5 */
        if (had_active && ver <= cur) abort();          /* R3 */
        if (model_blocked(pd)) abort();                 /* H6 */
        if (g_log.n != before.log_n + 1) abort();       /* H8 */
        if (g_r.state != HWR_ST_STAGED) abort();
        break;
    case HWR_E_VERIFY:
        if (authentic) abort(); /* a genuine image must verify */
        /* fall through */
    case HWR_E_ROLLBACK:
        if (rc == HWR_E_ROLLBACK && !authentic) abort(); /* got past the signature */
        if (g_r.state == HWR_ST_HALTED) abort(); /* would have been HWR_E_HALTED */
        if (g_log.n != before.log_n + 1 || !lpx_forbidden(g_log.rec[g_log.n - 1].tag) ||
            g_log.rec[g_log.n - 1].event != HWA_EV_REJECTED)
            abort(); /* H4 */
        if (g_sim.loads != before.loads) abort();
        before.log_n = g_log.n;
        before.n_quarantined++;
        expect_unchanged(&before);
        break;
    case HWR_E_READBACK:
        if (!authentic) abort(); /* only reached after the signature */
        if (g_nblocked < 64) memcpy(g_blocked[g_nblocked++], pd, 32);
        break;
    case HWR_E_HALTED:
        if (g_r.state != HWR_ST_HALTED) abort();
        break;
    case HWR_E_DEVICE:
        if (!authentic) abort();
        before.loads = g_sim.loads;
        expect_unchanged(&before);
        break;
    default: /* FORMAT, TARGET, KIND, FUNCTION, BLOCKED, STATE */
        if (rc == HWR_E_BLOCKED && !model_blocked(pd)) abort();
        expect_unchanged(&before); /* H4 */
        break;
    }
}

static void after_trial_like(int rc, const view_t *before)
{
    if (rc == HWR_E_CONFORMANCE || rc == HWR_E_ACTIVATE) {
        /* rolled back and (unless expiry) blocked */
        if (g_log.n < before->log_n + 2 && g_r.state != HWR_ST_HALTED) abort();
    }
}

static void op_trial(fz_in *in)
{
    view_t before;
    view(&before);
    uint8_t pd[32];
    bool staged = g_r.state == HWR_ST_STAGED;
    if (staged) memcpy(pd, g_r.slot[g_r.staged].digest, 32);
    uint32_t ticks = fz_u8(in) % 4u;
    int rc = hwr_trial(&g_r, ticks);
    if (rc == HWR_OK) {
        if (!staged || ticks == 0) abort();
        if (g_r.state != HWR_ST_PROBATION || g_log.n != before.log_n + 1) abort();
        /* the region now serves; the matrix proved it */
        if (g_m.pv[g_preg].attest != LPRES_STATE_TRUE) abort();
    } else if (rc == HWR_E_CONFORMANCE || rc == HWR_E_ACTIVATE) {
        if (g_nblocked < 64) memcpy(g_blocked[g_nblocked++], pd, 32);
        after_trial_like(rc, &before);
    } else if (rc != HWR_E_HALTED) {
        expect_unchanged(&before);
    }
}

static void op_commit(void)
{
    view_t before;
    view(&before);
    uint32_t ver = g_r.staged != HWR_NO_SLOT ? g_r.slot[g_r.staged].version : 0;
    int rc = hwr_commit(&g_r);
    if (rc == HWR_OK) {
        if (before.state != HWR_ST_PROBATION) abort();
        if (g_any_committed && ver <= g_max_committed) abort(); /* H5: forward only */
        g_max_committed = ver;
        g_any_committed = true;
        if (g_log.n != before.log_n + 1) abort();
    } else if (rc != HWR_E_HALTED) {
        expect_unchanged(&before);
    }
}

static void op_rollback(void)
{
    view_t before;
    view(&before);
    int rc = hwr_rollback(&g_r);
    if (rc == HWR_OK) {
        if (g_log.n != before.log_n + 1) abort();
        if (g_r.state != HWR_ST_ACTIVE && g_r.state != HWR_ST_EMPTY) abort();
    } else if (rc != HWR_E_HALTED) {
        expect_unchanged(&before);
    }
}

static void op_tick(void)
{
    view_t before;
    view(&before);
    int rc = hwr_tick(&g_r);
    if (rc == HWR_OK && before.state != HWR_ST_PROBATION) expect_unchanged(&before);
}

static void op_package(fz_in *in)
{
    bool authentic;
    uint8_t pd[32];
    uint32_t ver, svn, kind;
    uint16_t alg = g_r.alg;
    uint32_t len = build(in, alg, &authentic, pd, &ver, &svn, &kind);
    const uint8_t *keys[3] = {g_key[0], g_key[1], NULL};
    hwr_image_t im;
    uint8_t *img = fz_dup(g_img, len);
    int rc = hwr_package_verify(img, len, alg, mock_verify, 0, keys, 3, &im);
    if (rc == HWR_OK) {
        if (!authentic || kind != HWR_KIND_VECTORS) abort(); /* H9 */
        if (im.payload != img + HWR_HEADER) abort();
    } else if (authentic && kind == HWR_KIND_VECTORS) {
        abort();
    }
    free(img);
}

static void mode_region(fz_in *in, uint8_t sel)
{
    region_fresh((sel & 8) ? HWR_ALG_MLDSA87 : HWR_ALG_DUAL);
    check_consistent();
    for (int k = 0; k < 64 && in->n; k++) {
        uint8_t op = fz_u8(in);
        switch (op % 10u) {
        case 0: case 1: case 2: op_stage(in); break;
        case 3: op_trial(in); break;
        case 4: op_commit(); break;
        case 5: op_rollback(); break;
        case 6: op_tick(); break;
        case 7: op_package(in); break;
        case 8: /* device faults */
            g_sim.corrupt = (op >> 4) & 1;
            g_sim.refuse = (op >> 5) & 1;
            g_sim.refuse_load = (op >> 6) & 1;
            break;
        default: /* stage, trial, commit in one go */
            op_stage(in);
            op_trial(in);
            op_commit();
            break;
        }
        check_consistent();
    }
}

/* ======================= H10: real PQ ======================= */
static uint8_t g_dpk[HWR_DUAL_PK], g_dimg[HWR_HEADER + 64 + HWR_DUAL_SIG];
static uint32_t g_dn;

static void real_init(void)
{
    static uint8_t sk_m[4896], sk_s[128];
    uint8_t seed[32], sseed[96];
    memset(seed, 3, sizeof seed);
    memset(sseed, 7, sizeof sseed);
    pqm_mldsa87_keygen(seed, g_dpk, sk_m);
    pqm_slh256s_keygen(sseed, g_dpk + HWR_MLDSA87_PK, sk_s);
    hwr_image_t im;
    memset(&im, 0, sizeof im);
    im.alg = HWR_ALG_DUAL;
    im.kind = HWR_KIND_PARTIAL;
    im.target_class = TCLASS;
    im.fn_id = 0x200;
    im.version = 1;
    im.svn = 1;
    im.payload_len = 64;
    uint8_t pl[64];
    memcpy(pl, T_INC, 64);
    sha256(pl, 64, im.payload_sha256);
    hwr_build_header(&im, g_dimg);
    memcpy(g_dimg + HWR_HEADER, pl, 64);
    hwr_dual_sign(sk_m, sk_s, g_dimg, NULL, g_dimg + HWR_HEADER + 64);
    g_dn = (uint32_t) sizeof g_dimg;
}

static void mode_real(fz_in *in)
{
    if (!g_dn) real_init(); /* SLH-DSA-256s signing takes seconds: only when needed */
    static uint8_t buf[sizeof g_dimg + 16];
    memcpy(buf, g_dimg, g_dn);
    uint32_t len = g_dn;
    uint8_t nm = fz_u8(in) % 4u;
    for (uint8_t k = 0; k < nm; k++) {
        uint8_t t = fz_u8(in);
        uint32_t pos = fz_u16(in);
        switch (t & 3) {
        case 0: /* flip anywhere, biased toward the two signature halves */
            if (t & 4) pos = HWR_HEADER + 64 + (pos % HWR_MLDSA87_SIG);
            else if (t & 8) pos = HWR_HEADER + 64 + HWR_MLDSA87_SIG + (uint32_t) (fz_u16(in) % HWR_SLH256S_SIG);
            buf[pos % len] ^= (uint8_t) (1u + fz_u8(in) % 255u);
            break;
        case 1: len = len > 1 ? len - 1u - (pos % 16u) % len : len; break;
        case 2: if (len < sizeof buf) buf[len++] = fz_u8(in); break;
        default: /* zero the ML-DSA or the SLH half */
            if (len == g_dn) {
                if (t & 4) memset(buf + HWR_HEADER + 64, 0, HWR_MLDSA87_SIG);
                else memset(buf + HWR_HEADER + 64 + HWR_MLDSA87_SIG, 0, HWR_SLH256S_SIG);
            }
            break;
        }
    }
    bool same = len == g_dn && !memcmp(buf, g_dimg, g_dn);
    static hwa_rec_t rec[4];
    hwa_log_t log;
    hwa_init(&log, rec, 4);
    memset(&g_sim, 0, sizeof g_sim);
    g_sim.active = -1;
    static hwr_region_t r;
    if (hwr_region_init(&r, 5, TCLASS, g_tdig, HWR_ALG_DUAL, hwr_pq_verify, 0, &SIM_OPS, &g_sim,
                        &log) != HWR_OK ||
        hwr_trust_key(&r, g_dpk))
        abort();
    uint8_t *img = fz_dup(buf, len);
    int rc = hwr_stage(&r, img, len);
    free(img);
    if (same != (rc == HWR_OK)) abort(); /* H10 */
    if (!hwa_verify(&log)) abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static bool init;
    if (!init) {
        init = true;
        for (int i = 0; i < 256; i++) {
            T_VIN[i] = (uint8_t) i;
            T_INC[i] = (uint8_t) (i + 1);
            T_XOR[i] = (uint8_t) (i ^ 5);
        }
        memset(g_tdig, 0xAB, 32);
        for (int k = 0; k < KEYS; k++)
            for (uint32_t i = 0; i < HWR_DUAL_PK; i++) g_key[k][i] = (uint8_t) (k * 77 + i * 13 + 1);
    }
    if (size < 1 || size > 65536) return 0;
    fz_in in;
    fz_init(&in, data + 1, size - 1);
    switch (data[0] & 7u) {
    case 0: mode_profile(data + 1, size - 1); break;
    case 4: mode_profile_mut(&in); break;
    case 1: mode_image(data + 1, size - 1); break;
    case 3: case 7: mode_real(&in); break;
    default: mode_region(&in, data[0]); break;
    }
    return 0;
}
