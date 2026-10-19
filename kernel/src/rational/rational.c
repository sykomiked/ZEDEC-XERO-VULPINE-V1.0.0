/* rational.c — WyvernEye exact rational arithmetic. See rational.h. */
#include "rational.h"
#include "../../include/zxv_fixed.h"

/* 128-bit intermediates let every operation detect overflow before it
 * happens. Every 64-bit target this kernel builds for provides them.
 *
 * 32-bit targets (arm32, riscv32) have no __int128. There the "narrow"
 * path below builds the few 128-bit steps it needs from two 64-bit limbs
 * and checks every other step for overflow. It returns exactly what the
 * wide path returns, including refusing the same results, so a value is
 * never wrapped silently on any target. (Before 2026-10-12 this fallback
 * did plain 64-bit arithmetic with no checks at all.)
 *
 * Define RAT_NO_WIDE to force the narrow path on a 64-bit host, so it can
 * be tested there (src/rational/test_rational_narrow.c, run by verify-all). */
#if defined(__SIZEOF_INT128__) && !defined(RAT_NO_WIDE)
typedef __int128 wide_t;
#define HAVE_WIDE 1
#else
#    define HAVE_WIDE 0
#endif

static const int64_t I64_MAX = 0x7FFFFFFFFFFFFFFFLL;

static rat_t invalid(void) { rat_t r; r.num = 0; r.den = 1; r.valid = false; return r; }

static int64_t igcd(int64_t a, int64_t b) {
    int64_t g = zx_gcd_i64(a, b);
    return g ? g : 1;
}

#if !HAVE_WIDE
/* An unsigned 128-bit magnitude as two 64-bit limbs. No helper below wraps
 * or shifts out a set bit, so all of it stays clean under -fsanitize=integer. */
typedef struct {
    uint64_t hi, lo;
} u128n;

static bool mul_ovf(int64_t a, int64_t b, int64_t *r)
{
    return __builtin_mul_overflow(a, b, r);
}
static bool add_ovf(int64_t a, int64_t b, int64_t *r)
{
    return __builtin_add_overflow(a, b, r);
}

/* |v| as unsigned; correct for INT64_MIN too (2^63) */
static uint64_t umag(int64_t v)
{
    return v < 0 ? (uint64_t) (-(v + 1)) + 1u : (uint64_t) v;
}

/* Full 64x64 -> 128-bit unsigned product, built from 32-bit halves. */
static u128n mulu_wide(uint64_t a, uint64_t b)
{
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32;
    uint64_t b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    u128n r;
    r.lo = (p00 & 0xFFFFFFFFu) | ((mid & 0xFFFFFFFFu) << 32);
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return r;
}

static int u128_cmp(u128n a, u128n b)
{
    if (a.hi != b.hi) return a.hi < b.hi ? -1 : 1;
    return a.lo < b.lo ? -1 : a.lo > b.lo ? 1 : 0;
}
/* a + b; callers keep both below 2^127, so the sum cannot leave 128 bits */
static u128n u128_add(u128n a, u128n b)
{
    u128n r;
    if (a.lo > UINT64_MAX - b.lo) {
        r.lo = a.lo - (UINT64_MAX - b.lo) - 1u;
        r.hi = a.hi + b.hi + 1u;
    } else {
        r.lo = a.lo + b.lo;
        r.hi = a.hi + b.hi;
    }
    return r;
}
/* a - b; requires a >= b */
static u128n u128_sub(u128n a, u128n b)
{
    u128n r;
    if (a.lo >= b.lo) {
        r.lo = a.lo - b.lo;
        r.hi = a.hi - b.hi;
    } else {
        r.lo = (UINT64_MAX - b.lo) + a.lo + 1u;
        r.hi = a.hi - b.hi - 1u;
    }
    return r;
}

/* (hi:lo) / d by shift-and-subtract. Requires d <= 2^63-1 (every caller
 * passes a positive int64) and hi < d, which keeps hi below 2^63 so the
 * shift never drops a bit, and makes the quotient fit in 64 bits. */
