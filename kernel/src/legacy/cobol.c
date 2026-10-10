/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cobol.c — see cobol.h. */
#include "cobol.h"
#include "legacy_util.h"

/* ===================== COMP-3 packed decimal ===================== */

bool comp3_decode(const uint8_t *in, uint32_t nbytes, int64_t *out)
{
    if (nbytes == 0 || nbytes > 10) return false;
    uint64_t v = 0;
    /* all nibbles except the final sign nibble are digits */
    for (uint32_t i = 0; i < nbytes; i++) {
        uint8_t hi = (uint8_t) (in[i] >> 4);
        uint8_t lo = (uint8_t) (in[i] & 0x0F);
        if (hi > 9) return false; /* high nibble is always a digit */
        v = v * 10 + hi;
        if (i == nbytes - 1) {
            /* lo is sign */
            bool neg = (lo == 0x0D || lo == 0x0B);
            *out = neg ? -(int64_t) v : (int64_t) v;
            return (lo >= 0x0A); /* a valid sign nibble is A..F */
        }
        if (lo > 9) return false;
        v = v * 10 + lo;
    }
    return false;
}

bool comp3_encode(int64_t value, uint8_t *out, uint32_t nbytes)
{
    if (nbytes == 0 || nbytes > 10) return false;
    bool neg = value < 0;
    uint64_t mag = neg ? (uint64_t) (-(value + 1)) + 1u : (uint64_t) value;
    uint32_t ndig = nbytes * 2 - 1; /* digit positions */
    lg_fill(out, 0, nbytes);
    /* fill from least significant: sign nibble is low nibble of last byte */
    out[nbytes - 1] = (uint8_t) (neg ? 0x0D : 0x0C);
    uint32_t pos = 0; /* digit index from the right */
    for (uint32_t d = 0; d < ndig; d++) {
        uint32_t rem;
        mag = lg_udiv64_small(mag, 10, &rem);
        uint8_t digit = (uint8_t) rem;
        /* digit `pos` from the right: position in nibble stream.
         * nibble stream (from right): byte[last].low=sign, byte[last].high=digit0,
         * byte[last-1].low=digit1, byte[last-1].high=digit2, ... */
        uint32_t nib = pos + 1; /* +1 because nibble 0 is the sign */
        uint32_t byte = nbytes - 1 - (nib / 2);
        bool high = (nib % 2) == 1;
        if (high)
            out[byte] |= (uint8_t) (digit << 4);
        else
            out[byte] |= digit;
        pos++;
    }
    return mag == 0; /* false if value had more digits than the field holds */
}

/* ===================== zoned decimal ===================== */

bool zoned_decode(const uint8_t *in, uint32_t ndigits, int64_t *out)
{
    if (ndigits == 0 || ndigits > 18) return false;
    uint64_t v = 0;
    for (uint32_t i = 0; i < ndigits; i++) {
        uint8_t d = (uint8_t) (in[i] & 0x0F);
        if (d > 9) return false;
        v = v * 10 + d;
    }
    uint8_t zone = (uint8_t) (in[ndigits - 1] >> 4);
    bool neg = (zone == 0x0D);
    *out = neg ? -(int64_t) v : (int64_t) v;
    return true;
}

bool zoned_encode(int64_t value, uint8_t *out, uint32_t ndigits, bool signed_field)
{
    if (ndigits == 0 || ndigits > 18) return false;
    bool neg = value < 0;
    uint64_t mag = neg ? (uint64_t) (-(value + 1)) + 1u : (uint64_t) value;
    for (uint32_t i = 0; i < ndigits; i++) {
        uint32_t rem;
        mag = lg_udiv64_small(mag, 10, &rem);
        uint32_t idx = ndigits - 1 - i;
        out[idx] = (uint8_t) (0xF0 | (uint8_t) rem); /* EBCDIC zoned digit */
    }
    if (signed_field) {
        uint8_t d = (uint8_t) (out[ndigits - 1] & 0x0F);
        out[ndigits - 1] = (uint8_t) ((neg ? 0xD0 : 0xC0) | d);
    }
    return mag == 0;
}

