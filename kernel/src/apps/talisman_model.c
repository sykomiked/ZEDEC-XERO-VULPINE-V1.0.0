/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* talisman_model.c — Talisman (see talisman_model.h). */
#include <string.h>

#include "talisman_model.h"

/* ---- the rules ------------------------------------------------------- */

uint64_t talisman_effective_cred(uint64_t cred, uint64_t giving)
{
    uint64_t add = cred / 3u;
    if (giving < add) add = giving;
    return cred > UINT64_MAX - add ? UINT64_MAX : cred + add;
}

void talisman_measure(uint64_t have, bool missing, uint64_t need, bool uptime, int64_t *reach_x100,
                      int64_t *margin)
{
    const uint64_t top = (uint64_t) TALISMAN_MARGIN_MAX;
    if (missing || (uptime && have > (uint64_t) TALISMAN_BPS_MAX)) {
        *reach_x100 = 0; /* TM2: never met */
        *margin = -1;
        return;
    }
    if (need == 0)
        *reach_x100 = TALISMAN_REACH_FULL;
    else if (need > (uint64_t) TALISMAN_VALUE_MAX)
        *reach_x100 = TALISMAN_REFUSED;
    else /* past its threshold an input is met: held at the threshold (TL1) */
        *reach_x100 = talisman_reach_x100((int64_t) (have < need ? have : need), (int64_t) need);
    if (have >= need) {
        uint64_t d = have - need;
        *margin = (int64_t) (d > top ? top : d);
    } else {
        uint64_t d = need - have;
        *margin = -(int64_t) (d > top ? top : d);
    }
}

/* ---- the notice ------------------------------------------------------ */

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_u64(char *out, uint32_t cap, uint32_t *at, uint64_t u)
{
    char t[24];
    int k = 23;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 0);
    return put(out, cap, at, t + k);
}

/* x100 as a percentage with two digits after the point: 5000 -> "50.00%" */
static bool put_x100(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char f[4] = {(char) ('0' + (int) (v % 100 / 10)), (char) ('0' + (int) (v % 10)), '%', 0};
    return put_u64(out, cap, at, (uint64_t) (v / 100)) && put(out, cap, at, ".") &&
           put(out, cap, at, f);
}

static const char *const INPUT_WORD[TALISMAN_NEEDS] = {"credibility", "bond", "uptime"};

/* What the view says: the tier, the next tier input by input, the perks,
 * and that tiers give nothing else (TM3). */
static void compose(talisman_t *t)
{
    char *s = t->notice;
    uint32_t cap = sizeof t->notice, at = 0;
    const talisman_next_t *x = &t->next;
    s[0] = 0;
    bool ok = put(s, cap, &at, "Tier ") && put(s, cap, &at, t->metal) && put(s, cap, &at, " / ") &&
              put(s, cap, &at, t->stone);
    if (ok && !x->have) {
        ok = put(s, cap, &at, ", the highest a card earns");
    } else if (ok) {
        ok = put(s, cap, &at, ". Next, ") && put(s, cap, &at, x->metal) &&
             put(s, cap, &at, " / ") && put(s, cap, &at, x->stone) && put(s, cap, &at, ":");
        for (uint32_t i = 0; ok && i < TALISMAN_NEEDS; i++) {
            const talisman_input_t *in = &t->in[i];
            uint64_t have = i == TALISMAN_CRED ? t->effective_cred : in->value;
            ok = put(s, cap, &at, i ? "; " : " ") && put(s, cap, &at, INPUT_WORD[i]);
            bool malformed = i == TALISMAN_UPTIME && in->value > (uint64_t) TALISMAN_BPS_MAX;
            if (ok && (in->missing || malformed)) { /* TM2 */
                ok =
                    put(s, cap, &at, " missing (") &&
                    put(s, cap, &at, in->missing ? in->source : "past 10000 bps: no measurement") &&
                    put(s, cap, &at, ")");
                continue;
            }
            ok = ok && put(s, cap, &at, " ") && put_u64(s, cap, &at, have) &&
                 put(s, cap, &at, " of ") && put_u64(s, cap, &at, x->need[i]) &&
                 put(s, cap, &at, i == TALISMAN_UPTIME ? " bps" : "");
            if (ok && x->reach_x100[i] >= 0)
                ok = put(s, cap, &at, " (") && put_x100(s, cap, &at, x->reach_x100[i]) &&
                     put(s, cap, &at, x->met[i] ? ", met)" : ")");
            else if (ok)
                ok = put(s, cap, &at, x->met[i] ? " (met)" : " (too large to measure)");
        }
        if (ok && x->earned)
            ok = put(s, cap, &at,
                     "; every threshold is met, so zxvd counts the next tier on its "
                     "next card");
    }
    if (ok) ok = put(s, cap, &at, ". Perks: ");
    for (uint32_t i = 0; ok && i < t->nperks; i++)
        ok = put(s, cap, &at, i ? ", " : "") && put(s, cap, &at, t->perks[i]);
    if (ok && !t->nperks) ok = put(s, cap, &at, "none");
    if (ok) ok = put(s, cap, &at, ". Tiers give no priority, discount or credit.");
    if (!ok) {
        /* every part above fits TALISMAN_NOTICE_MAX; a cut notice says so */
        at = 0;
        s[0] = 0;
        (void) put(s, cap, &at,
                   "the card is too long to show. Tiers give no priority, discount "
                   "or credit.");
    }
}

