/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_evh.c — the paraconsistent evidence head (see zt_evh.h). */
#include "zt_evh.h"

#define W_MAX ((int64_t) 1 << 31) /* |Q24 weight| <= 128 while fitting */

/* x / 2^s rounded to nearest, halves away from zero (no signed shifts) */
static int64_t rshift_round(int64_t x, uint32_t s)
{
    if (!s) return x;
    uint64_t a = x < 0 ? (uint64_t) 0 - (uint64_t) x : (uint64_t) x;
    a = (a + ((uint64_t) 1 << (s - 1))) >> s;
    return x < 0 ? -(int64_t) a : (int64_t) a;
}

/* x / 2^s rounded toward minus infinity */
static int64_t rshift_floor(int64_t x, uint32_t s)
{
    if (x >= 0) return (int64_t) ((uint64_t) x >> s);
    uint64_t a = (uint64_t) 0 - (uint64_t) x;
    return -(int64_t) ((a + ((uint64_t) 1 << s) - 1u) >> s);
}

static zt_fx sat32(int64_t v)
{
    return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (zt_fx) v;
}

zt_fx zt_sigmoid(zt_fx z)
{
    if (z < 0) {
        zt_fx m = z == INT32_MIN ? INT32_MAX : -z;
        return ZT_ONE - zt_sigmoid(m);
    }
    uint64_t d = (uint64_t) ZT_ONE + (uint32_t) zt_exp(-z);
    return (zt_fx) zt_udiv64(((uint64_t) 1 << 32) + (d >> 1), d, 0);
}

uint8_t zt_evh_base(zt_fx mu, zt_fx la, zt_fx *cert, zt_fx *contr)
{
    int64_t gc = (int64_t) mu - la, gt = (int64_t) mu + la - ZT_ONE;
    if (cert) *cert = sat32(gc);
    if (contr) *contr = sat32(gt);
    uint64_t ac = gc < 0 ? (uint64_t) -gc : (uint64_t) gc;
    uint64_t at = gt < 0 ? (uint64_t) -gt : (uint64_t) gt;
    if (ac > at) return gc > 0 ? ZT_EVH_TRUE : ZT_EVH_FALSE;
    return gt > 0 ? ZT_EVH_BOTH : ZT_EVH_NEITHER;
}

static zt_fx logit(const int16_t *w, uint32_t sh, zt_fx b, const zt_fx *h, uint32_t n)
{
    int64_t z = 0;
    for (uint32_t i = 0; i < n; i++) z += (int64_t) w[i] * h[i];
    return sat32(rshift_round(z, sh) + b);
}

int32_t zt_evh_apply(const zt_evh_t *e, const zt_fx *h, uint32_t n, zt_evh_out_t *o)
{
    if (!e || !h || !o || !e->w_mu || !e->w_la || n != e->n || !n || n > 65536u || e->sh_mu > 40u ||
        e->sh_la > 40u)
        return ZT_EVH_EARG;
    o->z_mu = logit(e->w_mu, e->sh_mu, e->b_mu, h, n);
    o->z_la = logit(e->w_la, e->sh_la, e->b_la, h, n);
    o->mu = zt_sigmoid(o->z_mu);
    o->la = zt_sigmoid(o->z_la);
    o->base = zt_evh_base(o->mu, o->la, &o->cert, &o->contr);
    return 0;
}

/* ---- fitting ---- */

uint64_t zt_evh_fit_work_bytes(uint32_t n)
{
    return (uint64_t) n * 8u + 8u;
}

static int64_t clampw(int64_t w)
{
    return w > W_MAX ? W_MAX : w < -W_MAX ? -W_MAX : w;
}

/* One head: SGD in Q24, then int16 weights with the largest shift that fits. */
static void fit_one(const zt_fx *H, uint32_t ns, uint32_t n, const uint8_t *y,
                    const zt_evh_fit_params_t *p, int64_t *w, int16_t *wq, uint32_t *sh, zt_fx *bq)
{
    int64_t b = 0;
    for (uint32_t j = 0; j < n; j++) w[j] = 0;
    for (uint32_t ep = 0; ep < p->epochs; ep++)
        for (uint32_t i = 0; i < ns; i++) {
            const zt_fx *h = H + (uint64_t) i * n;
            int64_t z = rshift_floor(b, 8);
            for (uint32_t j = 0; j < n; j++) z += rshift_floor(w[j] * h[j], 24);
            int64_t err = (y[i] ? (int64_t) ZT_ONE : 0) - zt_sigmoid(sat32(z));
            if (!err) continue;
            for (uint32_t j = 0; j < n; j++)
                w[j] = clampw(w[j] + rshift_floor(err * h[j], 8u + p->lr_shift));
            b = clampw(b + rshift_floor(err * 256, p->lr_shift));
        }
    uint64_t mx = 0;
    for (uint32_t j = 0; j < n; j++) {
        uint64_t a = w[j] < 0 ? (uint64_t) -w[j] : (uint64_t) w[j];
        if (a > mx) mx = a;
    }
    uint32_t s = 40;
    for (; s > 0; s--) {
        int64_t q = s <= 24 ? rshift_round((int64_t) mx, 24 - s) : (int64_t) mx << (s - 24);
        if (q <= 32767) break;
    }
    for (uint32_t j = 0; j < n; j++)
        wq[j] = (int16_t) (s <= 24 ? rshift_round(w[j], 24 - s) : w[j] * ((int64_t) 1 << (s - 24)));
    *sh = s;
    *bq = sat32(rshift_round(b, 8));
}