/* ===================== binary COMP ===================== */

bool comp_decode(const uint8_t *in, uint32_t size, bool is_signed, int64_t *out)
{
    if (size != 2 && size != 4 && size != 8) return false;
    uint64_t v = 0;
    for (uint32_t i = 0; i < size; i++) v = (v << 8) | in[i];
    if (is_signed && size < 8) {
        uint64_t sign_bit = (uint64_t) 1 << (size * 8 - 1);
        if (v & sign_bit) v |= ~((sign_bit << 1) - 1); /* sign extend */
    }
    *out = (int64_t) v;
    return true;
}

bool comp_encode(int64_t value, uint8_t *out, uint32_t size, bool is_signed)
{
    if (size != 2 && size != 4 && size != 8) return false;
    (void) is_signed;
    uint64_t v = (uint64_t) value;
    for (uint32_t i = 0; i < size; i++) out[size - 1 - i] = (uint8_t) (v >> (8 * i));
    return true;
}

/* ===================== copybook parser ===================== */

/* binary size for a COMP field of n digits (IBM default). */
static uint32_t comp_size_for(uint32_t ndigits)
{
    if (ndigits <= 4) return 2;
    if (ndigits <= 9) return 4;
    return 8;
}

/* a line tokenizer over whitespace; words delimited by space/tab, '.' is a
 * terminator we strip. */
typedef struct {
    const char *p;
    uint32_t len;
} tok;

static uint32_t split_words(const char *line, uint32_t llen, tok *w, uint32_t maxw)
{
    uint32_t n = 0, i = 0;
    while (i < llen && n < maxw) {
        while (i < llen && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i >= llen) break;
        uint32_t s = i;
        while (i < llen && line[i] != ' ' && line[i] != '\t') i++;
        w[n].p = line + s;
        w[n].len = i - s;
        n++;
    }
    return n;
}

static bool word_ieq(tok t, const char *lit)
{
    /* Length first: comparing t.len bytes of a shorter literal read past it. */
    return lg_strnlen(lit, 32) == t.len && lg_ascii_ieq(t.p, lit, t.len);
}

static bool word_prefix_ieq(tok t, const char *lit)
{
    uint32_t n = lg_strnlen(lit, 16);
    return t.len >= n && lg_ascii_ieq(t.p, lit, n);
}

/* parse "9(6)", "X(20)", "S9(9)V99", "9(4)V9(2)" -> ndigits, scale, alpha,
 * signed. Returns true on success. */
/* Largest repeat count a PIC clause may give, so field sizes and offsets
 * stay far from uint32 wrap (128 fields x 2^20 < 2^32). */
#define COB_MAX_PIC (1u << 20)

static bool parse_pic(tok t, cob_type *type, uint32_t *ndigits, uint32_t *scale, bool *is_signed,
                      uint32_t *alpha_size)
{
    const char *s = t.p;
    uint32_t n = t.len, i = 0;
    *ndigits = 0;
    *scale = 0;
    *is_signed = false;
    *alpha_size = 0;
    bool after_v = false;
    bool is_num = false;
    while (i < n) {
        char c = s[i];
        if (c == 'S' || c == 's') {
            *is_signed = true;
            i++;
        } else if (c == 'V' || c == 'v') {
            after_v = true;
            i++;
        } else if (c == 'X' || c == 'x' || c == 'A' || c == 'a') {
            /* alpha run, optional (n) */
            uint32_t cnt = 1;
            i++;
            if (i < n && s[i] == '(') {
                i++;
                cnt = 0;
                while (i < n && lg_is_digit((uint8_t) s[i])) {
                    cnt = cnt * 10 + (uint32_t) (s[i++] - '0');
                    if (cnt > COB_MAX_PIC) return false; /* no wrapped sizes */
                }
                if (i < n && s[i] == ')') i++;
            }
            *alpha_size += cnt;
            *type = COB_ALPHA;
        } else if (c == '9') {
            is_num = true;
            uint32_t cnt = 1;
            i++;
            if (i < n && s[i] == '(') {
                i++;
                cnt = 0;
                while (i < n && lg_is_digit((uint8_t) s[i])) {
                    cnt = cnt * 10 + (uint32_t) (s[i++] - '0');
                    if (cnt > COB_MAX_PIC) return false; /* no wrapped sizes */
                }
                if (i < n && s[i] == ')') i++;
            }
            *ndigits += cnt;
            if (after_v) *scale += cnt;
        } else {
            i++; /* skip unknown picture chars (e.g. stray) */
        }
    }
    if (*alpha_size) {
        *type = COB_ALPHA;
        return true;
    }
    if (is_num) {
        *type = COB_ZONED; /* refined later by USAGE */
        return true;
    }
    return false;
}

