/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_harmonic_logic.h — explicit maps between the wire's truth enum and the
 * kernel's existing paraconsistent modules. Nothing here adds semantics: each
 * map says where information is lost.
 *
 *   zt_truth_state_t      lpres_state_t (K3, Belnap four-valued)
 *   UNKNOWN  0            NEITHER 0
 *   TRUE     1            TRUE    1
 *   FALSE    2            FALSE   2
 *   NEUTRAL  3            NEITHER 0   (lossy: lpres has no "silent" value)
 *   GLUT     4            BOTH    3
 *   PARADOX  5            BOTH    3   (lossy: lpres keeps no persistence)
 *   lpres -> wire: NEITHER -> UNKNOWN, TRUE, FALSE, BOTH -> GLUT.
 *
 *   swarm_hk_truth_t (K5) has the same six states in another numeric order
 *   (TRUE 0, FALSE 1, GLUT 2, NEUTRAL 3, PARADOX 4, UNKNOWN 5); the map is a
 *   bijection by name.
 *
 * To combine truth values use the owning module's operators (lpres_conjoin,
 * swarm_hk_and, ...); they differ (lpres: TRUE AND FALSE = FALSE; swarm_hk:
 * TRUE AND FALSE = GLUT), and this module does not pick one silently.
 */
#ifndef ZT_HARMONIC_LOGIC_H
#define ZT_HARMONIC_LOGIC_H

#include "zt_harmonic_wire.h"
#include "../lpres/lpres.h"
#include "../swarm/swarm_hk.h"

lpres_state_t zt_truth_to_lpres(zt_truth_state_t s);
zt_truth_state_t zt_truth_from_lpres(lpres_state_t s);
swarm_hk_truth_t zt_truth_to_hk(zt_truth_state_t s);
zt_truth_state_t zt_truth_from_hk(swarm_hk_truth_t s);

#endif /* ZT_HARMONIC_LOGIC_H */
