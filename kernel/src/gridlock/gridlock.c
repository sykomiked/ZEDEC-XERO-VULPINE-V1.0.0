/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gridlock.c — gridlock resolution: removal heuristic, exact branch and
 * bound with proven upper bounds, and brute force. See gridlock.h.
 *
 * HONEST LIMITS: exponential worst case (the free-mode problem contains
 * subset-sum); the node budget bounds the work and the result then carries a
 * proven upper bound instead of an optimality claim. Integers only, no libc,
 * no allocation.
 */
#include "gridlock.h"

#define GL_FRESH       (-1)
#define GL_BOUND_ITERS 3u   /* bound refinement rounds per node */
#define GL_ROOT_ITERS  256u /* ... at the root (reported bound) */

/* Byte loops through volatile so the compiler cannot turn them into
 * memset/memcpy calls (freestanding: no libc). */
static void gl_zero(void *p, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    while (n--) *d++ = 0;
}

static void gl_copy(void *dst, const void *src, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    const volatile uint8_t *s = (const volatile uint8_t *) src;
    while (n--) *d++ = *s++;
}

gl_lp_t gl_lp_join(gl_lp_t a, gl_lp_t b)
{
    return (gl_lp_t) (((uint32_t) a | (uint32_t) b) & 3u);
}

static int gl_parts_ok(const gl_instance_t *in)
{
    return in->num_parts >= 1u && in->num_parts <= GL_MAX_PART;
}

gl_status_t gl_init(gl_instance_t *in, uint32_t num_parts)
{
    if (!in) return GL_ERR_NULL;
    if (num_parts < 1u || num_parts > GL_MAX_PART) return GL_ERR_RANGE;
    gl_zero(in, (uint32_t) sizeof(*in));
    in->num_parts = num_parts;
    return GL_OK;
}

gl_status_t gl_set_balance(gl_instance_t *in, uint32_t p, int64_t balance, int64_t credit)
{
    if (!in) return GL_ERR_NULL;
    if (!gl_parts_ok(in) || p >= in->num_parts) return GL_ERR_RANGE;
    if (balance < 0 || credit < 0) return GL_ERR_BALANCE;
    in->balance[p] = balance;
    in->credit[p] = credit;
    return GL_OK;
}

gl_status_t gl_add_payment(gl_instance_t *in, uint32_t from, uint32_t to, int64_t amount,
                           uint32_t prio, uint32_t seq, uint32_t *out_index)
{
    uint32_t i;
    if (!in) return GL_ERR_NULL;
    if (!gl_parts_ok(in)) return GL_ERR_RANGE;
    if (in->num_pay >= GL_MAX_PAY) return GL_ERR_FULL;
    if (from >= in->num_parts || to >= in->num_parts) return GL_ERR_RANGE;
    if (from == to) return GL_ERR_SELF;
    if (amount <= 0) return GL_ERR_AMOUNT;
    if (prio >= GL_MAX_CLASSES) return GL_ERR_PARAM;
    i = in->num_pay++;
    in->from[i] = (uint8_t) from;
    in->to[i] = (uint8_t) to;
    in->amount[i] = amount;
    in->prio[i] = (uint8_t) prio;
    in->seq[i] = seq;
    in->status[i] = (uint8_t) GL_TRUE;
    if (out_index) *out_index = i;
    return GL_OK;
}

gl_status_t gl_report_status(gl_instance_t *in, uint32_t idx, gl_lp_t report)
{
    if (!in) return GL_ERR_NULL;
    if (idx >= in->num_pay || idx >= GL_MAX_PAY || (uint32_t) report > 3u) return GL_ERR_PARAM;
    in->status[idx] = (uint8_t) gl_lp_join((gl_lp_t) in->status[idx], report);
    return GL_OK;
}

gl_status_t gl_set_status(gl_instance_t *in, uint32_t idx, gl_lp_t status)
{
    if (!in) return GL_ERR_NULL;
    if (idx >= in->num_pay || idx >= GL_MAX_PAY || (uint32_t) status > 3u) return GL_ERR_PARAM;
    in->status[idx] = (uint8_t) status;
    return GL_OK;
}

