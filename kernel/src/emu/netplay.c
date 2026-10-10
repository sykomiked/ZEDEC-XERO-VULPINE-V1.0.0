/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* netplay.c — the OTHER side of the bridge: the networked-play capacity the
 * modern consoles (PS2 online forward: PS3, Xbox 360, networked PC) introduced,
 * reimagined in this system's P2P DISTRIBUTED model — NOT client-server.
 *
 * A game universe becomes a P2P NETPLAY SESSION: a mesh network (mesh_net) that
 * peers JOIN directly (optionally trust-gated via Porter House, or post-quantum
 * private). Each peer contributes its own game-universe STATE, and the session
 * relates them across the mesh — the DISTRIBUTED MULTIVERSE. The cross-peer
 * relationship (chg_interaction) is the "networked social interaction" between
 * universes, the same M5 relationship the local multiverse uses, now spanning
 * nodes. Built entirely on the existing P2P stack (mesh_net + Porter House),
 * so it inherits the mesh's trust, routing, and post-quantum access controls.
 *
 * Honest scope: this is the SESSION + distributed-relationship layer on the real
 * P2P mesh. Over-the-wire state transport between two PHYSICAL nodes uses
 * mn_send_data + the net stack (both built); a live two-node run needs two
 * instances and is the next step. Emulating PS3/360 hardware is out of scope —
 * we adapt their networking CAPACITY, not their silicon. */
#include "netplay.h"
#include "mesh_net.h"
#include "porter_house.h"
#include "chiglet.h"

static void peer_id(word168_t *w, uint32_t dev){
    for (unsigned i = 0; i < WORD168_OCTETS; i++) w->bytes[i] = 0;
    w->bytes[0] = (uint8_t)dev; w->bytes[1] = (uint8_t)(dev >> 8);
}

int netplay_selfcheck(uint32_t *relation_permille_out, int *peers_out){
    static porter_house_t ph;
    static mesh_net_t     mn;
    porter_house_init(&ph, 1, "netplay-node");
    mn_init(&mn, 1, "node-host", &ph);

    word168_t host, peer;
    peer_id(&host, 0x1001u);
    peer_id(&peer, 0x2002u);

    /* Host a game session as a P2P mesh network. Peers join directly (no central
     * server); OPEN here, but MN_NET_TRUSTED / MN_NET_PRIVATE gate the session by
     * Porter House trust / post-quantum invite when wanted. */
    int32_t nid = mn_create_network(&mn, "GAME-SESSION", MN_NET_OPEN, &host, 100u);
    if (nid < 0) return 0;

    /* A second peer joins the distributed session. */
    int32_t jr = mn_join_network(&mn, (uint32_t)nid, &peer, 100u, 1000000u);
    if (jr != 0) return 0;
    if (peers_out) *peers_out = 2;

    /* Each peer is a USER operating ALONGSIDE their AI companion — CHIGLET, our
     * own companion (ref: the operate-alongside-companion archetype from Battle
     * Network-style games — development reference only): the system plays WITH
     * you, not merely under you. Per this system's ISF principle, a strong
     * partnership is COMPLEMENTARY, not a mirror — the companion is valuable
     * because it is perpendicular to the user, covering blind spots. So the
     * user<->Chiglet bond is measured the same way as expert orthogonality:
     * chg_interaction high = complementary (good partner), ~0 = redundant clone. */
    surplus_real_t userA[GU_DIM] = { SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.3),
                                     SR_FROM_FLOAT(0.6),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.5),SR_FROM_FLOAT(0.3) };
    surplus_real_t naviA[GU_DIM] = { SR_FROM_FLOAT(0.3),SR_FROM_FLOAT(0.4),SR_FROM_FLOAT(0.5),SR_FROM_FLOAT(0.9),
                                     SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.4),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.7) };
    surplus_real_t companion = chg_interaction(userA, naviA, GU_DIM);   /* alongside bond */

    /* Peer B: a second user+Navi pair. The session relates the two pairs across
     * the mesh — the distributed multiverse (networked, productivity-capable). */
    surplus_real_t pairB[GU_DIM] = { SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.5),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.6),
                                     SR_FROM_FLOAT(0.4),SR_FROM_FLOAT(0.5),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.7) };
    surplus_real_t rel = chg_interaction(userA, pairB, GU_DIM);         /* cross-peer link */
    if (relation_permille_out) *relation_permille_out = SR_TO_PERMILLE(rel);

    /* PASS: P2P session created + a peer joined + BOTH relationships are finite —
     * the user<->Navi companionship (system playing alongside you) AND the
     * cross-peer link (the distributed multiverse) hold over the mesh. */
    int ok_companion = (companion > SR_ZERO) && (companion < SR_ONE);
    int ok_link      = (rel > SR_ZERO) && (rel < SR_ONE);
    return (ok_companion && ok_link) ? 1 : 0;
}
