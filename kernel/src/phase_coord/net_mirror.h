/* net_mirror.h — the triadic network space: the mesh mirrored into the kernel.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE THIRD SPACE. The dual mirror (Human <-> Chiglet) becomes triadic when
 * the network stops being a transport layer and becomes a first-class mirror
 * of the kernel: peer topology, content-addressed identity, post-quantum
 * sessions and distributed clocks all project through the SAME valence
 * machinery as the kernel's own rings. A node, route or resource boundary in
 * network space mirrors a process, memory region or agent inside the core —
 * the network topology IS the system topology across physical distance.
 *
 * THE GLOBAL TEMPORAL MANIFOLD. The phase coordinator's local ticks advance
 * network events on phase ordinals (OSEQ vector-clock discipline): the mirror
 * carries local_tick vs peer_max_tick, and drift renders as BOTH — ambiguity,
 * never a guess about which clock is right.
 *
 * TRIADIC STIGMERGY. When a human command or a Chiglet plan must execute
 * across the mesh, the network mirror allocates bandwidth / offload / storage
 * by the Interaction Surplus Framework: u = 1 - (a.b)^2 with a and b unit
 * vectors (the human intent and the AI action). Nodes cooperate as orthogonal,
 * non-extractive participants: u = 1 means fully complementary (maximal
 * surplus), u = 0 means identical (nothing to trade). This is computed in
 * EXACT Q16.16 integer arithmetic — no float for an allocation.
 */
#ifndef ZXV_NET_MIRROR_H
#define ZXV_NET_MIRROR_H

#include <stdint.h>
#include <stdbool.h>

#include "mirror_ui.h"
#include "zxv_fixed.h" /* Q16_ONE, Q16 fixed point                         */

/* --gc-sections KEEP — see mirror_ui.h: no boot caller yet, retain the API. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#  define NM_KEEP __attribute__((used, retain))
#else
#  define NM_KEEP __attribute__((used))
#endif

/* ---- triadic space ids ----------------------------------------------------- */

typedef enum {
    NET_SPACE_HUMAN = 0,   /* intent and visualization                     */
    NET_SPACE_CHIGLET = 1, /* reasoning and co-pilot execution             */
    NET_SPACE_NETWORK = 2, /* distributed consensus and peer topology      */
    NET_SPACE_COUNT
} net_space_id_t;

/* ---- the network-space mirror ---------------------------------------------- */

#define NET_MIRROR_MAX_VIEWS 16u

typedef struct {
    mirror_net_view_t topology;                      /* peers up / total in the baked mesh   */
    mirror_cid_view_t identity;                      /* this node's content-addressed ID     */
    mirror_pq_view_t sessions[NET_MIRROR_MAX_VIEWS]; /* PQ peer sessions  */
    uint32_t session_count;
    mirror_clock_view_t clock; /* local ordinal vs peer max ordinal    */
} net_mirror_state_t;

typedef struct {
    net_mirror_state_t state;
    mirror_ui_t mirror; /* the shared projection source         */
    uint64_t last_tick;
} net_mirror_t;

/* ---- Interaction Surplus Framework ------------------------------------------ */

/* u = 1 - dot^2 in Q16.16, where `dot_q16` is the Q16.16 dot product of two
 * unit vectors (the human intent a and the AI action b). Exact integer
 * arithmetic throughout: the square is kept in 64-bit halves and the result
 * saturates to [0, Q16_ONE]. Returns false (and writes Q16_ONE) if |dot|
 * exceeds 1 — an allocation never runs on a malformed pair.
 *   dot = 0    -> u = 1   (orthogonal: maximal interaction surplus)
 *   dot = +-1  -> u = 0   (parallel/anti-parallel: nothing to trade) */
bool net_mirror_isf(uint32_t dot_q16, uint32_t *u_q16_out);

/* ---- API ------------------------------------------------------------------- */

void net_mirror_init(net_mirror_t *nm);

/* Bind one PQ session view into the mirror. Returns its index, or -1 if the
 * table is full. */
int net_mirror_add_session(net_mirror_t *nm, mirror_pq_view_t *view);

/* Bind the topology / identity / clock views (called once at bring-up with
 * pointers into kernel static state). */
int net_mirror_bind_topology(net_mirror_t *nm, mirror_net_view_t *view);
int net_mirror_bind_identity(net_mirror_t *nm, mirror_cid_view_t *view);
int net_mirror_bind_clock(net_mirror_t *nm, mirror_clock_view_t *view);

/* Project the network mirror into a frame at `tick` (the shared mirror frame
 * shape, so the human, Chiglet and network consumers all read the same
 * bytes). Deterministic: same views + same tick -> same frame. */
void net_mirror_project(const net_mirror_t *nm, uint64_t tick, mirror_frame_t *out);

/* The triadic decision: given the mirror, which space's action proceeds on
 * this tick? 0=human, 1=chiglet, 2=network. Deterministic fold over the
 * frame digest — same state, same answer, no wall clock. */
net_space_id_t net_mirror_arbiter(const net_mirror_t *nm, uint64_t tick);

#endif /* ZXV_NET_MIRROR_H */