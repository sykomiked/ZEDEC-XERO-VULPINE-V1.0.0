/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_bond.c — the chiglet's private model of its person. See chg_bond.h. */
#include "chg_bond.h"

#define BOND_MAGIC 0x42435a58u /* "XZCB" little-endian: ZXCB on the wire */

static void bzero_(void *p, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static void bcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static bool same32(const uint8_t *a, const uint8_t *b)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < 32; i++) x |= (uint8_t) (a[i] ^ b[i]);
    return x == 0;
}

/* Shift-subtract division: no __udivdi3 on 32-bit freestanding targets. */
static uint64_t udiv(uint64_t n, uint64_t d)
{
    if (!d) return 0;
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    return q;
}

void chgb_init(chgb_t *b, const uint8_t owner[32])
{
    if (!b) return;
    bzero_(b, sizeof(*b));
    if (owner) bcpy(b->owner, owner, 32);
    b->learning = true;
    b->eta = 64;
    b->cost = 96;
    b->decay_permille = 8;
    b->min_share = 600;
}

void chgb_set_learning(chgb_t *b, bool on)
{
    if (b) b->learning = on;
}

chgb_status_t chgb_set_share(chgb_t *b, const uint8_t who[32], bool on)
{
    if (!b || !who) return CHGB_ERR_ARG;
    if (!same32(b->owner, who)) return CHGB_DENIED;
    b->share_opt_in = on;
    return CHGB_OK;
}

/* ----- vocabulary ----- */
static int32_t act_index(const chgb_t *b, uint16_t a)
{
    for (uint32_t i = 0; i < b->n_act; i++)
        if (b->act[i] == a) return (int32_t) i;
    return -1;
}

static int32_t app_index(const chgb_t *b, uint16_t a)
{
    for (uint32_t i = 0; i < b->n_app; i++)
        if (b->app[i] == a) return (int32_t) i;
    return -1;
}

static uint32_t act_total(const chgb_t *b, uint32_t k)
{
    uint32_t t = 0;
    for (uint32_t s = 0; s < CHGB_SLOTS; s++) t += b->w_time[s][k];
    for (uint32_t s = 0; s < CHGB_APPS; s++) t += b->w_app[s][k];
    for (uint32_t s = 0; s < CHGB_ACTIONS; s++) t += b->w_prev[s][k] + b->w_prev[k][s];
    return t;
}

static void clear_action_slot(chgb_t *b, uint32_t k)
{
    for (uint32_t s = 0; s < CHGB_SLOTS; s++) b->w_time[s][k] = 0;
    for (uint32_t s = 0; s < CHGB_APPS; s++) b->w_app[s][k] = 0;
    for (uint32_t s = 0; s < CHGB_ACTIONS; s++) b->w_prev[s][k] = b->w_prev[k][s] = 0;
}

static int32_t act_intern(chgb_t *b, uint16_t a)
{
    int32_t k = act_index(b, a);
    if (k >= 0) return k;
    if (b->n_act < CHGB_ACTIONS) {
        b->act[b->n_act] = a;
        return (int32_t) b->n_act++;
    }
    uint32_t worst = 0, wt = 0xffffffffu;
    for (uint32_t i = 0; i < CHGB_ACTIONS; i++) {
        uint32_t t = act_total(b, i);
        if (t < wt) wt = t, worst = i;
    }
    clear_action_slot(b, worst);
    b->act[worst] = a;
    return (int32_t) worst;
}

static uint32_t row_total(const uint16_t *r)
{
    uint32_t t = 0;
    for (uint32_t i = 0; i < CHGB_ACTIONS; i++) t += r[i];
    return t;
}

static int32_t app_intern(chgb_t *b, uint16_t a)
{
    int32_t k = app_index(b, a);
    if (k >= 0) return k;
    if (b->n_app < CHGB_APPS) {
        b->app[b->n_app] = a;
        return (int32_t) b->n_app++;
    }
    uint32_t worst = 0, wt = 0xffffffffu;
    for (uint32_t i = 0; i < CHGB_APPS; i++) {
        uint32_t t = row_total(b->w_app[i]);
        if (t < wt) wt = t, worst = i;
    }
    bzero_(b->w_app[worst], sizeof(b->w_app[worst]));
    b->app[worst] = a;
    return (int32_t) worst;
}

