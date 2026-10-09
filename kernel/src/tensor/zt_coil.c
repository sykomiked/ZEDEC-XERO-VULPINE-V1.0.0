/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt_coil.c — the nested golden coil, its core, its field, and coils of
 * coils. See zt.h T7, T8, T10, T11. */
#include "zt.h"

uint64_t zt__fib(uint32_t n);

#define INVPHI_Q16  40503
#define INVPHI2_Q16 25033 /* 1/phi^2 */

static uint32_t mulmod(uint32_t a, uint32_t b, uint32_t m)
{
    uint64_t r;
    (void) zt_udiv64((uint64_t) a * b, m, &r);
    return (uint32_t) r;
}

/* v * k / 2^16 rounded toward zero, for 0 <= k < 2^16 and any int64 v, so
 * the result is odd in v (f(-v) = -f(v)) and needs no signed shift. */
static int64_t mul_q16_trunc(int64_t v, uint32_t k)
{
    uint64_t m = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    uint64_t r = (m >> 16) * k + (((m & 0xFFFFu) * k) >> 16); /* < 2^63 */
    return v < 0 ? -(int64_t) r : (int64_t) r;
}

static uint32_t scale_down(uint32_t j, uint32_t from, uint32_t to)
{
    return (uint32_t) zt_udiv64((uint64_t) j * to, from, 0);
}

bool zt_coil_init(zt_coil_t *c, uint32_t base, zt_wind_t wind)
{
    if (base < 5 || base > 36) return false;
    c->base = base;
    c->total = 0;
    c->core = (uint32_t) zt__fib(base - 1);
    for (uint32_t s = 0; s < ZT_COIL_SHELLS; s++) {
        uint32_t n = base + s;
        uint32_t size = (uint32_t) zt__fib(n), prev = (uint32_t) zt__fib(n - 1);
        c->size[s] = size;
        c->offset[s] = c->total;
        c->total += size;
        if (wind == ZT_WIND_ABHA && (size & 1u)) {
            c->stride[s] = 2u;                 /* the doubling step */
            c->unstride[s] = (size + 1u) / 2u; /* 2 * (size+1)/2 = 1 mod size */
        } else {
            /* Cassini: F(n-1)^2 = (-1)^n mod F(n). */
            c->stride[s] = prev;
            c->unstride[s] = (n & 1u) ? size - prev : prev;
        }
    }
    return true;
}

zt_place_t zt_coil_place(const zt_coil_t *c, uint32_t index)
{
    zt_place_t p = {0, 0};
    uint32_t s = ZT_COIL_SHELLS - 1;
    while (s > 0 && index < c->offset[s]) s--;
    p.shell = s;
    p.slot = mulmod(index - c->offset[s], c->stride[s], c->size[s]);
    return p;
}

uint32_t zt_coil_index(const zt_coil_t *c, zt_place_t p)
{
    return c->offset[p.shell] + mulmod(p.slot, c->unstride[p.shell], c->size[p.shell]);
}

zt_place_t zt_coil_parent(const zt_coil_t *c, zt_place_t p)
{
    zt_place_t q = p;
    if (p.shell == ZT_SHELL_CORE) return q;
    if (p.shell == 0) {
        q.shell = ZT_SHELL_CORE;
        q.slot = scale_down(p.slot, c->size[0], c->core);
        return q;
    }
    q.shell = p.shell - 1;
    q.slot = scale_down(p.slot, c->size[p.shell], c->size[p.shell - 1]);
    return q;
}

/* Depth below the core: core 0, shell s is s + 1. */
static uint32_t depth(zt_place_t p)
{
    return p.shell == ZT_SHELL_CORE ? 0 : p.shell + 1;
}

uint32_t zt_coil_dist(const zt_coil_t *c, zt_place_t a, zt_place_t b)
{
    uint32_t d = 0;
    while (depth(a) > depth(b)) a = zt_coil_parent(c, a), d++;
    while (depth(b) > depth(a)) b = zt_coil_parent(c, b), d++;
    while (a.shell != b.shell || a.slot != b.slot) {
        if (a.shell == ZT_SHELL_CORE) return d + 2; /* core slots are joined by the axis */
        a = zt_coil_parent(c, a);
        b = zt_coil_parent(c, b);
        d += 2;
    }
    return d;
}

zt_fx zt_coil_scale(uint32_t shell)
{
    return (zt_fx) zt_phi_pow(-(int32_t) (shell > 24 ? 24 : shell));
}

static uint64_t at(const zt_coil_t *c, zt_place_t p);

void zt_coil_field(const zt_coil_t *c, const zt_fx *value, int64_t *field)
{
    for (uint32_t i = 0; i < c->total; i++) field[i] = 0;
    for (uint32_t i = 0; i < c->core; i++) field[c->total + i] = 0;
    for (uint32_t i = 0; i < c->total; i++) {
        zt_place_t p = zt_coil_place(c, i);
        field[c->offset[p.shell] + p.slot] = value[i];
    }
    /* Outer shells first, so each place has its whole subtree before it emits. */
    for (uint32_t s = ZT_COIL_SHELLS; s-- > 0;) {
        for (uint32_t j = 0; j < c->size[s]; j++) {
            zt_place_t p = {s, j}, q = zt_coil_parent(c, p);
            int64_t emit = mul_q16_trunc(field[c->offset[s] + j], INVPHI_Q16);
            field[at(c, q)] += emit;
        }
    }
}