static uint64_t divu_wide(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0;
    for (int i = 0; i < 64; i++) {
        hi = (hi << 1) | (lo >> 63);
        lo = (lo & 0x7FFFFFFFFFFFFFFFULL) << 1;
        q <<= 1; /* q < 2^63 here: at most i < 64 bits */
        if (hi >= d) {
            hi -= d;
            q |= 1;
        }
    }
    *rem = hi;
    return q;
}
#endif

/* Reduce to lowest terms with den > 0. Keeping values reduced after every
 * operation is what stops denominators exploding over a long column of
 * sums — the practical reason naive rational arithmetic gets a bad name. */
#if HAVE_WIDE
static rat_t reduce(wide_t num, wide_t den) {
    if (den == 0) return invalid();
    if (num == 0) { rat_t r; r.num = 0; r.den = 1; r.valid = true; return r; }
    if (den < 0) { num = -num; den = -den; }

    /* reduce in 128-bit, then check the result fits in 64 */
    wide_t a = num < 0 ? -num : num, b = den;
    while (b) { wide_t t = a % b; a = b; b = t; }
    wide_t g = a ? a : 1;
    num /= g; den /= g;
    if (num > (wide_t)I64_MAX || num < -(wide_t)I64_MAX) return invalid();
    if (den > (wide_t) I64_MAX) return invalid();
    rat_t r; r.num = (int64_t)num; r.den = (int64_t)den; r.valid = true;
    return r;
}
#else
static rat_t reduce(int64_t num, int64_t den)
{
    if (den == 0) return invalid();
    if (num == 0) {
        rat_t r;
        r.num = 0;
        r.den = 1;
        r.valid = true;
        return r;
    }
    /* reduce the magnitudes as unsigned, so INT64_MIN (2^63) reduces like
     * any other value, then check the result fits, as the wide path does */
    bool neg = (num < 0) != (den < 0);
    uint64_t n = umag(num), d = umag(den), a = n, b = d;
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    n /= a;
    d /= a;
    if (n > (uint64_t) I64_MAX || d > (uint64_t) I64_MAX) return invalid();
    rat_t r;
    r.num = neg ? -(int64_t) n : (int64_t) n;
    r.den = (int64_t) d;
    r.valid = true;
    return r;
}
#endif

rat_t rat_zero(void) { return rat_from_int(0); }

rat_t rat_from_int(int64_t v) {
    /* reduce() rejects num < -(2^63-1), so admitting INT64_MIN here would
     * create a "valid" rat_t that violates the invariant every other
     * function relies on — and every later negation of it is signed
     * overflow (UB), with rat_neg and rat_abs silently returning a
     * negative. Reject it at the door instead. */
    if (v == -I64_MAX - 1) return invalid();
    rat_t r; r.num = v; r.den = 1; r.valid = true; return r;
}

rat_t rat_make(int64_t num, int64_t den) {
    if (den == 0) return invalid();
    /* Unsigned magnitudes admit reducible INT64_MIN inputs without ever
     * negating INT64_MIN, including on targets with no wider integer type. */
    uint64_t n = num < 0 ? 0u - (uint64_t) num : (uint64_t) num;
    uint64_t d = den < 0 ? 0u - (uint64_t) den : (uint64_t) den;
    uint64_t a = n, b = d;
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    n /= a;
    d /= a;
    if (n > (uint64_t) I64_MAX || d > (uint64_t) I64_MAX) return invalid();
    rat_t r = {(int64_t) n, (int64_t) d, true};
    if ((num < 0) != (den < 0)) r.num = -r.num;
    return r;
#if HAVE_WIDE
    return reduce((wide_t)num, (wide_t)den);
#else
    return reduce(num, den);
#endif
}

/* ---- arithmetic ----
 * Cross-reducing before multiplying keeps the intermediates small, which
 * matters because it is the difference between a column of a thousand
 * sums staying representable and overflowing. */
