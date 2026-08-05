/* test_mesh_net.c — P2P Mesh Network Layer tests
 *
 * Tests network creation, peer joining with Porter House gating,
 * trade route creation, data transfer, route expiry, federation,
 * access control, and M5 coverage.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "mesh_net.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int feq(double a, double b, double eps) {
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

static word168_t make_peer(uint8_t seed) {
    word168_t w;
    for (int i = 0; i < WORD168_OCTETS; i++) w.bytes[i] = (uint8_t)(seed + i);
    return w;
}

int main(void) {
    /* ===== init ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        mesh_net_t mn;
        mn_init(&mn, 1, "ZEDEC:mesh-net", &ph);
        assert(mn.device_id == 1);
        assert(mn.num_networks == 0);
        assert(mn.num_routes == 0);
        assert(mn.porter == &ph);
    }

    /* ===== create network ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_TRUSTED, 500);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(1);
        int32_t nid = mn_create_network(&mn, "AlphaMesh", MN_NET_TRUSTED,
                                          &creator, 800);
        assert(nid > 0);
        assert(mn.num_networks == 1);

        mn_network_t *net = mn_get_network(&mn, (uint32_t)nid);
        assert(net != NULL);
        assert(net->state == MN_NET_ACTIVE);
        assert(net->access == MN_NET_TRUSTED);
        assert(net->num_peers == 1); /* creator */
        assert(net->peers[0].is_gateway == false);
    }

    /* ===== create network rejected by Porter House (low trust) ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_TRUSTED, 500);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(2);
        int32_t nid = mn_create_network(&mn, "BadMesh", MN_NET_TRUSTED,
                                          &creator, 100);
        assert(nid == -2);
    }

    /* ===== join network ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_TRUSTED, 500);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(10);
        int32_t nid = mn_create_network(&mn, "BetaMesh", MN_NET_TRUSTED,
                                          &creator, 800);
        assert(nid > 0);

        word168_t peer = make_peer(20);
        assert(mn_join_network(&mn, (uint32_t)nid, &peer, 600, 1000000) == 0);

        mn_network_t *net = mn_get_network(&mn, (uint32_t)nid);
        assert(net->num_peers == 2);
    }

    /* ===== join network rejected by Porter House ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_TRUSTED, 500);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(30);
        int32_t nid = mn_create_network(&mn, "GammaMesh", MN_NET_TRUSTED,
                                          &creator, 800);
        assert(nid > 0);

        word168_t peer = make_peer(40);
        assert(mn_join_network(&mn, (uint32_t)nid, &peer, 100, 500000) == -3);
    }

    /* ===== open network: anyone can join ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_OPEN, 0);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(50);
        int32_t nid = mn_create_network(&mn, "OpenMesh", MN_NET_OPEN,
                                          &creator, 0);
        assert(nid > 0);

        word168_t peer1 = make_peer(51);
        word168_t peer2 = make_peer(52);
        assert(mn_join_network(&mn, (uint32_t)nid, &peer1, 0, 100000) == 0);
        assert(mn_join_network(&mn, (uint32_t)nid, &peer2, 0, 200000) == 0);

        mn_network_t *net = mn_get_network(&mn, (uint32_t)nid);
        assert(net->num_peers == 3);
    }

    /* ===== private network: only creator can join initially ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(60);
        int32_t nid = mn_create_network(&mn, "PrivateMesh", MN_NET_PRIVATE,
                                          &creator, 1000);
        assert(nid > 0);

        word168_t stranger = make_peer(70);
        assert(mn_join_network(&mn, (uint32_t)nid, &stranger, 1000, 500000) == -4);
    }

    /* ===== leave network ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_OPEN, 0);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(80);
        int32_t nid = mn_create_network(&mn, "LeaveMesh", MN_NET_OPEN,
                                          &creator, 0);
        assert(nid > 0);

        word168_t peer = make_peer(81);
        assert(mn_join_network(&mn, (uint32_t)nid, &peer, 0, 100000) == 0);

        mn_network_t *net = mn_get_network(&mn, (uint32_t)nid);
        assert(net->num_peers == 2);

        assert(mn_leave_network(&mn, (uint32_t)nid, &peer) == 0);
        assert(net->num_peers == 1);
    }

    /* ===== create trade route + send data ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_OPEN, 0);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(90);
        int32_t nid = mn_create_network(&mn, "TradeMesh", MN_NET_OPEN,
                                          &creator, 0);
        assert(nid > 0);

        word168_t src = make_peer(91);
        word168_t dst = make_peer(92);
        int32_t rid = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_TRADE,
                                        &src, &dst, 10, 1000000, 0);
        assert(rid > 0);
        assert(mn.num_routes == 1);

        /* Add a hop */
        word168_t hop = make_peer(93);
        assert(mn_add_hop(&mn, (uint32_t)rid, &hop) == 0);

        mn_route_t *r = mn_get_route(&mn, (uint32_t)rid);
        assert(r->num_hops == 1);

        /* Send data */
        assert(mn_send_data(&mn, (uint32_t)rid, 5000, 100) == 0);
        assert(r->data_transferred == 5000);
        assert(r->revenue == 50000); /* 5000 * 10 */
        assert(mn.total_data_routed == 5000);
        assert(mn.total_revenue == 50000);
    }

    /* ===== route expiry ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_OPEN, 0);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(100);
        int32_t nid = mn_create_network(&mn, "ExpireMesh", MN_NET_OPEN,
                                          &creator, 0);
        assert(nid > 0);

        word168_t src = make_peer(101);
        word168_t dst = make_peer(102);
        int32_t rid = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_DATA,
                                        &src, &dst, 0, 100000, 0);
        assert(rid > 0);

        /* Not yet expired */
        assert(mn_check_expired(&mn, 5000) == 0);

        /* Expired */
        assert(mn_check_expired(&mn, MN_ROUTE_TIMEOUT + 100) == 1);

        mn_route_t *r = mn_get_route(&mn, (uint32_t)rid);
        assert(r->state == MN_ROUTE_EXPIRED);
        assert(mn.total_routes_expired == 1);
    }

    /* ===== federation ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_OPEN, 0);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t c1 = make_peer(110);
        word168_t c2 = make_peer(120);
        int32_t n1 = mn_create_network(&mn, "Fed1", MN_NET_OPEN, &c1, 0);
        int32_t n2 = mn_create_network(&mn, "Fed2", MN_NET_OPEN, &c2, 0);
        assert(n1 > 0 && n2 > 0);

        assert(mn_federate(&mn, (uint32_t)n1, (uint32_t)n2) == 0);

        mn_network_t *net1 = mn_get_network(&mn, (uint32_t)n1);
        mn_network_t *net2 = mn_get_network(&mn, (uint32_t)n2);
        assert(net1->state == MN_NET_FEDERATED);
        assert(net2->state == MN_NET_FEDERATED);
        assert(net1->peers[0].is_gateway == true);
        assert(net2->peers[0].is_gateway == true);
    }

    /* ===== route types ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_OPEN, 0);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(130);
        int32_t nid = mn_create_network(&mn, "TypeMesh", MN_NET_OPEN,
                                          &creator, 0);
        assert(nid > 0);

        word168_t src = make_peer(131);
        word168_t dst = make_peer(132);

        int32_t r1 = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_DATA, &src, &dst, 0, 100, 0);
        int32_t r2 = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_TRADE, &src, &dst, 5, 100, 0);
        int32_t r3 = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_COMPUTE, &src, &dst, 20, 100, 0);
        int32_t r4 = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_VOICE, &src, &dst, 1, 100, 0);
        int32_t r5 = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_EMERGENCY, &src, &dst, 0, 100, 0);

        assert(r1 > 0 && r2 > 0 && r3 > 0 && r4 > 0 && r5 > 0);
        assert(mn_get_route(&mn, (uint32_t)r1)->type == MN_ROUTE_DATA);
        assert(mn_get_route(&mn, (uint32_t)r2)->type == MN_ROUTE_TRADE);
        assert(mn_get_route(&mn, (uint32_t)r3)->type == MN_ROUTE_COMPUTE);
        assert(mn_get_route(&mn, (uint32_t)r4)->type == MN_ROUTE_VOICE);
        assert(mn_get_route(&mn, (uint32_t)r5)->type == MN_ROUTE_EMERGENCY);
    }

    /* ===== close route ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_OPEN, 0);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(140);
        int32_t nid = mn_create_network(&mn, "CloseMesh", MN_NET_OPEN,
                                          &creator, 0);
        word168_t src = make_peer(141);
        word168_t dst = make_peer(142);
        int32_t rid = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_DATA,
                                        &src, &dst, 0, 100, 0);
        assert(rid > 0);

        assert(mn_close_route(&mn, (uint32_t)rid) == 0);
        mn_route_t *r = mn_get_route(&mn, (uint32_t)rid);
        assert(r->state == MN_ROUTE_CLOSED);

        /* Can't send on closed route */
        assert(mn_send_data(&mn, (uint32_t)rid, 100, 0) == -1);
    }

    /* ===== coverage computation ===== */
    {
        porter_house_t ph;
        porter_house_init(&ph, 1, "porter");
        porter_house_seal_port(&ph, MN_MESH_PORT, PH_SEAL_OPEN, 0);
        mesh_net_t mn;
        mn_init(&mn, 1, "mesh", &ph);

        word168_t creator = make_peer(150);
        int32_t nid = mn_create_network(&mn, "CovMesh", MN_NET_OPEN,
                                          &creator, 0);
        word168_t src = make_peer(151);
        word168_t dst = make_peer(152);
        int32_t rid = mn_create_route(&mn, (uint32_t)nid, MN_ROUTE_DATA,
                                        &src, &dst, 0, 100, 0);

        /* Send data to make route active */
        mn_send_data(&mn, (uint32_t)rid, 1000, 100);

        mn_update_coverage(&mn);
        /* r = 1 active / 1 total = 1.0
         * ell = 1 active net / 1 total net = 1.0 */
        assert(feq(mn.m5.r, 1.0, 1e-9));
        assert(feq(mn.m5.ell, 1.0, 1e-9));
    }

    printf("All P2P Mesh Network tests passed\n");
    return 0;
}
