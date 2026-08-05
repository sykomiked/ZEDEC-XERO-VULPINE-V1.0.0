/* upaah.h — UPAAH / VPAAH / PIR: the Phase-7 Interface Engine
 *
 * The interface engine between the kernel's six-valued trit_t logic
 * and the OS layer's L13 phases (7-13), sitting at phase 7 (Netzach)
 * -- the first OS-native phase, the threshold where kernel logic
 * meets the OS layer. Split by charge polarity, using m5_types.h's
 * own trit_charge() (+1 / -1 / 0):
 *
 *   UPAAH (+):   the positive-charge bridge. TRIT_GLUT_PLUS (charge
 *                +1, constructive/excess superposition) lifts to
 *                VEIL_AIN_SOPH_AUR, the source of emanation.
 *   VPAAH (-):   the negative-charge bridge. TRIT_GLUT_MINUS (charge
 *                -1, destructive/deficit superposition) lifts to
 *                VEIL_AIN_SOPH, unbounded/unresolved potential.
 *   PIR  (+&-):  the both-charge bridge. Zero net charge covers two
 *                genuinely different cases, both handled here:
 *                balanced GLUT states (TRIT_GLUT / TRIT_GLUT_NEUTRAL,
 *                literally both + and - in superposition) lift to
 *                SEPH_YESOD; and the two definite, non-glut trits
 *                (TRIT_TRUE -> SEPH_MALKUTH, TRIT_FALSE -> VEIL_AIN)
 *                are PIR's degenerate, unambiguous cases -- zero
 *                charge because they are not glut at all, not
 *                because they are a superposition of both signs.
 *
 * phase7_bridge is the complete engine: it dispatches every trit_t to
 * exactly one of UPAAH/VPAAH/PIR and returns that phase. The reverse
 * direction (phase -> trit) is the corresponding un-bridge.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef UPAAH_H
#define UPAAH_H

#include "m5_types.h"
#include "sephirot.h"

bool upaah_applies(trit_t t);      /* true iff trit_charge(t) == +1 */
l13_phase_t upaah_bridge(trit_t t); /* TRIT_GLUT_PLUS -> VEIL_AIN_SOPH_AUR */

bool vpaah_applies(trit_t t);      /* true iff trit_charge(t) == -1 */
l13_phase_t vpaah_bridge(trit_t t); /* TRIT_GLUT_MINUS -> VEIL_AIN_SOPH */

bool pir_applies(trit_t t);        /* true iff trit_charge(t) == 0 */
l13_phase_t pir_bridge(trit_t t);  /* TRIT_GLUT/GLUT_NEUTRAL -> SEPH_YESOD;
                                     * TRIT_TRUE -> SEPH_MALKUTH; TRIT_FALSE -> VEIL_AIN */

/* The complete phase-7 interface engine: dispatches to whichever of
 * UPAAH/VPAAH/PIR applies. */
l13_phase_t phase7_bridge(trit_t t);

/* The reverse direction: which trit a given OS-layer phase (7-13)
 * un-bridges to. Phases with no kernel-trit equivalent (Netzach 7,
 * Hod 8 -- see dharma.h) fall back to TRIT_FALSE, matching this
 * codebase's existing convention (not an error sentinel). */
trit_t phase7_unbridge(l13_phase_t phase);

#endif
