/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Hedgerow's model against zxvd's real dispatcher (LINK). sys.info and
 * features.get are zxvd's own. net.command, peer.*, phone.* and net.peers
 * are served by hosts zxvd takes as ops (zxv_host.c, zxv_peer_host.c,
 * zxvd_phone.c, zxv_net_host.c), none linked into this test, so small hosts
 * here answer in their shapes (api/zxvd.openrpc.json), set as zxvd's ops;
 * the requests still go through zxvd_rpc_handle_line. pspace.* and irc.*
 * (zxvd_pspace.c, zxvd_irc.c) are not linked either and take no ops, so
 * their replies are read through FAKE, one canned object that holds every
 * part's shape. */
#include "hedgerow_model.h"

static char HR_CMD[32];
static uint32_t HR_NETS;
static bool HR_NET_FAIL;

static const char *hr_net(const char *command)
{
    HR_NETS++;
    snprintf(HR_CMD, sizeof HR_CMD, "%s", command);
    if (!strcmp(command, "off")) return "networking is off";
    if (!strcmp(command, "lan") || !strcmp(command, "online"))
        return HR_NET_FAIL ? "could not start" : "networking is on";
    return NULL;
}

static bool HR_SERVING;
static uint64_t HR_PRICE;

static void hr_status(zxvd_buf_t *out)
{
    zxvd_buf_printf(out,
                    "{\"serving\":%s,\"port\":8723,\"price_minor\":\"%llu\",\"currency\":\"VFV\","
                    "\"wallet_minor\":\"0\",\"sold\":0,\"bought\":0,\"refused\":0}",
                    HR_SERVING ? "true" : "false", (unsigned long long) HR_PRICE);
}

static const char *hr_ask(const char *peer, const char *prompt, uint64_t max, zxvd_buf_t *out)
{
    (void) peer, (void) prompt, (void) max, (void) out;
    return "not here";
}

static const char *hr_serve(bool on, uint64_t price, zxvd_buf_t *out)
{
    if (on && price > 1000000000000ull) return "price too high";
    HR_SERVING = on;
    HR_PRICE = on ? price : 0;
    hr_status(out);
    return NULL;
}

static bool HR_PHONE_ON, HR_BAD_PHONE;
static int32_t HR_PHONE_PORT = 47410;
static char HR_BIND[48];

static const char *hr_link(bool on, int32_t port, const char *bind_ip, zxvd_buf_t *out)
{
    HR_PHONE_ON = on;
    if (on) HR_PHONE_PORT = port < 0 ? 47410 : port == 0 ? 50123 : port;
    snprintf(HR_BIND, sizeof HR_BIND, "%s", bind_ip ? bind_ip : "");
    zxvd_buf_printf(out, "{\"on\":%s,\"port\":%d}", on ? "true" : "false", (int) HR_PHONE_PORT);
    return NULL;
}

static const char *hr_pair(const char *name, bool allow_post, zxvd_buf_t *out)
{
    (void) name, (void) allow_post, (void) out;
    return "not here";
}

static void hr_list(zxvd_buf_t *out)
{
    zxvd_buf_printf(
        out,
        "{\"on\":%s,\"port\":%d,\"phones\":["
        "{\"phone_id\":\"0123456789abcdef0123456789abcdef\",\"name\":\"Mira's phone\","
        "\"allow_post\":false,\"connected\":1},"
        "{\"phone_id\":\"%s\",\"name\":\"Spare\",\"allow_post\":true,\"connected\":0}]}",
        HR_PHONE_ON ? "true" : "false", (int) HR_PHONE_PORT,
        HR_BAD_PHONE ? "XYZ" : "ffffffffffffffffffffffffffffffff");
}

static const char *hr_revoke(const char *phone_id, zxvd_buf_t *out)
{
    (void) phone_id, (void) out;
    return "not here";
}

/* the routing table: one peer on this machine, one on the local network,
 * one beyond it, one with no IPv4 address and one in 224/3 */
static void hr_peers(zxvd_buf_t *out)
{
    zxvd_buf_printf(out,
                    "{\"peers\":[{\"addr\":\"127.0.0.1:8723\"},{\"addr\":\"192.168.1.7:8723\"},"
                    "{\"addr\":\"8.8.4.4:8723\"},{\"addr\":\"\"},{\"addr\":\"224.0.0.9:1\"}],"
                    "\"count\":5}");
}

