/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_bucket.c — update-bucket listings, ratings and their ISF-weighted
 * aggregate. See social_bucket.h B1-B5 and THE RULE: nothing here decides
 * install trust from a rating. Freestanding: integer only, no libc. */
#include "social_bucket.h"

#define PRIOR_W     (3 * ZT_ONE) /* B4: three raters' worth of prior */
#define PRIOR_STARS 3u

/* ---------------- B1 ---------------- */

int32_t sb_check_listing_body(const uint8_t *b, uint32_t len)
{
    uint32_t o = 0;
    if (!b || len < 4) return SP_ERR_RULE;
    if (b[o++] != 1) return SP_ERR_RULE;
    if (b[o++] >= SB_CAT_COUNT) return SP_ERR_RULE;
    if (b[o++] > SB_SRC_IPNS) return SP_ERR_RULE;
    uint32_t sl = b[o++];
    if (sl == 0 || sl > SB_SRC_MAX || sl > len - o) return SP_ERR_RULE;
    o += sl;
    if (o >= len) return SP_ERR_RULE;
    uint32_t tl = b[o++];
    if (tl == 0 || tl > SB_TITLE_MAX || tl > len - o) return SP_ERR_RULE;
    o += tl;
    if (len - o < 2) return SP_ERR_RULE;
    uint32_t dl = ((uint32_t) b[o] << 8) | b[o + 1];
    o += 2;
    if (dl > SB_DESC_MAX || dl > len - o) return SP_ERR_RULE;
    o += dl;
    if (len - o != SB_FPR_LEN) return SP_ERR_RULE; /* exactly the fingerprint left */
    return SP_OK;
}

int32_t sb_parse_listing(const sp_record_t *r, sb_listing_t *out)
{
    if (!r || !out || r->kind != SP_KIND_BUCKET_LISTING) return SP_ERR_ARG;
    if (sb_check_listing_body(r->body, r->body_len) != SP_OK) return SP_ERR_RULE;
    const uint8_t *b = r->body;
    uint32_t o = 1;
    out->category = b[o++];
    out->src_kind = b[o++];
    out->src_len = b[o++];
    out->src = b + o;
    o += out->src_len;
    out->title_len = b[o++];
    out->title = b + o;
    o += out->title_len;
    out->desc_len = ((uint32_t) b[o] << 8) | b[o + 1];
    o += 2;
    out->desc = b + o;
    o += out->desc_len;
    out->fingerprint = b + o;
    return SP_OK;
}

int32_t sb_make_listing(sp_record_t *r, sb_category_t cat, sb_src_kind_t src_kind,
                        const uint8_t *src, uint32_t src_len, const uint8_t *title,
                        uint32_t title_len, const uint8_t *desc, uint32_t desc_len,
                        const uint8_t fingerprint[SB_FPR_LEN])
{
    if (!r || !src || !title || (!desc && desc_len) || !fingerprint) return SP_ERR_ARG;
    if ((uint32_t) cat >= SB_CAT_COUNT || (uint32_t) src_kind > SB_SRC_IPNS || src_len == 0 ||
        src_len > SB_SRC_MAX || title_len == 0 || title_len > SB_TITLE_MAX ||
        desc_len > SB_DESC_MAX)
        return SP_ERR_RANGE;
    uint8_t *b = r->body;
    uint32_t o = 0;
    b[o++] = 1;
    b[o++] = (uint8_t) cat;
    b[o++] = (uint8_t) src_kind;
    b[o++] = (uint8_t) src_len;
    sp_copy(b + o, src, src_len);
    o += src_len;
    b[o++] = (uint8_t) title_len;
    sp_copy(b + o, title, title_len);
    o += title_len;
    b[o++] = (uint8_t) (desc_len >> 8);
    b[o++] = (uint8_t) desc_len;
    sp_copy(b + o, desc, desc_len);
    o += desc_len;
    sp_copy(b + o, fingerprint, SB_FPR_LEN);
    o += SB_FPR_LEN;
    r->body_len = (uint16_t) o;
    return SP_OK;
}

/* ---------------- B2 ---------------- */

int32_t sb_check_rating_body(const uint8_t *b, uint32_t len)
{
    if (!b || len < 1 || len > 1u + SB_COMMENT_MAX) return SP_ERR_RULE;
    return (b[0] >= 1 && b[0] <= 5) ? SP_OK : SP_ERR_RULE;
}

int32_t sb_make_rating(sp_record_t *r, const uint8_t listing_id[SP_ID_LEN], uint8_t stars,
                       const uint8_t *comment, uint32_t comment_len)
{
    if (!r || !listing_id || (!comment && comment_len)) return SP_ERR_ARG;
    if (stars < 1 || stars > 5 || comment_len > SB_COMMENT_MAX) return SP_ERR_RANGE;
    sp_set_parent(r, listing_id);
    r->body[0] = stars;
    sp_copy(r->body + 1, comment, comment_len);
    r->body_len = (uint16_t) (1u + comment_len);
    return SP_OK;
}

