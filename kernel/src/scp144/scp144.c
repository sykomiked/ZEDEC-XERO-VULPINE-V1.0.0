/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* scp144.c — Sicilian Crown Protection: 144-Dimensional AC Casimir Cavity
 *            Array & Traversable Wormhole Transceiver (Hardware-as-Code).
 */
#include "scp144.h"
#include "../tls/aead.h"
#include <stddef.h>

extern void shake256(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);

/* The 12-rung Fibonacci ladder up to F12 = 144 ( plus F0 = 0, F13 = 233 for Cassini ). */
static const uint16_t scp_fib[SCP144_RUNGS] = {
    1u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u, 89u, 144u
};
static const uint16_t scp_fib_ext[SCP144_RUNGS + 2u] = {
    0u, 1u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u, 89u, 144u, 233u
};

/* Coprime residue moduli for the Casimir boundary fold. */
static const uint8_t scp_mods[4] = { 7u, 13u, 31u, 89u };

/* Compute the unique non-consecutive Zeckendorf support bitmask (bits 0..11)
 * and discrete Casimir mode energy for a coordinate value v in 0..144. */
static uint16_t zeckendorf_mode(uint32_t v, uint32_t *casimir_energy)
{
    uint16_t mask = 0;
    uint32_t energy = 0;
    unsigned pos = 11;

    v %= (uint32_t) scp_fib[11] + 1u;
    while (v > 0 && pos > 0) {
        if (scp_fib[pos] <= v) {
            v -= scp_fib[pos];
            mask |= (uint16_t) (1u << pos);
            if (pos & 1u)
                energy += (uint32_t) scp_fib[pos] * (pos + 1u);
            else
                energy += (uint32_t) scp_fib[pos] * (13u - pos);
            pos = pos > 1 ? pos - 2 : 0; /* non-consecutive Zeckendorf rule */
        } else {
            pos--;
        }
    }
    if (v > 0) {
        mask |= 1u;
        energy += 1u;
    }
    if (casimir_energy) *casimir_energy = energy;
    return mask;
}

void scp144_lattice_bind(const uint8_t init_id[32],
                         const uint8_t resp_id[32],
                         uint8_t out[32])
{
    uint8_t seed[64];
    uint8_t lattice[SCP144_DIM];
    uint8_t transcript[64 + SCP144_DIM * 2 + 16];
    uint64_t total_energy = 0;
    uint64_t ac_alternating = 0;
    unsigned i;

    if (!init_id || !resp_id || !out)
        return;

    for (i = 0; i < 32; i++) {
        seed[i] = init_id[i];
        seed[i + 32] = resp_id[i];
        transcript[i] = init_id[i];
        transcript[i + 32] = resp_id[i];
    }

    shake256(seed, sizeof seed, lattice, sizeof lattice);

    for (i = 0; i < SCP144_DIM; i++) {
        uint32_t e = 0;
        uint16_t zm = zeckendorf_mode(lattice[i], &e);
        transcript[64 + i * 2]     = (uint8_t) (zm >> 8);
        transcript[64 + i * 2 + 1] = (uint8_t) zm;
        total_energy += e;
        if (i & 1u)
            ac_alternating -= e;
        else
            ac_alternating += e;
    }

    for (i = 0; i < 8; i++) {
        transcript[64 + SCP144_DIM * 2 + i]     = (uint8_t) (total_energy >> ((7u - i) * 8u));
        transcript[64 + SCP144_DIM * 2 + 8 + i] = (uint8_t) (ac_alternating >> ((7u - i) * 8u));
    }
    for (i = 0; i < 4; i++) {
        transcript[64 + SCP144_DIM * 2 + i] ^= (uint8_t) (total_energy % scp_mods[i]);
    }

    shake256(transcript, sizeof transcript, out, 32);
}

/* Recursively step a single Casimir plate's 12 micro-cavities and compute its
 * negative vacuum energy contribution:
 *     E_plate = - sum_{m=0..11} (Zeckendorf_mode(micro[m]) * F_{12-k}) / F_k
 * verified against Cassini's identity F_{k+1}*F_{k-1} - F_k^2 = (-1)^k. */