gl_status_t gl_check(const gl_instance_t *in)
{
    int64_t tot = 0;
    uint32_t p, i;
    if (!in) return GL_ERR_NULL;
    if (!gl_parts_ok(in) || in->num_pay > GL_MAX_PAY) return GL_ERR_RANGE;
    for (p = 0; p < in->num_parts; p++) {
        if (in->balance[p] < 0 || in->credit[p] < 0) return GL_ERR_BALANCE;
        if (__builtin_add_overflow(tot, in->balance[p], &tot)) return GL_ERR_OVERFLOW;
        if (__builtin_add_overflow(tot, in->credit[p], &tot)) return GL_ERR_OVERFLOW;
    }
    for (i = 0; i < in->num_pay; i++) {
        if (in->from[i] >= in->num_parts || in->to[i] >= in->num_parts) return GL_ERR_RANGE;
        if (in->from[i] == in->to[i]) return GL_ERR_SELF;
        if (in->amount[i] <= 0) return GL_ERR_AMOUNT;
        if (in->prio[i] >= GL_MAX_CLASSES || in->status[i] > 3u) return GL_ERR_PARAM;
        if (__builtin_add_overflow(tot, in->amount[i], &tot)) return GL_ERR_OVERFLOW;
    }
    return GL_OK;
}

gl_status_t gl_opts_default(gl_opts_t *o)
{
    if (!o) return GL_ERR_NULL;
    o->fifo = 0;
    o->objective = GL_OBJ_VALUE;
    o->removal = GL_RM_LAST;
    o->node_budget = 1000000u;
    return GL_OK;
}

gl_status_t gl_opts_check(const gl_opts_t *o)
{
    if (!o) return GL_ERR_NULL;
    if (o->fifo > 1u || o->objective > 1u || o->removal > 1u || o->node_budget == 0u)
        return GL_ERR_PARAM;
    return GL_OK;
}

int gl_key_cmp(const int64_t *a, const int64_t *b, uint32_t n)
{
    uint32_t c;
    for (c = 0; c < n; c++) {
        if (a[c] > b[c]) return 1;
        if (a[c] < b[c]) return -1;
    }
    return 0;
}

static uint32_t gl_nc(const gl_opts_t *o)
{
    return o->objective == GL_OBJ_LEX_PRIORITY ? GL_MAX_CLASSES : 1u;
}

static uint32_t gl_cls(const gl_instance_t *in, const gl_opts_t *o, uint32_t i)
{
    return o->objective == GL_OBJ_LEX_PRIORITY ? in->prio[i] : 0u;
}

/* 1 if payment j is ahead of payment i in the (shared) sender's queue. */
static int gl_ahead(const gl_instance_t *in, uint32_t j, uint32_t i)
{
    return in->seq[j] < in->seq[i] || (in->seq[j] == in->seq[i] && j < i);
}

static void gl_classify(const gl_instance_t *in, const gl_opts_t *o, uint8_t *disp)
{
    uint32_t i, j;
    for (i = 0; i < in->num_pay; i++) {
        uint8_t s = in->status[i];
        if (s == GL_FALSE)
            disp[i] = GL_D_CANCELLED;
        else if (s == GL_BOTH)
            disp[i] = GL_D_HELD_BOTH;
        else if (s == GL_NEITHER)
            disp[i] = GL_D_HELD_NEITHER;
        else {
            disp[i] = GL_D_QUEUED;
            if (o->fifo)
                for (j = 0; j < in->num_pay; j++)
                    if (j != i && in->from[j] == in->from[i] && gl_ahead(in, j, i) &&
                        (in->status[j] == GL_BOTH || in->status[j] == GL_NEITHER)) {
                        disp[i] = GL_D_BLOCKED_FIFO;
                        break;
                    }
        }
    }
}

