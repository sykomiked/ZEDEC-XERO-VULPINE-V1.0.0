/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_feed.c — ISF feed ranker. See social_feed.h F1-F5.
 * Freestanding: integer only, no libc, no allocation. */
#include "social_feed.h"
#include "../reputation/reputation.h"

#define UNIT_WORD   (ZT_ONE / 4)
#define UNIT_TOPIC  (3 * ZT_ONE / 4)
#define UNIT_AUTHOR (ZT_ONE / 2)
#define V_CLAMP     (64 * ZT_ONE)

/* ---------------- F1 ---------------- */

static uint32_t fnv_start(uint8_t salt)
{
    return (2166136261u ^ salt) * 16777619u;
}

static uint32_t fnv_byte(uint32_t h, uint8_t b)
{
    return (h ^ b) * 16777619u;
}

static void add_hash(sf_vec_t *v, uint32_t h, zt_fx unit)
{
    uint32_t i = h & (SF_DIM - 1u);
    int64_t x = (int64_t) v->v[i] + ((h >> 31) ? -(int64_t) unit : (int64_t) unit);
    if (x > V_CLAMP) x = V_CLAMP;
    if (x < -V_CLAMP) x = -V_CLAMP;
    v->v[i] = (zt_fx) x;
}

static bool word_byte(uint8_t b)
{
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b >= 0x80;
}

void sf_embed_bytes(const uint8_t *body, uint32_t len, const uint8_t author[SP_NODEID_LEN],
                    sf_vec_t *out)
{
    for (uint32_t i = 0; i < SF_DIM; i++) out->v[i] = 0;
    if (author) {
        uint32_t h = fnv_start('A');
        for (uint32_t i = 0; i < SP_NODEID_LEN; i++) h = fnv_byte(h, author[i]);
        add_hash(out, h, UNIT_AUTHOR);
    }
    if (!body) return;
    uint32_t i = 0;
    while (i < len) {
        bool topic = false;
        if (body[i] == '#' && i + 1 < len && word_byte(body[i + 1])) {
            topic = true;
            i++;
        } else if (!word_byte(body[i])) {
            i++;
            continue;
        }
        uint32_t h = fnv_start(topic ? 'T' : 'W');
        while (i < len && word_byte(body[i])) {
            uint8_t b = body[i++];
            if (b >= 'A' && b <= 'Z') b = (uint8_t) (b + ('a' - 'A'));
            h = fnv_byte(h, b);
        }
        add_hash(out, h, topic ? UNIT_TOPIC : UNIT_WORD);
    }
}

static bool rankable(const sp_record_t *r)
{
    return r && (r->kind == SP_KIND_POST || r->kind == SP_KIND_REPLY);
}

void sf_embed(const sp_record_t *r, sf_vec_t *out)
{
    if (!rankable(r)) {
        for (uint32_t i = 0; i < SF_DIM; i++) out->v[i] = 0;
        return;
    }
    sf_embed_bytes(r->body, r->body_len, r->author, out);
}

/* ---------------- seen set ---------------- */

void sf_seen_init(sf_seen_t *s)
{
    s->head = 0;
    s->n = 0;
}

void sf_seen_add(sf_seen_t *s, const sf_vec_t *v)
{
    for (uint32_t i = 0; i < SF_DIM; i++) s->v[s->head].v[i] = v->v[i];
    s->head = (s->head + 1u) % SF_SEEN_MAX;
    if (s->n < SF_SEEN_MAX) s->n++;
}

/* ---------------- F2 ---------------- */

static zt_fx pair_f(const sf_vec_t *a, const sf_vec_t *b, uint32_t N)
{
    return zt_surplus_f(zt_surplus_u(a->v, b->v, SF_DIM), N);
}

static zt_fx f_max(uint32_t N)
{
    return zt_surplus_f(ZT_ONE, N);
}

zt_fx sf_surplus_over(const sf_vec_t *c, const sf_seen_t *s, uint32_t N)
{
    if (N < 2) return 0;
    zt_fx m = f_max(N);
    for (uint32_t j = 0; s && j < s->n; j++) {
        zt_fx f = pair_f(c, &s->v[j], N);
        if (f < m) m = f;
    }
    return m;
}

/* ---------------- F4 ---------------- */

void sf_config_default(sf_config_t *c, uint64_t now)
{
    c->N = 8;
    c->now = now;
    c->horizon = 1024;
    zt_fx ln8 = f_max(8);
    c->w_fresh = ln8 / 4;
    c->w_rep = ln8 / 8;
    c->rep = 0;
    c->rep_ctx = 0;
}

static zt_fx clamp_fx(zt_fx v, zt_fx hi)
{
    if (v < 0) return 0;
    return v > hi ? hi : v;
}

