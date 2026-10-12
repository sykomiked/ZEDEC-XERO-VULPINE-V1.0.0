/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zlogix.c — Logix-Clear: container lifecycle on the hexagram phase,
 * multi-jurisdiction release rules, BOTH holds for a human broker, exact
 * duties posted to the ledger, signed records. Not connected to any customs
 * authority. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlogix.h"

#define DOMAIN "logix-clear"
#define BROKER_CTX "logix-clear/broker"
#define MAX_BROKERS 16u

typedef struct {
    char code[9];
    uint32_t duty_acct;
} jur_t;

typedef struct {
    uint8_t jur;
    char hs[11];
    char origin[3];
    int32_t action;
    zind_qty rate;
} rule_t;

typedef struct {
    char hs[11];
    char origin[3];
    zind_qty value;
} line_t;

typedef struct {
    uint8_t jur;
    int32_t lp;
} ruling_t;

typedef struct {
    char container[12];
    uint8_t route[ZLC_MAX_ROUTE];
    uint8_t n_route;
    uint32_t payer;
    line_t lines[ZLC_MAX_LINES];
    uint32_t n_lines;
    ruling_t rulings[ZLC_MAX_RULINGS];
    uint32_t n_rulings;
    int32_t state;
    uint8_t tag;
    uint8_t hold_hash[32];
    int64_t posted[ZLC_MAX_ROUTE];
    zind_qty residual[ZLC_MAX_ROUTE];
} ship_t;

struct zlc_desk {
    const zxv_cap *cap;
    char ccy[4];
    uint8_t minor;
    const zind_signer *signer;
    zind_qsink *qs;
    int32_t rounding;
    jur_t jur[ZLC_MAX_JURIS];
    uint32_t n_jur;
    rule_t rules[ZLC_MAX_RULES];
    uint32_t n_rules;
    uint8_t brokers[MAX_BROKERS][32];
    uint32_t n_brokers;
    ship_t *ships;
    uint32_t n_ships, alloc;
    zind_chain *chain;
    char refp[9]; /* ledger reference prefix: the desk key id, 8 hex digits */
};

const char *zlc_status_name(int32_t status) {
    switch (status) {
    case ZLC_E_REFUSED: return "ZLC_E_REFUSED";
    case ZLC_E_PENDING: return "ZLC_E_PENDING";
    case ZLC_E_CONTAINER: return "ZLC_E_CONTAINER";
    case ZLC_E_BROKER: return "ZLC_E_BROKER";
    case ZLC_E_JURIS: return "ZLC_E_JURIS";
    default: return zind_status_name(status);
    }
}

/* ===== ISO 6346 ===== */

static int letter_value(char c) {
    int v = 10;
    if (c < 'A' || c > 'Z') return -1;
    for (char x = 'A'; x < c; x++) {
        v++;
        if (v % 11 == 0) v++;
    }
    return v;
}

int32_t zlc_container_digit(const char *s) {
    long sum = 0;
    if (!s) return -1;
    for (int i = 0; i < 10; i++) {
        int v;
        if (i < 4) {
            v = letter_value(s[i]);
            if (v < 0) return -1;
        } else {
            if (s[i] < '0' || s[i] > '9') return -1;
            v = s[i] - '0';
        }
        sum += (long) v << i;
    }
    return (int32_t) ((sum % 11) % 10);
}

int32_t zlc_container_check(const char *id) {
    int32_t d;
    if (!id || strlen(id) != 11 || (id[3] != 'U' && id[3] != 'J' && id[3] != 'Z')) goto bad;
    d = zlc_container_digit(id);
    if (d < 0 || id[10] != (char) ('0' + d)) goto bad;
    return ZXV_OK;
bad:
    zind_set_error("zlc: \"%.20s\" is not an ISO 6346 container number", id ? id : "(null)");
    return ZLC_E_CONTAINER;
}

/* ===== helpers ===== */

static int digits_ok(const char *s, size_t lo, size_t hi) {
    size_t n;
    if (!s) return 0;
    for (n = 0; s[n]; n++)
        if (s[n] < '0' || s[n] > '9' || n >= hi) return 0;
    return n >= lo;
}

static int country_ok(const char *s) {
    return s && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z' && s[2] == 0;
}

static int juris_code_ok(const char *s) {
    size_t n;
    if (!s) return 0;
    for (n = 0; s[n]; n++)
        if (!((s[n] >= 'A' && s[n] <= 'Z') || (s[n] >= '0' && s[n] <= '9')) || n >= 8) return 0;
    return n >= 2;
}

