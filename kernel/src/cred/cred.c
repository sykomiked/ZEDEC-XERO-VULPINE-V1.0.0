/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cred.c — EigenTrust credibility. See cred.h C1-C11. */
#include "cred.h"

#define KIND_RECEIPT 1u
#define KIND_RULING  2u

void cred_config_default(cred_config_t *cfg)
{
    if (!cfg) return;
    pay_memset(cfg, 0, sizeof *cfg);
    cfg->decay_num = 1;
    cfg->decay_den = 16;
    cfg->fail_base = 16;
    cfg->fail_cap = 10;
    cfg->alpha_num = 1;
    cfg->alpha_den = 10;
    cfg->iters = 64;
    cfg->eps = 64;
    cfg->hold_enabled = false;
    cfg->hold_base = (uint64_t) 14u * 86400u; /* ticks are seconds */
    cfg->hold_floor = (uint64_t) 3u * 86400u;
    cfg->hold_sat = CRED_SCALE >> 3;
    cfg->credit_lines_enabled = false;
    cfg->credit_k = 100000u;
    cfg->credit_cap = 50000u;
}

bool cred_config_ok(const cred_config_t *f)
{
    if (!f || !f->decay_den || f->decay_num > f->decay_den) return false;
    if (!f->fail_base || f->fail_base > CRED_FAIL_BASE_MAX || f->fail_cap > CRED_FAIL_SHIFT_MAX)
        return false;
    if (!f->alpha_den || f->alpha_num > f->alpha_den) return false;
    if (!f->iters || f->iters > CRED_ITERS_MAX) return false;
    if (!f->hold_base || f->hold_floor > f->hold_base || !f->hold_sat) return false;
    return true;
}

cred_status_t cred_init(cred_t *c, const cred_config_t *cfg)
{
    cred_config_t d;
    if (!c) return CRED_ERR_ARG;
    if (!cfg) {
        cred_config_default(&d);
        cfg = &d;
    }
    if (!cred_config_ok(cfg)) return CRED_ERR_ARG;
    pay_memset(c, 0, sizeof *c);
    c->cfg = *cfg;
    return CRED_OK;
}

cred_status_t cred_set_config(cred_t *c, const cred_config_t *cfg)
{
    if (!c || !cred_config_ok(cfg)) return CRED_ERR_ARG;
    c->cfg = *cfg;
    return CRED_OK;
}

int32_t cred_index(const cred_t *c, uint32_t id)
{
    for (uint32_t i = 0; c && i < c->n; i++)
        if (c->subj[i].id == id) return (int32_t) i;
    return -1;
}

cred_status_t cred_subject_add(cred_t *c, uint32_t id, bool pretrusted)
{
    if (!c) return CRED_ERR_ARG;
    if (cred_index(c, id) >= 0) return CRED_ERR_DUP;
    if (c->n >= CRED_MAX_SUBJECTS) return CRED_ERR_FULL;
    cred_subject_t *s = &c->subj[c->n++];
    s->id = id;
    s->pretrusted = pretrusted;
    s->fails = 0;
    c->computed = false;
    return CRED_OK;
}

cred_status_t cred_set_pretrusted(cred_t *c, uint32_t id, bool pretrusted)
{
    if (!c) return CRED_ERR_ARG;
    int32_t i = cred_index(c, id);
    if (i < 0) return CRED_ERR_UNKNOWN;
    c->subj[i].pretrusted = pretrusted;
    c->computed = false;
    return CRED_OK;
}

/* ---- C2 / C3 ---- */
uint64_t cred_gain(uint64_t value)
{
    if (value == UINT64_MAX) return 65; /* 1 + v == 2^64 */
    uint64_t x = value + 1, b = 0;
    while (x) {
        b++;
        x >>= 1;
    }
    return b;
}

uint64_t cred_loss(const cred_config_t *cfg, uint32_t k)
{
    if (!cfg || k == 0) return 0;
    uint32_t sh = k - 1 < cfg->fail_cap ? k - 1 : cfg->fail_cap;
    return cfg->fail_base << sh;
}

