/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* i18n_locale.c — the locale registry, tag resolution, CLDR plural rules and
 * message lookup with fallback. See i18n.h. */
#include "i18n_internal.h"

#define POOL(off) (i18n_pool + (off))

/* ===== registry ===== */

uint32_t i18n_locale_count(void)
{
    return i18n_loc_count;
}

const i18n_locale_t *i18n_locale_at(uint32_t i)
{
    return i < i18n_loc_count ? &i18n_locs[i] : 0;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

/* Compare a BCP 47 tag ignoring case and treating '_' as '-'; `n` limits a. */
static bool tag_eq_n(const char *a, uint32_t n, const char *b)
{
    for (uint32_t i = 0; i < n; i++) {
        if (!a[i] || !b[i]) return false;
        char x = a[i] == '_' ? '-' : lower(a[i]);
        char y = b[i] == '_' ? '-' : lower(b[i]);
        if (x != y) return false;
    }
    return b[n] == 0;
}

static const i18n_locale_t *find_n(const char *tag, uint32_t n)
{
    /* the table is sorted by tag in ASCII order; a linear scan keeps this
     * independent of case folding and is fast enough for 800 entries */
    for (uint32_t i = 0; i < i18n_loc_count; i++)
        if (tag_eq_n(tag, n, POOL(i18n_locs[i].tag))) return &i18n_locs[i];
    return 0;
}

const i18n_locale_t *i18n_locale_find(const char *tag)
{
    if (!tag) return 0;
    return find_n(tag, i18n__strlen(tag));
}

const i18n_locale_t *i18n_locale_default(void)
{
    static const i18n_locale_t *en;
    if (!en) en = i18n_locale_find("en");
    return en;
}

/* Length of the first subtag. */
static uint32_t subtag_len(const char *s)
{
    uint32_t n = 0;
    while (s[n] && s[n] != '-' && s[n] != '_') n++;
    return n;
}

const i18n_locale_t *i18n_locale_resolve(const char *tag)
{
    char buf[48];
    uint32_t n;
    const i18n_locale_t *l;
    if (!tag || !tag[0]) return i18n_locale_default();
    n = i18n__strlen(tag);
    if (n >= sizeof buf) n = sizeof buf - 1;
    for (uint32_t i = 0; i < n; i++) buf[i] = tag[i] == '_' ? '-' : tag[i];
    buf[n] = 0;
    /* strip extensions and private use (-u-, -x-, ...): single-letter subtags */
    for (uint32_t i = 0; i + 2 < n; i++)
        if (buf[i] == '-' && buf[i + 2] == '-') {
            n = i;
            buf[n] = 0;
            break;
        }
    if (n >= 2 && buf[n - 2] == '-') {
        n -= 2;
        buf[n] = 0;
    }
    if ((l = find_n(buf, n))) return l;
    /* language alias: tl -> fil, iw -> he, ... */
    uint32_t ll = subtag_len(buf);
    for (uint32_t i = 0; i < i18n_alias_count; i++) {
        if (tag_eq_n(buf, ll, i18n_aliases[i].from)) {
            const i18n_locale_t *a = &i18n_locs[i18n_aliases[i].loc];
            const char *at = POOL(a->tag);
            uint32_t al = i18n__strlen(at), rest = n - ll;
            if (al + rest < sizeof buf) {
                char t[48];
                for (uint32_t k = 0; k < al; k++) t[k] = at[k];
                for (uint32_t k = 0; k < rest; k++) t[al + k] = buf[ll + k];
                t[al + rest] = 0;
                for (uint32_t k = 0; k <= al + rest; k++) buf[k] = t[k];
                n = al + rest;
                if ((l = find_n(buf, n))) return l;
            }
            break;
        }
    }
    /* lang-REGION where the region implies a different script */
    for (uint32_t i = 0; i < i18n_hint_count; i++) {
        const char *k = i18n_hints[i].key;
        uint32_t kl = i18n__strlen(k);
        if (kl <= n && tag_eq_n(buf, kl, k) && (buf[kl] == 0 || buf[kl] == '-')) {
            const i18n_locale_t *h = &i18n_locs[i18n_hints[i].loc];
            /* try lang-Script-REGION first */
            char t[48];
            const char *ht = POOL(h->tag);
            uint32_t hl = i18n__strlen(ht), rl = kl - ll;
            if (hl + rl < sizeof t) {
                for (uint32_t j = 0; j < hl; j++) t[j] = ht[j];
                for (uint32_t j = 0; j < rl; j++) t[hl + j] = buf[ll + j];
                t[hl + rl] = 0;
                if ((l = find_n(t, hl + rl))) return l;
            }
            return h;
        }
    }
    /* truncate from the right */
    while (n > 0) {
        while (n > 0 && buf[n - 1] != '-') n--;
        if (n == 0) break;
        n--;
        if ((l = find_n(buf, n))) return l;
    }
    return i18n_locale_default();
}

const i18n_locale_t *i18n_locale_parent(const i18n_locale_t *l)
{
    if (!l || l->parent == I18N_NONE16) return 0;
    return &i18n_locs[l->parent];
}

const char *i18n_locale_tag(const i18n_locale_t *l)
{
    return l ? POOL(l->tag) : "";
}
const char *i18n_locale_english(const i18n_locale_t *l)
{
    return l ? POOL(l->english) : "";
}
const char *i18n_locale_autonym(const i18n_locale_t *l)
{
    return l ? POOL(l->autonym) : "";
}
const char *i18n_locale_script(const i18n_locale_t *l)
{
    return l ? POOL(l->script) : "";
}
const char *i18n_locale_territories(const i18n_locale_t *l)
{
    return l ? POOL(l->territories) : "";
}
const char *i18n_locale_au_states(const i18n_locale_t *l)
{
    return l ? POOL(l->au_states) : "";
}
const char *i18n_locale_currency(const i18n_locale_t *l)
{
    return l ? POOL(l->region_currency) : "";
}
const char *i18n_locale_numbering(const i18n_locale_t *l)
{
    return l ? POOL(i18n_numfmts[l->numfmt].numsys) : "";
}
bool i18n_locale_rtl(const i18n_locale_t *l)
{
    return l && l->rtl;
}
uint32_t i18n_locale_flags(const i18n_locale_t *l)
{
    return l ? l->flags : 0;
}
i18n_coverage_t i18n_locale_coverage(const i18n_locale_t *l)
{
    return l ? (i18n_coverage_t) l->coverage : I18N_COVERAGE_NONE;
}
uint32_t i18n_locale_digit(const i18n_locale_t *l, uint32_t d)
{
    if (!l || d > 9) return '0' + (d > 9 ? 0 : d);
    return i18n_digitsets[i18n_numfmts[l->numfmt].digitset][d];
}

uint32_t i18n_locale_needs(const i18n_locale_t *l, const i18n_range_t **ranges, uint32_t *gaps)
{
    if (!l || l->covset == I18N_NONE16) {
        if (ranges) *ranges = 0;
        if (gaps) *gaps = 0;
        return 0;
    }
    const i18n_covset_t *c = &i18n_covsets[l->covset];
    if (ranges) *ranges = &i18n_cov_ranges[c->start];
    if (gaps) *gaps = c->gaps;
    return c->count;
}

const char *i18n_data_version(void)
{
    return "CLDR 48.2.0, Unicode 15.1.0";
}

/* ===== plurals ===== */

typedef struct {
    uint64_t n_int; /* integer part (n when f == 0) */
    uint64_t i, v, w, f, t;
    bool n_is_int;
} operands_t;

static uint64_t operand(const operands_t *o, uint8_t which)
{
    switch (which) {
    case 0:
        return o->n_int;
    case 1:
        return o->i;
    case 2:
        return o->v;
    case 3:
        return o->w;
    case 4:
        return o->f;
    case 5:
        return o->t;
    default:
        return 0; /* e, c: no compact exponent */
    }
}

static bool relation(const i18n_plural_rel_t *r, const operands_t *o)
{
    uint64_t x = operand(o, r->operand);
    bool in = false;
    if (r->mod) x = i18n__mod_u64(x, r->mod);
    if (r->operand == 0 && !o->n_is_int) {
        in = false; /* a non-integer n is never in an integer range */
    } else {
        for (uint32_t k = 0; k < r->nranges; k++) {
            const i18n_range_t *g = &i18n_plural_ranges[r->range0 + k];
            if (x >= g->lo && x <= g->hi) {
                in = true;
                break;
            }
        }
    }
    return r->neg ? !in : in;
}

static bool rule(uint16_t first, uint8_t count, const operands_t *o)
{
    /* OR of AND-chains */
    uint32_t k = 0;
    while (k < count) {
        bool all = true;
        for (;;) {
            const i18n_plural_rel_t *r = &i18n_plural_rels[first + k];
            if (!relation(r, o)) all = false;
            k++;
            if (!r->and_next || k >= count) break;
        }
        if (all) return true;
    }
    return false;
}

static i18n_plural_t select_plural(const i18n_locale_t *l, const operands_t *o)
{
    if (!l) l = i18n_locale_default();
    const i18n_plural_rules_t *p = &i18n_plurals[l->plural];
    for (uint32_t c = 0; c < p->ncats; c++)
        if (rule(p->first[c], p->count[c], o)) return (i18n_plural_t) p->cat[c];
    return I18N_OTHER;
}

i18n_plural_t i18n_plural(const i18n_locale_t *l, uint64_t n)
{
    operands_t o = {n, n, 0, 0, 0, 0, true};
    return select_plural(l, &o);
}

i18n_plural_t i18n_plural_decimal(const i18n_locale_t *l, uint64_t scaled, uint32_t frac_digits)
{
    uint8_t dig[20];
    uint32_t nd = 0;
    operands_t o;
    uint64_t x = scaled;
    /* decimal digits by repeated subtraction of powers of ten (no division) */
    static const uint64_t P10[20] = {1ull,
                                     10ull,
                                     100ull,
                                     1000ull,
                                     10000ull,
                                     100000ull,
                                     1000000ull,
                                     10000000ull,
                                     100000000ull,
                                     1000000000ull,
                                     10000000000ull,
                                     100000000000ull,
                                     1000000000000ull,
                                     10000000000000ull,
                                     100000000000000ull,
                                     1000000000000000ull,
                                     10000000000000000ull,
                                     100000000000000000ull,
                                     1000000000000000000ull,
                                     10000000000000000000ull};
    if (frac_digits > 18) frac_digits = 18;
    for (int k = 19; k >= 0; k--) {
        uint8_t d = 0;
        while (x >= P10[k]) {
            x -= P10[k];
            d++;
        }
        if (d || nd || k == 0) dig[nd++] = d;
    }
    while (nd <= frac_digits) { /* left-pad so there is an integer digit */
        for (uint32_t k = nd; k > 0; k--) dig[k] = dig[k - 1];
        dig[0] = 0;
        nd++;
    }
    o.i = 0;
    for (uint32_t k = 0; k < nd - frac_digits; k++) o.i = o.i * 10u + dig[k];
    o.f = 0;
    for (uint32_t k = nd - frac_digits; k < nd; k++) o.f = o.f * 10u + dig[k];
    o.v = frac_digits;
    uint32_t tw = frac_digits;
    while (tw > 0 && dig[nd - frac_digits + tw - 1] == 0) tw--;
    o.w = tw;
    o.t = 0;
    for (uint32_t k = 0; k < tw; k++) o.t = o.t * 10u + dig[nd - frac_digits + k];
    o.n_is_int = (o.f == 0);
    o.n_int = o.i;
    return select_plural(l, &o);
}

uint32_t i18n_plural_categories(const i18n_locale_t *l)
{
    if (!l) l = i18n_locale_default();
    const i18n_plural_rules_t *p = &i18n_plurals[l->plural];
    uint32_t m = 1u << I18N_OTHER;
    for (uint32_t c = 0; c < p->ncats; c++) m |= 1u << p->cat[c];
    return m;
}

/* ===== messages ===== */

static const i18n_entry_t *cat_find(uint16_t cat, uint16_t id, uint8_t form)
{
    const i18n_catalog_t *c = &i18n_catalogs[cat];
    uint32_t lo = c->start, hi = c->start + c->count;
    /* binary search for the first entry with this id */
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        if (i18n_entries[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    const i18n_entry_t *other = 0;
    for (uint32_t k = lo; k < c->start + c->count && i18n_entries[k].id == id; k++) {
        if (i18n_entries[k].cat == form) return &i18n_entries[k];
        if (i18n_entries[k].cat == I18N_OTHER) other = &i18n_entries[k];
    }
    return form == 255 ? 0 : other;
}

static const char *lookup(const i18n_locale_t *l, i18n_msgid_t id, uint8_t form,
                          i18n_msg_info_t *info)
{
    const i18n_locale_t *asked = l ? l : i18n_locale_default();
    const i18n_locale_t *en = i18n_locale_default();
    const i18n_locale_t *c = asked;
    bool en_seen = false;
    if ((uint32_t) id >= (uint32_t) I18N_MSG__COUNT) id = (i18n_msgid_t) 0;
    while (c) {
        if (c == en) en_seen = true;
        if (c->catalog != I18N_NONE16) {
            /* a fallback catalog may lack the asked plural form: cat_find
             * then returns its "other" form */
            const i18n_entry_t *e = cat_find(c->catalog, (uint16_t) id, form);
            if (e) {
                if (info) {
                    info->from = c;
                    info->review = (i18n_review_t) e->status;
                    info->note = i18n_cpool + e->note;
                    info->fallback = c != asked;
                }
                return i18n_cpool + e->text;
            }
        }
        c = i18n_locale_parent(c);
        if (!c && !en_seen) c = en;
    }
    if (info) {
        info->from = en;
        info->review = I18N_REVIEW_SOURCE;
        info->note = "";
        info->fallback = true;
    }
    return i18n_msg_keys[id]; /* only reachable if en.msg lacks the id */
}

const char *i18n_msg(const i18n_locale_t *l, i18n_msgid_t id, i18n_msg_info_t *info)
{
    const char *s = lookup(l, id, 255, info);
    return s;
}

const char *i18n_msg_plural(const i18n_locale_t *l, i18n_msgid_t id, uint64_t n,
                            i18n_msg_info_t *info)
{
    i18n_msg_info_t tmp;
    i18n_msg_info_t *pi = info ? info : &tmp;
    uint8_t form = (uint8_t) i18n_plural(l ? l : i18n_locale_default(), n);
    const char *s = lookup(l, id, form, pi);
    if (pi->from != (l ? l : i18n_locale_default())) {
        /* fell back to another language: choose that language's form */
        uint8_t f2 = (uint8_t) i18n_plural(pi->from, n);
        if (f2 != form) s = lookup(pi->from, id, f2, pi), pi->fallback = true;
    }
    return s;
}

const char *i18n_msg_key(i18n_msgid_t id)
{
    if ((uint32_t) id >= (uint32_t) I18N_MSG__COUNT) return "";
    return i18n_msg_keys[id];
}

int32_t i18n_msg_id(const char *key)
{
    for (uint32_t i = 0; i < (uint32_t) I18N_MSG__COUNT; i++)
        if (i18n__streq(i18n_msg_keys[i], key)) return (int32_t) i;
    return -1;
}