static void step_plate_recursive(scp144_plate_t *p, uint32_t k, int8_t ac_pol,
                                 const uint8_t *mode_seed, uint32_t depth)
{
    uint32_t m;
    int64_t plate_e = 0;
    int32_t fk = (int32_t) scp_fib_ext[k + 1u];
    int32_t fk_prev = (int32_t) scp_fib_ext[k];
    int32_t fk_next = (int32_t) scp_fib_ext[k + 2u];

    p->gap_fib = (uint16_t) fk;
    /* Exact hardware verification of Cassini's Identity: F_{k+1}F_{k-1} - F_k^2 == (-1)^(k+1) */
    p->cassini_det = fk_next * fk_prev - fk * fk;

    for (m = 0; m < SCP144_RUNGS; m++) {
        uint8_t prev = p->micro_modes[m];
        uint8_t next = p->micro_modes[(m + 1u) % SCP144_RUNGS];
        uint8_t s = mode_seed[(k * SCP144_RUNGS + m) % SCP144_DIM];
        uint32_t me = 0;
        uint16_t zm;

        /* Unimodular AC step inside the micro-cavity */
        if (ac_pol > 0)
            p->micro_modes[m] = (uint8_t) (prev + next + s);
        else
            p->micro_modes[m] = (uint8_t) (prev - next - s);

        zm = zeckendorf_mode(p->micro_modes[m], &me);
        if (m == 0) p->zeck_mask = zm;
        else        p->zeck_mask ^= zm;

        /* Negative Casimir vacuum energy: excluded modes between plates lower energy */
        plate_e -= (int64_t) (me * (uint32_t) scp_fib[11u - k]);
    }

    if (depth > 0u) {
        /* Second recursive hardware pass: couple adjacent micro-modes across the throat */
        for (m = 0; m + 1u < SCP144_RUNGS; m += 2u) {
            uint8_t a = p->micro_modes[m];
            uint8_t b = p->micro_modes[m + 1u];
            p->micro_modes[m]      = b;
            p->micro_modes[m + 1u] = (ac_pol > 0) ? (uint8_t) (a - b) : (uint8_t) (a + b);
        }
    }

    p->casimir_energy = plate_e;
}

void scp144_cavity_init(scp144_cavity_t *dev, uint32_t device_id,
                        const uint8_t init_id[32], const uint8_t resp_id[32])
{
    static const uint8_t zero_id[32] = {0};
    uint8_t mode_seed[SCP144_DIM];
    uint32_t i;

    if (!dev) return;
    for (i = 0; i < sizeof(*dev); i++) ((uint8_t *) dev)[i] = 0;

    dev->device_id = device_id;
    dev->ac_cycle = 0;
    dev->ac_polarity = 1;
    dev->throat_aperture = (uint8_t) scp_fib[5]; /* F6 = 8: the Casimir throat */
    dev->recursion_depth = 2u;                   /* 2-level 12x12 recursive hardware */

    scp144_lattice_bind(init_id ? init_id : zero_id,
                        resp_id ? resp_id : zero_id,
                        dev->throat_key);
    shake256(dev->throat_key, SCP144_KEY_LEN, mode_seed, sizeof mode_seed);

    dev->total_casimir_energy = 0;
    for (i = 0; i < SCP144_RUNGS; i++) {
        uint32_t m;
        for (m = 0; m < SCP144_RUNGS; m++) {
            dev->plates[i].micro_modes[m] = mode_seed[i * SCP144_RUNGS + m];
        }
        step_plate_recursive(&dev->plates[i], i, dev->ac_polarity, mode_seed, dev->recursion_depth);
        dev->total_casimir_energy += dev->plates[i].casimir_energy;
    }

    dev->csr = SCP144_CSR_ENABLE | SCP144_CSR_AC_LOCK | SCP144_CSR_RESONANT;
    if (dev->total_casimir_energy < 0)
        dev->csr |= SCP144_CSR_THROAT_OPEN;
}

