/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_gpu.c — zt_gpu.h: the forward pass as one op list, and the C backend.
 * Hosted only (libc). The steps and their order are zt_model.c forward()'s;
 * the arithmetic is zt_metal_math.h's. */
#include <stdlib.h>
#include <string.h>

#include "zt_gpu.h"
#include "zt_kern.h"
#include "zt_metal_c.h"

const char *const zg_kernel_name[ZG_NK] = {
    "zk_rms_inv", "zk_rms_apply", "zk_act",    "zk_mm",        "zk_bias",
    "zk_rope",    "zk_kv",        "zk_att_q",  "zk_att_score", "zk_att_max",
    "zk_att_exp", "zk_att_sum",   "zk_att_wv", "zk_resid",     "zk_silu"};

static _Thread_local uint32_t last_ops;

uint32_t zg_last_ops(void)
{
    return last_ops;
}

#define AL(x) (((x) + 255u) & ~(uint64_t) 255u)

static uint32_t row_bytes(const zt_mat_t *w)
{
    uint32_t nb = w->cols / 32;
    return w->qtype == ZT_GGML_Q4_K   ? (nb / 8) * ZT_Q4K_BYTES
           : w->qtype == ZT_GGML_Q6_K ? (nb / 8) * ZT_Q6K_BYTES
                                      : nb * 34;
}

static bool mat_ok(const zt_mat_t *w)
{
    if (!w->raw || w->cols % 32) return false;
    if (w->qtype == ZT_GGML_Q8_0) return true;
    return (w->qtype == ZT_GGML_Q4_K || w->qtype == ZT_GGML_Q6_K) && w->cols % 256 == 0;
}

bool zg_supported(const zt_model_t *m)
{
    const zt_model_cfg_t *c = &m->cfg;
    if (!m->layers || !mat_ok(&m->out) || c->head_dim < m->rope.n_rot || m->rope.n_rot % 2 ||
        c->n_head % c->n_head_kv || c->n_embd % 32 || c->n_ff % 32 || m->q_dim % 32)
        return false;
    for (uint32_t l = 0; l < c->n_layer; l++) {
        const zt_layer_t *L = &m->layers[l];
        const zt_mat_t *w[7] = {&L->wq, &L->wk, &L->wv, &L->wo, &L->wg, &L->wu, &L->wd};
        for (int i = 0; i < 7; i++)
            if (!mat_ok(w[i])) return false;
        if (!!L->q_norm.g != !!L->k_norm.g) return false;
    }
    return true;
}

/* ---- the work buffer ---- */

typedef struct {
    uint64_t x, xn, q, k, v, att, gt, up, a16, ash, asum, inv, qs, sq, sc, mx, sum, rope, logits,
        ctr, total;
} wl_t;

static void work_layout(const zt_model_t *m, uint32_t B, uint32_t P, bool logits, wl_t *w)
{
    const zt_model_cfg_t *c = &m->cfg;
    uint64_t ne = c->n_embd, nf = c->n_ff, qd = m->q_dim, kvd = m->kv_dim, nh = c->n_head;
    uint64_t amax = ne > qd ? ne : qd, at = 0;
    if (nf > amax) amax = nf;
#define PUT(f, bytes) (w->f = at, at += AL(bytes))
    PUT(ctr, 4); /* first: backends bind the saturation counter at work offset 0 */
    PUT(x, B * ne * 4);
    PUT(xn, B * ne * 4);
    PUT(q, B * qd * 4);
    PUT(k, B * kvd * 4);
    PUT(v, B * kvd * 4);
    PUT(att, B * qd * 4);
    PUT(gt, B * nf * 4);
    PUT(up, B * nf * 4);
    PUT(a16, B * amax * 2);
    PUT(ash, B * amax / 32);
    PUT(asum, B * amax / 32 * 4);
    PUT(inv, B * nh * 8);
    PUT(qs, B * qd * 4);
    PUT(sq, B * nh * 4);
    PUT(sc, B * nh * P * 4);
    PUT(mx, B * nh * 4);
    PUT(sum, B * nh * 8);
    PUT(rope, B * (m->rope.n_rot / 2) * 8);
    PUT(logits, logits ? B * (uint64_t) c->n_vocab * 4 : 0);
#undef PUT
    w->total = at;
}

/* ---- the op list ---- */

typedef struct {
    zg_op_t *ops; /* null: count only */
    uint32_t n;
    uint8_t *cdst; /* null: constants not written */
    uint64_t cur;  /* constants cursor */
    const wl_t *w;
} pl_t;

static zg_buf_t BW(uint64_t off)
{
    zg_buf_t b = {ZG_B_WORK, off, 0, 0, false};
    return b;
}

