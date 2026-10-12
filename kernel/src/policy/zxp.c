/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* zxp.c — contract DSL: parse, check and emit both forms (see zxp.h).
 * Freestanding: no libc, no allocation, no recursion, no floating point.
 *
 * Every rule is a check of item i against items 0..i-1 (order, uniqueness,
 * backward references), so the parsers run the same check functions as
 * zxp_validate, line by line or slot by slot, and report the offset of the
 * item that broke it. */
#include "zxp.h"

/* ------------------------------------------------------------ helpers */

static void zero_bytes(void *p, uint32_t n)
{
    volatile uint8_t *b = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

void zxp_clear(zxp_contract_t *c)
{
    if (c) zero_bytes(c, (uint32_t) sizeof(*c));
}

static bool is_upper(char c)
{
    return c >= 'A' && c <= 'Z';
}
static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* NUL-terminated within size, zero after. Returns the length or -1. */
static int32_t zlen(const char *s, uint32_t size)
{
    uint32_t n = 0;
    while (n < size && s[n] != 0) n++;
    if (n >= size) return -1;
    for (uint32_t i = n; i < size; i++)
        if (s[i] != 0) return -1;
    return (int32_t) n;
}

/* COBOL-style name: 1..max of [A-Z0-9-], starts with a letter, no trailing '-'. */
static bool name_ok(const char *s, uint32_t max, uint32_t size)
{
    int32_t n = zlen(s, size);
    if (n < 1 || (uint32_t) n > max) return false;
    if (!is_upper(s[0]) || s[n - 1] == '-') return false;
    for (int32_t i = 0; i < n; i++)
        if (!is_upper(s[i]) && !is_digit(s[i]) && s[i] != '-') return false;
    return true;
}

static bool str_eq(const char *a, const char *b, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
    return true;
}

/* strcmp on NUL-terminated strings. */
static int32_t str_cmp(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a[i] != 0 && a[i] == b[i]) i++;
    return (int32_t) (uint8_t) a[i] - (int32_t) (uint8_t) b[i];
}

static bool asset_ok(const char *a)
{
    return is_upper(a[0]) && is_upper(a[1]) && is_upper(a[2]) && a[3] == 0;
}

static bool amount_ok(int64_t v)
{
    return v > 0 && v <= ZXP_PIC18_MAX;
}

/* A NUL-terminated tag; reads at most ZXP_JUR_MAX + 1 bytes. */
bool zxp_jur_ok(const char *s)
{
    uint32_t n = 0;
    if (!s) return false;
    while (n <= ZXP_JUR_MAX && s[n] != 0) n++;
    if (n > ZXP_JUR_MAX || (n != 2 && n < 4)) return false;
    if (!is_upper(s[0]) || !is_upper(s[1])) return false;
    if (n == 2) return true;
    if (s[2] != '-') return false;
    for (uint32_t i = 3; i < n; i++)
        if (!is_upper(s[i]) && !is_digit(s[i])) return false;
    return true;
}

/* A tag held in a ZXP_JUR_MAX + 1 field: valid and zero after its NUL. */
static bool jur_field_ok(const char *s)
{
    return zlen(s, ZXP_JUR_MAX + 1u) >= 0 && zxp_jur_ok(s);
}

bool zxp_jur_covers(const char *scope, const char *tag)
{
    if (!zxp_jur_ok(scope) || !zxp_jur_ok(tag)) return false;
    if (str_eq(scope, tag, ZXP_JUR_MAX + 1u)) return true;
    return scope[2] == 0 && scope[0] == tag[0] && scope[1] == tag[1] && tag[2] == '-';
}

/* ------------------------------------------------------------ dates */

static bool leap(uint32_t y)
{
    return (y % 4u == 0 && y % 100u != 0) || y % 400u == 0;
}

bool zxp_date_ok(uint32_t d)
{
    uint32_t y = d / 10000u, m = (d / 100u) % 100u, day = d % 100u;
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1900u || y > 9999u || m < 1 || m > 12 || day < 1) return false;
    uint32_t lim = mdays[m - 1] + (m == 2 && leap(y) ? 1u : 0u);
    return day <= lim;
}

/* Howard Hinnant's days_from_civil, integer only. */
int32_t zxp_days(uint32_t d)
{
    int32_t y = (int32_t) (d / 10000u), m = (int32_t) ((d / 100u) % 100u), dd = (int32_t) (d % 100u);
    y -= m <= 2;
    int32_t era = y / 400; /* y >= 1899 here, never negative */
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

uint32_t zxp_date_add(uint32_t d, int32_t days)
{
    if (!zxp_date_ok(d)) return 0;
    int32_t z = zxp_days(d) + days + 719468;
    if (z < 0) return 0;
    int32_t era = z / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = yoe + era * 400;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp = (5 * doy + 2) / 153;
    int32_t dd = doy - (153 * mp + 2) / 5 + 1;
    int32_t m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    if (y < 1900 || y > 9999) return 0;
    return (uint32_t) y * 10000u + (uint32_t) m * 100u + (uint32_t) dd;
}

/* ------------------------------------------------------------ the rules */

static zxp_status_t chk_head(const zxp_contract_t *c)
{
    if (!name_ok(c->name, ZXP_NAME_MAX, ZXP_NAME_MAX + 1u)) return ZXP_E_NAME;
    if (!zxp_date_ok(c->date)) return ZXP_E_DATE;
    return ZXP_OK;
}

static zxp_status_t chk_law(const zxp_contract_t *c, uint32_t i)
{
    if (!jur_field_ok(c->law[i])) return ZXP_E_JUR;
    if (i > 0 && str_cmp(c->law[i - 1], c->law[i]) >= 0) return ZXP_E_ORDER;
    return ZXP_OK;
}

static zxp_status_t chk_party(const zxp_contract_t *c, uint32_t i)
{
    const zxp_party_t *p = &c->party[i];
    if (!name_ok(p->id, ZXP_ID_MAX, ZXP_ID_MAX + 1u)) return ZXP_E_NAME;
    if (str_eq(p->id, ZXP_ESCROW_ID, ZXP_ID_MAX + 1u)) return ZXP_E_PARTY;
    for (uint32_t k = 0; k < i; k++)
        if (str_eq(c->party[k].id, p->id, ZXP_ID_MAX + 1u)) return ZXP_E_PARTY;
    if (!jur_field_ok(p->jur)) return ZXP_E_JUR;
    return ZXP_OK;
}

static zxp_status_t chk_sign(const zxp_contract_t *c)
{
    return (c->sign >= 1 && c->sign <= c->n_party) ? ZXP_OK : ZXP_E_SIGN;
}

static zxp_status_t chk_window(const zxp_contract_t *c)
{
    if (!zxp_date_ok(c->window.from) || !zxp_date_ok(c->window.to) || c->window.from > c->window.to)
        return ZXP_E_DATE;
    return ZXP_OK;
}

static zxp_status_t chk_lock(const zxp_contract_t *c, uint32_t i)
{
    const zxp_span_t *l = &c->lock[i];
    if (!zxp_date_ok(l->from) || !zxp_date_ok(l->to) || l->from > l->to) return ZXP_E_DATE;
    if (i > 0 && c->lock[i - 1].to >= l->from) return ZXP_E_ORDER;
    return ZXP_OK;
}

static zxp_status_t chk_cap(const zxp_contract_t *c, uint32_t i)
{
    if (!asset_ok(c->cap[i].asset)) return ZXP_E_ASSET;
    if (!amount_ok(c->cap[i].amount)) return ZXP_E_NUMBER;
    if (i > 0 && str_cmp(c->cap[i - 1].asset, c->cap[i].asset) >= 0) return ZXP_E_ORDER;
    return ZXP_OK;
}

static zxp_status_t chk_limit(const zxp_contract_t *c, uint32_t i)
{
    const zxp_limit_t *l = &c->limit[i];
    if (l->party >= c->n_party) return ZXP_E_PARTY;
    if (!asset_ok(l->asset)) return ZXP_E_ASSET;
    if (!amount_ok(l->amount)) return ZXP_E_NUMBER;
    if (i > 0) {
        const zxp_limit_t *p = &c->limit[i - 1];
        if (p->party > l->party || (p->party == l->party && str_cmp(p->asset, l->asset) >= 0))
            return ZXP_E_ORDER;
    }
    return ZXP_OK;
}

static zxp_status_t chk_tag(char (*tags)[ZXP_JUR_MAX + 1u], uint32_t i)
{
    if (!jur_field_ok(tags[i])) return ZXP_E_JUR;
    if (i > 0 && str_cmp(tags[i - 1], tags[i]) >= 0) return ZXP_E_ORDER;
    return ZXP_OK;
}

static bool clause_id_used(const zxp_contract_t *c, uint32_t i)
{
    for (uint32_t k = 0; k < i; k++)
        if (str_eq(c->clause[k].id, c->clause[i].id, ZXP_ID_MAX + 1u)) return true;
    return false;
}

static zxp_status_t chk_clause(const zxp_contract_t *c, uint32_t i)
{
    const zxp_clause_t *k = &c->clause[i];
    if (!name_ok(k->id, ZXP_ID_MAX, ZXP_ID_MAX + 1u)) return ZXP_E_NAME;
    if (clause_id_used(c, i)) return ZXP_E_NAME;
    if (k->kind == ZXP_CL_DUE) {
        if (k->payer >= c->n_party || k->payee >= c->n_party) return ZXP_E_PARTY;
        if (k->payer == k->payee) return ZXP_E_CLAUSE;
        if (!asset_ok(k->asset)) return ZXP_E_ASSET;
        if (!amount_ok(k->amount)) return ZXP_E_NUMBER;
        if (k->penalty < 0 || k->penalty > ZXP_PIC18_MAX) return ZXP_E_NUMBER;
        if (!zxp_date_ok(k->date) || k->date < c->date) return ZXP_E_DATE;
        if (k->after > i) return ZXP_E_REF;
        if (k->op != 0 || k->n_ref != 0) return ZXP_E_CLAUSE;
        for (uint32_t r = 0; r < ZXP_MAX_REFS; r++)
            if (k->ref[r] != 0) return ZXP_E_CLAUSE;
        return ZXP_OK;
    }
    if (k->kind != ZXP_CL_GATE) return ZXP_E_CLAUSE;
    if (k->payer != 0 || k->payee != 0 || k->asset[0] != 0 || k->asset[1] != 0 ||
        k->asset[2] != 0 || k->asset[3] != 0 || k->amount != 0 || k->date != 0 ||
        k->penalty != 0 || k->after != 0)
        return ZXP_E_CLAUSE;
    if (k->op != ZXP_GATE_ALL && k->op != ZXP_GATE_ANY) return ZXP_E_CLAUSE;
    if (k->n_ref < 2 || k->n_ref > ZXP_MAX_REFS) return ZXP_E_CLAUSE;
    for (uint32_t r = 0; r < ZXP_MAX_REFS; r++) {
        if (r >= k->n_ref) {
            if (k->ref[r] != 0) return ZXP_E_CLAUSE;
            continue;
        }
        if (k->ref[r] >= i) return ZXP_E_REF;
        for (uint32_t q = 0; q < r; q++)
            if (k->ref[q] == k->ref[r]) return ZXP_E_REF;
    }
    return ZXP_OK;
}

static bool mem_zero(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *) p;
    for (uint32_t i = 0; i < n; i++)
        if (b[i] != 0) return false;
    return true;
}