static int find_jur(const zlc_desk *d, const char *code) {
    if (!code) return -1;
    for (uint32_t i = 0; i < d->n_jur; i++)
        if (strcmp(d->jur[i].code, code) == 0) return (int) i;
    return -1;
}

static ship_t *get_ship(const zlc_desk *d, uint32_t id) {
    if (!d || id == 0 || id > d->n_ships) {
        zind_set_error("zlc: unknown shipment %u", id);
        return NULL;
    }
    return &d->ships[id - 1];
}

static int32_t append(zlc_desk *d, uint16_t kind, const zind_wr *w, uint64_t *seq,
                      uint8_t hash[32]) {
    if (w->overflow) return ZXV_E_INTERNAL;
    return zind_chain_append(d->cap, d->chain, d->signer, kind, w->buf, w->len, seq, hash);
}

static void wr_head(zind_wr *w, uint32_t id, const ship_t *s) {
    zind_wr_u32(w, id);
    zind_wr_str(w, s->container);
}

/* ===== desk ===== */

int32_t zlc_desk_create(const zxv_cap *cap, const char *ccy, const zind_signer *desk_signer,
                        zxv_hexlog *qlog, zlc_desk **out) {
    zlc_desk *d;
    uint8_t minor = 0;
    uint32_t flags = 0;
    int32_t st;
    if (!cap || !ccy || !desk_signer || !out) return ZXV_E_ARG;
    *out = NULL;
    st = zxv_cap_check(cap, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("zlc_desk_create: the cap needs LEDGER_POST, LEDGER_READ and CRYPTO_SIGN");
        return st;
    }
    st = zxv_iso4217_by_alpha(ccy, NULL, &minor, &flags);
    if (st != ZXV_OK || minor == ZXV_MINOR_NA || minor > 18) {
        zind_set_error("zlc_desk_create: \"%.8s\" is not an ISO 4217 currency with minor units",
                       ccy);
        return ZXV_E_CURRENCY;
    }
    st = zxv_ccy_enable_cap(cap, ccy);
    if (st != ZXV_OK && st != ZXV_E_DUPLICATE) return st;
    d = (zlc_desk *) calloc(1, sizeof *d);
    if (!d) return ZXV_E_NOMEM;
    d->cap = cap;
    memcpy(d->ccy, ccy, 3);
    d->minor = minor;
    d->signer = desk_signer;
    d->rounding = ZIND_ROUND_EXACT;
    {
        const uint8_t *k = zind_signer_key_id(desk_signer);
        snprintf(d->refp, sizeof d->refp, "%02x%02x%02x%02x", k[0], k[1], k[2], k[3]);
    }
    st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &d->chain);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &d->qs);
    if (st != ZXV_OK) {
        zlc_desk_destroy(d);
        return st;
    }
    *out = d;
    return ZXV_OK;
}

void zlc_desk_destroy(zlc_desk *d) {
    if (!d) return;
    zind_chain_destroy(d->chain);
    zind_qsink_destroy(d->qs);
    free(d->ships);
    free(d);
}

int32_t zlc_set_rounding(zlc_desk *d, int32_t mode) {
    if (!d || mode < ZIND_ROUND_EXACT || mode > ZIND_ROUND_TRUNC) return ZXV_E_ARG;
    d->rounding = mode;
    return ZXV_OK;
}

const zind_chain *zlc_records(const zlc_desk *d) { return d ? d->chain : NULL; }

int32_t zlc_jurisdiction_add(zlc_desk *d, const char *code, uint32_t *out_duty_account) {
    char name[16];
    uint32_t acct = 0;
    int32_t st;
    if (!d) return ZXV_E_ARG;
    if (!juris_code_ok(code)) {
        zind_set_error("zlc_jurisdiction_add: a code is 2..8 of [A-Z0-9]");
        return ZLC_E_JURIS;
    }
    if (find_jur(d, code) >= 0) return ZXV_E_DUPLICATE;
    if (d->n_jur >= ZLC_MAX_JURIS) return ZXV_E_CAPACITY;
    snprintf(name, sizeof name, "duty:%s", code);
    st = zxv_ledger_open_account_cap(d->cap, name, d->ccy, ZXV_ACCT_HOLDER, &acct);
    if (st != ZXV_OK) return st;
    memcpy(d->jur[d->n_jur].code, code, strlen(code) + 1);
    d->jur[d->n_jur].duty_acct = acct;
    d->n_jur++;
    if (out_duty_account) *out_duty_account = acct;
    return ZXV_OK;
}

