/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* pq_matrix.c — layered hybrid KEM, dual signatures, wire format.
 * See pq_matrix.h for the design, the combiner and the HONEST LIMITS.
 *
 * Freestanding: no libc calls in this file. SHA3-256 / SHAKE-256 come from
 * the slhdsa-c Keccak (slhdsa/sha3_api.c, symbols zxv_slh_*), which is
 * already linked for SLH-DSA; the test checks the combiner against the
 * kernel's independent Keccak in src/mlkem/keccak.c.
 */
#include "pq_matrix.h"

#include "../tls/x25519.h"
#include "pq_security.h" /* ML-KEM-768 (src/mlkem) and ML-DSA-65 */
#include "slhdsa/sha3_api.h"

#if PQM_MLKEM768_EK_BYTES != MLKEM768_EK_BYTES || PQM_MLKEM768_DK_BYTES != MLKEM768_DK_BYTES ||    \
    PQM_MLKEM768_CT_BYTES != MLKEM768_CT_BYTES || PQM_MLDSA65_PK_BYTES != PQ_MLDSA65_PK_BYTES ||   \
    PQM_MLDSA65_SK_BYTES != PQ_MLDSA65_SK_BYTES || PQM_MLDSA65_SIG_BYTES != PQ_MLDSA65_SIG_BYTES
#    error "pq_matrix.h sizes disagree with the kernel's ML-KEM-768 / ML-DSA-65"
#endif

/* ===================================================================== */
/* small helpers                                                          */
/* ===================================================================== */

void pqm_wipe(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    while (n--) *v++ = 0;
}

