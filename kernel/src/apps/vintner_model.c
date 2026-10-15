/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vintner_model.c — Vintner, the vineyard and wine ledger (see
 * vintner_model.h). Integers only: volumes are millilitres, money is minor
 * units, and nothing that does not fit is ever rounded. The money rules
 * are Coffer's (coffer_balance, coffer_transfer_ok), never written again. */
#include <string.h>

#include "vintner_model.h"

/* ---- the rules ---------------------------------------------------------- */

int64_t vintner_sale_value(int64_t bottles, int64_t price)
{
    if (bottles < 0 || bottles > VINTNER_NINE || price < 0 || price > VINTNER_NINE)
        return VINTNER_REFUSED;
    return bottles * price; /* at most 18 digits: no 64-bit overflow */
}

int64_t vintner_flow_gap(int64_t before, int64_t after, int64_t loss)
{
    int64_t g = 0;
    if (__builtin_sub_overflow(before, after, &g) || __builtin_sub_overflow(g, loss, &g))
        return VINTNER_REFUSED;
    return g == VINTNER_REFUSED ? VINTNER_REFUSED : g;
}

int64_t vintner_probe_tenths(int64_t raw)
{
    if (raw < VINTNER_RAW_LO || raw > VINTNER_RAW_HI) return VINTNER_REFUSED;
    int64_t t = raw * 10;
    int64_t q = t / 16;
    if (t % 16 != 0 && t < 0) q -= 1; /* floored, as Forth and KRON */
    return q;
}

static bool digits(int64_t v, int64_t max)
{
    return v >= -max && v <= max;
}

bool vintner_lot_fits(int64_t lot, int64_t vintage, int64_t stage, int64_t bottles, int64_t volume)
{
    return digits(lot, VINTNER_NINE) && digits(vintage, VINTNER_VINTAGE_MAX) && digits(stage, 9) &&
           digits(bottles, VINTNER_NINE) && digits(volume, VINTNER_VOLUME_MAX);
}

/* ---- text ---------------------------------------------------------------- */

static void put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    while (*s && *at + 1u < cap) out[(*at)++] = *s++;
    out[*at] = 0;
}

static void put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    put(out, cap, at, t + k);
}

static void say(vintner_t *v, const char *s)
{
    uint32_t at = 0;
    v->notice[0] = 0;
    put(v->notice, sizeof v->notice, &at, s);
}

/* A whole decimal string with an optional '-', no leading zero, that fits
 * int64 (VN6). */
static bool dec_i64(const char *s, int64_t *v)
{
    bool neg = *s == '-';
    uint64_t x = 0, lim = neg ? (uint64_t) INT64_MAX + 1u : (uint64_t) INT64_MAX;
    const char *p = s + neg;
    if (!*p || (p[0] == '0' && p[1])) return false;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        uint32_t d = (uint32_t) (*p - '0');
        if (x > (lim - d) / 10u) return false;
        x = x * 10u + d;
    }
    if (neg && !x) return false;
    *v = neg ? (x == (uint64_t) INT64_MAX + 1u ? INT64_MIN : -(int64_t) x) : (int64_t) x;
    return true;
}

static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    return dec_i64(s, v) ? ZXM_OK : ZXM_E_PROTOCOL;
}

static int get_u32(const zxm_link_t *l, int32_t obj, const char *key, uint32_t *v)
{
    uint64_t u = 0;
    if (zxm_get_u64(l, obj, key, &u) != ZXM_OK || u > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) u;
    return ZXM_OK;
}

/* 1..max bytes, no control characters. */
static bool text_ok(const char *s, uint32_t max)
{
    if (!s || !s[0]) return false;
    uint32_t n = 0;
    for (const unsigned char *p = (const unsigned char *) s; *p; p++, n++)
        if (*p < 0x20u || *p == 0x7fu || n >= max) return false;
    return true;
}

/* An ISO 4217 alpha code: three capital letters. */
static bool ccy_ok(const char *s)
{
    if (!s) return false;
    for (uint32_t i = 0; i < 3u; i++)
        if (s[i] < 'A' || s[i] > 'Z') return false;
    return !s[3];
}

/* ---- the books ------------------------------------------------------------ */