gl_status_t gl_verify(const gl_instance_t *in, const gl_opts_t *o, const uint8_t *settle,
                      int64_t *balance_after, int64_t *key, int64_t *value)
{
    uint8_t disp[GL_MAX_PAY];
    int64_t b[GL_MAX_PART], k[GL_MAX_CLASSES];
    int64_t v = 0, s0 = 0, s1 = 0;
    uint32_t i, j, p, c;
    gl_status_t st;
    if (!in || !o || !settle) return GL_ERR_NULL;
    if ((st = gl_check(in)) != GL_OK) return st;
    if ((st = gl_opts_check(o)) != GL_OK) return st;
    gl_classify(in, o, disp);
    for (i = 0; i < in->num_pay; i++) {
        if (settle[i] > 1u) return GL_ERR_PARAM;
        if (settle[i] && disp[i] != GL_D_QUEUED) return GL_ERR_HELD;
    }
    if (o->fifo)
        for (i = 0; i < in->num_pay; i++)
            if (settle[i])
                for (j = 0; j < in->num_pay; j++)
                    if (!settle[j] && disp[j] == GL_D_QUEUED && in->from[j] == in->from[i] &&
                        gl_ahead(in, j, i))
                        return GL_ERR_FIFO;
    for (c = 0; c < GL_MAX_CLASSES; c++) k[c] = 0;
    for (p = 0; p < in->num_parts; p++) b[p] = in->balance[p];
    for (i = 0; i < in->num_pay; i++) {
        int64_t a = in->amount[i];
        if (!settle[i]) continue;
        c = gl_cls(in, o, i);
        if (__builtin_sub_overflow(b[in->from[i]], a, &b[in->from[i]]) ||
            __builtin_add_overflow(b[in->to[i]], a, &b[in->to[i]]) ||
            __builtin_add_overflow(v, a, &v) || __builtin_add_overflow(k[c], a, &k[c]))
            return GL_ERR_OVERFLOW;
    }
    for (p = 0; p < in->num_parts; p++) {
        if (b[p] < -in->credit[p]) return GL_ERR_INFEASIBLE;
        if (__builtin_add_overflow(s0, in->balance[p], &s0) ||
            __builtin_add_overflow(s1, b[p], &s1))
            return GL_ERR_OVERFLOW;
    }
    if (s0 != s1) return GL_ERR_INTERNAL; /* conservation */
    if (balance_after) {
        for (p = 0; p < in->num_parts; p++) balance_after[p] = b[p];
    }
    if (key) {
        for (c = 0; c < GL_MAX_CLASSES; c++) key[c] = k[c];
    }
    if (value) *value = v;
    return GL_OK;
}

static gl_status_t gl_prep(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                           gl_result_t *out)
{
    gl_status_t st;
    if (!in || !o || !ws || !out) return GL_ERR_NULL;
    if ((st = gl_check(in)) != GL_OK) return st;
    if ((st = gl_opts_check(o)) != GL_OK) return st;
    gl_classify(in, o, ws->disp);
    return GL_OK;
}

/* Verifies the chosen set and fills the whole result. */
static gl_status_t gl_finish(const gl_instance_t *in, const gl_opts_t *o, const uint8_t *settle,
                             const uint8_t *disp, uint32_t quality, uint64_t nodes,
                             const int64_t *ub_key, int64_t ub_value, gl_result_t *out)
{
    uint32_t i, c;
    gl_zero(out, (uint32_t) sizeof(*out));
    gl_copy(out->settle, settle, in->num_pay);
    if (gl_verify(in, o, out->settle, out->balance_after, out->key, &out->value) != GL_OK)
        return GL_ERR_INTERNAL;
    for (c = 0; c < GL_MAX_CLASSES; c++) out->ub_key[c] = ub_key[c];
    out->ub_value = ub_value;
    if (gl_key_cmp(out->ub_key, out->key, GL_MAX_CLASSES) < 0 || ub_value < out->value)
        return GL_ERR_INTERNAL;
    for (i = 0; i < in->num_pay; i++) {
        uint8_t d = disp[i];
        if (d == GL_D_QUEUED) out->eligible++;
        if (d == GL_D_CANCELLED) out->cancelled++;
        if (d == GL_D_HELD_BOTH) out->held_both++;
        if (d == GL_D_HELD_NEITHER) out->held_neither++;
        if (d == GL_D_BLOCKED_FIFO) out->blocked_fifo++;
        if (settle[i]) {
            out->settled++;
            d = GL_D_SETTLED;
        }
        out->disp[i] = d;
    }
    out->quality = quality;
    out->nodes = nodes;
    return GL_OK;
}

/* ------------------------------------------------------------------ */
/* Heuristic: iterative removal.                                       */

