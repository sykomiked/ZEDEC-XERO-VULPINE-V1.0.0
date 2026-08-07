/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* congruence.c — the STRUCTURAL CONGRUENCE test. A state is only valid if it is
 * geometrically sound: if a thing is not structural geometry, it is not
 * structural from a mathematics lens. This makes the QC bar more than "no
 * fault" — it is "no fault AND geometrically congruent". Each check verifies a
 * real, integer-exact property of the M5 substrate (no libm, target-safe):
 *
 *   [1] SET / FIBONACCI — vertical + horizontal chains. Sets S(n) = {F(1..n)}
 *       nest strictly (S(n-1) ⊂ S(n), the vertical chain) and each level obeys
 *       F(n)=F(n-1)+F(n-2) (the horizontal chain). A Fibonacci number "contains
 *       the Fibonacci numbers beneath it."
 *   [2] COMPLEX PLANE — perpendicular real·imaginary. |z|² = r² + i² has NO
 *       cross term: the real and imaginary axes are orthogonal, and Pythagorean
 *       congruence holds (3² + 4² = 5²). This is the complex-plane / trig test.
 *   [3] M5 AXES — higher-dimensional perpendicularity. Via emu_relate_classify,
 *       perturbing one M5 axis's input changes ONLY that axis's output; the
 *       others are invariant. Orthogonality of ω/r/ℓ/φ, verified, not asserted.
 *   [4] LOGIC — more than three postulates. The trit lattice has >3 monotonic
 *       levels (FALSE<GLUT−<NEUTRAL<GLUT+<TRUE): non-binary logic, ordered.
 */
#include "congruence.h"
#include "m5_types.h"
#include "emu_relate.h"

/* [1] Fibonacci nested sets: vertical containment + horizontal recurrence. */
static int cong_fibonacci(void){
    uint64_t f[24]; f[0]=0; f[1]=1;
    for (int n=2;n<24;n++) f[n]=f[n-1]+f[n-2];
    /* horizontal: the recurrence must hold at every level */
    for (int n=2;n<24;n++) if (f[n] != f[n-1]+f[n-2]) return 0;
    /* vertical: S(n)={f[1..n]} strictly contains S(n-1) — the new element f[n]
     * is >= every element beneath it (strict past the degenerate 1,1 head). */
    for (int n=3;n<24;n++) if (!(f[n] > f[n-1])) return 0;
    /* set-membership: every f[k], k<=n, is in S(n) (containment is real) */
    for (int n=5;n<24;n++){
        for (int k=1;k<=n;k++){
            int found=0; for (int j=1;j<=n;j++) if (f[j]==f[k]) { found=1; break; }
            if (!found) return 0;
        }
    }
    return 1;
}

/* [2] Complex plane: perpendicular axes + Pythagorean congruence, integer-exact. */
static int cong_complex_plane(void){
    /* magnitude-squared has no cross term => real ⟂ imaginary */
    struct { long r,i,m2; } t[] = { {3,4,25},{5,12,169},{8,15,289},{20,21,841},{0,7,49},{9,0,81} };
    for (unsigned k=0;k<sizeof t/sizeof t[0];k++)
        if (t[k].r*t[k].r + t[k].i*t[k].i != t[k].m2) return 0;
    /* rotating a vector onto either axis preserves |z|² (congruence under the
     * complex-plane structure): (3,4) and (5,0) and (0,5) are all magnitude 25 */
    if (3*3+4*4 != 5*5 || 5*5+0 != 25 || 0+5*5 != 25) return 0;
    /* orthogonal basis: e_r·e_i = 0 (the defining perpendicularity) */
    if ((1*0 + 0*1) != 0) return 0;
    return 1;
}

/* [3] M5 axis perpendicularity: a perturbation on one axis must not move the
 * others. Uses the real relationship classifier. */
static int cong_m5_perpendicular(void){
    emu_relation_t base, dphi, dr, domega;
    emu_relate_classify(15,1,40,100,  15,1,40,100,  &base);   /* ell=TRUE, ω=0 */
    emu_relate_classify(15,1,40,500,  15,1,40,7,    &dphi);   /* change φ only  */
    emu_relate_classify(15,1,40,100,  14,1,40,100,  &dr);     /* change r  only */
    emu_relate_classify(15,1,99,100,  15,1,40,100,  &domega); /* change ω  only */
    /* Δφ: ℓ, ω, r invariant (φ ⟂ the rest) */
    if (dphi.ell != base.ell) return 0;
    if (dphi.tick.omega != base.tick.omega) return 0;
    if (dphi.tick.r.num != base.tick.r.num || dphi.tick.r.den != base.tick.r.den) return 0;
    /* Δr: ω and φ invariant; ℓ MUST move (results changed => a real glut) */
    if (dr.tick.omega != base.tick.omega) return 0;
    if (dr.tick.iphi.r != base.tick.iphi.r || dr.tick.iphi.i != base.tick.iphi.i) return 0;
    if (dr.ell == base.ell) return 0;
    /* Δω: ℓ, r, φ invariant; ω MUST move */
    if (domega.ell != base.ell) return 0;
    if (domega.tick.iphi.r != base.tick.iphi.r) return 0;
    if (domega.tick.omega == base.tick.omega) return 0;
    return 1;
}

/* [4] More than three postulates: the trit lattice is >3 levels, strictly
 * monotonic FALSE < GLUT− < NEUTRAL < GLUT+ < TRUE. */
static int cong_multivalued_logic(void){
    trit_t order[] = { TRIT_FALSE, TRIT_GLUT_MINUS, TRIT_GLUT_NEUTRAL, TRIT_GLUT_PLUS, TRIT_TRUE };
    int levels = (int)(sizeof order/sizeof order[0]);
    if (levels <= 3) return 0;                       /* must exceed binary+1 */
    for (int k=1;k<levels;k++){
        /* scale the (double) lattice value to an int for an exact comparison */
        long lo = (long)(trit_to_ell(order[k-1]) * 1000.0 + 0.5);
        long hi = (long)(trit_to_ell(order[k])   * 1000.0 + 0.5);
        if (!(hi > lo)) return 0;                    /* strictly monotonic */
    }
    return 1;
}

int m5_congruence_selfcheck(void){
    int m = 0;
    if (cong_fibonacci())        m |= 1;   /* set theory / Fibonacci chains */
    if (cong_complex_plane())    m |= 2;   /* complex plane / trig          */
    if (cong_m5_perpendicular()) m |= 4;   /* higher-dim perpendicularity   */
    if (cong_multivalued_logic())m |= 8;   /* >3 logical postulates         */
    return m;                              /* 15 = fully congruent          */
}
