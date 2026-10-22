/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vrf.c — ECVRF-EDWARDS25519-SHA512-TAI (vrf.h V1-V13): the three RFC
 * 9381 B.3 vectors byte for byte (pk, H, k, U, V, pi, beta), tampering with
 * pi / pk / alpha, non-canonical s and point encodings, small-order public
 * keys with proofs forged to pass the equation, the dispute and allotment
 * adapters, and random round trips. Links robin_debanks/ed25519_verify.c too,
 * to prove the vrf_ namespace does not clash with it. */
#include <stdio.h>
#include <string.h>
#include "vrf.h"
#include "../robin_debanks/ed25519_verify.h"

/* the vendored arithmetic, under vrf.c's names, to forge proofs */
#define ge_scalarmult_base           vrf_ge_scalarmult_base
#define ge_double_scalarmult_vartime vrf_ge_double_scalarmult_vartime
#define ge_frombytes_negate_vartime  vrf_ge_frombytes_negate_vartime
#define ge_p3_tobytes                vrf_ge_p3_tobytes
#define ge_tobytes                   vrf_ge_tobytes
#define sha512                       vrf_sha512
#define sha512_init                  vrf_sha512_init
#define sha512_update                vrf_sha512_update
#define sha512_final                 vrf_sha512_final
#include "../../third_party/ed25519/src/ge.h"
#include "../../third_party/ed25519/src/sha512.h"

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

static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2) {
        unsigned v;
        sscanf(h, "%2x", &v);
        out[n++] = (uint8_t) v;
    }
    return n;
}

static bool zero(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (p[i]) return false;
    return true;
}

typedef struct {
    const char *sk, *pk, *alpha, *h, *k, *u, *v, *pi, *beta;
} vec_t;

/* RFC 9381 B.3, Examples 16-18 */
static const vec_t V[3] = {
    {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
     "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "91bbed02a99461df1ad4c6564a5f5d829d0b90cfc7903e7a5797bd658abf3318",
     "8a49edbd1492a8ee09766befe50a7d563051bf3406cbffc20a88def030730f0f",
     "aef27c725be964c6a9bf4c45ca8e35df258c1878b838f37d9975523f09034071",
     "5016572f71466c646c119443455d6cb9b952f07d060ec8286d678615d55f954f",
     "8657106690b5526245a92b003bb079ccd1a92130477671f6fc01ad16f26f723f26f8a57ccaed74ee1b190bed1f479"
     "d"
     "9727d2d0f9b005a6e456a35d4fb0daab1268a1b0db10836d9826a528ca76567805",
     "90cf1df3b703cce59e2a35b925d411164068269d7b2d29f3301c03dd757876ff66b71dda49d2de59d03450451af02"
     "6"
     "798e8f81cd2e333de5cdf4f3e140fdd8ae"},
    {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "5b659fc3d4e9263fd9a4ed1d022d75eaacc20df5e09f9ea937502396598dc551",
     "d8c3a66921444cb3427d5d989f9b315aa8ca3375e9ec4d52207711a1fdb44107",
     "1dcb0a4821a2c48bf53548228b7f170962988f6d12f5439f31987ef41f034ab3",
     "fd03c0bf498c752161bae4719105a074630a2aa5f200ff7b3995f7bfb1513423",
     "f3141cd382dc42909d19ec5110469e4feae18300e94f304590abdced48aed5933bf0864a62558b3ed7f2fea45c92a"
     "4"
     "65301b3bbf5e3e54ddf2d935be3b67926da3ef39226bbc355bdc9850112c8f4b02",
     "eb4440665d3891d668e7e0fcaf587f1b4bd7fbfe99d0eb2211ccec90496310eb5e33821bc613efb94db5e5b54c70a"
     "8"
     "48a0bef4553a41befc57663b56373a5031"},
    {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
     "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
     "bf4339376f5542811de615e3313d2b36f6f53c0acfebb482159711201192576a",
     "5ffdbc72135d936014e8ab708585fda379405542b07e3bd2c0bd48437fbac60a",
     "2bae73e15a64042fcebf062abe7e432b2eca6744f3e8265bc38e009cd577ecd5",
     "88cba1cb0d4f9b649d9a86026b69de076724a93a65c349c988954f0961c5d506",
     "9bc0f79119cc5604bf02d23b4caede71393cedfbb191434dd016d30177ccbf8096bb474e53895c362d8628ee9f9ea"
     "3"
     "c0e52c7a5c691b6c18c9979866568add7a2d41b00b05081ed0f58ee5e31b3a970e",
     "645427e5d00c62a23fb703732fa5d892940935942101e456ecca7bb217c61c452118fec1219202a0edcf038bb6373"
     "2"
     "41578be7217ba85a2687f7a0310b2df19f"},
};

