/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* taxhook.c — see taxhook.h (X1-X8). Freestanding: no libc, no float, no
 * 64-bit division helper (cp_udivmod64). NOT TAX ADVICE. */
#include "taxhook.h"

static bool cc_eq(const char a[2], const char b[2])
{
    return a[0] == b[0] && a[1] == b[1];
}

static bool region_eq(const char a[CP_REGION_LEN], const char b[CP_REGION_LEN])
{
    return cp_eq(a, b, CP_REGION_LEN);
}

static bool region_empty(const char r[CP_REGION_LEN])
{
    for (unsigned i = 0; i < CP_REGION_LEN; i++)
        if (r[i]) return false;
    return true;
}

static bool region_fmt(const char r[CP_REGION_LEN])
{
    bool end = false;
    for (unsigned i = 0; i < CP_REGION_LEN; i++) {
        char c = r[i];
        if (!c) {
            end = true;
            continue;
        }
        if (end || !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

/* YYYYMMDD with a real calendar day (Gregorian leap years). 32-bit
 * arithmetic only. */
static bool date_ok(uint32_t d)
{
    uint32_t y = d / 10000u, m = (d / 100u) % 100u, day = d % 100u;
    if (y < 1900u || y > 9999u || m < 1u || m > 12u || day < 1u) return false;
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t max = dim[m - 1];
    bool leap = (y % 4u == 0 && y % 100u != 0) || y % 400u == 0;
    if (m == 2 && leap) max = 29;
    return day <= max;
}

static bool table_ok(const taxhook_table_t *t)
{
    if (t->hdr.kind != CP_KIND_TAXTABLE || !cp_cc_ok(t->hdr.country, false)) return false;
    if (t->hdr.version == 0 || t->hdr.not_after < t->hdr.not_before) return false;
    if (!region_fmt(t->hdr.region)) return false;
    if (t->kind < TAXHOOK_VAT || t->kind > TAXHOOK_SALES) return false;
    if (t->ccy == 0 || t->ccy > 999) return false;
    if (t->rounding != TAXHOOK_HALF_UP && t->rounding != TAXHOOK_HALF_EVEN) return false;
    if (t->level != TAXHOOK_PER_LINE && t->level != TAXHOOK_PER_INVOICE) return false;
    if (t->flags & ~TAXHOOK_F_ALL) return false;
    for (unsigned i = 0; i < TAXHOOK_LABEL_LEN; i++) {
        char c = t->label[i];
        if (c && (c < 0x20 || c > 0x7e)) return false;
    }
    if (t->n_rates > TAXHOOK_MAX_RATES) return false;
    for (unsigned i = 0; i < t->n_rates; i++) {
        const taxhook_rate_t *r = &t->rate[i];
        if (r->category < TAXHOOK_STANDARD || r->category > TAXHOOK_ZERO) return false;
        if (r->rate_ppm > TAXHOOK_PPM) return false;
        if (r->category == TAXHOOK_ZERO && r->rate_ppm != 0) return false;
        if (!region_fmt(r->region) || !date_ok(r->from)) return false;
        if (r->to != 0 && (!date_ok(r->to) || r->to < r->from)) return false;
        for (unsigned j = 0; j < i; j++) {
            const taxhook_rate_t *o = &t->rate[j];
            if (o->category != r->category || !region_eq(o->region, r->region)) continue;
            uint32_t rt = r->to ? r->to : 0xFFFFFFFFu, ot = o->to ? o->to : 0xFFFFFFFFu;
            if (r->from <= ot && o->from <= rt) return false; /* overlap */
        }
    }
    return true;
}

size_t taxhook_encode(const taxhook_table_t *t, uint8_t *out, size_t cap)
{
    if (!t || !out || !table_ok(t)) return 0;
    cp_w_t w = {out, cap, 0, false};
    cp_hdr_put(&w, &t->hdr);
    cp_put8(&w, t->kind);
    cp_put16(&w, t->ccy);
    cp_put8(&w, t->rounding);
    cp_put8(&w, t->level);
    cp_put8(&w, t->flags);
    cp_put(&w, t->label, TAXHOOK_LABEL_LEN);
    cp_put8(&w, t->n_rates);
    for (unsigned i = 0; i < t->n_rates; i++) {
        cp_put8(&w, t->rate[i].category);
        cp_put(&w, t->rate[i].region, CP_REGION_LEN);
        cp_put32(&w, t->rate[i].rate_ppm);
        cp_put32(&w, t->rate[i].from);
        cp_put32(&w, t->rate[i].to);
    }
    return w.bad ? 0 : w.len;
}

taxhook_status_t taxhook_decode(const uint8_t *m, size_t len, taxhook_table_t *t)
{
    if (!m || !t) return TAXHOOK_E_ARG;
    cp_zero(t, sizeof *t);
    cp_r_t r = {m, len, 0, false};
    if (!cp_hdr_get(&r, &t->hdr, false)) return TAXHOOK_E_DECODE;
    t->kind = cp_get8(&r);
    t->ccy = cp_get16(&r);
    t->rounding = cp_get8(&r);
    t->level = cp_get8(&r);
    t->flags = cp_get8(&r);
    cp_get(&r, t->label, TAXHOOK_LABEL_LEN);
    t->n_rates = cp_get8(&r);
    if (t->n_rates > TAXHOOK_MAX_RATES) return TAXHOOK_E_DECODE;
    for (unsigned i = 0; i < t->n_rates; i++) {
        t->rate[i].category = cp_get8(&r);
        cp_get(&r, t->rate[i].region, CP_REGION_LEN);
        t->rate[i].rate_ppm = cp_get32(&r);
        t->rate[i].from = cp_get32(&r);
        t->rate[i].to = cp_get32(&r);
    }
    if (r.bad || r.off != len || !table_ok(t)) return TAXHOOK_E_DECODE;
    return TAXHOOK_OK;
}

void taxhook_init(taxhook_t *h, cp_verify_fn verify, void *vctx)
{
    cp_zero(h, sizeof *h);
    h->enabled = false;
    h->verify = verify;
    h->vctx = vctx;
}

void taxhook_enable(taxhook_t *h, bool on)
{
    h->enabled = on;
}

const taxhook_table_t *taxhook_table(const taxhook_t *h, const char country[2],
                                     const char region[CP_REGION_LEN], uint8_t kind)
{
    for (uint32_t i = 0; i < h->n_tables; i++) {
        const taxhook_table_t *t = &h->table[i];
        if (t->kind == kind && cc_eq(t->hdr.country, country) && region_eq(t->hdr.region, region))
            return t;
    }
    return 0;
}

taxhook_status_t taxhook_install(taxhook_t *h, const uint8_t *m, size_t len, const uint8_t *sig,
                                 size_t sig_len, uint64_t now)
{
    if (!h || !m) return TAXHOOK_E_ARG;
    taxhook_table_t t;
    taxhook_status_t st = taxhook_decode(m, len, &t);
    if (st != TAXHOOK_OK) return st;
    if (!cp_manifest_verify(h->verify, h->vctx, &t.hdr, m, len, sig, sig_len)) return TAXHOOK_E_SIG;
    if (now < t.hdr.not_before || now > t.hdr.not_after) return TAXHOOK_E_WINDOW;
    taxhook_table_t *old =
        (taxhook_table_t *) taxhook_table(h, t.hdr.country, t.hdr.region, t.kind);
    if (old) {
        if (t.hdr.version <= old->hdr.version) return TAXHOOK_E_VERSION;
        cp_copy(old, &t, sizeof t);
        return TAXHOOK_OK;
    }
    if (h->n_tables >= TAXHOOK_MAX_TABLES) return TAXHOOK_E_FULL;
    cp_copy(&h->table[h->n_tables++], &t, sizeof t);
    return TAXHOOK_OK;
}

uint64_t taxhook_round_div(uint64_t n, uint64_t d, uint8_t rounding)
{
    uint64_t r, q = cp_udivmod64(n, d, &r);
    if (d == 0) return 0;
    uint64_t other = d - r; /* r vs d - r: no 2*r overflow */
    if (r > other || (r == other && (rounding == TAXHOOK_HALF_UP || (q & 1u)))) q++;
    return q;
}

/* X3: the combined rate, or a status. */
static taxhook_status_t rate_for(const taxhook_table_t *t, const taxhook_invoice_t *inv,
                                 uint8_t cat, uint32_t *out)
{
    *out = 0;
    if (cat == TAXHOOK_EXEMPT) return TAXHOOK_OK;
    uint32_t sum = 0;
    bool found = false;
    for (unsigned i = 0; i < t->n_rates; i++) {
        const taxhook_rate_t *r = &t->rate[i];
        if (r->category != cat || inv->date < r->from || (r->to && inv->date > r->to)) continue;
        bool hit = region_empty(r->region);
        for (unsigned k = 0; k < TAXHOOK_MAX_REGION && !hit; k++)
            hit = region_eq(r->region, inv->regions[k]); /* r->region is not empty here */
        if (!hit) continue;
        found = true;
        sum += r->rate_ppm; /* <= 32 * 10^6 < 2^32 */
    }
    if (!found) return TAXHOOK_E_NO_RATE;
    if (sum > TAXHOOK_PPM) return TAXHOOK_E_RANGE;
    *out = sum;
    return TAXHOOK_OK;
}

static uint64_t tax_of(uint64_t amount, uint32_t rate, bool inclusive, uint8_t rounding)
{
    /* amount <= 2^40, rate <= 2^20: the product is below 2^61. */
    uint64_t d = inclusive ? (uint64_t) TAXHOOK_PPM + rate : TAXHOOK_PPM;
    return taxhook_round_div(amount * rate, d, rounding);
}

static bool inv_ok(const taxhook_invoice_t *inv)
{
    if (!inv->lines || inv->n_lines == 0 || !date_ok(inv->date)) return false;
    if (!cp_cc_ok(inv->seller, false) || !cp_cc_ok(inv->buyer, false)) return false;
    for (unsigned k = 0; k < TAXHOOK_MAX_REGION; k++)
        if (!region_fmt(inv->regions[k])) return false;
    return true;
}

taxhook_status_t taxhook_compute_table(const taxhook_table_t *t, const taxhook_invoice_t *inv,
                                       taxhook_receipt_t *out)
{
    if (!t || !inv || !out) return TAXHOOK_E_ARG;
    cp_zero(out, sizeof *out);
    if (!inv_ok(inv)) return TAXHOOK_E_ARG;
    if (inv->n_lines > TAXHOOK_MAX_LINES) return TAXHOOK_E_RANGE;
    cp_copy(out->country, t->hdr.country, 2);
    cp_copy(out->region, t->hdr.region, CP_REGION_LEN);
    out->kind = t->kind;
    out->rounding = t->rounding;
    out->level = t->level;
    out->flags = t->flags;
    out->version = t->hdr.version;
    out->reverse_charge = (t->flags & TAXHOOK_F_REVERSE_CHARGE) &&
                          (t->kind == TAXHOOK_VAT || t->kind == TAXHOOK_GST) &&
                          inv->buyer_business && !cc_eq(inv->seller, inv->buyer);
    bool incl = inv->inclusive; /* under reverse charge every rate is 0: gross = net */
    out->inclusive = inv->inclusive;
    bool per_line = t->level == TAXHOOK_PER_LINE;

    for (uint32_t i = 0; i < inv->n_lines; i++) {
        const taxhook_line_t *l = &inv->lines[i];
        if (l->category < TAXHOOK_STANDARD || l->category > TAXHOOK_EXEMPT) return TAXHOOK_E_ARG;
        if (l->qty == 0) return TAXHOOK_E_ARG;
        if (l->qty > TAXHOOK_QTY_MAX || l->unit_price > TAXHOOK_AMOUNT_MAX) return TAXHOOK_E_RANGE;
        uint64_t amount = (uint64_t) l->qty * l->unit_price; /* <= 2^60 */
        if (amount > TAXHOOK_AMOUNT_MAX) return TAXHOOK_E_RANGE;
        uint32_t rate;
        taxhook_status_t st = rate_for(t, inv, l->category, &rate);
        if (st != TAXHOOK_OK) return st;
        if (out->reverse_charge) rate = 0;
        taxhook_rline_t *rl = &out->line[out->n_lines++];
        rl->category = l->category;
        rl->rate_ppm = rate;
        rl->amount = amount;
        rl->tax = per_line ? tax_of(amount, rate, incl, t->rounding) : 0;

        taxhook_group_t *g = 0;
        for (uint32_t k = 0; k < out->n_groups && !g; k++)
            if (out->group[k].category == l->category && out->group[k].rate_ppm == rate)
                g = &out->group[k];
        if (!g) {
            g = &out->group[out->n_groups++];
            g->category = l->category;
            g->rate_ppm = rate;
        }
        /* The group's running amount lives in net (exclusive) or gross
         * (inclusive) until the totals pass below. */
        uint64_t *acc = incl ? &g->gross : &g->net;
        *acc += amount;
        if (*acc > TAXHOOK_AMOUNT_MAX) return TAXHOOK_E_RANGE;
        g->tax += rl->tax;
    }
    for (uint32_t k = 0; k < out->n_groups; k++) {
        taxhook_group_t *g = &out->group[k];
        if (!per_line) g->tax = tax_of(incl ? g->gross : g->net, g->rate_ppm, incl, t->rounding);
        if (incl)
            g->net = g->gross - g->tax;
        else
            g->gross = g->net + g->tax;
        out->net += g->net;
        out->tax += g->tax;
        out->gross += g->gross;
    }
    return TAXHOOK_OK;
}

taxhook_status_t taxhook_compute(const taxhook_t *h, const char country[2],
                                 const char region[CP_REGION_LEN], uint8_t kind,
                                 const taxhook_invoice_t *inv, uint64_t now, taxhook_receipt_t *out)
{
    if (out) cp_zero(out, sizeof *out);
    if (!h || !country || !region || !inv || !out) return TAXHOOK_E_ARG;
    if (!h->enabled) return TAXHOOK_E_DISABLED;
    const taxhook_table_t *t = taxhook_table(h, country, region, kind);
    if (!t) return TAXHOOK_E_NO_TABLE;
    if (now < t->hdr.not_before || now > t->hdr.not_after) return TAXHOOK_E_EXPIRED;
    return taxhook_compute_table(t, inv, out);
}

/* ---- X8 text receipt ------------------------------------------------- */
typedef struct {
    char *p;
    size_t cap, len;
    bool bad;
} tw_t;

static void ts(tw_t *w, const char *s)
{
    for (; *s; s++) {
        if (w->len + 1 >= w->cap) {
            w->bad = true;
            return;
        }
        w->p[w->len++] = *s;
    }
}

static void tc(tw_t *w, char c)
{
    char s[2] = {c, 0};
    ts(w, s);
}

/* An unsigned integer with `minor` digits after a point. */
static void tnum(tw_t *w, uint64_t v, unsigned minor)
{
    char d[24];
    unsigned n = 0;
    do {
        uint64_t r;
        v = cp_udivmod64(v, 10, &r);
        d[n++] = (char) ('0' + r);
    } while (v || n <= minor);
    while (n) {
        if (minor && n == minor) tc(w, '.');
        tc(w, d[--n]);
    }
}

/* ppm as a percentage with trailing zeros removed: 88750 -> "8.875". */
static void trate(tw_t *w, uint32_t ppm)
{
    uint32_t whole = ppm / 10000u, frac = ppm % 10000u;
    tnum(w, whole, 0);
    if (frac) {
        char f[5];
        for (int i = 3; i >= 0; i--) {
            f[i] = (char) ('0' + frac % 10u);
            frac /= 10u;
        }
        int end = 4;
        while (end > 0 && f[end - 1] == '0') end--;
        tc(w, '.');
        for (int i = 0; i < end; i++) tc(w, f[i]);
    }
    tc(w, '%');
}

static const char *cat_name(uint8_t c)
{
    switch (c) {
    case TAXHOOK_STANDARD:
        return "standard";
    case TAXHOOK_REDUCED:
        return "reduced";
    case TAXHOOK_SUPER_REDUCED:
        return "super-reduced";
    case TAXHOOK_ZERO:
        return "zero";
    case TAXHOOK_EXEMPT:
        return "exempt";
    }
    return "?";
}

size_t taxhook_receipt_text(const taxhook_receipt_t *r, unsigned minor, char *out, size_t cap)
{
    if (!r || !out || cap == 0 || minor > 4) return 0;
    tw_t w = {out, cap, 0, false};
    if (r->flags & TAXHOOK_F_SAMPLE)
        ts(&w, "SAMPLE DATA - NOT TAX ADVICE - a tax adviser signs off per country\n");
    ts(&w, r->kind == TAXHOOK_VAT ? "VAT " : r->kind == TAXHOOK_GST ? "GST " : "Sales tax ");
    tc(&w, r->country[0]);
    tc(&w, r->country[1]);
    if (!region_empty(r->region)) {
        tc(&w, '-');
        for (unsigned i = 0; i < CP_REGION_LEN && r->region[i]; i++) tc(&w, r->region[i]);
    }
    ts(&w, " table v");
    tnum(&w, r->version, 0);
    ts(&w, r->rounding == TAXHOOK_HALF_EVEN ? ", half-even" : ", half-up");
    ts(&w, r->level == TAXHOOK_PER_LINE ? ", per line" : ", per invoice");
    ts(&w, r->inclusive ? ", prices include tax\n" : ", prices exclude tax\n");
    for (uint32_t k = 0; k < r->n_groups; k++) {
        const taxhook_group_t *g = &r->group[k];
        ts(&w, cat_name(g->category));
        tc(&w, ' ');
        trate(&w, g->rate_ppm);
        ts(&w, ": net ");
        tnum(&w, g->net, minor);
        ts(&w, " tax ");
        tnum(&w, g->tax, minor);
        ts(&w, " gross ");
        tnum(&w, g->gross, minor);
        tc(&w, '\n');
    }
    if (r->reverse_charge) ts(&w, "REVERSE CHARGE: no tax charged; the buyer accounts for it\n");
    ts(&w, "TOTAL net ");
    tnum(&w, r->net, minor);
    ts(&w, " tax ");
    tnum(&w, r->tax, minor);
    ts(&w, " gross ");
    tnum(&w, r->gross, minor);
    tc(&w, '\n');
    if (w.bad) {
        out[0] = 0;
        return 0;
    }
    out[w.len] = 0;
    return w.len;
}

const char *taxhook_status_name(taxhook_status_t s)
{
    switch (s) {
    case TAXHOOK_OK:
        return "ok";
    case TAXHOOK_E_ARG:
        return "bad_arg";
    case TAXHOOK_E_DECODE:
        return "decode";
    case TAXHOOK_E_SIG:
        return "signature";
    case TAXHOOK_E_WINDOW:
        return "window";
    case TAXHOOK_E_VERSION:
        return "version";
    case TAXHOOK_E_FULL:
        return "full";
    case TAXHOOK_E_DISABLED:
        return "disabled";
    case TAXHOOK_E_NO_TABLE:
        return "no_table";
    case TAXHOOK_E_EXPIRED:
        return "expired";
    case TAXHOOK_E_NO_RATE:
        return "no_rate";
    case TAXHOOK_E_RANGE:
        return "range";
    }
    return "unknown";
}