/* 1 if payment i should be removed before payment j (same sender). */
static int gl_rm_before(const gl_instance_t *in, const gl_opts_t *o, uint32_t rule, uint32_t i,
                        uint32_t j)
{
    if (o->fifo) return gl_ahead(in, j, i); /* the later one goes first */
    if (o->objective == GL_OBJ_LEX_PRIORITY && in->prio[i] != in->prio[j])
        return in->prio[i] > in->prio[j];
    if (rule == GL_RM_LARGEST && in->amount[i] != in->amount[j])
        return in->amount[i] > in->amount[j];
    return gl_ahead(in, j, i);
}

static gl_status_t gl_heur_core(const gl_instance_t *in, const gl_opts_t *o, uint32_t rule,
                                gl_workspace_t *ws, uint8_t *incl, uint64_t *steps)
{
    int64_t *pos = ws->indec;
    uint32_t i, p;
    *steps = 0;
    for (p = 0; p < in->num_parts; p++) pos[p] = in->balance[p] + in->credit[p];
    for (i = 0; i < in->num_pay; i++) {
        incl[i] = (uint8_t) (ws->disp[i] == GL_D_QUEUED);
        if (incl[i]) {
            pos[in->from[i]] -= in->amount[i];
            pos[in->to[i]] += in->amount[i];
        }
    }
    for (;;) {
        uint32_t worst = GL_MAX_PART, r = GL_MAX_PAY;
        for (p = 0; p < in->num_parts; p++)
            if (pos[p] < 0 && (worst == GL_MAX_PART || pos[p] < pos[worst])) worst = p;
        if (worst == GL_MAX_PART) break;
        for (i = 0; i < in->num_pay; i++)
            if (incl[i] && in->from[i] == worst &&
                (r == GL_MAX_PAY || gl_rm_before(in, o, rule, i, r)))
                r = i;
        if (r == GL_MAX_PAY) return GL_ERR_INTERNAL;
        incl[r] = 0;
        pos[worst] += in->amount[r];
        pos[in->to[r]] -= in->amount[r];
        (*steps)++;
    }
    return GL_OK;
}

/* Trivial bound: every eligible payment settles. */
static void gl_trivial_ub(const gl_instance_t *in, const gl_opts_t *o, const uint8_t *disp,
                          int64_t *ub, int64_t *ubv)
{
    uint32_t i, c;
    for (c = 0; c < GL_MAX_CLASSES; c++) ub[c] = 0;
    *ubv = 0;
    for (i = 0; i < in->num_pay; i++)
        if (disp[i] == GL_D_QUEUED) {
            ub[gl_cls(in, o, i)] += in->amount[i];
            *ubv += in->amount[i];
        }
}

gl_status_t gl_heuristic(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                         gl_result_t *out)
{
    int64_t ub[GL_MAX_CLASSES], ubv;
    uint64_t steps;
    gl_status_t st = gl_prep(in, o, ws, out);
    if (st != GL_OK) return st;
    if ((st = gl_heur_core(in, o, o->removal, ws, ws->best, &steps)) != GL_OK) return st;
    gl_trivial_ub(in, o, ws->disp, ub, &ubv);
    return gl_finish(in, o, ws->best, ws->disp, GL_HEURISTIC, steps, ub, ubv, out);
}

/* ------------------------------------------------------------------ */
/* Branch and bound, free mode.                                        */

static int64_t gl_min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

/* Lexicographic refinement: class c can be at most its
 * own optimistic total and at most T minus the value already fixed in the
 * other classes. */
static void gl_lex_bound(const gl_workspace_t *ws, uint32_t nc, int64_t t, const int64_t *opt_c,
                         int64_t *b)
{
    uint32_t c;
    int64_t dec = 0;
    for (c = 0; c < GL_MAX_CLASSES; c++) b[c] = 0;
    if (nc == 1u) {
        b[0] = t;
        return;
    }
    for (c = 0; c < GL_MAX_CLASSES; c++) dec += ws->kcur[c];
    for (c = 0; c < GL_MAX_CLASSES; c++) b[c] = gl_min64(opt_c[c], t - (dec - ws->kcur[c]));
}

