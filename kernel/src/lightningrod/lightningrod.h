/* lightningrod.h — Lightning Rod: legacy/modern language adapters
 *
 * WHAT THE REAL PROBLEM IS
 * ------------------------
 * Making COBOL, Fortran and modern code work together is not mainly a
 * syntax problem. Compilers already emit machine code. What actually
 * breaks integration is that these languages DISAGREE ABOUT HOW DATA IS
 * SHAPED IN MEMORY:
 *
 *   COBOL    packed decimal (COMP-3), zoned decimal, EBCDIC, fixed
 *            record layouts, money as scaled integers
 *   Fortran  column-major arrays, 1-based indices, pass-by-reference,
 *            fixed-width space-padded strings
 *   C/modern row-major arrays, 0-based indices, NUL-terminated strings,
 *            binary floating point
 *
 * Lightning Rod is therefore a REPRESENTATION ADAPTER, not a transpiler.
 * It converts values and layouts across those conventions losslessly,
 * and says so when a conversion cannot be lossless.
 *
 * WHY THIS FITS THIS OS PARTICULARLY WELL
 * ---------------------------------------
 * COBOL money is a scaled decimal integer — it was never floating point,
 * which is precisely why 60-year-old ledgers still balance. Converting
 * COMP-3 into an IEEE double (what most migrations do) INTRODUCES the
 * rounding error the original system never had. Here we convert COMP-3
 * straight into `rat_t`, which is exact, so a migrated ledger balances
 * bit-for-bit with the mainframe. That is a real, checkable claim.
 *
 * Adapters register into the Orbital Elevator's compatibility graph, so
 * a caller asks for "this value, in that representation" and the graph
 * finds a conversion path — or reports NO SAFE BRIDGE rather than
 * guessing.
 *
 * WHAT WE DO NOT CLAIM
 * - Not "every language of code". We claim a REPRESENTATION LATTICE:
 *   any language whose data model is described by these primitives is
 *   reachable. Adding a language means describing its conventions, not
 *   writing a new compiler.
 * - Not automatic semantic translation of program logic.
 * - Conversions that cannot be exact (binary float -> decimal) are
 *   FLAGGED, never silently rounded.
 *
 * Freestanding: integer/rational only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Lightning Rod slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_LIGHTNINGROD_H
#define ZXV_LIGHTNINGROD_H

#include <stdint.h>
#include <stdbool.h>
#include "../rational/rational.h"

/* ---- language / representation families ---- */
typedef enum {
    LR_LANG_COBOL = 0,
    LR_LANG_FORTRAN,
    LR_LANG_C,
    LR_LANG_MODERN,      /* any 0-based, row-major, NUL-terminated runtime */
    LR_LANG_ZXV,         /* native: exact rationals, event envelopes */
    LR_LANG_MAX
} lr_lang_t;

/* ---- how a numeric value is laid out ---- */
typedef enum {
    LR_NUM_PACKED_DECIMAL = 0, /* COBOL COMP-3: 2 digits/byte, sign nibble */
    LR_NUM_ZONED_DECIMAL,      /* COBOL DISPLAY: one digit per byte */
    LR_NUM_BINARY_INT,         /* two's complement integer */
    LR_NUM_SCALED_INT,         /* integer + implied decimal places */
    LR_NUM_IEEE_DOUBLE,        /* binary floating point (LOSSY into decimal) */
    LR_NUM_RATIONAL,           /* ZXV native: exact */
    LR_NUM_MAX
} lr_numfmt_t;

/* ---- string conventions ---- */
typedef enum {
    LR_STR_FIXED_SPACE = 0,    /* COBOL/Fortran: fixed width, space padded */
    LR_STR_NUL_TERM,           /* C */
    LR_STR_LEN_PREFIX,         /* length-prefixed */
    LR_STR_MAX
} lr_strfmt_t;

/* ---- array layout ---- */
typedef enum {
    LR_ARR_ROW_MAJOR = 0,      /* C / modern */
    LR_ARR_COL_MAJOR,          /* Fortran */
} lr_layout_t;

/* Result of a conversion attempt. `exact` is the load-bearing field:
 * a caller must be able to distinguish a lossless bridge from a lossy
 * one, which is what "no safe bridge" reporting depends on. */
typedef struct {
    bool ok;          /* the conversion was performed */
    bool exact;       /* it was lossless */
    const char *note; /* why not, when !ok or !exact */
} lr_result_t;

/* ================= numeric conversions ================= */

/* COBOL COMP-3 packed decimal -> exact rational.
 * `bytes`/`len` is the packed field; `scale` is the implied number of
 * decimal places (PIC S9(7)V99 => scale 2). Exact by construction. */
lr_result_t lr_packed_to_rat(const uint8_t *bytes, uint32_t len,
                             uint32_t scale, rat_t *out);

/* Exact rational -> COBOL COMP-3. Fails (rather than rounding) if the
 * value cannot be represented at the requested scale. */
lr_result_t lr_rat_to_packed(rat_t v, uint32_t scale,
                             uint8_t *out, uint32_t max, uint32_t *written);

/* COBOL zoned decimal (one digit per byte, sign overpunched on the last)
 * -> exact rational. */
lr_result_t lr_zoned_to_rat(const uint8_t *bytes, uint32_t len,
                            uint32_t scale, rat_t *out);

/* Scaled integer (value * 10^-scale) -> exact rational. */
lr_result_t lr_scaled_to_rat(int64_t value, uint32_t scale, rat_t *out);

/* IEEE double bit pattern -> rational. ALWAYS reports exact=false when
 * the double is not exactly representable as the intended decimal —
 * this is the conversion that silently corrupts naive migrations. */
lr_result_t lr_double_bits_to_rat(uint64_t ieee_bits, rat_t *out);

/* ================= text conversions ================= */

/* EBCDIC (IBM cp037) <-> ASCII. Round-trips exactly for the invariant
 * subset; unmapped bytes are reported rather than substituted. */
lr_result_t lr_ebcdic_to_ascii(const uint8_t *in, uint32_t len,
                               uint8_t *out, uint32_t max);
lr_result_t lr_ascii_to_ebcdic(const uint8_t *in, uint32_t len,
                               uint8_t *out, uint32_t max);

/* Fixed-width space-padded <-> NUL-terminated. */
lr_result_t lr_fixed_to_cstr(const uint8_t *in, uint32_t width,
                             char *out, uint32_t max);
lr_result_t lr_cstr_to_fixed(const char *in, uint8_t *out, uint32_t width);

/* ================= array layout ================= */

/* Transpose between Fortran column-major and C row-major.
 * elem_size in bytes; rows/cols in elements. In-place is not supported —
 * src and dst must not overlap. */
lr_result_t lr_transpose(const uint8_t *src, uint8_t *dst,
                         uint32_t rows, uint32_t cols, uint32_t elem_size,
                         lr_layout_t from);

/* Index translation: Fortran's 1-based (i,j) to a C row-major offset. */
uint32_t lr_index_f_to_c(uint32_t i1, uint32_t j1, uint32_t cols);

/* ================= the bridge registry =================
 * Ask whether a conversion path exists between two representations, and
 * whether it is lossless. Returns false with a note when there is NO
 * SAFE BRIDGE — an explicit outcome, never a silent guess. */
bool lr_bridge_exists(lr_numfmt_t from, lr_numfmt_t to, bool *lossless,
                      const char **note);

/* Human-readable names, for diagnostics and the shell. */
const char *lr_lang_name(lr_lang_t l);
const char *lr_numfmt_name(lr_numfmt_t f);

#endif /* ZXV_LIGHTNINGROD_H */
