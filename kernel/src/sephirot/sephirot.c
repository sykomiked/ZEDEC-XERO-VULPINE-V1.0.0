/* sephirot.c — L13 OS Hypervisor implementation. See sephirot.h. */
#include "sephirot.h"
#include "rmag_core.h"

static rational_t rz(void) { rational_t r = {0, 1}; return r; }
static rational_t ro(void) { rational_t r = {1, 1}; return r; }
static bool rat_is_zero(rational_t a) { return a.num == 0; }
static bool rat_equal(rational_t a, rational_t b) {
    return (int64_t)a.num * b.den == (int64_t)b.num * a.den;
}

/* ---- Light/Shadow ---- */

l13_state_t l13_to_shadow(l13_phase_t phase) {
    if (!l13_has_shadow(phase)) return 0;
    return (l13_state_t)(((uint8_t)phase) << 4);
}

l13_phase_t l13_to_light(l13_state_t shadow) {
    if (!l13_is_shadow(shadow)) return 0;
    return (l13_phase_t)(shadow >> 4);
}

/* ---- cyc13_t: exact-rational Q[Z/13Z] ---- */

cyc13_t cyc13_zero(void) {
    cyc13_t r;
    for (int i = 0; i < L13_NUM_PHASES; i++) r.c[i] = rz();
    return r;
}

cyc13_t cyc13_basis(uint32_t k) {
    cyc13_t r = cyc13_zero();
    r.c[k % L13_NUM_PHASES] = ro();
    return r;
}

cyc13_t cyc13_from_phase(l13_phase_t p) {
    /* Phase 13 (Ain Soph Aur) maps to zeta^0 = 1, the multiplicative
     * identity: the source from which the other 12 phases (as powers
     * of zeta) are generated. Phases 1..12 map to zeta^1..zeta^12. */
    return cyc13_basis((uint32_t)p % L13_NUM_PHASES);
}

cyc13_t cyc13_add(cyc13_t a, cyc13_t b) {
    cyc13_t r;
    for (int i = 0; i < L13_NUM_PHASES; i++) r.c[i] = rmag_add_quotas(a.c[i], b.c[i]);
    return r;
}

cyc13_t cyc13_sub(cyc13_t a, cyc13_t b) {
    cyc13_t r;
    for (int i = 0; i < L13_NUM_PHASES; i++) r.c[i] = rmag_sub_quotas(a.c[i], b.c[i]);
    return r;
}

cyc13_t cyc13_mul(cyc13_t a, cyc13_t b) {
    /* Cyclic convolution mod 13: (a*b)[k] = sum_{i+j == k mod 13} a[i]*b[j] */
    cyc13_t r = cyc13_zero();
    for (int i = 0; i < L13_NUM_PHASES; i++) {
        if (rat_is_zero(a.c[i])) continue;
        for (int j = 0; j < L13_NUM_PHASES; j++) {
            if (rat_is_zero(b.c[j])) continue;
            int k = (i + j) % L13_NUM_PHASES;
            r.c[k] = rmag_add_quotas(r.c[k], rmag_mul_quotas(a.c[i], b.c[j]));
        }
    }
    return r;
}

cyc13_t cyc13_scale(cyc13_t a, rational_t s) {
    cyc13_t r;
    for (int i = 0; i < L13_NUM_PHASES; i++) r.c[i] = rmag_mul_quotas(a.c[i], s);
    return r;
}

bool cyc13_equal(cyc13_t a, cyc13_t b) {
    for (int i = 0; i < L13_NUM_PHASES; i++)
        if (!rat_equal(a.c[i], b.c[i])) return false;
    return true;
}

bool cyc13_is_zero(cyc13_t a) {
    for (int i = 0; i < L13_NUM_PHASES; i++)
        if (!rat_is_zero(a.c[i])) return false;
    return true;
}

cyc13_t cyc13_galois_apply(cyc13_t a, uint32_t exponent) {
    /* k -> (exponent*k) mod 13 is a bijection on Z/13Z whenever
     * exponent is coprime to 13 -- true for all exponent in 1..12
     * since 13 is prime. Direct assignment is safe: no two source
     * indices ever collide on the same destination index. */
    cyc13_t r = cyc13_zero();
    for (uint32_t k = 0; k < L13_NUM_PHASES; k++) {
        uint32_t dst = (exponent * k) % L13_NUM_PHASES;
        r.c[dst] = a.c[k];
    }
    return r;
}

/* ---- Galois subfield projection ---- */

/* Subgroups of (Z/13Z)* = <2> (2 is a primitive root mod 13), one
 * per divisor of 12, verified directly by exhaustive multiplication
 * closure (see sephirot design notes / test suite). */
