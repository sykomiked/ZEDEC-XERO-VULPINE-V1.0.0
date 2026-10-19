/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_grow.c — the stage-0 unfold, implemented exactly as zxv_grow.h and
 * build_system/gen_seed_blob.py's mirror specify: twelve rings, each
 * emitting
 *
 *   LE16  the rung's dimension F_k
 *   LE32  the rung's Cassini determinant  (F_{k+1}*F_{k-1} - F_k^2 = +/-1)
 *   the new coordinates' Zeckendorf modes  v in [F_{k-1}, F_k):
 *         LE16 mask, LE32 energy each
 *   the shell cells  (i,j) in [0,F_k)^2 \ [0,F_{k-1})^2, row-major:
 *         LE64 matrix[i][j] each
 *
 * Everything is recomputed (the extended ladder, Cassini, the Zeckendorf
 * modes and the fold matrix) from the kernel's own generators — the ROM
 * is only compared against, so a corrupted ROM still grows the true plant
 * and rom_diff reports how much of it had rotted (the heal path). Exact
 * integers throughout; the LE serialization is identical on every
 * architecture, so the root is bit-for-bit stable across
 * x86_64/arm64/riscv64/riscv32/arm32/wasm32. */
#include "zxv_grow.h"
#include "scp144.h"
#include "zxv_scp144_blob.h"
#include "../robin_debanks/sha256.h"

static void emit(zxv_grow_t *st, sha256_ctx_t *hash, uint8_t *arena,
                 uint32_t cap, uint64_t v, uint32_t n)
{
    uint32_t b;
    if (st->fault) return;
    if (st->written + n > cap) {
        st->fault = ZXV_GROW_ERR_SPACE;
        return;
    }
    for (b = 0; b < n; b++)
        arena[st->written + b] = (uint8_t) (v >> (b * 8u));
    sha256_update(hash, arena + st->written, n);
    st->written += n;
}

uint32_t zxv_grow_bytes(uint32_t max_rung)
{
    uint32_t prev = 0, total = 0;
    uint32_t a = 1, b = 1;
    if (max_rung > ZXV_GROW_RUNGS) max_rung = ZXV_GROW_RUNGS;
    for (uint32_t r = 0; r < max_rung; r++) {
        uint32_t dim = a;
        uint32_t f2 = a + b;
        a = b;
        b = f2;
        total += 6u + 6u * (dim - prev) + 8u * (dim * dim - prev * prev);
        prev = dim;
    }
    return total;
}

int zxv_grow(zxv_grow_t *st, uint8_t *arena, uint32_t cap, uint32_t max_rung)
{
    static uint16_t fib_ext[SCP144_BLOB_FIB_N + 2u];
    static uint16_t zmask[SCP144_BLOB_ZECK_N];
    static uint32_t zenergy[SCP144_BLOB_ZECK_N];
    sha256_ctx_t hash;
    uint32_t prev = 0;

    if (!st || (!arena && cap)) {
        if (st) st->fault = ZXV_GROW_ERR_ARG;
        return -1;
    }
    st->rings = 0;
    st->written = 0;
    st->rom_diff = 0;
    st->fault = ZXV_GROW_OK;
    sha256_init(&hash);
    if (max_rung > ZXV_GROW_RUNGS) max_rung = ZXV_GROW_RUNGS;

    /* Regenerate the rule tables — the plant's genetic code, not a copy of
     * the ROM it may have rotted in. */
    fib_ext[0] = 0u;
    fib_ext[1] = 1u;
    for (uint32_t i = 2; i < SCP144_BLOB_FIB_N + 2u; i++)
        fib_ext[i] = (uint16_t) (fib_ext[i - 1] + fib_ext[i - 2]);
    for (uint32_t v = 0; v < SCP144_BLOB_ZECK_N; v++)
        zmask[v] = scp144_zeckendorf_mode(v, &zenergy[v]);

    for (uint32_t r = 0; r < max_rung; r++) {
        uint32_t dim = fib_ext[r + 1u];          /* F_{r+1} */
        int32_t det = (int32_t) fib_ext[r + 2u] * (int32_t) fib_ext[r] -
                      (int32_t) fib_ext[r + 1u] * (int32_t) fib_ext[r + 1u];

        emit(st, &hash, arena, cap, dim, 2u);
        emit(st, &hash, arena, cap, (uint32_t) det, 4u);
        if (st->fault) return -1;
        if (dim != scp144_blob_fib[r]) st->rom_diff++;
        if (det != scp144_blob_cassini[r]) st->rom_diff++;

        for (uint32_t v = prev; v < dim; v++) {
            emit(st, &hash, arena, cap, zmask[v], 2u);
            emit(st, &hash, arena, cap, zenergy[v], 4u);
            if (zmask[v] != scp144_blob_zeck_mask[v]) st->rom_diff++;
            if (zenergy[v] != scp144_blob_zeck_energy[v]) st->rom_diff++;
        }

        for (uint32_t i = 0; i < dim; i++) {
            int64_t gap_hi = fib_ext[12u - (i % 12u)]; /* fib[11-(i%12)] */
            int64_t gap_lo = fib_ext[(i % 12u) + 1u];  /* fib[i%12]      */
            int64_t parity = (i & 1u) ? -1 : 1;
            for (uint32_t j = 0; j < dim; j++) {
                uint32_t d;
                int64_t cell;
                if (i < prev && j < prev) continue; /* the older rings' wood */
                d = (i + j) % SCP144_BLOB_ZECK_N;
                cell = parity * (int64_t) zenergy[d] * gap_hi +
                       (int64_t) zmask[d] * gap_lo;
                emit(st, &hash, arena, cap, (uint64_t) cell, 8u);
                if (cell != scp144_blob_matrix[i][j]) st->rom_diff++;
            }
        }
        if (st->fault) return -1;
        st->rings++;
        prev = dim;
    }
    sha256_final(&hash, st->root);
    return (int) st->rings;
}