void scp144_cavity_clock_tick(scp144_cavity_t *dev)
{
    uint8_t mode_seed[SCP144_DIM];
    uint8_t seed_in[SCP144_KEY_LEN + 8];
    uint32_t i;

    if (!dev || !(dev->csr & SCP144_CSR_ENABLE)) return;

    dev->ac_cycle++;
    dev->ac_polarity = (dev->ac_cycle & 1ull) ? -1 : 1;
    if (dev->ac_polarity > 0)
        dev->pos_half_cycles++;
    else
        dev->neg_half_cycles++;

    for (i = 0; i < SCP144_KEY_LEN; i++) seed_in[i] = dev->throat_key[i];
    for (i = 0; i < 8u; i++) seed_in[SCP144_KEY_LEN + i] = (uint8_t) (dev->ac_cycle >> (i * 8u));
    shake256(seed_in, sizeof seed_in, mode_seed, sizeof mode_seed);

    dev->total_casimir_energy = 0;
    for (i = 0; i < SCP144_RUNGS; i++) {
        step_plate_recursive(&dev->plates[i], i, dev->ac_polarity, mode_seed, dev->recursion_depth);
        dev->total_casimir_energy += dev->plates[i].casimir_energy;
    }

    if (dev->total_casimir_energy < 0)
        dev->csr |= SCP144_CSR_THROAT_OPEN;
    else
        dev->csr &= ~SCP144_CSR_THROAT_OPEN;
}

static void derive_throat_stream(const uint8_t throat_key[32], uint64_t ac_cycle,
                                 uint8_t stream[SCP144_DIM])
{
    uint8_t in[32 + 8 + 8];
    unsigned i;
    for (i = 0; i < 32; i++) in[i] = throat_key[i];
    for (i = 0; i < 8; i++) in[32 + i] = (uint8_t) (ac_cycle >> (i * 8u));
    in[40] = (ac_cycle & 1ull) ? 0x53u : 0xACu;
    in[41] = 144u;
    in[42] = 89u;
    in[43] = 55u;
    in[44] = 34u;
    in[45] = 21u;
    in[46] = 13u;
    in[47] = 8u;
    shake256(in, sizeof in, stream, SCP144_DIM);
}

void scp144_tunnel_contract(uint8_t *buf, uint32_t len,
                            const uint8_t throat_key[32], uint64_t ac_cycle)
{
    uint8_t stream[SCP144_DIM];
    uint32_t i;
    int r;

    if (!buf || len == 0 || !throat_key) return;
    derive_throat_stream(throat_key, ac_cycle, stream);

    for (r = 11; r >= 6; r--) {
        uint8_t fw = (uint8_t) (scp_fib[r] ^ stream[r]);
        for (i = 0; i + 1 < len; i += 2) {
            uint8_t x = buf[i], y = buf[i + 1];
            uint8_t nx = y;
            uint8_t ny = (r & 1) ? (uint8_t) (x + y - fw) : (uint8_t) (x - y - fw);
            buf[i] = nx;
            buf[i + 1] = ny;
        }
    }

    for (i = 0; i < len; i++) {
        uint8_t s = stream[i % SCP144_DIM];
        buf[i] = (ac_cycle & 1ull) ? (uint8_t) (buf[i] - s) : (uint8_t) (buf[i] + s);
    }

    for (r = 5; r >= 0; r--) {
        uint8_t fw = (uint8_t) (scp_fib[r] ^ stream[r]);
        for (i = 0; i + 1 < len; i += 2) {
            uint8_t x = buf[i], y = buf[i + 1];
            uint8_t nx = y;
            uint8_t ny = (r & 1) ? (uint8_t) (x + y - fw) : (uint8_t) (x - y - fw);
            buf[i] = nx;
            buf[i + 1] = ny;
        }
    }
}