/* ---- C1 ---- */
static bool seen_has(const cred_t *c, uint8_t kind, const uint8_t id[32])
{
    for (uint32_t i = 0; i < c->n_seen; i++)
        if (c->seen_kind[i] == kind && pay_memeq(c->seen[i], id, 32)) return true;
    return false;
}

static void seen_put(cred_t *c, uint8_t kind, const uint8_t id[32])
{
    pay_memcpy(c->seen[c->seen_head], id, 32);
    c->seen_kind[c->seen_head] = kind;
    c->seen_head = (c->seen_head + 1u) % CRED_SEEN;
    if (c->n_seen < CRED_SEEN) c->n_seen++;
}

static cred_status_t pair(const cred_t *c, uint32_t rater, uint32_t ratee, const uint8_t *id,
                          int32_t *i, int32_t *j)
{
    if (!c || !id) return CRED_ERR_ARG;
    *i = cred_index(c, rater);
    *j = cred_index(c, ratee);
    if (*i < 0 || *j < 0) return CRED_ERR_UNKNOWN;
    if (*i == *j) return CRED_ERR_SELF;
    return CRED_OK;
}

cred_status_t cred_fulfil(cred_t *c, uint32_t rater, uint32_t ratee, uint64_t value,
                          const uint8_t receipt[32])
{
    int32_t i, j;
    cred_status_t st = pair(c, rater, ratee, receipt, &i, &j);
    if (st != CRED_OK) return st;
    if (value == 0) return CRED_ERR_ARG;
    if (seen_has(c, KIND_RECEIPT, receipt)) return CRED_ERR_DUP;
    uint64_t old = c->s[i][j], nv = old + cred_gain(value); /* old <= 2^40: no overflow */
    if (nv > CRED_LOCAL_MAX) nv = CRED_LOCAL_MAX;
    c->s[i][j] = nv;
    c->s_total += nv - old; /* s_total <= 64*64*2^40 */
    seen_put(c, KIND_RECEIPT, receipt);
    c->n_fulfil++;
    return CRED_OK;
}

cred_status_t cred_fail(cred_t *c, uint32_t rater, uint32_t ratee, const uint8_t ruling[32])
{
    int32_t i, j;
    cred_status_t st = pair(c, rater, ratee, ruling, &i, &j);
    if (st != CRED_OK) return st;
    if (seen_has(c, KIND_RULING, ruling)) return CRED_ERR_DUP;
    cred_subject_t *s = &c->subj[j];
    if (s->fails < UINT32_MAX) s->fails++;
    uint64_t loss = cred_loss(&c->cfg, s->fails), old = c->s[i][j];
    uint64_t nv = old > loss ? old - loss : 0;
    c->s[i][j] = nv;
    c->s_total -= old - nv;
    seen_put(c, KIND_RULING, ruling);
    c->n_fail++;
    return CRED_OK;
}

/* ---- C4 ---- */
cred_status_t cred_decay_epoch(cred_t *c)
{
    if (!c) return CRED_ERR_ARG;
    uint64_t keep = c->cfg.decay_den - c->cfg.decay_num, total = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        for (uint32_t j = 0; j < c->n; j++) {
            uint64_t v;
            pay_muldiv(c->s[i][j], keep, c->cfg.decay_den, &v, 0); /* v <= s: fits */
            c->s[i][j] = v;
            total += v;
        }
        c->subj[i].fails >>= 1;
    }
    c->s_total = total;
    c->epoch++;
    return CRED_OK;
}

/* ---- C5 ---- */
static void make_p(cred_t *c)
{
    uint32_t P = 0, rank = 0;
    for (uint32_t i = 0; i < c->n; i++) P += c->subj[i].pretrusted;
    bool all = P == 0;
    if (all) P = c->n;
    uint64_t rem, share = pay_udiv64(CRED_SCALE, P, &rem);
    for (uint32_t i = 0; i < CRED_MAX_SUBJECTS; i++) {
        c->p[i] = 0;
        if (i < c->n && (all || c->subj[i].pretrusted)) c->p[i] = share + (rank++ < rem);
    }
}