static const uint8_t QL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                               0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                               0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

/* a = a + b over 32 bytes LE, returns carry */
static unsigned add32(uint8_t a[32], const uint8_t b[32])
{
    unsigned c = 0;
    for (int i = 0; i < 32; i++) {
        c += (unsigned) a[i] + b[i];
        a[i] = (uint8_t) c;
        c >>= 8;
    }
    return c;
}

static void test_vectors(void)
{
    for (int n = 0; n < 3; n++) {
        uint8_t seed[32], pk[32], epk[32], sk[64], alpha[4], h[32], k[32], u[32], v[32], pi[80],
            epi[80], beta[64], ebeta[64], b2[64], eh[32];
        vrf_trace_t tr;
        unhex(V[n].sk, seed);
        unhex(V[n].pk, epk);
        size_t al = unhex(V[n].alpha, alpha);
        unhex(V[n].h, h);
        unhex(V[n].k, k);
        unhex(V[n].u, u);
        unhex(V[n].v, v);
        unhex(V[n].pi, epi);
        unhex(V[n].beta, ebeta);
        vrf_keypair(seed, pk, sk);
        CHECK(memcmp(pk, epk, 32) == 0, "B.3 pk");
        CHECK(memcmp(sk, seed, 32) == 0 && memcmp(sk + 32, epk, 32) == 0, "sk = seed || pk");
        CHECK(vrf_encode_to_curve(pk, alpha, al, eh) && memcmp(eh, h, 32) == 0, "B.3 H");
        CHECK(vrf_prove_trace(sk, alpha, al, pi, &tr), "prove (trace)");
        CHECK(memcmp(tr.h, h, 32) == 0, "B.3 H (prover)");
        CHECK(memcmp(tr.k, k, 32) == 0, "B.3 k");
        CHECK(memcmp(tr.u, u, 32) == 0, "B.3 U = k*B");
        CHECK(memcmp(tr.v, v, 32) == 0, "B.3 V = k*H");
        CHECK(memcmp(pi, epi, 80) == 0, "B.3 pi");
        memset(pi, 0, 80);
        CHECK(vrf_prove(sk, alpha, al, pi) && memcmp(pi, epi, 80) == 0, "B.3 pi (vrf_prove)");
        CHECK(vrf_verify(pk, alpha, al, epi, beta) && memcmp(beta, ebeta, 64) == 0,
              "B.3 verify + beta");
        CHECK(vrf_proof_to_hash(epi, b2) && memcmp(b2, ebeta, 64) == 0, "B.3 proof_to_hash");
        /* tamper: every byte of pi, two bits each */
        int bad = 0;
        for (int i = 0; i < 80; i++)
            for (int bit = 0; bit < 8; bit += 7) {
                epi[i] ^= (uint8_t) (1u << bit);
                bad += vrf_verify(pk, alpha, al, epi, beta) || !zero(beta, 64);
                epi[i] ^= (uint8_t) (1u << bit);
            }
        CHECK(bad == 0, "every flipped bit of pi refused, beta zeroed");
        /* tamper: every byte of pk */
        bad = 0;
        for (int i = 0; i < 32; i++) {
            pk[i] ^= 0x01;
            bad += vrf_verify(pk, alpha, al, epi, beta);
            pk[i] ^= 0x81;
            bad += vrf_verify(pk, alpha, al, epi, beta);
            pk[i] ^= 0x80;
        }
        CHECK(bad == 0, "every flipped byte of pk refused");
        /* tamper: alpha */
        uint8_t a2[8] = {0};
        memcpy(a2, alpha, al);
        bad = 0;
        for (size_t i = 0; i < al; i++) {
            a2[i] ^= 1;
            bad += vrf_verify(pk, a2, al, epi, beta);
            a2[i] ^= 1;
        }
        bad += vrf_verify(pk, a2, al + 1, epi, beta); /* appended 0x00 */
        if (al) bad += vrf_verify(pk, a2, al - 1, epi, beta);
        CHECK(bad == 0, "changed, extended and truncated alpha refused");
        /* non-canonical s: s + q is the same scalar mod q */
        uint8_t p2[80];
        memcpy(p2, epi, 80);
        CHECK(add32(p2 + 48, QL) == 0, "s + q fits");
        CHECK(!vrf_verify(pk, alpha, al, p2, beta) && zero(beta, 64), "s + q refused");
        CHECK(!vrf_proof_to_hash(p2, beta) && zero(beta, 64), "proof_to_hash refuses s + q");
        memcpy(p2 + 48, QL, 32);
        CHECK(!vrf_verify(pk, alpha, al, p2, beta) && !vrf_proof_to_hash(p2, beta),
              "s = q refused");
        memset(p2 + 48, 0xff, 32);
        CHECK(!vrf_proof_to_hash(p2, beta), "s = 2^256-1 refused");
        /* s = q - 1 decodes */
        memcpy(p2 + 48, QL, 32);
        p2[48]--;
        CHECK(vrf_proof_to_hash(p2, beta) && memcmp(beta, ebeta, 64) == 0,
              "s = q - 1 decodes; beta depends on Gamma only");
        /* the same proof under another vector's key */
        uint8_t opk[32];
        unhex(V[(n + 1) % 3].pk, opk);
        CHECK(!vrf_verify(opk, alpha, al, epi, beta), "proof under another key refused");
        /* sk whose pk half is wrong */
        int took = 0;
        for (int i = 32; i < 64; i++) {
            sk[i] ^= 0x40;
            took += vrf_prove(sk, alpha, al, pi) || !zero(pi, 80);
            took += vrf_prove_trace(sk, alpha, al, pi, &tr);
            sk[i] ^= 0x40;
        }
        CHECK(took == 0, "sk with any byte of its pk half wrong refused");
        CHECK(!vrf_prove_trace(sk, alpha, al, pi, NULL), "trace NULL");
    }
}