int32_t zlc_rule_add(zlc_desk *d, const zlc_rule *r) {
    int j;
    int32_t c = 0;
    rule_t nr;
    if (!d || !r) return ZXV_E_ARG;
    j = find_jur(d, r->jurisdiction);
    if (j < 0) {
        zind_set_error("zlc_rule_add: unknown jurisdiction");
        return ZLC_E_JURIS;
    }
    if (!r->hs_prefix || (r->hs_prefix[0] && !digits_ok(r->hs_prefix, 1, 10)) || !r->origin ||
        (strcmp(r->origin, "*") != 0 && !country_ok(r->origin)) ||
        r->action < ZLC_RULE_RELEASE || r->action > ZLC_RULE_INSPECT ||
        !zind_qty_valid(r->duty_rate) || zind_qty_sign(r->duty_rate) < 0 ||
        zind_qty_cmp(r->duty_rate, (zind_qty){10, 1}, &c) != ZXV_OK || c > 0 ||
        (r->action != ZLC_RULE_RELEASE && r->duty_rate.num != 0)) {
        zind_set_error("zlc_rule_add: malformed rule (HS prefix 0..10 digits, origin alpha-2 or "
                       "\"*\", rate 0..10 and only on RELEASE)");
        return ZXV_E_ARG;
    }
    for (uint32_t i = 0; i < d->n_rules; i++)
        if (d->rules[i].jur == j && strcmp(d->rules[i].hs, r->hs_prefix) == 0 &&
            strcmp(d->rules[i].origin, r->origin) == 0)
            return ZXV_E_DUPLICATE;
    if (d->n_rules >= ZLC_MAX_RULES) return ZXV_E_CAPACITY;
    memset(&nr, 0, sizeof nr);
    nr.jur = (uint8_t) j;
    memcpy(nr.hs, r->hs_prefix, strlen(r->hs_prefix) + 1);
    memcpy(nr.origin, r->origin, strlen(r->origin) + 1);
    nr.action = r->action;
    nr.rate = r->duty_rate;
    d->rules[d->n_rules++] = nr;
    return ZXV_OK;
}

int32_t zlc_broker_add(zlc_desk *d, const uint8_t pk[ZIND_PK_BYTES]) {
    uint8_t id[32];
    int32_t st;
    if (!d || !pk) return ZXV_E_ARG;
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < d->n_brokers; i++)
        if (memcmp(d->brokers[i], id, 32) == 0) return ZXV_E_DUPLICATE;
    if (d->n_brokers >= MAX_BROKERS) return ZXV_E_CAPACITY;
    memcpy(d->brokers[d->n_brokers++], id, 32);
    return ZXV_OK;
}

/* ===== shipments ===== */

int32_t zlc_book(zlc_desk *d, const char *container, const char *const *route, size_t n_route,
                 uint32_t payer_account, uint32_t *out_shipment) {
    ship_t s;
    char acc_ccy[16];
    size_t n = 0;
    int32_t st;
    if (!d || !route || !out_shipment) return ZXV_E_ARG;
    st = zlc_container_check(container);
    if (st != ZXV_OK) return st;
    if (n_route == 0 || n_route > ZLC_MAX_ROUTE) {
        zind_set_error("zlc_book: a route has 1..%u jurisdictions", ZLC_MAX_ROUTE);
        return ZXV_E_ARG;
    }
    memset(&s, 0, sizeof s);
    for (size_t i = 0; i < n_route; i++) {
        int j = find_jur(d, route[i]);
        if (j < 0) {
            zind_set_error("zlc_book: route entry %zu is not a registered jurisdiction", i);
            return ZLC_E_JURIS;
        }
        for (size_t k = 0; k < i; k++)
            if (s.route[k] == j) {
                zind_set_error("zlc_book: jurisdiction %s repeats on the route", route[i]);
                return ZXV_E_DUPLICATE;
            }
        s.route[i] = (uint8_t) j;
    }
    st = zxv_ledger_account_ccy_cap(d->cap, payer_account, acc_ccy, sizeof acc_ccy, &n);
    if (st != ZXV_OK) return st;
    if (strcmp(acc_ccy, d->ccy) != 0) {
        zind_set_error("zlc_book: the payer account is in %s, the desk in %s", acc_ccy, d->ccy);
        return ZXV_E_CURRENCY;
    }
    for (uint32_t i = 0; i < d->n_ships; i++)
        if (strcmp(d->ships[i].container, container) == 0 && d->ships[i].state != ZLC_ST_CLOSED &&
            d->ships[i].state != ZLC_ST_REFUSED) {
            zind_set_error("zlc_book: container %s already has an open shipment (%u)", container,
                           i + 1);
            return ZXV_E_DUPLICATE;
        }
    if (d->n_ships >= ZLC_MAX_SHIPMENTS) return ZXV_E_CAPACITY;
    if (d->n_ships == d->alloc) {
        uint32_t a = d->alloc ? d->alloc * 2 : 16;
        ship_t *ns = (ship_t *) realloc(d->ships, (size_t) a * sizeof *ns);
        if (!ns) return ZXV_E_NOMEM;
        d->ships = ns;
        d->alloc = a;
    }
    memcpy(s.container, container, 12);
    s.n_route = (uint8_t) n_route;
    s.payer = payer_account;
    s.state = ZLC_ST_BOOKED;
    st = zind_tag(ZXV_LP_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &s.tag);
    if (st != ZXV_OK) return st;
    {
        uint8_t buf[256];
        zind_wr w;
        zind_wr_init(&w, buf, sizeof buf);
        wr_head(&w, d->n_ships + 1, &s);
        zind_wr_u8(&w, s.n_route);
        for (size_t i = 0; i < n_route; i++) zind_wr_str(&w, d->jur[s.route[i]].code);
        zind_wr_u32(&w, payer_account);
        st = append(d, ZLC_REC_BOOK, &w, NULL, NULL);
        if (st != ZXV_OK) return st;
    }
    d->ships[d->n_ships++] = s;
    *out_shipment = d->n_ships;
    return ZXV_OK;
}

