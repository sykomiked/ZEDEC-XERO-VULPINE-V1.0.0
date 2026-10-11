/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zau_absint.c — exports.analysis: a BOUNDED interval interpretation of a
 * straight-line export body.
 *
 * Every value is an interval [lo, hi] of its type (i32 as signed 32-bit,
 * i64 as signed 64-bit) or "any value of its type" (top). Parameters start
 * from the contract's declared ranges (the z-contract guard refuses any
 * call outside them before the body runs), declared locals at 0, constants
 * as themselves. add, sub and mul work on intervals and give top on any
 * possible overflow (Wasm wraps, so top is the honest answer); other
 * integer operations are folded when both operands are known exactly.
 *
 * Only straight-line bodies are analysed: the first block, loop, if,
 * branch, call of a defined function, global access or other instruction
 * outside the subset below ends the analysis with a NOT_ANALYZED note. At
 * most ZAU_ABS_STEPS instructions and ZAU_ABS_STACK stack slots.
 *
 * What it can say, for a body it finished: the result is always inside the
 * declared range, may leave it, or always leaves it on a normal return;
 * a constant proposal (num and den known exactly) breaks the proposes
 * type; more propose calls than `max N`; propose calls with no `proposes`
 * clause. A body that traps never returns normally, so these statements
 * are about normal returns only. This is a bounded check, not a proof.
 * Integer code only (R11). */
#include <stdio.h>
#include <string.h>

#include "zau_internal.h"

#define ZAU_ABS_STEPS 4096u
#define ZAU_ABS_STACK 64u
#define ZAU_ABS_LOCALS (ZAU_MAX_SIG + 256u)

typedef struct {
    int64_t lo, hi;
    int top;
    uint8_t vt; /* WT_I32 or WT_I64 */
} av;

static av mk(uint8_t vt, int64_t lo, int64_t hi) {
    av a;
    a.lo = lo;
    a.hi = hi;
    a.top = 0;
    a.vt = vt;
    return a;
}

static av top_of(uint8_t vt) {
    av a = mk(vt, 0, 0);
    a.top = 1;
    return a;
}

static int64_t tmin(uint8_t vt) { return vt == WT_I32 ? INT32_MIN : INT64_MIN; }
static int64_t tmax(uint8_t vt) { return vt == WT_I32 ? INT32_MAX : INT64_MAX; }
static int64_t lo_of(av a) { return a.top ? tmin(a.vt) : a.lo; }
static int64_t hi_of(av a) { return a.top ? tmax(a.vt) : a.hi; }

/* Normalise: an i32 interval outside int32 becomes top. */
static av fix(av a) {
    if (!a.top && (a.lo < tmin(a.vt) || a.hi > tmax(a.vt) || a.lo > a.hi)) return top_of(a.vt);
    return a;
}

/* Checked int64 arithmetic without relying on overflow behaviour. */
static int add_ok(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return 0;
    *out = a + b;
    return 1;
}

static int sub_ok(int64_t a, int64_t b, int64_t *out) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return 0;
    *out = a - b;
    return 1;
}

static int mul_ok(int64_t a, int64_t b, int64_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return 1;
    }
    if (a == -1) {
        if (b == INT64_MIN) return 0;
        *out = -b;
        return 1;
    }
    if (b == -1) {
        if (a == INT64_MIN) return 0;
        *out = -a;
        return 1;
    }
    if (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
              : (b > 0 ? a < INT64_MIN / b : a < INT64_MAX / b))
        return 0;
    *out = a * b;
    return 1;
}

static av iv_add(av a, av b) {
    int64_t l, h;
    if (a.top || b.top || !add_ok(a.lo, b.lo, &l) || !add_ok(a.hi, b.hi, &h)) return top_of(a.vt);
    return fix(mk(a.vt, l, h));
}

static av iv_sub(av a, av b) {
    int64_t l, h;
    if (a.top || b.top || !sub_ok(a.lo, b.hi, &l) || !sub_ok(a.hi, b.lo, &h)) return top_of(a.vt);
    return fix(mk(a.vt, l, h));
}