rat_t rat_add(rat_t a, rat_t b) {
    if (!a.valid || !b.valid) return invalid();
    int64_t g = igcd(a.den, b.den);
    wide_t lcm_den, left, right, num;
    if (__builtin_mul_overflow((wide_t) (a.den / g), (wide_t) b.den, &lcm_den) ||
        __builtin_mul_overflow((wide_t) a.num, (wide_t) (b.den / g), &left) ||
        __builtin_mul_overflow((wide_t) b.num, (wide_t) (a.den / g), &right) ||
        __builtin_add_overflow(left, right, &num))
        return invalid();
    /* The narrow backend deliberately refuses an unrepresentable intermediate. */
    if (!HAVE_WIDE && (num == -I64_MAX - 1)) return invalid();
#if HAVE_WIDE
    wide_t lcm_den = (wide_t)(a.den / g) * (wide_t)b.den;
    wide_t num = (wide_t)a.num * (wide_t)(b.den / g)
               + (wide_t)b.num * (wide_t)(a.den / g);
    return reduce(num, lcm_den);
#else
    /* Knuth's method (TAOCP 4.5.1): t = a.num*(b.den/g) + b.num*(a.den/g)
     * shares no factor with the denominators except possibly one of g, so
     * dividing by g2 = gcd(t, g) leaves the sum fully reduced. t needs up
     * to 128 bits; it is kept as sign + two-limb magnitude. */
    u128n p = mulu_wide(umag(a.num), (uint64_t) (b.den / g));
    u128n q = mulu_wide(umag(b.num), (uint64_t) (a.den / g));
    u128n t;
    bool tneg;
    if ((a.num < 0) == (b.num < 0)) {
        t = u128_add(p, q);
        tneg = a.num < 0;
    } else if (u128_cmp(p, q) >= 0) {
        t = u128_sub(p, q);
        tneg = a.num < 0;
    } else {
        t = u128_sub(q, p);
        tneg = b.num < 0;
    }
    if (t.hi == 0 && t.lo == 0) return rat_zero();
    uint64_t tmod, rem;
    divu_wide(t.hi % (uint64_t) g, t.lo, (uint64_t) g, &tmod);
    int64_t g2 = igcd((int64_t) tmod, g);
    if (t.hi >= (uint64_t) g2) return invalid(); /* |t|/g2 >= 2^64 */
    uint64_t n = divu_wide(t.hi, t.lo, (uint64_t) g2, &rem);
    int64_t d;
    if (n > (uint64_t) I64_MAX || mul_ovf(a.den / g, b.den / g2, &d)) return invalid();
    rat_t r;
    r.num = tneg ? -(int64_t) n : (int64_t) n;
    r.den = d;
    r.valid = true;
    return r;
#endif
}

rat_t rat_sub(rat_t a, rat_t b) { return rat_add(a, rat_neg(b)); }

rat_t rat_mul(rat_t a, rat_t b) {
    if (!a.valid || !b.valid) return invalid();
    /* cross-reduce first */
    int64_t g1 = igcd(a.num, b.den);
    int64_t g2 = igcd(b.num, a.den);
    wide_t n, d;
    if (__builtin_mul_overflow((wide_t) (a.num / g1), (wide_t) (b.num / g2), &n) ||
        __builtin_mul_overflow((wide_t) (a.den / g2), (wide_t) (b.den / g1), &d))
        return invalid();
    if (!HAVE_WIDE && (n == -I64_MAX - 1)) return invalid();
#if HAVE_WIDE
    wide_t n = (wide_t)(a.num / g1) * (wide_t)(b.num / g2);
    wide_t d = (wide_t)(a.den / g2) * (wide_t)(b.den / g1);
    return reduce(n, d);
#else
    /* Both inputs are reduced, so after cross-reduction the products share
     * no factor: the result is already in lowest terms, and it fits in 64
     * bits exactly when these checked products do (reduce() then refuses
     * the one in-range product it cannot hold, INT64_MIN). */
    int64_t n, d;
    if (mul_ovf(a.num / g1, b.num / g2, &n) || mul_ovf(a.den / g2, b.den / g1, &d))
        return invalid();
    return reduce(n, d);
#endif
}

