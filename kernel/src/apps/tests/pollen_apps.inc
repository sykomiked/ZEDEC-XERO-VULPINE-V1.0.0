/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pollen's model against zxvd's real dispatcher: files.*, sys.disk and
 * freight.pack and freight.unpack as zxvd runs them on the scratch Hoard.
 * This in-process zxvd links no peer network, so net.* answers "not
 * available" until the test gives the dispatcher a network of its own
 * (host and view ops, zxvd_rpc.h): it answers the command lines the way
 * zxv_host.c's net_command does, and keeps what is shared and wanted for
 * net.offers. Nothing goes online: the only modes asked are lan and off,
 * and the peers are on loopback or the local network. */
#include "pollen_model.h"

static pollen_t PO;
static char po_cmd[256];       /* the last command line the network got */
static int po_cmds, po_online; /* how many, and how many asked for "online" */
static unsigned long long po_compute, po_storage, po_want_units;
static int po_wants;
static const char *PO_PEERS = "{\"peers\":[],\"count\":0}";
static uint32_t po_steps;
static uint64_t po_done, po_total;

static const char *po_net(const char *command)
{
    unsigned long long a, b;
    char res[16];
    int used = -1;
    po_cmds++;
    snprintf(po_cmd, sizeof po_cmd, "%s", command);
    if (!strcmp(command, "online")) po_online++;
    if (!strcmp(command, "lan") || !strcmp(command, "online")) return "networking is on";
    if (!strcmp(command, "off")) return "networking is off";
    if (sscanf(command, "share %llu %llu%n", &a, &b, &used) == 2 && command[used] == 0) {
        po_compute = a, po_storage = b;
        return a || b ? "sharing" : "sharing nothing";
    }
    used = -1;
    if (sscanf(command, "want %15s %llu %llu%n", res, &a, &b, &used) == 3 && command[used] == 0 &&
        (!strcmp(res, "storage") || !strcmp(res, "compute"))) {
        if (po_wants >= 16 || !a) return "refused";
        po_wants++, po_want_units = a;
        return "wanted";
    }
    return NULL;
}

static void po_peers(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, PO_PEERS, strlen(PO_PEERS));
}

static void po_offers(zxvd_buf_t *o)
{
    zxvd_buf_printf(o, "{\"share\":{\"compute\":\"%llu\",\"storage\":\"%llu\"},\"wants\":[",
                    po_compute, po_storage);
    for (int i = 0; i < po_wants; i++)
        zxvd_buf_printf(o,
                        "%s{\"slot\":%d,\"resource\":\"storage\",\"units_left\":\"%llu\","
                        "\"max_price\":\"0\"}",
                        i ? "," : "", i, po_want_units);
    zxvd_buf_raw(o, "],\"outstanding\":[]}", 19);
}

static void po_on_progress(void *ctx, uint64_t done, uint64_t total)
{
    (void) ctx;
    po_steps++;
    po_done = done;
    po_total = total;
}

static bool po_write(const char *rel, const uint8_t *p, uint32_t n)
{
    uint64_t size;
    return zxf_write(rel, p, n, ZXF_W_REPLACE, false, &size) == 0;
}

/* the whole of a file of the Hoard equals p[0..n) */
static bool po_same(const char *rel, const uint8_t *p, uint32_t n)
{
    static uint8_t buf[8192];
    uint64_t off = 0, size = 0;
    uint32_t got = 0;
    do {
        if (zxf_read(rel, off, buf + off, (uint32_t) (sizeof buf - off), false, &got, &size) != 0)
            return false;
        off += got;
    } while (got && off < size && off < sizeof buf);
    return off == n && size == n && !memcmp(buf, p, n);
}

#define PO_PEER(id, addr)                                                                          \
    "{\"id\":\"" id "\",\"addr\":\"" addr "\",\"bucket\":3,\"ubh\":true,\"last_seen_ms\":\"1\","   \
    "\"in_book\":false,\"member\":false,\"held\":\"0\",\"receivable\":\"0\",\"social\":\"0\","     \
    "\"pending\":0}"