uint8_t sb_rating_stars(const sp_record_t *r)
{
    if (!r || r->kind != SP_KIND_RATING || !r->has_parent) return 0;
    if (sb_check_rating_body(r->body, r->body_len) != SP_OK) return 0;
    return r->body[0];
}

/* ---------------- THE RULE ---------------- */

bool sb_install_trusted(const sp_record_t *listing, const uint8_t (*trusted)[SB_FPR_LEN],
                        uint32_t n_trusted)
{
    sb_listing_t l;
    if (!trusted || sb_parse_listing(listing, &l) != SP_OK) return false;
    for (uint32_t i = 0; i < n_trusted; i++)
        if (sp_eq(l.fingerprint, trusted[i], SB_FPR_LEN)) return true;
    return false;
}

/* ---------------- B3 history vectors ---------------- */

static void add_term(sf_vec_t *v, uint8_t salt, const uint8_t *p, uint32_t n, int32_t unit)
{
    uint32_t h = (2166136261u ^ salt) * 16777619u;
    for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    uint32_t i = h & (SF_DIM - 1u);
    int64_t x = (int64_t) v->v[i] + ((h >> 31) ? -(int64_t) unit : (int64_t) unit);
    if (x > 64 * ZT_ONE) x = 64 * ZT_ONE;
    if (x < -64 * ZT_ONE) x = -64 * ZT_ONE;
    v->v[i] = (zt_fx) x;
}

static void history_of(const ss_store_t *s, const uint8_t author[SP_NODEID_LEN], sf_vec_t *out)
{
    for (uint32_t i = 0; i < SF_DIM; i++) out->v[i] = 0;
    for (uint32_t i = 0; i < ss_count(s); i++) {
        const sp_record_t *r = ss_at(s, i);
        uint8_t st = sb_rating_stars(r);
        if (!st || !sp_eq(r->author, author, SP_NODEID_LEN)) continue;
        add_term(out, 'P', r->parent, SP_ID_LEN, ZT_ONE / 2);
        if (st != 3) add_term(out, 'O', r->parent, SP_ID_LEN, ((int32_t) st - 3) * (ZT_ONE / 4));
    }
}

void sb_config_default(sb_config_t *c)
{
    c->N = 8;
    c->rep = 0;
    c->rep_ctx = 0;
}

static bool newer(const sp_record_t *a, const sp_record_t *b)
{
    if (a->lamport != b->lamport) return a->lamport > b->lamport;
    return sp_cmp(a->id, b->id, SP_ID_LEN) < 0;
}

/* f(u) / ln N in Q16. */
static zt_fx norm_f(zt_fx f, zt_fx lnN)
{
    if (lnN <= 0 || f <= 0) return 0;
    if (f >= lnN) return ZT_ONE;
    return (zt_fx) zt_udiv64((uint64_t) (uint32_t) f << 16, (uint32_t) lnN, 0);
}

void sb_aggregate(const ss_store_t *s, const uint8_t listing_id[SP_ID_LEN], const sb_config_t *cfg,
                  sb_rater_t *rt, uint32_t cap, sb_aggregate_t *out)
{
    if (!out) return;
    out->count = out->truncated = 0;
    out->effective = out->mean = out->confidence = out->unweighted = 0;
    out->score = (zt_fx) (PRIOR_STARS * ZT_ONE);
    if (!s || !listing_id || !cfg || !rt || cfg->N < 2) return;

    /* B2: one rater, one vote, the latest wins. */
    uint32_t n = 0;
    for (uint32_t i = 0; i < ss_count(s); i++) {
        const sp_record_t *r = ss_at(s, i);
        if (!sb_rating_stars(r) || !sp_eq(r->parent, listing_id, SP_ID_LEN)) continue;
        uint32_t j = 0;
        while (j < n && !sp_eq(rt[j].rating->author, r->author, SP_NODEID_LEN)) j++;
        if (j < n) {
            if (newer(r, rt[j].rating)) rt[j].rating = r;
        } else if (n < cap) {
            rt[n++].rating = r;
        } else {
            out->truncated++;
        }
    }
    if (n == 0) return;

    /* B3 */
    zt_fx lnN = zt_surplus_f(ZT_ONE, cfg->N);
    for (uint32_t i = 0; i < n; i++) {
        history_of(s, rt[i].rating->author, &rt[i].history);
        zt_fx q = cfg->rep ? cfg->rep(cfg->rep_ctx, rt[i].rating->author) : 0;
        if (q < 0) q = 0;
        if (q > ZT_ONE) q = ZT_ONE;
        rt[i].rep = (zt_fx) (ZT_ONE / 4 + ((3 * (int64_t) q) >> 2));
    }
    uint64_t sum_w = 0, sum_ws = 0, sum_stars = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t sim = 0;
        for (uint32_t j = 0; j < n; j++) {
            if (j == i) {
                sim += ZT_ONE;
                continue;
            }
            zt_fx u = zt_surplus_u(rt[i].history.v, rt[j].history.v, SF_DIM);
            sim += (uint32_t) (ZT_ONE - norm_f(zt_surplus_f(u, cfg->N), lnN));
        }
        rt[i].ind = (zt_fx) zt_udiv64((uint64_t) ZT_ONE << 16, sim, 0);
        rt[i].weight = (zt_fx) (((int64_t) rt[i].ind * rt[i].rep) >> 16);
        uint32_t st = rt[i].rating->body[0];
        sum_w += (uint32_t) rt[i].weight;
        sum_ws += (uint64_t) (uint32_t) rt[i].weight * st;
        sum_stars += st;
    }

    /* B4 */
    out->count = n;
    out->effective = (zt_fx) sum_w;
    out->unweighted = (zt_fx) zt_udiv64(sum_stars << 16, n, 0);
    if (sum_w) out->mean = (zt_fx) zt_udiv64(sum_ws << 16, sum_w, 0);
    out->confidence = (zt_fx) zt_udiv64(sum_w << 16, sum_w + PRIOR_W, 0);
    out->score =
        (zt_fx) zt_udiv64((sum_ws + (uint64_t) PRIOR_STARS * PRIOR_W) << 16, sum_w + PRIOR_W, 0);
}