zxp_status_t zxp_validate(const zxp_contract_t *c)
{
    zxp_status_t st;
    if (!c) return ZXP_E_ARG;
    if ((st = chk_head(c)) != ZXP_OK) return st;
    if (c->n_law < 1 || c->n_law > ZXP_MAX_LAWS || c->n_party < 1 || c->n_party > ZXP_MAX_PARTIES ||
        c->n_lock > ZXP_MAX_LOCKS || c->n_cap > ZXP_MAX_CAPS || c->n_limit > ZXP_MAX_LIMITS ||
        c->n_allow > ZXP_MAX_TAGS || c->n_deny > ZXP_MAX_TAGS || c->n_clause > ZXP_MAX_CLAUSES)
        return ZXP_E_COUNT;
    for (uint32_t i = 0; i < ZXP_MAX_LAWS; i++) {
        if (i >= c->n_law) {
            if (!mem_zero(c->law[i], ZXP_JUR_MAX + 1u)) return ZXP_E_PAD;
        } else if ((st = chk_law(c, i)) != ZXP_OK) {
            return st;
        }
    }
    for (uint32_t i = 0; i < ZXP_MAX_PARTIES; i++) {
        if (i >= c->n_party) {
            if (!mem_zero(&c->party[i], sizeof c->party[i])) return ZXP_E_PAD;
        } else if ((st = chk_party(c, i)) != ZXP_OK) {
            return st;
        }
    }
    if ((st = chk_sign(c)) != ZXP_OK) return st;
    if ((st = chk_window(c)) != ZXP_OK) return st;
    for (uint32_t i = 0; i < ZXP_MAX_LOCKS; i++) {
        if (i >= c->n_lock) {
            if (!mem_zero(&c->lock[i], sizeof c->lock[i])) return ZXP_E_PAD;
        } else if ((st = chk_lock(c, i)) != ZXP_OK) {
            return st;
        }
    }
    for (uint32_t i = 0; i < ZXP_MAX_CAPS; i++) {
        if (i >= c->n_cap) {
            if (!mem_zero(&c->cap[i], sizeof c->cap[i])) return ZXP_E_PAD;
        } else if ((st = chk_cap(c, i)) != ZXP_OK) {
            return st;
        }
    }
    for (uint32_t i = 0; i < ZXP_MAX_LIMITS; i++) {
        if (i >= c->n_limit) {
            if (!mem_zero(&c->limit[i], sizeof c->limit[i])) return ZXP_E_PAD;
        } else if ((st = chk_limit(c, i)) != ZXP_OK) {
            return st;
        }
    }
    for (uint32_t i = 0; i < ZXP_MAX_TAGS; i++) {
        if (i >= c->n_allow) {
            if (!mem_zero(c->allow[i], ZXP_JUR_MAX + 1u)) return ZXP_E_PAD;
        } else if ((st = chk_tag((char (*)[ZXP_JUR_MAX + 1u]) c->allow, i)) != ZXP_OK) {
            return st;
        }
    }
    for (uint32_t i = 0; i < ZXP_MAX_TAGS; i++) {
        if (i >= c->n_deny) {
            if (!mem_zero(c->deny[i], ZXP_JUR_MAX + 1u)) return ZXP_E_PAD;
        } else if ((st = chk_tag((char (*)[ZXP_JUR_MAX + 1u]) c->deny, i)) != ZXP_OK) {
            return st;
        }
    }
    for (uint32_t i = 0; i < ZXP_MAX_CLAUSES; i++) {
        if (i >= c->n_clause) {
            if (!mem_zero(&c->clause[i], sizeof c->clause[i])) return ZXP_E_PAD;
        } else if ((st = chk_clause(c, i)) != ZXP_OK) {
            return st;
        }
    }
    return ZXP_OK;
}