/* point encodings: y (LE, 255 bits) with sign bit */
static void enc(uint8_t s[32], const char *hex, int sign)
{
    unhex(hex, s);
    s[31] = (uint8_t) ((s[31] & 0x7f) | (sign << 7));
}

#define PM1  "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f" /* p - 1 */
#define P0   "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f" /* p */
#define P1   "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f" /* p + 1 */
#define Y0   "0000000000000000000000000000000000000000000000000000000000000000"
#define Y1   "0100000000000000000000000000000000000000000000000000000000000000"
#define BY2  "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05"
#define BY2N "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a"

/* challenge over five encoded points (V6), as the test's own reference */
static void ref_challenge(uint8_t c[16], const uint8_t *y, const uint8_t *h, const uint8_t *g,
                          const uint8_t *u, const uint8_t *v)
{
    sha512_context x;
    uint8_t f[2] = {3, 2}, b = 0, d[64];
    sha512_init(&x);
    sha512_update(&x, f, 2);
    sha512_update(&x, y, 32);
    sha512_update(&x, h, 32);
    sha512_update(&x, g, 32);
    sha512_update(&x, u, 32);
    sha512_update(&x, v, 32);
    sha512_update(&x, &b, 1);
    sha512_final(&x, d);
    memcpy(c, d, 16);
}

