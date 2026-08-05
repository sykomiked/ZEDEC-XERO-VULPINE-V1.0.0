/* lightningrod.c — legacy/modern representation adapters. See header. */
#include "lightningrod.h"

static lr_result_t R_ok(bool exact, const char *note) {
    lr_result_t r; r.ok = true; r.exact = exact; r.note = note; return r;
}
static lr_result_t R_err(const char *note) {
    lr_result_t r; r.ok = false; r.exact = false; r.note = note; return r;
}

static rat_t pow10_rat(uint32_t scale) {
    int64_t d = 1;
    for (uint32_t i = 0; i < scale && i < 18; i++) d *= 10;
    return rat_make(1, d);
}

/* ================= COBOL packed decimal (COMP-3) =================
 * Layout: two BCD digits per byte, most significant first; the LAST
 * nibble is the sign (0xC/0xF positive, 0xD negative). A field of n
 * bytes therefore holds (2n - 1) digits. This is how mainframe money has
 * always been stored — a scaled integer, never a float, which is exactly
 * why those ledgers balance. */
lr_result_t lr_packed_to_rat(const uint8_t *bytes, uint32_t len,
                             uint32_t scale, rat_t *out) {
    /* scale > 18 cannot be represented and was silently clamped, which
     * reported exact=true on a value wrong by a factor of 10^N. */
    if (!bytes || !out || len == 0 || len > 9 || scale > 18) return R_err("bad packed field");

    int64_t acc = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t hi = (uint8_t)(bytes[i] >> 4);
        uint8_t lo = (uint8_t)(bytes[i] & 0x0F);
        if (hi > 9) return R_err("invalid BCD digit");
        acc = acc * 10 + hi;
        if (i + 1 < len) {                    /* low nibble is a digit */
            if (lo > 9) return R_err("invalid BCD digit");
            acc = acc * 10 + lo;
        } else {                              /* last low nibble = sign */
            if (lo == 0x0D || lo == 0x0B) acc = -acc;
            else if (lo != 0x0C && lo != 0x0F && lo != 0x0A && lo != 0x0E)
                return R_err("invalid sign nibble");
        }
    }
    rat_t v = rat_mul(rat_from_int(acc), pow10_rat(scale));
    if (!v.valid) return R_err("value out of range");
    *out = v;
    return R_ok(true, "packed decimal is exact by construction");
}

lr_result_t lr_rat_to_packed(rat_t v, uint32_t scale,
                             uint8_t *out, uint32_t max, uint32_t *written) {
    if (!out || !v.valid || scale > 18) return R_err("bad argument");
    /* defense in depth: no constructor produces these, but a hand-built
     * struct must not reach the negations below */
    if (v.num == (-9223372036854775807LL - 1) ||
        v.den == (-9223372036854775807LL - 1)) return R_err("bad argument");

    /* scale up; the result must be a whole number or the value does not
     * fit the field's precision and we refuse rather than round */
    int64_t p = 1;
    for (uint32_t i = 0; i < scale; i++) p *= 10;
    rat_t scaled = rat_mul(v, rat_from_int(p));
    if (!scaled.valid) return R_err("overflow scaling to field precision");
    if (!rat_is_int(scaled))
        return R_err("value needs more decimal places than the field has");

    int64_t n = scaled.num;
    bool neg = n < 0;
    if (neg) n = -n;

    /* count digits, then pack; digits must be odd-length for COMP-3 */
    uint32_t digits = 1;
    for (int64_t t = n; t >= 10; t /= 10) digits++;
    if (digits % 2 == 0) digits++;            /* pad to odd */
    uint32_t nbytes = (digits + 1) / 2;
    if (nbytes > max) return R_err("output buffer too small");

    for (uint32_t i = 0; i < nbytes; i++) out[i] = 0;
    /* fill from the least significant nibble backwards */
    int32_t nib = (int32_t)(nbytes * 2 - 1);  /* index of the sign nibble */
    out[nbytes - 1] = (uint8_t)(neg ? 0x0D : 0x0C);
    nib--;
    int64_t t = n;
    while (nib >= 0) {
        uint8_t d = (uint8_t)(t % 10); t /= 10;
        uint32_t byte = (uint32_t)nib / 2;
        if ((uint32_t)nib % 2 == 0) out[byte] |= (uint8_t)(d << 4);
        else                        out[byte] |= d;
        nib--;
    }
    if (written) *written = nbytes;
    return R_ok(true, "exact");
}

