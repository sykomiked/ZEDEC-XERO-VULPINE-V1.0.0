/* aead.c — ChaCha20-Poly1305 (RFC 8439). See aead.h. */
#include "aead.h"

/* ===================== ChaCha20 ===================== */

static uint32_t rotl32(uint32_t v, int n) {
    return (uint32_t)((v << n) | (v >> (32 - n)));
}
static uint32_t ld32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void st32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

#define QR(a,b,c,d) do {                       \
    a += b; d ^= a; d = rotl32(d, 16);         \
    c += d; b ^= c; b = rotl32(b, 12);         \
    a += b; d ^= a; d = rotl32(d, 8);          \
    c += d; b ^= c; b = rotl32(b, 7);          \
} while (0)

void chacha20_block(const uint8_t key[CHACHA20_KEY_LEN],
                    uint32_t counter,
                    const uint8_t nonce[CHACHA20_NONCE_LEN],
                    uint8_t out[CHACHA20_BLOCK]) {
    /* "expand 32-byte k" */
    uint32_t s[16];
    s[0] = 0x61707865u; s[1] = 0x3320646eu;
    s[2] = 0x79622d32u; s[3] = 0x6b206574u;
    for (int i = 0; i < 8; i++) s[4 + i] = ld32(key + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; i++) s[13 + i] = ld32(nonce + 4 * i);

    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = s[i];

    /* 20 rounds = 10 double rounds. Fixed count, no data dependence. */
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[ 8], x[12]);   /* columns */
        QR(x[1], x[5], x[ 9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);   /* diagonals */
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[ 8], x[13]);
        QR(x[3], x[4], x[ 9], x[14]);
    }
    /* Adding the original state is what makes the core one-way; omitting it
     * leaves an invertible permutation and no security at all. */
    for (int i = 0; i < 16; i++) st32(out + 4 * i, x[i] + s[i]);
}

void chacha20_xor(const uint8_t key[CHACHA20_KEY_LEN],
                  uint32_t counter,
                  const uint8_t nonce[CHACHA20_NONCE_LEN],
                  const uint8_t *in, uint8_t *out, uint32_t len) {
    uint8_t ks[CHACHA20_BLOCK];
    uint32_t done = 0;
    while (done < len) {
        chacha20_block(key, counter, nonce, ks);
        uint32_t take = len - done;
        if (take > CHACHA20_BLOCK) take = CHACHA20_BLOCK;
        for (uint32_t i = 0; i < take; i++) out[done + i] = in[done + i] ^ ks[i];
        done += take;
        counter++;
    }
}

/* ===================== Poly1305 ===================== */

/* The accumulator is 5 limbs of 26 bits, so a 130-bit value fits and the
 * partial products fit in uint64 before reduction. */
typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t  buf[16];
    uint32_t buf_len;
} poly_state_t;

static void poly_init(poly_state_t *st, const uint8_t key[32]) {
    /* Clamp r per RFC 8439: certain bits MUST be cleared or the modular
     * reduction below is not valid and the MAC is forgeable. */
    uint32_t t0 = ld32(key + 0), t1 = ld32(key + 4);
    uint32_t t2 = ld32(key + 8), t3 = ld32(key + 12);
    st->r[0] = ( t0                    ) & 0x3ffffffu;
    st->r[1] = ((t0 >> 26) | (t1 <<  6)) & 0x3ffff03u;
    st->r[2] = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ffu;
    st->r[3] = ((t2 >> 14) | (t3 << 18)) & 0x3f03fffu;
    st->r[4] = ( t3 >>  8              ) & 0x00fffffu;

    for (int i = 0; i < 5; i++) st->h[i] = 0;
    for (int i = 0; i < 4; i++) st->pad[i] = ld32(key + 16 + 4 * i);
    st->buf_len = 0;
}

/* Process one 16-byte block. `hibit` is 1<<24 for a full block and 0 for the
 * final partial block (which carries its own 0x01 terminator). */
