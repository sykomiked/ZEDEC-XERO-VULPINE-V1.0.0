/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxt.c — treaty records and Sutra text: validate and lower (see zxt.h).
 * Freestanding: no libc, no allocation, no recursion, no floating point. */
#include "zxt.h"

#define REC_OFF_NAME 4u
#define REC_OFF_ASSET 34u
#define REC_OFF_OP 37u
#define REC_OFF_NPARTY 41u
#define REC_OFF_NOWE 43u
#define REC_OFF_OWES (ZXT_REC_HEAD + ZXT_MAX_PARTIES * ZXT_REC_PARTY)

/* ------------------------------------------------------------ small helpers */

static void zero_bytes(void *p, uint32_t n)
{
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (uint32_t i = 0; i < n; i++)
        b[i] = 0;
}

void zxt_clear(zxt_treaty_t *t)
{
    if (t)
        zero_bytes(t, (uint32_t)sizeof(*t));
}

static bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_name_char(char c) { return is_upper(c) || is_digit(c) || c == '-'; }

/* A COBOL-style name: 1..max of [A-Z0-9-], starting with a letter, not
 * ending in '-', NUL-terminated inside `size` bytes and zero after. */
static bool name_ok(const char *s, uint32_t max, uint32_t size)
{
    uint32_t n = 0;
    while (n < size && s[n] != 0)
        n++;
    if (n == 0 || n > max || n >= size)
        return false;
    if (!is_upper(s[0]) || s[n - 1] == '-')
        return false;
    for (uint32_t i = 0; i < n; i++)
        if (!is_name_char(s[i]))
            return false;
    for (uint32_t i = n; i < size; i++)
        if (s[i] != 0)
            return false;
    return true;
}

