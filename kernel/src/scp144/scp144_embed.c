/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* scp144_embed.c — the SCP-144 boot self-check: the embedded 144-dimensional
 * Zeckendorf-Casimir lattice ROM (zxv_scp144_blob.h) and the improved SCP-144
 * standard are verified before [BOOT_OK] on every architecture, like the AI
 * self-check beside them. The checks:
 *
 *   1. the Fibonacci ladder and its Cassini determinants recomputed from the
 *      ladder equal the ROM;
 *   2. scp144_zeckendorf_mode agrees with the ROM for every coordinate 0..144;
 *   3. the 144x144 fold matrix recomputed by the kernel's own formula equals
 *      the ROM;
 *   4. the SHA-256 digest of the ROM's serialization equals the ROM's digest;
 *   5. the improved SCP-144 live checks: Cassini's identity on every rung, the
 *      bijective AC tunnel contract->expand round trip, the lattice binding's
 *      determinism, and the cavity clock's invariants;
 *   6. the fractal seed chain (zxv_seed_blob.h) — the seed digest commits the
 *      release root, the curriculum, the lattice ROM and the previous seed;
 *   7. the stage-0 unfold (zxv_grow.c): the seed is grown ring by ring into
 *      a bounded arena — each rung emitting its Cassini determinant, the new
 *      Zeckendorf modes and the matrix shell the dimension jump adds — and
 *      the transcript must land on the committed zxv_seed_grow_root, the
 *      plant this release was built from.
 *
 * Every check must pass; a failure prints one [FAULT] line and the caller
 * halts before [BOOT_OK]. Freestanding: no libc, no allocation, no floats. */
#include "scp144_embed.h"
#include "scp144.h"
#include "zxv_grow.h"
#include "zxv_scp144_blob.h"
#include "zxv_seed_blob.h"
#include "zxv_lattice_route.h"
#include "zxv_route.h"
#include "../robin_debanks/sha256.h"
#include "../robin_debanks/ed25519_verify.h"
#include "../e8/fib_lattice.h"
#include <stdint.h>
#include <stddef.h>

static int fail_step;
static uint16_t fib[SCP144_BLOB_FIB_N];
static uint16_t fib_ext[SCP144_BLOB_FIB_N + 2u];

static void copy_bytes(uint8_t *d, const void *s, size_t n)
{
    const uint8_t *p = (const uint8_t *) s;
    for (size_t i = 0; i < n; i++) d[i] = p[i];
}

/* The 1-based ladder F1..F12 and F0..F13, recomputed (never taken from the
 * ROM: the comparison is the point). */
static void recompute_ladder(void)
{
    uint32_t i;
    fib[0] = 1u;
    fib[1] = 1u;
    for (i = 2; i < SCP144_BLOB_FIB_N; i++) fib[i] = (uint16_t) (fib[i - 1] + fib[i - 2]);
    fib_ext[0] = 0u;
    fib_ext[1] = 1u;
    for (i = 2; i < SCP144_BLOB_FIB_N + 2u; i++)
        fib_ext[i] = (uint16_t) (fib_ext[i - 1] + fib_ext[i - 2]);
}

static int check_ladder(void)
{
    int32_t k;
    recompute_ladder();
    for (uint32_t i = 0; i < SCP144_BLOB_FIB_N; i++)
        if (fib[i] != scp144_blob_fib[i]) return 11;
    for (uint32_t i = 0; i < SCP144_BLOB_FIB_N + 2u; i++)
        if (fib_ext[i] != scp144_blob_fib_ext[i]) return 12;
    for (k = 0; k < 12; k++) { /* F_{k+2}*F_k - F_{k+1}^2 = +/-1 */
        int32_t det = (int32_t) fib_ext[k + 2] * (int32_t) fib_ext[k] -
                      (int32_t) fib_ext[k + 1] * (int32_t) fib_ext[k + 1];
        if (det != scp144_blob_cassini[k]) return 13;
        if (det != 1 && det != -1) return 14;
    }
    return 0;
}

static int check_zeckendorf(void)
{
    for (uint32_t v = 0; v < SCP144_BLOB_ZECK_N; v++) {
        uint32_t e = 0;
        uint16_t m = scp144_zeckendorf_mode(v, &e);
        if (m != scp144_blob_zeck_mask[v] || e != scp144_blob_zeck_energy[v]) return 21;
    }
    return 0;
}