static const zxvd_host_ops_t HR_OPS = {
    .state = h_state, .ask = h_ask, .model = h_model, .net = hr_net, .app_version = "test"};
static const zxvd_peer_ops_t HR_PEER = {hr_ask, hr_serve, hr_status};
static const zxvd_phone_ops_t HR_PHONE = {hr_link, hr_pair, hr_list, hr_revoke};
static const zxvd_view_ops_t HR_VIEW = {hr_peers, NULL, NULL};

static hedgerow_t HR, HR2;

APPS_TEST(hedgerow)
{
    /* the rules as the model puts them together (HR4, HR5, HM4) */
    static const uint8_t v6_lo[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const uint8_t v6_odd[16] = {0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const uint8_t v6_map[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 168, 0, 9};
    static const uint8_t v6_ula[16] = {0xfd, 0x12, 0x34, 0x56, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const uint8_t v6_ll[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const uint8_t v6_pub[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    CHECK(hedgerow_scope_of("192.168.1.5") == HEDGEROW_SCOPE_PRIVATE &&
              hedgerow_scope_of("127.0.0.1:8723") == HEDGEROW_SCOPE_LOOPBACK &&
              hedgerow_scope_of("169.254.3.3") == HEDGEROW_SCOPE_LINK &&
              hedgerow_scope_of("8.8.8.8") == HEDGEROW_SCOPE_PUBLIC &&
              hedgerow_scope_of("0.0.0.0") == HEDGEROW_SCOPE_NONE,
          "HR4: dotted addresses scoped");
    CHECK(hedgerow_scope_of("256.1.1.1") == HEDGEROW_REFUSED &&
              hedgerow_scope_of("1.1.1") == HEDGEROW_REFUSED &&
              hedgerow_scope_of("1.1.1.1:65536") == HEDGEROW_REFUSED &&
              hedgerow_scope_of("1.1.1.1:") == HEDGEROW_REFUSED &&
              hedgerow_scope_of("1.1.1.1x") == HEDGEROW_REFUSED &&
              hedgerow_scope_of("0001.1.1.1") == HEDGEROW_REFUSED &&
              hedgerow_scope_of("") == HEDGEROW_REFUSED &&
              hedgerow_scope_of(NULL) == HEDGEROW_REFUSED,
          "HR4: text that is no address is refused");
    CHECK(hedgerow_scope6(v6_lo) == HEDGEROW_SCOPE_LOOPBACK &&
              hedgerow_scope6(v6_odd) == HEDGEROW_SCOPE_NONE &&
              hedgerow_scope6(v6_map) == HEDGEROW_SCOPE_PRIVATE &&
              hedgerow_scope6(v6_ula) == HEDGEROW_SCOPE_UNIQUE &&
              hedgerow_scope6(v6_ll) == HEDGEROW_SCOPE_LINK &&
              hedgerow_scope6(v6_pub) == HEDGEROW_SCOPE_PUBLIC && hedgerow_scope6(NULL) == -1,
          "HM4: IPv6 scoped from its 16 octets; ::1 only when the middle is 0");
    CHECK(hedgerow_reach(1, 2, 2, 2) && !hedgerow_reach(1, 1, 2, 2) &&
              !hedgerow_reach(1, 2, 1, 2) && hedgerow_reach(1, 0, 0, 0) &&
              !hedgerow_reach(0, 2, 2, 0) && !hedgerow_reach(1, 3, 2, 2),
          "HR1-HR3: reach from the ladders");

    hedgerow_init(&HR, &LINK);
    CHECK(HR.mode == HEDGEROW_OFF && !HR.nrules && !HR.serve_known && !HR.notice[0] &&
              hedgerow_open_count(&HR, 0) == 0,
          "init: off, nothing known, nothing open");

    /* HM4 on the bare dispatcher: what zxvd does not serve stays unknown */
    zxvd_rpc_set_peer_ops(NULL);
    zxvd_rpc_set_phone_ops(NULL);
    zxvd_rpc_set_view_ops(NULL);
    int hr_rc = hedgerow_refresh(&HR);
    CHECK(hr_rc == ZXM_OK && HR.now_ms > 0 && !HR.serve_known && !HR.phone_known &&
              !HR.peers_known && HR.features_known && HR.nfeatures > 0 &&
              HR.exposed <= HR.nfeatures,
          "refresh: the switches read; peers, phones, serving unknown (rc %d, %u switches, "
          "%u on, spaces %d, irc %d: %s)",
          hr_rc, HR.nfeatures, HR.exposed, (int) HR.spaces_known, (int) HR.irc_known, LINK.err);

    /* HM2/HM3: opening is asked first, nothing sent without the owner */
    zxvd_rpc_set_ops(&HR_OPS);
    zxvd_rpc_set_peer_ops(&HR_PEER);
    zxvd_rpc_set_phone_ops(&HR_PHONE);
    zxvd_rpc_set_view_ops(&HR_VIEW);
    HR_NETS = 0;
    CHECK(hedgerow_set_mode(&HR, HEDGEROW_LAN, false) == ZXM_E_STATE && HR_NETS == 0 &&
              HR.mode == HEDGEROW_OFF && strstr(HR.notice, "waits for your word") &&
              strstr(HR.notice, "local network") && (HR.asked >> HEDGEROW_P_LAN & 1u) && !HR.nrules,
          "lan without the owner's word: asked, nothing sent: %s", HR.notice);
    CHECK(hedgerow_set_mode(&HR, 3, true) == ZXM_E_ARG &&
              hedgerow_set_mode(&HR, -1, true) == ZXM_E_ARG,
          "a mode off the ladder is refused");
    CHECK(hedgerow_set_mode(&HR, HEDGEROW_LAN, true) == ZXM_OK && !strcmp(HR_CMD, "lan") &&
              HR.mode == HEDGEROW_LAN && HR.nrules == 1 && HR.rule[0].path == HEDGEROW_P_LAN &&
              HR.rule[0].open == 1 && HR.rule[0].seq == 1 && HR.rule[0].stamp > 1600000000,
          "lan on the owner's word: sent and filed (%s)", LINK.err);
    CHECK(hedgerow_reachable(&HR, HEDGEROW_P_LAN, 1) &&
              !hedgerow_reachable(&HR, HEDGEROW_P_LAN, 2) &&
              !hedgerow_reachable(&HR, HEDGEROW_P_ONLINE, 2),
          "on the local network: a neighbour reaches the mesh, the internet does not");
    HR_NETS = 0;
    CHECK(hedgerow_set_mode(&HR, HEDGEROW_ONLINE, false) == ZXM_E_STATE && HR_NETS == 0 &&
              hedgerow_set_mode(&HR, HEDGEROW_ONLINE, true) == ZXM_OK &&
              !strcmp(HR_CMD, "online") && HR.nrules == 2 && HR.rule[1].path == HEDGEROW_P_ONLINE &&
              HR.rule[1].seq == 2,
          "online: asked, then on the owner's word");
    CHECK(hedgerow_reachable(&HR, HEDGEROW_P_ONLINE, 2) && hedgerow_open_count(&HR, 2) == 1 &&
              hedgerow_open_count(&HR, 1) == 2,
          "online: one path from the internet, two from the local network");

    /* HM5: answering other nodes, at the owner's price */
    CHECK(hedgerow_serve(&HR, true, 5, false) == ZXM_E_STATE && !HR_SERVING &&
              strstr(HR.notice, "answering other nodes"),
          "serving without the owner's word: asked, nothing sent");
    CHECK(hedgerow_serve(&HR, true, HEDGEROW_PRICE_MAX + 1u, true) == ZXM_E_ARG,
          "a price past 18 digits is refused");
    CHECK(hedgerow_serve(&HR, true, 2000000000000ull, true) == ZXM_E_REMOTE &&
              strstr(LINK.err, "price too high") && !HR.serving && HR.nrules == 2,
          "zxvd's refusal changes nothing: %s", LINK.err);
    CHECK(hedgerow_serve(&HR, true, 5, true) == ZXM_OK && HR_SERVING && HR.serving &&
              HR.serve_price == 5 && HR.serve_port == 8723 && HR.nrules == 3 &&
              HR.rule[2].path == HEDGEROW_P_SERVE && HR.rule[2].port == 8723 &&
              hedgerow_reachable(&HR, HEDGEROW_P_SERVE, 2),
          "serving on the owner's word, reachable from the internet");
    CHECK(hedgerow_serve(&HR, false, 0, false) == ZXM_OK && !HR_SERVING && !HR.serving &&
              (HR.revoked >> HEDGEROW_P_SERVE & 1u) && HR.rule[3].open == 0,
          "closing is at once, and revokes the grant");

    /* closing the network is at once: both steps filed */
    HR_NETS = 0;
    CHECK(hedgerow_set_mode(&HR, HEDGEROW_OFF, false) == ZXM_OK && HR_NETS == 1 &&
              !strcmp(HR_CMD, "off") && HR.mode == HEDGEROW_OFF && HR.nrules == 6 &&
              HR.rule[4].path == HEDGEROW_P_LAN && HR.rule[4].open == 0 &&
              HR.rule[5].path == HEDGEROW_P_ONLINE && HR.rule[5].open == 0 &&
              hedgerow_open_count(&HR, 1) == 0 && hedgerow_open_count(&HR, 0) == 0,
          "off at once, nothing reachable");
    CHECK(hedgerow_serve(&HR, true, 5, true) == ZXM_E_STATE && !HR_SERVING,
          "no serving while the network is off");
    HR_NET_FAIL = true;
    CHECK(hedgerow_set_mode(&HR, HEDGEROW_ONLINE, true) == ZXM_E_REMOTE &&
              !strcmp(LINK.err, "could not start") && HR.mode == HEDGEROW_OFF && HR.nrules == 6,
          "zxvd could not start: the mode stays off, nothing filed");
    HR_NET_FAIL = false;

    /* the phone link: private addresses only, asked first, off at once */
    CHECK(hedgerow_phone(&HR, true, -1, "8.8.8.8", true) == ZXM_E_ARG &&
              hedgerow_phone(&HR, true, -1, "192.168.1.5:80", true) == ZXM_E_ARG &&
              hedgerow_phone(&HR, true, -1, "127.0.0.1", true) == ZXM_E_ARG &&
              hedgerow_phone(&HR, true, 65536, NULL, true) == ZXM_E_ARG &&
              hedgerow_phone(&HR, true, -2, NULL, true) == ZXM_E_ARG && !HR_PHONE_ON,
          "phone link: a public, loopback or ported bind, or a bad port, is refused");
    CHECK(hedgerow_phone(&HR, true, 0, "192.168.1.5", false) == ZXM_E_STATE && !HR_PHONE_ON &&
              strstr(HR.notice, "the phone link"),
          "phone link without the owner's word: asked");
    CHECK(hedgerow_phone(&HR, true, 0, "192.168.1.5", true) == ZXM_OK && HR_PHONE_ON &&
              !strcmp(HR_BIND, "192.168.1.5") && HR.phone_on && HR.phone_port == 50123 &&
              hedgerow_reachable(&HR, HEDGEROW_P_PHONE, 1) &&
              !hedgerow_reachable(&HR, HEDGEROW_P_PHONE, 2) && HR.rule[6].port == 50123,
          "phone link on: phones on the local network only");

    /* HM4 with every host: the routing table by distance, the phones */
    CHECK(hedgerow_refresh(&HR) == ZXM_OK && HR.serve_known && !HR.serving && HR.phone_known &&
              HR.phone_on && HR.nphones == 2 && !strcmp(HR.phone[0].name, "Mira's phone") &&
              HR.phone[1].allow_post && HR.peers_known && HR.peers_far[0] == 1 &&
              HR.peers_far[1] == 1 && HR.peers_far[2] == 1 && HR.peers_unscoped == 2,
          "refresh: serving, two phones, peers by distance (%s)", LINK.err);
    HR_BAD_PHONE = true;
    int64_t before = HR.now_ms;
    CHECK(hedgerow_refresh(&HR) == ZXM_E_PROTOCOL && HR.now_ms == before && HR.nphones == 2 &&
              HR.phone_known,
          "a phone id that is no id: what was shown stays");
    HR_BAD_PHONE = false;
    CHECK(hedgerow_phone(&HR, false, -1, NULL, false) == ZXM_OK && !HR_PHONE_ON && !HR.phone_on &&
              hedgerow_open_count(&HR, 1) == 0,
          "phone link off at once");

    /* HM2: the journal keeps the newest HEDGEROW_RULES, each a rule record */
    for (uint32_t i = 0; i < 20u; i++)
        CHECK(hedgerow_phone(&HR, true, 0, NULL, true) == ZXM_OK &&
                  hedgerow_phone(&HR, false, -1, NULL, false) == ZXM_OK,
              "phone link round %u", i);
    bool all_ok = HR.nrules == HEDGEROW_RULES;
    for (uint32_t i = 0; all_ok && i < HR.nrules; i++)
        all_ok = hedgerow_rule_ok(HR.rule[i].seq, HR.rule[i].path, HR.rule[i].open, HR.rule[i].port,
                                  HR.rule[i].stamp) &&
                 (i == 0 || HR.rule[i].seq == HR.rule[i - 1].seq + 1);
    CHECK(all_ok && HR.seq == 48 && HR.rule[HEDGEROW_RULES - 1u].seq == 48 && HR.rule[0].seq == 17,
          "journal: 32 records kept, in order, each a rule record (seq %lld)", (long long) HR.seq);

    zxvd_rpc_set_ops(&OPS);
    zxvd_rpc_set_peer_ops(NULL);
    zxvd_rpc_set_phone_ops(NULL);
    zxvd_rpc_set_view_ops(NULL);

    /* pspace.list and irc.status: one canned object holds every part's shape */
    zxm_link_init(&FAKE, canned, &FAKE);
    hedgerow_init(&HR2, &FAKE);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":1700000000000,"
             "\"serving\":false,\"port\":6697,\"price_minor\":\"0\",\"on\":true,\"phones\":[],"
             "\"features\":[{\"name\":\"pspace\",\"enabled\":true,\"regulated\":false,"
             "\"groups\":[{\"note\":\"spaces\",\"methods\":9}]}],\"online\":true,"
             "\"spaces\":[{\"phase\":\"in\"},{\"phase\":\"joining\"}],\"listening\":true,"
             "\"clients\":2,\"peers\":[]}}";
    CHECK(hedgerow_refresh(&HR2) == ZXM_OK && HR2.spaces_known && HR2.nspaces == 2 &&
              HR2.spaces_in == 1 && HR2.irc_known && HR2.irc_on && HR2.irc_listening &&
              HR2.irc_clients == 2 && HR2.exposed == 1 && HR2.feature[0].methods == 9,
          "refresh: one space hosted, the gateway listening (%s)", FAKE.err);
    HR2.mode = HEDGEROW_ONLINE;
    CHECK(hedgerow_reachable(&HR2, HEDGEROW_P_IRC, 0) &&
              !hedgerow_reachable(&HR2, HEDGEROW_P_IRC, 1) &&
              hedgerow_reachable(&HR2, HEDGEROW_P_SPACES, 2),
          "the IRC gateway answers this machine only; a hosted space the internet");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":1700000000000,"
             "\"serving\":false,\"port\":0,\"price_minor\":\"0\",\"on\":false,\"phones\":[],"
             "\"features\":[],\"online\":false,\"spaces\":[{\"phase\":\"elsewhere\"}],"
             "\"available\":false,\"peers\":[]}}";
    CHECK(hedgerow_refresh(&HR2) == ZXM_E_PROTOCOL && HR2.nspaces == 2 && HR2.irc_known,
          "a space in no phase: what was shown stays");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":1700000000000,"
             "\"serving\":false,\"port\":0,\"price_minor\":\"0\",\"on\":false,\"phones\":[],"
             "\"features\":[],\"online\":false,\"spaces\":[],\"available\":false,\"peers\":[]}}";
    CHECK(hedgerow_refresh(&HR2) == ZXM_OK && !HR2.irc_known && HR2.nspaces == 0 &&
              !hedgerow_reachable(&HR2, HEDGEROW_P_IRC, 0),
          "a zxvd without the gateway: unknown, never reachable");
    CANNED = NULL;
}