static zg_buf_t BC(uint64_t off)
{
    zg_buf_t b = {ZG_B_CONST, off, 0, 0, false};
    return b;
}

static zg_buf_t BH(const void *p, uint64_t len, bool inplace)
{
    zg_buf_t b = {ZG_B_HOST, 0, p, len, inplace};
    return b;
}

static zg_op_t *op(pl_t *pl, uint32_t k, uint32_t n)
{
    static zg_op_t sink;
    zg_op_t *o = pl->ops ? &pl->ops[pl->n] : &sink;
    pl->n++;
    memset(o, 0, sizeof *o);
    o->k = k;
    o->u[0] = n;
    return o;
}

/* n int32 values into the constants, at a 256-byte boundary */
static uint64_t cput(pl_t *pl, const int32_t *src, uint32_t n)
{
    uint64_t at = pl->cur;
    if (pl->cdst && src) memcpy(pl->cdst + at, src, (size_t) n * 4);
    pl->cur += AL((uint64_t) n * 4);
    return at;
}

/* rows of n values (stride xs) of x, normalised with gain gn, into y (stride ys) */
static void p_rms(pl_t *pl, const zt_model_t *m, uint64_t x, uint32_t xs, uint32_t rows, uint32_t n,
                  const zt_gain_t *gn, uint64_t y, uint32_t ys)
{
    uint64_t g = cput(pl, gn->g, n);
    zg_op_t *o = op(pl, ZG_K_RMS_INV, rows);
    o->u[1] = n, o->u[2] = xs, o->u[3] = (uint32_t) m->eps_q48,
    o->u[4] = (uint32_t) (m->eps_q48 >> 32);
    o->b[0] = BW(x), o->b[1] = BW(pl->w->inv);
    o = op(pl, ZG_K_RMS_APPLY, rows * n);
    o->u[1] = n, o->u[2] = xs, o->u[3] = gn->frac, o->u[4] = ys;
    o->b[0] = BW(x), o->b[1] = BW(pl->w->inv), o->b[2] = BC(g), o->b[3] = BW(y);
}

static void p_act(pl_t *pl, uint64_t x, uint32_t xs, uint32_t B, uint32_t n)
{
    zg_op_t *o = op(pl, ZG_K_ACT, B * (n / 32));
    o->u[1] = n, o->u[2] = xs;
    o->b[0] = BW(x), o->b[1] = BW(pl->w->a16), o->b[2] = BW(pl->w->ash), o->b[3] = BW(pl->w->asum);
}

/* y = w . act (B tokens), then bias and the saturation count */
static void p_mm(pl_t *pl, const zt_mat_t *w, uint32_t B, uint64_t y, const zt_fx *bias)
{
    uint32_t rb = row_bytes(w);
    zg_op_t *o = op(pl, ZG_K_MM, w->rows * B);
    o->u[1] = w->rows, o->u[2] = w->cols / 32, o->u[3] = w->cols, o->u[4] = w->qtype;
    o->u[5] = rb, o->u[6] = B;
    o->b[0] = BH(w->raw, (uint64_t) rb * w->rows, false);
    o->b[1] = BW(pl->w->a16), o->b[2] = BW(pl->w->ash), o->b[3] = BW(pl->w->asum), o->b[4] = BW(y);
    uint64_t bo = bias ? cput(pl, bias, w->rows) : 0;
    o = op(pl, ZG_K_BIAS, w->rows * B);
    o->u[1] = w->rows, o->u[2] = bias != 0;
    o->b[0] = BW(y);
    if (bias) o->b[1] = BC(bo);
}