static av iv_mul(av a, av b) {
    int64_t c[4];
    if (a.top || b.top || !mul_ok(a.lo, b.lo, &c[0]) || !mul_ok(a.lo, b.hi, &c[1]) ||
        !mul_ok(a.hi, b.lo, &c[2]) || !mul_ok(a.hi, b.hi, &c[3]))
        return top_of(a.vt);
    int64_t l = c[0], h = c[0];
    for (int i = 1; i < 4; i++) {
        if (c[i] < l) l = c[i];
        if (c[i] > h) h = c[i];
    }
    return fix(mk(a.vt, l, h));
}

static int single(av a) { return !a.top && a.lo == a.hi; }

/* The bit pattern of a value as an unsigned of its width. */
static uint64_t bits(av a) {
    return a.vt == WT_I32 ? (uint64_t) (uint32_t) (int32_t) a.lo : (uint64_t) a.lo;
}

/* A bit pattern back to the signed value of its width. */
static av from_bits(uint8_t vt, uint64_t u) {
    if (vt == WT_I32) {
        uint32_t w = (uint32_t) u;
        int64_t v = w > (uint32_t) INT32_MAX ? (int64_t) w - (INT64_C(1) << 32) : (int64_t) w;
        return mk(vt, v, v);
    }
    int64_t v = u > (uint64_t) INT64_MAX ? -(int64_t) (~u) - 1 : (int64_t) u;
    return mk(vt, v, v);
}

/* Fold a binary integer operation on exactly known operands. Returns 0 if
 * the operation is not folded (or would trap: division by zero, overflow). */
static int fold(uint32_t op, av a, av b, av *out) {
    uint8_t vt = a.vt;
    unsigned w = vt == WT_I32 ? 32u : 64u;
    uint64_t x = bits(a), y = bits(b), mask = vt == WT_I32 ? 0xFFFFFFFFu : UINT64_MAX;
    int64_t sx = a.lo, sy = b.lo;
    uint32_t k = op >= 0x7C ? op - 0x7C : op - 0x6A; /* 0 add 1 sub 2 mul 3 div_s 4 div_u 5 rem_s */
                                                     /* 6 rem_u 7 and 8 or 9 xor 10 shl 11 shr_s  */
                                                     /* 12 shr_u 13 rotl 14 rotr                  */
    switch (k) {
    case 3:
        if (sy == 0 || (sy == -1 && sx == tmin(vt))) return 0;
        *out = mk(vt, sx / sy, sx / sy);
        return 1;
    case 4:
        if (y == 0) return 0;
        *out = from_bits(vt, x / y);
        return 1;
    case 5:
        if (sy == 0) return 0;
        *out = mk(vt, sy == -1 ? 0 : sx % sy, sy == -1 ? 0 : sx % sy);
        return 1;
    case 6:
        if (y == 0) return 0;
        *out = from_bits(vt, x % y);
        return 1;
    case 7: *out = from_bits(vt, x & y); return 1;
    case 8: *out = from_bits(vt, x | y); return 1;
    case 9: *out = from_bits(vt, x ^ y); return 1;
    case 10: *out = from_bits(vt, (x << (y % w)) & mask); return 1;
    case 11: {
        unsigned s = (unsigned) (y % w);
        uint64_t r = x >> s;
        if (s && (x >> (w - 1u)) & 1u) r |= (mask << (w - s)) & mask;
        *out = from_bits(vt, r & mask);
        return 1;
    }
    case 12: *out = from_bits(vt, x >> (y % w)); return 1;
    case 13: {
        unsigned s = (unsigned) (y % w);
        *out = from_bits(vt, s ? ((x << s) | (x >> (w - s))) & mask : x);
        return 1;
    }
    case 14: {
        unsigned s = (unsigned) (y % w);
        *out = from_bits(vt, s ? ((x >> s) | (x << (w - s))) & mask : x);
        return 1;
    }
    default: return 0;
    }
}