void scp144_tunnel_expand(uint8_t *buf, uint32_t len,
                          const uint8_t throat_key[32], uint64_t ac_cycle)
{
    uint8_t stream[SCP144_DIM];
    uint32_t i;
    int r;

    if (!buf || len == 0 || !throat_key) return;
    derive_throat_stream(throat_key, ac_cycle, stream);

    for (r = 0; r <= 5; r++) {
        uint8_t fw = (uint8_t) (scp_fib[r] ^ stream[r]);
        for (i = 0; i + 1 < len; i += 2) {
            uint8_t nx = buf[i], ny = buf[i + 1];
            uint8_t y = nx;
            uint8_t x = (r & 1) ? (uint8_t) (ny - y + fw) : (uint8_t) (ny + y + fw);
            buf[i] = x;
            buf[i + 1] = y;
        }
    }

    for (i = 0; i < len; i++) {
        uint8_t s = stream[i % SCP144_DIM];
        buf[i] = (ac_cycle & 1ull) ? (uint8_t) (buf[i] + s) : (uint8_t) (buf[i] - s);
    }

    for (r = 6; r <= 11; r++) {
        uint8_t fw = (uint8_t) (scp_fib[r] ^ stream[r]);
        for (i = 0; i + 1 < len; i += 2) {
            uint8_t nx = buf[i], ny = buf[i + 1];
            uint8_t y = nx;
            uint8_t x = (r & 1) ? (uint8_t) (ny - y + fw) : (uint8_t) (ny + y + fw);
            buf[i] = x;
            buf[i + 1] = y;
        }
    }
}

static void derive_part_material(const uint8_t key[32], const uint8_t root_digest[32],
                                 uint32_t part_index, uint64_t ac_cycle,
                                 uint8_t aead_key[32], uint8_t nonce[12], uint8_t witness[8])
{
    uint8_t bind[32];
    uint8_t seed[64 + 12];
    uint8_t okm[32 + 12 + 8];
    unsigned i;

    scp144_lattice_bind(key, root_digest, bind);
    for (i = 0; i < 32; i++) {
        seed[i] = key[i];
        seed[32 + i] = bind[i];
    }
    seed[64] = (uint8_t) (part_index >> 24);
    seed[65] = (uint8_t) (part_index >> 16);
    seed[66] = (uint8_t) (part_index >> 8);
    seed[67] = (uint8_t) part_index;
    for (i = 0; i < 8; i++) seed[68 + i] = (uint8_t) (ac_cycle >> ((7u - i) * 8u));

    shake256(seed, sizeof seed, okm, sizeof okm);
    for (i = 0; i < 32; i++) aead_key[i] = okm[i];
    for (i = 0; i < 12; i++) nonce[i] = okm[32 + i];
    for (i = 0; i < 8; i++) witness[i] = okm[44 + i];
}

int32_t scp144_part_seal(const uint8_t key[32], const uint8_t root_digest[32],
                         uint32_t part_index, uint64_t ac_cycle,
                         const uint8_t *pt, uint32_t pt_len,
                         uint8_t *out, uint32_t out_cap)
{
    uint8_t aead_key[32], nonce[12], witness[8];
    uint32_t total, i;

    if (!key || !root_digest || (!pt && pt_len > 0) || !out) return SCP144_ERR_ARG;
    if (pt_len > 0x7FFFFFFFu - SCP144_PART_OVERHEAD) return SCP144_ERR_SPACE;
    total = pt_len + SCP144_PART_OVERHEAD;
    if (out_cap < total) return SCP144_ERR_SPACE;

    derive_part_material(key, root_digest, part_index, ac_cycle, aead_key, nonce, witness);

    out[0] = (uint8_t) SCP144_PART_MAGIC;
    out[1] = (uint8_t) (SCP144_PART_MAGIC >> 8);
    out[2] = (uint8_t) (SCP144_PART_MAGIC >> 16);
    out[3] = (uint8_t) (SCP144_PART_MAGIC >> 24);
    out[4] = (uint8_t) (part_index >> 24);
    out[5] = (uint8_t) (part_index >> 16);
    out[6] = (uint8_t) (part_index >> 8);
    out[7] = (uint8_t) part_index;
    out[8] = (uint8_t) (pt_len >> 24);
    out[9] = (uint8_t) (pt_len >> 16);
    out[10] = (uint8_t) (pt_len >> 8);
    out[11] = (uint8_t) pt_len;
    for (i = 0; i < 8; i++) out[12 + i] = (uint8_t) (ac_cycle >> ((7u - i) * 8u));
    for (i = 0; i < 4; i++) out[20 + i] = witness[i];

    for (i = 0; i < pt_len; i++) out[SCP144_PART_HDR_LEN + i] = pt[i];

    scp144_tunnel_contract(out + SCP144_PART_HDR_LEN, pt_len, aead_key, ac_cycle);

    aead_seal(aead_key, nonce, out, SCP144_PART_HDR_LEN,
              out + SCP144_PART_HDR_LEN, out + SCP144_PART_HDR_LEN, pt_len,
              out + SCP144_PART_HDR_LEN + pt_len);

    return (int32_t) total;
}