int32_t zlc_line_add(zlc_desk *d, uint32_t shipment, const char *hs_code, const char *origin,
                     zind_qty value) {
    ship_t *s = get_ship(d, shipment);
    line_t *l;
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!digits_ok(hs_code, 6, 10) || !country_ok(origin) || !zind_qty_valid(value) ||
        zind_qty_sign(value) <= 0) {
        zind_set_error("zlc_line_add: HS code 6..10 digits, origin alpha-2, value > 0");
        return ZXV_E_ARG;
    }
    if (s->state != ZLC_ST_BOOKED) {
        zind_set_error("zlc_line_add: lines are locked once the container departs");
        return ZXV_E_STATE;
    }
    if (s->n_lines >= ZLC_MAX_LINES) return ZXV_E_CAPACITY;
    l = &s->lines[s->n_lines++];
    memcpy(l->hs, hs_code, strlen(hs_code) + 1);
    memcpy(l->origin, origin, 3);
    l->value = value;
    return ZXV_OK;
}

int32_t zlc_depart(zlc_desk *d, uint32_t shipment) {
    ship_t *s = get_ship(d, shipment);
    uint8_t nt = 0, buf[64];
    zind_wr w;
    int32_t st;
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (s->state != ZLC_ST_BOOKED) return ZXV_E_STATE;
    if (s->n_lines == 0) {
        zind_set_error("zlc_depart: a shipment needs at least one declared line");
        return ZXV_E_STATE;
    }
    st = zxv_hex_step(s->tag, ZXV_HEX_PHASE_EMERGING, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    wr_head(&w, shipment, s);
    st = append(d, ZLC_REC_DEPART, &w, NULL, NULL);
    if (st != ZXV_OK) return st;
    s->tag = nt;
    s->state = ZLC_ST_IN_TRANSIT;
    return ZXV_OK;
}

int32_t zlc_ruling_record(zlc_desk *d, uint32_t shipment, const char *jurisdiction, int32_t lp) {
    ship_t *s = get_ship(d, shipment);
    int j, on_route = 0;
    uint8_t buf[64];
    zind_wr w;
    int32_t st;
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (lp != ZXV_LP_TRUE && lp != ZXV_LP_FALSE) {
        zind_set_error("zlc_ruling_record: a ruling is ZXV_LP_TRUE or ZXV_LP_FALSE");
        return ZXV_E_ARG;
    }
    j = find_jur(d, jurisdiction);
    for (uint32_t i = 0; j >= 0 && i < s->n_route; i++) on_route |= s->route[i] == j;
    if (!on_route) {
        zind_set_error("zlc_ruling_record: the jurisdiction is not on this shipment's route");
        return ZLC_E_JURIS;
    }
    if (s->state != ZLC_ST_IN_TRANSIT && s->state != ZLC_ST_HELD) return ZXV_E_STATE;
    if (s->n_rulings >= ZLC_MAX_RULINGS) return ZXV_E_CAPACITY;
    zind_wr_init(&w, buf, sizeof buf);
    wr_head(&w, shipment, s);
    zind_wr_str(&w, jurisdiction);
    zind_wr_u8(&w, (uint8_t) lp);
    st = append(d, ZLC_REC_RULING, &w, NULL, NULL);
    if (st != ZXV_OK) return st;
    s->rulings[s->n_rulings].jur = (uint8_t) j;
    s->rulings[s->n_rulings].lp = lp;
    s->n_rulings++;
    return ZXV_OK;
}

/* The best rule of jurisdiction j for a line: the longest HS prefix, then
 * an exact origin over "*". NULL if none. */
static const rule_t *match(const zlc_desk *d, int j, const line_t *l) {
    const rule_t *best = NULL;
    size_t best_len = 0;
    int best_exact = 0;
    for (uint32_t i = 0; i < d->n_rules; i++) {
        const rule_t *r = &d->rules[i];
        size_t pl = strlen(r->hs);
        int exact;
        if (r->jur != j || strncmp(r->hs, l->hs, pl) != 0) continue;
        exact = strcmp(r->origin, l->origin) == 0;
        if (!exact && strcmp(r->origin, "*") != 0) continue;
        if (!best || pl > best_len || (pl == best_len && exact && !best_exact)) {
            best = r;
            best_len = pl;
            best_exact = exact;
        }
    }
    return best;
}

static int32_t juris_lp(const zlc_desk *d, const ship_t *s, int j, int32_t *out) {
    int32_t table = ZXV_LP_TRUE, st;
    uint32_t pos = 0, neg = 0;
    for (uint32_t i = 0; i < s->n_lines; i++) {
        const rule_t *r = match(d, j, &s->lines[i]);
        int32_t line = !r                              ? ZXV_LP_NEITHER
                       : r->action == ZLC_RULE_RELEASE ? ZXV_LP_TRUE
                       : r->action == ZLC_RULE_REFUSE  ? ZXV_LP_FALSE
                                                       : ZXV_LP_NEITHER;
        st = zxv_lp_and(table, line, &table);
        if (st != ZXV_OK) return st;
    }
    if (s->n_lines == 0) table = ZXV_LP_NEITHER;
    pos += table == ZXV_LP_TRUE;
    neg += table == ZXV_LP_FALSE;
    for (uint32_t i = 0; i < s->n_rulings; i++)
        if (s->rulings[i].jur == j) {
            pos += s->rulings[i].lp == ZXV_LP_TRUE;
            neg += s->rulings[i].lp == ZXV_LP_FALSE;
        }
    return zxv_lp_from_evidence(pos, neg, out);
}

static int32_t evaluate(const zlc_desk *d, const ship_t *s, int32_t *out_lp, int32_t *per,
                        size_t per_cap) {
    uint32_t pos = 0, neg = 0, both = 0;
    for (uint32_t i = 0; i < s->n_route; i++) {
        int32_t lp = ZXV_LP_NEITHER, st = juris_lp(d, s, s->route[i], &lp);
        if (st != ZXV_OK) return st;
        if (per && i < per_cap) per[i] = lp;
        pos += lp == ZXV_LP_TRUE;
        neg += lp == ZXV_LP_FALSE;
        both += lp == ZXV_LP_BOTH;
    }
    if (both || (pos && neg)) *out_lp = ZXV_LP_BOTH;
    else if (neg) *out_lp = ZXV_LP_FALSE;
    else if (pos == s->n_route) *out_lp = ZXV_LP_TRUE;
    else *out_lp = ZXV_LP_NEITHER;
    return ZXV_OK;
}

int32_t zlc_evaluate(const zlc_desk *d, uint32_t shipment, int32_t *out_lp, int32_t *per_juris,
                     size_t per_juris_cap) {
    const ship_t *s = get_ship(d, shipment);
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!out_lp) return ZXV_E_ARG;
    return evaluate(d, s, out_lp, per_juris, per_juris_cap);
}