/* Fold a comparison (0x46..0x4F for i32, 0x51..0x5A for i64). */
static int cmp(uint32_t op, av a, av b) {
    uint32_t k = op >= 0x51 ? op - 0x51 : op - 0x46; /* eq ne lt_s lt_u gt_s gt_u le_s le_u ge_s ge_u */
    uint64_t x = bits(a), y = bits(b);
    switch (k) {
    case 0: return x == y;
    case 1: return x != y;
    case 2: return a.lo < b.lo;
    case 3: return x < y;
    case 4: return a.lo > b.lo;
    case 5: return x > y;
    case 6: return a.lo <= b.lo;
    case 7: return x <= y;
    case 8: return a.lo >= b.lo;
    default: return x >= y;
    }
}

typedef struct {
    zau_report *r;
    const zau_module *m;
    const uint8_t *w;
    const char *exp;
    uint32_t fidx;
    const zc_contract *c;
    const char *fn;
    av st[ZAU_ABS_STACK];
    uint32_t sp;
    av loc[ZAU_ABS_LOCALS];
    uint32_t nloc;
    uint32_t proposes;
    int stopped;
} A;

static void stop(A *a, size_t off, const char *why) {
    zau_add(a->r, ZAU_C_ANALYSIS, ZAU_INFO, "NOT_ANALYZED", a->exp, (int64_t) a->fidx, (int64_t) off,
            "%s: not analysed (%s)", a->exp, why);
    a->stopped = 1;
}

static int push(A *a, av v, size_t off) {
    if (a->sp >= ZAU_ABS_STACK) {
        stop(a, off, "the value stack is deeper than the analyser's bound");
        return -1;
    }
    a->st[a->sp++] = v;
    return 0;
}

static int pop(A *a, av *v, size_t off) {
    if (!a->sp) {
        stop(a, off, "stack underflow: the module is not valid");
        return -1;
    }
    *v = a->st[--a->sp];
    return 0;
}

/* Initial interval of one Wasm parameter from the contract's declaration. */
static av param_iv(uint8_t vt, const zc_bounds *b) {
    switch (b->type) {
    case ZC_T_INT: {
        int64_t lo = b->has_lo ? b->lo_num : INT64_MIN, hi = b->has_hi ? b->hi_num : INT64_MAX;
        if (vt == WT_I32 && (lo < INT32_MIN || hi > INT32_MAX)) return top_of(vt);
        if (!b->has_lo && !b->has_hi) return top_of(vt);
        return mk(vt, lo, hi);
    }
    case ZC_T_TRUTH:
    case ZC_T_TAG: {
        int64_t lo = -1, hi = -1;
        for (int k = 0; k < 64; k++)
            if ((b->set_mask >> k) & 1u) {
                if (lo < 0) lo = k;
                hi = k;
            }
        return lo < 0 ? top_of(vt) : mk(vt, lo, hi);
    }
    default: return top_of(vt);
    }
}

static void check_constant_proposal(A *a, av num, av den, size_t off) {
    zc_vals *v = NULL;
    int32_t reason = 0;
    char msg[300];
    if (!single(num) || !single(den)) return;
    if (zc_vals_create(&v) != ZXV_OK) return;
    if (zc_vals_push_rat(v, num.lo, den.lo) == ZXV_OK &&
        zc_check(a->c, a->fn, ZC_PART_PROPOSALS, v, &reason, NULL, msg, sizeof msg, NULL) == ZC_E_VIOLATION &&
        reason != ZC_V_COUNT)
        zau_add(a->r, ZAU_C_ANALYSIS, ZAU_ERROR, "PROPOSAL_OUT_OF_BOUNDS", a->exp, (int64_t) a->fidx,
                (int64_t) off, "%s proposes the constant %lld/%lld, which breaks its proposes type (%s): "
                "every normal return that staged it would stop the plugin", a->exp,
                (long long) num.lo, (long long) den.lo, msg);
    zc_vals_destroy(v);
}