/* ----- B2: the open-system row update ----- */
static void row_decay(uint16_t *r, uint16_t delta)
{
    if (!delta) return;
    for (uint32_t i = 0; i < CHGB_ACTIONS; i++) {
        uint32_t w = r[i];
        if (!w) continue;
        uint32_t d = (w * delta) / 1000u;
        if (!d) d = 1; /* small habits fade too */
        r[i] = (uint16_t) (w > d ? w - d : 0);
    }
}

static void row_add(uint16_t *r, uint32_t k, int32_t v)
{
    int32_t w = (int32_t) r[k] + v;
    if (w < 0) w = 0;
    if (w > (int32_t) CHGB_WMAX) w = (int32_t) CHGB_WMAX;
    r[k] = (uint16_t) w;
}

/* The (up to three) rows a context selects; NULL where unknown. */
static void rows_for(const chgb_t *b, const chgb_ctx_t *c, const uint16_t *rows[CHGB_EXPERTS])
{
    rows[CHGB_E_TIME] = c->slot < CHGB_SLOTS ? b->w_time[c->slot] : 0;
    int32_t ai = c->app == CHGB_NONE ? -1 : app_index(b, c->app);
    rows[CHGB_E_APP] = ai >= 0 ? b->w_app[ai] : 0;
    int32_t pi = c->prev == CHGB_NONE ? -1 : act_index(b, c->prev);
    rows[CHGB_E_PREV] = pi >= 0 ? b->w_prev[pi] : 0;
}

chgb_status_t chgb_observe(chgb_t *b, const chgb_ctx_t *c, uint16_t action)
{
    if (!b || !c || action == CHGB_NONE || c->slot >= CHGB_SLOTS) return CHGB_ERR_ARG;
    if (!b->learning) return CHGB_PAUSED;
    /* intern app and prev first: interning the action may evict a slot */
    if (c->app != CHGB_NONE) app_intern(b, c->app);
    int32_t k = act_intern(b, action);
    int32_t pi = c->prev == CHGB_NONE ? -1 : act_index(b, c->prev);
    int32_t ai = c->app == CHGB_NONE ? -1 : app_index(b, c->app);
    uint16_t *rows[CHGB_EXPERTS];
    rows[CHGB_E_TIME] = b->w_time[c->slot];
    rows[CHGB_E_APP] = ai >= 0 ? b->w_app[ai] : 0;
    rows[CHGB_E_PREV] = pi >= 0 ? b->w_prev[pi] : 0;
    for (uint32_t e = 0; e < CHGB_EXPERTS; e++) {
        if (!rows[e]) continue;
        row_decay(rows[e], b->decay_permille);
        row_add(rows[e], (uint32_t) k, b->eta);
    }
    b->observed++;
    return CHGB_OK;
}

chgb_status_t chgb_feedback(chgb_t *b, const chgb_ctx_t *c, uint16_t action, bool accepted)
{
    if (!b || !c || c->slot >= CHGB_SLOTS) return CHGB_ERR_ARG;
    if (!b->learning) return CHGB_PAUSED;
    int32_t k = act_index(b, action);
    if (k < 0) return CHGB_ERR_ARG;
    const uint16_t *ro[CHGB_EXPERTS];
    rows_for(b, c, ro);
    for (uint32_t e = 0; e < CHGB_EXPERTS; e++) {
        if (!ro[e]) continue;
        uint16_t *r = (uint16_t *) ro[e];
        row_add(r, (uint32_t) k, accepted ? (int32_t) b->eta : -(int32_t) b->cost);
    }
    if (accepted)
        b->accepted++;
    else
        b->rejected++;
    return CHGB_OK;
}

void chgb_decay_all(chgb_t *b)
{
    if (!b) return;
    for (uint32_t s = 0; s < CHGB_SLOTS; s++) row_decay(b->w_time[s], b->decay_permille);
    for (uint32_t s = 0; s < CHGB_APPS; s++) row_decay(b->w_app[s], b->decay_permille);
    for (uint32_t s = 0; s < CHGB_ACTIONS; s++) row_decay(b->w_prev[s], b->decay_permille);
}

