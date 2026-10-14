/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_net_host.c — two app-side Vinea net hosts (zxv_net_host.h) on
 * 127.0.0.1, different ports, real UDP sockets, driven by one select() loop
 * as zxv_host.c drives them: A pings B by address, both end up in each
 * other's routing table, and a FIND_NODE lookup from each reaches the other.
 * Also: OFF opens no socket and sends nothing; LAN mode refuses a public
 * address before anything is sent; ONLINE accepts it.
 * U9-U12: the identity and peer cache persist in a state folder (and a
 * damaged or exposed identity file stops the node), two hosts find each
 * other by LAN multicast alone, introductions are asked for, and an AIPI
 * call goes from one host to the other over Vinea, is verified, and its
 * bounty settles as a compute receipt in both mesh books.
 *
 *   sh kernel/arch/hosted/test_hosted.sh runs it (POSIX hosts). */
#define _DEFAULT_SOURCE
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* ===== U12: a fake sandbox (isqrt, checked by squaring) ================== */
static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static void wr64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static int fx_run(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                  uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    (void) ctx;
    if (in_len != 8 || cap < 8 || strcmp(code->fn->entry, "isqrt")) return AIPI_ERR_EXEC;
    uint64_t n = rd64(in), r = 0;
    for (int b = 31; b >= 0; b--)
        if ((r | (1ull << b)) * (r | (1ull << b)) <= n) r |= 1ull << b;
    wr64(out, r);
    *out_len = 8;
    return AIPI_OK;
}
static int fx_check(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                    const uint8_t *out, uint32_t out_len, const uint8_t *cert, uint32_t cert_len)
{
    (void) ctx, (void) code, (void) cert, (void) cert_len;
    if (in_len != 8 || out_len != 8) return 0;
    uint64_t n = rd64(in), r = rd64(out);
    return r * r <= n && (r + 1) * (r + 1) > n;
}

typedef struct {
    ipfsn_memstore_t ms;
    ipfsn_storage_ops_t ops;
    ipfsn_bs_entry_t tab[32];
    ipfsn_bs_t bs;
    ipfsn_pin_t pins[8];
    ipfsn_node_t in;
    aipi_job_t jobs[4];
    aipi_memo_t memo[4];
    aipi_ctx_t a;
    uint8_t *scratch, *net, *mod, *con;
} ai_box_t;

static void ai_free(ai_box_t *x)
{
    free(x->ms.buf);
    free(x->scratch);
    free(x->net);
    free(x->mod);
    free(x->con);
    free(x);
}

static ai_box_t *ai_new(uint8_t tag)
{
    ai_box_t *x = calloc(1, sizeof *x);
    x->ms.cap = 1u << 20;
    x->ms.buf = malloc(x->ms.cap);
    ipfsn_memstore_ops(&x->ms, &x->ops);
    x->scratch = malloc(IPFSN_SCRATCH_MIN);
    x->net = malloc(IPFSN_BLOCK_MAX + 256);
    x->mod = malloc(AIPI_MODULE_MAX);
    x->con = malloc(AIPI_CONTRACT_MAX);
    if (ipfsn_bs_init(&x->bs, &x->ops, x->tab, 32, x->scratch, IPFSN_SCRATCH_MIN) != IPFSN_OK ||
        ipfsn_node_init(&x->in, &x->bs, x->pins, 8, x->net, IPFSN_BLOCK_MAX + 256) != IPFSN_OK ||
        aipi_init(&x->a, &x->in, x->jobs, 4, x->memo, 4, x->mod, x->con) != AIPI_OK)
        abort();
    uint8_t key[IPFSN_KEY_LEN];
    memset(key, tag, sizeof key);
    ipfsn_bs_set_key(&x->bs, key);
    x->a.online = true;
    x->a.exec.run = fx_run;
    x->a.exec.check = fx_check;
    return x;
}

static bool wait_for(zxv_net_t *a, zxv_net_t *b, bool (*done)(void *), void *ctx, int ms)
{
    for (int i = 0; i < ms / 20; i++) {
        if (done(ctx)) return true;
        pump(a, b, 20);
    }
    return done(ctx);
}