/* ================= COBOL zoned decimal (DISPLAY) =================
 * One digit per byte in the low nibble; the sign is overpunched into the
 * high nibble of the final byte. */
lr_result_t lr_zoned_to_rat(const uint8_t *bytes, uint32_t len,
                            uint32_t scale, rat_t *out) {
    if (!bytes || !out || len == 0 || len > 18 || scale > 18) return R_err("bad zoned field");
    int64_t acc = 0;
    bool neg = false;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t hi = (uint8_t)(bytes[i] >> 4);
        uint8_t lo = (uint8_t)(bytes[i] & 0x0F);
        if (lo > 9) return R_err("invalid zoned digit");
        acc = acc * 10 + lo;
        if (i + 1 == len && (hi == 0x0D || hi == 0x0B)) neg = true;
    }
    if (neg) acc = -acc;
    rat_t v = rat_mul(rat_from_int(acc), pow10_rat(scale));
    if (!v.valid) return R_err("value out of range");
    *out = v;
    return R_ok(true, "zoned decimal is exact");
}

lr_result_t lr_scaled_to_rat(int64_t value, uint32_t scale, rat_t *out) {
    if (!out || scale > 18) return R_err("bad scale");
    /* INT64_MIN has no int64 negation; rational refuses it internally,
     * but the adapter boundary states it explicitly and first. */
    if (value == (-9223372036854775807LL - 1)) return R_err("value out of int64 rational range");
    rat_t v = rat_mul(rat_from_int(value), pow10_rat(scale));
    if (!v.valid) return R_err("overflow");
    *out = v;
    return R_ok(true, "scaled integer is exact");
}

/* IEEE-754 double -> rational.
 * A double IS a rational (mantissa * 2^exp), so the conversion itself is
 * exact — but the value it holds is usually NOT the decimal the user
 * typed. We therefore return the true value and report exact=false
 * whenever the denominator is not a power of ten, which is the honest
 * signal that a decimal round-trip will not match. */
lr_result_t lr_double_bits_to_rat(uint64_t bits, rat_t *out) {
    if (!out) return R_err("null out");
    uint32_t sign = (uint32_t)(bits >> 63);
    int32_t  exp  = (int32_t)((bits >> 52) & 0x7FF);
    uint64_t mant = bits & 0xFFFFFFFFFFFFFull;

    if (exp == 0x7FF) return R_err("NaN or Infinity has no rational value");
    if (exp == 0 && mant == 0) { *out = rat_zero(); return R_ok(true, "zero"); }

    int32_t e; uint64_t m;
    if (exp == 0) { m = mant; e = -1074; }                 /* subnormal */
    else          { m = mant | (1ull << 52); e = exp - 1075; }

    /* value = m * 2^e ; keep it exact but bail if it will not fit int64 */
    rat_t v;
    if (e >= 0) {
        if (e > 10) return R_err("magnitude too large for exact int64 rational");
        int64_t mul = 1; for (int32_t i = 0; i < e; i++) mul *= 2;
        v = rat_from_int((int64_t)m * mul);
    } else {
        int32_t shift = -e;
        /* cancel common factors of two BEFORE the range guard: the guard
         * used to test the unreduced exponent and rejected values that
         * are exactly representable (e.g. 2^-60 stored as mant 2^52) */
        while (shift > 0 && (m & 1ull) == 0) { m >>= 1; shift--; }
        if (shift > 62) return R_err("magnitude too small for exact int64 rational");
        int64_t den = 1ll << shift;
        v = rat_make((int64_t)m, den);
    }
    if (!v.valid) return R_err("not representable in int64 rational");
    if (sign) v = rat_neg(v);
    *out = v;

    /* Is this the decimal a human would have typed?
     *
     * Every double is n/2^k, so it ALWAYS terminates in decimal in the
     * strict mathematical sense — which makes "is it a terminating
     * decimal" the wrong question. 0.1 as a double is
     * 3602879701896397/2^55: terminating, but it needs 55 decimal places
     * and is NOT one tenth. The useful question is whether the value has
     * a SHORT decimal form, because that is what distinguishes a value
     * the user actually entered from a binary approximation of one.
     *
     * 18 places is the bound: it is the most an int64-scaled decimal can
     * carry, and no hand-entered figure exceeds it. */
    int64_t d = v.den; uint32_t twos = 0, fives = 0;
    while (d % 2 == 0) { d /= 2; twos++; }
    while (d % 5 == 0) { d /= 5; fives++; }
    uint32_t places = (twos > fives) ? twos : fives;
    bool decimal_exact = (d == 1) && (places <= 18);
    return R_ok(decimal_exact,
                decimal_exact ? "value has a short exact decimal form"
                              : "double holds a binary approximation, not the typed decimal");
}

