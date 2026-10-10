/* m5_types.h — Shared M5 Axiomatic Kernel Types (Reactor Moderator)
 * Author: H.M. Michael-Laurence: Curzi (c)
 * ALL subsystems MUST include this and MUST NOT redefine these types.
 *
 * INTEGER ONLY
 *   This header has no floating point at all: phase vectors and complex
 *   values are Q16.16 integers (zxv_fixed.h), ell is trit_to_ell_q16(), and
 *   rationals are compared exactly with rational_cmp() / m5_coverage_cmp().
 *   The double helpers trit_to_ell() and rational_mag() were removed, so a
 *   stale caller is a compile error rather than a silent FPU dependency.
 *   M5_TYPES_INTEGER_ONLY is still accepted and is now a no-op.
 */
#ifndef M5_TYPES_H
#define M5_TYPES_H
#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "zxv_fixed.h"
typedef uint64_t ordinal_t;
typedef struct rational_t { int64_t num, den; } rational_t;
typedef enum {
    TRIT_FALSE = 0, /* Absence — no presence, no charge */
    TRIT_TRUE = 1,  /* Presence — confirmed, positive */
    TRIT_GLUT = 2,  /* DEPRECATED input alias -> canonicalises to GLUT_NEUTRAL.
                     * Accept it, never emit it. See trit_canon() below. */
    TRIT_GLUT_PLUS =
        3, /* Constructive superposition — both true and false, positive charge (excess) */
    TRIT_GLUT_MINUS =
        4, /* Destructive superposition — both true and false, negative charge (deficit) */
    TRIT_GLUT_NEUTRAL = 5, /* Balanced superposition — both true and false, zero net charge */
} trit_t;
typedef struct collapse_t { uint32_t bits[2]; } collapse_t;
typedef enum {
    ISOMETRY_IDENTITY = 0,
    ISOMETRY_LIFT_M8 = 1,
    ISOMETRY_LIFT_M13 = 2,
    ISOMETRY_PROJECT_BACK = 3,
    ISOMETRY_SWAP_R_L = 4,
    ISOMETRY_NEGATE_PHASE = 5,
} isometry_id_t;
#define AXIOM_MATRIX_DEFAULT_SIZE 1024
#define WORD168_OCTETS 21
#define WORD168_SEPTETS 24
#define WORD168_SEXTETS 28
typedef struct word168_t { uint8_t bytes[WORD168_OCTETS]; } word168_t;
typedef enum { EXEC_DC = 0, EXEC_AC = 1, EXEC_PC = 2 } exec_profile_t;
/* ---- phase / complex types: Q16.16 integers ---- */
typedef struct phase_t {
    int32_t r, i; /* Q16.16 */
} phase_t;
typedef struct telemetry_t {
    zxv_cq16_t value; /* Q16.16 complex */
    uint32_t recursion_depth;
} telemetry_t;
typedef struct axiom_matrix {
    uint64_t size;
    zxv_cq16_t *entries; /* Q16.16 complex */
} axiom_matrix_t;
typedef struct phase_tick {
    ordinal_t omega; rational_t r; trit_t ell; phase_t iphi; collapse_t chi;
} phase_tick_t;
typedef struct shadow_event {
    zxv_cq16_t shadow;
    uint32_t paradox_level;
    char origin[64];
} shadow_event_t;

/* Cycle Pulse (SS5B) — replaces wall clock with event-cycle pulses */
typedef struct cycle_pulse {
    ordinal_t cycle_count;       /* this node's current cycle */
    uint32_t dimensional_level;  /* which M^{F_n} this node is at (0=M5, 1=M8, ...) */
    int64_t coverage_hyperbola;  /* r*ell at last commit, Q16.16 */
    zxv_cq16_t matrix_projection; /* Axiom Matrix projection at current tick, Q16.16 */
    uint32_t fib_cycle_levels[8]; /* Fibonacci-scaled cycle level per axis */
} cycle_pulse_t;

