/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_pspace.h — public spaces and the watch hall inside zxvd: the pspace.*
 * and hall.* method group (zxvd_ext.h) over the node's Vinea spaces
 * (zxv_net_host.h U14, kernel/src/vinea/vna_space.h). docs/SPACES.md.
 *
 *   W1  The group has the feature "pspace" and is not regulated; like every
 *       feature it starts off unless --enable pspace or features.set turns
 *       it on. pspace.leave, pspace.events, pspace.members, pspace.list and
 *       hall.status are `always` (zxvd_ext.h X4): reading, and leaving, keep
 *       working with the switch off.
 *   W2  Every method needs the network on (LAN or ONLINE); while it is off
 *       each answers "the network is off" (ZXVD_E_PSPACE). Spaces live only
 *       while the node runs: turning the network off leaves them all.
 *   W3  CIDs are text: the space's base32 CIDv1 ("b..."). A hall's media is
 *       carried as the text the owner gave (any CID form, at most 64 bytes),
 *       so every viewer fetches the same thing.
 *   W4  hall.play and hall.pause without pos_ms continue from the position
 *       every viewer computes now; hall.seek keeps the current state and
 *       rate; hall.load stops at pos_ms (default 0). Only the creator of an
 *       owner hall, or any member of a quorum hall, may change playback.
 *   W5  pspace.find starts a directory search for a topic, or reports the one
 *       running for that topic; call it again until "done".
 *   W6  Times are this node's wall clock in ms since 1970; integers only.
 *       Bad parameters answer -32602; a refusal by the space layer answers
 *       ZXVD_E_PSPACE with the reason.
 */
#ifndef ZXVD_PSPACE_H
#define ZXVD_PSPACE_H

#include "zxvd_ext.h"
#include "zxv_net_host.h"

#define ZXVD_E_PSPACE (-32030)

/* Bind the group to this net host (NULL: every method answers W2's error). */
void zxvd_pspace_bind(zxv_net_t *net);

#endif
