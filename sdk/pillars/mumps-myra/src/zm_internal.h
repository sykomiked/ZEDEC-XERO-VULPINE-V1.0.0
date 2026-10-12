/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zm_internal.h — libzmyra internals (not part of the pillar API; the
 * fuzzer and the M glue include it). Integer code only (R11). */
#ifndef ZM_INTERNAL_H
#define ZM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "zmyra.h"

/* ---- text builder (zm_util.c) ----
 * A growable buffer. Every append checks its room; after the first
 * failure (no memory, or past `limit`) the builder is dead and every
 * later append is a no-op, so callers check `err` once at the end. */
typedef struct {
    char *b;
    size_t n, cap, limit;
    int32_t err; /* ZXV_OK, ZXV_E_NOMEM or ZXV_E_TOO_LONG */
} zm_sb;

void zm_sb_init(zm_sb *sb, size_t limit);
void zm_sb_free(zm_sb *sb);
void zm_sb_put(zm_sb *sb, const char *s, size_t n);
void zm_sb_str(zm_sb *sb, const char *s);
void zm_sb_chr(zm_sb *sb, char c);
void zm_sb_u64(zm_sb *sb, uint64_t v);
/* R4: copy the builder's text out, then free it. Returns sb->err if set. */
int32_t zm_sb_out(zm_sb *sb, char *buf, size_t cap, size_t *out_len);

/* ---- errors (zm_util.c) ---- */
void zm_set_error(const char *msg);
void zm_set_errorf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
/* Fill *d (may be NULL) with a position and a message, and set the last
 * error to the same text. Returns `status`. */
int32_t zm_diag_set(zm_diag *d, int32_t status, const char *text, size_t len, uint64_t offset,
                    const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 6, 7)))
#endif
    ;
size_t zm_utf8_check(const uint8_t *s, size_t n);
int32_t zm_out_text(const char *src, size_t n, char *buf, size_t cap, size_t *out_len);
/* Quote s[0..n) for a message: at most 40 bytes, control bytes as \xNN. */
const char *zm_show(const char *s, size_t n, char *b, size_t cap);

/* ---- numbers and dates (zm_num.c) ---- */
extern const int64_t zm_pow10[19];
/* A canonical number split into its parts. */
typedef struct {
    int neg;
    const char *ip; /* integer digits, no leading zero ("" below 1, "0" for zero) */
    size_t in;
    const char *fp; /* fraction digits, no trailing zero */
    size_t fn;
} zm_cnum;
/* Split s[0..n) if it has the canonical shape (sign, digits, point);
 * the GT.M precision and range limits are checked by zm_is_canonic. */
int zm_cnum_split(const char *s, size_t n, zm_cnum *out);
/* A canonical number into unscaled at `scale`, with at most `digits`
 * total digits. ZM_E_DATA if not canonical, ZM_E_FIT if it does not fit. */
int32_t zm_cnum_to_dec(const char *s, size_t n, uint32_t digits, uint32_t scale,
                       int64_t *out_unscaled);
/* Loose decimal text ("+0012.50", "-.5", "7.") into unscaled at `scale`.
 * ZXV_E_FORMAT if malformed; ZM_E_FIT if a non-zero digit falls past
 * `scale` or it overflows int64. */
int32_t zm_dec_parse(const char *s, size_t n, uint32_t scale, int64_t *out_unscaled);
/* Exact text of unscaled/10^scale: M canonical ("12.5", "-.25") or with
 * all decimals ("12.50", "-0.25"). b must hold 24 bytes. */
size_t zm_dec_mtext(int64_t unscaled, uint32_t scale, char *b);
size_t zm_dec_text(int64_t unscaled, uint32_t scale, char *b);
/* Number of decimal digits of |v| (0 has 1). */
uint32_t zm_digits(int64_t v);

/* ---- schemas (zm_schema.c) ---- */
typedef struct {
    uint32_t line, col;
    uint64_t offset;
} zm_pos;

#define ZM_SUB_KEY 0
#define ZM_SUB_LIT 1

typedef struct {
    int32_t kind;                  /* ZM_SUB_*                                  */
    uint32_t field;                /* KEY: the field's index in the node        */
    int32_t isnum;                 /* LIT: a canonical number                   */
    uint32_t litlen;               /* LIT: bytes                                */
    char lit[ZM_MAX_LITERAL + 1];  /* LIT: the value (numbers in canonical form)*/
} zm_sub;

typedef struct {
    zm_field f;
    zm_pos pos;
} zm_item;

typedef struct {
    uint32_t nsubs;
    zm_sub subs[ZM_MAX_SUBS];
    uint32_t nkeys, npieces, width;
    uint32_t nf, fcap;
    zm_item *f; /* keys (subscript order), then pieces (piece order) */
    zm_pos pos;
} zm_node;

struct zm_schema {
    char gname[ZM_MAX_GNAME + 1];
    uint32_t n, cap;
    zm_node *nodes;
};