bool zxp_equal(const zxp_contract_t *a, const zxp_contract_t *b)
{
    if (!a || !b) return false;
    /* Valid contracts are zero in every unused byte (zxp_validate), and the
     * parsers build them field by field on a cleared struct, so equality of
     * the fields below is equality of the contracts. */
    if (!str_eq(a->name, b->name, ZXP_NAME_MAX + 1u) || a->date != b->date) return false;
    for (uint32_t i = 0; i < 32u; i++)
        if (a->doc[i] != b->doc[i]) return false;
    if (a->n_law != b->n_law || a->n_party != b->n_party || a->sign != b->sign ||
        a->n_lock != b->n_lock || a->n_cap != b->n_cap || a->n_limit != b->n_limit ||
        a->n_allow != b->n_allow || a->n_deny != b->n_deny || a->n_clause != b->n_clause ||
        a->window.from != b->window.from || a->window.to != b->window.to)
        return false;
    for (uint32_t i = 0; i < ZXP_MAX_LAWS; i++)
        if (!str_eq(a->law[i], b->law[i], ZXP_JUR_MAX + 1u)) return false;
    for (uint32_t i = 0; i < ZXP_MAX_PARTIES; i++)
        if (!str_eq(a->party[i].id, b->party[i].id, ZXP_ID_MAX + 1u) ||
            !str_eq(a->party[i].jur, b->party[i].jur, ZXP_JUR_MAX + 1u))
            return false;
    for (uint32_t i = 0; i < ZXP_MAX_LOCKS; i++)
        if (a->lock[i].from != b->lock[i].from || a->lock[i].to != b->lock[i].to) return false;
    for (uint32_t i = 0; i < ZXP_MAX_CAPS; i++)
        if (!str_eq(a->cap[i].asset, b->cap[i].asset, 4u) || a->cap[i].amount != b->cap[i].amount)
            return false;
    for (uint32_t i = 0; i < ZXP_MAX_LIMITS; i++)
        if (a->limit[i].party != b->limit[i].party ||
            !str_eq(a->limit[i].asset, b->limit[i].asset, 4u) ||
            a->limit[i].amount != b->limit[i].amount)
            return false;
    for (uint32_t i = 0; i < ZXP_MAX_TAGS; i++)
        if (!str_eq(a->allow[i], b->allow[i], ZXP_JUR_MAX + 1u) ||
            !str_eq(a->deny[i], b->deny[i], ZXP_JUR_MAX + 1u))
            return false;
    for (uint32_t i = 0; i < ZXP_MAX_CLAUSES; i++) {
        const zxp_clause_t *p = &a->clause[i], *q = &b->clause[i];
        if (p->kind != q->kind || !str_eq(p->id, q->id, ZXP_ID_MAX + 1u) || p->payer != q->payer ||
            p->payee != q->payee || !str_eq(p->asset, q->asset, 4u) || p->amount != q->amount ||
            p->date != q->date || p->penalty != q->penalty || p->after != q->after ||
            p->op != q->op || p->n_ref != q->n_ref)
            return false;
        for (uint32_t r = 0; r < ZXP_MAX_REFS; r++)
            if (p->ref[r] != q->ref[r]) return false;
    }
    return true;
}

int32_t zxp_find_clause(const zxp_contract_t *c, const char *id)
{
    if (!c || !id) return -1;
    for (uint32_t i = 0; i < c->n_clause && i < ZXP_MAX_CLAUSES; i++)
        if (str_eq(c->clause[i].id, id, ZXP_ID_MAX + 1u)) return (int32_t) i;
    return -1;
}

/* ------------------------------------------------------------ SUTRA emit */

/* Writes to p, or (p == NULL, cmp != NULL) compares against cmp and
 * records the first differing offset, or (both NULL) only counts. */
typedef struct {
    char *p;
    uint32_t cap, n;
    bool ok;
    const char *cmp;
    uint32_t diff; /* first offset where emit != cmp; 0xFFFFFFFF = none */
} sbuf_t;

static void put_c(sbuf_t *b, char c)
{
    if (b->n >= b->cap) {
        b->ok = false;
        return;
    }
    if (b->p) b->p[b->n] = c;
    else if (b->cmp && b->diff == 0xFFFFFFFFu && b->cmp[b->n] != c) b->diff = b->n;
    b->n++;
}

static void put_s(sbuf_t *b, const char *s)
{
    for (uint32_t i = 0; s[i] != 0; i++) put_c(b, s[i]);
}

