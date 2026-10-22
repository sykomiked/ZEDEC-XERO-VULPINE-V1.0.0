/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pavise's model against zxvd's real dispatcher (LINK): features.get and
 * sys.net as zxvd answers them, passkeys refused while their
 * switch is off, the journal on disk (journal.query, journal.verify). This
 * in-process zxvd links no device mesh, no passkey store and no routing
 * host (net.peers), so the roster, the credentials and the peers are
 * answered in their shapes on a link of its own (PV_LINK), which
 * passes every other request to the real dispatcher. The ids are fixed
 * test values, public ids, never a key's bytes. Then policy A allows app
 * 12 the network and policy B denies it: BOTH, denied until the owner
 * rules. */
#include "pavise_model.h"

static pavise_t PV;
static zxm_link_t PV_LINK;
static int pv_calls;
static const char *PV_KEYS, *PV_DEVICES, *PV_PEERS;

static int32_t pv_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    pv_calls++;
    if (strstr(req, "\"passkeys.list\"")) r = PV_KEYS;
    if (strstr(req, "\"devmesh.devices\"")) r = PV_DEVICES;
    if (strstr(req, "\"net.peers\"")) r = PV_PEERS;
    if (!r) return inproc(NULL, req, len, resp, cap);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define PV_K1   "00112233445566778899aabbccddeeff"
#define PV_K2   "ffeeddccbbaa99887766554433221100"
#define PV_K3   "0123456789abcdef0123456789abcdef"
#define PV_USER "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
#define PV_KEY(id, revoked, reg)                                                                   \
    "{\"cred_id\":\"" id "\",\"user\":\"" PV_USER "\",\"rp_id\":\"zxv.local\",\"sign_count\":3,"   \
    "\"revoked\":" revoked ",\"registered\":" reg ",\"last_used\":null}"
#define PV_DEV(id, status, self)                                                                   \
    "{\"id\":\"" id "\",\"name\":\"d\",\"role\":\"home\",\"admin\":true,\"held\":false,"           \
    "\"status\":\"" status "\",\"self\":" self ",\"up\":true,\"since\":\"1700000000\"}"

/* now: 1700000000 + 10 days; K1 registered then (365 days: 355 left),
 * K2 340 days before (15 left), K3 revoked */
#define PV_T0  1700000000
#define PV_NOW (PV_T0 + 10 * 86400)

static const char *const PV_KEYS3 =
    "{\"credentials\":[" PV_KEY(PV_K1, "false", "1700000000") "," PV_KEY(
        PV_K2, "false", "1670624000") "," PV_KEY(PV_K3, "true", "1700000000") "]}";
static const char *const PV_ROSTER = "{\"roster_version\":\"2\",\"devices\":[" PV_DEV(
    "00112233445566778899aabbccddeeff", "active",
    "true") "," PV_DEV("fedcba9876543210fedcba9876543210", "revoked", "false") "]}";

#define PV_PEER(addr)                                                                              \
    "{\"id\":\"" PV_K1 PV_K1 "\",\"addr\":\"" addr "\",\"bucket\":3,\"ubh\":false,"                \
    "\"last_seen_ms\":\"1700000000000\",\"in_book\":false,\"member\":false,\"held\":\"0\","        \
    "\"receivable\":\"0\",\"social\":\"0\",\"pending\":0}"
/* one peer on the private network, one beyond, one with no IPv4 address */
static const char *const PV_PEERS3 = "{\"peers\":[" PV_PEER("192.168.1.20:4001") "," PV_PEER(
    "8.8.8.8:4001") "," PV_PEER("") "],\"count\":3}";