/* ================= EBCDIC (cp037) <-> ASCII =================
 * Table covers the invariant subset: digits, A-Z, a-z, space and the
 * common punctuation that appears in fixed record layouts. Anything
 * outside it is REPORTED, not silently substituted. */
static const uint8_t EB2A[256] = {
    /* 0x00 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x10 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x20 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x30 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0x40 */ ' ',0,0,0,0,0,0,0,0,0,0,'.','<','(','+','|',
    /* 0x50 */ '&',0,0,0,0,0,0,0,0,0,'!','$','*',')',';',0,
    /* 0x60 */ '-','/',0,0,0,0,0,0,0,0,0,',','%','_','>','?',
    /* 0x70 */ 0,0,0,0,0,0,0,0,0,'`',':','#','@','\'','=','"',
    /* 0x80 */ 0,'a','b','c','d','e','f','g','h','i',0,0,0,0,0,0,
    /* 0x90 */ 0,'j','k','l','m','n','o','p','q','r',0,0,0,0,0,0,
    /* 0xA0 */ 0,'~','s','t','u','v','w','x','y','z',0,0,0,0,0,0,
    /* 0xB0 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    /* 0xC0 */ '{','A','B','C','D','E','F','G','H','I',0,0,0,0,0,0,
    /* 0xD0 */ '}','J','K','L','M','N','O','P','Q','R',0,0,0,0,0,0,
    /* 0xE0 */ '\\',0,'S','T','U','V','W','X','Y','Z',0,0,0,0,0,0,
    /* 0xF0 */ '0','1','2','3','4','5','6','7','8','9',0,0,0,0,0,0
};

lr_result_t lr_ebcdic_to_ascii(const uint8_t *in, uint32_t len,
                               uint8_t *out, uint32_t max) {
    if (!in || !out) return R_err("null buffer");
    if (len > max) return R_err("output buffer too small");
    bool all = true;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t a = EB2A[in[i]];
        if (a == 0 && in[i] != 0x00) { all = false; a = '?'; }
        out[i] = a;
    }
    return R_ok(all, all ? "all bytes mapped"
                         : "some bytes are outside the mapped subset");
}

lr_result_t lr_ascii_to_ebcdic(const uint8_t *in, uint32_t len,
                               uint8_t *out, uint32_t max) {
    if (!in || !out) return R_err("null buffer");
    if (len > max) return R_err("output buffer too small");
    bool all = true;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t want = in[i], found = 0;
        for (uint32_t e = 0; e < 256; e++) {
            if (EB2A[e] == want && want != 0) { found = (uint8_t)e; break; }
        }
        if (!found && want != 0) { all = false; found = 0x6F; /* '?' */ }
        out[i] = found;
    }
    return R_ok(all, all ? "all bytes mapped" : "unmapped characters present");
}

/* ================= string conventions ================= */
lr_result_t lr_fixed_to_cstr(const uint8_t *in, uint32_t width,
                             char *out, uint32_t max) {
    if (!in || !out || max == 0) return R_err("null buffer");
    /* NEVER add to a caller-supplied uint32: width = UINT32_MAX makes
     * width+1 wrap to 0, the check passes, and the copy below runs off the
     * end of `out`. Subtract instead — max is already known non-zero. */
    if (width > max - 1) return R_err("output buffer too small");
    uint32_t end = width;
    while (end > 0 && in[end - 1] == ' ') end--;      /* strip pad */
    for (uint32_t i = 0; i < end; i++) out[i] = (char)in[i];
    out[end] = '\0';
    return R_ok(true, "trailing spaces stripped");
}