/* Temporal Lattice node identity (SS5B.4) */
typedef struct lattice_node_id {
    uint64_t node_id;            /* unique node identifier */
    uint32_t dim_level;          /* current dimensional level */
    ordinal_t last_sync_cycle;   /* last cycle synchronized with peers */
} lattice_node_id_t;

/* M5 Coordinates — used by rails, identity, mesh_net, crypto_wallet, etc. */
typedef struct m5_coords {
    uint64_t omega;              /* ordinal cycle */
    surplus_real_t r;            /* radial coordinate */
    surplus_real_t ell;          /* angular coordinate (trit-derived) */
    surplus_real_t phi;          /* phase coordinate */
    int32_t chi;                 /* collapse coordinate */
} m5_coords_t;
/* ---- CANONICAL TRIT FORM -------------------------------------------------
 * trit_t has SIX enumerators denoting FIVE states: TRIT_GLUT (2) is a legacy
 * spelling of TRIT_GLUT_NEUTRAL (5). That is fine at an INPUT, and fatal at a
 * BOUNDARY: two encodings of one state make marshalling non-injective, so no
 * unmarshaller can recover what was sent, and it breaks any algebra that needs
 * exactly five elements (GF(5) among them).
 *
 * Resolution, per owner decision: keep TRIT_GLUT as a deprecated alias and
 * CANONICALISE. The rule is one line —
 *
 *      ACCEPT TRIT_GLUT ANYWHERE.  NEVER EMIT IT.
 *
 * Everything crossing a module boundary, entering a lattice, or being compared
 * for equality passes through trit_canon() first. The 59 existing call sites
 * that pass TRIT_GLUT keep working untouched; the canonical image has exactly
 * five values, so the algebra layer is unblocked. */
#define TRIT_CANONICAL_COUNT 5u

static inline trit_t trit_canon(trit_t t) {
    return (t == TRIT_GLUT) ? TRIT_GLUT_NEUTRAL : t;
}

/* Is this value already canonical (i.e. safe to emit)? */
static inline bool trit_is_canonical(trit_t t) {
    return t != TRIT_GLUT;
}

/* The canonicaliser is sound iff it is idempotent, collapses the alias, and its
 * image has exactly TRIT_CANONICAL_COUNT members. Checkable at runtime, which is
 * what lets modbind unblock the trit boundary on evidence rather than on a
 * promise that someone fixed it. */
static inline bool trit_canon_is_sound(void) {
    const trit_t all[6] = { TRIT_FALSE, TRIT_TRUE, TRIT_GLUT,
                            TRIT_GLUT_PLUS, TRIT_GLUT_MINUS, TRIT_GLUT_NEUTRAL };
    if (trit_canon(TRIT_GLUT) != TRIT_GLUT_NEUTRAL) return false;
    unsigned n = 0;
    for (unsigned i = 0; i < 6; i++) {
        trit_t c = trit_canon(all[i]);
        if (trit_canon(c) != c) return false;          /* idempotent */
        if (!trit_is_canonical(c)) return false;       /* lands in the image */
        unsigned seen = 0;
        for (unsigned j = 0; j < i; j++) if (trit_canon(all[j]) == c) seen = 1;
        if (!seen) n++;
    }
    return n == TRIT_CANONICAL_COUNT;                  /* exactly five */
}

/* trit_to_ell in Q16.16 (65536 == 1.0): integer-only equivalent. */
static inline uint32_t trit_to_ell_q16(trit_t t)
{
    switch (t) {
    case TRIT_TRUE:
        return 65536u;
    case TRIT_GLUT_PLUS:
        return 49152u;
    case TRIT_GLUT:
    case TRIT_GLUT_NEUTRAL:
        return 32768u;
    case TRIT_GLUT_MINUS:
        return 16384u;
    case TRIT_FALSE:
    default:
        return 0u;
    }
}