rat_t rat_div(rat_t a, rat_t b) {
    if (!a.valid || !b.valid) return invalid();
    if (b.num == 0) return invalid();          /* division by zero */
    rat_t inv; inv.num = b.den; inv.den = b.num; inv.valid = true;
    if (inv.den < 0) { inv.num = -inv.num; inv.den = -inv.den; }
    return rat_mul(a, inv);
}

rat_t rat_neg(rat_t a) {
    if (!a.valid) return invalid();
    rat_t r = a; r.num = -a.num; return r;
}
rat_t rat_abs(rat_t a) {
    if (!a.valid) return invalid();
    rat_t r = a; if (r.num < 0) r.num = -r.num; return r;
}

/* ---- comparison: exact, so no epsilon is ever needed ---- */
bool rat_eq(rat_t a, rat_t b) {
    if (!a.valid || !b.valid) return false;
    return a.num == b.num && a.den == b.den;   /* both are reduced */
}
int rat_cmp(rat_t a, rat_t b) {
    /* Invalids order AFTER every valid value, and equal to each other, so
     * sorting is total and agrees with rat_eq (which is false for any
     * invalid operand). Returning 0 here used to make "invalid == 3"
     * true for cmp while false for eq — a non-transitive mess. */
    if (!a.valid && !b.valid) return 0;
    if (!a.valid) return 1;
    if (!b.valid) return -1;
    /* Continued fractions compare exactly without cross-product overflow,
     * including on ARM32/RV32 where __int128 is unavailable. */
    if (a.num < 0 && b.num >= 0) return -1;
    if (b.num < 0 && a.num >= 0) return 1;
    int direction = a.num < 0 ? -1 : 1;
    uint64_t an = a.num < 0 ? (uint64_t) -a.num : (uint64_t) a.num;
    uint64_t bn = b.num < 0 ? (uint64_t) -b.num : (uint64_t) b.num;
    uint64_t ad = (uint64_t) a.den, bd = (uint64_t) b.den;
    for (;;) {
        uint64_t aq = an / ad, bq = bn / bd;
        if (aq != bq) return aq < bq ? -direction : direction;
        uint64_t ar = an % ad, br = bn % bd;
        if (!ar || !br) return ar == br ? 0 : !ar ? -direction : direction;
        an = ad;
        ad = ar;
        bn = bd;
        bd = br;
        direction = -direction;
    }
#if HAVE_WIDE
    wide_t l = (wide_t)a.num * (wide_t)b.den;
    wide_t r = (wide_t)b.num * (wide_t)a.den;
    return (l < r) ? -1 : (l > r) ? 1 : 0;
#else
    /* exact: compare signs, then the full 128-bit magnitudes */
    int sa = (a.num > 0) - (a.num < 0), sb = (b.num > 0) - (b.num < 0);
    if (sa != sb) return (sa < sb) ? -1 : 1;
    if (sa == 0) return 0;
    int c = u128_cmp(mulu_wide(umag(a.num), (uint64_t) b.den),
                     mulu_wide(umag(b.num), (uint64_t) a.den));
    return sa < 0 ? -c : c;
#endif
}
bool rat_is_zero(rat_t a) { return a.valid && a.num == 0; }
bool rat_is_int(rat_t a)  { return a.valid && a.den == 1; }

/* ---- parsing ---- */
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* A parse that stops early has not understood its input. "1.5x" and
 * "3/4junk" must be rejected, not silently read as 1.5 and 3/4. */
static bool at_end(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return *s == '\0';
}

