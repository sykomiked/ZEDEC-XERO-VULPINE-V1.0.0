/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* i18n_internal.h — table layouts shared by the generated tables and the
 * i18n code. Not a public interface: callers use i18n.h. The generator
 * (gen_i18n_tables.py) writes rows in exactly these field orders.
 *
 * Every string is an offset into a NUL-separated pool (i18n_pool for locale
 * data, i18n_cpool for catalogs), so identical strings are stored once.
 */
#ifndef ZXV_I18N_INTERNAL_H
#define ZXV_I18N_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include "i18n.h"

#define I18N_NONE16 0xFFFFu

/* Affix control bytes inside number-pattern prefixes and suffixes. */
#define I18N_AFX_CURRENCY 0x01
#define I18N_AFX_MINUS    0x02
#define I18N_AFX_PERCENT  0x03
#define I18N_AFX_PERMILLE 0x04
#define I18N_AFX_PLUS     0x05

/* Date-pattern field bytes (the generator compiles CLDR patterns to these). */
#define I18N_DF_D    0x11 /* d    */
#define I18N_DF_DD   0x12 /* dd   */
#define I18N_DF_M    0x13 /* M, L */
#define I18N_DF_MM   0x14 /* MM   */
#define I18N_DF_MMM  0x15 /* MMM  */
#define I18N_DF_MMMM 0x16 /* MMMM */
#define I18N_DF_LLL  0x17 /* stand-alone abbreviated month */
#define I18N_DF_LLLL 0x18 /* stand-alone wide month */
#define I18N_DF_Y    0x19 /* y    */
#define I18N_DF_YY   0x1A /* yy   */
#define I18N_DF_YYYY 0x1B /* yyyy */
#define I18N_DF_E    0x1C /* E, EEE (abbreviated weekday) */
#define I18N_DF_EEEE 0x1D /* EEEE (wide weekday)          */
#define I18N_DF_ERA  0x1E /* G    */

struct i18n_loc {
    uint32_t tag, english, autonym, script, territories, au_states, region_currency;
    uint16_t parent, catalog, plural, numfmt, datefmt, symset, covset;
    uint8_t rtl, coverage, flags;
};
typedef struct i18n_loc i18n_loc_t;

typedef struct {
    uint32_t numsys, decimal, group, minus, plus, percent, ins_before, ins_after;
    /* affixes: decimal, percent, currency, currency-alpha; each pos-prefix,
     * pos-suffix, neg-prefix, neg-suffix */
    uint32_t affix[16];
    uint16_t digitset;
    uint8_t min_group;
    uint8_t groups[4]; /* low nibble primary, high nibble secondary; 0 = none */
} i18n_numfmt_t;

typedef struct {
    uint32_t pattern[4]; /* full, long, medium, short */
    uint16_t mon_abbr, mon_wide, mon_sa_abbr, mon_sa_wide, day_abbr, day_wide;
    uint32_t era;
} i18n_datefmt_t;

typedef struct {
    uint8_t ncats;
    uint8_t cat[5];    /* i18n_plural_t, 255 = unused */
    uint16_t first[5]; /* first relation of each category's rule */
    uint8_t count[5];  /* relations in the rule */
} i18n_plural_rules_t;

typedef struct {
    uint8_t operand; /* n i v w f t e c */
    uint8_t neg;     /* != instead of = */
    uint8_t and_next;
    uint8_t nranges;
    uint16_t range0;
    uint32_t mod; /* 0 = no modulus */
} i18n_plural_rel_t;

typedef struct {
    uint32_t start;
    uint16_t count;
} i18n_symset_t;

typedef struct {
    char code[4];
    uint32_t symbol, narrow;
    uint8_t flags; /* 1 symbol starts S/Z, 2 symbol ends S/Z, 4 and 8 the same for narrow */
} i18n_sym_t;

typedef struct {
    char code[4];
    uint8_t digits;
} i18n_curdigits_t;

typedef struct {
    char from[4];
    uint16_t loc;
} i18n_alias_t;

typedef struct {
    char key[12];
    uint16_t loc;
} i18n_hint_t;

typedef struct {
    uint32_t start;
    uint16_t count, gaps, ncps;
} i18n_covset_t;

typedef struct {
    uint16_t id;
    uint8_t cat; /* i18n_plural_t, 255 for a plain message */
    uint8_t status;
    uint32_t note, text; /* offsets into i18n_cpool */
} i18n_entry_t;

typedef struct {
    uint32_t tag, start, count;
} i18n_catalog_t;

extern const char i18n_cldr_version[];
extern const char i18n_unicode_version[];
extern const char *const i18n_pool;
extern const i18n_loc_t i18n_locs[];
extern const uint32_t i18n_loc_count;
extern const i18n_numfmt_t i18n_numfmts[];
extern const uint32_t i18n_digitsets[][10];
extern const i18n_datefmt_t i18n_datefmts[];
extern const uint32_t i18n_monthsets[][12];
extern const uint32_t i18n_daysets[][7];
extern const i18n_plural_rules_t i18n_plurals[];
extern const i18n_plural_rel_t i18n_plural_rels[];
extern const i18n_range_t i18n_plural_ranges[];
extern const uint16_t i18n_root_symset;
extern const i18n_symset_t i18n_symsets[];
extern const i18n_sym_t i18n_syms[];
extern const i18n_curdigits_t i18n_curdigits[];
extern const uint32_t i18n_curdigits_count;
extern const i18n_alias_t i18n_aliases[];
extern const uint32_t i18n_alias_count;
extern const i18n_hint_t i18n_hints[];
extern const uint32_t i18n_hint_count;
extern const i18n_range_t i18n_extend[];
extern const uint32_t i18n_extend_count;
extern const i18n_covset_t i18n_covsets[];
extern const i18n_range_t i18n_cov_ranges[];

extern const char *const i18n_cpool;
extern const i18n_entry_t i18n_entries[];
extern const i18n_catalog_t i18n_catalogs[];
extern const uint32_t i18n_catalog_count;
extern const char *const *const i18n_msg_keys;

/* shared helpers (i18n_text.c) */
uint32_t i18n__strlen(const char *s);
bool i18n__streq(const char *a, const char *b);
/* x mod m without a division instruction (bitwise long division). */
uint32_t i18n__mod_u64(uint64_t x, uint32_t m);
/* Bounded writer. */
typedef struct {
    char *buf;
    uint32_t cap, len;
    bool overflow;
    uint8_t next[4]; /* first bytes that did not fit (for the boundary check) */
    uint8_t next_n;
} i18n_w_t;
void i18n__w_init(i18n_w_t *w, char *buf, uint32_t cap);
void i18n__w_bytes(i18n_w_t *w, const char *s, uint32_t n);
void i18n__w_str(i18n_w_t *w, const char *s);
void i18n__w_cp(i18n_w_t *w, uint32_t cp);
/* NUL-terminates; on overflow cuts back to a grapheme boundary and returns -1. */
int32_t i18n__w_end(i18n_w_t *w);

#endif /* ZXV_I18N_INTERNAL_H */