bool cobol_parse_copybook(const char *text, uint32_t len, cob_layout *lo)
{
    lo->nfields = 0;
    lo->record_size = 0;
    uint32_t off = 0;
    uint32_t pos = 0;

    /* track the most recent group/OCCURS context for expansion: we support a
     * single OCCURS group at a time. When an OCCURS group header is seen we
     * record its start field index and copy count, and on the group's end we
     * replicate. To keep it bounded and simple, we handle OCCURS on an
     * elementary item directly (PIC ... OCCURS n). */

    while (pos < len) {
        /* read a line */
        uint32_t ls = pos;
        while (pos < len && text[pos] != '\n') pos++;
        uint32_t le = pos;
        if (le > ls && text[le - 1] == '\r') le--;
        if (pos < len) pos++;
        if (le <= ls) continue;

        tok w[12];
        uint32_t nw = split_words(text + ls, le - ls, w, 12);
        if (nw == 0) continue;
        /* strip a trailing '.' from the last word */
        if (w[nw - 1].len > 0 && w[nw - 1].p[w[nw - 1].len - 1] == '.') w[nw - 1].len--;
        if (nw == 0) continue;

        /* first word must be a level number */
        if (!lg_is_digit((uint8_t) w[0].p[0])) continue;
        uint32_t level = 0;
        for (uint32_t k = 0; k < w[0].len && lg_is_digit((uint8_t) w[0].p[k]); k++)
            level = level * 10 + (uint32_t) (w[0].p[k] - '0');
        if (level < 1 || level > 49) continue;
        if (nw < 2) continue; /* need at least a name */

        /* name is w[1] */
        char name[COB_MAX_NAME];
        uint32_t nl = w[1].len < COB_MAX_NAME - 1 ? w[1].len : COB_MAX_NAME - 1;
        for (uint32_t k = 0; k < nl; k++) name[k] = w[1].p[k];
        name[nl] = 0;

        /* scan remaining words for PIC, USAGE, OCCURS, REDEFINES */
        bool has_pic = false;
        tok pic = {0, 0};
        cob_type usage_type = COB_ZONED; /* default DISPLAY */
        bool usage_comp3 = false, usage_comp = false;
        uint32_t occurs = 1;
        bool redefines = false;
        const char *redef_name = 0;
        uint32_t redef_len = 0;

        for (uint32_t k = 2; k < nw; k++) {
            if (word_ieq(w[k], "PIC") || word_ieq(w[k], "PICTURE")) {
                if (k + 1 < nw) {
                    pic = w[k + 1];
                    has_pic = true;
                    k++;
                }
            } else if (word_prefix_ieq(w[k], "COMP-3") || word_ieq(w[k], "PACKED-DECIMAL")) {
                usage_comp3 = true;
            } else if (word_ieq(w[k], "COMP") || word_ieq(w[k], "COMP-4") ||
                       word_ieq(w[k], "COMP-5") || word_ieq(w[k], "BINARY") ||
                       word_ieq(w[k], "COMPUTATIONAL") || word_ieq(w[k], "COMPUTATIONAL-4") ||
                       word_ieq(w[k], "COMPUTATIONAL-5")) {
                usage_comp = true;
            } else if (word_ieq(w[k], "OCCURS")) {
                if (k + 1 < nw) {
                    uint32_t v = 0;
                    for (uint32_t j = 0; j < w[k + 1].len && lg_is_digit((uint8_t) w[k + 1].p[j]);
                         j++)
                        v = v * 10 + (uint32_t) (w[k + 1].p[j] - '0');
                    if (v > 0) occurs = v;
                }
            } else if (word_ieq(w[k], "REDEFINES")) {
                redefines = true;
                if (k + 1 < nw) {
                    redef_name = w[k + 1].p;
                    redef_len = w[k + 1].len;
                }
            }
        }

        if (!has_pic) {
            /* a group item: no storage of its own; its children carry it.
             * We do not expand group-level OCCURS (documented limit). */
            continue;
        }

        uint32_t ndigits = 0, scale = 0, alpha_size = 0;
        cob_type ptype;
        bool is_signed = false;
        if (!parse_pic(pic, &ptype, &ndigits, &scale, &is_signed, &alpha_size)) continue;

        cob_type ftype = ptype;
        uint32_t fsize = 0;
        if (ptype == COB_ALPHA) {
            ftype = COB_ALPHA;
            fsize = alpha_size;
        } else {
            if (usage_comp3) {
                ftype = COB_COMP3;
                fsize = ndigits / 2 + 1;
            } else if (usage_comp) {
                ftype = COB_COMP;
                fsize = comp_size_for(ndigits);
            } else {
                ftype = COB_ZONED;
                fsize = ndigits; /* one byte per digit */
            }
            (void) usage_type;
        }

        /* REDEFINES: this field starts at the redefined field's offset. */
        uint32_t base_off = off;
        if (redefines && redef_name) {
            char rn[COB_MAX_NAME];
            uint32_t rl = redef_len < COB_MAX_NAME - 1 ? redef_len : COB_MAX_NAME - 1;
            for (uint32_t k = 0; k < rl; k++) rn[k] = redef_name[k];
            rn[rl] = 0;
            const cob_field *rf = cobol_find(lo, rn);
            if (rf) base_off = rf->offset;
        }

        /* emit `occurs` copies */
        for (uint32_t c = 0; c < occurs; c++) {
            if (lo->nfields >= COB_MAX_FIELDS) return false;
            cob_field *f = &lo->fields[lo->nfields++];
            for (uint32_t k = 0; k <= nl; k++) f->name[k] = name[k];
            f->type = ftype;
            f->offset = base_off + c * fsize;
            f->size = fsize;
            f->ndigits = (ftype == COB_ALPHA) ? 0 : ndigits;
            f->scale = scale;
            f->is_signed = is_signed;
            f->level = (uint8_t) level;
            f->occurs_index = (uint16_t) c;
        }

        if (!redefines) {
            off = base_off + occurs * fsize;
            if (off > lo->record_size) lo->record_size = off;
        } else {
            uint32_t endo = base_off + occurs * fsize;
            if (endo > lo->record_size) lo->record_size = endo;
        }
    }
    return lo->nfields > 0;
}

