/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fortran.h — Fortran unformatted sequential record framing and IBM
 * hexadecimal floating point <-> IEEE 754, done entirely in integer
 * arithmetic (no float types anywhere).
 *
 * Record framing: an unformatted sequential record is bracketed by a leading
 * and trailing record-length marker. Compilers use a 4-byte marker by default
 * and an 8-byte marker for large records (gfortran -frecord-marker=8). Both
 * sizes and both byte orders are supported. The reader validates that the
 * trailing marker equals the leading one before yielding a record. (Fortran
 * source "fixed form" vs "free form" is a compiler front-end concern, not a
 * data format; this is the on-disk record layout both produce.)
 *
 * Floating point conversion operates on 32-bit and 64-bit BIT PATTERNS: IBM
 * System/360 HFP (sign, 7-bit excess-64 base-16 exponent, hex fraction) to and
 * from IEEE 754 binary32/binary64. No C float/double is ever formed; the work
 * is shifts, masks and integer add on uint32_t/uint64_t.
 *
 * HONEST LIMITS. The FP conversion covers finite normal values and signed
 * zero; IEEE Inf/NaN are carried across as Inf/NaN patterns, single-precision
 * narrowing rounds to nearest-even and flushes sub-normals to zero, and HFP
 * has no Inf/NaN so those map to the largest HFP magnitude. Exact round-trip
 * holds for values representable in both formats; base-16 "wobble" means some
 * IEEE values lose low bits when stored as single HFP. This is a bit-pattern
 * converter, not a numerics library.
 */
#ifndef ZXV_LEGACY_FORTRAN_H
#define ZXV_LEGACY_FORTRAN_H

#include <stdbool.h>
#include <stdint.h>

/* ===== unformatted sequential record markers ===== */
typedef struct {
    const uint8_t *buf;
    uint32_t len;
    uint32_t pos;
    uint8_t marker_size; /* 4 or 8 */
    bool big_endian;
} fortran_rec_iter;

void fortran_rec_init(fortran_rec_iter *it, const uint8_t *buf, uint32_t len, uint8_t marker_size,
                      bool big_endian);
/* Yield the next record payload; sets *out_len; advances. NULL at end or if
 * the leading/trailing markers disagree or run past the buffer. */
const uint8_t *fortran_rec_next(fortran_rec_iter *it, uint32_t *out_len);

/* Write one record (marker, data, marker) into out[cap]. Returns total bytes
 * written or 0 on overflow. */
uint32_t fortran_rec_write(const uint8_t *data, uint32_t n, uint8_t marker_size, bool big_endian,
                           uint8_t *out, uint32_t cap);

/* ===== IBM HFP <-> IEEE 754, bit patterns only ===== */
uint32_t hfp32_to_ieee32(uint32_t hfp);
uint32_t ieee32_to_hfp32(uint32_t ieee);
uint64_t hfp64_to_ieee64(uint64_t hfp);
uint64_t ieee64_to_hfp64(uint64_t ieee);

/* IEEE width conversions (integer, round-to-nearest-even on narrowing). */
uint64_t ieee32_to_ieee64(uint32_t x);
uint32_t ieee64_to_ieee32(uint64_t x);

#endif /* ZXV_LEGACY_FORTRAN_H */