/* Upper bound on the total value of any completion of the current node, or
 * -1 when no completion is feasible. M[p] bounds what p can pay in total:
 * at most everything it still has queued, and at most its funds plus what
 * can still arrive, where an undecided payment i from q can bring at most
 * min(a_i, M[q] - outdec[q]). The rule is applied up to iters times (every
 * round keeps M valid and can only lower it). Undecided payments are
 * ord[d..n_ord-1]. */
static int64_t gl_free_total(const gl_instance_t *in, gl_workspace_t *ws, uint32_t d,
                             uint32_t iters)
{
    int64_t t = 0;
    uint32_t p, k, it, np = in->num_parts;
    for (p = 0; p < np; p++)
        ws->mb[p] =
            gl_min64(ws->outdec[p] + ws->outund[p], ws->capb[p] + ws->indec[p] + ws->inund[p]);
    for (it = 0; it < iters; it++) {
        int changed = 0;
        for (p = 0; p < np; p++) ws->acc[p] = ws->capb[p] + ws->indec[p];
        for (k = d; k < ws->n_ord; k++) {
            uint32_t i = ws->ord[k], q = in->from[i];
            ws->acc[in->to[i]] += gl_min64(in->amount[i], ws->mb[q] - ws->outdec[q]);
        }
        for (p = 0; p < np; p++) {
            int64_t m = gl_min64(ws->outdec[p] + ws->outund[p], ws->acc[p]);
            if (m < ws->outdec[p]) return -1; /* p cannot cover what it already pays */
            if (m < ws->mb[p]) {
                ws->mb[p] = m;
                changed = 1;
            }
        }
        if (!changed) break;
    }
    for (p = 0; p < np; p++) t += ws->mb[p];
    return t;
}

/* 0 when the node has no feasible completion. */
static int gl_free_bound(const gl_instance_t *in, gl_workspace_t *ws, uint32_t d, uint32_t nc,
                         int64_t *b)
{
    int64_t opt[GL_MAX_CLASSES], t = gl_free_total(in, ws, d, GL_BOUND_ITERS);
    uint32_t c;
    if (t < 0) return 0;
    for (c = 0; c < GL_MAX_CLASSES; c++) opt[c] = ws->kcur[c] + ws->kund[c];
    gl_lex_bound(ws, nc, t, opt, b);
    return 1;
}

static void gl_kmax(int64_t *ub, const int64_t *b)
{
    if (gl_key_cmp(b, ub, GL_MAX_CLASSES) > 0) {
        uint32_t c;
        for (c = 0; c < GL_MAX_CLASSES; c++) ub[c] = b[c];
    }
}

static void gl_free_move(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                         uint32_t i, int incl, int64_t sign)
{
    uint32_t s = in->from[i], t = in->to[i], c = gl_cls(in, o, i);
    int64_t a = sign * in->amount[i];
    ws->outund[s] -= a;
    ws->inund[t] -= a;
    ws->kund[c] -= a;
    if (incl) {
        ws->outdec[s] += a;
        ws->indec[t] += a;
        ws->kcur[c] += a;
        ws->cur[i] = (uint8_t) (sign > 0);
    }
}

static void gl_sort_free(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws)
{
    uint32_t i, k, n = 0;
    for (i = 0; i < in->num_pay; i++) {
        if (ws->disp[i] != GL_D_QUEUED) continue;
        k = n++;
        while (k > 0) {
            uint32_t j = ws->ord[k - 1u];
            int after; /* 1 if i belongs after j */
            if (gl_cls(in, o, j) != gl_cls(in, o, i))
                after = gl_cls(in, o, j) < gl_cls(in, o, i);
            else
                after = in->amount[j] >= in->amount[i]; /* stable: ties by index */
            if (after) break;
            ws->ord[k] = ws->ord[k - 1u];
            k--;
        }
        ws->ord[k] = (uint16_t) i;
    }
    ws->n_ord = n;
}

static void gl_setup_flows(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws)
{
    uint32_t p, i, c;
    for (p = 0; p < GL_MAX_PART; p++) {
        ws->capb[p] = p < in->num_parts ? in->balance[p] + in->credit[p] : 0;
        ws->indec[p] = ws->inund[p] = ws->outdec[p] = ws->outund[p] = 0;
        ws->pk[p] = 0;
    }
    for (c = 0; c < GL_MAX_CLASSES; c++) ws->kcur[c] = ws->kund[c] = 0;
    for (i = 0; i < GL_MAX_PAY; i++) ws->cur[i] = 0;
    for (i = 0; i < in->num_pay; i++)
        if (ws->disp[i] == GL_D_QUEUED) {
            ws->inund[in->to[i]] += in->amount[i];
            ws->outund[in->from[i]] += in->amount[i];
            ws->kund[gl_cls(in, o, i)] += in->amount[i];
        }
}