const cob_field *cobol_find(const cob_layout *lo, const char *name)
{
    uint32_t nl = lg_strnlen(name, COB_MAX_NAME);
    for (uint32_t i = 0; i < lo->nfields; i++) {
        if (lo->fields[i].occurs_index != 0) continue;
        if (lg_strnlen(lo->fields[i].name, COB_MAX_NAME) == nl &&
            lg_ascii_ieq(lo->fields[i].name, name, nl))
            return &lo->fields[i];
    }
    return 0;
}

const cob_field *cobol_find_indexed(const cob_layout *lo, const char *name, uint16_t index)
{
    uint32_t nl = lg_strnlen(name, COB_MAX_NAME);
    for (uint32_t i = 0; i < lo->nfields; i++) {
        if (lo->fields[i].occurs_index != index) continue;
        if (lg_strnlen(lo->fields[i].name, COB_MAX_NAME) == nl &&
            lg_ascii_ieq(lo->fields[i].name, name, nl))
            return &lo->fields[i];
    }
    return 0;
}

/* ===================== record codec ===================== */

/* offset + size can wrap a uint32 when a copybook declares a huge PIC, which
 * turned the old `offset + size > rec_len` test into a pass and let the codec
 * write past the record. Compare without adding. */
static bool field_fits(const cob_field *f, uint32_t rec_len)
{
    return f->size <= rec_len && f->offset <= rec_len - f->size;
}

