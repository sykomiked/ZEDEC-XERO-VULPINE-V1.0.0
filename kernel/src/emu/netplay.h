/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* netplay.h — the far side of the bridge: modern networked play, done P2P.
 *
 * The retro consoles were LOCAL machines. Starting at PS2-online and forward
 * (PS3, Xbox 360, PS4/5/6, Xbox One/Series, Steam-style networked PC), the
 * defining new capacity is NETWORKED play. This layer adapts that capacity into
 * this system's own P2P DISTRIBUTED model (mesh_net + Porter House) — peers join
 * a session directly, no central server.
 *
 * KEY REFRAME (per design intent): a networked session here is NOT merely "play
 * a game together." Video-game interfaces have something ordinary computers lack
 * — a PLAYABLE user interface — and the networked version of that is PRODUCTIVITY
 * together: people doing real-world WORK inside a shared, playable space (ref: the
 * shared-virtual-workspace ambition of early metaverse experiments — reference
 * only). So a netplay session is a productive P2P workspace.
 * That workspace already exists in this kernel as the JDR Pirate Fleet
 * (jdr_channel_* — a P2P work crew with affinity matching); netplay is the
 * game-universe front to the same distributed substrate: the session carries
 * both PLAY state and WORK, and relates peers by game-universe AND by work
 * affinity across the mesh — the distributed multiverse as a shared workshop.
 *
 * The AI in the session is CHIGLET (our own companion — the system plays WITH
 * you, not under you; ref: the operate-alongside-companion archetype from
 * Battle Network-style games, used here strictly as a development reference). */
#ifndef ZXV_NETPLAY_H
#define ZXV_NETPLAY_H

#include <stdint.h>
#include "game_universe.h"   /* GU_DIM, surplus_real_t */

/* On-target self-check: host a P2P game session (mesh network), a second peer
 * joins, and the cross-peer game-universe relationship is computed over the mesh
 * — proving networked play in the P2P distributed model. Returns 1 on pass;
 * *relation_permille_out = cross-peer relationship (0..1000), *peers_out = peers. */
int netplay_selfcheck(uint32_t *relation_permille_out, int *peers_out);

#endif /* ZXV_NETPLAY_H */