/* Exact duty owed to route entry i. */
static int32_t duty_exact(const zlc_desk *d, const ship_t *s, uint32_t i, zind_qty *out) {
    zind_qty sum = ZIND_QTY_ZERO;
    for (uint32_t k = 0; k < s->n_lines; k++) {
        const rule_t *r = match(d, s->route[i], &s->lines[k]);
        zind_qty part;
        int32_t st;
        if (!r || r->action != ZLC_RULE_RELEASE || r->rate.num == 0) continue;
        st = zind_qty_mul(s->lines[k].value, r->rate, &part);
        if (st == ZXV_OK) st = zind_qty_add(sum, part, &sum);
        if (st != ZXV_OK) {
            zind_set_error("zlc: the duty for %s overflows the exact range", d->jur[s->route[i]].code);
            return st;
        }
    }
    *out = sum;
    return ZXV_OK;
}

static void quarantine(zlc_desk *d, uint32_t shipment, const ship_t *s, uint8_t reason) {
    uint8_t subj[32];
    if (!d->qs) return;
    if (zind_subject("logix-clear/shipment", (const uint8_t *) s->container, strlen(s->container),
                     subj) != ZXV_OK)
        return;
    (void) zind_qsink_event(d->qs, s->tag, reason, shipment, subj, NULL);
}