/* Returns 1 if the node budget ran out (ub then holds the proven bound). */
static int gl_bb_free(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                      uint64_t *nodes, int64_t *ub)
{
    uint32_t nc = gl_nc(o), n, d = 0, j;
    gl_sort_free(in, o, ws);
    gl_setup_flows(in, o, ws);
    n = ws->n_ord;
    ws->st[0] = GL_FRESH;
    for (;;) {
        int32_t s = ws->st[d];
        uint32_t i = d < n ? ws->ord[d] : 0u;
        if (s == GL_FRESH) {
            uint32_t snd;
            (*nodes)++;
            if (*nodes > o->node_budget) {
                uint32_t c;
                for (c = 0; c < GL_MAX_CLASSES; c++) ub[c] = ws->kbest[c];
                if (d > 0) gl_kmax(ub, ws->bnd[d - 1u]);
                for (j = 0; j < d; j++)
                    if (ws->st[j] == 1 && !ws->forced[j]) gl_kmax(ub, ws->bnd[j]);
                return 1;
            }
            if (!gl_free_bound(in, ws, d, nc, ws->bnd[d]) ||
                gl_key_cmp(ws->bnd[d], ws->kbest, GL_MAX_CLASSES) <= 0)
                goto up;
            if (d == n) { /* leaf: bound == current key, strictly better */
                for (j = 0; j < GL_MAX_CLASSES; j++) ws->kbest[j] = ws->kcur[j];
                gl_copy(ws->best, ws->cur, in->num_pay);
                goto up;
            }
            snd = in->from[i];
            ws->forced[d] =
                (uint8_t) (ws->capb[snd] + ws->indec[snd] - ws->outdec[snd] - ws->outund[snd] >= 0);
            if (ws->capb[snd] + ws->indec[snd] + ws->inund[snd] - ws->outdec[snd] - in->amount[i] >=
                0) {
                gl_free_move(in, o, ws, i, 1, 1);
                ws->st[d] = 1;
                d++;
                ws->st[d] = GL_FRESH;
                continue;
            }
            goto try_excl;
        } else if (s == 1) {
            gl_free_move(in, o, ws, i, 1, -1);
            goto try_excl;
        } else { /* s == 2: exclude branch finished */
            gl_free_move(in, o, ws, i, 0, -1);
            goto up;
        }
    try_excl:
        ws->st[d] = 2;
        if (ws->forced[d] || gl_key_cmp(ws->bnd[d], ws->kbest, GL_MAX_CLASSES) <= 0) goto up;
        {
            uint32_t rcv = in->to[i];
            if (ws->capb[rcv] + ws->indec[rcv] + ws->inund[rcv] - ws->outdec[rcv] - in->amount[i] <
                0)
                goto up;
        }
        gl_free_move(in, o, ws, i, 0, 1);
        d++;
        ws->st[d] = GL_FRESH;
        continue;
    up:
        if (d == 0) return 0;
        d--;
    }
}

/* ------------------------------------------------------------------ */
/* FIFO mode: the removal fixpoint is the exact optimum.               */
/*
 * Proof sketch. A FIFO-feasible set is a vector k of per-sender prefix
 * lengths. (1) Feasible vectors are closed under componentwise max: if k and
 * k' are feasible and k_p >= k'_p, participant p pays out exactly what it
 * pays under k and receives at least as much, so p stays >= -credit. Hence a
 * unique GREATEST feasible vector k* exists; it contains every other
 * feasible set, so it maximises the settled value and, since amounts are
 * positive, every priority-class total at once (both objectives).
 * (2) The removal heuristic in FIFO mode starts at the full queues (>= k*)
 * and only removes the last payment of a participant p that is negative at
 * the current k >= k*. If k*_p were equal to k_p, p would receive no more
 * under k* than under k and so would be negative under k* too; therefore
 * k*_p < k_p and the removal keeps k >= k*. It stops at a feasible k >= k*,
 * which is k*. So gl_solve returns it as GL_OPTIMAL with no search; the
 * tests check it against brute force on thousands of instances.
 */