static void test_points(void)
{
    uint8_t s[32];
    /* canonical small-order points decode (they are points) */
    const char *small[8] = {Y0, Y0, Y1, BY2, BY2, BY2N, BY2N, PM1};
    const int sg[8] = {0, 1, 0, 0, 1, 0, 1, 0};
    int ok = 0;
    for (int i = 0; i < 8; i++) {
        enc(s, small[i], sg[i]);
        ok += vrf_point_valid(s);
    }
    CHECK(ok == 8, "the eight small-order points decode");
    /* non-canonical encodings */
    enc(s, Y1, 1);
    CHECK(!vrf_point_valid(s), "-0 (y = 1, sign set) refused");
    enc(s, PM1, 1);
    CHECK(!vrf_point_valid(s), "-0 (y = p - 1, sign set) refused");
    int bad = 0;
    for (int add = 0; add < 19; add++)
        for (int sign = 0; sign < 2; sign++) {
            enc(s, P0, sign);
            s[0] = (uint8_t) (s[0] + add); /* p + add, add < 19 */
            bad += vrf_point_valid(s);
        }
    CHECK(bad == 0, "y + p (y < 19) refused, both signs");
    memset(s, 0xff, 32);
    CHECK(!vrf_point_valid(s), "y = 2^255 - 1 refused");
    /* a byte below 0x7f in the top byte with y canonical: the base point */
    uint8_t B[32];
    ge_p3 P;
    uint8_t one[32] = {1};
    ge_scalarmult_base(&P, one);
    ge_p3_tobytes(B, &P);
    CHECK(vrf_point_valid(B) && B[0] == 0x58, "the base point decodes");
    B[31] ^= 0x80;
    CHECK(vrf_point_valid(B), "-B decodes");
    CHECK(!vrf_point_valid(NULL), "NULL");
    /* canonical y with byte 0 >= 0xed and ONE middle byte below 0xff: for
     * every position k some such y is a point (each y is with odds 1/2) */
    int every = 0;
    for (int k = 1; k <= 30; k++) {
        int cnt = 0;
        for (int y0 = 0xed; y0 <= 0xff; y0++) {
            enc(s, PM1, 0);
            s[0] = (uint8_t) y0;
            s[k] = 0x00;
            cnt += vrf_point_valid(s);
        }
        every += cnt > 0;
    }
    CHECK(every == 30, "y < p with a low byte >= 0xed is canonical at every byte position");
    /* top byte 0x7e: canonical regardless of the rest */
    int cnt = 0;
    for (int y0 = 0xed; y0 <= 0xff; y0++) {
        memset(s, 0xff, 32);
        s[31] = 0x7e;
        s[0] = (uint8_t) y0;
        cnt += vrf_point_valid(s);
    }
    CHECK(cnt > 0, "top byte 0x7e is canonical");
}

/* forge (Gamma = identity, c, s) that satisfies the verification equation
 * under a small-order Y: U = s*B, V = s*H when 8 | c (so c*Y = 0). */
static void test_small_order(void)
{
    const char *small[8] = {Y0, Y0, Y1, BY2, BY2, BY2N, BY2N, PM1};
    const int sg[8] = {0, 1, 0, 0, 1, 0, 1, 0};
    uint8_t alpha[3] = {'z', 'x', 'v'};
    int refused = 0, forged = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t y[32], h[32], g[32] = {1}, u[32], v[32], c[16], pi[80], beta[64];
        enc(y, small[i], sg[i]);
        if (!vrf_encode_to_curve(y, alpha, 3, h)) continue;
        ge_p3 H, P;
        ge_p2 R;
        vrf_ge_frombytes_negate_vartime(&H, h); /* -H */
        for (uint32_t sv = 1; sv < 1000; sv++) {
            uint8_t s[32] = {0}, z[32] = {0};
            memcpy(s, &sv, 4);
            ge_scalarmult_base(&P, s);
            ge_p3_tobytes(u, &P);
            ge_double_scalarmult_vartime(&R, s, &H, z); /* -s*H */
            ge_tobytes(v, &R);
            v[31] ^= 0x80; /* s*H */
            ref_challenge(c, y, h, g, u, v);
            if (c[0] & 7) continue;
            memcpy(pi, g, 32);
            memcpy(pi + 32, c, 16);
            memcpy(pi + 48, s, 32);
            forged++;
            refused += !vrf_verify(y, alpha, 3, pi, beta) && zero(beta, 64);
            break;
        }
    }
    CHECK(forged == 8, "a forgery exists for every small-order key");
    CHECK(refused == 8, "validate_key refuses every small-order key");

    /* the same forgery shape under a real key fails (control) */
    uint8_t seed[32] = {9}, pk[32], sk[64], pi[80], beta[64];
    vrf_keypair(seed, pk, sk);
    CHECK(vrf_prove(sk, alpha, 3, pi) && vrf_verify(pk, alpha, 3, pi, beta), "control proof");
    /* Gamma replaced by a non-canonical encoding of the identity */
    uint8_t p2[80];
    memcpy(p2, pi, 80);
    enc(p2, P1, 0);
    CHECK(!vrf_verify(pk, alpha, 3, p2, beta) && !vrf_proof_to_hash(p2, beta),
          "non-canonical Gamma refused");
    memcpy(p2, pi, 80);
    p2[31] ^= 0x80; /* -Gamma: a valid point, wrong proof */
    CHECK(!vrf_verify(pk, alpha, 3, p2, beta), "-Gamma refused");
    CHECK(vrf_proof_to_hash(p2, beta), "-Gamma decodes for proof_to_hash");
    /* pk non-canonical: refused before anything else */
    uint8_t npk[32];
    enc(npk, P1, 0);
    CHECK(!vrf_verify(npk, alpha, 3, pi, beta), "non-canonical pk refused");
    memset(npk, 0xff, 32);
    CHECK(!vrf_verify(npk, alpha, 3, pi, beta), "pk 2^256-1 refused");
}

