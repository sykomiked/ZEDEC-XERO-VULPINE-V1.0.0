/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mfg_gcode.c — F.2 toolpath checker (see mfg_gcode.h).
 *
 * WHAT IT DOES
 *   Parses G-code text exactly into integer micrometres and checks every
 *   block against a machine profile, producing a list of issues and a
 *   RELEASE_OK / HOLD / REJECT verdict. Read-only and advisory: it never
 *   talks to a machine; the machine's interlocks and a person stay in
 *   charge.
 *
 * HONEST LIMITS
 *   Point tool, G17 only, no offsets or cycles; conservative arc bounding
 *   boxes may HOLD safe arcs. Details in mfg_gcode.h.
 */
#include "mfg_gcode.h"

#define AX_X 0
#define AX_Y 1
#define AX_Z 2
#define AX_I 3
#define AX_J 4
#define AX_F 5
#define AX_S 6
#define NVAL 7

/* ---- small helpers ---------------------------------------------------- */

static int64_t mfg_min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static int64_t mfg_max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int mfg_in_coord(int64_t v)
{
    return v >= -MFG_GC_COORD_MAX && v <= MFG_GC_COORD_MAX;
}

uint64_t mfg_isqrt_u64(uint64_t v)
{
    uint64_t res = 0;
    uint64_t bit = (uint64_t) 1 << 62;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* Unsigned 64-bit division by shift-and-subtract, so 32-bit targets do not
 * need the compiler's __udivdi3/__umoddi3 helpers. d must be non-zero. */
static uint64_t mfg_udivmod(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    *rem = r;
    return q;
}

/* Parse optional sign, digits, optional '.', digits. Trailing fraction
 * zeros are stripped so 1.500 == 15/10. */
static mfg_status_t mfg_parse_dec(const char *s, size_t len, int64_t *mant, uint32_t *frac)
{
    size_t i = 0;
    int neg = 0, dot = 0, digits = 0;
    uint32_t k = 0;
    int64_t m = 0;
    if (len == 0) return MFG_ERR_RANGE;
    if (s[0] == '+' || s[0] == '-') {
        neg = s[0] == '-';
        i = 1;
    }
    for (; i < len; i++) {
        char c = s[i];
        if (c == '.') {
            if (dot) return MFG_ERR_RANGE;
            dot = 1;
            continue;
        }
        if (c < '0' || c > '9') return MFG_ERR_RANGE;
        digits++;
        if (dot) {
            if (k >= 9u) return MFG_ERR_STATE; /* more than 1e-9 resolution */
            k++;
        }
        if (__builtin_mul_overflow(m, (int64_t) 10, &m)) return MFG_ERR_OVERFLOW;
        if (__builtin_add_overflow(m, (int64_t) (c - '0'), &m)) return MFG_ERR_OVERFLOW;
    }
    if (digits == 0) return MFG_ERR_RANGE;
    while (k > 0) {
        uint64_t rem, q = mfg_udivmod((uint64_t) m, 10u, &rem);
        if (rem != 0) break;
        m = (int64_t) q;
        k--;
    }
    *mant = neg ? -m : m;
    *frac = k;
    return MFG_OK;
}

/* value = mant / 10^frac; returns value * mult exactly or MFG_ERR_STATE if
 * not an integer. */
static mfg_status_t mfg_scale_exact(int64_t mant, uint32_t frac, int64_t mult, int64_t *out)
{
    int64_t p = 1, prod;
    uint64_t mag, q, rem;
    uint32_t i;
    for (i = 0; i < frac; i++) p *= 10;
    if (__builtin_mul_overflow(mant, mult, &prod)) return MFG_ERR_OVERFLOW;
    mag = prod < 0 ? (uint64_t) 0 - (uint64_t) prod : (uint64_t) prod;
    q = mfg_udivmod(mag, (uint64_t) p, &rem);
    if (rem != 0) return MFG_ERR_STATE;
    if (q > (uint64_t) INT64_MAX) return MFG_ERR_OVERFLOW;
    *out = prod < 0 ? -(int64_t) q : (int64_t) q;
    return MFG_OK;
}

mfg_status_t mfg_gc_parse_um(const char *s, size_t len, int units_inch, int64_t *out_um)
{
    int64_t m;
    uint32_t k;
    mfg_status_t st;
    if (!s || !out_um) return MFG_ERR_NULL;
    if (units_inch != 0 && units_inch != 1) return MFG_ERR_RANGE;
    st = mfg_parse_dec(s, len, &m, &k);
    if (st != MFG_OK) return st;
    return mfg_scale_exact(m, k, units_inch ? 25400 : 1000, out_um);
}

/* ---- geometry --------------------------------------------------------- */

/* a/b < c/d with b, d > 0; magnitudes bounded so products fit. */
static int mfg_rat_lt(int64_t a, int64_t b, int64_t c, int64_t d)
{
    return a * d < c * b;
}

static int mfg_box_ok(const mfg_box_t *b)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (!mfg_in_coord(b->lo[i]) || !mfg_in_coord(b->hi[i])) return 0;
        if (b->lo[i] > b->hi[i]) return 0;
    }
    return b->kind == MFG_BOX_FIXTURE || b->kind == MFG_BOX_KEEPOUT;
}