rat_t rat_from_string(const char *s) {
    if (!s) return invalid();
    while (*s == ' ') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;

    int64_t ip = 0;
    bool any = false;
    while (is_digit(*s)) {
        int64_t d = *s - '0';
        /* digit-exact bound: 9223372036854775807 itself must parse */
        if (ip > (I64_MAX - d) / 10) return invalid();
        ip = ip * 10 + d; s++; any = true;
    }

    if (*s == '/') {                 /* ratio form "3/4" */
        s++;
        int64_t d = 0; bool anyd = false;
        while (is_digit(*s)) {
            int64_t dig = *s - '0';   /* digit-exact bound, matching numerator */
            if (d > (I64_MAX - dig) / 10) return invalid();
            d = d * 10 + dig; s++; anyd = true;
        }
        if (!any || !anyd || d == 0) return invalid();
        if (!at_end(s)) return invalid();      /* trailing garbage is an error */
        return rat_make(sign * ip, d);
    }

    if (*s == '.') {                 /* exact decimal "0.1" -> 1/10 */
        s++;
        int64_t frac = 0, scale = 1;
        while (is_digit(*s)) {
            int64_t d = *s - '0';
            if (scale > I64_MAX / 10 || frac > (I64_MAX - d) / 10) {
                /* Beyond 18 places: trailing zeros drop no information and
                 * stay exact ("0.1000000000000000000000"); a significant
                 * digit out here cannot be represented and must refuse. */
                if (d == 0) { s++; any = true; continue; }
                return invalid();
            }
            frac = frac * 10 + d;
            scale *= 10; s++; any = true;
        }
        if (!any) return invalid();
        if (!at_end(s)) return invalid();      /* trailing garbage is an error */
        /* ip + frac/scale, exactly */
#if HAVE_WIDE
        wide_t num = (wide_t)ip * (wide_t)scale + (wide_t)frac;
        return reduce(sign * num, (wide_t)scale);
#else
        /* reduce frac/scale first: then ip*s + f is already in lowest
         * terms over s, so it overflows only when the answer cannot fit */
        int64_t g = igcd(frac, scale), s2 = scale / g, num;
        if (mul_ovf(ip, s2, &num) || add_ovf(num, frac / g, &num)) return invalid();
        return reduce(sign * num, s2);
#endif
    }

    if (!any) return invalid();
    if (!at_end(s)) return invalid();          /* trailing garbage is an error */
    return rat_from_int(sign * ip);
}

/* ---- rendering ---- */
static uint32_t put(char *out, uint32_t max, uint32_t at, char c) {
    if (at + 1 < max) out[at] = c;
    return at + 1;
}
static uint32_t put_u64(char *out, uint32_t max, uint32_t at, uint64_t v) {
    char tmp[24]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) at = put(out, max, at, tmp[--n]);
    return at;
}

/* A fraction has a terminating decimal iff its denominator's only prime
 * factors are 2 and 5. We check by dividing them out. */
static bool terminating(int64_t den, uint32_t *places) {
    int64_t d = den; uint32_t twos = 0, fives = 0;
    while (d % 2 == 0) { d /= 2; twos++; }
    while (d % 5 == 0) { d /= 5; fives++; }
    if (d != 1) return false;
    *places = (twos > fives) ? twos : fives;
    return true;
}