static uint32_t plan(pl_t *pl, const zt_model_t *m, const zt_model_state_t *s, uint32_t B,
                     uint32_t first, bool logits)
{
    const zt_model_cfg_t *c = &m->cfg;
    const wl_t *w = pl->w;
    uint32_t ne = c->n_embd, nf = c->n_ff, qd = m->q_dim, kvd = m->kv_dim, hd = c->head_dim;
    uint32_t nh = c->n_head, nkv = c->n_head_kv, half = m->rope.n_rot / 2;
    uint32_t p0 = s->pos, P = p0 + B;
    uint64_t kvbytes = (uint64_t) c->n_layer * s->n_ctx * kvd * 4;
    pl->cur = 0;
    pl->n = 0;
    uint64_t e2 = pl->cur;
    pl->cur += AL(257 * 4);
    if (pl->cdst) memcpy(pl->cdst + e2, zt_exp2_table(), 257 * 4);
    zg_op_t *o;
    for (uint32_t l = 0; l < c->n_layer; l++) {
        const zt_layer_t *L = &m->layers[l];
        uint32_t base = l * s->n_ctx;
        p_rms(pl, m, w->x, ne, B, ne, &L->attn_norm, w->xn, ne);
        p_act(pl, w->xn, ne, B, ne);
        p_mm(pl, &L->wq, B, w->q, L->bq);
        p_mm(pl, &L->wk, B, w->k, L->bk);
        p_mm(pl, &L->wv, B, w->v, L->bv);
        if (L->q_norm.g) {
            p_rms(pl, m, w->q, hd, B * nh, hd, &L->q_norm, w->q, hd);
            p_rms(pl, m, w->k, hd, B * nkv, hd, &L->k_norm, w->k, hd);
        }
        for (int t = 0; t < 2; t++) {
            o = op(pl, ZG_K_ROPE, B * (t ? nkv : nh) * half);
            o->u[1] = t ? nkv : nh, o->u[2] = hd, o->u[3] = half, o->u[4] = m->rope.neox;
            o->b[0] = BW(t ? w->k : w->q), o->b[1] = BW(w->rope);
        }
        o = op(pl, ZG_K_KV, B * kvd);
        o->u[1] = kvd, o->u[2] = base + p0;
        o->b[0] = BW(w->k), o->b[1] = BW(w->v);
        o->b[2] = BH(s->k16, kvbytes, true), o->b[3] = BH(s->v16, kvbytes, true);
        uint32_t att_u[9] = {0, nh, hd, nh / nkv, kvd, P, p0, base, m->inv_sqrt_q30};
        o = op(pl, ZG_K_ATT_Q, B * nh);
        memcpy(o->u + 1, att_u + 1, 8 * 4);
        o->b[0] = BW(w->q), o->b[1] = BW(w->qs), o->b[2] = BW(w->sq);
        o = op(pl, ZG_K_ATT_SCORE, B * nh * P);
        memcpy(o->u + 1, att_u + 1, 8 * 4);
        o->b[0] = BW(w->qs), o->b[1] = BW(w->sq), o->b[2] = BW(w->sc);
        o->b[3] = BH(s->k16, kvbytes, true);
        o = op(pl, ZG_K_ATT_MAX, B * nh);
        memcpy(o->u + 1, att_u + 1, 8 * 4);
        o->b[0] = BW(w->sc), o->b[1] = BW(w->mx);
        o = op(pl, ZG_K_ATT_EXP, B * nh * P);
        memcpy(o->u + 1, att_u + 1, 8 * 4);
        o->b[0] = BW(w->sc), o->b[1] = BW(w->mx), o->b[2] = BC(e2);
        o = op(pl, ZG_K_ATT_SUM, B * nh);
        memcpy(o->u + 1, att_u + 1, 8 * 4);
        o->b[0] = BW(w->sc), o->b[1] = BW(w->sum);
        o = op(pl, ZG_K_ATT_WV, B * nh * hd);
        memcpy(o->u + 1, att_u + 1, 8 * 4);
        o->b[0] = BW(w->sc), o->b[1] = BW(w->sum), o->b[2] = BW(w->att);
        o->b[3] = BH(s->v16, kvbytes, true);
        p_act(pl, w->att, qd, B, qd);
        p_mm(pl, &L->wo, B, w->xn, 0);
        o = op(pl, ZG_K_RESID, B * ne);
        o->b[0] = BW(w->x), o->b[1] = BW(w->xn);
        p_rms(pl, m, w->x, ne, B, ne, &L->ffn_norm, w->xn, ne);
        p_act(pl, w->xn, ne, B, ne);
        p_mm(pl, &L->wg, B, w->gt, 0);
        p_mm(pl, &L->wu, B, w->up, 0);
        o = op(pl, ZG_K_SILU, B * nf);
        o->b[0] = BW(w->gt), o->b[1] = BW(w->up), o->b[2] = BC(e2);
        p_act(pl, w->gt, nf, B, nf);
        p_mm(pl, &L->wd, B, w->xn, 0);
        o = op(pl, ZG_K_RESID, B * ne);
        o->b[0] = BW(w->x), o->b[1] = BW(w->xn);
    }
    if (logits) {
        uint32_t Bo = B - first;
        uint64_t xo = (uint64_t) first * ne * 4;
        p_rms(pl, m, w->x + xo, ne, Bo, ne, &m->out_norm, w->xn + xo, ne);
        p_act(pl, w->xn + xo, ne, Bo, ne);
        p_mm(pl, &m->out, Bo, w->logits, 0);
    }
    return pl->n;
}