static void cpy(uint8_t *d, const uint8_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static bool level_ok(unsigned level)
{
    return level == PQM_LEVEL_STANDARD || level == PQM_LEVEL_HIGH || level == PQM_LEVEL_MATRIX;
}

const char *pqm_level_name(pqm_level_t level)
{
    switch (level) {
    case PQM_LEVEL_STANDARD:
        return "STANDARD";
    case PQM_LEVEL_HIGH:
        return "HIGH";
    case PQM_LEVEL_MATRIX:
        return "MATRIX";
    }
    return "?";
}

/* SHAKE-256( label || 0x00 || level || in ) -> out. Domain-separated
 * expansion of the caller's 32-byte seeds into component inputs. */
static void expand(const char *label, unsigned level, const uint8_t *in, size_t in_len,
                   uint8_t *out, size_t out_len)
{
    sha3_var_t c;
    size_t n = 0;
    while (label[n]) n++;
    uint8_t sep[2] = {0, (uint8_t) level};
    shake256_init(&c);
    shake_update(&c, label, n);
    shake_update(&c, sep, 2);
    shake_update(&c, in, in_len);
    shake_out(&c, out, out_len);
    pqm_wipe(&c, sizeof c);
}

/* FIPS 203 section 7.2 modulus check on k encoded 12-bit polynomials. */
static bool mlkem_ek_ok(const uint8_t *ek, unsigned k)
{
    unsigned bad = 0;
    for (unsigned i = 0; i < 384u * k; i += 3) {
        unsigned a = ek[i] | ((unsigned) (ek[i + 1] & 0x0f) << 8);
        unsigned b = (ek[i + 1] >> 4) | ((unsigned) ek[i + 2] << 4);
        bad |= (unsigned) (a >= 3329u) | (unsigned) (b >= 3329u);
    }
    return bad == 0;
}

/* ===================================================================== */
/* component tables: one place that says what each level contains        */
/* ===================================================================== */

typedef struct {
    uint16_t id;
    uint32_t len;  /* total component length */
    uint8_t *p;    /* first part */
    uint32_t len1; /* bytes at p (== len unless a second part follows) */
    uint8_t *p2;   /* optional second part (X25519 sk || pk) */
} comp_t;

#define MAXC 3

static void put(comp_t *c, uint16_t id, uint8_t *p, uint32_t len)
{
    c->id = id;
    c->len = len;
    c->p = p;
    c->len1 = len;
    c->p2 = 0;
}

static unsigned kem_pk_comps(pqm_kem_pk_t *o, unsigned level, comp_t *c)
{
    unsigned n = 0;
    if (level == PQM_LEVEL_STANDARD)
        put(&c[n++], PQM_ALG_MLKEM768, o->mlkem_ek, PQM_MLKEM768_EK_BYTES);
    else
        put(&c[n++], PQM_ALG_MLKEM1024, o->mlkem_ek, PQM_MLKEM1024_EK_BYTES);
    if (level == PQM_LEVEL_MATRIX) put(&c[n++], PQM_ALG_HQC5, o->hqc_pk, PQM_HQC5_PK_BYTES);
    put(&c[n++], PQM_ALG_X25519, o->x25519_pk, PQM_X25519_LEN);
    return n;
}

static unsigned kem_ct_comps(pqm_kem_ct_t *o, unsigned level, comp_t *c)
{
    unsigned n = 0;
    if (level == PQM_LEVEL_STANDARD)
        put(&c[n++], PQM_ALG_MLKEM768, o->mlkem_ct, PQM_MLKEM768_CT_BYTES);
    else
        put(&c[n++], PQM_ALG_MLKEM1024, o->mlkem_ct, PQM_MLKEM1024_CT_BYTES);
    if (level == PQM_LEVEL_MATRIX) put(&c[n++], PQM_ALG_HQC5, o->hqc_ct, PQM_HQC5_CT_BYTES);
    put(&c[n++], PQM_ALG_X25519, o->x25519_eph, PQM_X25519_LEN);
    return n;
}

static unsigned kem_sk_comps(pqm_kem_sk_t *o, unsigned level, comp_t *c)
{
    unsigned n = 0;
    if (level == PQM_LEVEL_STANDARD)
        put(&c[n++], PQM_ALG_MLKEM768, o->mlkem_dk, PQM_MLKEM768_DK_BYTES);
    else
        put(&c[n++], PQM_ALG_MLKEM1024, o->mlkem_dk, PQM_MLKEM1024_DK_BYTES);
    if (level == PQM_LEVEL_MATRIX) put(&c[n++], PQM_ALG_HQC5, o->hqc_sk, PQM_HQC5_SK_BYTES);
    put(&c[n], PQM_ALG_X25519, o->x25519_sk, 2 * PQM_X25519_LEN);
    c[n].len1 = PQM_X25519_LEN;
    c[n].p2 = o->x25519_pk;
    n++;
    return n;
}

static unsigned sig_pk_comps(pqm_sig_pk_t *o, unsigned level, comp_t *c)
{
    unsigned n = 0;
    if (level == PQM_LEVEL_STANDARD)
        put(&c[n++], PQM_ALG_MLDSA65, o->mldsa_pk, PQM_MLDSA65_PK_BYTES);
    else
        put(&c[n++], PQM_ALG_MLDSA87, o->mldsa_pk, PQM_MLDSA87_PK_BYTES);
    if (level == PQM_LEVEL_MATRIX) put(&c[n++], PQM_ALG_SLH256S, o->slh_pk, PQM_SLH256S_PK_BYTES);
    return n;
}

static unsigned sig_comps(pqm_sig_t *o, unsigned level, comp_t *c)
{
    unsigned n = 0;
    if (level == PQM_LEVEL_STANDARD)
        put(&c[n++], PQM_ALG_MLDSA65, o->mldsa_sig, PQM_MLDSA65_SIG_BYTES);
    else
        put(&c[n++], PQM_ALG_MLDSA87, o->mldsa_sig, PQM_MLDSA87_SIG_BYTES);
    if (level == PQM_LEVEL_MATRIX) put(&c[n++], PQM_ALG_SLH256S, o->slh_sig, PQM_SLH256S_SIG_BYTES);
    return n;
}

static unsigned sig_sk_comps(pqm_sig_sk_t *o, unsigned level, comp_t *c)
{
    unsigned n = 0;
    if (level == PQM_LEVEL_STANDARD)
        put(&c[n++], PQM_ALG_MLDSA65, o->mldsa_sk, PQM_MLDSA65_SK_BYTES);
    else
        put(&c[n++], PQM_ALG_MLDSA87, o->mldsa_sk, PQM_MLDSA87_SK_BYTES);
    if (level == PQM_LEVEL_MATRIX) put(&c[n++], PQM_ALG_SLH256S, o->slh_sk, PQM_SLH256S_SK_BYTES);
    put(&c[n++], PQM_ALG_PKHASH, o->pk_hash, 32);
    return n;
}

/* ===================================================================== */
/* wire format                                                            */
/* ===================================================================== */

static size_t comps_size(const comp_t *c, unsigned n)
{
    size_t s = PQM_HDR_BYTES;
    for (unsigned i = 0; i < n; i++) s += PQM_COMP_HDR_BYTES + c[i].len;
    return s;
}

/* A byte sink: either a buffer or a SHA3 context (for hashing an
 * encoding without materialising it). */
typedef struct {
    uint8_t *buf;
    size_t pos;
    sha3_var_t *h;
} sink_t;

static void emit(sink_t *s, const uint8_t *p, size_t n)
{
    if (s->h) {
        sha3_update(s->h, p, n);
    } else {
        cpy(s->buf + s->pos, p, n);
    }
    s->pos += n;
}

static void emit_obj(sink_t *s, unsigned type, unsigned level, const comp_t *c, unsigned n)
{
    uint8_t hdr[PQM_HDR_BYTES] = {
        'Z', 'Q', PQM_WIRE_VERSION, (uint8_t) type, (uint8_t) level, (uint8_t) n};
    emit(s, hdr, sizeof hdr);
    for (unsigned i = 0; i < n; i++) {
        uint8_t ch[PQM_COMP_HDR_BYTES] = {(uint8_t) (c[i].id >> 8),   (uint8_t) c[i].id,
                                          (uint8_t) (c[i].len >> 24), (uint8_t) (c[i].len >> 16),
                                          (uint8_t) (c[i].len >> 8),  (uint8_t) c[i].len};
        emit(s, ch, sizeof ch);
        emit(s, c[i].p, c[i].len1);
        if (c[i].p2) emit(s, c[i].p2, c[i].len - c[i].len1);
    }
}

static size_t encode_obj(unsigned type, unsigned level, const comp_t *c, unsigned n, uint8_t *out,
                         size_t cap)
{
    size_t need = comps_size(c, n);
    if (!out || cap < need) return 0;
    sink_t s = {out, 0, 0};
    emit_obj(&s, type, level, c, n);
    return s.pos;
}

/* Strict parse: header, then exactly the components `c` describes, in
 * order, with exactly their lengths, and nothing after. Copies into the
 * component pointers only once the whole input has been validated. */
static bool decode_obj(unsigned type, const uint8_t *in, size_t len, unsigned level,
                       const comp_t *c, unsigned n)
{
    if (len != comps_size(c, n)) return false;
    if (in[0] != 'Z' || in[1] != 'Q' || in[2] != PQM_WIRE_VERSION || in[3] != type ||
        in[4] != level || in[5] != n)
        return false;
    size_t pos = PQM_HDR_BYTES;
    for (unsigned i = 0; i < n; i++) {
        uint16_t id = (uint16_t) ((in[pos] << 8) | in[pos + 1]);
        uint32_t l = ((uint32_t) in[pos + 2] << 24) | ((uint32_t) in[pos + 3] << 16) |
                     ((uint32_t) in[pos + 4] << 8) | in[pos + 5];
        if (id != c[i].id || l != c[i].len) return false;
        pos += PQM_COMP_HDR_BYTES + l;
    }
    pos = PQM_HDR_BYTES;
    for (unsigned i = 0; i < n; i++) {
        pos += PQM_COMP_HDR_BYTES;
        cpy(c[i].p, in + pos, c[i].len1);
        if (c[i].p2) cpy(c[i].p2, in + pos + c[i].len1, c[i].len - c[i].len1);
        pos += c[i].len;
    }
    return true;
}

/* Level byte of a candidate encoding, or 0 if it cannot be one. */
static unsigned peek_level(const uint8_t *in, size_t len)
{
    if (!in || len < PQM_HDR_BYTES || !level_ok(in[4])) return 0;
    return in[4];
}

size_t pqm_encoded_size(pqm_obj_t type, pqm_level_t level)
{
    if (!level_ok(level)) return 0;
    comp_t c[MAXC];
    unsigned n;
    /* Sizes only depend on the level; a zero object serves as a template. */
    static pqm_kem_pk_t kp;
    static pqm_kem_ct_t kc;
    static pqm_kem_sk_t ks;
    static pqm_sig_pk_t sp;
    static pqm_sig_t sg;
    static pqm_sig_sk_t ss;
    switch (type) {
    case PQM_OBJ_KEM_PK:
        n = kem_pk_comps(&kp, level, c);
        break;
    case PQM_OBJ_KEM_CT:
        n = kem_ct_comps(&kc, level, c);
        break;
    case PQM_OBJ_KEM_SK:
        n = kem_sk_comps(&ks, level, c);
        break;
    case PQM_OBJ_SIG_PK:
        n = sig_pk_comps(&sp, level, c);
        break;
    case PQM_OBJ_SIG:
        n = sig_comps(&sg, level, c);
        break;
    case PQM_OBJ_SIG_SK:
        n = sig_sk_comps(&ss, level, c);
        break;
    default:
        return 0;
    }
    return comps_size(c, n);
}

/* One encode/decode pair per object type. The casts drop const only to
 * share the component table with the decoder; encode never writes. */
#define PQM_CODEC(name, T, OBJ, COMPS)                                                             \
    size_t name##_encode(const T *o, uint8_t *out, size_t cap)                                     \
    {                                                                                              \
        if (!o || !level_ok(o->level)) return 0;                                                   \
        comp_t c[MAXC];                                                                            \
        unsigned n = COMPS((T *) o, o->level, c);                                                  \
        return encode_obj(OBJ, o->level, c, n, out, cap);                                          \
    }                                                                                              \
    bool name##_decode(T *o, const uint8_t *in, size_t len)                                        \
    {                                                                                              \
        if (!o) return false;                                                                      \
        pqm_wipe(o, sizeof *o);                                                                    \
        unsigned level = peek_level(in, len);                                                      \
        if (!level) return false;                                                                  \
        comp_t c[MAXC];                                                                            \
        unsigned n = COMPS(o, level, c);                                                           \
        if (!decode_obj(OBJ, in, len, level, c, n)) {                                              \
            pqm_wipe(o, sizeof *o);                                                                \
            return false;                                                                          \
        }                                                                                          \
        o->level = (uint8_t) level;                                                                \
        return true;                                                                               \
    }

PQM_CODEC(pqm_kem_pk, pqm_kem_pk_t, PQM_OBJ_KEM_PK, kem_pk_comps)
PQM_CODEC(pqm_kem_ct, pqm_kem_ct_t, PQM_OBJ_KEM_CT, kem_ct_comps)
PQM_CODEC(pqm_kem_sk, pqm_kem_sk_t, PQM_OBJ_KEM_SK, kem_sk_comps)
PQM_CODEC(pqm_sig_pk, pqm_sig_pk_t, PQM_OBJ_SIG_PK, sig_pk_comps)
PQM_CODEC(pqm_sig, pqm_sig_t, PQM_OBJ_SIG, sig_comps)
PQM_CODEC(pqm_sig_sk, pqm_sig_sk_t, PQM_OBJ_SIG_SK, sig_sk_comps)

void pqm_kem_sk_wipe(pqm_kem_sk_t *sk)
{
    if (sk) pqm_wipe(sk, sizeof *sk);
}

void pqm_sig_sk_wipe(pqm_sig_sk_t *sk)
{
    if (sk) pqm_wipe(sk, sizeof *sk);
}

/* ===================================================================== */
/* hybrid KEM                                                             */
/* ===================================================================== */

/* keygen expansion: d | z | hqc seed_kem | x25519 sk */
#define KG_BYTES (4 * 32)
/* encaps expansion: ML-KEM m | HQC m | HQC salt | x25519 eph sk */
#define EN_BYTES (32 + 32 + PQM_HQC5_SALT_BYTES + 32)

bool pqm_kem_keygen(pqm_level_t level, const uint8_t seed[PQM_SEED_BYTES], pqm_kem_pk_t *pk,
                    pqm_kem_sk_t *sk)
{
    if (!seed || !pk || !sk || !level_ok(level)) return false;
    pqm_wipe(pk, sizeof *pk);
    pqm_wipe(sk, sizeof *sk);
    uint8_t r[KG_BYTES];
    expand("ZXV-PQM-v1/kem-keygen", level, seed, PQM_SEED_BYTES, r, sizeof r);

    if (level == PQM_LEVEL_STANDARD) {
        mlkem768_keygen(r, r + 32, pk->mlkem_ek, sk->mlkem_dk);
    } else {
        pqm_mlkem1024_keygen(r, r + 32, pk->mlkem_ek, sk->mlkem_dk);
    }
    if (level == PQM_LEVEL_MATRIX) pqm_hqc5_keygen(r + 64, pk->hqc_pk, sk->hqc_sk);
    cpy(sk->x25519_sk, r + 96, 32);
    x25519_public(pk->x25519_pk, sk->x25519_sk);
    cpy(sk->x25519_pk, pk->x25519_pk, 32);

    pk->level = sk->level = (uint8_t) level;
    pqm_wipe(r, sizeof r);
    return true;
}

/* The combiner. Field lengths are fixed per level (see pq_matrix.h). */
static void combine(unsigned level, const uint8_t ss_m[32], const uint8_t ss_h[32],
                    const uint8_t ss_x[32], const pqm_kem_ct_t *ct, const uint8_t pk_x[32],
                    uint8_t out[PQM_SS_BYTES])
{
    sha3_var_t h;
    sha3_init(&h, 32);
    sha3_update(&h, ss_m, 32);
    if (level == PQM_LEVEL_MATRIX) sha3_update(&h, ss_h, 32);
    sha3_update(&h, ss_x, 32);
    sha3_update(&h, ct->mlkem_ct,
                level == PQM_LEVEL_STANDARD ? PQM_MLKEM768_CT_BYTES : PQM_MLKEM1024_CT_BYTES);
    if (level == PQM_LEVEL_MATRIX) sha3_update(&h, ct->hqc_ct, PQM_HQC5_CT_BYTES);
    sha3_update(&h, ct->x25519_eph, 32);
    sha3_update(&h, pk_x, 32);
    uint8_t tail[sizeof PQM_LABEL] = PQM_LABEL; /* label then the level byte */
    tail[sizeof PQM_LABEL - 1] = (uint8_t) level;
    sha3_update(&h, tail, sizeof tail);
    sha3_final(&h, out);
    pqm_wipe(&h, sizeof h);
}

bool pqm_encaps(const pqm_kem_pk_t *pk, const uint8_t coins[PQM_SEED_BYTES], pqm_kem_ct_t *ct,
                uint8_t ss[PQM_SS_BYTES])
{
    if (!ss) return false;
    pqm_wipe(ss, PQM_SS_BYTES);
    if (!pk || !coins || !ct || !level_ok(pk->level)) return false;
    unsigned level = pk->level;
    pqm_wipe(ct, sizeof *ct);

    uint8_t r[EN_BYTES], ss_m[32], ss_h[32] = {0}, ss_x[32];
    bool ok = true;
    expand("ZXV-PQM-v1/kem-encaps", level, coins, PQM_SEED_BYTES, r, sizeof r);

    if (level == PQM_LEVEL_STANDARD) {
        ok = mlkem_ek_ok(pk->mlkem_ek, 3);
        if (ok) mlkem768_encaps(pk->mlkem_ek, r, ct->mlkem_ct, ss_m);
    } else {
        ok = pqm_mlkem1024_encaps(pk->mlkem_ek, r, ct->mlkem_ct, ss_m);
    }
    if (ok && level == PQM_LEVEL_MATRIX)
        pqm_hqc5_encaps(pk->hqc_pk, r + 32, r + 64, ct->hqc_ct, ss_h);
    if (ok) {
        const uint8_t *eph = r + 64 + PQM_HQC5_SALT_BYTES;
        x25519_public(ct->x25519_eph, eph);
        ok = x25519_shared(ss_x, eph, pk->x25519_pk); /* false on a low-order pk */
    }
    if (ok) {
        ct->level = (uint8_t) level;
        combine(level, ss_m, ss_h, ss_x, ct, pk->x25519_pk, ss);
    } else {
        pqm_wipe(ct, sizeof *ct);
    }
    pqm_wipe(r, sizeof r);
    pqm_wipe(ss_m, sizeof ss_m);
    pqm_wipe(ss_h, sizeof ss_h);
    pqm_wipe(ss_x, sizeof ss_x);
    return ok;
}

bool pqm_decaps(const pqm_kem_sk_t *sk, const pqm_kem_ct_t *ct, uint8_t ss[PQM_SS_BYTES])
{
    if (!ss) return false;
    pqm_wipe(ss, PQM_SS_BYTES);
    if (!sk || !ct || !level_ok(sk->level) || ct->level != sk->level) return false;
    unsigned level = sk->level;
    uint8_t ss_m[32], ss_h[32] = {0}, ss_x[32];

    /* Every layer runs, whatever the others produce: no early exit that
     * would time-stamp which layer a forged ciphertext upset. */
    if (level == PQM_LEVEL_STANDARD)
        mlkem768_decaps(sk->mlkem_dk, ct->mlkem_ct, ss_m);
    else
        pqm_mlkem1024_decaps(sk->mlkem_dk, ct->mlkem_ct, ss_m);
    if (level == PQM_LEVEL_MATRIX) pqm_hqc5_decaps(sk->hqc_sk, ct->hqc_ct, ss_h);
    /* Raw X25519: a low-order ephemeral key gives an all-zero ss_x, which
     * the combiner absorbs; the post-quantum layers still carry it. */
    x25519(ss_x, sk->x25519_sk, ct->x25519_eph);

    combine(level, ss_m, ss_h, ss_x, ct, sk->x25519_pk, ss);
    pqm_wipe(ss_m, sizeof ss_m);
    pqm_wipe(ss_h, sizeof ss_h);
    pqm_wipe(ss_x, sizeof ss_x);
    return true;
}

/* ===================================================================== */
/* dual signatures                                                        */
/* ===================================================================== */

pqm_level_t pqm_sig_level_for(pqm_sig_purpose_t purpose)
{
    switch (purpose) {
    case PQM_SIG_PURPOSE_IDENTITY_KEY:
    case PQM_SIG_PURPOSE_RELEASE:
    case PQM_SIG_PURPOSE_SETTLEMENT_BATCH:
    case PQM_SIG_PURPOSE_CHARTER_RECORD:
        return PQM_LEVEL_MATRIX;
    case PQM_SIG_PURPOSE_SESSION_MESSAGE:
        break;
    }
    return (pqm_level_t) 0;
}

const char *pqm_sig_purpose_name(pqm_sig_purpose_t purpose)
{
    switch (purpose) {
    case PQM_SIG_PURPOSE_IDENTITY_KEY:
        return "identity key";
    case PQM_SIG_PURPOSE_RELEASE:
        return "release / update";
    case PQM_SIG_PURPOSE_SETTLEMENT_BATCH:
        return "settlement batch";
    case PQM_SIG_PURPOSE_CHARTER_RECORD:
        return "treaty / charter record";
    case PQM_SIG_PURPOSE_SESSION_MESSAGE:
        return "session message (AEAD, no signature)";
    }
    return "?";
}

#define SIG_LABEL     "ZXV-PQM-v1/sig"
#define SIG_LABEL_LEN (sizeof SIG_LABEL - 1)
#define SIGCTX_MAX    (SIG_LABEL_LEN + 1 + 2 + 2 + 32 + 1 + PQM_CTX_MAX)

static void sig_pk_hash(const pqm_sig_pk_t *pk, uint8_t out[32])
{
    comp_t c[MAXC];
    unsigned n = sig_pk_comps((pqm_sig_pk_t *) pk, pk->level, c);
    sha3_var_t h;
    sha3_init(&h, 32);
    sink_t s = {0, 0, &h};
    emit_obj(&s, PQM_OBJ_SIG_PK, pk->level, c, n);
    sha3_final(&h, out);
}

/* The FIPS 204 / 205 context string each half signs under. */
static size_t sig_ctx(uint8_t out[SIGCTX_MAX], unsigned level, uint16_t self, uint16_t partner,
                      const uint8_t pk_hash[32], const uint8_t *ctx, size_t ctx_len)
{
    size_t n = 0;
    cpy(out, (const uint8_t *) SIG_LABEL, SIG_LABEL_LEN);
    n += SIG_LABEL_LEN;
    out[n++] = (uint8_t) level;
    out[n++] = (uint8_t) (self >> 8);
    out[n++] = (uint8_t) self;
    out[n++] = (uint8_t) (partner >> 8);
    out[n++] = (uint8_t) partner;
    cpy(out + n, pk_hash, 32);
    n += 32;
    out[n++] = (uint8_t) ctx_len;
    if (ctx_len) cpy(out + n, ctx, ctx_len);
    return n + ctx_len;
}

#define SKG_BYTES (32 + 3 * PQM_SLH256S_N) /* ML-DSA seed | SLH SK.seed, SK.prf, PK.seed */
#define SRN_BYTES (32 + PQM_SLH256S_N)     /* ML-DSA rnd | SLH addrnd */

bool pqm_sig_keygen(pqm_level_t level, const uint8_t seed[PQM_SEED_BYTES], pqm_sig_pk_t *pk,
                    pqm_sig_sk_t *sk)
{
    if (!seed || !pk || !sk || !level_ok(level)) return false;
    pqm_wipe(pk, sizeof *pk);
    pqm_wipe(sk, sizeof *sk);
    uint8_t r[SKG_BYTES];
    expand("ZXV-PQM-v1/sig-keygen", level, seed, PQM_SEED_BYTES, r, sizeof r);
    if (level == PQM_LEVEL_STANDARD)
        pq_mldsa65_keygen(r, pk->mldsa_pk, sk->mldsa_sk);
    else
        pqm_mldsa87_keygen(r, pk->mldsa_pk, sk->mldsa_sk);
    if (level == PQM_LEVEL_MATRIX) pqm_slh256s_keygen(r + 32, pk->slh_pk, sk->slh_sk);
    pk->level = sk->level = (uint8_t) level;
    sig_pk_hash(pk, sk->pk_hash);
    pqm_wipe(r, sizeof r);
    return true;
}

static bool args_ok(const uint8_t *msg, size_t msg_len, const uint8_t *ctx, size_t ctx_len)
{
    return (msg || !msg_len) && (ctx || !ctx_len) && ctx_len <= PQM_CTX_MAX &&
           (uint64_t) msg_len <= 0xffffffffu;
}

bool pqm_sign(const pqm_sig_sk_t *sk, const uint8_t *msg, size_t msg_len, const uint8_t *ctx,
              size_t ctx_len, const uint8_t *rnd, pqm_sig_t *sig)
{
    if (!sig) return false;
    pqm_wipe(sig, sizeof *sig);
    if (!sk || !level_ok(sk->level) || !args_ok(msg, msg_len, ctx, ctx_len)) return false;
    unsigned level = sk->level;
    uint8_t c[SIGCTX_MAX], r[SRN_BYTES];
    size_t cl;
    if (rnd) expand("ZXV-PQM-v1/sig-rnd", level, rnd, PQM_SEED_BYTES, r, sizeof r);

    if (level == PQM_LEVEL_STANDARD) {
        cl = sig_ctx(c, level, PQM_ALG_MLDSA65, PQM_ALG_NONE, sk->pk_hash, ctx, ctx_len);
        pq_mldsa65_sign(sk->mldsa_sk, msg, (uint32_t) msg_len, c, (uint32_t) cl, rnd ? r : 0,
                        sig->mldsa_sig);
    } else {
        uint16_t partner = level == PQM_LEVEL_MATRIX ? PQM_ALG_SLH256S : PQM_ALG_NONE;
        cl = sig_ctx(c, level, PQM_ALG_MLDSA87, partner, sk->pk_hash, ctx, ctx_len);
        pqm_mldsa87_sign(sk->mldsa_sk, msg, msg_len, c, cl, rnd ? r : 0, sig->mldsa_sig);
    }
    if (level == PQM_LEVEL_MATRIX) {
        cl = sig_ctx(c, level, PQM_ALG_SLH256S, PQM_ALG_MLDSA87, sk->pk_hash, ctx, ctx_len);
        pqm_slh256s_sign(sk->slh_sk, msg, msg_len, c, cl, rnd ? r + 32 : 0, sig->slh_sig);
    }
    sig->level = (uint8_t) level;
    pqm_wipe(r, sizeof r);
    return true;
}

bool pqm_verify(const pqm_sig_pk_t *pk, const uint8_t *msg, size_t msg_len, const uint8_t *ctx,
                size_t ctx_len, const pqm_sig_t *sig)
{
    if (!pk || !sig || !level_ok(pk->level) || sig->level != pk->level ||
        !args_ok(msg, msg_len, ctx, ctx_len))
        return false;
    unsigned level = pk->level;
    uint8_t c[SIGCTX_MAX], h[32];
    size_t cl;
    bool a, b = true;
    sig_pk_hash(pk, h);

    if (level == PQM_LEVEL_STANDARD) {
        cl = sig_ctx(c, level, PQM_ALG_MLDSA65, PQM_ALG_NONE, h, ctx, ctx_len);
        a = pq_mldsa65_verify(pk->mldsa_pk, msg, (uint32_t) msg_len, c, (uint32_t) cl,
                              sig->mldsa_sig);
    } else {
        uint16_t partner = level == PQM_LEVEL_MATRIX ? PQM_ALG_SLH256S : PQM_ALG_NONE;
        cl = sig_ctx(c, level, PQM_ALG_MLDSA87, partner, h, ctx, ctx_len);
        a = pqm_mldsa87_verify(pk->mldsa_pk, msg, msg_len, c, cl, sig->mldsa_sig);
    }
    /* Both halves are always evaluated; the result is their AND. */
    if (level == PQM_LEVEL_MATRIX) {
        cl = sig_ctx(c, level, PQM_ALG_SLH256S, PQM_ALG_MLDSA87, h, ctx, ctx_len);
        b = pqm_slh256s_verify(pk->slh_pk, msg, msg_len, c, cl, sig->slh_sig);
    }
    return a & b;
}