uint32_t zt_coil_fit(uint64_t max_elems)
{
    uint32_t base = 5;
    while (base < 36 && zt__fib(base + 12) - zt__fib(base + 2) <= max_elems) base++;
    return base;
}

uint32_t zt_fractal_levels(const zt_coil_t *c, uint64_t n)
{
    uint32_t L = 1;
    uint64_t cap = c->total;
    while (cap < n && L < ZT_FRACTAL_MAX) {
        if (cap > UINT64_MAX / c->total) return ZT_FRACTAL_MAX;
        cap *= c->total;
        L++;
    }
    return L;
}

bool zt_fractal_place(const zt_coil_t *c, uint32_t levels, uint64_t index, zt_place_t *out)
{
    if (levels == 0 || levels > ZT_FRACTAL_MAX) return false;
    for (uint32_t l = levels; l-- > 0;) {
        uint64_t r;
        index = zt_udiv64(index, c->total, &r);
        out[l] = zt_coil_place(c, (uint32_t) r);
    }
    return index == 0;
}

uint64_t zt_fractal_index(const zt_coil_t *c, uint32_t levels, const zt_place_t *in)
{
    uint64_t index = 0;
    for (uint32_t l = 0; l < levels; l++) index = index * c->total + zt_coil_index(c, in[l]);
    return index;
}

static uint64_t at(const zt_coil_t *c, zt_place_t p)
{
    return p.shell == ZT_SHELL_CORE ? (uint64_t) c->total + p.slot
                                    : (uint64_t) c->offset[p.shell] + p.slot;
}

void zt_coil_ac(const zt_coil_t *c, int64_t *field, uint32_t phase)
{
    if ((phase & 1u) == 0) {
        for (uint32_t s = ZT_COIL_SHELLS; s-- > 0;)
            for (uint32_t j = 0; j < c->size[s]; j++) {
                zt_place_t p = {s, j};
                uint64_t a = at(c, p), b = at(c, zt_coil_parent(c, p));
                int64_t t = mul_q16_trunc(field[a], INVPHI2_Q16);
                field[a] -= t;
                field[b] += t;
            }
    } else {
        for (uint32_t s = 0; s < ZT_COIL_SHELLS; s++)
            for (uint32_t j = 0; j < c->size[s]; j++) {
                zt_place_t p = {s, j};
                uint64_t a = at(c, p), b = at(c, zt_coil_parent(c, p));
                int64_t t = mul_q16_trunc(field[b], INVPHI2_Q16);
                field[b] -= t;
                field[a] += t;
            }
    }
}

bool zt_device_plan(const zt_device_t *d, uint64_t n_elems, zt_wind_t wind, zt_plan_t *out)
{
    if (!d || d->banks == 0 || d->lanes == 0) return false;
    if (!zt_coil_init(&out->coil, zt_coil_fit(d->fast_elems), wind)) return false;
    out->levels = zt_fractal_levels(&out->coil, n_elems);
    return true;
}

uint32_t zt_coil_bank(const zt_coil_t *c, zt_place_t p, uint32_t banks)
{
    uint64_t r;
    (void) zt_udiv64(at(c, p), banks ? banks : 1u, &r);
    return (uint32_t) r;
}

static uint8_t read_truth(int64_t p, int64_t n, int64_t t)
{
    /* magnitudes as unsigned, so INT64_MIN is 2^63 rather than undefined */
    uint64_t up = p < 0 ? (uint64_t) 0 - (uint64_t) p : (uint64_t) p;
    uint64_t un = n < 0 ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    bool sp = t <= 0 || up >= (uint64_t) t, sn = t <= 0 || un >= (uint64_t) t;
    if (sp && sn) return ZT_COIL_GLUT;
    if (sp) return ZT_COIL_TRUE;
    if (sn) return ZT_COIL_FALSE;
    return up || un ? ZT_COIL_NEUTRAL : ZT_COIL_UNKNOWN;
}

/* p - n, saturated to int64. */
static int64_t sat_sub64(int64_t p, int64_t n)
{
    if (n < 0 && p > INT64_MAX + n) return INT64_MAX;
    if (n > 0 && p < INT64_MIN + n) return INT64_MIN;
    return p - n;
}

void zt_coil_interfere(const zt_coil_t *c, const int64_t *pos, const int64_t *neg,
                       int64_t threshold, uint8_t *truth, int64_t *interference)
{
    uint64_t all = (uint64_t) c->total + c->core;
    for (uint64_t i = 0; i < all; i++) {
        truth[i] = read_truth(pos[i], neg[i], threshold);
        interference[i] = sat_sub64(pos[i], neg[i]);
    }
    /* A glut whose whole path to the core is glut cannot be settled inside
     * the coil: it is a paradox. Core first, so each place sees its parent's
     * final reading. */
    for (uint32_t i = 0; i < c->core; i++)
        if (truth[c->total + i] == ZT_COIL_GLUT) truth[c->total + i] = ZT_COIL_PARADOX;
    for (uint32_t s = 0; s < ZT_COIL_SHELLS; s++)
        for (uint32_t j = 0; j < c->size[s]; j++) {
            zt_place_t p = {s, j};
            uint64_t a = at(c, p);
            if (truth[a] == ZT_COIL_GLUT && truth[at(c, zt_coil_parent(c, p))] == ZT_COIL_PARADOX)
                truth[a] = ZT_COIL_PARADOX;
        }
}
