/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_net_host.c — two app-side Vinea net hosts (zxv_net_host.h) on
 * 127.0.0.1, different ports, real UDP sockets, driven by one select() loop
 * as zxv_host.c drives them: A pings B by address, both end up in each
 * other's routing table, and a FIND_NODE lookup from each reaches the other.
 * Also: OFF opens no socket and sends nothing; LAN mode refuses a public
 * address before anything is sent; ONLINE accepts it.
 *
 *   sh kernel/arch/hosted/test_hosted.sh runs it (POSIX hosts). */
#define _DEFAULT_SOURCE
#include <sys/select.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_net_host.h"

static int failures = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        int ok_ = (c) ? 1 : 0; /* evaluated once: c may have side effects */                       \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

/* One pass of the app's event loop for both hosts. */
static void pump(zxv_net_t *a, zxv_net_t *b, int ms)
{
    fd_set rd;
    FD_ZERO(&rd);
    int fa = (int) zxv_net_fd(a), fb = (int) zxv_net_fd(b), mx = -1;
    if (fa >= 0) FD_SET(fa, &rd), mx = fa;
    if (fb >= 0) FD_SET(fb, &rd), mx = fb > mx ? fb : mx;
    struct timeval tv = {0, ms * 1000};
    int r = select(mx + 1, &rd, NULL, NULL, &tv);
    uint64_t now = zxv_net_wall_ms();
    if (r > 0 && fa >= 0 && FD_ISSET(fa, &rd)) zxv_net_on_readable(a, now);
    if (r > 0 && fb >= 0 && FD_ISSET(fb, &rd)) zxv_net_on_readable(b, now);
    zxv_net_tick(a, now);
    zxv_net_tick(b, now);
}