static int mfg_seg_box(const int64_t a[3], const int64_t b[3], const int64_t lo[3],
                       const int64_t hi[3])
{
    /* t in [tl_n/tl_d, th_n/th_d], start [0, 1] */
    int64_t tl_n = 0, tl_d = 1, th_n = 1, th_d = 1;
    int i;
    for (i = 0; i < 3; i++) {
        int64_t d = b[i] - a[i];
        int64_t en_n, ex_n, den;
        if (d == 0) {
            if (a[i] < lo[i] || a[i] > hi[i]) return 0;
            continue;
        }
        if (d > 0) {
            en_n = lo[i] - a[i];
            ex_n = hi[i] - a[i];
            den = d;
        } else {
            en_n = a[i] - hi[i];
            ex_n = a[i] - lo[i];
            den = -d;
        }
        if (mfg_rat_lt(tl_n, tl_d, en_n, den)) {
            tl_n = en_n;
            tl_d = den;
        }
        if (mfg_rat_lt(ex_n, den, th_n, th_d)) {
            th_n = ex_n;
            th_d = den;
        }
        if (mfg_rat_lt(th_n, th_d, tl_n, tl_d)) return 0;
    }
    return 1;
}

int mfg_gc_segment_hits_box(const int64_t a[3], const int64_t b[3], const mfg_box_t *box)
{
    int i;
    if (!a || !b || !box) return MFG_ERR_NULL;
    for (i = 0; i < 3; i++)
        if (!mfg_in_coord(a[i]) || !mfg_in_coord(b[i])) return MFG_ERR_RANGE;
    if (!mfg_box_ok(box)) return MFG_ERR_RANGE;
    return mfg_seg_box(a, b, box->lo, box->hi);
}

static int mfg_pt_in(const int64_t p[3], const int64_t lo[3], const int64_t hi[3])
{
    int i;
    for (i = 0; i < 3; i++)
        if (p[i] < lo[i] || p[i] > hi[i]) return 0;
    return 1;
}

/* closed boxes overlap */
static int mfg_box_overlap(const int64_t alo[3], const int64_t ahi[3], const int64_t blo[3],
                           const int64_t bhi[3])
{
    int i;
    for (i = 0; i < 3; i++)
        if (ahi[i] < blo[i] || bhi[i] < alo[i]) return 0;
    return 1;
}

/* a inside b */
static int mfg_box_inside(const int64_t alo[3], const int64_t ahi[3], const int64_t blo[3],
                          const int64_t bhi[3])
{
    int i;
    for (i = 0; i < 3; i++)
        if (alo[i] < blo[i] || ahi[i] > bhi[i]) return 0;
    return 1;
}

/* Vectors here are bounded by 1.5e9, so each product is < 2.3e18 and a sum
 * or difference of two is < 4.6e18: no overflow. */
static int64_t mfg_cross(const int64_t u[2], const int64_t w[2])
{
    return u[0] * w[1] - u[1] * w[0];
}

static int64_t mfg_dot(const int64_t u[2], const int64_t w[2])
{
    return u[0] * w[0] + u[1] * w[1];
}

/* 0 if angle of u measured CCW from a is in [0, pi), else 1 */
static int mfg_half(const int64_t a[2], const int64_t u[2])
{
    int64_t cr = mfg_cross(a, u);
    return (cr > 0 || (cr == 0 && mfg_dot(a, u) > 0)) ? 0 : 1;
}