static void judge_result(A *a, av res, size_t off) {
    zc_bounds b;
    int64_t rl = lo_of(res), rh = hi_of(res);
    if (zc_slot_bounds(a->c, a->fn, ZC_PART_RESULT, 0, &b) != ZXV_OK) return;
    if (b.type == ZC_T_INT) {
        int64_t cl = b.has_lo ? b.lo_num : INT64_MIN, ch = b.has_hi ? b.hi_num : INT64_MAX;
        if (rl >= cl && rh <= ch) {
            zau_add(a->r, ZAU_C_ANALYSIS, ZAU_INFO, "RESULT_IN_RANGE", a->exp, (int64_t) a->fidx, (int64_t) off,
                    "%s: every normal return gives a result in [%lld..%lld], inside the declared range",
                    a->exp, (long long) rl, (long long) rh);
        } else if (res.top) {
            zau_add(a->r, ZAU_C_ANALYSIS, ZAU_INFO, "RESULT_NOT_BOUNDED", a->exp, (int64_t) a->fidx,
                    (int64_t) off, "%s: the analysis cannot bound the result (the runtime guard checks it)",
                    a->exp);
        } else if (rh < cl || rl > ch) {
            zau_add(a->r, ZAU_C_ANALYSIS, ZAU_ERROR, "RESULT_ALWAYS_OUTSIDE", a->exp, (int64_t) a->fidx,
                    (int64_t) off, "%s: every normal return gives a result in [%lld..%lld], outside the "
                    "declared range: the guard would stop the plugin on every call", a->exp,
                    (long long) rl, (long long) rh);
        } else {
            zau_add(a->r, ZAU_C_ANALYSIS, ZAU_WARNING, "RESULT_MAY_LEAVE", a->exp, (int64_t) a->fidx,
                    (int64_t) off, "%s: the result can be anywhere in [%lld..%lld], which is not inside the "
                    "declared range", a->exp, (long long) rl, (long long) rh);
        }
        return;
    }
    if (b.type == ZC_T_TRUTH || b.type == ZC_T_TAG) {
        uint32_t in = 0, out = 0;
        int64_t width = 0;
        if (res.top || !sub_ok(rh, rl, &width) || width > 64) {
            zau_add(a->r, ZAU_C_ANALYSIS, ZAU_INFO, "RESULT_NOT_BOUNDED", a->exp, (int64_t) a->fidx,
                    (int64_t) off, "%s: the analysis cannot bound the result (the runtime guard checks it)",
                    a->exp);
            return;
        }
        for (int64_t v = rl; v <= rh; v++) {
            if (v >= 0 && v <= 63 && ((b.set_mask >> v) & 1u)) in++;
            else out++;
        }
        if (!out)
            zau_add(a->r, ZAU_C_ANALYSIS, ZAU_INFO, "RESULT_IN_RANGE", a->exp, (int64_t) a->fidx, (int64_t) off,
                    "%s: every normal return gives a declared %s value", a->exp,
                    b.type == ZC_T_TRUTH ? "truth" : "tag");
        else if (!in)
            zau_add(a->r, ZAU_C_ANALYSIS, ZAU_ERROR, "RESULT_ALWAYS_OUTSIDE", a->exp, (int64_t) a->fidx,
                    (int64_t) off, "%s: no normal return gives a declared %s value: the guard would stop the "
                    "plugin on every call", a->exp, b.type == ZC_T_TRUTH ? "truth" : "tag");
        else
            zau_add(a->r, ZAU_C_ANALYSIS, ZAU_WARNING, "RESULT_MAY_LEAVE", a->exp, (int64_t) a->fidx,
                    (int64_t) off, "%s: %u of the %u possible results are not declared %s values", a->exp,
                    (unsigned) out, (unsigned) (in + out), b.type == ZC_T_TRUTH ? "truth" : "tag");
    }
}

static int is_propose(const zau_module *m, uint32_t fidx) {
    uint32_t k = 0;
    for (uint32_t i = 0; i < m->nimports; i++) {
        if (m->imports[i].kind != 0) continue;
        if (k++ == fidx)
            return strcmp(m->imports[i].mod, "zxv") == 0 && strcmp(m->imports[i].name, "propose") == 0;
    }
    return 0;
}