int main(void)
{
    zxv_net_t *a = zxv_net_new(), *b = zxv_net_new();
    zxv_net_status_t sa, sb;
    CHECK(a && b, "two hosts created");

    printf("[1] off by default\n");
    zxv_net_status(a, &sa);
    CHECK(sa.mode == ZXV_NET_OFF && !sa.bound && zxv_net_fd(a) == -1,
          "a new host is OFF with no socket");
    CHECK(zxv_net_add_peer(a, "127.0.0.1", 9, zxv_net_wall_ms()) == -1,
          "OFF: adding a peer is refused");
    zxv_net_status(a, &sa);
    CHECK(sa.datagrams_out == 0, "OFF: nothing was sent");

    printf("[2] two hosts on 127.0.0.1\n");
    uint64_t now = zxv_net_wall_ms();
    CHECK(zxv_net_start(a, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0, "A starts in LAN mode");
    CHECK(zxv_net_start(b, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0, "B starts in LAN mode");
    zxv_net_status(a, &sa);
    zxv_net_status(b, &sb);
    CHECK(sa.bound && sb.bound && sa.port && sb.port && sa.port != sb.port,
          "bound on ports %u and %u", sa.port, sb.port);
    CHECK(strcmp(sa.node_id, sb.node_id) != 0 && strlen(sa.node_id) == 16,
          "distinct NodeIDs %s / %s", sa.node_id, sb.node_id);

    CHECK(zxv_net_add_peer(a, "8.8.8.8", 8723, now) == -1, "LAN mode refuses a public address");
    zxv_net_status(a, &sa);
    CHECK(sa.datagrams_out == 0 && sa.policy_drops == 1,
          "and nothing was sent for it (out %u, drops %u)", sa.datagrams_out, sa.policy_drops);

    CHECK(zxv_net_add_peer(a, "127.0.0.1", sb.port, now) == 0, "A pings B by address");
    for (int i = 0; i < 100 && !(zxv_net_knows(a, "127.0.0.1", sb.port) &&
                                 zxv_net_knows(b, "127.0.0.1", sa.port));
         i++)
        pump(a, b, 20);
    CHECK(zxv_net_knows(a, "127.0.0.1", sb.port), "A has B in its routing table");
    CHECK(zxv_net_knows(b, "127.0.0.1", sa.port), "B has A in its routing table");

    uint8_t ida[32], idb[32];
    zxv_net_node_id(a, ida);
    zxv_net_node_id(b, idb);
    int32_t la = zxv_net_find_node(a, idb, zxv_net_wall_ms());
    int32_t lb = zxv_net_find_node(b, ida, zxv_net_wall_ms());
    CHECK(la >= 0 && lb >= 0, "FIND_NODE lookups started (slots %d, %d)", (int) la, (int) lb);
    int ra = -1, rb = -1;
    for (int i = 0; i < 200 && (ra < 0 || rb < 0); i++) {
        pump(a, b, 20);
        if (ra < 0) ra = zxv_net_find_result(a, la);
        if (rb < 0) rb = zxv_net_find_result(b, lb);
    }
    CHECK(ra == 1, "A's FIND_NODE for B was answered by B");
    CHECK(rb == 1, "B's FIND_NODE for A was answered by A");
    /* U6: the economic cycle closes on a live host, with nothing traded */
    static swarm_budget_t sbud;
    swarm_budget_init(&sbud, 2, 5000);
    swarm_budget_register(&sbud, 1, 0);
    swarm_budget_begin_cycle(&sbud);
    uint64_t imp = 99;
    CHECK(zxv_net_econ_cycle(a, &sbud, 5000, now, &imp) == 0 && imp == 0 &&
              sbud.tokens_per_cycle == 5000,
          "U6: a cycle with no trades imports nothing; the rate stays at the base");
    CHECK(zxv_net_econ_cycle(a, NULL, 5000, now, &imp) == -1, "U6: no budget, refused");
    zxv_net_status(a, &sa);
    zxv_net_status(b, &sb);
    CHECK(sa.peers >= 1 && sb.peers >= 1 && sa.refused_in == 0 && sb.refused_in == 0,
          "peers %u / %u, every datagram verified (in %u / %u, out %u / %u)", sa.peers, sb.peers,
          sa.datagrams_in, sb.datagrams_in, sa.datagrams_out, sb.datagrams_out);
    CHECK(sa.conserved && sb.conserved && sa.trades == 0 && sa.receipts_refused == 0,
          "U6: both mesh books conserved, nothing traded or refused");

    printf("[2b] U9 listings\n");
    {
        static zxv_net_peer_t pl[8];
        static zxv_net_want_info_t wl[8];
        static zxv_net_pend_info_t ql[16];
        char hb[65], want_addr[22];
        static const char hx[] = "0123456789abcdef";
        for (int i = 0; i < 32; i++) hb[2 * i] = hx[idb[i] >> 4], hb[2 * i + 1] = hx[idb[i] & 15];
        hb[64] = 0;
        snprintf(want_addr, sizeof want_addr, "127.0.0.1:%u", (unsigned) sb.port);
        uint32_t np = zxv_net_peers(a, pl, 8);
        int at = -1;
        for (uint32_t i = 0; i < np && i < 8; i++)
            if (!strcmp(pl[i].id, hb)) at = (int) i;
        CHECK(np >= 1 && np == zxv_net_peers(a, NULL, 0) && at >= 0,
              "A lists %u peer(s), B among them; the count needs no buffer", np);
        CHECK(at >= 0 && !strcmp(pl[at].addr, want_addr) && pl[at].last_seen_ms > 0 &&
                  !pl[at].in_book && pl[at].pending == 0 && pl[at].held == 0,
              "B's row: address %s, seen, not yet in the mesh book", at >= 0 ? pl[at].addr : "?");
        CHECK(zxv_net_wants(b, wl, 8) == 0 && zxv_net_pending(a, ql, 16) == 0,
              "nothing wanted or outstanding yet");
        CHECK(zxv_net_share(a, 50, 0) == 0, "A shares 50 compute");
        int32_t ws = zxv_net_want(b, VNA_RES_STORAGE, 7, 3);
        int32_t wc = zxv_net_want(b, VNA_RES_COMPUTE, 10, 100);
        CHECK(ws >= 0 && wc >= 0 && zxv_net_wants(b, wl, 1) == 2 && wl[0].slot == (uint32_t) ws &&
                  wl[0].resource == VNA_RES_STORAGE && wl[0].units_left == 7 &&
                  wl[0].max_price == 3,
              "B's wants listed in slot order; the count is all of them, even past cap");
        CHECK(zxv_net_wants(b, wl, 8) == 2 && wl[1].slot == (uint32_t) wc &&
                  wl[1].resource == VNA_RES_COMPUTE && wl[1].units_left == 10 &&
                  wl[1].max_price == 100,
              "and the compute want");
        CHECK(zxv_net_sell(a, idb, VNA_RES_COMPUTE, 4, 9, zxv_net_wall_ms()) == 0,
              "A offers B 4 compute");
        uint32_t nq = zxv_net_pending(a, ql, 16);
        CHECK(nq == 1 && ql[0].selling && !strcmp(ql[0].peer, hb) && ql[0].units == 4 &&
                  ql[0].want == -1 && ql[0].pair_seq >= 1,
              "A's offer is outstanding until B answers (seq %llu)",
              (unsigned long long) ql[0].pair_seq);
        np = zxv_net_peers(a, pl, 8);
        at = -1;
        for (uint32_t i = 0; i < np && i < 8; i++)
            if (!strcmp(pl[i].id, hb)) at = (int) i;
        CHECK(at >= 0 && pl[at].in_book && !pl[at].member && pl[at].pending == 1,
              "B is now in A's mesh book with one receipt pending");
        for (int i = 0; i < 200 && (zxv_net_pending(a, NULL, 0) || zxv_net_pending(b, NULL, 0));
             i++)
            pump(a, b, 20);
        CHECK(zxv_net_pending(a, NULL, 0) == 0 && zxv_net_pending(b, NULL, 0) == 0,
              "after B accepts, nothing is outstanding on either side");
        CHECK(zxv_net_wants(b, wl, 8) == 2 && wl[1].units_left == 6, "B still wants 6 compute");
        np = zxv_net_peers(a, pl, 8);
        at = -1;
        for (uint32_t i = 0; i < np && i < 8; i++)
            if (!strcmp(pl[i].id, hb)) at = (int) i;
        CHECK(at >= 0 && pl[at].receivable > 0 && pl[at].pending == 0,
              "A's row for B shows the credit delivered (%llu)",
              at >= 0 ? (unsigned long long) pl[at].receivable : 0ull);
        CHECK(zxv_net_share(a, 0, 0) == 0, "A stops sharing");
    }

    printf("[3] modes\n");
    CHECK(zxv_net_start(a, ZXV_NET_OFF, NULL, 0, now) == 0 && zxv_net_fd(a) == -1,
          "switching A off closes its socket");
    zxv_net_status(a, &sa);
    CHECK(sa.peers == 0 && sa.node_id[0] == 0, "and forgets its session");
    CHECK(zxv_net_peers(a, NULL, 0) == 0 && zxv_net_wants(a, NULL, 0) == 0 &&
              zxv_net_pending(a, NULL, 0) == 0 && zxv_net_peers(NULL, NULL, 0) == 0,
          "U9: off (or no host), every listing is empty");
    CHECK(zxv_net_econ_cycle(a, &sbud, 7000, now, &imp) == 0 && imp == 0 &&
              sbud.tokens_per_cycle == 5000,
          "U6: off, the economic cycle does nothing");
    CHECK(zxv_net_start(a, ZXV_NET_ONLINE, "127.0.0.1", 0, now) == 0, "A restarts ONLINE");
    CHECK(zxv_net_add_peer(a, "192.0.2.1", 8723, now) == 0,
          "ONLINE accepts a public address (TEST-NET-1, nothing answers)");
    zxv_net_mode_t m;
    CHECK(zxv_net_mode_parse("lan", &m) == 0 && m == ZXV_NET_LAN &&
              zxv_net_mode_parse("everything", &m) == -1,
          "mode names parse strictly");
    CHECK(zxv_net_add_peer_str(b, "127.0.0.1:0", now) == -1 &&
              zxv_net_add_peer_str(b, "localhost:80", now) == -1,
          "bad peer strings refused");

    zxv_net_free(a);
    zxv_net_free(b);
    printf(failures ? "net host test FAILED (%d)\n" : "net host test passed\n", failures);
    return failures ? 1 : 0;
}
