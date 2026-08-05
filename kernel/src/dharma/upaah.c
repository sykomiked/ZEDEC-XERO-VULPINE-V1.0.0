/* upaah.c — UPAAH / VPAAH / PIR implementation. See upaah.h. */
#include "upaah.h"

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