static void poly_block(poly_state_t *st, const uint8_t m[16], uint32_t hibit) {
    uint32_t t0 = ld32(m + 0), t1 = ld32(m + 4);
    uint32_t t2 = ld32(m + 8), t3 = ld32(m + 12);

    st->h[0] +=  t0                     & 0x3ffffffu;
    st->h[1] += ((t0 >> 26) | (t1 <<  6)) & 0x3ffffffu;
    st->h[2] += ((t1 >> 20) | (t2 << 12)) & 0x3ffffffu;
    st->h[3] += ((t2 >> 14) | (t3 << 18)) & 0x3ffffffu;
    st->h[4] += ( t3 >>  8) | hibit;

    /* h = h * r mod (2^130 - 5); the 5x folding is the *5 terms below */
    uint64_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
    uint64_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint64_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];

    uint64_t d0 = h0*r0 + h1*s4 + h2*s3 + h3*s2 + h4*s1;
    uint64_t d1 = h0*r1 + h1*r0 + h2*s4 + h3*s3 + h4*s2;
    uint64_t d2 = h0*r2 + h1*r1 + h2*r0 + h3*s4 + h4*s3;
    uint64_t d3 = h0*r3 + h1*r2 + h2*r1 + h3*r0 + h4*s4;
    uint64_t d4 = h0*r4 + h1*r3 + h2*r2 + h3*r1 + h4*r0;

    uint64_t c;
    c = d0 >> 26; st->h[0] = (uint32_t)d0 & 0x3ffffffu; d1 += c;
    c = d1 >> 26; st->h[1] = (uint32_t)d1 & 0x3ffffffu; d2 += c;
    c = d2 >> 26; st->h[2] = (uint32_t)d2 & 0x3ffffffu; d3 += c;
    c = d3 >> 26; st->h[3] = (uint32_t)d3 & 0x3ffffffu; d4 += c;
    c = d4 >> 26; st->h[4] = (uint32_t)d4 & 0x3ffffffu;
    st->h[0] += (uint32_t)(c * 5);
    c = st->h[0] >> 26; st->h[0] &= 0x3ffffffu; st->h[1] += (uint32_t)c;
}

static void poly_finish(poly_state_t *st, uint8_t tag[16]) {
    if (st->buf_len) {
        /* RFC 8439: append a single 0x01 byte, then zero-pad. */
        st->buf[st->buf_len++] = 1;
        while (st->buf_len < 16) st->buf[st->buf_len++] = 0;
        poly_block(st, st->buf, 0);
    }

    /* carry propagate */
    uint32_t c;
    c = st->h[1] >> 26; st->h[1] &= 0x3ffffffu; st->h[2] += c;
    c = st->h[2] >> 26; st->h[2] &= 0x3ffffffu; st->h[3] += c;
    c = st->h[3] >> 26; st->h[3] &= 0x3ffffffu; st->h[4] += c;
    c = st->h[4] >> 26; st->h[4] &= 0x3ffffffu; st->h[0] += c * 5;
    c = st->h[0] >> 26; st->h[0] &= 0x3ffffffu; st->h[1] += c;

    /* compute h + -p, and select it if there was no borrow — branchlessly */
    uint32_t g[5];
    g[0] = st->h[0] + 5; c = g[0] >> 26; g[0] &= 0x3ffffffu;
    g[1] = st->h[1] + c; c = g[1] >> 26; g[1] &= 0x3ffffffu;
    g[2] = st->h[2] + c; c = g[2] >> 26; g[2] &= 0x3ffffffu;
    g[3] = st->h[3] + c; c = g[3] >> 26; g[3] &= 0x3ffffffu;
    g[4] = st->h[4] + c - (1u << 26);

    uint32_t mask = (g[4] >> 31) - 1;      /* all ones if g >= 0, i.e. h >= p */
    for (int i = 0; i < 5; i++) g[i] &= mask;
    mask = ~mask;
    for (int i = 0; i < 5; i++) st->h[i] = (st->h[i] & mask) | g[i];

    /* serialise the 130-bit accumulator as 128 bits */
    uint32_t f0 = ((st->h[0]      ) | (st->h[1] << 26)) & 0xffffffffu;
    uint32_t f1 = ((st->h[1] >>  6) | (st->h[2] << 20)) & 0xffffffffu;
    uint32_t f2 = ((st->h[2] >> 12) | (st->h[3] << 14)) & 0xffffffffu;
    uint32_t f3 = ((st->h[3] >> 18) | (st->h[4] <<  8)) & 0xffffffffu;

    uint64_t t = (uint64_t)f0 + st->pad[0]; f0 = (uint32_t)t;
    t = (uint64_t)f1 + st->pad[1] + (t >> 32); f1 = (uint32_t)t;
    t = (uint64_t)f2 + st->pad[2] + (t >> 32); f2 = (uint32_t)t;
    t = (uint64_t)f3 + st->pad[3] + (t >> 32); f3 = (uint32_t)t;

    st32(tag +  0, f0); st32(tag +  4, f1);
    st32(tag +  8, f2); st32(tag + 12, f3);
}