static int32_t do_release(zlc_desk *d, uint32_t shipment, ship_t *s, const uint8_t *broker_id,
                          zlc_release_info *out) {
    int64_t minor[ZLC_MAX_ROUTE] = {0}, total = 0, eq = 0;
    zind_qty exact[ZLC_MAX_ROUTE], res[ZLC_MAX_ROUTE];
    uint64_t entry[ZLC_MAX_ROUTE] = {0}, seq = 0;
    uint32_t count = 0;
    uint8_t nt = 0, buf[1024];
    zind_wr w;
    int32_t st;

    for (uint32_t i = 0; i < s->n_route; i++) {
        st = duty_exact(d, s, i, &exact[i]);
        if (st != ZXV_OK) return st;
        st = zind_qty_to_minor(exact[i], d->minor, d->rounding, &minor[i], &res[i]);
        if (st != ZXV_OK) {
            if (st == ZIND_E_INEXACT)
                zind_set_error("zlc_release: the %s duty has no exact form in %s minor units; set "
                               "a rounding rule (zlc_set_rounding)",
                               d->jur[s->route[i]].code, d->ccy);
            return st;
        }
        if (minor[i] < 0) return ZXV_E_INTERNAL;
        if (minor[i] > ZXV_AMOUNT_MAX - total) return ZXV_E_RANGE;
        total += minor[i];
        count += minor[i] > 0;
    }
    st = zxv_ledger_balance_cap(d->cap, s->payer, NULL, NULL, &eq);
    if (st != ZXV_OK) return st;
    if (eq < total) {
        zind_set_error("zlc_release: the payer holds %lld minor units, duties are %lld",
                       (long long) eq, (long long) total);
        return ZXV_E_FUNDS;
    }
    for (uint32_t i = 0; i < s->n_route; i++) {
        char ref[36];
        if (minor[i] == 0) continue;
        snprintf(ref, sizeof ref, "LC%s-%u-%s", d->refp, shipment, d->jur[s->route[i]].code);
        st = zxv_ledger_post_cap(d->cap, s->payer, d->jur[s->route[i]].duty_acct, minor[i],
                                 d->ccy, ref, &entry[i]);
        if (st != ZXV_OK) {
            /* reserved funds were checked above; a failure here is reported as is */
            zind_set_error("zlc_release: posting the %s duty failed (%s)",
                           d->jur[s->route[i]].code, zxv_status_name(st));
            return st;
        }
        s->posted[i] = minor[i];
    }
    for (uint32_t i = 0; i < s->n_route; i++) s->residual[i] = res[i];
    zind_wr_init(&w, buf, sizeof buf);
    wr_head(&w, shipment, s);
    zind_wr_u8(&w, broker_id ? 1 : 0);
    if (broker_id) zind_wr_bytes(&w, broker_id, 32);
    zind_wr_str(&w, d->ccy);
    zind_wr_u8(&w, (uint8_t) d->rounding);
    zind_wr_u8(&w, s->n_route);
    for (uint32_t i = 0; i < s->n_route; i++) {
        zind_wr_str(&w, d->jur[s->route[i]].code);
        zind_wr_qty(&w, exact[i]);
        zind_wr_u64(&w, (uint64_t) minor[i]);
        zind_wr_qty(&w, res[i]);
        zind_wr_u64(&w, entry[i]);
    }
    zind_wr_u64(&w, (uint64_t) total);
    st = zxv_hex_set(s->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st == ZXV_OK) st = zxv_hex_step(nt, ZXV_HEX_PHASE_MANIFEST, &nt);
    if (st == ZXV_OK) st = append(d, ZLC_REC_RELEASE, &w, &seq, NULL);
    if (st != ZXV_OK) return st;
    s->tag = nt;
    s->state = ZLC_ST_RELEASED;
    if (out) {
        out->lp = ZXV_LP_TRUE;
        out->total_minor = total;
        out->duty_count = count;
        out->record_seq = seq;
    }
    return ZXV_OK;
}