/* ----- B3 ----- */
uint32_t chgb_interaction(const uint16_t *a, const uint16_t *b, uint32_t n)
{
    if (!a || !b) return 0;
    uint64_t dot = 0, na = 0, nb = 0;
    for (uint32_t i = 0; i < n; i++) {
        dot += (uint64_t) a[i] * b[i];
        na += (uint64_t) a[i] * a[i];
        nb += (uint64_t) b[i] * b[i];
    }
    if (!na || !nb) return 0;
    /* weights <= 1023 and n <= 32: dot^2, na*nb < 2^50, so x1000 < 2^60 */
    uint64_t c = udiv(dot * dot * 1000u, na * nb); /* cos^2, permille */
    if (c > 1000u) c = 1000u;
    return 1000u - (uint32_t) c;
}

chgb_status_t chgb_suggest(const chgb_t *b, const chgb_ctx_t *c, chgb_suggestion_t *out)
{
    if (!b || !c || !out) return CHGB_ERR_ARG;
    bzero_(out, sizeof(*out));
    const uint16_t *rows[CHGB_EXPERTS];
    rows_for(b, c, rows);
    uint32_t sum[CHGB_EXPERTS];
    uint32_t live[CHGB_EXPERTS], nl = 0;
    for (uint32_t e = 0; e < CHGB_EXPERTS; e++) {
        sum[e] = rows[e] ? row_total(rows[e]) : 0;
        if (sum[e]) live[nl++] = e;
    }
    out->experts = (uint8_t) nl;
    if (!nl) {
        out->state = CHGB_NOTHING;
        return CHGB_OK;
    }
    for (uint32_t i = 0; i < nl; i++)
        for (uint32_t j = 0; j < nl; j++)
            out->u[live[i]][live[j]] =
                i == j ? 0 : (uint16_t) chgb_interaction(rows[live[i]], rows[live[j]], CHGB_ACTIONS);

    /* merge near-parallel experts into one voice (duplication cannot add
     * confidence: the chiglet.h lesson) */
    uint32_t grp[CHGB_EXPERTS], size[CHGB_EXPERTS];
    for (uint32_t i = 0; i < CHGB_EXPERTS; i++) size[i] = 0;
    for (uint32_t i = 0; i < nl; i++) {
        grp[i] = i;
        for (uint32_t j = 0; j < i; j++)
            if (grp[j] == j && out->u[live[i]][live[j]] < CHGB_U_MERGE) {
                grp[i] = j;
                break;
            }
        size[grp[i]]++;
    }
    uint32_t reps[CHGB_EXPERTS], nr = 0;
    for (uint32_t i = 0; i < nl; i++)
        if (grp[i] == i) reps[nr++] = i;
    out->distinct = (uint8_t) nr;
    uint64_t csum = 0;
    for (uint32_t i = 0; i < nr; i++)
        for (uint32_t j = 0; j < nr; j++)
            csum += i == j ? 1000u : 1000u - out->u[live[reps[i]]][live[reps[j]]];
    out->R_permille = (uint32_t) udiv((uint64_t) nr * nr * 1000000u, csum);

    uint32_t wg[CHGB_EXPERTS];
    for (uint32_t i = 0; i < CHGB_EXPERTS; i++) wg[i] = 0;
    for (uint32_t i = 0; i < nr; i++) {
        uint32_t w = 1000;
        for (uint32_t j = 0; j < nr; j++)
            if (j != i) w += out->u[live[reps[i]]][live[reps[j]]];
        wg[reps[i]] = w;
    }
    uint64_t score[CHGB_ACTIONS], total = 0;
    for (uint32_t a = 0; a < CHGB_ACTIONS; a++) score[a] = 0;
    for (uint32_t i = 0; i < nl; i++) {
        uint32_t e = live[i], g = grp[i];
        for (uint32_t a = 0; a < b->n_act; a++) {
            uint64_t s = udiv((uint64_t) wg[g] * rows[e][a] * 1024u, (uint64_t) sum[e] * size[g]);
            score[a] += s;
            total += s;
        }
    }
    uint32_t top = 0, sec = CHGB_ACTIONS;
    for (uint32_t a = 1; a < b->n_act; a++)
        if (score[a] > score[top]) top = a;
    for (uint32_t a = 0; a < b->n_act; a++)
        if (a != top && (sec == CHGB_ACTIONS || score[a] > score[sec])) sec = a;
    out->share = total ? (uint16_t) udiv(score[top] * 1000u, total) : 0;
    out->second = (total && sec < CHGB_ACTIONS) ? (uint16_t) udiv(score[sec] * 1000u, total) : 0;
    out->action = b->act[top];
    uint32_t seen = 0;
    for (uint32_t e = 0; e < CHGB_EXPERTS; e++) {
        out->support[e] = rows[e] ? rows[e][top] : 0;
        if (out->support[e] > seen) seen = out->support[e];
    }
    out->state = (out->share >= b->min_share && seen > b->eta) ? CHGB_SUGGEST : CHGB_UNSURE;
    return CHGB_OK;
}

