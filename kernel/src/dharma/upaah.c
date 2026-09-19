/* upaah.c — UPAAH / VPAAH / PIR implementation. See upaah.h. */
#include "upaah.h"

/* This module is COMPILED as of build_system/Makefile.arm64, and it has a
 * real caller: refinery.c maps a forged card's net Enochian charge through
 * eno_charge_role -> {UPAAH, VPAAH, PIR} and dispatches it here. That is
 * the whole reason the charge channel exists -- PROVENANCE/ENOCHIAN_POLARITY.md
 * §6 gives the table (>0 S+ UPAAH VEIL_AIN_SOPH_AUR, <0 S- VPAAH
 * VEIL_AIN_SOPH, =0 S0 PIR SEPH_YESOD) and §9G says wiring this file in
 * without a callee leaves the feature dangling.
 *
 * NO RETENTION MARKERS. An earlier revision of this change marked every
 * entry point __attribute__((used, retain)); measured on this toolchain
 * (aarch64 gcc 11.4 / binutils 2.38) `retain` is IGNORED -- it warns and
 * emits a plain AX section -- so the module was still absent from the ELF.
 * The Makefile's GC_ROOTS comment gives the rule this tree settled on:
 * retention must not be able to impersonate use. An ordinary call chain
 * from kernel_main is the only thing that counts, and that is what this
 * module now has. */

bool upaah_applies(trit_t t) { return trit_charge(t) == 1; }

l13_phase_t upaah_bridge(trit_t t) {
    if (!upaah_applies(t)) return (l13_phase_t)0; /* not UPAAH's domain */
    return VEIL_AIN_SOPH_AUR;
}

bool vpaah_applies(trit_t t) { return trit_charge(t) == -1; }

l13_phase_t vpaah_bridge(trit_t t) {
    if (!vpaah_applies(t)) return (l13_phase_t)0;
    return VEIL_AIN_SOPH;
}

bool pir_applies(trit_t t) { return trit_charge(t) == 0; }

l13_phase_t pir_bridge(trit_t t) {
    if (!pir_applies(t)) return (l13_phase_t)0;
    if (trit_is_glut(t)) return SEPH_YESOD;       /* balanced GLUT: genuinely both + and - */
    if (t == TRIT_TRUE) return SEPH_MALKUTH;       /* definite, non-glut: zero charge because not glut at all */
    return VEIL_AIN;                                /* TRIT_FALSE (and any other non-glut, zero-charge case) */
}

l13_phase_t phase7_bridge(trit_t t) {
    if (upaah_applies(t)) return upaah_bridge(t);
    if (vpaah_applies(t)) return vpaah_bridge(t);
    return pir_bridge(t);
}

trit_t phase7_unbridge(l13_phase_t phase) {
    switch (phase) {
        case VEIL_AIN_SOPH_AUR: return TRIT_GLUT_PLUS;
        case VEIL_AIN_SOPH:     return TRIT_GLUT_MINUS;
        case SEPH_YESOD:        return TRIT_GLUT_NEUTRAL;
        case SEPH_MALKUTH:      return TRIT_TRUE;
        case VEIL_AIN:          return TRIT_FALSE;
        default:                return TRIT_FALSE; /* Netzach(7)/Hod(8): OS-native, no kernel-trit equivalent */
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * upaah is the phase-7 bridge. dharma.o's `nm -u` names phase7_bridge and
 * phase7_unbridge, and this file defines them; its own `nm -u` is empty.
 *
 * This module was ALREADY wired and already reachable before this pass. The
 * declaration adds no code and roots nothing -- it records the edge dharma.c
 * depends on, so that "dharma silently lost its bridge" becomes a graph
 * question instead of a mystery.
 */
#include "zxv_decl.h"
ZXV_DECLARE(upaah,
    ZXV_PROVIDES(phase7_ready),
    ZXV_REQUIRES_NONE,
    ZXV_NO_BRINGUP);