static int32_t refuse(zlc_desk *d, uint32_t shipment, ship_t *s, const uint8_t *broker_id) {
    uint8_t nt = 0, buf[128];
    zind_wr w;
    int32_t st = zxv_hex_set(s->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_NEUTRAL, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    wr_head(&w, shipment, s);
    zind_wr_u8(&w, broker_id ? 1 : 0);
    if (broker_id) zind_wr_bytes(&w, broker_id, 32);
    st = append(d, ZLC_REC_REFUSE, &w, NULL, NULL);
    if (st != ZXV_OK) return st;
    s->tag = nt;
    s->state = ZLC_ST_REFUSED;
    return ZXV_OK;
}

int32_t zlc_release(zlc_desk *d, uint32_t shipment, zlc_release_info *out) {
    ship_t *s = get_ship(d, shipment);
    int32_t lp = ZXV_LP_NEITHER, st;
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    switch (s->state) {
    case ZLC_ST_IN_TRANSIT: break;
    case ZLC_ST_HELD:
        zind_set_error("zlc_release: shipment %u is held (BOTH) for a human broker", shipment);
        return ZIND_E_HELD;
    case ZLC_ST_REFUSED: return ZLC_E_REFUSED;
    case ZLC_ST_RELEASED:
        zind_set_error("zlc_release: shipment %u was already released", shipment);
        return ZIND_E_REPLAY;
    default:
        zind_set_error("zlc_release: shipment %u is not in transit", shipment);
        return ZXV_E_STATE;
    }
    st = evaluate(d, s, &lp, NULL, 0);
    if (st != ZXV_OK) return st;
    if (lp == ZXV_LP_TRUE) return do_release(d, shipment, s, NULL, out);
    if (lp == ZXV_LP_FALSE) {
        st = refuse(d, shipment, s, NULL);
        return st == ZXV_OK ? ZLC_E_REFUSED : st;
    }
    if (lp == ZXV_LP_NEITHER) {
        zind_set_error("zlc_release: shipment %u is waiting for rulings", shipment);
        return ZLC_E_PENDING;
    }
    /* BOTH: hold and freeze for a human broker */
    {
        uint8_t nt = 0, buf[64], h[32];
        zind_wr w;
        st = zxv_hex_set(s->tag, ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, &nt);
        if (st != ZXV_OK) return st;
        zind_wr_init(&w, buf, sizeof buf);
        wr_head(&w, shipment, s);
        st = append(d, ZLC_REC_HOLD, &w, NULL, h);
        if (st != ZXV_OK) return st;
        s->tag = nt;
        s->state = ZLC_ST_HELD;
        memcpy(s->hold_hash, h, 32);
        quarantine(d, shipment, s, ZIND_REASON_HELD_BOTH);
    }
    zind_set_error("zlc_release: conflicting rulings; shipment %u is held for a broker", shipment);
    return ZIND_E_HELD;
}

static int32_t broker_msg(const zlc_desk *d, uint32_t shipment, const ship_t *s, int32_t decision,
                          uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "zlc/broker/v1", 13);
    wr_head(&w, shipment, s);
    zind_wr_bytes(&w, s->hold_hash, 32);
    zind_wr_u8(&w, (uint8_t) decision);
    (void) d;
    *out_len = 13 + 4 + 1 + strlen(s->container) + 32 + 1;
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

int32_t zlc_broker_message(const zlc_desk *d, uint32_t shipment, int32_t decision, uint8_t *buf,
                           size_t cap, size_t *out_len) {
    const ship_t *s = get_ship(d, shipment);
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!out_len || (decision != ZXV_LP_TRUE && decision != ZXV_LP_FALSE)) return ZXV_E_ARG;
    if (s->state != ZLC_ST_HELD) return ZXV_E_STATE;
    return broker_msg(d, shipment, s, decision, buf, cap, out_len);
}