APPS_TEST(pavise)
{
    lpres_state_t v = LPRES_STATE_NEITHER;
    int64_t score = -1;
    char jdir[256];

    /* the rules */
    CHECK(pavise_allow(1, 1, 1, 1, 0) && !pavise_allow(1, 1, 1, 0, 0) &&
              !pavise_allow(1, 1, 1, 30, 1) && !pavise_allow(2, 1, 1, 30, 0) &&
              pavise_clause(1, 2, 0) == LPRES_STATE_BOTH &&
              pavise_clause(0, 0, 0) == LPRES_STATE_TRUE && pavise_far_of("8.8.8.8:4001") == 2 &&
              pavise_far_of("192.168.1.9") == 1 && pavise_far_of("127.0.0.1") == 0 &&
              pavise_far_of("") == PAVISE_REFUSED && pavise_far_of("300.1.1.1") == PAVISE_REFUSED &&
              pavise_far_of("::1") == 0 && pavise_far_of("fe80::1") == 1 &&
              pavise_far_of("2001:db8::1") == 2,
          "pavise: the rules");

    /* PM1 on the real dispatcher: switches and interfaces read; the mesh
     * and the routing host are not linked and passkeys are off: unknown */
    pavise_init(&PV, &LINK);
    CHECK(pavise_refresh(&PV) == ZXM_OK && PV.features_known && PV.nfeatures > 0 &&
              !PV.peers_known && !PV.devices_known && !PV.keys_known,
          "pavise: a refresh on zxvd: %u switches, peers %d, mesh %d, net %d (%s)", PV.nfeatures,
          (int) PV.peers_known, (int) PV.devices_known, (int) PV.net_known, PV.notice);

    /* PM2: apps from the view; out of range refused */
    uint32_t net = 1u << PAVISE_CAP_NET, fsr = 1u << gatehouse_cap_index("fs.read");
    CHECK(pavise_add_app(&PV, 12, net | fsr, 1, 2, "") == ZXM_OK &&
              pavise_add_app(&PV, 7, fsr, 0, 0, "") == ZXM_OK &&
              pavise_add_app(&PV, 12, net, 1, 0, "") == ZXM_E_ARG &&
              pavise_add_app(&PV, 0, net, 1, 0, "") == ZXM_E_ARG &&
              pavise_add_app(&PV, 10000, net, 1, 0, "") == ZXM_E_ARG &&
              pavise_add_app(&PV, 9, 1u << 19, 1, 0, "") == ZXM_E_ARG &&
              pavise_add_app(&PV, 9, net, 2, 0, "") == ZXM_E_ARG &&
              pavise_add_app(&PV, 9, net, 1, 3, "") == ZXM_E_ARG && PV.napps == 2,
          "pavise: two apps, the rest refused");

    /* keys unknown: a decision the rest allows is NEITHER, denied, and still filed */
    CHECK(pavise_grant(&PV, 7, 3, true, true) == ZXM_OK &&
              pavise_decide(&PV, 7, 3, 0, PV_NOW, &v) == ZXM_OK && v == LPRES_STATE_NEITHER &&
              PV.last == 1 && pavise_entry(&PV, 0)->verdict == 0,
          "pavise: no keys known: NEITHER (%s)", PV.notice);
    CHECK(pavise_exposure_now(&PV, PV_NOW, &score) == ZXM_E_STATE,
          "pavise: no exposure score without the keys");
    CHECK(pavise_bind_key(&PV, 12, PV_K1, true) == ZXM_E_STATE, "pavise: no key to bind yet");

    /* PM1 with the roster and the keys on PV_LINK */
    zxm_link_init(&PV_LINK, pv_send, &PV_LINK);
    PV.link = &PV_LINK;
    PV_KEYS = PV_KEYS3;
    PV_DEVICES = PV_ROSTER;
    PV_PEERS = PV_PEERS3;
    CHECK(pavise_refresh(&PV) == ZXM_OK && PV.keys_known && PV.nkeys == 3 && PV.key[2].revoked &&
              PV.key[0].life_days == PAVISE_LIFE_DAYS && PV.devices_known && PV.ndevices == 2 &&
              PV.device[0].active && !PV.device[1].active && PV.device[0].since == PV_T0 &&
              PV.peers_known && PV.peers_far[0] == 0 && PV.peers_far[1] == 1 &&
              PV.peers_far[2] == 1 && PV.peers_unscoped == 1,
          "pavise: the keys and the roster: %s", PV.notice);
    PV_KEYS = "{\"credentials\":[{\"cred_id\":\"zz\"}]}";
    CHECK(pavise_refresh(&PV) == ZXM_E_PROTOCOL && PV.nkeys == 3 && PV.keys_known,
          "pavise: a reply out of bounds changes nothing");
    PV_KEYS = PV_KEYS3;

    /* PM2: grants and keys need the owner's word; a revoke is at once */
    CHECK(pavise_grant(&PV, 12, PAVISE_CAP_NET, true, false) == ZXM_E_STATE &&
              !(PV.app[0].granted & net) && pavise_grant(&PV, 99, 0, true, true) == ZXM_E_ARG &&
              pavise_grant(&PV, 12, 19, true, true) == ZXM_E_ARG,
          "pavise: a grant asked, not given");
    CHECK(pavise_grant(&PV, 12, PAVISE_CAP_NET, true, true) == ZXM_OK &&
              pavise_grant(&PV, 12, 3, true, true) == ZXM_OK &&
              pavise_grant(&PV, 7, 3, true, true) == ZXM_OK,
          "pavise: the owner grants");
    CHECK(pavise_bind_key(&PV, 12, PV_K1, false) == ZXM_E_STATE &&
              pavise_bind_key(&PV, 12, "nothex", true) == ZXM_E_ARG &&
              pavise_bind_key(&PV, 12, PV_K1, true) == ZXM_OK &&
              pavise_bind_key(&PV, 7, PV_K2, true) == ZXM_OK && PV.app[0].key == 0 &&
              PV.app[1].key == 1,
          "pavise: keys bound on the owner's word");
    CHECK(pavise_key_life(&PV, PV_K1, 0, true) == ZXM_E_ARG &&
              pavise_key_life(&PV, PV_K1, 400, false) == ZXM_E_STATE,
          "pavise: a lifetime of 1..36525 days, on the owner's word");

    /* PM5: decisions */
    CHECK(pavise_set_mode(&PV, 3) == ZXM_E_ARG && pavise_set_mode(&PV, 2) == ZXM_OK,
          "pavise: the mode Hedgerow set: online");
    CHECK(pavise_decide(&PV, 12, PAVISE_CAP_NET, 2, PV_NOW, &v) == ZXM_OK &&
              v == LPRES_STATE_TRUE && pavise_entry(&PV, 0)->verdict == 1 &&
              pavise_entry(&PV, 0)->decision == 2 && pavise_entry(&PV, 0)->prev == 1,
          "pavise: app 12 may use the network from beyond: %s", PV.notice);
    CHECK(pavise_decide(&PV, 12, 4, 0, PV_NOW, &v) == ZXM_OK && v == LPRES_STATE_FALSE &&
              strstr(PV.notice, "not granted"),
          "pavise: never asked, never granted: %s", PV.notice);
    CHECK(pavise_set_mode(&PV, 1) == ZXM_OK &&
              pavise_decide(&PV, 12, PAVISE_CAP_NET, 2, PV_NOW, &v) == ZXM_OK &&
              v == LPRES_STATE_FALSE && strstr(PV.notice, "path is closed") &&
              pavise_decide(&PV, 12, PAVISE_CAP_NET, 1, PV_NOW, &v) == ZXM_OK &&
              v == LPRES_STATE_TRUE,
          "pavise: lan only: closed from beyond, open nearby");
    CHECK(pavise_decide(&PV, 7, 3, 0, PV_NOW + 26 * 86400, &v) == ZXM_OK &&
              v == LPRES_STATE_FALSE && strstr(PV.notice, "expired") &&
              pavise_decide(&PV, 7, 3, 0, PV_NOW, &v) == ZXM_OK && v == LPRES_STATE_TRUE,
          "pavise: app 7's key ends in 15 days; past it, denied");
    CHECK(pavise_grant(&PV, 7, 3, false, false) == ZXM_OK &&
              pavise_decide(&PV, 7, 3, 0, PV_NOW, &v) == ZXM_OK && v == LPRES_STATE_FALSE &&
              pavise_grant(&PV, 7, 3, true, true) == ZXM_OK,
          "pavise: a revoke at once, then granted again");
    CHECK(pavise_decide(&PV, 99, 0, 0, PV_NOW, &v) == ZXM_E_ARG &&
              pavise_decide(&PV, 12, 19, 0, PV_NOW, &v) == ZXM_E_ARG &&
              pavise_decide(&PV, 12, 0, 3, PV_NOW, &v) == ZXM_E_ARG &&
              pavise_decide(&PV, 12, 0, 0, -1, &v) == ZXM_E_ARG,
          "pavise: a decision off its range is refused, and not filed");

    /* PM7: measures. Paths open from beyond: none in lan mode; keys within
     * 30 days: K2 (15 days); unsigned apps: app 7. 3 + 5 = 8 */
    CHECK(pavise_exposure_now(&PV, PV_NOW, &score) == ZXM_OK && score == 8, "pavise: exposure %lld",
          (long long) score);
    CHECK(pavise_set_mode(&PV, 2) == ZXM_OK && pavise_exposure_now(&PV, PV_NOW, &score) == ZXM_OK &&
              score == (PV.net_known && !PV.public_ifaces ? 8 : 10),
          "pavise: online, app 12's path counts when an interface is public: %lld (%u public)",
          (long long) score, PV.public_ifaces);
    CHECK(pavise_key_life(&PV, PV_K1, 35, true) == ZXM_OK &&
              pavise_exposure_now(&PV, PV_NOW, &score) == ZXM_OK &&
              score == (PV.net_known && !PV.public_ifaces ? 11 : 13),
          "pavise: K1 cut to 35 days, 25 left: one more key soon: %lld", (long long) score);
    CHECK(pavise_app_caps(&PV, 12) == 2 && pavise_app_caps(&PV, 7) == 1 &&
              pavise_app_caps(&PV, 5) == PAVISE_REFUSED && pavise_cap_holders(&PV, 3) == 2 &&
              pavise_cap_holders(&PV, 0) == 1 && pavise_cap_holders(&PV, 19) == PAVISE_REFUSED,
          "pavise: the permission matrix: app 12 holds 2, app 7 1; fs.read held by 2");
    CHECK(pavise_trust_tree(&PV) == 2 && PV.nnodes == 4 && PV.node[0].trust == 1 &&
              PV.node[2].trust == 0 && PV.node[0].app == 12 && PV.node[0].key == 1,
          "pavise: the trust tree: two devices by two apps, the active one trusted");

    /* PM6: the ledger numbered without reuse, every entry linked */
    CHECK(pavise_ledger_intact(&PV) && PV.count == PV.last && pavise_entry(&PV, PV.count) == NULL,
          "pavise: %lld decisions filed, each linked", (long long) PV.last);
    {
        int64_t was = PV.last;
        PV.entry[(PV.head + PAVISE_LEDGER - 2u) % PAVISE_LEDGER].prev = 0;
        CHECK(!pavise_ledger_intact(&PV), "pavise: a broken link is seen");
        PV.entry[(PV.head + PAVISE_LEDGER - 2u) % PAVISE_LEDGER].prev = was - 2;
        CHECK(pavise_ledger_intact(&PV), "pavise: restored");
        for (uint32_t i = 0; i < PAVISE_LEDGER; i++)
            CHECK(pavise_decide(&PV, 7, 3, 0, PV_NOW, &v) == ZXM_OK, "pavise: decision %u", i);
        CHECK(PV.count == PAVISE_LEDGER && PV.last == was + PAVISE_LEDGER &&
                  pavise_entry(&PV, PAVISE_LEDGER - 1u)->decision == was + 1 &&
                  pavise_ledger_intact(&PV),
              "pavise: the ring keeps the newest, numbers go on");
        PV.last = PAVISE_DECISION_MAX;
        CHECK(pavise_decide(&PV, 7, 3, 0, PV_NOW, &v) == ZXM_E_TOOBIG &&
                  PV.last == PAVISE_DECISION_MAX,
              "pavise: past 999999999 refused, never wrapped");
        PV.last = was + PAVISE_LEDGER;
    }

    /* PM8: history against a journal on disk */
    CHECK(pavise_history(&PV) == LPRES_STATE_NEITHER, "pavise: no journal yet: NEITHER");
    snprintf(jdir, sizeof jdir, "%s/zxv-pav-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(jdir) != NULL && zjr_open(jdir) == 0, "pavise: a journal");
    CHECK(pavise_refresh(&PV) == ZXM_OK && pavise_history(&PV) == LPRES_STATE_TRUE &&
              PV.history >= 0,
          "pavise: the history holds: %s", PV.notice);
    zjr_close();
    CHECK(rm_tree(jdir) == 0, "pavise: cleanup");

    /* PM4, the BOTH test: policy A allows app 12 the network, policy B
     * denies it: BOTH, denied until the owner resolves it */
    CHECK(pavise_policy(&PV, 1, 12, PAVISE_CAP_NET, 1, false) == ZXM_E_STATE &&
              pavise_policy(&PV, 0, 12, PAVISE_CAP_NET, 1, true) == ZXM_E_ARG &&
              pavise_policy(&PV, 1, 12, PAVISE_CAP_NET, 2, true) == ZXM_E_ARG,
          "pavise: a policy on the owner's word, inside its bounds");
    CHECK(pavise_policy(&PV, 1, 12, PAVISE_CAP_NET, 1, true) == ZXM_OK &&
              pavise_decide(&PV, 12, PAVISE_CAP_NET, 2, PV_NOW, &v) == ZXM_OK &&
              v == LPRES_STATE_TRUE,
          "pavise: policy A allows: TRUE");
    CHECK(pavise_policy(&PV, 2, 12, PAVISE_CAP_NET, 0, true) == ZXM_OK &&
              pavise_clause_now(&PV, 12, PAVISE_CAP_NET) == LPRES_STATE_BOTH,
          "pavise: policy B denies: the clause is BOTH");
    CHECK(pavise_decide(&PV, 12, PAVISE_CAP_NET, 2, PV_NOW, &v) == ZXM_OK &&
              v == LPRES_STATE_BOTH && (int) v == 3 && pavise_entry(&PV, 0)->verdict == 3 &&
              strstr(PV.notice, "until you rule"),
          "pavise: the access is BOTH and denied: %s", PV.notice);
    CHECK(pavise_rule(&PV, 12, PAVISE_CAP_NET, 1, false) == ZXM_E_STATE &&
              pavise_rule(&PV, 12, PAVISE_CAP_NET, 3, true) == ZXM_E_ARG &&
              pavise_clause_now(&PV, 12, PAVISE_CAP_NET) == LPRES_STATE_BOTH,
          "pavise: only the owner's ruling");
    CHECK(pavise_rule(&PV, 12, PAVISE_CAP_NET, 1, true) == ZXM_OK &&
              pavise_decide(&PV, 12, PAVISE_CAP_NET, 2, PV_NOW, &v) == ZXM_OK &&
              v == LPRES_STATE_TRUE,
          "pavise: ruled allow: TRUE");
    CHECK(pavise_rule(&PV, 12, PAVISE_CAP_NET, 2, true) == ZXM_OK &&
              pavise_decide(&PV, 12, PAVISE_CAP_NET, 2, PV_NOW, &v) == ZXM_OK &&
              v == LPRES_STATE_FALSE && strstr(PV.notice, "policy"),
          "pavise: ruled deny: FALSE");
    CHECK(pavise_rule(&PV, 12, PAVISE_CAP_NET, 0, true) == ZXM_OK &&
              pavise_clause_now(&PV, 12, PAVISE_CAP_NET) == LPRES_STATE_BOTH &&
              pavise_ledger_intact(&PV),
          "pavise: the ruling withdrawn: BOTH again");
    CHECK(pv_calls > 0, "pavise: the roster and the keys came over PV_LINK");
}