static void test_args(void)
{
    uint8_t seed[32] = {1, 2, 3}, pk[32], sk[64], pi[80], beta[64], h[32], a = 7;
    vrf_keypair(seed, pk, sk);
    CHECK(!vrf_prove(NULL, &a, 1, pi), "prove NULL sk");
    CHECK(!vrf_prove(sk, &a, 1, NULL), "prove NULL pi");
    memset(pi, 0xaa, 80);
    CHECK(!vrf_prove(sk, NULL, 1, pi) && zero(pi, 80), "prove NULL alpha with length");
    CHECK(vrf_prove(sk, NULL, 0, pi), "prove empty alpha as NULL");
    CHECK(vrf_verify(pk, NULL, 0, pi, beta), "verify empty alpha as NULL");
    uint8_t b2[64];
    CHECK(vrf_verify(pk, (const uint8_t *) "", 0, pi, b2) && memcmp(beta, b2, 64) == 0,
          "NULL and \"\" are the same empty alpha");
    CHECK(!vrf_verify(pk, NULL, 1, pi, beta) && zero(beta, 64), "verify NULL alpha with length");
    CHECK(!vrf_verify(NULL, &a, 0, pi, beta), "verify NULL pk");
    CHECK(!vrf_verify(pk, &a, 0, NULL, beta), "verify NULL pi");
    CHECK(!vrf_verify(pk, &a, 0, pi, NULL), "verify NULL beta");
    CHECK(!vrf_proof_to_hash(NULL, beta) && zero(beta, 64), "p2h NULL pi");
    CHECK(!vrf_proof_to_hash(pi, NULL), "p2h NULL beta");
    memset(h, 0xaa, 32);
    CHECK(!vrf_encode_to_curve(NULL, &a, 1, h) && zero(h, 32), "e2c NULL pk");
    memset(h, 0xaa, 32);
    CHECK(!vrf_encode_to_curve(pk, NULL, 1, h) && zero(h, 32), "e2c NULL alpha");
    CHECK(!vrf_encode_to_curve(pk, &a, 1, NULL), "e2c NULL h");
    CHECK(vrf_encode_to_curve(pk, NULL, 0, h) && vrf_point_valid(h), "e2c gives a point");
}