static uint64_t mdiv(uint64_t a, uint64_t b, uint64_t d)
{
    uint64_t q;
    pay_muldiv(a, b, d, &q, 0); /* every caller has a * b / d <= a or <= b */
    return q;
}

cred_status_t cred_trust_compute(cred_t *c)
{
    if (!c) return CRED_ERR_ARG;
    if (c->n == 0) return CRED_ERR_STATE;
    uint32_t n = c->n;
    uint64_t row[CRED_MAX_SUBJECTS], nt[CRED_MAX_SUBJECTS];
    make_p(c);
    for (uint32_t i = 0; i < CRED_MAX_SUBJECTS; i++) c->t[i] = c->p[i];
    for (uint32_t i = 0; i < n; i++) {
        row[i] = 0;
        for (uint32_t j = 0; j < n; j++) row[i] += c->s[i][j];
    }
    uint64_t keep = c->cfg.alpha_den - c->cfg.alpha_num, delta = 0;
    uint32_t it = 0;
    while (it < c->cfg.iters) {
        uint64_t flow = 0, tele = 0;
        for (uint32_t j = 0; j < n; j++) nt[j] = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint64_t give = mdiv(c->t[i], keep, c->cfg.alpha_den);
            if (!give) continue;
            for (uint32_t j = 0; j < n; j++) {
                uint64_t w = row[i] ? c->s[i][j] : c->p[j];
                if (!w) continue;
                uint64_t f = mdiv(give, w, row[i] ? row[i] : CRED_SCALE);
                nt[j] += f;
                flow += f;
            }
        }
        uint64_t T = CRED_SCALE - flow;
        for (uint32_t j = 0; j < n; j++) {
            uint64_t x = mdiv(T, c->p[j], CRED_SCALE);
            nt[j] += x;
            tele += x;
        }
        uint64_t dust = T - tele;
        for (uint32_t j = 0; j < n && dust; j++)
            if (c->p[j]) {
                nt[j]++;
                dust--;
            }
        delta = 0;
        for (uint32_t j = 0; j < n; j++) {
            delta += nt[j] > c->t[j] ? nt[j] - c->t[j] : c->t[j] - nt[j];
            c->t[j] = nt[j];
        }
        it++;
        if (delta <= c->cfg.eps) break;
    }
    c->iters_used = it;
    c->last_delta = delta;
    c->computed = true;
    return CRED_OK;
}

/* ---- readers ---- */
uint64_t cred_local(const cred_t *c, uint32_t rater, uint32_t ratee)
{
    int32_t i = cred_index(c, rater), j = cred_index(c, ratee);
    return (i < 0 || j < 0) ? 0 : c->s[i][j];
}

uint64_t cred_trust(const cred_t *c, uint32_t id)
{
    int32_t j = cred_index(c, id);
    return (j < 0 || !c->computed) ? 0 : c->t[j];
}

uint64_t cred_credibility(const cred_t *c, uint32_t id)
{
    uint64_t t = cred_trust(c, id);
    if (!t) return 0;
    uint64_t q = mdiv(t, (uint64_t) c->n * 100u, CRED_SCALE); /* <= 6400 */
    return q < CRED_CARD_CRED_MAX ? q : CRED_CARD_CRED_MAX;
}

/* ---- C8 / C9 ---- */
uint64_t cred_escrow_hold(const cred_config_t *cfg, uint64_t score)
{
    cred_config_t d;
    if (!cfg) {
        cred_config_default(&d);
        cfg = &d;
    }
    if (!cred_config_ok(cfg)) return 0;
    if (!cfg->hold_enabled) return cfg->hold_base;
    uint64_t sc = score < cfg->hold_sat ? score : cfg->hold_sat;
    return cfg->hold_base - mdiv(cfg->hold_base - cfg->hold_floor, sc, cfg->hold_sat);
}