int zg_forward(const zg_backend_t *be, const zt_model_t *m, zt_model_state_t *s, uint32_t B,
               zt_fx *logits, uint32_t first, uint32_t *n_sat)
{
    const zt_model_cfg_t *c = &m->cfg;
    if (!be || !B || first >= B || s->kv_type != ZT_KV_Q16 || !s->k16 || !zg_supported(m)) return 1;
    uint32_t ne = c->n_embd, half = m->rope.n_rot / 2, p0 = s->pos;
    wl_t w;
    work_layout(m, B, p0 + B, logits != 0, &w);
    pl_t pl = {0, 0, 0, 0, &w};
    uint32_t n = plan(&pl, m, s, B, first, logits != 0);
    uint64_t cbytes = pl.cur;
    uint8_t *work = be->work(be->ctx, w.total);
    uint8_t *cst = work ? be->cst(be->ctx, cbytes) : 0;
    zg_op_t *ops = cst ? malloc((size_t) n * sizeof *ops) : 0;
    if (!ops) return 1;
    pl.ops = ops;
    pl.cdst = cst;
    plan(&pl, m, s, B, first, logits != 0);
    memcpy(work + w.x, s->x, (size_t) B * ne * 4);
    int32_t *rope = (int32_t *) (work + w.rope);
    for (uint32_t b = 0; b < B; b++)
        for (uint32_t i = 0; i < half; i++)
            zt_rope_sincos((uint64_t) (p0 + b) * m->rope.freq[i], &rope[(b * half + i) * 2],
                           &rope[(b * half + i) * 2 + 1]);
    memset(work + w.ctr, 0, 4);
    int r = be->run(be->ctx, ops, n, work, cst);
    free(ops);
    if (r) return 1;
    last_ops = n;
    memcpy(s->x, work + w.x, (size_t) B * ne * 4);
    if (logits) {
        uint64_t xo = (uint64_t) first * ne;
        memcpy(s->xn + xo, work + w.xn + xo * 4, (size_t) (B - first) * ne * 4);
        memcpy(logits, work + w.logits, (size_t) (B - first) * c->n_vocab * 4);
    }
    uint32_t ns;
    memcpy(&ns, work + w.ctr, 4);
    *n_sat = ns;
    return 0;
}

/* ---- the C backend ---- */

struct zg_sim {
    uint8_t *work, *cst;
    uint64_t wcap, ccap;
};

zg_sim_t *zg_sim_new(void)
{
    return calloc(1, sizeof(zg_sim_t));
}

void zg_sim_free(zg_sim_t *g)
{
    if (!g) return;
    free(g->work);
    free(g->cst);
    free(g);
}

static void *sim_work(void *ctx, uint64_t bytes)
{
    zg_sim_t *g = ctx;
    if (bytes > g->wcap) {
        free(g->work);
        g->work = calloc(1, bytes);
        g->wcap = g->work ? bytes : 0;
    }
    return g->work;
}

static void *sim_cst(void *ctx, uint64_t bytes)
{
    zg_sim_t *g = ctx;
    if (bytes > g->ccap) {
        free(g->cst);
        g->cst = calloc(1, bytes);
        g->ccap = g->cst ? bytes : 0;
    }
    return g->cst;
}

typedef void (*zk_fn)(ZK_A);
static const zk_fn sim_fn[ZG_NK] = {zk_rms_inv, zk_rms_apply, zk_act,    zk_mm,        zk_bias,
                                    zk_rope,    zk_kv,        zk_att_q,  zk_att_score, zk_att_max,
                                    zk_att_exp, zk_att_sum,   zk_att_wv, zk_resid,     zk_silu};

static int sim_run(void *ctx, const zg_op_t *ops, uint32_t n, const void *work, const void *cst)
{
    (void) ctx;
    uint8_t *wk = (uint8_t *) work, *cs = (uint8_t *) cst;
    for (uint32_t i = 0; i < n; i++) {
        const zg_op_t *o = &ops[i];
        if (o->k >= ZG_NK) return 1;
        zb *b[6];
        for (int j = 0; j < 6; j++)
            b[j] = o->b[j].kind == ZG_B_WORK    ? wk + o->b[j].off
                   : o->b[j].kind == ZG_B_CONST ? cs + o->b[j].off
                   : o->b[j].kind == ZG_B_HOST  ? (zb *) o->b[j].host
                                                : 0;
        zg_p p;
        memcpy(p.u, o->u, sizeof p.u);
        for (uint32_t t = 0; t < o->u[0]; t++)
            sim_fn[o->k](t, p, b[0], b[1], b[2], b[3], b[4], b[5], (zm_ctr *) wk);
    }
    return 0;
}

int zg_sim_fwd(void *fctx, const void *m, void *s, uint32_t B, zt_fx *logits, uint32_t first,
               uint32_t *n_sat)
{
    zg_backend_t be = {fctx, sim_work, sim_cst, sim_run};
    return zg_forward(&be, m, s, B, logits, first, n_sat);
}