static int check_matrix(void)
{
    for (uint32_t c = 0; c < SCP144_BLOB_DIM; c++) {
        int64_t gap_hi = scp144_blob_fib[11u - (c % 12u)];
        int64_t gap_lo = scp144_blob_fib[c % 12u];
        int64_t parity = (c & 1u) ? -1 : 1;
        for (uint32_t j = 0; j < SCP144_BLOB_DIM; j++) {
            uint32_t d = (c + j) % SCP144_BLOB_ZECK_N;
            int64_t got = parity * (int64_t) scp144_blob_zeck_energy[d] * gap_hi +
                          (int64_t) scp144_blob_zeck_mask[d] * gap_lo;
            if (got != scp144_blob_matrix[c][j]) return 31;
        }
    }
    return 0;
}

static int check_digest(void)
{
    sha256_ctx_t c;
    uint8_t got[32];
    /* the little-endian serialization, hashed incrementally: the arrays are
     * exactly the generator's struct.pack('<...') bytes on every target */
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) scp144_blob_fib, sizeof scp144_blob_fib);
    sha256_update(&c, (const uint8_t *) scp144_blob_fib_ext, sizeof scp144_blob_fib_ext);
    sha256_update(&c, (const uint8_t *) scp144_blob_cassini, sizeof scp144_blob_cassini);
    sha256_update(&c, (const uint8_t *) scp144_blob_zeck_mask, sizeof scp144_blob_zeck_mask);
    sha256_update(&c, (const uint8_t *) scp144_blob_zeck_energy, sizeof scp144_blob_zeck_energy);
    sha256_update(&c, (const uint8_t *) scp144_blob_matrix, sizeof scp144_blob_matrix);
    sha256_final(&c, got);
    for (uint32_t i = 0; i < 32; i++)
        if (got[i] != scp144_blob_digest[i]) return 41;
    return 0;
}

static int bytes_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* The improved SCP-144 standard, live: Cassini on every rung, the bijective
 * AC tunnel round trip, the lattice binding's determinism, the cavity clock. */
static int check_live(void)
{
    static const uint8_t id_a[32] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    };
    static const uint8_t id_b[32] = {
        0xf1, 0xe2, 0xd3, 0xc4, 0xb5, 0xa6, 0x97, 0x88, 0x79, 0x6a, 0x5b, 0x4c, 0x3d, 0x2e, 0x1f, 0x10,
        0xef, 0xde, 0xcd, 0xbc, 0xab, 0x9a, 0x89, 0x78, 0x67, 0x56, 0x45, 0x34, 0x23, 0x12, 0x01, 0x00,
    };
    uint8_t k1[32], k2[32], k3[32], t[192], orig[192];
    scp144_cavity_t dev;
    uint32_t i;

    scp144_lattice_bind(id_a, id_b, k1);
    scp144_lattice_bind(id_a, id_b, k2);
    if (!bytes_eq(k1, k2, 32)) return 51;
    {
        static const uint8_t zero[32] = {0};
        if (bytes_eq(k1, zero, 32)) return 52;
    }
    scp144_lattice_bind(id_b, id_a, k3);
    if (bytes_eq(k1, k3, 32)) return 53; /* order matters: a != b */

    for (i = 0; i < sizeof orig; i++) orig[i] = (uint8_t) (i * 7u + 3u);
    for (uint64_t cyc = 0; cyc < 3; cyc++) {
        copy_bytes(t, orig, sizeof t);
        scp144_tunnel_contract(t, sizeof t, k1, cyc);
        scp144_tunnel_expand(t, sizeof t, k1, cyc);
        if (!bytes_eq(t, orig, sizeof t)) return 54;
    }

    scp144_cavity_init(&dev, 0x144u, id_a, id_b);
    if (!(dev.csr & SCP144_CSR_ENABLE)) return 55;
    for (i = 0; i < SCP144_RUNGS; i++)
        if (dev.plates[i].cassini_det != 1 && dev.plates[i].cassini_det != -1) return 56;
    for (i = 0; i < 3; i++) {
        scp144_cavity_clock_tick(&dev);
        if (!(dev.csr & SCP144_CSR_ENABLE)) return 57;
        for (uint32_t k = 0; k < SCP144_RUNGS; k++)
            if (dev.plates[k].cassini_det != 1 && dev.plates[k].cassini_det != -1) return 58;
    }
    if (dev.pos_half_cycles == 0 || dev.neg_half_cycles == 0) return 59;
    return 0;
}