lr_result_t lr_cstr_to_fixed(const char *in, uint8_t *out, uint32_t width) {
    if (!in || !out) return R_err("null buffer");
    uint32_t i = 0;
    while (in[i] && i < width) { out[i] = (uint8_t)in[i]; i++; }
    if (in[i]) return R_err("string longer than the fixed field");
    for (; i < width; i++) out[i] = ' ';
    return R_ok(true, "space padded to width");
}

/* ================= array layout ================= */
lr_result_t lr_transpose(const uint8_t *src, uint8_t *dst,
                         uint32_t rows, uint32_t cols, uint32_t elem_size,
                         lr_layout_t from) {
    if (!src || !dst || rows == 0 || cols == 0 || elem_size == 0)
        return R_err("bad argument");
    for (uint32_t r = 0; r < rows; r++) {
        for (uint32_t c = 0; c < cols; c++) {
            uint32_t si, di;
            if (from == LR_ARR_COL_MAJOR) {   /* Fortran -> C */
                si = (c * rows + r) * elem_size;
                di = (r * cols + c) * elem_size;
            } else {                          /* C -> Fortran */
                si = (r * cols + c) * elem_size;
                di = (c * rows + r) * elem_size;
            }
            for (uint32_t b = 0; b < elem_size; b++) dst[di + b] = src[si + b];
        }
    }
    return R_ok(true, "layout transposed, values unchanged");
}

uint32_t lr_index_f_to_c(uint32_t i1, uint32_t j1, uint32_t cols) {
    /* Fortran is 1-based: A(i,j) -> C a[(i-1)*cols + (j-1)] */
    uint32_t i = (i1 ? i1 - 1 : 0), j = (j1 ? j1 - 1 : 0);
    return i * cols + j;
}

/* ================= bridge registry ================= */
bool lr_bridge_exists(lr_numfmt_t from, lr_numfmt_t to, bool *lossless,
                      const char **note) {
    if (from >= LR_NUM_MAX || to >= LR_NUM_MAX) {
        if (note) *note = "unknown representation";
        return false;
    }
    if (from == to) {
        if (lossless) *lossless = true;
        if (note) *note = "identity";
        return true;
    }
    /* Everything decimal-shaped converts to/from RATIONAL exactly, so the
     * rational form is the hub of the lattice — add a language by
     * describing its representation, not by writing N^2 converters. */
    bool from_exact = (from == LR_NUM_PACKED_DECIMAL || from == LR_NUM_ZONED_DECIMAL ||
                       from == LR_NUM_BINARY_INT || from == LR_NUM_SCALED_INT ||
                       from == LR_NUM_RATIONAL);
    bool to_exact   = (to == LR_NUM_PACKED_DECIMAL || to == LR_NUM_ZONED_DECIMAL ||
                       to == LR_NUM_BINARY_INT || to == LR_NUM_SCALED_INT ||
                       to == LR_NUM_RATIONAL);

    if (from_exact && to_exact) {
        if (lossless) *lossless = true;
        if (note) *note = "exact via the rational hub";
        return true;
    }
    if (from == LR_NUM_IEEE_DOUBLE || to == LR_NUM_IEEE_DOUBLE) {
        if (lossless) *lossless = false;
        if (note) *note = "binary float cannot represent every decimal — LOSSY";
        return true;      /* a bridge exists, but it is explicitly lossy */
    }
    if (note) *note = "no safe bridge";
    return false;
}

const char *lr_lang_name(lr_lang_t l) {
    switch (l) {
        case LR_LANG_COBOL:   return "COBOL";
        case LR_LANG_FORTRAN: return "Fortran";
        case LR_LANG_C:       return "C";
        case LR_LANG_MODERN:  return "modern";
        case LR_LANG_ZXV:     return "ZXV";
        default:              return "unknown";
    }
}
const char *lr_numfmt_name(lr_numfmt_t f) {
    switch (f) {
        case LR_NUM_PACKED_DECIMAL: return "packed-decimal(COMP-3)";
        case LR_NUM_ZONED_DECIMAL:  return "zoned-decimal(DISPLAY)";
        case LR_NUM_BINARY_INT:     return "binary-int";
        case LR_NUM_SCALED_INT:     return "scaled-int";
        case LR_NUM_IEEE_DOUBLE:    return "ieee-double";
        case LR_NUM_RATIONAL:       return "exact-rational";
        default:                    return "unknown";
    }
}