/* ----- P2 ----- */
void chgb_forget_action(chgb_t *b, uint16_t action)
{
    if (!b) return;
    int32_t k = act_index(b, action);
    if (k < 0) return;
    clear_action_slot(b, (uint32_t) k);
    /* keep the vocabulary dense: move the last action into the hole */
    uint32_t last = b->n_act - 1u;
    if ((uint32_t) k != last) {
        b->act[k] = b->act[last];
        for (uint32_t s = 0; s < CHGB_SLOTS; s++) b->w_time[s][k] = b->w_time[s][last];
        for (uint32_t s = 0; s < CHGB_APPS; s++) b->w_app[s][k] = b->w_app[s][last];
        for (uint32_t s = 0; s < CHGB_ACTIONS; s++) b->w_prev[s][k] = b->w_prev[s][last];
        for (uint32_t s = 0; s < CHGB_ACTIONS; s++) b->w_prev[k][s] = b->w_prev[last][s];
        clear_action_slot(b, last);
    }
    b->act[last] = 0;
    b->n_act--;
}

void chgb_forget_app(chgb_t *b, uint16_t app)
{
    if (!b) return;
    int32_t k = app_index(b, app);
    if (k < 0) return;
    uint32_t last = b->n_app - 1u;
    bcpy(b->w_app[k], b->w_app[last], sizeof(b->w_app[k]));
    b->app[k] = b->app[last];
    bzero_(b->w_app[last], sizeof(b->w_app[last]));
    b->app[last] = 0;
    b->n_app--;
}

void chgb_forget_all(chgb_t *b)
{
    if (!b) return;
    uint8_t owner[32];
    bool learning = b->learning;
    bcpy(owner, b->owner, 32);
    chgb_init(b, owner);
    b->learning = learning; /* forgetting is not opting in or out of anything */
}

uint32_t chgb_app_affinity(const chgb_t *b, uint16_t app)
{
    if (!b) return 0;
    int32_t k = app_index(b, app);
    return k < 0 ? 0 : row_total(b->w_app[k]);
}

/* ----- P3 ----- */
#define TABLE_WORDS (CHGB_SLOTS * CHGB_ACTIONS + CHGB_APPS * CHGB_ACTIONS + CHGB_ACTIONS * CHGB_ACTIONS)
#define EXPORT_SIZE                                                                                \
    (4u + 32u + 2u * 4u + 1u + 2u + 2u * CHGB_ACTIONS + 2u + 2u * CHGB_APPS + 2u * TABLE_WORDS + 8u)

uint32_t chgb_export_size(void)
{
    return EXPORT_SIZE;
}