/* ---------------- B5 ---------------- */

static void listing_vec(const sp_record_t *r, const sb_listing_t *l, sf_vec_t *v)
{
    sf_vec_t d;
    sf_embed_bytes(l->title, l->title_len, 0, v);
    sf_embed_bytes(l->desc, l->desc_len, 0, &d);
    for (uint32_t i = 0; i < SF_DIM; i++) v->v[i] += d.v[i];
    add_term(v, 'C', &l->category, 1, ZT_ONE / 2);
    add_term(v, 'A', r->author, SP_NODEID_LEN, ZT_ONE / 4);
}

static bool is_default(const sb_listing_t *l)
{
    static const char def[] = SB_DEFAULT_CID;
    uint32_t n = (uint32_t) sizeof def - 1u;
    return l->src_kind == SB_SRC_CID && l->src_len == n && sp_eq(l->src, (const uint8_t *) def, n);
}

static void emit(sb_entry_t *e, const sb_order_work_t *w, int32_t adjusted)
{
    e->store_index = w->store_index;
    e->listing = w->rec;
    e->agg = w->agg;
    e->surplus = w->min_surplus;
    e->adjusted = adjusted;
}

uint32_t sb_order(const ss_store_t *s, const sb_config_t *cfg, sb_order_work_t *work,
                  uint32_t work_cap, sb_rater_t *raters, uint32_t raters_cap, sb_entry_t *out,
                  uint32_t max)
{
    if (!s || !cfg || !work || !out || max == 0 || cfg->N < 2) return 0;
    zt_fx lnN = zt_surplus_f(ZT_ONE, cfg->N);
    uint32_t n = 0;
    int32_t def = -1;
    for (uint32_t i = 0; i < ss_count(s) && n < work_cap; i++) {
        const sp_record_t *r = ss_at(s, i);
        sb_listing_t l;
        if (r->kind != SP_KIND_BUCKET_LISTING || sb_parse_listing(r, &l) != SP_OK) continue;
        sb_order_work_t *w = &work[n];
        w->rec = r;
        w->store_index = i;
        w->taken = false;
        w->min_surplus = ZT_ONE;
        listing_vec(r, &l, &w->vec);
        sb_aggregate(s, r->id, cfg, raters, raters_cap, &w->agg);
        /* The user's own default bucket: the earliest listing of it wins. */
        if (def < 0 && is_default(&l)) def = (int32_t) n;
        n++;
    }

    uint32_t count = 0;
    const sb_order_work_t *pick = 0;
    if (def >= 0) {
        work[def].taken = true;
        emit(&out[count++], &work[def], work[def].agg.score);
        pick = &work[def];
    } else {
        sb_order_work_t b;
        b.rec = 0;
        b.store_index = SB_BUILTIN;
        b.min_surplus = ZT_ONE;
        sb_aggregate(0, 0, cfg, 0, 0, &b.agg); /* no ratings: the 3-star prior */
        emit(&out[count++], &b, b.agg.score);
    }
    while (count < max) {
        if (pick)
            for (uint32_t i = 0; i < n; i++) {
                if (work[i].taken) continue;
                zt_fx f = norm_f(
                    zt_surplus_f(zt_surplus_u(work[i].vec.v, pick->vec.v, SF_DIM), cfg->N), lnN);
                if (f < work[i].min_surplus) work[i].min_surplus = f;
            }
        int32_t best = -1, best_adj = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (work[i].taken) continue;
            int32_t adj =
                (int32_t) (((int64_t) work[i].agg.score * (ZT_ONE + work[i].min_surplus)) >> 17);
            if (best < 0 || adj > best_adj ||
                (adj == best_adj &&
                 (work[i].agg.score > work[best].agg.score ||
                  (work[i].agg.score == work[best].agg.score &&
                   sp_cmp(work[i].rec->id, work[best].rec->id, SP_ID_LEN) < 0)))) {
                best = (int32_t) i;
                best_adj = adj;
            }
        }
        if (best < 0) break;
        work[best].taken = true;
        emit(&out[count++], &work[best], best_adj);
        pick = &work[best];
    }
    return count;
}