static void put_u(sbuf_t *b, uint64_t v)
{
    char t[20];
    uint32_t n = 0;
    do {
        t[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    while (n > 0) put_c(b, t[--n]);
}

static const char HEX[] = "0123456789abcdef";

/* Emit into b (a NULL b->p only measures). Stops after clause `stop`'s
 * line starts when stop >= 0 (used for the clause offset). */
static void emit_all(const zxp_contract_t *c, sbuf_t *b, int32_t stop, uint32_t *stop_off)
{
    put_s(b, "CONTRACT ");
    put_s(b, c->name);
    put_c(b, ' ');
    put_u(b, c->date);
    put_s(b, "\nDOC ");
    for (uint32_t i = 0; i < 32u; i++) {
        put_c(b, HEX[c->doc[i] >> 4]);
        put_c(b, HEX[c->doc[i] & 15u]);
    }
    put_c(b, '\n');
    for (uint32_t i = 0; i < c->n_law; i++) {
        put_s(b, "LAW ");
        put_s(b, c->law[i]);
        put_c(b, '\n');
    }
    for (uint32_t i = 0; i < c->n_party; i++) {
        put_s(b, "PARTY ");
        put_s(b, c->party[i].id);
        put_c(b, ' ');
        put_s(b, c->party[i].jur);
        put_c(b, '\n');
    }
    put_s(b, "SIGN ");
    put_u(b, c->sign);
    put_s(b, "\nWINDOW ");
    put_u(b, c->window.from);
    put_c(b, ' ');
    put_u(b, c->window.to);
    put_c(b, '\n');
    for (uint32_t i = 0; i < c->n_lock; i++) {
        put_s(b, "LOCK ");
        put_u(b, c->lock[i].from);
        put_c(b, ' ');
        put_u(b, c->lock[i].to);
        put_c(b, '\n');
    }
    for (uint32_t i = 0; i < c->n_cap; i++) {
        put_s(b, "CAP ");
        put_s(b, c->cap[i].asset);
        put_c(b, ' ');
        put_u(b, (uint64_t) c->cap[i].amount);
        put_c(b, '\n');
    }
    for (uint32_t i = 0; i < c->n_limit; i++) {
        put_s(b, "LIMIT ");
        put_s(b, c->party[c->limit[i].party].id);
        put_c(b, ' ');
        put_s(b, c->limit[i].asset);
        put_c(b, ' ');
        put_u(b, (uint64_t) c->limit[i].amount);
        put_c(b, '\n');
    }
    for (uint32_t i = 0; i < c->n_allow; i++) {
        put_s(b, "ALLOW ");
        put_s(b, c->allow[i]);
        put_c(b, '\n');
    }
    for (uint32_t i = 0; i < c->n_deny; i++) {
        put_s(b, "DENY ");
        put_s(b, c->deny[i]);
        put_c(b, '\n');
    }
    for (uint32_t i = 0; i < c->n_clause; i++) {
        const zxp_clause_t *k = &c->clause[i];
        if (stop >= 0 && (uint32_t) stop == i) {
            *stop_off = b->n;
            return;
        }
        if (k->kind == ZXP_CL_DUE) {
            put_s(b, "DUE ");
            put_s(b, k->id);
            put_c(b, ' ');
            put_s(b, c->party[k->payer].id);
            put_c(b, ' ');
            put_s(b, c->party[k->payee].id);
            put_c(b, ' ');
            put_s(b, k->asset);
            put_c(b, ' ');
            put_u(b, (uint64_t) k->amount);
            put_c(b, ' ');
            put_u(b, k->date);
            if (k->penalty > 0) {
                put_s(b, " PENALTY ");
                put_u(b, (uint64_t) k->penalty);
            }
            if (k->after > 0) {
                put_s(b, " AFTER ");
                put_s(b, c->clause[k->after - 1u].id);
            }
        } else {
            put_s(b, "GATE ");
            put_s(b, k->id);
            put_s(b, k->op == ZXP_GATE_ALL ? " ALL" : " ANY");
            for (uint32_t r = 0; r < k->n_ref; r++) {
                put_c(b, ' ');
                put_s(b, c->clause[k->ref[r]].id);
            }
        }
        put_c(b, '\n');
    }
    put_s(b, "END\n");
}

zxp_status_t zxp_emit_sutra(const zxp_contract_t *c, char *out, uint32_t cap, uint32_t *n_out)
{
    zxp_status_t st;
    if (n_out) *n_out = 0;
    if (!c || !out || !n_out) return ZXP_E_ARG;
    if ((st = zxp_validate(c)) != ZXP_OK) return st;
    sbuf_t b = {out, cap, 0, true, (const char *) 0, 0xFFFFFFFFu};
    emit_all(c, &b, -1, (uint32_t *) 0);
    if (!b.ok) return ZXP_E_CAP;
    *n_out = b.n;
    return ZXP_OK;
}

int32_t zxp_clause_offset(const zxp_contract_t *c, uint32_t i)
{
    if (!c || zxp_validate(c) != ZXP_OK || i >= c->n_clause) return -1;
    sbuf_t b = {(char *) 0, 0xFFFFFFFFu, 0, true, (const char *) 0, 0xFFFFFFFFu};
    uint32_t off = 0;
    emit_all(c, &b, (int32_t) i, &off);
    return (int32_t) off;
}

/* ------------------------------------------------------------ SUTRA parse */

typedef struct {
    const char *s;
    uint32_t len, pos; /* pos: start of the current line */
    uint32_t lend;     /* index of the current line's '\n' */
    uint32_t tok;      /* cursor inside the line */
} lex_t;

/* Load the line at l->pos. False at end of input or if no '\n' ends it. */
static bool line_load(lex_t *l)
{
    uint32_t e = l->pos;
    while (e < l->len && l->s[e] != '\n') e++;
    if (e >= l->len) return false;
    l->lend = e;
    l->tok = l->pos;
    return true;
}

static void line_next(lex_t *l)
{
    l->pos = l->lend + 1u;
}

/* The next single-space-separated token: [*a, *a + *n). False on an empty
 * token (double, leading or trailing space) or at the end of the line. */
static bool tok(lex_t *l, const char **a, uint32_t *n)
{
    if (l->tok > l->lend) return false;
    uint32_t b = l->tok, e = b;
    while (e < l->lend && l->s[e] != ' ') e++;
    if (e == b) return false;
    *a = l->s + b;
    *n = e - b;
    l->tok = e + 1u; /* past the space, or past lend when e == lend */
    if (e == l->lend) l->tok = l->lend + 1u;
    else if (e + 1u == l->lend) return false; /* trailing space */
    return true;
}

static bool at_eol(const lex_t *l)
{
    return l->tok > l->lend;
}

static bool tok_is(const char *a, uint32_t n, const char *lit)
{
    uint32_t i = 0;
    for (; i < n; i++)
        if (lit[i] == 0 || a[i] != lit[i]) return false;
    return lit[i] == 0;
}

/* Is the next token on the current line `lit`? Does not consume. */
static bool peek_kw(const lex_t *l, const char *lit)
{
    uint32_t i = 0, p = l->pos;
    while (lit[i] != 0) {
        if (p + i >= l->len || l->s[p + i] != lit[i]) return false;
        i++;
    }
    return p + i < l->len && (l->s[p + i] == ' ' || l->s[p + i] == '\n');
}

static bool tok_copy(const char *a, uint32_t n, char *dst, uint32_t size)
{
    if (n + 1u > size) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] == 0) return false;
        dst[i] = a[i];
    }
    return true;
}

/* Canonical unsigned decimal of at most `digits` digits. */
static bool tok_num(const char *a, uint32_t n, uint32_t digits, uint64_t *v)
{
    uint64_t x = 0;
    if (n < 1 || n > digits || (n > 1 && a[0] == '0')) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (!is_digit(a[i])) return false;
        x = x * 10u + (uint64_t) (a[i] - '0');
    }
    *v = x;
    return true;
}

static bool tok_date(const char *a, uint32_t n, uint32_t *d)
{
    uint64_t v;
    if (n != 8 || !tok_num(a, n, 8, &v)) return false;
    *d = (uint32_t) v;
    return true;
}

static int32_t party_index(const zxp_contract_t *c, const char *a, uint32_t n)
{
    char id[ZXP_ID_MAX + 1u] = {0};
    if (!tok_copy(a, n, id, sizeof id)) return -1;
    for (uint32_t i = 0; i < c->n_party; i++)
        if (str_eq(c->party[i].id, id, ZXP_ID_MAX + 1u)) return (int32_t) i;
    return -1;
}

static int32_t clause_index(const zxp_contract_t *c, uint32_t upto, const char *a, uint32_t n)
{
    char id[ZXP_ID_MAX + 1u] = {0};
    if (!tok_copy(a, n, id, sizeof id)) return -1;
    for (uint32_t i = 0; i < upto; i++)
        if (str_eq(c->clause[i].id, id, ZXP_ID_MAX + 1u)) return (int32_t) i;
    return -1;
}