static bool str_eq(const char *a, const char *b, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

static uint64_t gcd_u64(uint64_t a, uint64_t b)
{
    while (b != 0) {
        uint64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

/* ------------------------------------------------------------ PIC S9(18) */

bool zxt_pic_ok(int64_t v) { return v >= -ZXT_PIC18_MAX && v <= ZXT_PIC18_MAX; }

bool zxt_pic_add(int64_t a, int64_t b, int64_t *r)
{
    if (!r || !zxt_pic_ok(a) || !zxt_pic_ok(b))
        return false;
    int64_t s = a + b; /* |s| < 2 * 10^18 < 2^63 */
    if (!zxt_pic_ok(s))
        return false;
    *r = s;
    return true;
}

bool zxt_pic_sub(int64_t a, int64_t b, int64_t *r)
{
    if (!zxt_pic_ok(b))
        return false;
    return zxt_pic_add(a, -b, r);
}

bool zxt_pic_mul(int64_t a, int64_t b, int64_t *r)
{
    int64_t p;
    if (!r || !zxt_pic_ok(a) || !zxt_pic_ok(b))
        return false;
    if (__builtin_mul_overflow(a, b, &p) || !zxt_pic_ok(p))
        return false;
    *r = p;
    return true;
}

bool zxt_pic_div(int64_t a, int64_t b, int64_t *q, int64_t *rem)
{
    if (!q || !rem || !zxt_pic_ok(a) || !zxt_pic_ok(b) || b == 0)
        return false;
    *q = a / b; /* C99 truncates toward zero, as COBOL DIVIDE does */
    *rem = a % b;
    return true;
}

/* ------------------------------------------------------------ the rules */

static zxt_status_t chk_header(const zxt_treaty_t *t)
{
    if (!name_ok(t->name, ZXT_NAME_MAX, ZXT_NAME_MAX + 1u))
        return ZXT_E_NAME;
    for (uint32_t i = 0; i < 3u; i++)
        if (!is_upper(t->asset[i]))
            return ZXT_E_ASSET;
    if (t->asset[3] != 0)
        return ZXT_E_ASSET;
    if (t->op != ZXT_OP_NET && t->op != ZXT_OP_SETL)
        return ZXT_E_OP;
    return ZXT_OK;
}

/* Party i on its own and against parties 0..i-1. */
static zxt_status_t chk_party(const zxt_treaty_t *t, uint32_t i)
{
    const zxt_party_t *p = &t->party[i];
    if (!name_ok(p->id, ZXT_ID_MAX, ZXT_ID_MAX + 1u))
        return ZXT_E_PARTY;
    for (uint32_t k = 0; k < i; k++)
        if (str_eq(t->party[k].id, p->id, ZXT_ID_MAX + 1u))
            return ZXT_E_PARTY;
    if (p->deposit < 0 || !zxt_pic_ok(p->deposit) || p->claim < 0 || !zxt_pic_ok(p->claim))
        return ZXT_E_NUMBER;
    if (p->claim > p->deposit)
        return ZXT_E_CLAIM;
    if (p->w_den == 0 || p->w_den > ZXT_WEIGHT_MAX || p->w_num > p->w_den ||
        gcd_u64(p->w_num, p->w_den) != 1)
        return ZXT_E_WEIGHT;
    return ZXT_OK;
}

/* R1: the weights of parties 0..n_party-1 sum to exactly 1. */
static zxt_status_t chk_wsum(const zxt_treaty_t *t)
{
    uint64_t an = 0, ad = 1; /* running sum an/ad, lowest terms, <= 1 */
    for (uint32_t i = 0; i < t->n_party; i++) {
        uint64_t n = t->party[i].w_num, d = t->party[i].w_den, l, x, y, s, g;
        if (__builtin_mul_overflow(ad / gcd_u64(ad, d), d, &l)) /* l = lcm(ad, d) */
            return ZXT_E_WEIGHT;
        if (__builtin_mul_overflow(an, l / ad, &x) || __builtin_mul_overflow(n, l / d, &y) ||
            __builtin_add_overflow(x, y, &s))
            return ZXT_E_WEIGHT;
        g = gcd_u64(s, l);
        an = s / g;
        ad = l / g;
        if (an > ad)
            return ZXT_E_WEIGHT;
    }
    return (an == 1 && ad == 1) ? ZXT_OK : ZXT_E_WEIGHT;
}

static zxt_status_t chk_owe(const zxt_treaty_t *t, uint32_t j)
{
    const zxt_owe_t *o = &t->owe[j];
    if (o->from >= t->n_party || o->to >= t->n_party)
        return ZXT_E_PARTY;
    if (o->from == o->to)
        return ZXT_E_OWE;
    if (!zxt_pic_ok(o->amount))
        return ZXT_E_NUMBER;
    if (o->amount <= 0)
        return ZXT_E_OWE;
    return ZXT_OK;
}

static bool party_blank(const zxt_party_t *p)
{
    for (uint32_t i = 0; i <= ZXT_ID_MAX; i++)
        if (p->id[i] != 0)
            return false;
    return p->deposit == 0 && p->claim == 0 && p->w_num == 0 && p->w_den == 0;
}

static bool owe_blank(const zxt_owe_t *o) { return o->from == 0 && o->to == 0 && o->amount == 0; }

zxt_status_t zxt_validate(const zxt_treaty_t *t)
{
    zxt_status_t st;
    if (!t)
        return ZXT_E_ARG;
    if ((st = chk_header(t)) != ZXT_OK)
        return st;
    if (t->n_party < 1 || t->n_party > ZXT_MAX_PARTIES || t->n_owe > ZXT_MAX_OWES)
        return ZXT_E_COUNT;
    for (uint32_t i = 0; i < ZXT_MAX_PARTIES; i++) {
        if (i >= t->n_party) {
            if (!party_blank(&t->party[i]))
                return ZXT_E_PAD;
        } else if ((st = chk_party(t, i)) != ZXT_OK) {
            return st;
        }
    }
    if ((st = chk_wsum(t)) != ZXT_OK)
        return st;
    for (uint32_t j = 0; j < ZXT_MAX_OWES; j++) {
        if (j >= t->n_owe) {
            if (!owe_blank(&t->owe[j]))
                return ZXT_E_PAD;
        } else if ((st = chk_owe(t, j)) != ZXT_OK) {
            return st;
        }
    }
    return ZXT_OK;
}

bool zxt_equal(const zxt_treaty_t *a, const zxt_treaty_t *b)
{
    if (!a || !b)
        return false;
    if (!str_eq(a->name, b->name, ZXT_NAME_MAX + 1u) || !str_eq(a->asset, b->asset, 4u) ||
        a->op != b->op || a->n_party != b->n_party || a->n_owe != b->n_owe)
        return false;
    for (uint32_t i = 0; i < ZXT_MAX_PARTIES; i++) {
        const zxt_party_t *p = &a->party[i], *q = &b->party[i];
        if (!str_eq(p->id, q->id, ZXT_ID_MAX + 1u) || p->deposit != q->deposit ||
            p->claim != q->claim || p->w_num != q->w_num || p->w_den != q->w_den)
            return false;
    }
    for (uint32_t j = 0; j < ZXT_MAX_OWES; j++) {
        const zxt_owe_t *o = &a->owe[j], *r = &b->owe[j];
        if (o->from != r->from || o->to != r->to || o->amount != r->amount)
            return false;
    }
    return true;
}

/* ------------------------------------------------------------ RECORD form */

/* n ASCII digits, exactly. */
static bool rec_udigits(const uint8_t *s, uint32_t n, uint64_t *v)
{
    uint64_t x = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!is_digit((char)s[i]))
            return false;
        x = x * 10u + (uint64_t)(s[i] - '0');
    }
    *v = x;
    return true;
}

/* PIC S9(18) SIGN LEADING SEPARATE: '+' or '-' then 18 digits; no "-0". */
static bool rec_s18(const uint8_t *s, int64_t *v)
{
    uint64_t m;
    if ((s[0] != '+' && s[0] != '-') || !rec_udigits(s + 1, 18u, &m))
        return false;
    if (s[0] == '-' && m == 0)
        return false;
    *v = s[0] == '-' ? -(int64_t)m : (int64_t)m;
    return true;
}

/* A space-padded X(size) field holding a name of at most `max` chars:
 * copied up to the first space; everything after it must be spaces. The
 * name itself is judged later by name_ok. */
static bool rec_name(const uint8_t *s, uint32_t size, char *dst)
{
    uint32_t n = 0;
    while (n < size && s[n] != ' ') {
        if (s[n] == 0)
            return false;
        dst[n] = (char)s[n];
        n++;
    }
    for (uint32_t i = n; i < size; i++)
        if (s[i] != ' ')
            return false;
    return true;
}

static bool rec_is(const uint8_t *s, const char *lit, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (s[i] != (uint8_t)lit[i])
            return false;
    return true;
}

static bool rec_all(const uint8_t *s, uint8_t c, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (s[i] != c)
            return false;
    return true;
}

/* An unused signed slot: "+000000000000000000". */
static bool rec_zero_s18(const uint8_t *s) { return s[0] == '+' && rec_all(s + 1, '0', 18u); }

static zxt_status_t rec_fail(zxt_treaty_t *out, uint32_t *err_off, zxt_status_t st, uint32_t off)
{
    zxt_clear(out);
    if (err_off)
        *err_off = off;
    return st;
}

zxt_status_t zxt_parse_record(const uint8_t *rec, uint32_t len, zxt_treaty_t *out,
                              uint32_t *err_off)
{
    uint64_t u;
    zxt_status_t st;
    if (err_off)
        *err_off = 0;
    if (!out)
        return ZXT_E_ARG;
    zxt_clear(out);
    if (!rec || len != ZXT_REC_LEN)
        return ZXT_E_ARG;
    if (!rec_is(rec, ZXT_REC_MAGIC, 4u))
        return rec_fail(out, err_off, ZXT_E_MAGIC, 0);
    if (!rec_name(rec + REC_OFF_NAME, ZXT_NAME_MAX, out->name))
        return rec_fail(out, err_off, ZXT_E_NAME, REC_OFF_NAME);
    for (uint32_t i = 0; i < 3u; i++)
        out->asset[i] = (char)rec[REC_OFF_ASSET + i];
    if (rec_is(rec + REC_OFF_OP, "NET ", 4u))
        out->op = ZXT_OP_NET;
    else if (rec_is(rec + REC_OFF_OP, "SETL", 4u))
        out->op = ZXT_OP_SETL;
    else
        return rec_fail(out, err_off, ZXT_E_OP, REC_OFF_OP);
    if ((st = chk_header(out)) != ZXT_OK)
        return rec_fail(out, err_off, st, st == ZXT_E_NAME ? REC_OFF_NAME : REC_OFF_ASSET);
    if (!rec_udigits(rec + REC_OFF_NPARTY, 2u, &u) || u < 1 || u > ZXT_MAX_PARTIES)
        return rec_fail(out, err_off, ZXT_E_COUNT, REC_OFF_NPARTY);
    out->n_party = (uint8_t)u;
    if (!rec_udigits(rec + REC_OFF_NOWE, 3u, &u) || u > ZXT_MAX_OWES)
        return rec_fail(out, err_off, ZXT_E_COUNT, REC_OFF_NOWE);
    out->n_owe = (uint8_t)u;

    for (uint32_t i = 0; i < ZXT_MAX_PARTIES; i++) {
        uint32_t off = ZXT_REC_HEAD + i * ZXT_REC_PARTY;
        const uint8_t *s = rec + off;
        if (i >= out->n_party) {
            if (!rec_all(s, ' ', ZXT_ID_MAX) || !rec_zero_s18(s + 8) || !rec_zero_s18(s + 27) ||
                !rec_all(s + 46, '0', 18u))
                return rec_fail(out, err_off, ZXT_E_PAD, off);
            continue;
        }
        zxt_party_t *p = &out->party[i];
        uint64_t wn, wd;
        if (!rec_name(s, ZXT_ID_MAX, p->id))
            return rec_fail(out, err_off, ZXT_E_PARTY, off);
        if (!rec_s18(s + 8, &p->deposit))
            return rec_fail(out, err_off, ZXT_E_NUMBER, off + 8u);
        if (!rec_s18(s + 27, &p->claim))
            return rec_fail(out, err_off, ZXT_E_NUMBER, off + 27u);
        if (!rec_udigits(s + 46, 9u, &wn) || !rec_udigits(s + 55, 9u, &wd))
            return rec_fail(out, err_off, ZXT_E_NUMBER, off + 46u);
        p->w_num = (uint32_t)wn;
        p->w_den = (uint32_t)wd;
        if ((st = chk_party(out, i)) != ZXT_OK)
            return rec_fail(out, err_off, st, off);
    }
    if ((st = chk_wsum(out)) != ZXT_OK)
        return rec_fail(out, err_off, st, ZXT_REC_HEAD);

    for (uint32_t j = 0; j < ZXT_MAX_OWES; j++) {
        uint32_t off = REC_OFF_OWES + j * ZXT_REC_OWE;
        const uint8_t *s = rec + off;
        if (j >= out->n_owe) {
            if (!rec_all(s, '0', 4u) || !rec_zero_s18(s + 4))
                return rec_fail(out, err_off, ZXT_E_PAD, off);
            continue;
        }
        uint64_t f, to;
        if (!rec_udigits(s, 2u, &f) || !rec_udigits(s + 2, 2u, &to) || !rec_s18(s + 4, &out->owe[j].amount))
            return rec_fail(out, err_off, ZXT_E_NUMBER, off);
        if (f < 1 || to < 1)
            return rec_fail(out, err_off, ZXT_E_PARTY, off);
        out->owe[j].from = (uint8_t)(f - 1u);
        out->owe[j].to = (uint8_t)(to - 1u);
        if ((st = chk_owe(out, j)) != ZXT_OK)
            return rec_fail(out, err_off, st, off);
    }
    if ((st = zxt_validate(out)) != ZXT_OK) /* the same rules once more, whole */
        return rec_fail(out, err_off, st, len);
    return ZXT_OK;
}

static void put_udigits(uint8_t *d, uint64_t v, uint32_t n)
{
    for (uint32_t i = n; i-- > 0;) {
        d[i] = (uint8_t)('0' + v % 10u);
        v /= 10u;
    }
}

static void put_s18(uint8_t *d, int64_t v)
{
    d[0] = v < 0 ? '-' : '+';
    put_udigits(d + 1, (uint64_t)(v < 0 ? -v : v), 18u);
}

static void put_name(uint8_t *d, const char *s, uint32_t size)
{
    uint32_t i = 0;
    for (; i < size && s[i] != 0; i++)
        d[i] = (uint8_t)s[i];
    for (; i < size; i++)
        d[i] = ' ';
}

zxt_status_t zxt_emit_record(const zxt_treaty_t *t, uint8_t *rec, uint32_t cap)
{
    zxt_status_t st;
    if (!rec)
        return ZXT_E_ARG;
    if ((st = zxt_validate(t)) != ZXT_OK)
        return st;
    if (cap < ZXT_REC_LEN)
        return ZXT_E_CAP;
    for (uint32_t i = 0; i < 4u; i++)
        rec[i] = (uint8_t)ZXT_REC_MAGIC[i];
    put_name(rec + REC_OFF_NAME, t->name, ZXT_NAME_MAX);
    for (uint32_t i = 0; i < 3u; i++)
        rec[REC_OFF_ASSET + i] = (uint8_t)t->asset[i];
    const char *op = t->op == ZXT_OP_NET ? "NET " : "SETL";
    for (uint32_t i = 0; i < 4u; i++)
        rec[REC_OFF_OP + i] = (uint8_t)op[i];
    put_udigits(rec + REC_OFF_NPARTY, t->n_party, 2u);
    put_udigits(rec + REC_OFF_NOWE, t->n_owe, 3u);
    for (uint32_t i = 0; i < ZXT_MAX_PARTIES; i++) {
        uint8_t *d = rec + ZXT_REC_HEAD + i * ZXT_REC_PARTY;
        const zxt_party_t *p = &t->party[i]; /* unused slots are all zero */
        put_name(d, p->id, ZXT_ID_MAX);
        put_s18(d + 8, p->deposit);
        put_s18(d + 27, p->claim);
        put_udigits(d + 46, p->w_num, 9u);
        put_udigits(d + 55, p->w_den, 9u);
    }
    for (uint32_t j = 0; j < ZXT_MAX_OWES; j++) {
        uint8_t *d = rec + REC_OFF_OWES + j * ZXT_REC_OWE;
        const zxt_owe_t *o = &t->owe[j];
        bool used = j < t->n_owe;
        put_udigits(d, used ? o->from + 1u : 0u, 2u);
        put_udigits(d + 2, used ? o->to + 1u : 0u, 2u);
        put_s18(d + 4, o->amount);
    }
    return ZXT_OK;
}

/* ------------------------------------------------------------ SUTRA form */

typedef struct {
    const char *s;
    uint32_t len, pos;
} cur_t;

static bool cur_lit(cur_t *c, const char *lit)
{
    uint32_t n = 0;
    while (lit[n] != 0)
        n++;
    if (c->len - c->pos < n)
        return false;
    for (uint32_t i = 0; i < n; i++)
        if (c->s[c->pos + i] != lit[i])
            return false;
    c->pos += n;
    return true;
}

/* A run of name characters, at most max of them, into dst (zeroed by the
 * caller). Returns false on an empty or too long run. */
static bool cur_word(cur_t *c, char *dst, uint32_t max)
{
    uint32_t n = 0;
    while (c->pos < c->len && is_name_char(c->s[c->pos])) {
        if (n == max)
            return false;
        dst[n++] = c->s[c->pos++];
    }
    return n > 0;
}

/* A canonical unsigned decimal of at most max_digits: "0" or no leading
 * zero. */
static bool cur_udec(cur_t *c, uint32_t max_digits, uint64_t *v)
{
    uint32_t n = 0;
    uint64_t x = 0;
    while (c->pos < c->len && is_digit(c->s[c->pos])) {
        if (n == max_digits || (n == 1 && x == 0))
            return false;
        x = x * 10u + (uint64_t)(c->s[c->pos++] - '0');
        n++;
    }
    if (n == 0)
        return false;
    *v = x;
    return true;
}

static int find_party(const zxt_treaty_t *t, const char *id)
{
    for (uint32_t i = 0; i < t->n_party; i++)
        if (str_eq(t->party[i].id, id, ZXT_ID_MAX + 1u))
            return (int)i;
    return -1;
}

static zxt_status_t sutra_fail(zxt_treaty_t *out, uint32_t *err_off, zxt_status_t st, uint32_t off)
{
    return rec_fail(out, err_off, st, off);
}

zxt_status_t zxt_parse_sutra(const char *src, uint32_t len, zxt_treaty_t *out, uint32_t *err_off)
{
    cur_t c = {src, len, 0};
    zxt_status_t st;
    uint64_t v;
    if (err_off)
        *err_off = 0;
    if (!out)
        return ZXT_E_ARG;
    zxt_clear(out);
    if (!src || len > ZXT_SUTRA_MAX)
        return ZXT_E_ARG;

    if (!cur_lit(&c, "TREATY "))
        return sutra_fail(out, err_off, ZXT_E_SYNTAX, c.pos);
    if (!cur_word(&c, out->name, ZXT_NAME_MAX))
        return sutra_fail(out, err_off, ZXT_E_NAME, 7u);
    if (!cur_lit(&c, " "))
        return sutra_fail(out, err_off, ZXT_E_SYNTAX, c.pos);
    const uint32_t asset_at = c.pos;
    if (!cur_word(&c, out->asset, 3u) || !cur_lit(&c, " "))
        return sutra_fail(out, err_off, ZXT_E_ASSET, asset_at);
    uint32_t at = c.pos;
    if (cur_lit(&c, "NET\n"))
        out->op = ZXT_OP_NET;
    else if (cur_lit(&c, "SETL\n"))
        out->op = ZXT_OP_SETL;
    else
        return sutra_fail(out, err_off, ZXT_E_OP, at);
    if ((st = chk_header(out)) != ZXT_OK)
        return sutra_fail(out, err_off, st, st == ZXT_E_NAME ? 7u : asset_at);

    for (;;) {
        uint32_t line = c.pos;
        if (cur_lit(&c, "PARTY ")) {
            if (out->n_owe != 0)
                return sutra_fail(out, err_off, ZXT_E_SYNTAX, line);
            if (out->n_party == ZXT_MAX_PARTIES)
                return sutra_fail(out, err_off, ZXT_E_COUNT, line);
            uint32_t i = out->n_party++;
            zxt_party_t *p = &out->party[i];
            uint64_t wn, wd;
            if (!cur_word(&c, p->id, ZXT_ID_MAX))
                return sutra_fail(out, err_off, ZXT_E_PARTY, line + 6u);
            if (!cur_lit(&c, " "))
                return sutra_fail(out, err_off, ZXT_E_SYNTAX, c.pos);
            at = c.pos;
            if (!cur_udec(&c, 18u, &v) || !cur_lit(&c, " "))
                return sutra_fail(out, err_off, ZXT_E_NUMBER, at);
            p->deposit = (int64_t)v;
            at = c.pos;
            if (!cur_udec(&c, 18u, &v) || !cur_lit(&c, " "))
                return sutra_fail(out, err_off, ZXT_E_NUMBER, at);
            p->claim = (int64_t)v;
            at = c.pos;
            if (!cur_udec(&c, 9u, &wn) || !cur_lit(&c, "/") || !cur_udec(&c, 9u, &wd) ||
                !cur_lit(&c, "\n"))
                return sutra_fail(out, err_off, ZXT_E_NUMBER, at);
            p->w_num = (uint32_t)wn;
            p->w_den = (uint32_t)wd;
            if ((st = chk_party(out, i)) != ZXT_OK)
                return sutra_fail(out, err_off, st, line);
        } else if (cur_lit(&c, "OWE ")) {
            if (out->n_party == 0)
                return sutra_fail(out, err_off, ZXT_E_SYNTAX, line);
            if (out->n_owe == ZXT_MAX_OWES)
                return sutra_fail(out, err_off, ZXT_E_COUNT, line);
            char a[ZXT_ID_MAX + 1u] = {0}, b[ZXT_ID_MAX + 1u] = {0};
            uint32_t j = out->n_owe++;
            if (!cur_word(&c, a, ZXT_ID_MAX) || !cur_lit(&c, " "))
                return sutra_fail(out, err_off, ZXT_E_PARTY, line + 4u);
            at = c.pos;
            if (!cur_word(&c, b, ZXT_ID_MAX) || !cur_lit(&c, " "))
                return sutra_fail(out, err_off, ZXT_E_PARTY, at);
            int fa = find_party(out, a), fb = find_party(out, b);
            if (fa < 0)
                return sutra_fail(out, err_off, ZXT_E_PARTY, line + 4u);
            if (fb < 0)
                return sutra_fail(out, err_off, ZXT_E_PARTY, at);
            out->owe[j].from = (uint8_t)fa;
            out->owe[j].to = (uint8_t)fb;
            at = c.pos;
            if (!cur_udec(&c, 18u, &v) || !cur_lit(&c, "\n"))
                return sutra_fail(out, err_off, ZXT_E_NUMBER, at);
            out->owe[j].amount = (int64_t)v;
            if ((st = chk_owe(out, j)) != ZXT_OK)
                return sutra_fail(out, err_off, st, line);
        } else if (cur_lit(&c, "END\n")) {
            if (c.pos != c.len)
                return sutra_fail(out, err_off, ZXT_E_SYNTAX, c.pos);
            if (out->n_party == 0)
                return sutra_fail(out, err_off, ZXT_E_COUNT, line);
            if ((st = chk_wsum(out)) != ZXT_OK)
                return sutra_fail(out, err_off, st, line);
            break;
        } else {
            return sutra_fail(out, err_off, ZXT_E_SYNTAX, line);
        }
    }
    if ((st = zxt_validate(out)) != ZXT_OK)
        return sutra_fail(out, err_off, st, len);
    return ZXT_OK;
}

typedef struct {
    char *d;
    uint32_t cap, n;
    bool full;
} out_t;

static void o_ch(out_t *o, char ch)
{
    if (o->n == o->cap) {
        o->full = true;
        return;
    }
    o->d[o->n++] = ch;
}

static void o_str(out_t *o, const char *s)
{
    while (*s)
        o_ch(o, *s++);
}

static void o_dec(out_t *o, uint64_t v)
{
    char tmp[20];
    uint32_t n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    while (n > 0)
        o_ch(o, tmp[--n]);
}

zxt_status_t zxt_emit_sutra(const zxt_treaty_t *t, char *dst, uint32_t cap, uint32_t *n_out)
{
    zxt_status_t st;
    if (n_out)
        *n_out = 0;
    if (!dst || !n_out)
        return ZXT_E_ARG;
    if ((st = zxt_validate(t)) != ZXT_OK)
        return st;
    out_t o = {dst, cap, 0, false};
    o_str(&o, "TREATY ");
    o_str(&o, t->name);
    o_ch(&o, ' ');
    o_str(&o, t->asset);
    o_str(&o, t->op == ZXT_OP_NET ? " NET\n" : " SETL\n");
    for (uint32_t i = 0; i < t->n_party; i++) {
        const zxt_party_t *p = &t->party[i];
        o_str(&o, "PARTY ");
        o_str(&o, p->id);
        o_ch(&o, ' ');
        o_dec(&o, (uint64_t)p->deposit);
        o_ch(&o, ' ');
        o_dec(&o, (uint64_t)p->claim);
        o_ch(&o, ' ');
        o_dec(&o, p->w_num);
        o_ch(&o, '/');
        o_dec(&o, p->w_den);
        o_ch(&o, '\n');
    }
    for (uint32_t j = 0; j < t->n_owe; j++) {
        const zxt_owe_t *w = &t->owe[j];
        o_str(&o, "OWE ");
        o_str(&o, t->party[w->from].id);
        o_ch(&o, ' ');
        o_str(&o, t->party[w->to].id);
        o_ch(&o, ' ');
        o_dec(&o, (uint64_t)w->amount);
        o_ch(&o, '\n');
    }
    o_str(&o, "END\n");
    if (o.full)
        return ZXT_E_CAP;
    *n_out = o.n;
    return ZXT_OK;
}