int32_t zt_evh_fit(const zt_fx *H, uint32_t ns, uint32_t n, const uint8_t *y_mu,
                   const uint8_t *y_la, const zt_evh_fit_params_t *p, int16_t *w_mu, int16_t *w_la,
                   zt_evh_t *out, void *work, uint64_t work_bytes)
{
    if (!H || !ns || !n || n > 65536u || !y_mu || !y_la || !p || !p->epochs || p->lr_shift > 40u ||
        !w_mu || !w_la || !out)
        return ZT_EVH_EARG;
    if (!work || work_bytes < zt_evh_fit_work_bytes(n)) return ZT_EVH_ESPACE;
    int64_t *w = (int64_t *) ((uint8_t *) work + ((8 - ((uintptr_t) work & 7)) & 7));
    out->n = n;
    out->w_mu = w_mu;
    out->w_la = w_la;
    fit_one(H, ns, n, y_mu, p, w, w_mu, &out->sh_mu, &out->b_mu);
    fit_one(H, ns, n, y_la, p, w, w_la, &out->sh_la, &out->b_la);
    return 0;
}

static uint8_t label_base(uint8_t ym, uint8_t yl)
{
    if (ym && yl) return ZT_EVH_BOTH;
    if (ym) return ZT_EVH_TRUE;
    if (yl) return ZT_EVH_FALSE;
    return ZT_EVH_NEITHER;
}

int32_t zt_evh_score(const zt_evh_t *e, const zt_fx *H, uint32_t ns, const uint8_t *y_mu,
                     const uint8_t *y_la, uint32_t conf[4][4])
{
    if (!e || !H || !y_mu || !y_la || !conf || ns > 0x7FFFFFFFu) return ZT_EVH_EARG;
    for (uint32_t a = 0; a < 4; a++)
        for (uint32_t b = 0; b < 4; b++) conf[a][b] = 0;
    int32_t ok = 0;
    for (uint32_t i = 0; i < ns; i++) {
        zt_evh_out_t o;
        int32_t r = zt_evh_apply(e, H + (uint64_t) i * e->n, e->n, &o);
        if (r) return r;
        uint8_t t = label_base(y_mu[i], y_la[i]);
        conf[t][o.base]++;
        ok += t == o.base;
    }
    return ok;
}

/* ---- per answer and per span ---- */

void zt_evh_acc_reset(zt_evh_acc_t *a)
{
    zt_evh_acc_t z = {0};
    if (a) *a = z;
}

void zt_evh_acc_add(zt_evh_acc_t *a, const zt_evh_out_t *o, uint32_t n_sat)
{
    if (!a || !o || a->n == UINT32_MAX) return;
    if (a->n && !(a->n & (a->n - 1u))) {
        a->pos0 = a->pos;
        a->neg0 = a->neg;
    }
    a->n++;
    a->sum_mu += (uint32_t) (o->mu < 0 ? 0 : o->mu);
    a->sum_la += (uint32_t) (o->la < 0 ? 0 : o->la);
    a->cnt[o->base & 3u]++;
    a->pos += o->mu >= ZT_ONE / 2;
    a->neg += o->la >= ZT_ONE / 2;
    if (n_sat > a->sat) a->sat = n_sat;
}

void zt_evh_summary(const zt_evh_acc_t *a, zt_evh_sum_t *s)
{
    if (!s) return;
    zt_evh_sum_t z = {0};
    *s = z;
    s->base = ZT_EVH_NEITHER;
    if (!a) return;
    s->n = a->n;
    for (uint32_t k = 0; k < 4; k++) s->cnt[k] = a->cnt[k];
    s->pos0 = a->pos0;
    s->neg0 = a->neg0;
    s->pos1 = a->pos;
    s->neg1 = a->neg;
    s->saturated = a->sat > 0;
    if (!a->n) return;
    s->mu = (zt_fx) zt_udiv64(a->sum_mu + a->n / 2u, a->n, 0);
    s->la = (zt_fx) zt_udiv64(a->sum_la + a->n / 2u, a->n, 0);
    s->base = zt_evh_base(s->mu, s->la, &s->cert, &s->contr);
}

void zt_evh_hook(void *hctx, int32_t id, const zt_fx *h, uint32_t n, uint32_t n_sat)
{
    zt_evh_hook_t *k = (zt_evh_hook_t *) hctx;
    (void) id;
    if (!k) return;
    zt_evh_out_t o;
    if (!k->head || !k->acc || zt_evh_apply(k->head, h, n, &o)) {
        k->skipped++;
        return;
    }
    zt_evh_acc_add(k->acc, &o, n_sat);
}