/* Per-sender queues of eligible payments in queue order (brute force). */
static void gl_build_fifo(const gl_instance_t *in, gl_workspace_t *ws)
{
    uint32_t s, i, n = 0, k;
    for (s = 0; s < GL_MAX_PART; s++) {
        uint32_t base = n;
        ws->fstart[s] = (uint16_t) n;
        if (s < in->num_parts)
            for (i = 0; i < in->num_pay; i++) {
                if (in->from[i] != s || ws->disp[i] != GL_D_QUEUED) continue;
                k = n++;
                while (k > base && gl_ahead(in, i, ws->ord[k - 1u])) {
                    ws->ord[k] = ws->ord[k - 1u];
                    k--;
                }
                ws->ord[k] = (uint16_t) i;
            }
        ws->flen[s] = (uint16_t) (n - base);
    }
    ws->n_ord = n;
}

/* Greedy re-add: after removal, add back any eligible payment whose sender
 * can now afford it (branching order), until nothing changes. Adding a
 * payment only lowers its sender (checked) and raises its receiver, so the
 * set stays feasible. Used to seed the exact search, not in gl_heuristic. */
static void gl_readd(const gl_instance_t *in, gl_workspace_t *ws, uint8_t *incl)
{
    int64_t *pos = ws->acc;
    uint32_t i, k, p;
    int changed = 1;
    for (p = 0; p < in->num_parts; p++) pos[p] = in->balance[p] + in->credit[p];
    for (i = 0; i < in->num_pay; i++)
        if (incl[i]) {
            pos[in->from[i]] -= in->amount[i];
            pos[in->to[i]] += in->amount[i];
        }
    while (changed) {
        changed = 0;
        for (k = 0; k < ws->n_ord; k++) {
            i = ws->ord[k];
            if (incl[i] || pos[in->from[i]] < in->amount[i]) continue;
            incl[i] = 1;
            pos[in->from[i]] -= in->amount[i];
            pos[in->to[i]] += in->amount[i];
            changed = 1;
        }
    }
}

gl_status_t gl_solve(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                     gl_result_t *out)
{
    int64_t ub[GL_MAX_CLASSES], key[GL_MAX_CLASSES], ubv, val = 0;
    uint64_t steps, nodes = 0;
    uint32_t c, rule;
    int ab;
    gl_status_t st = gl_prep(in, o, ws, out);
    if (st != GL_OK) return st;
    if (o->fifo) { /* greatest fixpoint == optimum (see the proof above) */
        if ((st = gl_heur_core(in, o, GL_RM_LAST, ws, ws->best, &steps)) != GL_OK) return st;
        if (gl_verify(in, o, ws->best, (int64_t *) 0, ws->kbest, &val) != GL_OK)
            return GL_ERR_INTERNAL;
        return gl_finish(in, o, ws->best, ws->disp, GL_OPTIMAL, steps, ws->kbest, val, out);
    }
    /* incumbent: the better of both removal rules, each followed by re-add */
    gl_sort_free(in, o, ws);
    for (rule = GL_RM_LAST; rule <= GL_RM_LARGEST; rule++) {
        if ((st = gl_heur_core(in, o, rule, ws, ws->cur, &steps)) != GL_OK) return st;
        gl_readd(in, ws, ws->cur);
        if (gl_verify(in, o, ws->cur, (int64_t *) 0, key, &val) != GL_OK) return GL_ERR_INTERNAL;
        if (rule == GL_RM_LAST || gl_key_cmp(key, ws->kbest, GL_MAX_CLASSES) > 0) {
            for (c = 0; c < GL_MAX_CLASSES; c++) ws->kbest[c] = key[c];
            gl_copy(ws->best, ws->cur, in->num_pay);
        }
    }
    /* root total bound (valid for every feasible set, either objective) */
    gl_setup_flows(in, o, ws);
    ubv = gl_free_total(in, ws, 0, GL_ROOT_ITERS);
    if (ubv < 0) return GL_ERR_INTERNAL; /* the empty set is always feasible */
    ab = gl_bb_free(in, o, ws, &nodes, ub);
    if (!ab)
        for (c = 0; c < GL_MAX_CLASSES; c++) ub[c] = ws->kbest[c];
    if (o->objective == GL_OBJ_VALUE) {
        ubv = gl_min64(ubv, ub[0]);
        ub[0] = ubv;
    }
    return gl_finish(in, o, ws->best, ws->disp, ab ? GL_FEASIBLE : GL_OPTIMAL, nodes, ub, ubv, out);
}