void vintner_init(vintner_t *v, zxm_link_t *link)
{
    memset(v, 0, sizeof *v);
    v->link = link;
}

int vintner_revenue(vintner_t *v)
{
    int64_t d = 0, cr = 0, q = 0;
    if (!v->books) return ZXM_E_STATE;
    zxm_begin(v->link, "ledger.balance");
    zxm_p_u64(v->link, "account", v->sales);
    int rc = zxm_call(v->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = v->link->result;
    if (get_dec(v->link, r, "debit_minor", &d) != ZXM_OK ||
        get_dec(v->link, r, "credit_minor", &cr) != ZXM_OK ||
        get_dec(v->link, r, "equity_minor", &q) != ZXM_OK || cr == INT64_MIN)
        return ZXM_E_PROTOCOL;
    int64_t want = coffer_balance(d, -cr);
    if (want == COFFER_REFUSED || want != q || q < 0) return ZXM_E_PROTOCOL;
    v->revenue = q;
    return ZXM_OK;
}

/* ledger.accounts: the ids of the sales and buyers accounts in ccy, 0 when
 * absent (bit 1 sales, bit 2 buyers in *have). */
static int find_accounts(vintner_t *v, const char *ccy, uint32_t *sales, uint32_t *buyers,
                         uint32_t *have)
{
    char name[COFFER_NAME_MAX + 1], cur[VINTNER_CCY_MAX + 1], kind[16];
    *have = 0;
    zxm_begin(v->link, "ledger.accounts");
    int rc = zxm_call(v->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(v->link, v->link->result, "accounts");
    if (arr < 0 || v->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(v->link, arr);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(v->link, arr, i);
        uint32_t id = 0;
        if (o < 0 || get_u32(v->link, o, "account", &id) != ZXM_OK ||
            zxm_get_str(v->link, o, "name", name, sizeof name) != ZXM_OK ||
            zxm_get_str(v->link, o, "currency", cur, sizeof cur) != ZXM_OK ||
            zxm_get_str(v->link, o, "kind", kind, sizeof kind) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (strcmp(cur, ccy)) continue;
        if (!strcmp(name, VINTNER_SALES_NAME)) {
            if (strcmp(kind, "holder")) return ZXM_E_PROTOCOL;
            *sales = id;
            *have |= 1u;
        } else if (!strcmp(name, VINTNER_BUYERS_NAME)) {
            if (strcmp(kind, "issuer")) return ZXM_E_PROTOCOL;
            *buyers = id;
            *have |= 2u;
        }
    }
    return ZXM_OK;
}

static int open_one(vintner_t *v, const char *name, const char *ccy, bool issuer, uint32_t *id)
{
    zxm_begin(v->link, "ledger.open_account");
    zxm_p_str(v->link, "name", name);
    zxm_p_str(v->link, "currency", ccy);
    zxm_p_str(v->link, "kind", issuer ? "issuer" : "holder");
    int rc = zxm_call(v->link);
    if (rc != ZXM_OK) return rc;
    return get_u32(v->link, v->link->result, "account", id);
}

int vintner_open_books(vintner_t *v, const char *ccy)
{
    uint32_t sales = 0, buyers = 0, have = 0;
    if (!ccy_ok(ccy)) return ZXM_E_ARG;
    if (v->held.pending) return ZXM_E_STATE;
    int rc = find_accounts(v, ccy, &sales, &buyers, &have);
    if (rc != ZXM_OK) return rc;
    if (!(have & 1u) && (rc = open_one(v, VINTNER_SALES_NAME, ccy, false, &sales)) != ZXM_OK)
        return rc;
    if (!(have & 2u) && (rc = open_one(v, VINTNER_BUYERS_NAME, ccy, true, &buyers)) != ZXM_OK)
        return rc;
    if (sales == buyers) return ZXM_E_PROTOCOL;
    v->sales = sales;
    v->buyers = buyers;
    memcpy(v->ccy, ccy, 4);
    v->books = true;
    v->ne = 0;
    rc = vintner_revenue(v);
    if (rc != ZXM_OK) {
        v->books = false;
        return rc;
    }
    return ZXM_OK;
}

int vintner_sales(vintner_t *v)
{
    static int64_t got[VINTNER_ENTRIES];
    char ref[VINTNER_REF_MAX + 1];
    if (!v->books) return ZXM_E_STATE;
    zxm_begin(v->link, "ledger.entries");
    zxm_p_u64(v->link, "account", v->sales);
    int rc = zxm_call(v->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(v->link, v->link->result, "entries");
    if (arr < 0 || v->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(v->link, arr);
    if (n > VINTNER_ENTRIES) return ZXM_E_TOOBIG;
    int64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(v->link, arr, i);
        int64_t id = 0;
        uint32_t cp = 0;
        if (o < 0 || get_dec(v->link, o, "entry_id", &id) != ZXM_OK || id < 0 ||
            get_dec(v->link, o, "signed_minor", &got[i]) != ZXM_OK ||
            get_u32(v->link, o, "counterparty", &cp) != ZXM_OK ||
            zxm_get_str(v->link, o, "reference", ref, sizeof ref) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        sum = coffer_balance(sum, got[i]); /* refused past 18 digits */
        if (sum == COFFER_REFUSED) return ZXM_E_PROTOCOL;
    }
    if (sum != v->revenue) return ZXM_E_PROTOCOL; /* VN6: the journal is the balance */
    memcpy(v->entry, got, n * sizeof got[0]);
    v->ne = n;
    return ZXM_OK;
}

/* ---- lots (VN2, VN4) --------------------------------------------------------- */

const vintner_lot_t *vintner_lot(const vintner_t *v, uint32_t lot)
{
    for (uint32_t i = 0; i < v->nlots; i++)
        if (v->lot[i].lot == lot) return &v->lot[i];
    return 0;
}

static vintner_lot_t *lot_of(vintner_t *v, uint32_t lot)
{
    for (uint32_t i = 0; i < v->nlots; i++)
        if (v->lot[i].lot == lot) return &v->lot[i];
    return 0;
}

bool vintner_conserved(const vintner_lot_t *l)
{
    return l && vintner_flow_gap(l->harvest, l->volume, l->lost) == 0;
}

int vintner_add_lot(vintner_t *v, uint32_t lot, int64_t vintage, int64_t harvest_ml)
{
    if (vintage < 1 || harvest_ml < 1 ||
        !vintner_lot_fits(lot, vintage, VINTNER_HARVEST, 0, harvest_ml))
        return ZXM_E_ARG;
    if (vintner_lot(v, lot)) return ZXM_E_STATE;
    if (v->nlots >= VINTNER_LOTS) return ZXM_E_TOOBIG;
    vintner_lot_t *l = &v->lot[v->nlots];
    memset(l, 0, sizeof *l);
    l->lot = lot;
    l->vintage = vintage;
    l->stage = VINTNER_HARVEST;
    l->harvest = harvest_ml;
    l->volume = harvest_ml;
    v->nlots++;
    return ZXM_OK;
}

/* One step of a lot: volume -> after, loss lost; refused unless conserved. */
static int step(vintner_t *v, vintner_lot_t *l, int64_t after, int64_t loss, int64_t bottles)
{
    int64_t lost = 0;
    if (after < 0 || loss < 0 || vintner_flow_gap(l->volume, after, loss) != 0 ||
        __builtin_add_overflow(l->lost, loss, &lost) ||
        !vintner_lot_fits(l->lot, l->vintage, l->stage + 1, bottles, after)) {
        say(v, "The volumes do not add up: what came in must equal what went on plus what was "
               "lost.");
        return ZXM_E_ARG;
    }
    if (vintner_flow_gap(l->harvest, after, lost) != 0) return ZXM_E_STATE; /* the whole chain */
    l->volume = after;
    l->lost = lost;
    l->bottles = bottles;
    l->stage = (vintner_stage_t) (l->stage + 1);
    say(v, "The step conserves the lot's volume.");
    return ZXM_OK;
}

int vintner_advance(vintner_t *v, uint32_t lot, int64_t after_ml, int64_t loss_ml)
{
    vintner_lot_t *l = lot_of(v, lot);
    if (!l) return ZXM_E_ARG;
    if (l->stage != VINTNER_HARVEST && l->stage != VINTNER_MUST) return ZXM_E_STATE;
    return step(v, l, after_ml, loss_ml, 0);
}

int vintner_bottle(vintner_t *v, uint32_t lot, int64_t bottles, int64_t bottle_ml, int64_t loss_ml)
{
    vintner_lot_t *l = lot_of(v, lot);
    int64_t filled = 0;
    if (!l) return ZXM_E_ARG;
    if (l->stage != VINTNER_BARREL) return ZXM_E_STATE;
    if (bottles < 1 || bottles > VINTNER_NINE || bottle_ml < 1 || bottle_ml > VINTNER_NINE ||
        __builtin_mul_overflow(bottles, bottle_ml, &filled))
        return ZXM_E_ARG;
    return step(v, l, filled, loss_ml, bottles);
}

/* ---- labels from the grant engine ---------------------------------------------- */

int vintner_labels(vintner_t *v, uint64_t now, uint32_t *found)
{
    static struct {
        uint32_t lot;
        bool backed;
        char label[VINTNER_LABEL_MAX + 1], status[VINTNER_STATUS_MAX + 1];
    } got[VINTNER_LOTS];
    if (!found || now > (uint64_t) INT64_MAX) return ZXM_E_ARG;
    *found = 0;
    zxm_begin(v->link, "grants.status");
    zxm_p_u64(v->link, "now", now);
    int rc = zxm_call(v->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(v->link, v->link->result, "lots");
    uint32_t k = 0;
    if (arr >= 0) { /* no lots before the engine is set up */
        if (v->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
        uint32_t n = zxm_len(v->link, arr);
        for (uint32_t i = 0; i < n; i++) {
            int32_t o = zxm_at(v->link, arr, i);
            uint32_t id = 0;
            if (o < 0 || get_u32(v->link, o, "lot", &id) != ZXM_OK) return ZXM_E_PROTOCOL;
            if (!vintner_lot(v, id)) continue;
            if (k >= VINTNER_LOTS) return ZXM_E_TOOBIG;
            got[k].lot = id;
            if (zxm_get_bool(v->link, o, "backed", &got[k].backed) != ZXM_OK ||
                zxm_get_str(v->link, o, "label", got[k].label, sizeof got[k].label) != ZXM_OK ||
                zxm_get_str(v->link, o, "status", got[k].status, sizeof got[k].status) != ZXM_OK ||
                !text_ok(got[k].label, VINTNER_LABEL_MAX) ||
                !text_ok(got[k].status, VINTNER_STATUS_MAX))
                return ZXM_E_PROTOCOL;
            k++;
        }
    }
    /* the whole answer is read: only now is the copy changed */
    for (uint32_t i = 0; i < v->nlots; i++) v->lot[i].labelled = false;
    for (uint32_t j = 0; j < k; j++) {
        vintner_lot_t *l = lot_of(v, got[j].lot);
        l->labelled = true;
        l->backed = got[j].backed;
        memcpy(l->label, got[j].label, sizeof l->label);
        memcpy(l->status, got[j].status, sizeof l->status);
    }
    *found = k;
    return ZXM_OK;
}

/* ---- the cellar probe (VN3) ---------------------------------------------------------- */

int vintner_probe(vintner_t *v, int64_t raw)
{
    int64_t t = vintner_probe_tenths(raw);
    uint32_t at = 0;
    v->notice[0] = 0;
    if (t == VINTNER_REFUSED) {
        v->probe_ok = false;
        say(v, "The cellar probe gave a count outside its range: check the probe.");
        return ZXM_E_ARG;
    }
    v->probe_ok = true;
    v->probe_tenths = t;
    int64_t a = t < 0 ? -t : t;
    put(v->notice, sizeof v->notice, &at, "Cellar at ");
    if (t < 0) put(v->notice, sizeof v->notice, &at, "-");
    put_num(v->notice, sizeof v->notice, &at, a / 10);
    put(v->notice, sizeof v->notice, &at, ".");
    put_num(v->notice, sizeof v->notice, &at, a % 10);
    put(v->notice, sizeof v->notice, &at, " degrees.");
    return ZXM_OK;
}

/* ---- sales (VN1, VN5) ------------------------------------------------------------------ */

static void say_sale(vintner_t *v, const char *lead, const char *tail)
{
    uint32_t at = 0;
    v->notice[0] = 0;
    put(v->notice, sizeof v->notice, &at, lead);
    put_num(v->notice, sizeof v->notice, &at, v->held.bottles);
    put(v->notice, sizeof v->notice, &at, " bottles of lot ");
    put_num(v->notice, sizeof v->notice, &at, v->held.lot);
    put(v->notice, sizeof v->notice, &at, " at ");
    put_num(v->notice, sizeof v->notice, &at, v->held.price);
    put(v->notice, sizeof v->notice, &at, " = ");
    put_num(v->notice, sizeof v->notice, &at, v->held.value);
    put(v->notice, sizeof v->notice, &at, " minor units of ");
    put(v->notice, sizeof v->notice, &at, v->ccy);
    put(v->notice, sizeof v->notice, &at, tail);
}

int vintner_propose_sale(vintner_t *v, uint32_t lot, int64_t bottles, int64_t price,
                         const char *ref)
{
    if (!v->books) return ZXM_E_STATE;
    if (v->held.pending) {
        say(v, "Another sale is waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    const vintner_lot_t *l = vintner_lot(v, lot);
    if (!l || !text_ok(ref, VINTNER_REF_MAX)) return ZXM_E_ARG;
    if (l->stage != VINTNER_BOTTLED) {
        say(v, "Only a bottled lot can be sold.");
        return ZXM_E_STATE;
    }
    if (bottles < 1 || bottles > l->bottles - l->sold) {
        say(v, "Not that many bottles of this lot are left to sell.");
        return ZXM_E_ARG;
    }
    int64_t value = vintner_sale_value(bottles, price);
    /* VN5: Coffer's rule; the buyers' account is an issuer: after and room at the edge */
    int64_t gap = -value + value;
    if (value == VINTNER_REFUSED || !coffer_transfer_ok(value, 0, 0, gap) ||
        coffer_balance(v->revenue, value) == COFFER_REFUSED) {
        say(v, "The sale's value must be at least one minor unit and fit the books.");
        return ZXM_E_ARG;
    }
    v->held.pending = true;
    v->held.lot = lot;
    v->held.bottles = bottles;
    v->held.price = price;
    v->held.value = value;
    memset(v->held.ref, 0, sizeof v->held.ref);
    memcpy(v->held.ref, ref, strlen(ref));
    say_sale(v, "Sell ", "? Waiting for your confirmation.");
    return ZXM_OK;
}

int vintner_cancel_sale(vintner_t *v)
{
    if (!v->held.pending) return ZXM_E_STATE;
    memset(&v->held, 0, sizeof v->held);
    say(v, "Sale cancelled; nothing was posted.");
    return ZXM_OK;
}

int vintner_confirm_sale(vintner_t *v, uint64_t *entry)
{
    if (!entry) return ZXM_E_ARG;
    if (!v->held.pending || !v->books) return ZXM_E_STATE;
    vintner_lot_t *l = lot_of(v, v->held.lot);
    if (!l) return ZXM_E_STATE;
    vintner_sale_t h = v->held;
    zxm_begin(v->link, "ledger.post");
    zxm_p_u64(v->link, "from", v->buyers);
    zxm_p_u64(v->link, "to", v->sales);
    zxm_p_i64(v->link, "amount_minor", h.value);
    zxm_p_str(v->link, "currency", v->ccy);
    zxm_p_str(v->link, "reference", h.ref);
    int rc = zxm_call(v->link);
    if (rc != ZXM_OK) {
        memset(&v->held, 0, sizeof v->held);
        say(v, "The books refused the sale; nothing was posted.");
        return rc;
    }
    int64_t id = 0;
    if (get_dec(v->link, v->link->result, "entry_id", &id) != ZXM_OK || id < 0)
        return ZXM_E_PROTOCOL;
    *entry = (uint64_t) id;
    l->sold += h.bottles; /* within l->bottles: checked when proposed */
    say_sale(v, "Posted: ", ".");
    memset(&v->held, 0, sizeof v->held);
    return vintner_revenue(v);
}