typedef struct {
    zxv_net_t *x, *y;
    uint16_t px, py;
} pair_t;
static bool knows_both(void *c)
{
    pair_t *p = (pair_t *) c;
    return zxv_net_knows(p->x, "127.0.0.1", p->py) && zxv_net_knows(p->y, "127.0.0.1", p->px);
}

/* the introduction request has gone out and a peer has reported our address */
static bool asked_and_voted(void *x)
{
    zxv_net_status_t st;
    zxv_net_status((zxv_net_t *) x, &st);
    return st.punch_asked == 1 && st.public_votes >= 1;
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
    zxv_net_set_lan_discovery(a, false, 0); /* [4] tests discovery on its own */
    zxv_net_set_lan_discovery(b, false, 0);
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

    printf("[2b] U13 listings\n");
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
          "U13: off (or no host), every listing is empty");
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

    printf("[4] U10: two hosts find each other by LAN multicast alone\n");
    uint16_t lport = (uint16_t) (20000 + getpid() % 20000); /* runs never cross */
    zxv_net_t *c = zxv_net_new(), *d = zxv_net_new();
    zxv_net_status_t sc, sd;
    pair_t pr;
    zxv_net_set_lan_discovery(c, true, lport);
    zxv_net_set_lan_discovery(d, true, lport);
    CHECK(zxv_net_start(c, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0 &&
              zxv_net_start(d, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0,
          "C and D start in LAN mode with discovery on");
    zxv_net_status(c, &sc);
    zxv_net_status(d, &sd);
    if (!sc.lan_discovery || !sd.lan_discovery) {
        printf("  skip this host cannot join a multicast group on the loopback\n");
    } else {
        CHECK(zxv_net_lan_fd(c) >= 0 && zxv_net_lan_fd(c) != zxv_net_fd(c), "a second socket");
        pr = (pair_t){c, d, sc.port, sd.port};
        CHECK(wait_for(c, d, knows_both, &pr, 4000),
              "with no address typed, each has the other in its routing table");
        for (int i = 0; i < 150; i++) { /* the announcement burst repeats at 1 s, 2 s */
            zxv_net_status(c, &sc);
            zxv_net_status(d, &sd);
            if (sc.lan_accepted >= 1 && sd.lan_accepted >= 1) break;
            pump(c, d, 20);
        }
        CHECK(sc.lan_accepted >= 1 && sd.lan_accepted >= 1 && sc.lan_peers >= 1 &&
                  sd.lan_peers >= 1,
              "signed LAN records accepted (C %u, D %u)", sc.lan_accepted, sd.lan_accepted);
    }

    printf("[5] U9: the identity and the peer cache persist\n");
    char dir[] = "/tmp/zxv-net-state.XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "a state folder");
    char idp[256], pcp[256];
    snprintf(idp, sizeof idp, "%s/vinea_identity", dir);
    snprintf(pcp, sizeof pcp, "%s/vinea_peers", dir);
    zxv_net_set_lan_discovery(c, true, lport);
    CHECK(zxv_net_set_state_dir(c, dir) == 0, "C keeps its state there");
    CHECK(zxv_net_start(c, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0, "C starts");
    zxv_net_status(c, &sc);
    char id1[65];
    memcpy(id1, sc.node_id_full, sizeof id1);
    struct stat st;
    CHECK(sc.persisted && stat(idp, &st) == 0 && (st.st_mode & 0777) == 0600,
          "the identity file was written 0600");
    zxv_net_status(d, &sd);
    CHECK(zxv_net_add_peer(c, "127.0.0.1", sd.port, now) == 0, "C pings D");
    pr = (pair_t){c, d, sc.port, sd.port};
    CHECK(wait_for(c, d, knows_both, &pr, 3000), "C and D know each other");
    zxv_net_stop(c);
    CHECK(stat(pcp, &st) == 0 && st.st_size == 12 + 38, "stop saved one cached peer");
    uint32_t acc0 = 0;
    zxv_net_status(d, &sd);
    acc0 = sd.lan_accepted;
    CHECK(zxv_net_start(c, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0, "C restarts");
    zxv_net_status(c, &sc);
    CHECK(sc.persisted && !strcmp(sc.node_id_full, id1), "with the same NodeID %.16s", id1);
    CHECK(sc.cached_peers == 1, "and pings the cached peer");
    pr.px = sc.port;
    CHECK(wait_for(c, d, knows_both, &pr, 3000),
          "which answers: they meet again with no address typed");
    {
        zxv_net_t *e = zxv_net_new();
        zxv_net_status_t se;
        zxv_net_set_lan_discovery(e, false, 0);
        zxv_net_set_state_dir(e, dir);
        CHECK(zxv_net_start(e, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0,
              "a second process on the same folder starts");
        zxv_net_status(e, &se);
        CHECK(se.state_busy && !se.persisted && strcmp(se.node_id_full, id1) != 0,
              "with a fresh NodeID: the folder is locked to C");
        zxv_net_free(e);
    }
    if (sd.lan_discovery) {
        for (int i = 0; i < 50; i++) pump(c, d, 20);
        zxv_net_status(d, &sd);
        CHECK(sd.lan_accepted > acc0 && sd.lan_refused == 0,
              "D accepts the restarted identity's LAN record: its seq moved on (%u -> %u)", acc0,
              sd.lan_accepted);
    }
    zxv_net_stop(c);
    chmod(idp, 0644);
    CHECK(zxv_net_start(c, ZXV_NET_LAN, "127.0.0.1", 0, now) == -1,
          "an identity file others can read is refused");
    zxv_net_status(c, &sc);
    CHECK(strstr(sc.error, "readable by others") != NULL, "and says why: %s", sc.error);
    chmod(idp, 0600);
    {
        FILE *f = fopen(idp, "r+b");
        fseek(f, 50, SEEK_SET);
        int ch = fgetc(f);
        fseek(f, 50, SEEK_SET);
        fputc(ch ^ 1, f);
        fclose(f);
    }
    CHECK(zxv_net_start(c, ZXV_NET_LAN, "127.0.0.1", 0, now) == -1,
          "a damaged identity file is refused, not replaced");
    zxv_net_status(c, &sc);
    CHECK(strstr(sc.error, "NodeID") != NULL, "and says why: %s", sc.error);
    {
        FILE *f = fopen(idp, "r+b");
        fseek(f, 50, SEEK_SET);
        int ch = fgetc(f);
        fseek(f, 50, SEEK_SET);
        fputc(ch ^ 1, f);
        fclose(f);
    }
    chmod(dir, 0777);
    CHECK(zxv_net_start(c, ZXV_NET_LAN, "127.0.0.1", 0, now) == -1,
          "a folder others can write is refused");
    chmod(dir, 0700);
    CHECK(zxv_net_start(c, ZXV_NET_LAN, "127.0.0.1", 0, now) == 0, "repaired, C starts again");
    zxv_net_status(c, &sc);
    CHECK(!strcmp(sc.node_id_full, id1), "still the same NodeID");

    printf("[6] U11: introductions\n");
    CHECK(zxv_net_punch(a, (const uint8_t *) id1, now) == 0, "off: nobody to ask");
    pr.px = sc.port;
    CHECK(wait_for(c, d, knows_both, &pr, 3000), "C and D are contacts");
    uint8_t ghost[32];
    memset(ghost, 0x5C, 32);
    CHECK(zxv_net_punch(c, ghost, zxv_net_wall_ms()) == 1,
          "C asks its one contact for an introduction");
    /* D reports C's address in its PONG, so C pings D: contacts made from
     * LAN records alone (the usual path on macOS) never exchanged one */
    CHECK(zxv_net_add_peer(c, "127.0.0.1", sd.port, zxv_net_wall_ms()) == 0, "C pings D");
    (void) wait_for(c, d, asked_and_voted, c, 3000);
    for (int i = 0; i < 5; i++) pump(c, d, 20); /* time for a wrong relay to show */
    zxv_net_status(c, &sc);
    zxv_net_status(d, &sd);
    CHECK(sc.punch_asked == 1 && sd.punch_relayed == 0,
          "D does not know the target, so relays nothing");
    CHECK(sc.public_votes >= 1 && sc.public_addr[0] == 0,
          "one peer's report is counted but not shown as the public address");

    printf("[7] U12: an AIPI call over Vinea, verified, and its bounty settled in both books\n");
    ai_box_t *xa = ai_new(1), *xb = ai_new(2);
    static const uint8_t MOD[] = "fake-module: isqrt";
    aipi_fn_t fn;
    memset(&fn, 0, sizeof fn);
    fn.intent = AIPI_INTENT_FN;
    fn.verify = AIPI_VERIFY_CHECKER;
    fn.privacy = AIPI_PRIV_PUBLIC;
    fn.determinism = AIPI_DET_DETERMINISTIC;
    fn.fuel_max = 1000;
    strcpy(fn.entry, "isqrt");
    strcpy(fn.checker, "isqrt_ok");
    ipfsn_cid_t mod, f1, f2;
    CHECK(aipi_put_blob(&xa->a, MOD, sizeof MOD, IPFSN_VIS_PUBLIC, &mod) == AIPI_OK &&
              aipi_put_blob(&xb->a, MOD, sizeof MOD, IPFSN_VIS_PUBLIC, &mod) == AIPI_OK,
          "both hold the module");
    fn.module = mod;
    CHECK(aipi_publish_fn(&xa->a, &fn, &f1) == AIPI_OK &&
              aipi_publish_fn(&xb->a, &fn, &f2) == AIPI_OK && ipfsn_cid_equal(&f1, &f2),
          "and the same function CID");
    CHECK(zxv_net_aipi(c) == NULL, "nothing attached yet");
    CHECK(zxv_net_attach_aipi(c, &xa->a) == 0 && zxv_net_attach_aipi(d, &xb->a) == 0 &&
              zxv_net_aipi(c) && zxv_net_aipi(d) && xa->a.mesh.broadcast,
          "attached: the contexts now have a mesh");
    zxv_net_share(d, 100, 0);
    zxv_net_aipi_serve(d, true, 1);
    uint8_t idd[32];
    zxv_net_node_id(d, idd);
    vna_id_t vd;
    memcpy(vd.b, idd, 32);
    CHECK(aipi_vna_add_executor(zxv_net_aipi(c), &vd) == AIPI_OK, "C chooses D as its executor");
    uint8_t in[8], out[8];
    uint32_t job, len = 0;
    wr64(in, 1000000);
    CHECK(zxv_net_aipi_execute(c, &f1, in, 8, false, 25, 10000, zxv_net_wall_ms(), &job) ==
              AIPI_ERR_FUNDS,
          "no budget: a bounty cannot be escrowed");
    zxv_net_aipi_budget(c, 100);
    CHECK(zxv_net_aipi_execute(c, &f1, in, 8, false, 25, 10000, zxv_net_wall_ms(), &job) == AIPI_OK,
          "with a budget, the call goes to the mesh");
    zxv_net_status(c, &sc);
    CHECK(sc.aipi && sc.aipi_escrow == 25 && sc.aipi_calls_out == 1, "25 in escrow, one call out");
    for (int i = 0; i < 150 && aipi_job(&xa->a, job)->status == AIPI_JOB_PENDING; i++)
        pump(c, d, 20);
    zxv_net_status(c, &sc);
    zxv_net_status(d, &sd);
    CHECK(aipi_result(&xa->a, job, out, 8, &len) == AIPI_OK && rd64(out) == 1000,
          "D answered; C checked it by squaring: isqrt(1000000) = 1000");
    for (int i = 0; i < 100; i++) {
        zxv_net_status(c, &sc);
        zxv_net_status(d, &sd);
        if (sc.trades >= 1 && sd.trades >= 1) break;
        pump(c, d, 20);
    }
    CHECK(sc.aipi_escrow == 0 && sd.aipi_served == 1, "escrow released to D");
    CHECK(sc.trades == 1 && sd.trades == 1 && sc.conserved && sd.conserved,
          "the bounty settled as one compute receipt in both books (trades %u / %u)", sc.trades,
          sd.trades);
    zxv_net_stop(c);
    CHECK(xa->a.mesh.broadcast == NULL, "stopped: the context is local-only again");
    CHECK(zxv_net_attach_aipi(c, NULL) == 0, "detached");

    zxv_net_free(c);
    zxv_net_free(d);
    ai_free(xa);
    ai_free(xb);
    unlink(idp);
    unlink(pcp);
    rmdir(dir);
    zxv_net_free(a);
    zxv_net_free(b);
    printf(failures ? "net host test FAILED (%d)\n" : "net host test passed\n", failures);
    return failures ? 1 : 0;
}