static int hexval(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static zxp_status_t s_fail(zxp_contract_t *out, uint32_t *err_off, zxp_status_t st, uint32_t off)
{
    zxp_clear(out);
    if (err_off) *err_off = off;
    return st;
}

#define FAIL(st) return s_fail(out, err_off, (st), l.pos)

zxp_status_t zxp_parse_sutra(const char *src, uint32_t len, zxp_contract_t *out, uint32_t *err_off)
{
    lex_t l = {src, len, 0, 0, 0};
    const char *a;
    uint32_t n;
    uint64_t v;
    zxp_status_t st;
    if (err_off) *err_off = 0;
    if (!out) return ZXP_E_ARG;
    zxp_clear(out);
    if (!src) return ZXP_E_ARG;

    /* CONTRACT name date */
    if (!line_load(&l) || !tok(&l, &a, &n) || !tok_is(a, n, "CONTRACT")) FAIL(ZXP_E_SYNTAX);
    if (!tok(&l, &a, &n) || !tok_copy(a, n, out->name, sizeof out->name)) FAIL(ZXP_E_NAME);
    if (!tok(&l, &a, &n) || !tok_date(a, n, &out->date)) FAIL(ZXP_E_DATE);
    if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
    if ((st = chk_head(out)) != ZXP_OK) FAIL(st);
    line_next(&l);

    /* DOC hex */
    if (!line_load(&l) || !tok(&l, &a, &n) || !tok_is(a, n, "DOC")) FAIL(ZXP_E_SYNTAX);
    if (!tok(&l, &a, &n) || n != 64) FAIL(ZXP_E_DOC);
    for (uint32_t i = 0; i < 32u; i++) {
        int h = hexval(a[2 * i]), lo = hexval(a[2 * i + 1]);
        if (h < 0 || lo < 0) FAIL(ZXP_E_DOC);
        out->doc[i] = (uint8_t) (h * 16 + lo);
    }
    if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
    line_next(&l);

    /* LAW jur (1..4) */
    while (line_load(&l) && peek_kw(&l, "LAW")) {
        if (out->n_law >= ZXP_MAX_LAWS) FAIL(ZXP_E_COUNT);
        (void) tok(&l, &a, &n);
        if (!tok(&l, &a, &n) || !tok_copy(a, n, out->law[out->n_law], ZXP_JUR_MAX + 1u))
            FAIL(ZXP_E_JUR);
        if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
        if ((st = chk_law(out, out->n_law)) != ZXP_OK) FAIL(st);
        out->n_law++;
        line_next(&l);
    }
    if (out->n_law == 0) FAIL(ZXP_E_COUNT);

    /* PARTY id jur (1..15) */
    while (line_load(&l) && peek_kw(&l, "PARTY")) {
        zxp_party_t *p;
        if (out->n_party >= ZXP_MAX_PARTIES) FAIL(ZXP_E_COUNT);
        p = &out->party[out->n_party];
        (void) tok(&l, &a, &n);
        if (!tok(&l, &a, &n) || !tok_copy(a, n, p->id, sizeof p->id)) FAIL(ZXP_E_NAME);
        if (!tok(&l, &a, &n) || !tok_copy(a, n, p->jur, sizeof p->jur)) FAIL(ZXP_E_JUR);
        if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
        if ((st = chk_party(out, out->n_party)) != ZXP_OK) FAIL(st);
        out->n_party++;
        line_next(&l);
    }
    if (out->n_party == 0) FAIL(ZXP_E_COUNT);

    /* SIGN k */
    if (!line_load(&l) || !tok(&l, &a, &n) || !tok_is(a, n, "SIGN")) FAIL(ZXP_E_SYNTAX);
    if (!tok(&l, &a, &n) || !tok_num(a, n, 2, &v)) FAIL(ZXP_E_SIGN);
    out->sign = (uint8_t) v;
    if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
    if ((st = chk_sign(out)) != ZXP_OK) FAIL(st);
    line_next(&l);

    /* WINDOW from to */
    if (!line_load(&l) || !tok(&l, &a, &n) || !tok_is(a, n, "WINDOW")) FAIL(ZXP_E_SYNTAX);
    if (!tok(&l, &a, &n) || !tok_date(a, n, &out->window.from)) FAIL(ZXP_E_DATE);
    if (!tok(&l, &a, &n) || !tok_date(a, n, &out->window.to)) FAIL(ZXP_E_DATE);
    if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
    if ((st = chk_window(out)) != ZXP_OK) FAIL(st);
    line_next(&l);

    while (line_load(&l) && peek_kw(&l, "LOCK")) {
        zxp_span_t *s;
        if (out->n_lock >= ZXP_MAX_LOCKS) FAIL(ZXP_E_COUNT);
        s = &out->lock[out->n_lock];
        (void) tok(&l, &a, &n);
        if (!tok(&l, &a, &n) || !tok_date(a, n, &s->from)) FAIL(ZXP_E_DATE);
        if (!tok(&l, &a, &n) || !tok_date(a, n, &s->to)) FAIL(ZXP_E_DATE);
        if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
        if ((st = chk_lock(out, out->n_lock)) != ZXP_OK) FAIL(st);
        out->n_lock++;
        line_next(&l);
    }

    while (line_load(&l) && peek_kw(&l, "CAP")) {
        zxp_cap_t *k;
        if (out->n_cap >= ZXP_MAX_CAPS) FAIL(ZXP_E_COUNT);
        k = &out->cap[out->n_cap];
        (void) tok(&l, &a, &n);
        if (!tok(&l, &a, &n) || n != 3 || !tok_copy(a, n, k->asset, 4u)) FAIL(ZXP_E_ASSET);
        if (!tok(&l, &a, &n) || !tok_num(a, n, 18, &v)) FAIL(ZXP_E_NUMBER);
        k->amount = (int64_t) v;
        if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
        if ((st = chk_cap(out, out->n_cap)) != ZXP_OK) FAIL(st);
        out->n_cap++;
        line_next(&l);
    }

    while (line_load(&l) && peek_kw(&l, "LIMIT")) {
        zxp_limit_t *k;
        int32_t pi;
        if (out->n_limit >= ZXP_MAX_LIMITS) FAIL(ZXP_E_COUNT);
        k = &out->limit[out->n_limit];
        (void) tok(&l, &a, &n);
        if (!tok(&l, &a, &n) || (pi = party_index(out, a, n)) < 0) FAIL(ZXP_E_PARTY);
        k->party = (uint8_t) pi;
        if (!tok(&l, &a, &n) || n != 3 || !tok_copy(a, n, k->asset, 4u)) FAIL(ZXP_E_ASSET);
        if (!tok(&l, &a, &n) || !tok_num(a, n, 18, &v)) FAIL(ZXP_E_NUMBER);
        k->amount = (int64_t) v;
        if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
        if ((st = chk_limit(out, out->n_limit)) != ZXP_OK) FAIL(st);
        out->n_limit++;
        line_next(&l);
    }

    while (line_load(&l) && peek_kw(&l, "ALLOW")) {
        if (out->n_allow >= ZXP_MAX_TAGS) FAIL(ZXP_E_COUNT);
        (void) tok(&l, &a, &n);
        if (!tok(&l, &a, &n) || !tok_copy(a, n, out->allow[out->n_allow], ZXP_JUR_MAX + 1u))
            FAIL(ZXP_E_JUR);
        if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
        if ((st = chk_tag(out->allow, out->n_allow)) != ZXP_OK) FAIL(st);
        out->n_allow++;
        line_next(&l);
    }
    while (line_load(&l) && peek_kw(&l, "DENY")) {
        if (out->n_deny >= ZXP_MAX_TAGS) FAIL(ZXP_E_COUNT);
        (void) tok(&l, &a, &n);
        if (!tok(&l, &a, &n) || !tok_copy(a, n, out->deny[out->n_deny], ZXP_JUR_MAX + 1u))
            FAIL(ZXP_E_JUR);
        if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
        if ((st = chk_tag(out->deny, out->n_deny)) != ZXP_OK) FAIL(st);
        out->n_deny++;
        line_next(&l);
    }

    /* DUE and GATE clauses, in any order, references backwards only. */
    while (line_load(&l) && (peek_kw(&l, "DUE") || peek_kw(&l, "GATE"))) {
        zxp_clause_t *k;
        int32_t x;
        uint32_t i = out->n_clause;
        if (i >= ZXP_MAX_CLAUSES) FAIL(ZXP_E_COUNT);
        k = &out->clause[i];
        (void) tok(&l, &a, &n);
        bool due = tok_is(a, n, "DUE");
        if (!tok(&l, &a, &n) || !tok_copy(a, n, k->id, sizeof k->id)) FAIL(ZXP_E_NAME);
        if (due) {
            k->kind = ZXP_CL_DUE;
            if (!tok(&l, &a, &n) || (x = party_index(out, a, n)) < 0) FAIL(ZXP_E_PARTY);
            k->payer = (uint8_t) x;
            if (!tok(&l, &a, &n) || (x = party_index(out, a, n)) < 0) FAIL(ZXP_E_PARTY);
            k->payee = (uint8_t) x;
            if (!tok(&l, &a, &n) || n != 3 || !tok_copy(a, n, k->asset, 4u)) FAIL(ZXP_E_ASSET);
            if (!tok(&l, &a, &n) || !tok_num(a, n, 18, &v)) FAIL(ZXP_E_NUMBER);
            k->amount = (int64_t) v;
            if (!tok(&l, &a, &n) || !tok_date(a, n, &k->date)) FAIL(ZXP_E_DATE);
            /* optional " PENALTY n" then optional " AFTER id", in that order */
            while (!at_eol(&l)) {
                if (!tok(&l, &a, &n)) FAIL(ZXP_E_SYNTAX);
                if (tok_is(a, n, "PENALTY") && k->penalty == 0 && k->after == 0) {
                    if (!tok(&l, &a, &n) || !tok_num(a, n, 18, &v) || v == 0) FAIL(ZXP_E_NUMBER);
                    k->penalty = (int64_t) v;
                } else if (tok_is(a, n, "AFTER") && k->after == 0) {
                    if (!tok(&l, &a, &n) || (x = clause_index(out, i, a, n)) < 0) FAIL(ZXP_E_REF);
                    k->after = (uint8_t) (x + 1);
                } else {
                    FAIL(ZXP_E_SYNTAX);
                }
            }
        } else {
            k->kind = ZXP_CL_GATE;
            if (!tok(&l, &a, &n)) FAIL(ZXP_E_SYNTAX);
            if (tok_is(a, n, "ALL")) k->op = ZXP_GATE_ALL;
            else if (tok_is(a, n, "ANY")) k->op = ZXP_GATE_ANY;
            else FAIL(ZXP_E_CLAUSE);
            while (!at_eol(&l)) {
                if (k->n_ref >= ZXP_MAX_REFS) FAIL(ZXP_E_CLAUSE);
                if (!tok(&l, &a, &n) || (x = clause_index(out, i, a, n)) < 0) FAIL(ZXP_E_REF);
                k->ref[k->n_ref++] = (uint8_t) x;
            }
        }
        if (!at_eol(&l)) FAIL(ZXP_E_SYNTAX);
        if ((st = chk_clause(out, i)) != ZXP_OK) FAIL(st);
        out->n_clause++;
        line_next(&l);
    }

    if (!line_load(&l) || l.lend != l.pos + 3u || !peek_kw(&l, "END")) FAIL(ZXP_E_SYNTAX);
    if (l.lend + 1u != len) {
        l.pos = l.lend + 1u;
        FAIL(ZXP_E_SYNTAX);
    }
    /* C3 by construction: the canonical emit must reproduce the input
     * byte for byte; any other spelling the checks above let through is
     * refused at the first differing byte. */
    {
        sbuf_t b = {(char *) 0, len, 0, true, src, 0xFFFFFFFFu};
        emit_all(out, &b, -1, (uint32_t *) 0);
        if (!b.ok) return s_fail(out, err_off, ZXP_E_SYNTAX, len);
        if (b.diff != 0xFFFFFFFFu) return s_fail(out, err_off, ZXP_E_SYNTAX, b.diff);
        if (b.n != len) return s_fail(out, err_off, ZXP_E_SYNTAX, b.n);
    }
    return ZXP_OK;
}

#undef FAIL

/* ------------------------------------------------------------ RECORD form */

#define R_NAME    4u
#define R_DATE    34u
#define R_DOC     42u
#define R_SIGN    106u
#define R_WFROM   108u
#define R_WTO     116u
#define R_NLAW    124u
#define R_NPARTY  125u
#define R_NLOCK   127u
#define R_NCAP    128u
#define R_NLIMIT  129u
#define R_NALLOW  131u
#define R_NDENY   133u
#define R_NCLAUSE 135u
#define R_LAWS    ZXP_REC_HEAD
#define R_PARTIES (R_LAWS + ZXP_REC_LAWS)
#define R_LOCKS   (R_PARTIES + ZXP_MAX_PARTIES * ZXP_REC_PARTY)
#define R_CAPS    (R_LOCKS + ZXP_MAX_LOCKS * ZXP_REC_LOCK)
#define R_LIMITS  (R_CAPS + ZXP_MAX_CAPS * ZXP_REC_CAP)
#define R_ALLOWS  (R_LIMITS + ZXP_MAX_LIMITS * ZXP_REC_LIMIT)
#define R_DENIES  (R_ALLOWS + ZXP_MAX_TAGS * ZXP_REC_TAG)
#define R_CLAUSES (R_DENIES + ZXP_MAX_TAGS * ZXP_REC_TAG)

typedef char zxp_rec_len_check[(R_CLAUSES + ZXP_MAX_CLAUSES * ZXP_REC_CLAUSE == ZXP_REC_LEN) ? 1 : -1];

static bool r_udig(const uint8_t *s, uint32_t n, uint64_t *v)
{
    uint64_t x = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!is_digit((char) s[i])) return false;
        x = x * 10u + (uint64_t) (s[i] - '0');
    }
    *v = x;
    return true;
}