/* angle(a -> u) <= angle(a -> w), CCW, in [0, 2pi) */
static int mfg_angle_le(const int64_t a[2], const int64_t u[2], const int64_t w[2])
{
    int hu = mfg_half(a, u), hw = mfg_half(a, w);
    if (hu != hw) return hu < hw;
    return mfg_cross(u, w) >= 0;
}

/* ---- checker state ---------------------------------------------------- */

typedef struct {
    mfg_gc_result_t *res;
    const mfg_gc_profile_t *p;
    int64_t pos[3];
    int motion; /* -1 unset, else 0..3 */
    int inch;   /* 0 mm, 1 inch */
    int units_set;
    int incr;
    int dist_set;
    int modal_warned;
    int64_t feed; /* um/min */
    int feed_set;
    int64_t rpm;
    int spindle_on;
    int ended;
    int after_end_warned;
    int stop;
} mfg_gc_state_t;

typedef struct {
    int64_t nword;
    int gmotion, gunits, gdist, g17;
    int mspin, mend;
    int has[NVAL];
    int64_t mant[NVAL];
    uint32_t frac[NVAL];
} mfg_gc_line_t;

static void mfg_issue(mfg_gc_state_t *st, uint32_t line, int64_t nword, int code, int sev, int box,
                      const char *msg)
{
    mfg_gc_result_t *r = st->res;
    if (sev > r->verdict) r->verdict = (uint8_t) sev;
    if (r->nissue < MFG_GC_MAX_ISSUES) {
        mfg_gc_issue_t *is = &r->issue[r->nissue++];
        is->line = line;
        is->nword = nword;
        is->code = (uint8_t) code;
        is->severity = (uint8_t) sev;
        is->box = (uint8_t) box;
        is->message = msg;
    } else {
        r->dropped++;
    }
}

mfg_status_t mfg_gc_profile_check(const mfg_gc_profile_t *p)
{
    uint32_t i;
    if (!p) return MFG_ERR_NULL;
    for (i = 0; i < 3; i++) {
        if (!mfg_in_coord(p->env_lo[i]) || !mfg_in_coord(p->env_hi[i])) return MFG_ERR_RANGE;
        if (p->env_lo[i] > p->env_hi[i]) return MFG_ERR_RANGE;
        if (p->start[i] < p->env_lo[i] || p->start[i] > p->env_hi[i]) return MFG_ERR_RANGE;
    }
    if (!mfg_in_coord(p->safe_z)) return MFG_ERR_RANGE;
    if (p->feed_min_cut < 1 || p->feed_max < p->feed_min_cut || p->feed_max > MFG_GC_FEED_MAX)
        return MFG_ERR_RANGE;
    if (p->spindle_min < 1 || p->spindle_max < p->spindle_min || p->spindle_max > MFG_GC_RPM_MAX)
        return MFG_ERR_RANGE;
    if (p->arc_r2_tol < 0) return MFG_ERR_RANGE;
    if (p->nbox > MFG_GC_MAX_BOXES) return MFG_ERR_CAPACITY;
    for (i = 0; i < p->nbox; i++)
        if (!mfg_box_ok(&p->box[i])) return MFG_ERR_RANGE;
    return MFG_OK;
}

static int mfg_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