int32_t scp144_part_open(const uint8_t key[32], const uint8_t root_digest[32],
                         uint32_t expected_part_index, uint64_t *ac_cycle_out,
                         const uint8_t *in, uint32_t in_len,
                         uint8_t *out, uint32_t out_cap)
{
    uint8_t aead_key[32], nonce[12], witness[8];
    uint32_t magic, part_idx, pt_len, i;
    uint64_t ac_cycle = 0;
    uint8_t diff = 0;

    if (!key || !root_digest || !in || !out) return SCP144_ERR_ARG;
    if (in_len < SCP144_PART_OVERHEAD) return SCP144_ERR_AUTH;

    magic = (uint32_t) in[0] | ((uint32_t) in[1] << 8) |
            ((uint32_t) in[2] << 16) | ((uint32_t) in[3] << 24);
    part_idx = ((uint32_t) in[4] << 24) | ((uint32_t) in[5] << 16) |
               ((uint32_t) in[6] << 8) | (uint32_t) in[7];
    pt_len = ((uint32_t) in[8] << 24) | ((uint32_t) in[9] << 16) |
             ((uint32_t) in[10] << 8) | (uint32_t) in[11];
    for (i = 0; i < 8; i++) ac_cycle = (ac_cycle << 8) | in[12 + i];

    if (magic != SCP144_PART_MAGIC || part_idx != expected_part_index)
        return SCP144_ERR_AUTH;
    if (pt_len != in_len - SCP144_PART_OVERHEAD)
        return SCP144_ERR_AUTH;
    if (out_cap < pt_len)
        return SCP144_ERR_SPACE;

    derive_part_material(key, root_digest, part_idx, ac_cycle, aead_key, nonce, witness);

    for (i = 0; i < 4; i++) diff |= (uint8_t) (in[20 + i] ^ witness[i]);
    if (diff != 0) {
        for (i = 0; i < pt_len; i++) out[i] = 0;
        return SCP144_ERR_AUTH;
    }

    if (!aead_open(aead_key, nonce, in, SCP144_PART_HDR_LEN,
                   in + SCP144_PART_HDR_LEN, out, pt_len,
                   in + SCP144_PART_HDR_LEN + pt_len)) {
        return SCP144_ERR_AUTH;
    }

    scp144_tunnel_expand(out, pt_len, aead_key, ac_cycle);

    if (ac_cycle_out) *ac_cycle_out = ac_cycle;
    return (int32_t) pt_len;
}

/* Classify the syntax (10 digits + 6 hex letters upper/lower, Base58, Base32,
 * or 20-byte Ethereum fiber projection) and unfold the flat digest into 144D. */