uint32_t rat_to_string(rat_t a, char *out, uint32_t max) {
    if (!out || max == 0) return 0;
    uint32_t at = 0;
    if (!a.valid) {
        const char *e = "#OVERFLOW";
        for (const char *p = e; *p; p++) at = put(out, max, at, *p);
        if (at < max) out[at] = '\0'; else if (max) out[max-1] = '\0';
        return at;
    }
    if (a.num < 0) at = put(out, max, at, '-');
    uint64_t n = (uint64_t)(a.num < 0 ? -a.num : a.num);
    uint64_t d = (uint64_t)a.den;

    uint32_t places = 0;
    if (terminating(a.den, &places) && places <= 18) {
        /* exact terminating decimal */
        uint64_t scale = 1;
        for (uint32_t i = 0; i < places; i++) scale *= 10;
        /* Split off the integer part first. A plain n * (scale/d) wraps
         * mod 2^64 (n reaches 2^63-1, scale/d reaches 5^18) and would print
         * a different number with valid == true. Since d divides scale,
         * (n % d) * (scale/d) < scale <= 10^18 always fits, on every target. */
        uint64_t ip = n / d;
        uint64_t fp = (n % d) * (scale / d);
        at = put_u64(out, max, at, ip);
        if (places > 0 && fp != 0) {
            at = put(out, max, at, '.');
            /* leading zeros of the fraction */
            uint64_t probe = scale / 10;
            while (probe > fp && probe > 1) { at = put(out, max, at, '0'); probe /= 10; }
            /* strip trailing zeros for a clean reading */
            while (fp % 10 == 0 && fp > 0) fp /= 10;
            at = put_u64(out, max, at, fp);
        }
    } else {
        /* non-terminating: show the exact ratio rather than rounding */
        at = put_u64(out, max, at, n);
        at = put(out, max, at, '/');
        at = put_u64(out, max, at, d);
    }
    if (at < max) out[at] = '\0'; else if (max) out[max-1] = '\0';
    return at;
}

uint32_t rat_to_fixed(rat_t a, uint32_t places, char *out, uint32_t max,
                      bool *exact) {
    if (exact) *exact = false;
    if (!out || max == 0) return 0;
    if (!a.valid) {
        /* terminate on every path — max==1 used to leave out[] untouched */
        if (max > 1) { out[0] = '#'; out[1] = '\0'; return 1; }
        out[0] = '\0';
        return 1;
    }
    /* places is capped at 18 (10^19 overflows the scale); the cap is
     * applied to `places` itself so the emitted width always equals the
     * effective request instead of silently rendering fewer decimals */
    if (places > 18u) places = 18u;

    uint64_t scale = 1;
    for (uint32_t i = 0; i < places; i++) scale *= 10;

    bool neg = a.num < 0;
    uint64_t n = (uint64_t) (neg ? -a.num : a.num);
    uint64_t d = (uint64_t) a.den;
    /* integer part, then the scaled remainder; r < d keeps the fractional
     * quotient below `scale`, so only r * scale needs 128 bits */
    uint64_t ip = n / d, r = n % d, fp, rem;
#if HAVE_WIDE
    wide_t f = (wide_t) r * (wide_t) scale;
    fp = (uint64_t) (f / (wide_t) d);
    rem = (uint64_t) (f % (wide_t) d);
#else
    u128n f = mulu_wide(r, scale);
    fp = divu_wide(f.hi, f.lo, d, &rem);
#endif
    if (exact) *exact = (rem == 0);
    /* round half away from zero; rem < d < 2^63, so rem * 2 cannot wrap */
    if (rem * 2 >= d) {
        if (++fp == scale) {
            fp = 0;
            ip++;
        }
    }

    uint32_t at = 0;
    if (neg && (ip != 0 || fp != 0)) at = put(out, max, at, '-');
    at = put_u64(out, max, at, ip);
    if (places > 0) {
        at = put(out, max, at, '.');
        uint64_t probe = scale / 10;
        while (probe > fp && probe > 1) { at = put(out, max, at, '0'); probe /= 10; }
        at = put_u64(out, max, at, fp);
    }
    if (at < max) out[at] = '\0'; else if (max) out[max-1] = '\0';
    return at;
}

bool rat_split(rat_t amount, uint32_t parts, rat_t *out) {
    if (!out || parts == 0 || !amount.valid) return false;
    /* Exact split: each share is amount/parts, and rationals divide
     * exactly, so the shares sum back to the original with no residue.
     * (This is precisely what floating point cannot promise.) */
    rat_t share = rat_div(amount, rat_from_int((int64_t)parts));
    if (!share.valid) return false;
    for (uint32_t i = 0; i < parts; i++) out[i] = share;
    return true;
}