/* ---- the card -------------------------------------------------------- */

void talisman_init(talisman_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
}

const talisman_mark_t *talisman_mark(const talisman_t *t, uint32_t i)
{
    if (!t || i >= t->nhist) return NULL;
    return &t->hist[(t->hist_at + i) % TALISMAN_HISTORY];
}

/* A decimal string of 1..20 digits that fits uint64 (player.card's values). */
static bool digits_u64(const char *s, uint64_t *v)
{
    uint64_t x = 0;
    uint32_t n = 0;
    for (; s[n]; n++) {
        if (s[n] < '0' || s[n] > '9' || n >= 20u) return false;
        uint64_t d = (uint64_t) (s[n] - '0');
        if (x > (UINT64_MAX - d) / 10u) return false;
        x = x * 10u + d;
    }
    *v = x;
    return n > 0;
}

static int get_digits(const zxm_link_t *l, int32_t o, const char *k, uint64_t *v)
{
    char b[24];
    if (zxm_get_str(l, o, k, b, sizeof b) || !digits_u64(b, v)) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

static bool word_ok(const char *w)
{
    for (const char *p = w; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) return false;
    return w[0] != 0;
}

static const char *const INPUT_KEY[TALISMAN_INPUTS] = {"credibility", "bond", "uptime_bps",
                                                       "giving"};

/* inputs: {"<name>":{"value":"<digits>","source":"..."}}; giving may be left out */
static int read_inputs(const zxm_link_t *l, int32_t o, talisman_t *n)
{
    if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < TALISMAN_INPUTS; i++) {
        talisman_input_t *in = &n->in[i];
        int32_t e = zxm_get(l, o, INPUT_KEY[i]);
        if (e < 0 && i == TALISMAN_GIVING) {
            in->missing = true;
            memcpy(in->source, "not reported", sizeof "not reported");
            continue;
        }
        if (e < 0 || l->tok[e].type != W4_J_OBJ || get_digits(l, e, "value", &in->value) ||
            zxm_get_str(l, e, "source", in->source, sizeof in->source) || !in->source[0])
            return ZXM_E_PROTOCOL;
        in->missing = in->value == 0; /* TM2 */
    }
    return ZXM_OK;
}

/* next: {"tier","metal","stone","credibility","bond","uptime_bps","missing":[...]} */
static int read_next(const zxm_link_t *l, int32_t o, talisman_t *n)
{
    talisman_next_t *x = &n->next;
    uint64_t tier;
    if (l->tok[o].type == W4_J_NULL)
        return n->tier + 1u == TALISMAN_TIERS ? ZXM_OK : ZXM_E_PROTOCOL;
    if (l->tok[o].type != W4_J_OBJ || zxm_get_u64(l, o, "tier", &tier) ||
        tier != (uint64_t) n->tier + 1u || tier >= TALISMAN_TIERS ||
        zxm_get_str(l, o, "metal", x->metal, sizeof x->metal) ||
        zxm_get_str(l, o, "stone", x->stone, sizeof x->stone) || !word_ok(x->metal) ||
        !word_ok(x->stone))
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < TALISMAN_NEEDS; i++)
        if (get_digits(l, o, INPUT_KEY[i], &x->need[i])) return ZXM_E_PROTOCOL;
    int32_t m = zxm_get(l, o, "missing");
    if (m < 0 || l->tok[m].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    for (uint32_t k = 0; k < zxm_len(l, m); k++) {
        char w[16];
        int32_t e = zxm_at(l, m, k);
        uint32_t i = 0;
        if (e < 0 || l->tok[e].type != W4_J_STR ||
            w4_json_str(l->resp, &l->tok[e], w, sizeof w) < 0)
            return ZXM_E_PROTOCOL;
        while (i < TALISMAN_NEEDS && strcmp(w, INPUT_KEY[i])) i++;
        if (i == TALISMAN_NEEDS) return ZXM_E_PROTOCOL;
        x->zxvd_missing[i] = true;
    }
    x->have = true;
    x->tier = (uint32_t) tier;
    for (uint32_t i = 0; i < TALISMAN_NEEDS; i++) {
        uint64_t have = i == TALISMAN_CRED ? n->effective_cred : n->in[i].value;
        talisman_measure(have, n->in[i].missing, x->need[i], i == TALISMAN_UPTIME,
                         &x->reach_x100[i], &x->margin[i]);
        x->met[i] = x->margin[i] >= 0;
    }
    x->earned = talisman_earned(x->margin[TALISMAN_CRED], x->margin[TALISMAN_BOND],
                                x->margin[TALISMAN_UPTIME]);
    return ZXM_OK;
}