static bool r_s18(const uint8_t *s, int64_t *v)
{
    uint64_t m;
    if ((s[0] != '+' && s[0] != '-') || !r_udig(s + 1, 18u, &m)) return false;
    if (s[0] == '-' && m == 0) return false;
    *v = s[0] == '-' ? -(int64_t) m : (int64_t) m;
    return true;
}

/* Space-padded X(size) text into dst (at most size chars). */
static bool r_text(const uint8_t *s, uint32_t size, char *dst)
{
    uint32_t n = 0;
    while (n < size && s[n] != ' ') {
        if (s[n] == 0) return false;
        dst[n] = (char) s[n];
        n++;
    }
    for (uint32_t i = n; i < size; i++)
        if (s[i] != ' ') return false;
    return true;
}

static bool r_all(const uint8_t *s, uint8_t c, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (s[i] != c) return false;
    return true;
}

static bool r_is(const uint8_t *s, const char *lit, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (s[i] != (uint8_t) lit[i]) return false;
    return true;
}

static bool r_zero_s18(const uint8_t *s)
{
    return s[0] == '+' && r_all(s + 1, '0', 18u);
}

static zxp_status_t r_fail(zxp_contract_t *out, uint32_t *err_off, zxp_status_t st, uint32_t off)
{
    zxp_clear(out);
    if (err_off) *err_off = off;
    return st;
}

#define RFAIL(st, off) return r_fail(out, err_off, (st), (off))