static uint64_t fnv(const uint8_t *p, uint32_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static void put16(uint8_t *o, uint32_t *at, uint16_t v)
{
    o[(*at)++] = (uint8_t) v;
    o[(*at)++] = (uint8_t) (v >> 8);
}

static uint16_t get16(const uint8_t *in, uint32_t *at)
{
    uint16_t v = (uint16_t) (in[*at] | (in[*at + 1] << 8));
    *at += 2;
    return v;
}

static const uint16_t *table_word(const chgb_t *b, uint32_t i)
{
    if (i < CHGB_SLOTS * CHGB_ACTIONS) return &b->w_time[0][0] + i;
    i -= CHGB_SLOTS * CHGB_ACTIONS;
    if (i < CHGB_APPS * CHGB_ACTIONS) return &b->w_app[0][0] + i;
    return &b->w_prev[0][0] + (i - CHGB_APPS * CHGB_ACTIONS);
}

int32_t chgb_export(const chgb_t *b, const uint8_t who[32], uint8_t *out, uint32_t cap)
{
    if (!b || !who || !out) return CHGB_ERR_ARG;
    if (!same32(b->owner, who) || !b->share_opt_in) return CHGB_DENIED;
    if (cap < EXPORT_SIZE) return CHGB_ERR_SPACE;
    uint32_t at = 0;
    out[at++] = 'Z', out[at++] = 'X', out[at++] = 'C', out[at++] = 'B';
    bcpy(out + at, b->owner, 32);
    at += 32;
    put16(out, &at, b->eta);
    put16(out, &at, b->cost);
    put16(out, &at, b->decay_permille);
    put16(out, &at, b->min_share);
    out[at++] = (uint8_t) (b->learning ? 1 : 0);
    put16(out, &at, b->n_act);
    for (uint32_t i = 0; i < CHGB_ACTIONS; i++) put16(out, &at, b->act[i]);
    put16(out, &at, b->n_app);
    for (uint32_t i = 0; i < CHGB_APPS; i++) put16(out, &at, b->app[i]);
    for (uint32_t i = 0; i < TABLE_WORDS; i++) put16(out, &at, *table_word(b, i));
    uint64_t h = fnv(out, at);
    for (uint32_t i = 0; i < 8; i++) out[at++] = (uint8_t) (h >> (8 * i));
    return (int32_t) at;
}

chgb_status_t chgb_import(chgb_t *b, const uint8_t owner[32], const uint8_t *in, uint32_t len)
{
    if (!b || !owner || !in) return CHGB_ERR_ARG;
    if (len != EXPORT_SIZE) return CHGB_ERR_PARSE;
    if (in[0] != 'Z' || in[1] != 'X' || in[2] != 'C' || in[3] != 'B') return CHGB_ERR_PARSE;
    uint64_t h = 0;
    for (uint32_t i = 0; i < 8; i++) h |= (uint64_t) in[len - 8 + i] << (8 * i);
    if (h != fnv(in, len - 8)) return CHGB_ERR_PARSE;
    if (!same32(in + 4, owner)) return CHGB_DENIED;
    uint32_t at = 36;
    uint16_t eta = get16(in, &at), cost = get16(in, &at), dec = get16(in, &at),
             ms = get16(in, &at);
    uint8_t learning = in[at++];
    uint16_t n_act = get16(in, &at);
    uint32_t act_at = at;
    at += 2 * CHGB_ACTIONS;
    uint16_t n_app = get16(in, &at);
    uint32_t app_at = at;
    at += 2 * CHGB_APPS;
    if (n_act > CHGB_ACTIONS || n_app > CHGB_APPS || learning > 1 || dec > 1000 || ms > 1000 ||
        eta > CHGB_WMAX || cost > CHGB_WMAX)
        return CHGB_ERR_PARSE;
    /* check the weights before touching b */
    for (uint32_t i = 0, t = at; i < TABLE_WORDS; i++)
        if (get16(in, &t) > CHGB_WMAX) return CHGB_ERR_PARSE;
    chgb_init(b, owner);
    b->eta = eta, b->cost = cost, b->decay_permille = dec, b->min_share = ms;
    b->learning = learning == 1;
    b->n_act = n_act, b->n_app = n_app;
    for (uint32_t i = 0; i < CHGB_ACTIONS; i++) b->act[i] = get16(in, &act_at);
    for (uint32_t i = 0; i < CHGB_APPS; i++) b->app[i] = get16(in, &app_at);
    for (uint32_t i = 0; i < TABLE_WORDS; i++) *(uint16_t *) table_word(b, i) = get16(in, &at);
    /* sharing stays off on the new device until the owner opts in there */
    return CHGB_OK;
}