/* The fractal seed: the seed digest must commit the release root, the
 * curriculum, the lattice ROM and the previous generation's seed. */
static int check_seed(void)
{
    sha256_ctx_t c;
    uint8_t got[32];
    if (!bytes_eq(zxv_seed_lattice_digest, scp144_blob_digest, 32)) return 61;
    sha256_init(&c);
    sha256_update(&c, zxv_seed_upstream_cid, ZXV_SEED_UPSTREAM_LEN);
    sha256_update(&c, zxv_seed_cur_digest, 32);
    sha256_update(&c, zxv_seed_lattice_digest, 32);
    sha256_update(&c, zxv_seed_prev_digest, 32);
    sha256_final(&c, got);
    if (!bytes_eq(got, zxv_seed_digest, 32)) return 62;
    /* the root seal (the rollback invariant): when a signature is present,
     * the seed digest must be signed by the owner's Ed25519 key, so no
     * recursive derivation of the seed can be tainted by an unauthorized
     * write. An all-zero signature is an explicitly unsigned seed. */
    {
        uint32_t nonzero = 0;
        for (uint32_t i = 0; i < 64; i++)
            if (zxv_seed_sig[i]) {
                nonzero = 1;
                break;
            }
        if (nonzero && !ed25519_verify(zxv_seed_digest, 32, zxv_seed_sig, zxv_seed_owner_pub))
            return 63;
    }
    return 0;
}

/* The holographic healing self-audit: re-project the canonical SCP-144
 * tables (the Fibonacci ladder, the Cassini determinants and the Zeckendorf
 * masks and energies) into `shadow` as their little-endian serialization,
 * and report how many bytes differ from the .rodata ROM. Zero means the
 * ROM is intact; otherwise the caller has a clean re-derived shadow that
 * can regenerate the 144x144 matrix (healing from the seed's invariants).
 * Returns UINT32_MAX when the buffer is too small. */
uint32_t scp144_heal_audit(uint8_t *shadow, uint32_t cap)
{
    uint32_t need = sizeof scp144_blob_fib + sizeof scp144_blob_fib_ext +
                    sizeof scp144_blob_cassini + sizeof scp144_blob_zeck_mask +
                    sizeof scp144_blob_zeck_energy;
    uint32_t o = 0, bad = 0;
    const uint8_t *rf = (const uint8_t *) scp144_blob_fib;
    const uint8_t *rfe = (const uint8_t *) scp144_blob_fib_ext;
    const uint8_t *rc = (const uint8_t *) scp144_blob_cassini;
    const uint8_t *rm = (const uint8_t *) scp144_blob_zeck_mask;
    const uint8_t *re = (const uint8_t *) scp144_blob_zeck_energy;
    if (!shadow || cap < need) return UINT32_MAX;
    recompute_ladder();
    for (uint32_t i = 0; i < SCP144_BLOB_FIB_N; i++) {
        shadow[o] = (uint8_t) (fib[i] & 0xFFu);
        shadow[o + 1u] = (uint8_t) (fib[i] >> 8);
        bad += (shadow[o] != rf[2u * i]) + (shadow[o + 1u] != rf[2u * i + 1u]);
        o += 2u;
    }
    for (uint32_t i = 0; i < SCP144_BLOB_FIB_N + 2u; i++) {
        shadow[o] = (uint8_t) (fib_ext[i] & 0xFFu);
        shadow[o + 1u] = (uint8_t) (fib_ext[i] >> 8);
        bad += (shadow[o] != rfe[2u * i]) + (shadow[o + 1u] != rfe[2u * i + 1u]);
        o += 2u;
    }
    for (uint32_t k = 0; k < 12u; k++) {
        int32_t det = (int32_t) fib_ext[k + 2u] * (int32_t) fib_ext[k] -
                      (int32_t) fib_ext[k + 1u] * (int32_t) fib_ext[k + 1u];
        uint32_t u = (uint32_t) det;
        for (uint32_t b = 0; b < 4u; b++) {
            shadow[o + b] = (uint8_t) (u >> (b * 8u));
            bad += shadow[o + b] != rc[4u * k + b];
        }
        o += 4u;
    }
    for (uint32_t v = 0; v < SCP144_BLOB_ZECK_N; v++) {
        uint32_t e = 0;
        uint16_t m = scp144_zeckendorf_mode(v, &e);
        shadow[o] = (uint8_t) (m & 0xFFu);
        shadow[o + 1u] = (uint8_t) (m >> 8);
        bad += (shadow[o] != rm[2u * v]) + (shadow[o + 1u] != rm[2u * v + 1u]);
        o += 2u;
        for (uint32_t b = 0; b < 4u; b++) {
            shadow[o + b] = (uint8_t) (e >> (b * 8u));
            bad += shadow[o + b] != re[4u * v + b];
        }
        o += 4u;
    }
    return bad;
}