static void finish(A *a, const zau_ftype *t, size_t off, uint32_t steps) {
    uint32_t np = 0, maxp = 0;
    int32_t has_res = 0, has_prop = 0;
    if (zc_fn_info(a->c, a->fn, &np, &has_res, &has_prop, &maxp) != ZXV_OK) return;
    if (t->nr == 1) {
        av res;
        if (pop(a, &res, off)) return;
        if (has_res) judge_result(a, res, off);
    }
    if (a->proposes && !has_prop)
        zau_add(a->r, ZAU_C_ANALYSIS, ZAU_ERROR, "PROPOSES_UNDECLARED", a->exp, (int64_t) a->fidx, (int64_t) off,
                "%s calls zxv.propose %u time(s) on every normal return, but its declaration has no "
                "proposes clause: a staged proposal stops the plugin", a->exp, (unsigned) a->proposes);
    else if (has_prop && a->proposes > maxp)
        zau_add(a->r, ZAU_C_ANALYSIS, ZAU_ERROR, "PROPOSAL_COUNT", a->exp, (int64_t) a->fidx, (int64_t) off,
                "%s calls zxv.propose %u times on every normal return; the declaration allows max %u",
                a->exp, (unsigned) a->proposes, (unsigned) maxp);
    zau_add(a->r, ZAU_C_ANALYSIS, ZAU_INFO, "ANALYZED", a->exp, (int64_t) a->fidx, -1,
            "%s: straight-line body analysed (%u instructions, %u propose call(s))", a->exp,
            (unsigned) steps, (unsigned) a->proposes);
}