/* Charge accessor: +1 for GLUT_PLUS, -1 for GLUT_MINUS, 0 for all others */
static inline int trit_charge(trit_t t) {
    switch(t) {
        case TRIT_GLUT_PLUS:   return 1;
        case TRIT_GLUT_MINUS:  return -1;
        default:               return 0;
    }
}

/* Check if a trit is in any GLUT (superposition) state */
static inline int trit_is_glut(trit_t t) {
    return t == TRIT_GLUT || t == TRIT_GLUT_PLUS ||
           t == TRIT_GLUT_MINUS || t == TRIT_GLUT_NEUTRAL;
}
/* Exact rational comparison, no floating point and no division: -1, 0, 1.
 * A zero denominator reads as the value 0 (what rational_mag() returned). */
static inline int rational_cmp(rational_t a, rational_t b)
{
    int as = (a.den == 0 || a.num == 0) ? 0 : (((a.num < 0) != (a.den < 0)) ? -1 : 1);
    int bs = (b.den == 0 || b.num == 0) ? 0 : (((b.num < 0) != (b.den < 0)) ? -1 : 1);
    if (as != bs) return as < bs ? -1 : 1;
    if (as == 0) return 0;
    uint64_t an = a.num < 0 ? (uint64_t) 0 - (uint64_t) a.num : (uint64_t) a.num;
    uint64_t ad = a.den < 0 ? (uint64_t) 0 - (uint64_t) a.den : (uint64_t) a.den;
    uint64_t bn = b.num < 0 ? (uint64_t) 0 - (uint64_t) b.num : (uint64_t) b.num;
    uint64_t bd = b.den < 0 ? (uint64_t) 0 - (uint64_t) b.den : (uint64_t) b.den;
    int c = fx_cmp_umul(an, bd, bn, ad); /* |a| vs |b| */
    return as > 0 ? c : -c;
}

/* The M5 coverage hyperbola r * ell compared with num/den, exactly: -1, 0, 1.
 * ell = e/4 with e in 0..4, so r*ell is the rational (r.num*e) / (4*r.den).
 * For |r.num| or |r.den| near 2^62 the operands are scaled down by 4 first
 * (a relative error below 2^-60). */
static inline int m5_coverage_cmp(rational_t r, trit_t ell, int64_t num, int64_t den)
{
    int64_t e = (int64_t) (trit_to_ell_q16(ell) / 16384u);
    int64_t n = r.num, d = r.den;
    const int64_t lim = INT64_MAX / 4;
    while (n > lim || n < -lim || d > lim || d < -lim) {
        n /= 4;
        d /= 4;
    }
    rational_t lhs = {n * e, d * 4};
    rational_t rhs = {num, den};
    return rational_cmp(lhs, rhs);
}

/* floor(r * ell * 1000) clamped to [0, 0x7FFFFFFF] (the scheduler's phase). */
static inline uint32_t m5_coverage_permille(rational_t r, trit_t ell)
{
    int64_t e = (int64_t) (trit_to_ell_q16(ell) / 16384u);
    int64_t n = r.num, d = r.den, t;
    if (d == 0 || n == 0 || e == 0) return 0;
    if (d < 0) {
        n = -n;
        d = -d;
    }
    if (n < 0) return 0;
    if (__builtin_mul_overflow(n, e * 250, &t)) return 0x7FFFFFFFu;
    uint64_t q = fx_udiv64((uint64_t) t, (uint64_t) d, 0);
    return q > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t) q;
}
static inline int64_t m5_gcd(int64_t a, int64_t b) {
    if(a<0) a=-a;
    if(b<0) b=-b;
    while(b){int64_t t=b; b=a%b; a=t;} return a;
}
static inline rational_t rational_normalize(rational_t r) {
    if(r.den<0){r.num=-r.num; r.den=-r.den;}
    int64_t g=m5_gcd(r.num,r.den);
    if(g>1){r.num/=g; r.den/=g;}
    if(r.den==0)r.den=1;
    return r;
}
#endif