zxp_status_t zxp_parse_record(const uint8_t *rec, uint32_t len, zxp_contract_t *out,
                              uint32_t *err_off)
{
    uint64_t u;
    zxp_status_t st;
    if (err_off) *err_off = 0;
    if (!out) return ZXP_E_ARG;
    zxp_clear(out);
    if (!rec || len != ZXP_REC_LEN) return ZXP_E_ARG;
    if (!r_is(rec, ZXP_REC_MAGIC, 4u)) RFAIL(ZXP_E_MAGIC, 0);
    if (!r_text(rec + R_NAME, ZXP_NAME_MAX, out->name)) RFAIL(ZXP_E_NAME, R_NAME);
    if (!r_udig(rec + R_DATE, 8u, &u)) RFAIL(ZXP_E_DATE, R_DATE);
    out->date = (uint32_t) u;
    if ((st = chk_head(out)) != ZXP_OK) RFAIL(st, st == ZXP_E_NAME ? R_NAME : R_DATE);
    for (uint32_t i = 0; i < 32u; i++) {
        int h = hexval((char) rec[R_DOC + 2 * i]), lo = hexval((char) rec[R_DOC + 2 * i + 1]);
        if (h < 0 || lo < 0) RFAIL(ZXP_E_DOC, R_DOC);
        out->doc[i] = (uint8_t) (h * 16 + lo);
    }
    if (!r_udig(rec + R_NLAW, 1u, &u) || u < 1 || u > ZXP_MAX_LAWS) RFAIL(ZXP_E_COUNT, R_NLAW);
    out->n_law = (uint8_t) u;
    if (!r_udig(rec + R_NPARTY, 2u, &u) || u < 1 || u > ZXP_MAX_PARTIES) RFAIL(ZXP_E_COUNT, R_NPARTY);
    out->n_party = (uint8_t) u;
    if (!r_udig(rec + R_NLOCK, 1u, &u) || u > ZXP_MAX_LOCKS) RFAIL(ZXP_E_COUNT, R_NLOCK);
    out->n_lock = (uint8_t) u;
    if (!r_udig(rec + R_NCAP, 1u, &u) || u > ZXP_MAX_CAPS) RFAIL(ZXP_E_COUNT, R_NCAP);
    out->n_cap = (uint8_t) u;
    if (!r_udig(rec + R_NLIMIT, 2u, &u) || u > ZXP_MAX_LIMITS) RFAIL(ZXP_E_COUNT, R_NLIMIT);
    out->n_limit = (uint8_t) u;
    if (!r_udig(rec + R_NALLOW, 2u, &u) || u > ZXP_MAX_TAGS) RFAIL(ZXP_E_COUNT, R_NALLOW);
    out->n_allow = (uint8_t) u;
    if (!r_udig(rec + R_NDENY, 2u, &u) || u > ZXP_MAX_TAGS) RFAIL(ZXP_E_COUNT, R_NDENY);
    out->n_deny = (uint8_t) u;
    if (!r_udig(rec + R_NCLAUSE, 2u, &u) || u > ZXP_MAX_CLAUSES) RFAIL(ZXP_E_COUNT, R_NCLAUSE);
    out->n_clause = (uint8_t) u;

    for (uint32_t i = 0; i < ZXP_MAX_LAWS; i++) {
        uint32_t off = R_LAWS + i * ZXP_JUR_MAX;
        if (i >= out->n_law) {
            if (!r_all(rec + off, ' ', ZXP_JUR_MAX)) RFAIL(ZXP_E_PAD, off);
            continue;
        }
        if (!r_text(rec + off, ZXP_JUR_MAX, out->law[i])) RFAIL(ZXP_E_JUR, off);
        if ((st = chk_law(out, i)) != ZXP_OK) RFAIL(st, off);
    }
    for (uint32_t i = 0; i < ZXP_MAX_PARTIES; i++) {
        uint32_t off = R_PARTIES + i * ZXP_REC_PARTY;
        if (i >= out->n_party) {
            if (!r_all(rec + off, ' ', ZXP_REC_PARTY)) RFAIL(ZXP_E_PAD, off);
            continue;
        }
        if (!r_text(rec + off, ZXP_ID_MAX, out->party[i].id)) RFAIL(ZXP_E_NAME, off);
        if (!r_text(rec + off + 8u, ZXP_JUR_MAX, out->party[i].jur)) RFAIL(ZXP_E_JUR, off);
        if ((st = chk_party(out, i)) != ZXP_OK) RFAIL(st, off);
    }
    if (!r_udig(rec + R_SIGN, 2u, &u)) RFAIL(ZXP_E_SIGN, R_SIGN);
    out->sign = (uint8_t) u;
    if ((st = chk_sign(out)) != ZXP_OK) RFAIL(st, R_SIGN);
    if (!r_udig(rec + R_WFROM, 8u, &u)) RFAIL(ZXP_E_DATE, R_WFROM);
    out->window.from = (uint32_t) u;
    if (!r_udig(rec + R_WTO, 8u, &u)) RFAIL(ZXP_E_DATE, R_WTO);
    out->window.to = (uint32_t) u;
    if ((st = chk_window(out)) != ZXP_OK) RFAIL(st, R_WFROM);

    for (uint32_t i = 0; i < ZXP_MAX_LOCKS; i++) {
        uint32_t off = R_LOCKS + i * ZXP_REC_LOCK;
        if (i >= out->n_lock) {
            if (!r_all(rec + off, '0', ZXP_REC_LOCK)) RFAIL(ZXP_E_PAD, off);
            continue;
        }
        if (!r_udig(rec + off, 8u, &u)) RFAIL(ZXP_E_DATE, off);
        out->lock[i].from = (uint32_t) u;
        if (!r_udig(rec + off + 8u, 8u, &u)) RFAIL(ZXP_E_DATE, off);
        out->lock[i].to = (uint32_t) u;
        if ((st = chk_lock(out, i)) != ZXP_OK) RFAIL(st, off);
    }
    for (uint32_t i = 0; i < ZXP_MAX_CAPS; i++) {
        uint32_t off = R_CAPS + i * ZXP_REC_CAP;
        if (i >= out->n_cap) {
            if (!r_all(rec + off, ' ', 3u) || !r_zero_s18(rec + off + 3u)) RFAIL(ZXP_E_PAD, off);
            continue;
        }
        if (!r_text(rec + off, 3u, out->cap[i].asset)) RFAIL(ZXP_E_ASSET, off);
        if (!r_s18(rec + off + 3u, &out->cap[i].amount)) RFAIL(ZXP_E_NUMBER, off);
        if ((st = chk_cap(out, i)) != ZXP_OK) RFAIL(st, off);
    }
    for (uint32_t i = 0; i < ZXP_MAX_LIMITS; i++) {
        uint32_t off = R_LIMITS + i * ZXP_REC_LIMIT;
        if (i >= out->n_limit) {
            if (!r_all(rec + off, '0', 2u) || !r_all(rec + off + 2u, ' ', 3u) ||
                !r_zero_s18(rec + off + 5u))
                RFAIL(ZXP_E_PAD, off);
            continue;
        }
        if (!r_udig(rec + off, 2u, &u) || u < 1 || u > out->n_party) RFAIL(ZXP_E_PARTY, off);
        out->limit[i].party = (uint8_t) (u - 1u);
        if (!r_text(rec + off + 2u, 3u, out->limit[i].asset)) RFAIL(ZXP_E_ASSET, off);
        if (!r_s18(rec + off + 5u, &out->limit[i].amount)) RFAIL(ZXP_E_NUMBER, off);
        if ((st = chk_limit(out, i)) != ZXP_OK) RFAIL(st, off);
    }
    for (uint32_t i = 0; i < ZXP_MAX_TAGS; i++) {
        uint32_t off = R_ALLOWS + i * ZXP_REC_TAG;
        if (i >= out->n_allow) {
            if (!r_all(rec + off, ' ', ZXP_REC_TAG)) RFAIL(ZXP_E_PAD, off);
            continue;
        }
        if (!r_text(rec + off, ZXP_JUR_MAX, out->allow[i])) RFAIL(ZXP_E_JUR, off);
        if ((st = chk_tag(out->allow, i)) != ZXP_OK) RFAIL(st, off);
    }
    for (uint32_t i = 0; i < ZXP_MAX_TAGS; i++) {
        uint32_t off = R_DENIES + i * ZXP_REC_TAG;
        if (i >= out->n_deny) {
            if (!r_all(rec + off, ' ', ZXP_REC_TAG)) RFAIL(ZXP_E_PAD, off);
            continue;
        }
        if (!r_text(rec + off, ZXP_JUR_MAX, out->deny[i])) RFAIL(ZXP_E_JUR, off);
        if ((st = chk_tag(out->deny, i)) != ZXP_OK) RFAIL(st, off);
    }
    for (uint32_t i = 0; i < ZXP_MAX_CLAUSES; i++) {
        uint32_t off = R_CLAUSES + i * ZXP_REC_CLAUSE;
        const uint8_t *s = rec + off;
        /* KIND 0, ID 4, PAYER 12, PAYEE 14, ASSET 16, AMOUNT 19, DATE 38,
         * PENALTY 46, AFTER 65, GOP 67, NREF 70, REF 71 */
        bool due_blank = r_all(s + 12, '0', 4u) && r_all(s + 16, ' ', 3u) && r_zero_s18(s + 19) &&
                         r_all(s + 38, '0', 8u) && r_zero_s18(s + 46) && r_all(s + 65, '0', 2u);
        bool gate_blank = r_all(s + 67, ' ', 3u) && r_all(s + 70, '0', 17u);
        if (i >= out->n_clause) {
            if (!r_all(s, ' ', 12u) || !due_blank || !gate_blank) RFAIL(ZXP_E_PAD, off);
            continue;
        }
        zxp_clause_t *k = &out->clause[i];
        if (!r_text(s + 4, ZXP_ID_MAX, k->id)) RFAIL(ZXP_E_NAME, off);
        if (r_is(s, "DUE ", 4u)) {
            k->kind = ZXP_CL_DUE;
            if (!gate_blank) RFAIL(ZXP_E_CLAUSE, off);
            if (!r_udig(s + 12, 2u, &u) || u < 1 || u > out->n_party) RFAIL(ZXP_E_PARTY, off);
            k->payer = (uint8_t) (u - 1u);
            if (!r_udig(s + 14, 2u, &u) || u < 1 || u > out->n_party) RFAIL(ZXP_E_PARTY, off);
            k->payee = (uint8_t) (u - 1u);
            if (!r_text(s + 16, 3u, k->asset)) RFAIL(ZXP_E_ASSET, off);
            if (!r_s18(s + 19, &k->amount)) RFAIL(ZXP_E_NUMBER, off);
            if (!r_udig(s + 38, 8u, &u)) RFAIL(ZXP_E_DATE, off);
            k->date = (uint32_t) u;
            if (!r_s18(s + 46, &k->penalty)) RFAIL(ZXP_E_NUMBER, off);
            if (!r_udig(s + 65, 2u, &u)) RFAIL(ZXP_E_REF, off);
            k->after = (uint8_t) u;
        } else if (r_is(s, "GATE", 4u)) {
            k->kind = ZXP_CL_GATE;
            if (!due_blank) RFAIL(ZXP_E_CLAUSE, off);
            if (r_is(s + 67, "ALL", 3u)) k->op = ZXP_GATE_ALL;
            else if (r_is(s + 67, "ANY", 3u)) k->op = ZXP_GATE_ANY;
            else RFAIL(ZXP_E_CLAUSE, off);
            if (!r_udig(s + 70, 1u, &u) || u < 2 || u > ZXP_MAX_REFS) RFAIL(ZXP_E_CLAUSE, off);
            k->n_ref = (uint8_t) u;
            for (uint32_t r = 0; r < ZXP_MAX_REFS; r++) {
                if (!r_udig(s + 71 + 2 * r, 2u, &u)) RFAIL(ZXP_E_REF, off);
                if (r >= k->n_ref) {
                    if (u != 0) RFAIL(ZXP_E_PAD, off);
                    continue;
                }
                if (u < 1 || u > i) RFAIL(ZXP_E_REF, off);
                k->ref[r] = (uint8_t) (u - 1u);
            }
        } else {
            RFAIL(ZXP_E_CLAUSE, off);
        }
        if ((st = chk_clause(out, i)) != ZXP_OK) RFAIL(st, off);
    }
    return ZXP_OK;
}