int32_t zlc_broker_resolve(zlc_desk *d, uint32_t shipment, const uint8_t broker_pk[ZIND_PK_BYTES],
                           int32_t decision, const uint8_t sig[ZIND_SIG_BYTES],
                           zlc_release_info *out) {
    ship_t *s = get_ship(d, shipment);
    uint8_t id[32], msg[128], buf[128], nt = 0;
    size_t len = 0;
    uint32_t i;
    zind_wr w;
    int32_t st;
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (!broker_pk || !sig || (decision != ZXV_LP_TRUE && decision != ZXV_LP_FALSE))
        return ZXV_E_ARG;
    st = zxv_cap_check(d->cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zind_set_error("zlc_broker_resolve: the desk cap needs ATTEST to apply a decision");
        return st;
    }
    if (s->state != ZLC_ST_HELD) {
        zind_set_error("zlc_broker_resolve: shipment %u is not held", shipment);
        return s->state == ZLC_ST_RELEASED || s->state == ZLC_ST_REFUSED ? ZIND_E_REPLAY
                                                                         : ZXV_E_STATE;
    }
    st = zind_key_id(broker_pk, id);
    if (st != ZXV_OK) return st;
    for (i = 0; i < d->n_brokers; i++)
        if (memcmp(d->brokers[i], id, 32) == 0) break;
    if (i == d->n_brokers) {
        zind_set_error("zlc_broker_resolve: the key is not a registered broker");
        return ZLC_E_BROKER;
    }
    st = broker_msg(d, shipment, s, decision, msg, sizeof msg, &len);
    if (st != ZXV_OK) return st;
    st = zind_verify(d->cap, broker_pk, msg, len, BROKER_CTX, sig);
    if (st != ZXV_OK) {
        uint8_t ftag = 0, subj[32];
        zind_set_error("zlc_broker_resolve: the broker's signature does not verify");
        /* a forged decision is quarantined as its own forbidden work item */
        if (d->qs && zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING,
                              &ftag) == ZXV_OK &&
            zind_subject("logix-clear/forged", sig, ZIND_SIG_BYTES, subj) == ZXV_OK)
            (void) zind_qsink_event(d->qs, ftag, ZIND_REASON_TAMPER, shipment, subj, NULL);
        return st;
    }
    /* the broker's decision record, then the action */
    zind_wr_init(&w, buf, sizeof buf);
    wr_head(&w, shipment, s);
    zind_wr_bytes(&w, id, 32);
    zind_wr_u8(&w, (uint8_t) decision);
    {
        uint8_t sh[32];
        st = zxv_sha3_256(sig, ZIND_SIG_BYTES, sh);
        if (st != ZXV_OK) return st;
        zind_wr_bytes(&w, sh, 32);
    }
    if (decision == ZXV_LP_TRUE) {
        /* release needs a non-BOTH base first: the broker settled the conflict */
        int32_t prev_state = s->state;
        uint8_t prev_tag = s->tag;
        st = zxv_hex_set(s->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &nt);
        if (st != ZXV_OK) return st;
        s->tag = nt;
        s->state = ZLC_ST_IN_TRANSIT;
        st = do_release(d, shipment, s, id, out);
        if (st != ZXV_OK) {
            s->tag = prev_tag;
            s->state = prev_state;
            return st;
        }
        return append(d, ZLC_REC_BROKER, &w, NULL, NULL);
    }
    st = refuse(d, shipment, s, id);
    if (st != ZXV_OK) return st;
    return append(d, ZLC_REC_BROKER, &w, NULL, NULL);
}

int32_t zlc_close(zlc_desk *d, uint32_t shipment) {
    ship_t *s = get_ship(d, shipment);
    uint8_t nt = 0, buf[64];
    zind_wr w;
    int32_t st;
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (s->state != ZLC_ST_RELEASED) {
        zind_set_error("zlc_close: only a released shipment can be closed");
        return s->state == ZLC_ST_HELD ? ZIND_E_HELD : ZXV_E_STATE;
    }
    st = zxv_hex_step(s->tag, ZXV_HEX_PHASE_DISSOLVING, &nt);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    wr_head(&w, shipment, s);
    st = append(d, ZLC_REC_CLOSE, &w, NULL, NULL);
    if (st != ZXV_OK) return st;
    s->tag = nt;
    s->state = ZLC_ST_CLOSED;
    return ZXV_OK;
}

int32_t zlc_shipment_state(const zlc_desk *d, uint32_t shipment, int32_t *out_state,
                           uint8_t *out_tag) {
    const ship_t *s = get_ship(d, shipment);
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    if (out_state) *out_state = s->state;
    if (out_tag) *out_tag = s->tag;
    return ZXV_OK;
}

int32_t zlc_duty(const zlc_desk *d, uint32_t shipment, const char *jurisdiction,
                 zind_qty *out_exact, int64_t *out_posted_minor, zind_qty *out_residual) {
    const ship_t *s = get_ship(d, shipment);
    int j;
    if (!s) return d ? ZXV_E_NOT_FOUND : ZXV_E_ARG;
    j = find_jur(d, jurisdiction);
    for (uint32_t i = 0; j >= 0 && i < s->n_route; i++) {
        if (s->route[i] != j) continue;
        if (out_exact) {
            int32_t st = duty_exact(d, s, i, out_exact);
            if (st != ZXV_OK) return st;
        }
        if (out_posted_minor) *out_posted_minor = s->posted[i];
        if (out_residual)
            *out_residual =
                (s->state == ZLC_ST_RELEASED || s->state == ZLC_ST_CLOSED) ? s->residual[i]
                                                                           : ZIND_QTY_ZERO;
        return ZXV_OK;
    }
    zind_set_error("zlc_duty: the jurisdiction is not on this shipment's route");
    return ZLC_E_JURIS;
}