void zau_analyze(zau_report *r, const zau_module *m, const uint8_t *w, const char *exp,
                 uint32_t fidx, const zc_contract *c, const char *fn) {
    A a;
    const zau_ftype *t = zau_func_type(m, fidx);
    const zau_body *bd;
    uint32_t nparams = 0, k = 0, steps = 0;
    size_t pos;
    memset(&a, 0, sizeof a);
    a.r = r;
    a.m = m;
    a.w = w;
    a.exp = exp;
    a.fidx = fidx;
    a.c = c;
    a.fn = fn;
    if (!t || fidx < m->nfimports || fidx - m->nfimports >= m->nbodies) {
        stop(&a, 0, "an imported or bodiless function");
        return;
    }
    bd = &m->bodies[fidx - m->nfimports];
    if (t->np > ZAU_MAX_SIG || t->nr > 1u || t->has_float || !bd->locals_ok) {
        stop(&a, bd->start, "its type or locals are outside the analyser's bounds");
        return;
    }
    /* parameters, from the contract */
    if (zc_fn_info(c, fn, &nparams, NULL, NULL, NULL) != ZXV_OK) return;
    for (uint32_t i = 0; i < nparams; i++) {
        zc_bounds b;
        if (zc_slot_bounds(c, fn, ZC_PART_PARAMS, i, &b) != ZXV_OK) return;
        uint32_t width = b.type == ZC_T_RAT ? 2u : 1u;
        for (uint32_t j = 0; j < width; j++, k++) {
            if (k >= t->np) {
                stop(&a, bd->start, "the declaration does not bind to this export");
                return;
            }
            a.loc[k] = b.type == ZC_T_RAT ? top_of(t->p[k]) : param_iv(t->p[k], &b);
        }
    }
    if (k != t->np) {
        stop(&a, bd->start, "the declaration does not bind to this export");
        return;
    }
    for (uint32_t i = 0; i < bd->nlocals; i++) a.loc[k + i] = mk(bd->local_type[i], 0, 0);
    a.nloc = k + bd->nlocals;

    pos = bd->code;
    while (pos < bd->end && !a.stopped) {
        zw_ins ins;
        size_t err = 0;
        av x, y, z;
        char d[128];
        if (++steps > ZAU_ABS_STEPS) {
            stop(&a, pos, "more instructions than the analyser's bound");
            return;
        }
        if (zw_decode(w, bd->end, pos, &ins, &err) != ZW_R_OK) {
            stop(&a, pos, "an instruction does not decode");
            return;
        }
        uint32_t op = ins.op;
        switch (op) {
        case 0x01: break; /* nop */
        case 0x02: case 0x03: case 0x04: case 0x05: case 0x0C: case 0x0D: case 0x0E:
            snprintf(d, sizeof d, "a block, loop, if or branch (opcode 0x%02X): only straight-line bodies",
                     (unsigned) op);
            stop(&a, pos, d);
            return;
        case 0x00:
            zau_add(r, ZAU_C_ANALYSIS, ZAU_INFO, "ALWAYS_TRAPS", exp, (int64_t) fidx, (int64_t) pos,
                    "%s reaches unreachable on every call: it never returns normally", exp);
            return;
        case 0x0F: finish(&a, t, pos, steps); return;
        case 0x0B:
            if (pos + 1u == bd->end) {
                finish(&a, t, pos, steps);
                return;
            }
            stop(&a, pos, "a nested end");
            return;
        case 0x1A:
            if (pop(&a, &x, pos)) return;
            break;
        case 0x1B: /* select */
            if (pop(&a, &z, pos) || pop(&a, &y, pos) || pop(&a, &x, pos)) return;
            if (single(z)) x = z.lo ? x : y;
            else if (x.top || y.top) x = top_of(x.vt);
            else x = mk(x.vt, x.lo < y.lo ? x.lo : y.lo, x.hi > y.hi ? x.hi : y.hi);
            if (push(&a, x, pos)) return;
            break;
        case 0x20:
        case 0x21:
        case 0x22:
            if (ins.idx >= a.nloc) {
                stop(&a, pos, "a local index out of range");
                return;
            }
            if (op == 0x20) {
                if (push(&a, a.loc[ins.idx], pos)) return;
            } else if (op == 0x21) {
                if (pop(&a, &x, pos)) return;
                a.loc[ins.idx] = x;
            } else {
                if (!a.sp) {
                    stop(&a, pos, "stack underflow: the module is not valid");
                    return;
                }
                a.loc[ins.idx] = a.st[a.sp - 1u];
            }
            break;
        case 0x41: {
            size_t p = pos + 1u;
            int32_t v = 0;
            if (zw_rd_s32(w, bd->end, &p, &v) != 0 || push(&a, mk(WT_I32, v, v), pos)) return;
            break;
        }
        case 0x42: {
            size_t p = pos + 1u;
            int64_t v = 0;
            if (zw_rd_s64(w, bd->end, &p, &v) != 0 || push(&a, mk(WT_I64, v, v), pos)) return;
            break;
        }
        case 0x45:
        case 0x50: /* eqz */
            if (pop(&a, &x, pos)) return;
            if (push(&a, single(x) ? mk(WT_I32, x.lo == 0, x.lo == 0) : mk(WT_I32, 0, 1), pos)) return;
            break;
        case 0xA7: /* i32.wrap_i64 */
            if (pop(&a, &x, pos)) return;
            if (single(x)) x = from_bits(WT_I32, (uint64_t) x.lo);
            else if (!x.top && x.lo >= INT32_MIN && x.hi <= INT32_MAX) x.vt = WT_I32;
            else x = top_of(WT_I32);
            if (push(&a, x, pos)) return;
            break;
        case 0xAC: /* i64.extend_i32_s */
            if (pop(&a, &x, pos)) return;
            x = x.top ? mk(WT_I64, INT32_MIN, INT32_MAX) : mk(WT_I64, x.lo, x.hi);
            if (push(&a, x, pos)) return;
            break;
        case 0xAD: /* i64.extend_i32_u */
            if (pop(&a, &x, pos)) return;
            if (!x.top && x.lo >= 0) x = mk(WT_I64, x.lo, x.hi);
            else if (!x.top && x.hi < 0) x = mk(WT_I64, x.lo + (INT64_C(1) << 32), x.hi + (INT64_C(1) << 32));
            else x = mk(WT_I64, 0, (INT64_C(1) << 32) - 1);
            if (push(&a, x, pos)) return;
            break;
        case 0x10: {
            const zau_ftype *ct = zau_func_type(m, ins.idx);
            av args[ZAU_MAX_SIG];
            if (ins.idx >= m->nfimports || !ct) {
                snprintf(d, sizeof d, "it calls function %u; no inter-procedural analysis", (unsigned) ins.idx);
                stop(&a, pos, d);
                return;
            }
            if (ct->np > ZAU_MAX_SIG || ct->nr > 1u) {
                stop(&a, pos, "it calls an import with too many parameters");
                return;
            }
            for (uint32_t i = ct->np; i > 0; i--)
                if (pop(&a, &args[i - 1u], pos)) return;
            if (is_propose(m, ins.idx) && ct->np == 5u) {
                a.proposes++;
                check_constant_proposal(&a, args[1], args[2], pos);
            }
            if (ct->nr == 1u && push(&a, top_of(ct->r[0]), pos)) return;
            break;
        }
        case 0x3F: /* memory.size */
            if (push(&a, mk(WT_I32, 0, 65536), pos)) return;
            break;
        case 0x40: /* memory.grow */
            if (pop(&a, &x, pos) || push(&a, mk(WT_I32, -1, 65536), pos)) return;
            break;
        default:
            if (op >= 0x28 && op <= 0x35 && !zw_is_float_op(op)) { /* integer loads */
                if (pop(&a, &x, pos)) return;
                uint8_t vt = (op == 0x28 || (op >= 0x2C && op <= 0x2F)) ? WT_I32 : WT_I64;
                if (push(&a, top_of(vt), pos)) return;
                break;
            }
            if (op >= 0x36 && op <= 0x3E && !zw_is_float_op(op)) { /* integer stores */
                if (pop(&a, &x, pos) || pop(&a, &y, pos)) return;
                break;
            }
            if ((op >= 0x46 && op <= 0x4F) || (op >= 0x51 && op <= 0x5A)) { /* comparisons */
                if (pop(&a, &y, pos) || pop(&a, &x, pos)) return;
                if (single(x) && single(y)) {
                    int v = cmp(op, x, y);
                    x = mk(WT_I32, v, v);
                } else {
                    x = mk(WT_I32, 0, 1);
                }
                if (push(&a, x, pos)) return;
                break;
            }
            if ((op >= 0x67 && op <= 0x69) || (op >= 0x79 && op <= 0x7B)) { /* clz ctz popcnt */
                if (pop(&a, &x, pos)) return;
                if (push(&a, mk(x.vt, 0, x.vt == WT_I32 ? 32 : 64), pos)) return;
                break;
            }
            if ((op >= 0x6A && op <= 0x78) || (op >= 0x7C && op <= 0x8A)) { /* binary integer ops */
                av res;
                if (pop(&a, &y, pos) || pop(&a, &x, pos)) return;
                uint32_t kk = op >= 0x7C ? op - 0x7C : op - 0x6A;
                if (single(x) && single(y) && kk >= 3u && fold(op, x, y, &res)) {
                    /* folded */
                } else if (kk == 0u) {
                    res = iv_add(x, y);
                } else if (kk == 1u) {
                    res = iv_sub(x, y);
                } else if (kk == 2u) {
                    res = iv_mul(x, y);
                } else if (kk == 7u && !x.top && !y.top && x.lo >= 0 && y.lo >= 0) {
                    res = mk(x.vt, 0, x.hi < y.hi ? x.hi : y.hi); /* and of non-negatives */
                } else {
                    res = top_of(x.vt);
                }
                if (push(&a, res, pos)) return;
                break;
            }
            snprintf(d, sizeof d, "instruction 0x%02X is outside the analyser's straight-line subset",
                     (unsigned) (op > 0xFFu ? 0xFCu : op));
            stop(&a, pos, d);
            return;
        }
        pos += ins.len;
    }
    if (!a.stopped) stop(&a, pos, "the body has no final end");
}