/* ------------------------------------------------------------------ */
/* Brute force.                                                        */

static int gl_eval(const gl_instance_t *in, const gl_opts_t *o, const uint8_t *sel, int64_t *key,
                   int64_t *val)
{
    int64_t pos[GL_MAX_PART];
    uint32_t p, i, c;
    for (c = 0; c < GL_MAX_CLASSES; c++) key[c] = 0;
    *val = 0;
    for (p = 0; p < in->num_parts; p++) pos[p] = in->balance[p] + in->credit[p];
    for (i = 0; i < in->num_pay; i++)
        if (sel[i]) {
            pos[in->from[i]] -= in->amount[i];
            pos[in->to[i]] += in->amount[i];
            key[gl_cls(in, o, i)] += in->amount[i];
            *val += in->amount[i];
        }
    for (p = 0; p < in->num_parts; p++)
        if (pos[p] < 0) return 0;
    return 1;
}

gl_status_t gl_brute_force(const gl_instance_t *in, const gl_opts_t *o, gl_workspace_t *ws,
                           gl_result_t *out)
{
    int64_t key[GL_MAX_CLASSES], bestk[GL_MAX_CLASSES], val, maxv = 0;
    uint64_t nodes = 0;
    uint32_t i, n = 0, c;
    gl_status_t st = gl_prep(in, o, ws, out);
    if (st != GL_OK) return st;
    for (i = 0; i < in->num_pay; i++)
        if (ws->disp[i] == GL_D_QUEUED) ws->ord[n++] = (uint16_t) i;
    if (n > GL_BRUTE_MAX) return GL_ERR_TOO_BIG;
    for (c = 0; c < GL_MAX_CLASSES; c++) bestk[c] = 0;
    for (i = 0; i < GL_MAX_PAY; i++) ws->cur[i] = ws->best[i] = 0;
    if (!o->fifo) {
        uint32_t m, lim = 1u << n;
        for (m = 0; m < lim; m++) {
            for (i = 0; i < n; i++) ws->cur[ws->ord[i]] = (uint8_t) ((m >> i) & 1u);
            nodes++;
            if (!gl_eval(in, o, ws->cur, key, &val)) continue;
            if (val > maxv) maxv = val;
            if (gl_key_cmp(key, bestk, GL_MAX_CLASSES) > 0) {
                for (c = 0; c < GL_MAX_CLASSES; c++) bestk[c] = key[c];
                gl_copy(ws->best, ws->cur, in->num_pay);
            }
        }
    } else {
        uint32_t s, k;
        gl_build_fifo(in, ws);
        for (s = 0; s < GL_MAX_PART; s++) ws->pk[s] = 0;
        for (;;) {
            for (k = 0; k < in->num_pay; k++) ws->cur[k] = 0;
            for (s = 0; s < in->num_parts; s++)
                for (k = 0; k < ws->pk[s]; k++) ws->cur[ws->ord[ws->fstart[s] + k]] = 1;
            nodes++;
            if (gl_eval(in, o, ws->cur, key, &val)) {
                if (val > maxv) maxv = val;
                if (gl_key_cmp(key, bestk, GL_MAX_CLASSES) > 0) {
                    for (c = 0; c < GL_MAX_CLASSES; c++) bestk[c] = key[c];
                    gl_copy(ws->best, ws->cur, in->num_pay);
                }
            }
            /* mixed-radix increment over prefix lengths */
            for (s = 0; s < in->num_parts; s++) {
                if (ws->pk[s] < ws->flen[s]) {
                    ws->pk[s]++;
                    break;
                }
                ws->pk[s] = 0;
            }
            if (s == in->num_parts) break;
        }
    }
    return gl_finish(in, o, ws->best, ws->disp, GL_OPTIMAL, nodes, bestk, maxv, out);
}