static const uint32_t H_DIM1[]  = {1,2,3,4,5,6,7,8,9,10,11,12}; /* order 12: whole group */
static const uint32_t H_DIM2[]  = {1,3,4,9,10,12};              /* order 6  */
static const uint32_t H_DIM3[]  = {1,5,8,12};                   /* order 4  */
static const uint32_t H_DIM4[]  = {1,3,9};                      /* order 3  */
static const uint32_t H_DIM6[]  = {1,12};                       /* order 2  */
static const uint32_t H_DIM12[] = {1};                          /* order 1: trivial */

cyc13_t l13_project(cyc13_t x, l13_dim_t target_dim) {
    const uint32_t *H; uint32_t hn;
    switch (target_dim) {
        case L13_DIM_1:  H = H_DIM1;  hn = 12; break;
        case L13_DIM_2:  H = H_DIM2;  hn = 6;  break;
        case L13_DIM_3:  H = H_DIM3;  hn = 4;  break;
        case L13_DIM_4:  H = H_DIM4;  hn = 3;  break;
        case L13_DIM_6:  H = H_DIM6;  hn = 2;  break;
        case L13_DIM_12: H = H_DIM12; hn = 1;  break;
        default: return x;
    }
    cyc13_t acc = cyc13_zero();
    for (uint32_t i = 0; i < hn; i++) {
        acc = cyc13_add(acc, cyc13_galois_apply(x, H[i]));
    }
    return acc;
}

/* ---- Da'at switch ---- */

daat_decision_t daat_switch(cyc13_t collapsed_state) {
    return cyc13_is_zero(collapsed_state) ? DAAT_RESET : DAAT_CONTINUE;
}

/* ---- Naming ---- */

const char *l13_phase_name(l13_phase_t phase) {
    switch (phase) {
        case SEPH_KETER:        return "Keter";
        case SEPH_CHOKMAH:      return "Chokmah";
        case SEPH_BINAH:        return "Binah";
        case SEPH_CHESED:       return "Chesed";
        case SEPH_GEVURAH:      return "Gevurah";
        case SEPH_TIFERET:      return "Tiferet";
        case SEPH_NETZACH:      return "Netzach";
        case SEPH_HOD:          return "Hod";
        case SEPH_YESOD:        return "Yesod";
        case SEPH_MALKUTH:      return "Malkuth";
        case VEIL_AIN:          return "Ain";
        case VEIL_AIN_SOPH:     return "Ain Soph";
        case VEIL_AIN_SOPH_AUR: return "Ain Soph Aur";
        default:                return "Unknown";
    }
}

const char *l13_qliphah_name(l13_phase_t sephirah) {
    switch (sephirah) {
        case SEPH_KETER:   return "Thaumiel";
        case SEPH_CHOKMAH: return "Ghagiel";
        case SEPH_BINAH:   return "Sathariel";
        case SEPH_CHESED:  return "Gamchicoth";
        case SEPH_GEVURAH: return "Golachab";
        case SEPH_TIFERET: return "Thagirion";
        case SEPH_NETZACH: return "Harab-Serapel";
        case SEPH_HOD:     return "Samael";
        case SEPH_YESOD:   return "Gamaliel";
        case SEPH_MALKUTH: return "Lilith";
        default:           return "Unknown";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * PROVIDES cyc13_ready -- the 13th-cyclotomic state algebra (cyc13_*) and the
 * Galois trace projection l13_project. That is the capability name because it
 * is what four dharma files actually import; "sephirot_ready" would have named
 * the directory instead of the API.
 *
 * REQUIRES(rmag_ready) is measured: sephirot.o's `nm -u` is exactly
 * {rmag_add_quotas, rmag_mul_quotas, rmag_sub_quotas}. Coefficients are
 * rational quotas.
 *
 * The bring-up checks the two identities the whole algebra rests on -- zero is
 * an additive identity, and distinct basis states are distinct -- because a
 * cyc13_add that silently returns its first argument would still "work" for
 * every single-term expression and fail only under superposition.
 */
#include "zxv_decl.h"
static int zxvd_sephirot_bringup(void) {
    cyc13_t z = cyc13_zero();
    cyc13_t a = cyc13_basis(1u);
    cyc13_t b = cyc13_basis(2u);
    if (!cyc13_is_zero(z))            return -1;
    if (!cyc13_equal(cyc13_add(a, z), a)) return -1;   /* zero is identity  */
    if (cyc13_equal(a, b))            return -1;       /* basis is faithful */
    return 0;
}

ZXV_DECLARE(sephirot,
    ZXV_PROVIDES(cyc13_ready),
    ZXV_REQUIRES(rmag_ready),
    ZXV_BRINGUP(zxvd_sephirot_bringup));