/* The stage-0 unfold: grow the seed's twelve rings into a bounded static
 * arena (the whole plant is 166,824 bytes of canonical geometry) and land
 * on the committed grow root — the ELF proves it carries the rules, not
 * just the fruit. rom_diff != 0 means the ROM rotted but the plant still
 * grew true (the heal audit reports the same rot). */
static int check_grow(void)
{
    static uint8_t arena[ZXV_SEED_GROW_BYTES];
    zxv_grow_t st;
    int rings;
    if (zxv_grow_bytes(ZXV_GROW_RUNGS) != ZXV_SEED_GROW_BYTES) return 81;
    rings = zxv_grow(&st, arena, sizeof arena, ZXV_GROW_RUNGS);
    if (rings != (int) ZXV_GROW_RUNGS || st.fault != ZXV_GROW_OK) return 82;
    if (st.written != ZXV_SEED_GROW_BYTES) return 83;
    if (st.rom_diff != 0) return 84;
    if (!bytes_eq(st.root, zxv_seed_grow_root, 32)) return 85;
    return 0;
}

static char *put_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *put_hex8(char *p, const uint8_t *d)
{
    static const char hx[] = "0123456789abcdef";
    for (int k = 0; k < 8; k++) {
        *p++ = hx[d[k] >> 4];
        *p++ = hx[d[k] & 0x0Fu];
    }
    return p;
}

bool scp144_boot_selfcheck(void (*out)(const char *))
{
    static char line[96];
    char *p;

    fail_step = 0;
    if ((fail_step = check_ladder()) != 0) goto fault;
    if ((fail_step = check_zeckendorf()) != 0) goto fault;
    if ((fail_step = check_matrix()) != 0) goto fault;
    if ((fail_step = check_digest()) != 0) goto fault;
    if ((fail_step = check_live()) != 0) goto fault;
    if ((fail_step = check_seed()) != 0) goto fault;
    /* 7. the Fibonacci lattice ladder (fib_lattice.c): the two Type-II
     * anchors F6 = 8 and F12 = 144, the 144 harmonic and the enumerated
     * kissing numbers. The digest line then covers both ROMs. */
    {
        int fl = fib_lattice_selfcheck();
        if (fl != 0) {
            fail_step = 70 + fl;
            goto fault;
        }
    }
    if ((fail_step = check_grow()) != 0) goto fault;
    /* 8. the lattice interconnect table (zxv_lattice_route.h): the sealed
     * manifold — every module's cell, ring, plane and the 145th-layer
     * container census, verified fail-closed before the kernel proceeds. */
    if (zxv_route_verify(zxv_lattice_route, ZXV_LATTICE_ROUTE_LEN,
                         NULL) != ZXV_ROUTE_OK) {
        fail_step = 90;
        goto fault;
    }
    p = line;
    p = put_str(p, "[SCP144_OK] 144D lattice + tunnel + seed selfcheck ");
    p = put_hex8(p, scp144_blob_digest);
    *p = 0;
    if (out) out(line);
    return true;
fault:
    p = line;
    p = put_str(p, "[FAULT] SCP-144 embed selfcheck step ");
    {
        char d[10];
        uint32_t n = 0, v = (uint32_t) fail_step;
        do {
            d[n++] = (char) ('0' + v % 10u);
            v /= 10u;
        } while (v && n < 10u);
        while (n) *p++ = d[--n];
    }
    *p = 0;
    if (out) out(line);
    return false;
}