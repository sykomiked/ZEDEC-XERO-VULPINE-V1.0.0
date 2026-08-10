/* m5_types.h — Shared M5 Axiomatic Kernel Types (Reactor Moderator)
 * Author: H.M. Michael-Laurence: Curzi (c)
 * ALL subsystems MUST include this and MUST NOT redefine these types.
 */
#ifndef M5_TYPES_H
#define M5_TYPES_H
#include <stdint.h>
#include <stdbool.h>
#include <complex.h>
#include <math.h>
typedef uint64_t ordinal_t;
typedef struct rational_t { int64_t num, den; } rational_t;
typedef enum {
    TRIT_FALSE    = 0,  /* Absence — no presence, no charge */
    TRIT_TRUE     = 1,  /* Presence — confirmed, positive */
    TRIT_GLUT     = 2,  /* DEPRECATED input alias -> canonicalises to GLUT_NEUTRAL.
                         * Accept it, never emit it. See trit_canon() below. */
    TRIT_GLUT_PLUS  = 3,  /* Constructive superposition — both true and false, positive charge (excess) */
    TRIT_GLUT_MINUS = 4,  /* Destructive superposition — both true and false, negative charge (deficit) */
    TRIT_GLUT_NEUTRAL = 5, /* Balanced superposition — both true and false, zero net charge */
} trit_t;
typedef struct phase_t { double r, i; } phase_t;
typedef struct collapse_t { uint32_t bits[2]; } collapse_t;
typedef enum {
    ISOMETRY_IDENTITY=0, ISOMETRY_LIFT_M8=1, ISOMETRY_LIFT_M13=2,
    ISOMETRY_PROJECT_BACK=3, ISOMETRY_SWAP_R_L=4, ISOMETRY_NEGATE_PHASE=5,
} isometry_id_t;
typedef struct telemetry_t { double complex value; uint32_t recursion_depth; } telemetry_t;
#define AXIOM_MATRIX_DEFAULT_SIZE 1024
typedef struct axiom_matrix { uint64_t size; double complex *entries; } axiom_matrix_t;
#define WORD168_OCTETS 21
#define WORD168_SEPTETS 24
#define WORD168_SEXTETS 28
typedef struct word168_t { uint8_t bytes[WORD168_OCTETS]; } word168_t;
typedef struct phase_tick {
    ordinal_t omega; rational_t r; trit_t ell; phase_t iphi; collapse_t chi;
} phase_tick_t;
typedef enum { EXEC_DC=0, EXEC_AC=1, EXEC_PC=2 } exec_profile_t;
typedef struct shadow_event { double complex shadow; uint32_t paradox_level; char origin[64]; } shadow_event_t;

/* Cycle Pulse (SS5B) — replaces wall clock with event-cycle pulses */
typedef struct cycle_pulse {
    ordinal_t cycle_count;       /* this node's current cycle */
    uint32_t dimensional_level;  /* which M^{F_n} this node is at (0=M5, 1=M8, ...) */
    double coverage_hyperbola;   /* r*ell at last commit */
    double complex matrix_projection; /* Axiom Matrix projection at current tick */
    uint32_t fib_cycle_levels[8]; /* Fibonacci-scaled cycle level per axis */
} cycle_pulse_t;

/* Temporal Lattice node identity (SS5B.4) */
typedef struct lattice_node_id {
    uint64_t node_id;            /* unique node identifier */
    uint32_t dim_level;          /* current dimensional level */
    ordinal_t last_sync_cycle;   /* last cycle synchronized with peers */
} lattice_node_id_t;
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

static inline double trit_to_ell(trit_t t) {
    switch(t) {
        case TRIT_TRUE:        return 1.0;
        case TRIT_GLUT_PLUS:   return 0.75;  /* Constructive — leaning true */
        case TRIT_GLUT:
        case TRIT_GLUT_NEUTRAL: return 0.5;  /* Balanced — equal true/false */
        case TRIT_GLUT_MINUS:  return 0.25;  /* Destructive — leaning false */
        case TRIT_FALSE:
        default:               return 0.0;
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
static inline double rational_mag(rational_t r) {
    return r.den==0 ? 0.0 : (double)r.num/(double)r.den;
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