bool cobol_get_int(const uint8_t *rec, uint32_t rec_len, const cob_field *f, int64_t *out)
{
    if (!field_fits(f, rec_len)) return false;
    const uint8_t *p = rec + f->offset;
    switch (f->type) {
    case COB_COMP3:
        return comp3_decode(p, f->size, out);
    case COB_ZONED:
        return zoned_decode(p, f->size, out);
    case COB_COMP:
        return comp_decode(p, f->size, f->is_signed, out);
    default:
        return false;
    }
}

bool cobol_set_int(uint8_t *rec, uint32_t rec_len, const cob_field *f, int64_t value)
{
    if (!field_fits(f, rec_len)) return false;
    uint8_t *p = rec + f->offset;
    switch (f->type) {
    case COB_COMP3:
        return comp3_encode(value, p, f->size);
    case COB_ZONED:
        return zoned_encode(value, p, f->size, f->is_signed);
    case COB_COMP:
        return comp_encode(value, p, f->size, f->is_signed);
    default:
        return false;
    }
}

bool cobol_get_text(const uint8_t *rec, uint32_t rec_len, const cob_field *f, uint8_t *out,
                    uint32_t cap, uint32_t *n)
{
    if (f->type != COB_ALPHA) return false;
    if (!field_fits(f, rec_len)) return false;
    if (f->size > cap) return false;
    lg_copy(out, rec + f->offset, f->size);
    *n = f->size;
    return true;
}

bool cobol_set_text(uint8_t *rec, uint32_t rec_len, const cob_field *f, const uint8_t *in,
                    uint32_t n)
{
    if (f->type != COB_ALPHA) return false;
    if (!field_fits(f, rec_len)) return false;
    uint32_t copy = n < f->size ? n : f->size;
    lg_copy(rec + f->offset, in, copy);
    for (uint32_t i = copy; i < f->size; i++) rec[f->offset + i] = ' ';
    return true;
}

/* ===================== record readers ===================== */

const uint8_t *cobol_fixed_record(const uint8_t *buf, uint32_t buf_len, uint32_t rec_size,
                                  uint32_t index, uint32_t *out_len)
{
    if (rec_size == 0) return 0;
    uint32_t start = index * rec_size;
    if (start + rec_size > buf_len) return 0;
    if (out_len) *out_len = rec_size;
    return buf + start;
}

void cobol_rdw_init(cob_rdw_iter *it, const uint8_t *buf, uint32_t len)
{
    it->buf = buf;
    it->len = len;
    it->pos = 0;
}

const uint8_t *cobol_rdw_next(cob_rdw_iter *it, uint32_t *out_len)
{
    if (it->pos + 4 > it->len) return 0;
    uint32_t rdw_len = ((uint32_t) it->buf[it->pos] << 8) | it->buf[it->pos + 1];
    if (rdw_len < 4) return 0;
    if (it->pos + rdw_len > it->len) return 0;
    const uint8_t *payload = it->buf + it->pos + 4;
    if (out_len) *out_len = rdw_len - 4;
    it->pos += rdw_len;
    return payload;
}