static void poly_update(poly_state_t *st, const uint8_t *m, uint32_t len) {
    while (len) {
        if (st->buf_len == 0 && len >= 16) {
            poly_block(st, m, 1u << 24);
            m += 16; len -= 16;
        } else {
            uint32_t take = 16 - st->buf_len;
            if (take > len) take = len;
            for (uint32_t i = 0; i < take; i++) st->buf[st->buf_len + i] = m[i];
            st->buf_len += take;
            m += take; len -= take;
            if (st->buf_len == 16) { poly_block(st, st->buf, 1u << 24); st->buf_len = 0; }
        }
    }
}

void poly1305(const uint8_t key[POLY1305_KEY_LEN],
              const uint8_t *msg, uint32_t len,
              uint8_t tag[POLY1305_TAG_LEN]) {
    poly_state_t st;
    poly_init(&st, key);
    poly_update(&st, msg, len);
    poly_finish(&st, tag);
}

/* ===================== the AEAD construction ===================== */

static void poly_pad16(poly_state_t *st, uint32_t written) {
    static const uint8_t z[16] = {0};
    uint32_t r = written % 16u;
    if (r) poly_update(st, z, 16u - r);
}

static void poly_len64(poly_state_t *st, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
    poly_update(st, b, 8);
}

/* The authenticated data is AAD || pad || ciphertext || pad || len(AAD) ||
 * len(C), each length a little-endian 64-bit value. The padding and the
 * explicit lengths are what stop an attacker moving bytes between the AAD and
 * the ciphertext without changing the tag. */
static void aead_tag(const uint8_t key[32], const uint8_t nonce[12],
                     const uint8_t *aad, uint32_t aad_len,
                     const uint8_t *ct, uint32_t ct_len,
                     uint8_t tag[16]) {
    uint8_t block0[CHACHA20_BLOCK];
    chacha20_block(key, 0, nonce, block0);   /* counter 0 -> the one-time key */

    poly_state_t st;
    poly_init(&st, block0);
    if (aad && aad_len) { poly_update(&st, aad, aad_len); poly_pad16(&st, aad_len); }
    if (ct && ct_len)   { poly_update(&st, ct, ct_len);   poly_pad16(&st, ct_len);  }
    poly_len64(&st, aad_len);
    poly_len64(&st, ct_len);
    poly_finish(&st, tag);

    for (uint32_t i = 0; i < CHACHA20_BLOCK; i++) block0[i] = 0;
}

void aead_seal(const uint8_t key[CHACHA20_KEY_LEN],
               const uint8_t nonce[CHACHA20_NONCE_LEN],
               const uint8_t *aad, uint32_t aad_len,
               const uint8_t *in, uint8_t *out, uint32_t len,
               uint8_t tag[POLY1305_TAG_LEN]) {
    /* Counter 1: block 0 is reserved for the Poly1305 key, and reusing it for
     * keystream would hand the MAC key to anyone with a known plaintext. */
    chacha20_xor(key, 1, nonce, in, out, len);
    aead_tag(key, nonce, aad, aad_len, out, len, tag);
}

bool aead_open(const uint8_t key[CHACHA20_KEY_LEN],
               const uint8_t nonce[CHACHA20_NONCE_LEN],
               const uint8_t *aad, uint32_t aad_len,
               const uint8_t *in, uint8_t *out, uint32_t len,
               const uint8_t tag[POLY1305_TAG_LEN]) {
    uint8_t want[POLY1305_TAG_LEN];
    aead_tag(key, nonce, aad, aad_len, in, len, want);

    /* Constant-time compare: no early exit, so nothing is learned about where
     * a forged tag first differs. */
    uint8_t diff = 0;
    for (uint32_t i = 0; i < POLY1305_TAG_LEN; i++) diff |= (uint8_t)(want[i] ^ tag[i]);
    if (diff != 0) {
        /* Wipe rather than return unauthenticated plaintext: a caller who
         * ignores the return value must not find usable data here. */
        for (uint32_t i = 0; i < len; i++) out[i] = 0;
        return false;
    }
    chacha20_xor(key, 1, nonce, in, out, len);
    return true;
}

void tls13_record_nonce(const uint8_t iv[CHACHA20_NONCE_LEN], uint64_t seq,
                        uint8_t out[CHACHA20_NONCE_LEN]) {
    /* RFC 8446 5.3: the 64-bit sequence number is left-padded with zeroes to
     * the IV length, then XORed with the static IV. Because the sequence
     * number never repeats within a connection, the nonce never repeats —
     * which is the property the whole cipher depends on. */
    for (uint32_t i = 0; i < CHACHA20_NONCE_LEN; i++) out[i] = iv[i];
    for (uint32_t i = 0; i < 8; i++)
        out[CHACHA20_NONCE_LEN - 1u - i] ^= (uint8_t)(seq >> (8 * i));
}
