/* event_envelope.c — Canonical event envelope implementation (ZXB encoding)
 *
 * Implements the architecture-neutral event envelope: identity, causal
 * ordering, payload hashing, and integrity verification via CRC32.
 *
 * The CRC32 is a simple freestanding implementation (no lookup table,
 * uses bit-by-bit computation) suitable for kernel use.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */
#include "event_space.h"

/* ===== Freestanding CRC32 (IEEE 802.3 polynomial, no lookup table) ===== */

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len) {
    /* IEEE 802.3 polynomial: 0xEDB88320 (reflected) */
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static uint32_t crc32_compute(const uint8_t *data, uint32_t len) {
    return crc32_update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

/* ===== Simple SHA-256 (freestanding, for payload hashing) ===== */
/* We use a minimal SHA-256 implementation for payload integrity.
 * This is the same approach as boot_evidence.c — accumulate and hash. */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    uint32_t buflen;
} sha256_ctx_t;

static const uint32_t SHA256_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
    0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
    0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
    0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
    0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
    0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define SHA256_ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x)  (SHA256_ROTR(x,2)  ^ SHA256_ROTR(x,13) ^ SHA256_ROTR(x,22))
#define SHA256_EP1(x)  (SHA256_ROTR(x,6)  ^ SHA256_ROTR(x,11) ^ SHA256_ROTR(x,25))
#define SHA256_SIG0(x) (SHA256_ROTR(x,7)  ^ SHA256_ROTR(x,18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x,17) ^ SHA256_ROTR(x,19) ^ ((x) >> 10))

static void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->bitlen = 0;
    ctx->buflen = 0;
}

static void sha256_block(sha256_ctx_t *ctx, const uint8_t *block) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = SHA256_SIG1(w[i-2]) + w[i-7] + SHA256_SIG0(w[i-15]) + w[i-16];
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e,f,g) + SHA256_K[i] + w[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    ctx->bitlen += (uint64_t)len * 8;
    while (len > 0) {
        uint32_t copy = 64 - ctx->buflen;
        if (copy > len) copy = len;
        ev_memcpy(ctx->buffer + ctx->buflen, data, copy);
        ctx->buflen += copy;
        data += copy;
        len -= copy;
        if (ctx->buflen == 64) {
            sha256_block(ctx, ctx->buffer);
            ctx->buflen = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t *out) {
    /* Pad: append 0x80, then zeros, then 64-bit big-endian length */
    ctx->buffer[ctx->buflen++] = 0x80;
    if (ctx->buflen > 56) {
        while (ctx->buflen < 64) ctx->buffer[ctx->buflen++] = 0;
        sha256_block(ctx, ctx->buffer);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 56) ctx->buffer[ctx->buflen++] = 0;

    uint64_t bitlen = ctx->bitlen;
    for (int i = 7; i >= 0; i--) {
        ctx->buffer[56 + i] = (uint8_t)(bitlen & 0xFF);
        bitlen >>= 8;
    }
    sha256_block(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        out[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        out[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        out[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

/* ===== Event Envelope API ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

void ev_envelope_init(ev_envelope_t *env, const ev_node_id_t *node,
                      uint64_t sequence, const char *schema,
                      uint16_t schema_version) {
    if (!env) return;
    ev_memset(env, 0, sizeof(*env));

    if (node) {
        env->event_id.node = *node;
    }
    env->event_id.local_sequence = sequence;

    if (schema) {
        copy_str(env->schema, schema, EV_SCHEMA_LEN);
    }
    env->schema_version = schema_version;

    env->delivery = EV_DELIVERY_BEST_EFFORT;
    env->priority = EV_PRIORITY_NORMAL;
    env->max_cost = 100;
    env->deadline_sequence = 0;
    env->idempotent = false;
    env->num_causal_parents = 0;
    env->payload_len = 0;
}

bool ev_envelope_add_causal_parent(ev_envelope_t *env, const ev_event_id_t *parent) {
    if (!env || !parent) return false;
    if (env->num_causal_parents >= EV_MAX_CAUSAL_PARENTS) return false;
    env->causal_parents[env->num_causal_parents++] = *parent;
    return true;
}

bool ev_envelope_set_payload(ev_envelope_t *env, const uint8_t *data, uint16_t len) {
    if (!env) return false;
    if (len > EV_PAYLOAD_MAX) return false;
    if (len > 0 && !data) return false;

    if (len > 0) {
        ev_memcpy(env->payload, data, len);
        /* Compute SHA-256 of payload */
        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, data, len);
        sha256_final(&ctx, env->payload_hash);
    } else {
        ev_memset(env->payload_hash, 0, sizeof(env->payload_hash));
    }
    env->payload_len = len;
    return true;
}

void ev_envelope_compute_crc(ev_envelope_t *env) {
    if (!env) return;
    /* Save old CRC, compute over everything except the CRC field itself */
    uint32_t saved_crc = env->header_crc;
    env->header_crc = 0;
    /* Compute CRC over the entire structure (CRC field is zeroed) */
    env->header_crc = crc32_compute((const uint8_t *)env, sizeof(*env));
    (void)saved_crc; /* not needed — we overwrite */
}

bool ev_envelope_verify_crc(const ev_envelope_t *env) {
    if (!env) return false;
    /* Recompute CRC with the field zeroed and compare */
    ev_envelope_t copy = *env;
    copy.header_crc = 0;
    uint32_t computed = crc32_compute((const uint8_t *)&copy, sizeof(copy));
    return computed == env->header_crc;
}

bool ev_envelope_validate(const ev_envelope_t *env) {
    if (!env) return false;

    /* Check CRC */
    if (!ev_envelope_verify_crc(env)) return false;

    /* Check schema is non-empty */
    if (env->schema[0] == '\0') return false;

    /* Check payload length */
    if (env->payload_len > EV_PAYLOAD_MAX) return false;

    /* Check causal parents count */
    if (env->num_causal_parents > EV_MAX_CAUSAL_PARENTS) return false;

    /* Check delivery is valid */
    if (env->delivery > EV_DELIVERY_TRANSACTIONAL) return false;

    /* Check priority is valid */
    if (env->priority > EV_PRIORITY_IDLE) return false;

    return true;
}