static int read_perks(const zxm_link_t *l, int32_t a, talisman_t *n)
{
    if (a < 0 || l->tok[a].type != W4_J_ARR || zxm_len(l, a) > TALISMAN_PERKS_MAX)
        return ZXM_E_PROTOCOL;
    for (uint32_t k = 0; k < zxm_len(l, a); k++) {
        int32_t e = zxm_at(l, a, k);
        if (e < 0 || l->tok[e].type != W4_J_STR ||
            w4_json_str(l->resp, &l->tok[e], n->perks[k], sizeof n->perks[k]) <= 0)
            return ZXM_E_PROTOCOL;
    }
    n->nperks = zxm_len(l, a);
    return ZXM_OK;
}

/* TM4: mark the tier when it changed (or on the first refresh) */
static void mark(talisman_t *n, uint32_t day)
{
    const talisman_mark_t *last = talisman_mark(n, n->nhist ? n->nhist - 1u : 0u);
    if (last && last->tier == n->tier) return;
    if (n->nhist == TALISMAN_HISTORY) { /* full: the oldest goes */
        n->hist_at = (n->hist_at + 1u) % TALISMAN_HISTORY;
        n->nhist--;
        n->dropped++;
    }
    talisman_mark_t *m = &n->hist[(n->hist_at + n->nhist) % TALISMAN_HISTORY];
    m->day = day;
    m->tier = n->tier;
    n->nhist++;
}

int talisman_refresh(talisman_t *t, uint32_t day)
{
    static talisman_t n;
    const talisman_mark_t *last = talisman_mark(t, t->nhist ? t->nhist - 1u : 0u);
    if (last && day < last->day) return ZXM_E_ARG; /* TM4: nothing asked */
    zxm_link_t *l = t->link;
    zxm_begin(l, "player.card");
    int rc = zxm_call(l);
    if (rc) return rc;
    /* a fresh card on the same history (TM5: kept unless all of it reads) */
    talisman_init(&n, l);
    memcpy(n.hist, t->hist, sizeof n.hist);
    n.hist_at = t->hist_at;
    n.nhist = t->nhist;
    n.dropped = t->dropped;
    uint64_t tier;
    if (zxm_get_u64(l, l->result, "tier", &tier) || tier >= TALISMAN_TIERS ||
        zxm_get_str(l, l->result, "metal", n.metal, sizeof n.metal) ||
        zxm_get_str(l, l->result, "stone", n.stone, sizeof n.stone) || !word_ok(n.metal) ||
        !word_ok(n.stone))
        return ZXM_E_PROTOCOL;
    n.tier = (uint32_t) tier;
    if (read_inputs(l, zxm_get(l, l->result, "inputs"), &n)) return ZXM_E_PROTOCOL;
    n.effective_cred =
        talisman_effective_cred(n.in[TALISMAN_CRED].value,
                                n.in[TALISMAN_GIVING].missing ? 0u : n.in[TALISMAN_GIVING].value);
    int32_t nx = zxm_get(l, l->result, "next");
    if (nx < 0 || read_next(l, nx, &n) || read_perks(l, zxm_get(l, l->result, "perks"), &n))
        return ZXM_E_PROTOCOL;
    n.have = true;
    compose(&n);
    mark(&n, day);
    *t = n;
    return ZXM_OK;
}