static void test_adapters(void)
{
    uint8_t seed[32], pk[32], sk[64], pi[80], out[32], beta[64], ds[32];
    vrf_dsp_ctx_t ctx;
    for (int i = 0; i < 32; i++) {
        seed[i] = (uint8_t) (i * 5 + 3);
        ds[i] = (uint8_t) (i ^ 0x5a);
    }
    vrf_keypair(seed, pk, sk);
    memcpy(ctx.pk, pk, 32);
    CHECK(vrf_dsp_out(sk, ds, pi, out), "dsp out");
    CHECK(vrf_verify(pk, ds, 32, pi, beta) && memcmp(out, beta, 32) == 0, "out = beta[0..31]");
    CHECK(vrf_dsp_verify(&ctx, ds, pi, 80, out), "dsp verify");
    CHECK(!vrf_dsp_verify(&ctx, ds, pi, 79, out), "dsp short proof");
    CHECK(!vrf_dsp_verify(&ctx, ds, pi, 81, out), "dsp long proof");
    CHECK(!vrf_dsp_verify(NULL, ds, pi, 80, out), "dsp NULL ctx");
    CHECK(!vrf_dsp_verify(&ctx, NULL, pi, 80, out), "dsp NULL seed");
    CHECK(!vrf_dsp_verify(&ctx, ds, pi, 80, NULL), "dsp NULL out");
    CHECK(!vrf_dsp_verify(&ctx, ds, NULL, 80, out), "dsp NULL proof");
    int bad = 0;
    for (int i = 0; i < 32; i++) {
        out[i] ^= 1;
        bad += vrf_dsp_verify(&ctx, ds, pi, 80, out);
        out[i] ^= 1;
    }
    CHECK(bad == 0, "dsp: every byte of out checked");
    ds[0] ^= 1;
    CHECK(!vrf_dsp_verify(&ctx, ds, pi, 80, out), "dsp other seed");
    ds[0] ^= 1;
    ctx.pk[3] ^= 1;
    CHECK(!vrf_dsp_verify(&ctx, ds, pi, 80, out), "dsp other key");
    memset(out, 0xaa, 32);
    CHECK(!vrf_dsp_out(sk, NULL, pi, out) && zero(out, 32), "dsp_out NULL seed");
    CHECK(!vrf_dsp_out(sk, ds, pi, NULL), "dsp_out NULL out");
    memset(out, 0xaa, 32);
    CHECK(!vrf_dsp_out(NULL, ds, pi, out) && zero(out, 32), "dsp_out NULL sk");

    /* allotment beacon */
    uint8_t alpha[VRF_BEACON_ALPHA], b[32];
    vrf_beacon_alpha(0x0102030405060708ull, alpha);
    CHECK(memcmp(alpha, "ZXV-ALLOT-BEACON", 16) == 0 && alpha[16] == 8 && alpha[23] == 1,
          "beacon alpha = tag || epoch LE64");
    CHECK(vrf_prove(sk, alpha, sizeof alpha, pi), "beacon prove");
    CHECK(vrf_beacon(pk, alpha, sizeof alpha, pi, b), "beacon verify");
    CHECK(vrf_verify(pk, alpha, sizeof alpha, pi, beta) && memcmp(b, beta, 32) == 0,
          "beacon secret = beta[0..31]");
    pi[50] ^= 1;
    memset(b, 0xaa, 32);
    CHECK(!vrf_beacon(pk, alpha, sizeof alpha, pi, b) && zero(b, 32), "beacon refused, zeroed");
    CHECK(!vrf_beacon(pk, alpha, sizeof alpha, pi, NULL), "beacon NULL out");
    /* allotment's direct use: a 32-byte header hash as alpha */
    pi[50] ^= 1;
    CHECK(vrf_prove(sk, ds, 32, pi) && vrf_verify(pk, ds, 32, pi, beta), "32-byte alpha");
}

static uint64_t xs = 0x9e3779b97f4a7c15ull;
static uint64_t rnd(void)
{
    xs ^= xs << 13;
    xs ^= xs >> 7;
    xs ^= xs << 17;
    return xs;
}

static void test_random(void)
{
    int bad = 0, n = 300;
    for (int t = 0; t < n; t++) {
        uint8_t seed[32], pk[32], sk[64], alpha[200], pi[80], beta[64], b2[64];
        for (int i = 0; i < 32; i++) seed[i] = (uint8_t) rnd();
        size_t al = (size_t) (rnd() % (t < 20 ? 200 : 40));
        for (size_t i = 0; i < al; i++) alpha[i] = (uint8_t) rnd();
        vrf_keypair(seed, pk, sk);
        bad += !vrf_prove(sk, alpha, al, pi);
        bad += !vrf_verify(pk, alpha, al, pi, beta);
        bad += !vrf_proof_to_hash(pi, b2) || memcmp(beta, b2, 64) != 0;
        size_t j = (size_t) (rnd() % 80);
        pi[j] ^= (uint8_t) (1u << (rnd() % 8));
        bad += vrf_verify(pk, alpha, al, pi, beta);
    }
    CHECK(bad == 0, "random round trips (prove, verify, proof_to_hash, one-bit tamper)");
    /* determinism and distinct outputs */
    uint8_t seed[32] = {7}, pk[32], sk[64], p1[80], p2[80], m[1] = {0};
    vrf_keypair(seed, pk, sk);
    vrf_prove(sk, m, 1, p1);
    vrf_prove(sk, m, 1, p2);
    CHECK(memcmp(p1, p2, 80) == 0, "proving is deterministic");
    m[0] = 1;
    vrf_prove(sk, m, 1, p2);
    CHECK(memcmp(p1, p2, 32) != 0, "different alpha, different Gamma");
}

/* ed25519_verify.c links alongside: its RFC 8032 test 1 still verifies */
static void test_link(void)
{
    uint8_t pk[32], sig[64];
    unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk);
    unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e3970"
          "1cf9b46bd25bf5f0595bbe24655141438e7a100b",
          sig);
    CHECK(ed25519_verify(NULL, 0, sig, pk), "ed25519_verify links next to vrf.c");
}

int main(void)
{
    test_vectors();
    test_points();
    test_small_order();
    test_args();
    test_adapters();
    test_random();
    test_link();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}