static int mfg_is_num_char(char c)
{
    return (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-';
}

/* integer code for G/M/N words: no fraction, not negative */
static int mfg_int_code(int64_t m, uint32_t k, int64_t *out)
{
    if (k != 0 || m < 0) return 0;
    *out = m;
    return 1;
}

/* Scan one line into words. Returns 1 if the line may execute. */
static int mfg_scan_line(mfg_gc_state_t *st, const char *s, size_t len, uint32_t ln,
                         mfg_gc_line_t *w, int *any)
{
    size_t i = 0;
    int nwords = 0, v;
    w->nword = -1;
    w->gmotion = w->gunits = w->gdist = -1;
    w->g17 = 0;
    w->mspin = w->mend = -1;
    for (v = 0; v < NVAL; v++) {
        w->has[v] = 0;
        w->mant[v] = 0;
        w->frac[v] = 0;
    }
    *any = 0;
    /* a tape delimiter line holding only '%' */
    {
        size_t j = 0, pc = 0, other = 0;
        for (j = 0; j < len; j++) {
            if (s[j] == '%')
                pc++;
            else if (s[j] != ' ' && s[j] != '\t')
                other++;
        }
        if (pc == 1 && other == 0) return 1;
    }
    while (i < len) {
        char c = s[i];
        int L;
        size_t j;
        int64_t m, code;
        uint32_t k;
        mfg_status_t ps;
        if (c == ' ' || c == '\t') {
            i++;
            continue;
        }
        if (c == ';') break;
        if (c == '(') {
            for (j = i + 1; j < len && s[j] != ')'; j++) {
                if (s[j] == '(') {
                    mfg_issue(st, ln, w->nword, MFG_ISS_MALFORMED, MFG_REJECT, 0, "nested comment");
                    return 0;
                }
            }
            if (j >= len) {
                mfg_issue(st, ln, w->nword, MFG_ISS_MALFORMED, MFG_REJECT, 0,
                          "unterminated comment");
                return 0;
            }
            i = j + 1;
            continue;
        }
        L = mfg_upper(c);
        if (L < 'A' || L > 'Z') {
            mfg_issue(st, ln, w->nword, MFG_ISS_MALFORMED, MFG_REJECT, 0, "unexpected character");
            return 0;
        }
        *any = 1;
        for (j = i + 1; j < len && mfg_is_num_char(s[j]); j++) {
        }
        ps = mfg_parse_dec(s + i + 1, j - i - 1, &m, &k);
        if (ps == MFG_ERR_STATE) {
            mfg_issue(st, ln, w->nword, MFG_ISS_TOO_PRECISE, MFG_REJECT, 0,
                      "more than 9 fraction digits");
            return 0;
        }
        if (ps != MFG_OK) {
            mfg_issue(st, ln, w->nword, MFG_ISS_MALFORMED, MFG_REJECT, 0,
                      ps == MFG_ERR_OVERFLOW ? "number too large" : "malformed number");
            return 0;
        }
        i = j;
        switch (L) {
        case 'N':
            if (nwords != 0 || !mfg_int_code(m, k, &code)) {
                mfg_issue(st, ln, w->nword, MFG_ISS_MALFORMED, MFG_REJECT, 0,
                          "N must be the first word and a whole number");
                return 0;
            }
            w->nword = code;
            break;
        case 'G':
            if (!mfg_int_code(m, k, &code)) {
                mfg_issue(st, ln, w->nword, MFG_ISS_UNKNOWN_WORD, MFG_REJECT, 0,
                          "unsupported G code");
                return 0;
            }
            if (code <= 3) {
                if (w->gmotion >= 0) goto conflict;
                w->gmotion = (int) code;
            } else if (code == 17) {
                w->g17 = 1;
            } else if (code == 20 || code == 21) {
                if (w->gunits >= 0) goto conflict;
                w->gunits = (int) code;
            } else if (code == 90 || code == 91) {
                if (w->gdist >= 0) goto conflict;
                w->gdist = (int) code;
            } else {
                mfg_issue(st, ln, w->nword, MFG_ISS_UNKNOWN_WORD, MFG_REJECT, 0,
                          "unsupported G code");
                return 0;
            }
            break;
        case 'M':
            if (!mfg_int_code(m, k, &code)) {
                mfg_issue(st, ln, w->nword, MFG_ISS_UNKNOWN_WORD, MFG_REJECT, 0,
                          "unsupported M code");
                return 0;
            }
            if (code == 3 || code == 4 || code == 5) {
                if (w->mspin >= 0) goto conflict;
                w->mspin = (int) code;
            } else if (code == 2 || code == 30) {
                if (w->mend >= 0) goto conflict;
                w->mend = (int) code;
            } else {
                mfg_issue(st, ln, w->nword, MFG_ISS_UNKNOWN_WORD, MFG_REJECT, 0,
                          "unsupported M code");
                return 0;
            }
            break;
        case 'X':
        case 'Y':
        case 'Z':
        case 'I':
        case 'J':
        case 'F':
        case 'S':
            v = L == 'X'   ? AX_X
                : L == 'Y' ? AX_Y
                : L == 'Z' ? AX_Z
                : L == 'I' ? AX_I
                : L == 'J' ? AX_J
                : L == 'F' ? AX_F
                           : AX_S;
            if (w->has[v]) goto conflict;
            w->has[v] = 1;
            w->mant[v] = m;
            w->frac[v] = k;
            break;
        default:
            mfg_issue(st, ln, w->nword, MFG_ISS_UNKNOWN_WORD, MFG_REJECT, 0,
                      "unsupported word letter");
            return 0;
        }
        nwords++;
    }
    return 1;
conflict:
    mfg_issue(st, ln, w->nword, MFG_ISS_CONFLICT, MFG_REJECT, 0,
              "two words of the same group on one line");
    return 0;
}

static void mfg_check_feed(mfg_gc_state_t *st, uint32_t ln, int64_t nw)
{
    if (!st->feed_set)
        mfg_issue(st, ln, nw, MFG_ISS_FEED_UNSET, MFG_REJECT, 0, "cutting move with no feed set");
    else if (st->feed > st->p->feed_max)
        mfg_issue(st, ln, nw, MFG_ISS_FEED_HIGH, MFG_REJECT, 0, "feed above machine maximum");
    else if (st->feed < st->p->feed_min_cut)
        mfg_issue(st, ln, nw, MFG_ISS_FEED_LOW, MFG_REJECT, 0, "feed below cutting minimum");
    if (!st->spindle_on)
        mfg_issue(st, ln, nw, MFG_ISS_CUT_SPINDLE_OFF, MFG_REJECT, 0,
                  "cutting move with spindle off");
}

static void mfg_check_rpm(mfg_gc_state_t *st, uint32_t ln, int64_t nw)
{
    if (st->rpm < st->p->spindle_min || st->rpm > st->p->spindle_max)
        mfg_issue(st, ln, nw, MFG_ISS_SPINDLE_RANGE, MFG_REJECT, 0,
                  "spindle speed outside machine range");
}

static int mfg_env_out(const mfg_gc_profile_t *p, const int64_t q[3])
{
    return !mfg_pt_in(q, p->env_lo, p->env_hi);
}

static void mfg_check_linear(mfg_gc_state_t *st, uint32_t ln, int64_t nw, const int64_t to[3])
{
    const mfg_gc_profile_t *p = st->p;
    uint32_t b;
    if (st->motion == 0) {
        /* a rapid may retract straight up out of the zone below safe_z,
         * but may not move sideways in it or descend into it */
        int sideways = st->pos[0] != to[0] || st->pos[1] != to[1];
        if ((sideways && mfg_min64(st->pos[2], to[2]) < p->safe_z) ||
            (to[2] < st->pos[2] && to[2] < p->safe_z))
            mfg_issue(st, ln, nw, MFG_ISS_RAPID_LOW, MFG_REJECT, 0, "rapid move below safe Z");
    } else {
        mfg_check_feed(st, ln, nw);
    }
    for (b = 0; b < p->nbox; b++) {
        const mfg_box_t *bx = &p->box[b];
        if (bx->kind == MFG_BOX_KEEPOUT && st->motion != 0) continue;
        if (mfg_seg_box(st->pos, to, bx->lo, bx->hi))
            mfg_issue(st, ln, nw,
                      bx->kind == MFG_BOX_KEEPOUT ? MFG_ISS_RAPID_KEEPOUT : MFG_ISS_FIXTURE,
                      MFG_REJECT, (int) b,
                      bx->kind == MFG_BOX_KEEPOUT ? "rapid move enters keep-out box"
                                                  : "move touches fixture box");
    }
    if (mfg_env_out(p, to))
        mfg_issue(st, ln, nw, MFG_ISS_ENVELOPE, MFG_REJECT, 0, "end point outside travel envelope");
}

static void mfg_check_arc(mfg_gc_state_t *st, uint32_t ln, int64_t nw, const int64_t to[3],
                          int64_t ci, int64_t cj)
{
    const mfg_gc_profile_t *p = st->p;
    int64_t c[2], v0[2], v1[2], r0, r1, diff, rlo, rhi, olo[3], ohi[3];
    int64_t a[2], bb[2];
    static const int64_t dirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    int incl[4], full, k, env_def, env_maybe;
    uint64_t sq;
    uint32_t b;

    mfg_check_feed(st, ln, nw);
    if (ci == 0 && cj == 0) {
        mfg_issue(st, ln, nw, MFG_ISS_ARC_CENTRE, MFG_REJECT, 0, "arc with zero centre offset");
        if (mfg_env_out(p, to))
            mfg_issue(st, ln, nw, MFG_ISS_ENVELOPE, MFG_REJECT, 0,
                      "end point outside travel envelope");
        return;
    }
    c[0] = st->pos[0] + ci;
    c[1] = st->pos[1] + cj;
    v0[0] = -ci;
    v0[1] = -cj;
    v1[0] = to[0] - c[0];
    v1[1] = to[1] - c[1];
    r0 = v0[0] * v0[0] + v0[1] * v0[1];
    r1 = v1[0] * v1[0] + v1[1] * v1[1];
    diff = r0 > r1 ? r0 - r1 : r1 - r0;
    if (diff > p->arc_r2_tol) {
        mfg_issue(st, ln, nw, MFG_ISS_ARC_RADIUS, MFG_REJECT, 0, "arc start and end radius differ");
        if (mfg_env_out(p, to))
            mfg_issue(st, ln, nw, MFG_ISS_ENVELOPE, MFG_REJECT, 0,
                      "end point outside travel envelope");
        return;
    }
    sq = mfg_isqrt_u64((uint64_t) mfg_max64(r0, r1));
    rhi = (int64_t) sq + ((int64_t) (sq * sq) != mfg_max64(r0, r1) ? 1 : 0);
    rlo = (int64_t) mfg_isqrt_u64((uint64_t) mfg_min64(r0, r1));
    /* CCW sweep from a to bb */
    if (st->motion == 3) {
        a[0] = v0[0], a[1] = v0[1], bb[0] = v1[0], bb[1] = v1[1];
    } else {
        a[0] = v1[0], a[1] = v1[1], bb[0] = v0[0], bb[1] = v0[1];
    }
    full = (to[0] == st->pos[0] && to[1] == st->pos[1]) ||
           (mfg_cross(v0, v1) == 0 && mfg_dot(v0, v1) > 0);
    for (k = 0; k < 4; k++) incl[k] = full || mfg_angle_le(a, dirs[k], bb);

    for (k = 0; k < 3; k++) {
        olo[k] = mfg_min64(st->pos[k], to[k]);
        ohi[k] = mfg_max64(st->pos[k], to[k]);
    }
    if (incl[0]) ohi[0] = mfg_max64(ohi[0], c[0] + rhi);
    if (incl[1]) ohi[1] = mfg_max64(ohi[1], c[1] + rhi);
    if (incl[2]) olo[0] = mfg_min64(olo[0], c[0] - rhi);
    if (incl[3]) olo[1] = mfg_min64(olo[1], c[1] - rhi);

    /* Each included axis extreme lies, exactly, inside a small "candidate"
     * box: radius in [rlo, rhi] along that direction, z in the move's
     * z range. A box containing a whole candidate box is definitely hit. */
    env_def = mfg_env_out(p, to);
    env_maybe = !mfg_box_inside(olo, ohi, p->env_lo, p->env_hi);
    for (b = 0; b <= p->nbox; b++) {
        const mfg_box_t *bx = b < p->nbox ? &p->box[b] : 0;
        int def = 0;
        if (bx && bx->kind != MFG_BOX_FIXTURE) continue;
        if (bx && mfg_pt_in(to, bx->lo, bx->hi)) def = 1;
        for (k = 0; k < 4; k++) {
            int64_t clo[3], chi[3];
            if (!incl[k]) continue;
            clo[2] = olo[2];
            chi[2] = ohi[2];
            if (k == 0 || k == 2) {
                int64_t e1 = c[0] + (k == 0 ? rlo : -rlo), e2 = c[0] + (k == 0 ? rhi : -rhi);
                clo[0] = mfg_min64(e1, e2);
                chi[0] = mfg_max64(e1, e2);
                clo[1] = chi[1] = c[1];
            } else {
                int64_t e1 = c[1] + (k == 1 ? rlo : -rlo), e2 = c[1] + (k == 1 ? rhi : -rhi);
                clo[1] = mfg_min64(e1, e2);
                chi[1] = mfg_max64(e1, e2);
                clo[0] = chi[0] = c[0];
            }
            if (bx) {
                if (mfg_box_inside(clo, chi, bx->lo, bx->hi)) def = 1;
            } else if (!mfg_box_overlap(clo, chi, p->env_lo, p->env_hi)) {
                env_def = 1;
            }
        }
        if (!bx) break;
        if (def)
            mfg_issue(st, ln, nw, MFG_ISS_FIXTURE, MFG_REJECT, (int) b, "arc touches fixture box");
        else if (mfg_box_overlap(olo, ohi, bx->lo, bx->hi))
            mfg_issue(st, ln, nw, MFG_ISS_FIXTURE_MAYBE, MFG_HOLD, (int) b,
                      "arc bounding box overlaps fixture: person to check");
    }
    if (env_def)
        mfg_issue(st, ln, nw, MFG_ISS_ENVELOPE, MFG_REJECT, 0, "arc leaves travel envelope");
    else if (env_maybe)
        mfg_issue(st, ln, nw, MFG_ISS_ENVELOPE_MAYBE, MFG_HOLD, 0,
                  "arc bounding box leaves envelope: person to check");
}

static void mfg_exec_line(mfg_gc_state_t *st, const mfg_gc_line_t *w, uint32_t ln)
{
    int64_t nw = w->nword, val[NVAL], to[3];
    int v, axes;
    int64_t mult;

    if (w->gunits >= 0) {
        st->inch = w->gunits == 20;
        st->units_set = 1;
    }
    if (w->gdist >= 0) {
        st->incr = w->gdist == 91;
        st->dist_set = 1;
    }
    mult = st->inch ? 25400 : 1000;
    for (v = 0; v < NVAL; v++) {
        mfg_status_t s;
        val[v] = 0;
        if (!w->has[v]) continue;
        if (v == AX_S) {
            if (w->frac[v] != 0 || w->mant[v] < 0) {
                mfg_issue(st, ln, nw, MFG_ISS_MALFORMED, MFG_REJECT, 0,
                          "spindle speed must be a whole non-negative rpm");
                return;
            }
            val[v] = w->mant[v];
            continue;
        }
        s = mfg_scale_exact(w->mant[v], w->frac[v], mult, &val[v]);
        if (s == MFG_ERR_STATE) {
            mfg_issue(st, ln, nw, MFG_ISS_TOO_PRECISE, MFG_REJECT, 0,
                      "value is not a whole number of micrometres");
            return;
        }
        if (s != MFG_OK || (v != AX_F && !mfg_in_coord(val[v]))) {
            mfg_issue(st, ln, nw, MFG_ISS_COORD_RANGE, MFG_REJECT, 0,
                      "value beyond coordinate range: checking stopped");
            st->stop = 1;
            return;
        }
        if (v == AX_F && val[v] < 0) {
            mfg_issue(st, ln, nw, MFG_ISS_MALFORMED, MFG_REJECT, 0, "negative feed");
            return;
        }
    }
    if (w->has[AX_F]) {
        st->feed = val[AX_F];
        st->feed_set = 1;
    }
    if (w->has[AX_S]) {
        st->rpm = val[AX_S];
        if (st->spindle_on && w->mspin < 0) mfg_check_rpm(st, ln, nw);
    }
    if (w->mspin == 3 || w->mspin == 4) {
        st->spindle_on = 1;
        mfg_check_rpm(st, ln, nw);
    } else if (w->mspin == 5) {
        st->spindle_on = 0;
    }
    if (w->gmotion >= 0) st->motion = w->gmotion;
    axes = w->has[AX_X] || w->has[AX_Y] || w->has[AX_Z] || w->has[AX_I] || w->has[AX_J];
    if (axes) {
        if (st->motion < 0) {
            mfg_issue(st, ln, nw, MFG_ISS_MOTION_UNSET, MFG_REJECT, 0,
                      "axis words with no motion mode (G0/G1/G2/G3)");
            goto end;
        }
        if ((w->has[AX_I] || w->has[AX_J]) && st->motion < 2) {
            mfg_issue(st, ln, nw, MFG_ISS_MALFORMED, MFG_REJECT, 0, "I/J given on a linear move");
            goto end;
        }
        if (st->motion >= 2 && !w->has[AX_I] && !w->has[AX_J]) {
            mfg_issue(st, ln, nw, MFG_ISS_ARC_CENTRE, MFG_REJECT, 0,
                      "arc without I/J centre offset");
            goto end;
        }
        if ((!st->units_set || !st->dist_set) && !st->modal_warned) {
            mfg_issue(st, ln, nw, MFG_ISS_MODAL_UNSET, MFG_HOLD, 0,
                      "units (G20/G21) or distance mode (G90/G91) not declared");
            st->modal_warned = 1;
        }
        for (v = 0; v < 3; v++) {
            to[v] = st->pos[v];
            if (!w->has[v]) continue;
            to[v] = st->incr ? st->pos[v] + val[v] : val[v];
            if (!mfg_in_coord(to[v])) {
                mfg_issue(st, ln, nw, MFG_ISS_COORD_RANGE, MFG_REJECT, 0,
                          "position beyond coordinate range: checking stopped");
                st->stop = 1;
                return;
            }
        }
        st->res->moves++;
        if (st->motion <= 1)
            mfg_check_linear(st, ln, nw, to);
        else
            mfg_check_arc(st, ln, nw, to, val[AX_I], val[AX_J]);
        for (v = 0; v < 3; v++) st->pos[v] = to[v];
    }
end:
    if (w->mend >= 0) st->ended = 1;
}

mfg_status_t mfg_gc_check(const char *text, size_t len, const mfg_gc_profile_t *p,
                          mfg_gc_result_t *out)
{
    mfg_gc_state_t st;
    mfg_gc_line_t w;
    size_t i = 0;
    uint32_t ln = 0;
    mfg_status_t ps;
    int v;

    if (!p || !out || (!text && len != 0)) return MFG_ERR_NULL;
    if (len > (size_t) MFG_GC_MAX_LINES * (MFG_GC_MAX_LINE + 2u)) return MFG_ERR_RANGE;
    ps = mfg_gc_profile_check(p);
    if (ps != MFG_OK) return ps;

    out->verdict = MFG_RELEASE_OK;
    out->nissue = 0;
    out->dropped = 0;
    out->lines = 0;
    out->moves = 0;
    st.res = out;
    st.p = p;
    for (v = 0; v < 3; v++) st.pos[v] = p->start[v];
    st.motion = -1;
    st.inch = 0;
    st.units_set = 0;
    st.incr = 0;
    st.dist_set = 0;
    st.modal_warned = 0;
    st.feed = 0;
    st.feed_set = 0;
    st.rpm = 0;
    st.spindle_on = 0;
    st.ended = 0;
    st.after_end_warned = 0;
    st.stop = 0;

    while (i < len && !st.stop) {
        size_t s0 = i, e;
        int any, has_nul = 0;
        while (i < len && text[i] != '\n') {
            if (text[i] == '\0') has_nul = 1;
            i++;
        }
        e = i;
        if (i < len) i++; /* skip '\n' */
        if (e > s0 && text[e - 1] == '\r') e--;
        ln++;
        if (ln > MFG_GC_MAX_LINES) {
            mfg_issue(&st, ln, -1, MFG_ISS_TOO_MANY_LINES, MFG_REJECT, 0,
                      "too many lines: checking stopped");
            break;
        }
        out->lines = ln;
        if (e - s0 > MFG_GC_MAX_LINE) {
            mfg_issue(&st, ln, -1, MFG_ISS_LINE_TOO_LONG, MFG_REJECT, 0, "line too long");
            continue;
        }
        if (has_nul) {
            mfg_issue(&st, ln, -1, MFG_ISS_MALFORMED, MFG_REJECT, 0, "NUL byte in program");
            continue;
        }
        if (!mfg_scan_line(&st, text + s0, e - s0, ln, &w, &any)) continue;
        if (!any) continue;
        if (st.ended) {
            if (!st.after_end_warned)
                mfg_issue(&st, ln, w.nword, MFG_ISS_AFTER_END, MFG_HOLD, 0,
                          "words after program end (M2/M30)");
            st.after_end_warned = 1;
            continue;
        }
        mfg_exec_line(&st, &w, ln);
    }
    if (!st.ended && !st.stop)
        mfg_issue(&st, 0, -1, MFG_ISS_NO_END, MFG_HOLD, 0, "program has no M2/M30 end");
    for (v = 0; v < 3; v++) out->end_pos[v] = st.pos[v];
    return MFG_OK;
}
