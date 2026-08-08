/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* mechanic_mod.h — the admission policy for native mechanic-mods. A game/ROM/mod
 * may add or change mechanics, but it must NOT be able to take over the whole
 * system. This layer is the gate: it runs the mod's mechanics through the
 * break-potency detector and DECIDES whether to admit it as-is or CONTAIN it.
 *
 *   - SAFE mod (potency within bounds)   -> ADMITTED: runs at full strength.
 *   - BROKEN mod (brazenly over-potent)  -> CONTAINED: admitted only in a capped
 *     sandbox — its potency is scaled back to the break threshold so it can add
 *     to the experience without dominating the system. Nothing is rejected
 *     outright (over-potence is a prized mechanic — we harness it, bounded).
 *
 * Honest scope: this is the ADMISSION DECISION + the potency cap it implies. The
 * memory/privilege sandbox that physically confines a contained mod is the
 * kernel's existing EL0/process + W^X machinery — this module produces the
 * policy verdict that machinery should enforce. See break_potency.h. */
#ifndef ZXV_MECHANIC_MOD_H
#define ZXV_MECHANIC_MOD_H

#include <stdint.h>
#include "surplus.h"
#include "break_potency.h"   /* BP_DIM, break_report_t */

#define MM_MAX_MECHANICS 8

typedef enum {
    MM_ADMITTED  = 0,   /* within bounds — runs at full strength   */
    MM_CONTAINED = 1,   /* over-potent — admitted, but capped/sandboxed */
} mm_status_t;

typedef struct mechanic_mod {
    const char    *name;
    uint32_t       count;                          /* mechanics this mod adds */
    surplus_real_t mech[MM_MAX_MECHANICS][BP_DIM]; /* their potency vectors   */
} mechanic_mod_t;

typedef struct mm_admission {
    mm_status_t    status;
    break_report_t report;      /* the potency analysis behind the verdict     */
    surplus_real_t cap;         /* potency ceiling to enforce (== base if broken,
                                 * else the mod's own combined potency)         */
} mm_admission_t;

/* Evaluate a mod for admission: analyse its mechanics, decide admit vs contain,
 * and set the potency cap the sandbox should enforce. */
void mechanic_mod_admit(const mechanic_mod_t *mod, mm_admission_t *out);

/* On-target self-check: a modest mod is ADMITTED; a synergy-break mod is
 * CONTAINED with its cap pulled below its raw combined potency. Returns 1 on pass. */
int  mechanic_mod_selfcheck(uint32_t *contained_cap_permille_out);

#endif /* ZXV_MECHANIC_MOD_H */