#undef RFAIL

static void w_text(uint8_t *d, uint32_t size, const char *s)
{
    uint32_t i = 0;
    for (; i < size && s[i] != 0; i++) d[i] = (uint8_t) s[i];
    for (; i < size; i++) d[i] = ' ';
}

static void w_udig(uint8_t *d, uint32_t n, uint64_t v)
{
    for (uint32_t i = n; i > 0; i--) {
        d[i - 1] = (uint8_t) ('0' + v % 10u);
        v /= 10u;
    }
}

static void w_s18(uint8_t *d, int64_t v)
{
    d[0] = v < 0 ? '-' : '+';
    w_udig(d + 1, 18u, (uint64_t) (v < 0 ? -v : v));
}

zxp_status_t zxp_emit_record(const zxp_contract_t *c, uint8_t *rec, uint32_t cap)
{
    zxp_status_t st;
    if (!c || !rec) return ZXP_E_ARG;
    if (cap < ZXP_REC_LEN) return ZXP_E_CAP;
    if ((st = zxp_validate(c)) != ZXP_OK) return st;
    for (uint32_t i = 0; i < 4u; i++) rec[i] = (uint8_t) ZXP_REC_MAGIC[i];
    w_text(rec + R_NAME, ZXP_NAME_MAX, c->name);
    w_udig(rec + R_DATE, 8u, c->date);
    for (uint32_t i = 0; i < 32u; i++) {
        rec[R_DOC + 2 * i] = (uint8_t) HEX[c->doc[i] >> 4];
        rec[R_DOC + 2 * i + 1] = (uint8_t) HEX[c->doc[i] & 15u];
    }
    w_udig(rec + R_SIGN, 2u, c->sign);
    w_udig(rec + R_WFROM, 8u, c->window.from);
    w_udig(rec + R_WTO, 8u, c->window.to);
    w_udig(rec + R_NLAW, 1u, c->n_law);
    w_udig(rec + R_NPARTY, 2u, c->n_party);
    w_udig(rec + R_NLOCK, 1u, c->n_lock);
    w_udig(rec + R_NCAP, 1u, c->n_cap);
    w_udig(rec + R_NLIMIT, 2u, c->n_limit);
    w_udig(rec + R_NALLOW, 2u, c->n_allow);
    w_udig(rec + R_NDENY, 2u, c->n_deny);
    w_udig(rec + R_NCLAUSE, 2u, c->n_clause);
    for (uint32_t i = 0; i < ZXP_MAX_LAWS; i++) w_text(rec + R_LAWS + i * ZXP_JUR_MAX, ZXP_JUR_MAX, c->law[i]);
    for (uint32_t i = 0; i < ZXP_MAX_PARTIES; i++) {
        uint8_t *d = rec + R_PARTIES + i * ZXP_REC_PARTY;
        w_text(d, ZXP_ID_MAX, c->party[i].id);
        w_text(d + 8, ZXP_JUR_MAX, c->party[i].jur);
    }
    for (uint32_t i = 0; i < ZXP_MAX_LOCKS; i++) {
        uint8_t *d = rec + R_LOCKS + i * ZXP_REC_LOCK;
        w_udig(d, 8u, c->lock[i].from);
        w_udig(d + 8, 8u, c->lock[i].to);
    }
    for (uint32_t i = 0; i < ZXP_MAX_CAPS; i++) {
        uint8_t *d = rec + R_CAPS + i * ZXP_REC_CAP;
        w_text(d, 3u, c->cap[i].asset);
        w_s18(d + 3, c->cap[i].amount);
    }
    for (uint32_t i = 0; i < ZXP_MAX_LIMITS; i++) {
        uint8_t *d = rec + R_LIMITS + i * ZXP_REC_LIMIT;
        w_udig(d, 2u, i < c->n_limit ? c->limit[i].party + 1u : 0u);
        w_text(d + 2, 3u, c->limit[i].asset);
        w_s18(d + 5, c->limit[i].amount);
    }
    for (uint32_t i = 0; i < ZXP_MAX_TAGS; i++) {
        w_text(rec + R_ALLOWS + i * ZXP_REC_TAG, ZXP_JUR_MAX, c->allow[i]);
        w_text(rec + R_DENIES + i * ZXP_REC_TAG, ZXP_JUR_MAX, c->deny[i]);
    }
    for (uint32_t i = 0; i < ZXP_MAX_CLAUSES; i++) {
        uint8_t *d = rec + R_CLAUSES + i * ZXP_REC_CLAUSE;
        const zxp_clause_t *k = &c->clause[i];
        bool used = i < c->n_clause, due = used && k->kind == ZXP_CL_DUE;
        bool gate = used && k->kind == ZXP_CL_GATE;
        w_text(d, 4u, due ? "DUE " : gate ? "GATE" : "");
        w_text(d + 4, ZXP_ID_MAX, k->id);
        w_udig(d + 12, 2u, due ? k->payer + 1u : 0u);
        w_udig(d + 14, 2u, due ? k->payee + 1u : 0u);
        w_text(d + 16, 3u, k->asset);
        w_s18(d + 19, k->amount);
        w_udig(d + 38, 8u, k->date);
        w_s18(d + 46, k->penalty);
        w_udig(d + 65, 2u, k->after);
        w_text(d + 67, 3u, gate ? (k->op == ZXP_GATE_ALL ? "ALL" : "ANY") : "");
        w_udig(d + 70, 1u, k->n_ref);
        for (uint32_t r = 0; r < ZXP_MAX_REFS; r++)
            w_udig(d + 71 + 2 * r, 2u, r < k->n_ref ? k->ref[r] + 1u : 0u);
    }
    return ZXP_OK;
}