uint64_t cred_credit_line(const cred_config_t *cfg, uint64_t score)
{
    cred_config_t d;
    uint64_t q;
    if (!cfg) {
        cred_config_default(&d);
        cfg = &d;
    }
    if (!cfg->credit_lines_enabled) return 0;
    if (!pay_muldiv(score, cfg->credit_k, CRED_SCALE, &q, 0)) return cfg->credit_cap;
    return q < cfg->credit_cap ? q : cfg->credit_cap;
}

/* ---- C10 ---- */
cred_status_t cred_player_card(const cred_t *c, uint32_t subject, uint64_t bond,
                               uint32_t uptime_bps, uint64_t giving, const sic_tier_table_t *tab,
                               cred_card_t *out)
{
    sic_tier_table_t def;
    if (!c || !out) return CRED_ERR_ARG;
    int32_t j = cred_index(c, subject);
    if (j < 0) return CRED_ERR_UNKNOWN;
    if (!c->computed) return CRED_ERR_STATE;
    if (!tab) {
        sic_tier_table_default(&def);
        tab = &def;
    }
    pay_memset(out, 0, sizeof *out);
    out->subject = subject;
    out->trust = c->t[j];
    out->credibility = cred_credibility(c, subject);
    for (uint32_t i = 0; i < c->n; i++) out->local += c->s[i][j];
    out->bond = bond;
    out->giving = giving;
    out->uptime_bps = uptime_bps;
    sic_tier_t tier = sic_tier_eval(tab, out->credibility, bond, uptime_bps, giving);
    out->tier = (uint8_t) tier;
    out->perks = sic_tier_perks(tier);
    if ((uint32_t) tier < SIC_EARNED_TIERS) {
        out->has_next = true;
        out->next = tab->t[tier];
        if (sic_effective_cred(out->credibility, giving) < out->next.cred)
            out->missing |= CRED_MISS_CRED;
        if (bond < out->next.bond) out->missing |= CRED_MISS_BOND;
        if (uptime_bps < out->next.uptime_bps || uptime_bps > SIC_BPS_MAX)
            out->missing |= CRED_MISS_UPTIME;
    }
    out->escrow_hold = cred_escrow_hold(&c->cfg, out->trust);
    out->escrow_hold_bps = mdiv(out->escrow_hold, 10000u, c->cfg.hold_base);
    out->credit_line = cred_credit_line(&c->cfg, out->trust);
    return CRED_OK;
}

static void jstr(pay_w *w, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    pay_w_c(w, '"');
    for (; *s; s++) {
        unsigned char ch = (unsigned char) *s;
        if (ch == '"' || ch == '\\') {
            pay_w_c(w, '\\');
            pay_w_c(w, (char) ch);
        } else if (ch < 0x20u) {
            pay_w_s(w, "\\u00");
            pay_w_c(w, hex[ch >> 4]);
            pay_w_c(w, hex[ch & 15u]);
        } else {
            pay_w_c(w, (char) ch);
        }
    }
    pay_w_c(w, '"');
}

static void jnum(pay_w *w, uint64_t v)
{
    pay_w_c(w, '"');
    pay_w_u64(w, v);
    pay_w_c(w, '"');
}

static void jinput(pay_w *w, const char *key, uint64_t v, const char *src)
{
    pay_w_c(w, '"');
    pay_w_s(w, key);
    pay_w_s(w, "\":{\"value\":");
    jnum(w, v);
    pay_w_s(w, ",\"source\":");
    jstr(w, src ? src : "unspecified");
    pay_w_c(w, '}');
}

static void jtier(pay_w *w, uint32_t tier)
{
    pay_w_s(w, "\"tier\":");
    pay_w_u64(w, tier);
    pay_w_s(w, ",\"metal\":");
    jstr(w, sic_tier_name((sic_tier_t) tier, 0));
    pay_w_s(w, ",\"stone\":");
    jstr(w, sic_tier_name((sic_tier_t) tier, 1));
}