/* Schema construction, shared by both front ends. */
zm_schema *zm_schema_new(void);
/* Append an empty node; returns it (NULL: no memory, or too many nodes,
 * reported through *st). */
zm_node *zm_schema_add_node(zm_schema *s, int32_t *st);
/* Append a field to a node. */
int32_t zm_node_add_field(zm_node *nd, const zm_item *it);
/* Validate a parsed schema: types and ranges, unique names, keys bound to
 * subscripts, pieces sorted, no two nodes that could match the same
 * reference. Fills *d with the offending declaration's position. */
int32_t zm_schema_finish(zm_schema *s, const char *text, size_t len, zm_diag *d);

/* Names: 1..30 of A-Z a-z 0-9 '-', a letter first, no '-' last. */
int zm_valid_name(const char *s, size_t n);
/* Global names: '%' or a letter, then letters and digits, 1..31 bytes. */
int zm_valid_gname(const char *s, size_t n);
/* Lower-case copy into dst (n < size). */
void zm_lower(char *dst, const char *s, size_t n);
int zm_ieq(const char *a, const char *b);
/* Check a type's parameters; normalises NUMBER(p,0) to INTEGER(p).
 * Returns NULL or a message. */
const char *zm_type_check(zm_field *f);

/* Text of one value against a field type (pieces and key subscripts):
 * returns ZXV_OK, ZM_E_DATA or ZM_E_FIT with a message in msg. For
 * INT/NUM/DATE, *out_v receives the unscaled value. */
int32_t zm_value_check(const zm_field *f, const char *s, size_t n, int64_t *out_v, char *msg,
                       size_t msg_cap);

/* Does a subscript (s[0..n), a canonical number when isnum) fit key
 * field f? ZXV_OK, or ZM_E_KEY with a message. */
int32_t zm_key_check(const zm_field *f, const char *s, size_t n, int isnum, int64_t *out_v,
                     char *msg, size_t msg_cap);
/* Append a type in MYRA ("t16", "n15.2") or global-map ("NUMBER(15,2)") form. */
void zm_sb_type(zm_sb *sb, const zm_field *f, int gmap);

/* ---- references (zm_ref.c) ---- */
typedef struct {
    const char *s; /* points into the reference text (quoted strings: raw) */
    size_t n;
    int32_t quoted, isnum;
} zm_rsub;

typedef struct {
    const char *g;
    size_t gn;
    uint32_t n;
    zm_rsub sub[ZM_MAX_SUBS];
} zm_ref;

/* Parse a reference. ZM_E_KEY on any malformation (message set). */
int32_t zm_ref_parse(const char *s, size_t n, zm_ref *out);
/* Unquote subscript i into b (cap bytes); returns its length or
 * (size_t)-1 if it does not fit. */
size_t zm_rsub_text(const zm_rsub *r, char *b, size_t cap);
/* Append one subscript as M prints it (number bare, string quoted). */
void zm_sb_sub(zm_sb *sb, const char *s, size_t n, int isnum);
/* Does reference r match node nd? ZXV_OK or ZM_E_KEY with msg. */
int32_t zm_ref_match_node(const zm_schema *s, const zm_node *nd, const zm_ref *r, char *msg,
                          size_t msg_cap);

/* ---- the shared lexer (zm_lex.c) ----
 * Both front ends are line based. A ';' outside a quoted literal starts a
 * comment that runs to the end of the line. */
typedef struct {
    const char *t;
    size_t len, i;
    zm_diag *d;
} zm_lx;

/* Check the whole text (size, NUL bytes, UTF-8) before parsing. */
int32_t zm_lx_start(zm_lx *lx, const char *text, size_t len, zm_diag *d);
void zm_lx_ws(zm_lx *lx);           /* skip spaces, tabs and a CR before LF */
int zm_lx_eol(const zm_lx *lx);     /* at end of line, end of text or ';'   */
void zm_lx_next_line(zm_lx *lx);    /* past the next LF                     */
int32_t zm_lx_err(zm_lx *lx, int32_t st, size_t at, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
/* A word: letters, digits and '-'. Returns its length (0 if none). */
size_t zm_lx_word(zm_lx *lx, const char **out);
/* A field name, lower-cased into dst. */
int32_t zm_lx_name(zm_lx *lx, char dst[ZM_MAX_NAME + 1], zm_pos *pos);
/* "^" and a global name into dst. */
int32_t zm_lx_gname(zm_lx *lx, char dst[ZM_MAX_GNAME + 1]);
/* An unsigned decimal up to `max`. */
int32_t zm_lx_uint(zm_lx *lx, uint32_t max, uint32_t *out, const char *what);
/* A literal subscript: a canonical number or a quoted string. */
int32_t zm_lx_literal(zm_lx *lx, zm_sub *out);
/* Does the next character start a literal? */
int zm_lx_at_literal(const zm_lx *lx);
zm_pos zm_lx_pos(const zm_lx *lx, size_t at);

#endif /* ZM_INTERNAL_H */