#define PO_ID_A "aa00000000000000000000000000000000000000000000000000000000000001"
#define PO_ID_B "bb00000000000000000000000000000000000000000000000000000000000002"
#define PO_ID_C "cc00000000000000000000000000000000000000000000000000000000000003"
#define PO_ID_D "dd00000000000000000000000000000000000000000000000000000000000004"

APPS_TEST(pollen)
{
    static uint8_t a[5000], b[3361];
    static zxvd_host_ops_t NET;
    static const zxvd_view_ops_t VIEW = {.peers = po_peers, .offers = po_offers};
    uint32_t idx = 99, ticket = 0;
    int before, rc;
    for (uint32_t i = 0; i < sizeof a; i++) a[i] = (uint8_t) (i * 13u + 5u);
    for (uint32_t i = 0; i < sizeof b; i++) b[i] = (uint8_t) (255u - i * 7u);
    pollen_init(&PO, &LINK);

    /* nothing to act on: nothing asked */
    before = sent;
    CHECK(pollen_approve(&PO, 0, true, 1) == ZXM_E_ARG && pollen_withdraw(&PO, 0, 1) == ZXM_E_ARG &&
              pollen_carry(&PO, 0, 0, "pollen/x.txt", 10) == ZXM_E_ARG &&
              pollen_offer(&PO, NULL, 1, 1, &idx) == ZXM_E_ARG && !pollen_fits(&PO, 1) &&
              pollen_cross_confirm(&PO, 0) == ZXM_E_ARG && sent == before,
          "pollen: nothing drafted, nothing measured: nothing asked");

    /* PM1, PM2: this zxvd has no peer network */
    CHECK(pollen_lan(&PO, true) == ZXM_E_REMOTE && LINK.err_code == -32601 && PO.net == -1,
          "pollen: net.command refused by this zxvd: %s", LINK.err);
    zxvd_rpc_set_view_ops(NULL);
    CHECK(pollen_peers(&PO) == ZXM_E_REMOTE && LINK.err_code == -32601 && PO.npeers == 0,
          "pollen: net.peers refused: %s", LINK.err);

    /* the dispatcher's network for the rest: lan, then two nearby peers of four */
    NET = OPS;
    NET.net = po_net;
    zxvd_rpc_set_ops(&NET);
    zxvd_rpc_set_view_ops(&VIEW);
    CHECK(pollen_lan(&PO, true) == ZXM_OK && PO.net == 1 && !strcmp(po_cmd, "lan") &&
              !strcmp(PO.notice, "Pollen is on the local network."),
          "pollen: on the local network: %s", LINK.err);
    PO_PEERS = "{\"peers\":[" PO_PEER(PO_ID_A, "192.168.1.7:8723") "," PO_PEER(
        PO_ID_B, "127.0.0.1:9000") "," PO_PEER(PO_ID_C, "8.8.8.8:8723") "," PO_PEER(PO_ID_D,
                                                                                    "") "],"
                                                                                        "\"count\":"
                                                                                        "4}";
    CHECK(pollen_peers(&PO) == ZXM_OK && PO.npeers == 4 && PO.nearby == 2 && PO.peer[0].nearby &&
              PO.peer[0].scope == HEDGEROW_SCOPE_PRIVATE && PO.peer[1].nearby &&
              PO.peer[1].scope == HEDGEROW_SCOPE_LOOPBACK && !PO.peer[2].nearby &&
              PO.peer[2].scope == HEDGEROW_SCOPE_PUBLIC && !PO.peer[3].nearby &&
              PO.peer[3].scope == HEDGEROW_SCOPE_NONE &&
              !strcmp(PO.notice, "2 nearby devices of 4 peers."),
          "pollen: two nearby of four (hedgerow.addr_scope): %s", PO.notice);
    PO_PEERS = "{\"peers\":[" PO_PEER(PO_ID_A, "300.1.1.1:8723") "],\"count\":1}";
    CHECK(pollen_peers(&PO) == ZXM_E_PROTOCOL && PO.npeers == 4 && PO.nearby == 2,
          "pollen: a peer list that does not parse leaves the last one");

    /* PM3: offers, drafted, then opened only on the owner's word */
    CHECK(zxf_mkdir("pollen") == 0 && zxf_mkdir("pollen/got") == 0 &&
              zxf_mkdir("pollen/dir") == 0 && po_write("pollen/a.txt", a, sizeof a) &&
              po_write("pollen/b.bin", b, sizeof b) && po_write("pollen/empty.txt", a, 0),
          "pollen: three files in the Hoard");
    const char *ab[2] = {"pollen/a.txt", "pollen/b.bin"};
    const char *one[1] = {"pollen/a.txt"};
    const char *empty[2] = {"pollen/a.txt", "pollen/empty.txt"};
    const char *dir[1] = {"pollen/dir"};
    const char *many[17];
    for (uint32_t i = 0; i < 17; i++) many[i] = "pollen/a.txt";
    before = sent;
    CHECK(pollen_offer(&PO, many, 17, 1, &idx) == ZXM_E_ARG &&
              pollen_offer(&PO, ab, 0, 1, &idx) == ZXM_E_ARG &&
              pollen_offer(&PO, ab, 2, -1, &idx) == ZXM_E_ARG && sent == before && PO.noffers == 0,
          "pollen: 17 files, none, a time that is no time: nothing asked");
    CHECK(pollen_offer(&PO, empty, 2, 1, &idx) == ZXM_E_ARG && PO.fault == POLLEN_F_LIMITS &&
              pollen_offer(&PO, dir, 1, 1, &idx) == ZXM_E_ARG && PO.fault == POLLEN_F_FILE &&
              pollen_offer(&PO, ab, 2, POLLEN_STAMP_MAX + 1, &idx) == ZXM_E_ARG &&
              PO.fault == POLLEN_F_RECORD && PO.noffers == 0,
          "pollen: an empty file (PR3), a folder, a stamp past 15 digits (PR4): refused");
    CHECK(pollen_offer(&PO, ab, 2, INT64_C(1760000000000), &idx) == ZXM_OK && idx == 0 &&
              PO.offer[0].number == 1 && PO.offer[0].nfiles == 2 &&
              PO.offer[0].bytes == (int64_t) (sizeof a + sizeof b) &&
              PO.offer[0].largest == (int64_t) sizeof a && PO.offer[0].state == POLLEN_DRAFTED &&
              pollen_count(&PO, POLLEN_DRAFTED) == 1 && pollen_count(&PO, POLLEN_OPEN) == 0,
          "pollen: offer 1 drafted: %s", PO.notice);
    po_compute = 5;
    before = po_cmds;
    CHECK(pollen_approve(&PO, 0, false, 2) == ZXM_E_STATE && PO.fault == POLLEN_F_WORD &&
              PO.offer[0].state == POLLEN_DRAFTED && po_cmds == before &&
              pollen_carry(&PO, 0, 0, "pollen/got/x.txt", 10) == ZXM_E_STATE,
          "pollen: no word from the owner: still a draft, nothing shared, nothing carried");
    rc = pollen_approve(&PO, 0, true, INT64_C(1760000000001));
    CHECK(rc == ZXM_OK && PO.offer[0].state == POLLEN_OPEN && !strcmp(po_cmd, "share 5 8361") &&
              po_storage == 8361 && po_compute == 5 && PO.share_storage == 8361 &&
              PO.offer[0].stamp == INT64_C(1760000000001) && pollen_count(&PO, POLLEN_OPEN) == 1,
          "pollen: opened, 8361 bytes shared, the compute share kept: %d %s", rc, LINK.err);
    CHECK(pollen_approve(&PO, 0, true, 3) == ZXM_E_STATE, "pollen: only a draft opens");

    /* PR3: at most 8 open; a ninth is refused before it is drafted */
    for (uint32_t i = 1; i < POLLEN_OPEN_MAX; i++)
        CHECK(pollen_offer(&PO, one, 1, 10 + i, &idx) == ZXM_OK && idx == i &&
                  pollen_approve(&PO, idx, true, 20 + i) == ZXM_OK,
              "pollen: offer %u open", i + 1u);
    CHECK(pollen_count(&PO, POLLEN_OPEN) == POLLEN_OPEN_MAX && po_storage == 8361u + 7u * sizeof a,
          "pollen: 8 open, every byte shared: %llu", po_storage);
    CHECK(pollen_offer(&PO, one, 1, 40, &idx) == ZXM_E_ARG && PO.fault == POLLEN_F_LIMITS &&
              PO.noffers == POLLEN_OPEN_MAX,
          "pollen: a ninth open offer refused (PR3)");
    CHECK(pollen_withdraw(&PO, 7, 41) == ZXM_OK && PO.offer[7].state == POLLEN_WITHDRAWN &&
              po_storage == 8361u + 6u * sizeof a && pollen_withdraw(&PO, 7, 42) == ZXM_E_STATE &&
              pollen_count(&PO, POLLEN_WITHDRAWN) == 1,
          "pollen: one withdrawn, its bytes no longer shared: %llu", po_storage);
    CHECK(pollen_offer(&PO, one, 1, 43, &idx) == ZXM_OK && idx == 8 && PO.offer[8].number == 9 &&
              pollen_approve(&PO, 8, true, 44) == ZXM_OK,
          "pollen: room for one more once one is withdrawn");
    CHECK(pollen_read_offers(&PO) == ZXM_OK && PO.share_compute == 5 &&
              PO.share_storage == 8361u + 7u * sizeof a && PO.wants == 0,
          "pollen: what zxvd shares now (net.offers)");

    /* PM4: wanting bytes from nearby devices */
    static pollen_t R;
    pollen_init(&R, &LINK);
    before = po_cmds;
    CHECK(pollen_want(&R, 4096, false) == ZXM_E_STATE && R.fault == POLLEN_F_WORD &&
              pollen_want(&R, 4096, true) == ZXM_E_STATE && R.fault == POLLEN_F_NOBODY &&
              pollen_want(&R, 0, true) == ZXM_E_ARG && po_cmds == before,
          "pollen: no word, nobody nearby, nothing wanted: nothing asked");
    CHECK(pollen_peers(&R) == ZXM_E_PROTOCOL && pollen_set_avail(&R, -1) == ZXM_E_ARG &&
              pollen_set_spare(&R, FERRY_BYTES_MAX + 1) == ZXM_E_ARG,
          "pollen: a bad peer list, a room that is no room");
    PO_PEERS = "{\"peers\":[" PO_PEER(PO_ID_A, "169.254.3.4:8723") "],\"count\":1}";
    CHECK(pollen_peers(&R) == ZXM_OK && R.nearby == 1 && R.peer[0].scope == HEDGEROW_SCOPE_LINK &&
              pollen_set_spare(&R, 1000) == ZXM_OK && pollen_set_avail(&R, 5095) == ZXM_OK &&
              pollen_want(&R, 4096, true) == ZXM_E_STATE && R.fault == POLLEN_F_NO_ROOM &&
              po_cmds == before,
          "pollen: 4096 bytes and 1000 kept free do not fit in 5095 (ferry.target_fits)");
    CHECK(pollen_set_avail(&R, 5096) == ZXM_OK && pollen_want(&R, 4096, true) == ZXM_OK &&
              !strcmp(po_cmd, "want storage 4096 0") && R.wanted == 4096 &&
              pollen_read_offers(&R) == ZXM_OK && R.wants == 1,
          "pollen: exactly fits: wanted, at a price of 0: %s", po_cmd);
    CHECK(pollen_measure(&R) == ZXM_OK && R.avail > 0, "pollen: the room here (sys.disk): %lld",
          (long long) R.avail);

    /* PM5: carried in freights, each rebuilt from its last k packets */
    zxm_link_progress(&LINK, po_on_progress, NULL);
    CHECK(pollen_carry(&PO, 0, 0, "pollen/got/a.txt", 10) == ZXM_E_STATE &&
              PO.fault == POLLEN_F_NO_ROOM,
          "pollen: not measured: it does not fit");
    CHECK(pollen_set_avail(&PO, POLLEN_SPARE + 4999) == ZXM_OK &&
              pollen_carry(&PO, 0, 0, "pollen/got/a.txt", 10) == ZXM_E_STATE &&
              PO.fault == POLLEN_F_NO_ROOM &&
              pollen_carry(&PO, 0, 2, "pollen/got/a.txt", 10) == ZXM_E_ARG &&
              pollen_carry(&PO, 0, 0, "pollen/got/a.txt", 169) == ZXM_E_ARG &&
              pollen_carry(&PO, 0, 0, "", 10) == ZXM_E_ARG,
          "pollen: a byte short, no such file, k past 168, no path: refused");
    CHECK(pollen_set_avail(&PO, POLLEN_SPARE + 5000) == ZXM_OK, "pollen: exactly room");
    po_steps = 0;
    rc = pollen_carry(&PO, 0, 0, "pollen/got/a.txt", 10);
    CHECK(rc == ZXM_OK && PO.carried && PO.freights == 25 && PO.packets == 250 && PO.left == 0 &&
              PO.progress_x100 == 10000 && po_steps == 25 && po_done == 250 && po_total == 250 &&
              po_same("pollen/got/a.txt", a, sizeof a),
          "pollen: 5000 bytes at k 10: 25 freights, 250 packets, parity only, byte for byte: %d %s",
          rc, LINK.err);
    rc = pollen_carry(&PO, 0, 1, "pollen/got/b.bin", 84);
    CHECK(rc == ZXM_OK && PO.freights == 3 && PO.packets == 252 &&
              po_same("pollen/got/b.bin", b, sizeof b),
          "pollen: 3361 bytes at k 84: three freights from parity rows alone: %d %s", rc, LINK.err);
    rc = pollen_carry(&PO, 0, 1, "pollen/got/b168.bin", 168);
    CHECK(rc == ZXM_OK && PO.freights == 2 && PO.packets == 336 &&
              po_same("pollen/got/b168.bin", b, sizeof b),
          "pollen: at k 168, every row data: %d %s", rc, LINK.err);
    rc = pollen_carry(&PO, 0, 1, "pollen/got/b.bin", 168);
    CHECK(rc == ZXM_E_REMOTE && !PO.carried && po_same("pollen/got/b.bin", b, sizeof b),
          "pollen: never over a file that is there: %s", LINK.err);
    CHECK(pollen_carry(&PO, 7, 0, "pollen/got/w.txt", 10) == ZXM_E_STATE,
          "pollen: a withdrawn offer is not carried");
    zxm_link_progress(&LINK, NULL, NULL);

    /* PM6: out of a space only through files.cross */
    CHECK(raw_call("spaces.create", "{\"kind\":\"office\",\"name\":\"Pollen Hold\"}") == ZXM_OK,
          "pollen: an office: %s", LINK.err);
    CHECK(pollen_cross(&PO, "", "pollen/got/a.txt", "pollen-hold", "a.txt", &ticket) == ZXM_OK &&
              ticket > 0,
          "pollen: into a more open space: held (%u) %s", ticket, LINK.err);
    CHECK(pollen_cross_confirm(&PO, ticket) == ZXM_OK &&
              pollen_cross_confirm(&PO, ticket) == ZXM_E_REMOTE,
          "pollen: confirmed by the owner, only once: %s", LINK.err);
    CHECK(pollen_cross(&PO, "pollen-hold", "a.txt", "", "pollen/back.txt", &ticket) == ZXM_OK &&
              ticket == 0 && po_same("pollen/back.txt", a, sizeof a),
          "pollen: back home at once, byte for byte: %s", LINK.err);
    CHECK(pollen_cross(&PO, "", "", "home", "x.txt", &ticket) == ZXM_E_ARG &&
              pollen_cross(&PO, "", "pollen/a.txt", "home", "x.txt", NULL) == ZXM_E_ARG,
          "pollen: no path, no ticket: refused");

    /* PM1: off again; never online */
    CHECK(pollen_lan(&PO, false) == ZXM_OK && PO.net == 0 && !strcmp(po_cmd, "off") &&
              po_online == 0,
          "pollen: networking off; online was never asked");
    zxvd_rpc_set_view_ops(NULL);
    zxvd_rpc_set_ops(&OPS);
}
