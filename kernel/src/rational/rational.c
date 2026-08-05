/* rational.c — WyvernEye exact rational arithmetic. See rational.h. */
#include "rational.h"

/* 128-bit intermediates let every operation detect overflow before it
 * happens. Every 64-bit target this kernel builds for provides them. */
#if defined(__SIZEOF_INT128__)
typedef __int128 wide_t;
#define HAVE_WIDE 1
#else
typedef int64_t wide_t;
#define HAVE_WIDE 0
#endif

static const int64_t I64_MAX = 0x7FFFFFFFFFFFFFFFLL;

static rat_t invalid(void) { rat_t r; r.num = 0; r.den = 1; r.valid = false; return r; }

static int64_t igcd(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a ? a : 1;
}

/* Reduce to lowest terms with den > 0. Keeping values reduced after every
 * operation is what stops denominators exploding over a long column of
 * sums — the practical reason naive rational arithmetic gets a bad name. */
static rat_t reduce(wide_t num, wide_t den) {
    if (den == 0) return invalid();
    if (num == 0) { rat_t r; r.num = 0; r.den = 1; r.valid = true; return r; }
    if (den < 0) { num = -num; den = -den; }

#if HAVE_WIDE
    /* reduce in 128-bit, then check the result fits in 64 */
    wide_t a = num < 0 ? -num : num, b = den;
    while (b) { wide_t t = a % b; a = b; b = t; }
    wide_t g = a ? a : 1;
    num /= g; den /= g;
    if (num > (wide_t)I64_MAX || num < -(wide_t)I64_MAX) return invalid();
    if (den > (wide_t)I64_MAX) return invalid();
#else
    int64_t g = igcd((int64_t)num, (int64_t)den);
    num /= g; den /= g;
#endif
    rat_t r; r.num = (int64_t)num; r.den = (int64_t)den; r.valid = true;
    return r;
}

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
    return reduce((wide_t)num, (wide_t)den);
}

/* ---- arithmetic ----
 * Cross-reducing before multiplying keeps the intermediates small, which
 * matters because it is the difference between a column of a thousand
 * sums staying representable and overflowing. */
rat_t rat_add(rat_t a, rat_t b) {
    if (!a.valid || !b.valid) return invalid();
    int64_t g = igcd(a.den, b.den);
    wide_t lcm_den = (wide_t)(a.den / g) * (wide_t)b.den;
    wide_t num = (wide_t)a.num * (wide_t)(b.den / g)
               + (wide_t)b.num * (wide_t)(a.den / g);
    return reduce(num, lcm_den);
}

rat_t rat_sub(rat_t a, rat_t b) { return rat_add(a, rat_neg(b)); }

rat_t rat_mul(rat_t a, rat_t b) {
    if (!a.valid || !b.valid) return invalid();
    /* cross-reduce first */
    int64_t g1 = igcd(a.num, b.den);
    int64_t g2 = igcd(b.num, a.den);
    wide_t n = (wide_t)(a.num / g1) * (wide_t)(b.num / g2);
    wide_t d = (wide_t)(a.den / g2) * (wide_t)(b.den / g1);
    return reduce(n, d);
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
    wide_t l = (wide_t)a.num * (wide_t)b.den;
    wide_t r = (wide_t)b.num * (wide_t)a.den;
    return (l < r) ? -1 : (l > r) ? 1 : 0;
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
            if (d > (I64_MAX - 9) / 10) return invalid();
            d = d * 10 + (*s - '0'); s++; anyd = true;
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
        wide_t num = (wide_t)ip * (wide_t)scale + (wide_t)frac;
        return reduce(sign * num, (wide_t)scale);
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
        /* The scaling multiply MUST be done wide. `n` reaches 2^63-1 and
         * `scale/d` reaches 5^18, so a 64-bit product wraps mod 2^64 and
         * prints a completely different number with valid == true — which
         * is exactly what rational.h promises can never happen. The sibling
         * rat_to_fixed already scales in wide_t; this path did not. */
        wide_t wscaled = (wide_t)n * (wide_t)(scale / d);
        uint64_t ip = (uint64_t)(wscaled / (wide_t)scale);
        uint64_t fp = (uint64_t)(wscaled % (wide_t)scale);
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
    wide_t n = (wide_t)(neg ? -a.num : a.num) * (wide_t)scale;
    wide_t q = n / a.den;
    wide_t rem = n % a.den;
    if (exact) *exact = (rem == 0);
    /* round half away from zero */
    if (rem * 2 >= a.den) q += 1;

    uint32_t at = 0;
    if (neg && q != 0) at = put(out, max, at, '-');
    uint64_t ip = (uint64_t)(q / (wide_t)scale);
    uint64_t fp = (uint64_t)(q % (wide_t)scale);
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