int32_t cred_card_json(const cred_card_t *k, char *buf, size_t cap)
{
    static const char *const perk[5] = {"free registration", "storefronts", "forwards", "IP notes",
                                        "cross-border licensing"};
    static const char *const miss[3] = {"credibility", "bond", "uptime_bps"};
    pay_w w;
    pay_w_init(&w, buf, cap > UINT32_MAX ? UINT32_MAX : (uint32_t) cap);
    if (!k) w.trunc = true;
    if (w.trunc) return pay_w_finish(&w);
    pay_w_c(&w, '{');
    jtier(&w, k->tier);
    pay_w_s(&w, ",\"inputs\":{");
    jinput(&w, "credibility", k->credibility, CRED_SOURCE_CRED);
    pay_w_c(&w, ',');
    jinput(&w, "bond", k->bond, k->src_bond);
    pay_w_c(&w, ',');
    jinput(&w, "uptime_bps", k->uptime_bps, k->src_uptime);
    pay_w_c(&w, ',');
    jinput(&w, "giving", k->giving, k->src_giving);
    pay_w_s(&w, "},\"next\":");
    if (!k->has_next) {
        pay_w_s(&w, "null");
    } else {
        pay_w_c(&w, '{');
        jtier(&w, k->tier + 1u);
        pay_w_s(&w, ",\"credibility\":");
        jnum(&w, k->next.cred);
        pay_w_s(&w, ",\"bond\":");
        jnum(&w, k->next.bond);
        pay_w_s(&w, ",\"uptime_bps\":");
        jnum(&w, k->next.uptime_bps);
        pay_w_s(&w, ",\"missing\":[");
        bool first = true;
        for (uint32_t b = 0; b < 3; b++)
            if (k->missing & (1u << b)) {
                if (!first) pay_w_c(&w, ',');
                jstr(&w, miss[b]);
                first = false;
            }
        pay_w_s(&w, "]}");
    }
    pay_w_s(&w, ",\"perks\":[");
    bool first = true;
    for (uint32_t b = 0; b < 5; b++)
        if (k->perks & (1u << b)) {
            if (!first) pay_w_c(&w, ',');
            jstr(&w, perk[b]);
            first = false;
        }
    pay_w_s(&w, "],\"score\":");
    jnum(&w, k->trust);
    pay_w_s(&w, ",\"local\":");
    jnum(&w, k->local);
    pay_w_s(&w, ",\"escrow_hold_bps\":");
    jnum(&w, k->escrow_hold_bps);
    pay_w_s(&w, ",\"credit_line\":");
    jnum(&w, k->credit_line);
    pay_w_c(&w, '}');
    return pay_w_finish(&w);
}

/* ---- C11 ---- */
bool cred_check(const cred_t *c)
{
    if (!c || c->n > CRED_MAX_SUBJECTS || !cred_config_ok(&c->cfg)) return false;
    if (c->n_seen > CRED_SEEN || c->seen_head >= CRED_SEEN) return false;
    uint64_t total = 0, ps = 0, ts = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        for (uint32_t k = i + 1; k < c->n; k++)
            if (c->subj[i].id == c->subj[k].id) return false;
        if (c->s[i][i]) return false;
        for (uint32_t j = 0; j < c->n; j++) {
            if (c->s[i][j] > CRED_LOCAL_MAX) return false;
            total += c->s[i][j];
        }
        ps += c->p[i];
        ts += c->t[i];
    }
    if (total != c->s_total) return false;
    uint32_t P = 0;
    for (uint32_t i = 0; i < c->n; i++) P += c->subj[i].pretrusted;
    for (uint32_t i = 0; c->computed && i < c->n; i++)
        if (c->p[i] && P && !c->subj[i].pretrusted) return false;
    if (c->computed && (ps != CRED_SCALE || ts != CRED_SCALE)) return false;
    return true;
}