static zt_fx freshness(const sf_config_t *c, zt_fx w, uint64_t lamport)
{
    uint32_t hz = c->horizon ? c->horizon : 1u;
    uint64_t age = c->now > lamport ? c->now - lamport : 0;
    if (age >= hz) return 0;
    uint64_t num = (uint64_t) (uint32_t) w * (uint64_t) (hz - (uint32_t) age);
    return (zt_fx) zt_udiv64(num, hz, 0);
}

/* ---------------- F3, F5 ---------------- */

static bool better(int32_t sa, const sp_record_t *a, int32_t sb, const sp_record_t *b)
{
    if (sa != sb) return sa > sb;
    if (a->lamport != b->lamport) return a->lamport > b->lamport;
    return sp_cmp(a->id, b->id, SP_ID_LEN) < 0;
}

uint32_t sf_rank(const sf_config_t *cfg, const sf_seen_t *seen, const sp_record_t *const *cand,
                 uint32_t n, sf_work_t *work, sf_pick_t *out, uint32_t k)
{
    if (!cfg || !cand || !work || !out || cfg->N < 2) return 0;
    zt_fx lnN = f_max(cfg->N);
    zt_fx wf = clamp_fx(cfg->w_fresh, lnN / 4);
    zt_fx wr = clamp_fx(cfg->w_rep, lnN / 8);

    for (uint32_t i = 0; i < n; i++) {
        sf_work_t *w = &work[i];
        w->taken = false;
        w->eligible = rankable(cand[i]);
        for (uint32_t j = 0; w->eligible && j < i; j++)
            if (work[j].eligible && sp_eq(cand[j]->id, cand[i]->id, SP_ID_LEN)) w->eligible = false;
        if (!w->eligible) {
            w->min_surplus = 0;
            continue;
        }
        sf_embed(cand[i], &w->vec);
        w->min_surplus = sf_surplus_over(&w->vec, seen, cfg->N);
    }

    uint32_t count = 0;
    while (count < k) {
        int32_t best_score = 0;
        int32_t best = -1;
        zt_fx bf = 0, br = 0;
        for (uint32_t i = 0; i < n; i++) {
            const sf_work_t *w = &work[i];
            if (!w->eligible || w->taken) continue;
            zt_fx fr = freshness(cfg, wf, cand[i]->lamport);
            zt_fx rp = 0;
            if (cfg->rep && wr) {
                zt_fx q = clamp_fx(cfg->rep(cfg->rep_ctx, cand[i]->author), ZT_ONE);
                rp = (zt_fx) (((int64_t) wr * q) >> 16);
            }
            int32_t s = w->min_surplus + fr + rp;
            if (best < 0 || better(s, cand[i], best_score, cand[(uint32_t) best])) {
                best = (int32_t) i;
                best_score = s;
                bf = fr;
                br = rp;
            }
        }
        if (best < 0) break;
        sf_work_t *pw = &work[(uint32_t) best];
        pw->taken = true;
        out[count].index = (uint32_t) best;
        out[count].score = best_score;
        out[count].surplus = pw->min_surplus;
        out[count].fresh = bf;
        out[count].rep = br;
        count++;
        /* F3: the pick joins the working seen set. */
        for (uint32_t i = 0; i < n; i++) {
            sf_work_t *w = &work[i];
            if (!w->eligible || w->taken) continue;
            zt_fx f = pair_f(&w->vec, &pw->vec, cfg->N);
            if (f < w->min_surplus) w->min_surplus = f;
        }
    }
    return count;
}

void sf_commit(sf_seen_t *seen, const sf_work_t *work, const sf_pick_t *picks, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) sf_seen_add(seen, &work[picks[i].index].vec);
}

zt_fx sf_diversity(const sf_work_t *work, const sf_pick_t *picks, uint32_t count)
{
    if (count < 2) return 0;
    uint64_t sum = 0, pairs = 0;
    for (uint32_t i = 0; i < count; i++)
        for (uint32_t j = i + 1; j < count; j++) {
            sum += (uint32_t) zt_surplus_u(work[picks[i].index].vec.v, work[picks[j].index].vec.v,
                                           SF_DIM);
            pairs++;
        }
    return (zt_fx) zt_udiv64(sum, pairs, 0);
}

/* ---------------- reputation adapter ---------------- */

uint32_t sf_subject_of(const uint8_t node_id[SP_NODEID_LEN])
{
    return ((uint32_t) node_id[0] << 24) | ((uint32_t) node_id[1] << 16) |
           ((uint32_t) node_id[2] << 8) | node_id[3];
}

zt_fx sf_rep_badges(void *ctx, const uint8_t author[SP_NODEID_LEN])
{
    const rep_state_t *s = (const rep_state_t *) ctx;
    if (!s || !author) return 0;
    uint32_t sc = badge_score(s, sf_subject_of(author));
    if (sc > 4096u) sc = 4096u;
    return (zt_fx) ((sc * (uint32_t) ZT_ONE) / (sc + 16u));
}