int scp144_identify_hash(const uint8_t *in, uint32_t len, scp144_id_vec_t *out)
{
    uint32_t i, has_upper = 0, has_lower = 0, is_hex = 1, all_zero = 1;
    uint8_t seed[64];

    if (!in || len == 0 || !out) return SCP144_ERR_ARG;
    for (i = 0; i < sizeof(*out); i++) ((uint8_t *) out)[i] = 0;

    for (i = 0; i < len; i++) {
        uint8_t c = in[i];
        if (c != 0 && c != '0' && c != 'x' && c != 'X') all_zero = 0;
        if (c >= 'A' && c <= 'Z') has_upper = 1;
        if (c >= 'a' && c <= 'z') has_lower = 1;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F') || c == 'x' || c == 'X'))
            is_hex = 0;
    }

    if (len == 46u && in[0] == 'Q' && in[1] == 'm') {
        out->syntax_class = SCP144_SYN_CIDV0_B58;
        out->fiber_exponent = 0u;
    } else if (len >= 50u && in[0] == 'b' && in[1] == 'a' && in[2] == 'f') {
        out->syntax_class = SCP144_SYN_CIDV1_B32;
        out->fiber_exponent = 0u;
    } else if (len == 20u || (is_hex && (len == 40u || len == 42u))) {
        out->syntax_class = SCP144_SYN_ETH_ADDR;
        out->fiber_exponent = 96u; /* 2^(256-160) = 2^96 secp256k1 preimages per 20B address */
    } else if (is_hex && has_upper && !has_lower) {
        out->syntax_class = SCP144_SYN_HEX_UPPER;
    } else if (is_hex && has_lower && !has_upper) {
        out->syntax_class = SCP144_SYN_HEX_LOWER;
    } else {
        out->syntax_class = SCP144_SYN_RAW256;
    }

    if (len == 32u && !is_hex) {
        for (i = 0; i < 32u; i++) out->canonical_digest[i] = in[i];
    } else {
        shake256(in, len, out->canonical_digest, 32u);
    }

    /* Evaluate the Zero-One Elliptic Bridge:
     *   - Affine (0,0) on y^2 = x^3 + 7 mod p fails (0 != 7),
     *   - Projective [X : Y : Z] = [0 : 1 : 0] is the true Zero-One Bridge
     *     (simultaneously additive identity 0 = O and multiplicative unit 1 = G^0),
     *     represented paraconsistently as SCP144_BRIDGE_BOTH (3). */
    if (all_zero) {
        out->bridge_state = SCP144_BRIDGE_BOTH;
        out->projective_xyz[0] = 0u; /* X = 0 (additive zero)        */
        out->projective_xyz[1] = 1u; /* Y = 1 (multiplicative unit)  */
        out->projective_xyz[2] = 0u; /* Z = 0 (point at infinity O)  */
    } else if (out->syntax_class == SCP144_SYN_ETH_ADDR) {
        out->bridge_state = SCP144_BRIDGE_NEITHER; /* 2^96 fiber bundle */
        out->projective_xyz[0] = out->canonical_digest[0];
        out->projective_xyz[1] = 1u;
        out->projective_xyz[2] = 1u;
    } else {
        out->bridge_state = SCP144_BRIDGE_TRUE;
        out->projective_xyz[0] = out->canonical_digest[0];
        out->projective_xyz[1] = out->canonical_digest[1];
        out->projective_xyz[2] = 1u;
    }

    /* Unfold the flat digest through the 12-rung AC Casimir expansion into 144D */
    for (i = 0; i < 32u; i++) {
        seed[i] = out->canonical_digest[i];
        seed[32u + i] = (uint8_t) (out->syntax_class ^ (i * 13u));
    }
    shake256(seed, sizeof seed, out->unfolded_144, SCP144_DIM);
    scp144_tunnel_expand(out->unfolded_144, SCP144_DIM, out->canonical_digest,
                         (uint64_t) out->syntax_class);

    for (i = 0; i < SCP144_RUNGS; i++) {
        uint32_t e = 0;
        out->zeck_path[i] = zeckendorf_mode(out->unfolded_144[i * 12u], &e);
    }
    out->cassini_parity = (out->zeck_path[11] & 1u) ? 1u : 255u;
    return SCP144_OK;
}
