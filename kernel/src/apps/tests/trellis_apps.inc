/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Trellis's model against zxvd's real dispatcher (LINK). This in-process
 * zxvd links no device mesh, phone link or peer network, so it refuses
 * devmesh.*, phone.* and net.* ("not available"); the mesh, the phones and
 * the peers are answered on a link of its own (TRL_LINK), which passes
 * every other request to the real dispatcher. Nothing goes online: the
 * network is only ever set off or to the local network, and that answer is
 * this link's own. */
#include "trellis_model.h"

static trellis_t TRL, TRL2;
static zxm_link_t TRL_LINK;
static int trl_sent; /* requests TRL_LINK answered itself */
static char trl_req[ZXM_REQ_MAX + 1];
static const char *TRL_STATUS, *TRL_DEVICES, *TRL_INVITE, *TRL_JOIN, *TRL_SAS, *TRL_REVOKE,
    *TRL_PHONES, *TRL_PAIR, *TRL_PREVOKE, *TRL_PEERS, *TRL_NET;

static int32_t trl_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"devmesh.status\"")) r = TRL_STATUS;
    if (strstr(req, "\"devmesh.devices\"")) r = TRL_DEVICES;
    if (strstr(req, "\"devmesh.invite\"")) r = TRL_INVITE;
    if (strstr(req, "\"devmesh.join\"")) r = TRL_JOIN;
    if (strstr(req, "\"devmesh.confirm_sas\"")) r = TRL_SAS;
    if (strstr(req, "\"devmesh.revoke\"")) r = TRL_REVOKE;
    if (strstr(req, "\"phone.list\"")) r = TRL_PHONES;
    if (strstr(req, "\"phone.pair\"")) r = TRL_PAIR;
    if (strstr(req, "\"phone.revoke\"")) r = TRL_PREVOKE;
    if (strstr(req, "\"net.peers\"")) r = TRL_PEERS;
    if (strstr(req, "\"net.command\"")) r = TRL_NET;
    if (!r) return inproc(NULL, req, len, resp, cap);
    trl_sent++;
    memcpy(trl_req, req, len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX);
    trl_req[len < ZXM_REQ_MAX ? len : ZXM_REQ_MAX] = 0;
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define TRL_SELF   "00112233445566778899aabbccddeeff"
#define TRL_PHONE  "0123456789abcdef0123456789abcdef"
#define TRL_LAPTOP "fedcba9876543210fedcba9876543210"
#define TRL_OLD    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define TRL_NEW    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define TRL_DEV(id, name, role, admin, held, status, self)                                         \
    "{\"id\":\"" id "\",\"name\":\"" name "\",\"role\":\"" role "\",\"admin\":" admin              \
    ",\"held\":" held ",\"status\":\"" status "\",\"self\":" self ",\"up\":true,\"since\":\"2\"}"

static const char *const TRL_ROSTER = "{\"roster_version\":\"4\",\"devices\":[" TRL_DEV(
    TRL_SELF, "desk", "home", "true", "false", "active",
    "true") "," TRL_DEV(TRL_PHONE, "pocket", "thin", "false", "true", "active",
                        "false") "," TRL_DEV(TRL_LAPTOP, "laptop", "thin", "true", "false",
                                             "active", "false") "," TRL_DEV(TRL_OLD, "old tablet",
                                                                            "thin", "false", "true",
                                                                            "revoked",
                                                                            "false") "]}";

static const char *const TRL_ST_ADMIN =
    "{\"joined\":true,\"mesh_id\":\"" TRL_OLD "\",\"self\":\"" TRL_SELF "\",\"name\":\"desk\","
    "\"role\":\"home\",\"admin\":true,\"level\":\"high\",\"roster_version\":\"4\",\"devices\":4,"
    "\"inviting\":false,\"joining\":false,\"pairing\":null,\"frames_waiting\":0,"
    "\"frames_dropped\":\"0\",\"carrier\":\"none\",\"saved\":false,\"save_error\":null,"
    "\"events\":[]}";

APPS_TEST(trellis)
{
    int before;
    char link[512];

    /* the real dispatcher: no mesh, no phone link, no peer network here */
    trellis_init(&TRL, &LINK);
    before = sent;
    CHECK(trellis_revoke_device(&TRL, 0) == ZXM_E_STATE &&
              trellis_confirm_sas(&TRL, true) == ZXM_E_STATE &&
              trellis_revoke_confirm(&TRL, true) == ZXM_E_STATE &&
              trellis_invite_left(&TRL, 0) == TRELLIS_REFUSED && sent == before,
          "trellis: nothing judged before the mesh is known, nothing asked");
    CHECK(trellis_read_status(&TRL) == ZXM_E_REMOTE && !TRL.known && strstr(LINK.err, "available"),
          "trellis: devmesh.status refused by this zxvd: %s", LINK.err);
    CHECK(trellis_read_phones(&TRL) == ZXM_E_REMOTE && !TRL.phones_known,
          "trellis: phone.list refused: %s", LINK.err);
    CHECK(trellis_read_peers(&TRL) == ZXM_E_REMOTE && TRL.peers == 0,
          "trellis: net.peers refused: %s", LINK.err);
    CHECK(trellis_net(&TRL, TRELLIS_NET_OFF) == ZXM_E_REMOTE && TRL.net_mode == -1,
          "trellis: net.command refused: %s", LINK.err);

    /* TM1: the mesh on its own link */
    zxm_link_init(&TRL_LINK, trl_send, &TRL_LINK);
    trellis_init(&TRL, &TRL_LINK);
    TRL_STATUS = TRL_ST_ADMIN;
    TRL_DEVICES = TRL_ROSTER;
    CHECK(trellis_read_status(&TRL) == ZXM_OK && TRL.known && TRL.joined && TRL.admin &&
              TRL.version == 4 && !TRL.pairing && !strcmp(TRL.self, TRL_SELF),
          "trellis: the mesh: %s", TRL.notice);
    CHECK(trellis_read_devices(&TRL) == ZXM_OK && TRL.devices == 4 && TRL.roster_known &&
              TRL.device[0].self && TRL.device[0].role == TRELLIS_ROLE_HOME &&
              TRL.device[3].state == TRELLIS_REVOKED && TRL.device[1].held == 1 &&
              trellis_find_device(&TRL, TRL_LAPTOP) == 2 && trellis_find_device(&TRL, "x") == -1 &&
              !strcmp(TRL.notice, "4 devices in your mesh: 3 active, 1 revoked; 2 admins."),
          "trellis: the roster: %s", TRL.notice);
    CHECK(trellis_record_ok(&TRL, 0) && trellis_record_ok(&TRL, 3) && !trellis_record_ok(&TRL, 4),
          "trellis: every row fits ^ZXTRELLIS (TM7)");
    CHECK(trellis_count(&TRL, TRELLIS_ACTIVE) == 3 && trellis_count(&TRL, TRELLIS_REVOKED) == 1 &&
              trellis_count(&TRL, TRELLIS_WAITING) == 0 &&
              trellis_count(&TRL, 4) == TRELLIS_REFUSED,
          "trellis: peers per state (TM6)");

    /* TM4: what may be revoked, and why not */
    CHECK(!trellis_device_revocable(&TRL, 0) && trellis_device_revocable(&TRL, 1) &&
              trellis_device_revocable(&TRL, 2) && !trellis_device_revocable(&TRL, 3) &&
              !trellis_device_revocable(&TRL, 4),
          "trellis: revocable (TL1)");
    before = trl_sent;
    CHECK(trellis_revoke_device(&TRL, 0) == ZXM_OK && TRL.revoke_kind == TRELLIS_NOTHING &&
              strstr(TRL.notice, "it is this device"),
          "trellis: not this device: %s", TRL.notice);
    CHECK(trellis_revoke_device(&TRL, 3) == ZXM_OK && TRL.revoke_kind == TRELLIS_NOTHING &&
              strstr(TRL.notice, "already revoked"),
          "trellis: not twice: %s", TRL.notice);
    CHECK(trellis_revoke_device(&TRL, 4) == ZXM_E_ARG && trl_sent == before,
          "trellis: past the roster, nothing asked");

    /* TM4: two yeses, and only then devmesh.revoke */
    TRL_REVOKE = "{\"revoked\":true,\"roster_version\":\"5\"}";
    CHECK(trellis_revoke_device(&TRL, 1) == ZXM_OK && TRL.revoke_kind == TRELLIS_DEVICE &&
              strstr(TRL.notice, "Confirm twice"),
          "trellis: revoking waits: %s", TRL.notice);
    CHECK(trellis_revoke_device(&TRL, 2) == ZXM_E_STATE, "trellis: one revocation at a time");
    CHECK(trellis_revoke_confirm(&TRL, true) == ZXM_OK && TRL.revoke_kind == TRELLIS_DEVICE &&
              strstr(TRL.notice, "can never be undone") && trl_sent == before &&
              TRL.device[1].state == TRELLIS_ACTIVE,
          "trellis: the first yes asks again, nothing sent: %s", TRL.notice);
    CHECK(trellis_revoke_confirm(&TRL, true) == ZXM_OK && trl_sent == before + 1 &&
              strstr(trl_req, "\"devmesh.revoke\"") && strstr(trl_req, TRL_PHONE) &&
              TRL.device[1].state == TRELLIS_REVOKED && TRL.device[1].since == 5 &&
              TRL.version == 5 && TRL.revoke_kind == TRELLIS_NOTHING &&
              trellis_count(&TRL, TRELLIS_REVOKED) == 2,
          "trellis: the second yes revokes: %s", TRL.notice);
    CHECK(trellis_revoke_confirm(&TRL, true) == ZXM_E_STATE, "trellis: a word is spent once");
    before = trl_sent;
    CHECK(trellis_revoke_device(&TRL, 2) == ZXM_OK &&
              trellis_revoke_confirm(&TRL, true) == ZXM_OK &&
              trellis_revoke_confirm(&TRL, false) == ZXM_OK && TRL.revoke_kind == TRELLIS_NOTHING &&
              TRL.device[2].state == TRELLIS_ACTIVE && trl_sent == before &&
              strstr(TRL.notice, "nothing was revoked"),
          "trellis: a no at the second asking keeps it: %s", TRL.notice);
    TRL_REVOKE = "{\"revoked\":true,\"roster_version\":\"5\"}"; /* not past 5 */
    CHECK(trellis_revoke_device(&TRL, 2) == ZXM_OK &&
              trellis_revoke_confirm(&TRL, true) == ZXM_OK &&
              trellis_revoke_confirm(&TRL, true) == ZXM_E_PROTOCOL &&
              TRL.device[2].state == TRELLIS_ACTIVE && TRL.revoke_kind == TRELLIS_NOTHING,
          "trellis: a roster version that does not go up is not zxvd's answer");

    /* TL1's margin: a roster whose only active admin is the laptop */
    trellis_init(&TRL2, &TRL_LINK);
    TRL_DEVICES = "{\"roster_version\":\"4\",\"devices\":[" TRL_DEV(
        TRL_SELF, "desk", "home", "false", "false", "active",
        "true") "," TRL_DEV(TRL_LAPTOP, "laptop", "thin", "true", "false", "active", "false") "]}";
    CHECK(trellis_read_status(&TRL2) == ZXM_OK && trellis_read_devices(&TRL2) == ZXM_OK &&
              trellis_revoke_device(&TRL2, 1) == ZXM_OK && TRL2.revoke_kind == TRELLIS_NOTHING &&
              strstr(TRL2.notice, "the last admin"),
          "trellis: never the last admin: %s", TRL2.notice);
    TRL_STATUS = "{\"joined\":true,\"self\":\"" TRL_SELF "\",\"admin\":false,"
                 "\"roster_version\":\"4\",\"pairing\":null}";
    CHECK(trellis_read_status(&TRL2) == ZXM_OK && trellis_revoke_device(&TRL2, 1) == ZXM_OK &&
              strstr(TRL2.notice, "not an admin"),
          "trellis: only from an admin: %s", TRL2.notice);

    /* answers that are not zxvd's: nothing kept */
    TRL_STATUS = "{\"joined\":true,\"self\":\"xyz\",\"admin\":true,\"roster_version\":\"4\","
                 "\"pairing\":null}";
    trellis_init(&TRL2, &TRL_LINK);
    CHECK(trellis_read_status(&TRL2) == ZXM_E_PROTOCOL && !TRL2.known,
          "trellis: a device id that is none");
    TRL_DEVICES = "{\"roster_version\":\"4\",\"devices\":[" TRL_DEV(
        TRL_SELF, "desk", "home", "true", "false", "lost", "true") "]}";
    CHECK(trellis_read_devices(&TRL2) == ZXM_E_PROTOCOL && !TRL2.roster_known,
          "trellis: a status that is neither active nor revoked");
    TRL_DEVICES = "{\"roster_version\":\"4\",\"devices\":[" TRL_DEV(
        TRL_SELF, "desk", "home", "true", "false", "active",
        "true") "," TRL_DEV(TRL_LAPTOP, "laptop", "thin", "true", "false", "active", "true") "]}";
    CHECK(trellis_read_devices(&TRL2) == ZXM_E_PROTOCOL && !TRL2.roster_known,
          "trellis: two devices that are both this one");

    /* TM2: an invitation and its minutes */
    TRL_STATUS = TRL_ST_ADMIN;
    TRL_INVITE = "{\"qr\":\"ABC\",\"qr_bytes\":3,\"code\":\"AB2CD-EF3GH\",\"expires_in_ms\":600000,"
                 "\"created_mesh\":false,\"mesh_id\":\"" TRL_OLD "\"}";
    before = trl_sent;
    CHECK(trellis_invite(&TRL, 0, true, false, 1000) == ZXM_E_ARG &&
              trellis_invite(&TRL, TRELLIS_ROLE_THIN, true, false, -1) == ZXM_E_ARG &&
              trl_sent == before,
          "trellis: an invitation of no role or at no time: nothing asked");
    CHECK(trellis_invite(&TRL, TRELLIS_ROLE_THIN, true, false, 1000) == ZXM_OK &&
              !strcmp(TRL.code, "AB2CD-EF3GH") && strstr(trl_req, "\"thin\"") &&
              trellis_invite_left(&TRL, 1000) == 10 && trellis_invite_left(&TRL, 541000) == 1 &&
              trellis_invite_left(&TRL, 600999) == 1 && trellis_invite_left(&TRL, 601000) == 0 &&
              strstr(TRL.notice, "valid 10 minutes"),
          "trellis: an invitation: %s", TRL.notice);
    TRL_INVITE = "{\"code\":\"AB2CD-EF3GH\",\"expires_in_ms\":600001}";
    CHECK(trellis_invite(&TRL, TRELLIS_ROLE_HOME, false, true, 2000) == ZXM_E_PROTOCOL &&
              TRL.issued_ms == 1000,
          "trellis: a life past devmesh's ten minutes is not zxvd's answer; nothing kept");

    /* TM2: joining, the six digits, the owner's word */
    TRL_JOIN = "{\"pairing\":true,\"frames_waiting\":1,\"next\":\"compare\"}";
    before = trl_sent;
    CHECK(trellis_join(&TRL, "ab2cd-ef3gh") == ZXM_E_ARG &&
              trellis_join(&TRL, "AB2CDEF3GH") == ZXM_E_ARG &&
              trellis_join(&TRL, NULL) == ZXM_E_ARG && trl_sent == before,
          "trellis: codes of another form: nothing asked");
    CHECK(trellis_join(&TRL, "AB2CD-EF3GH") == ZXM_OK && strstr(TRL.notice, "six digits"),
          "trellis: joining: %s", TRL.notice);
    TRL_STATUS = "{\"joined\":true,\"self\":\"" TRL_SELF "\",\"admin\":true,"
                 "\"roster_version\":\"5\",\"pairing\":{\"device\":\"" TRL_NEW "\","
                 "\"sas\":\"012345\"}}";
    CHECK(trellis_read_status(&TRL) == ZXM_OK && TRL.pairing && !strcmp(TRL.sas, "012345") &&
              trellis_count(&TRL, TRELLIS_WAITING) == 1 && strstr(TRL.notice, "012345"),
          "trellis: a pairing waits: %s", TRL.notice);
    TRL_SAS = "{\"confirmed\":true,\"in_roster\":true,\"devices\":5}";
    CHECK(trellis_confirm_sas(&TRL, true) == ZXM_OK && strstr(trl_req, TRL_NEW) &&
              strstr(trl_req, "\"match\":true") && !TRL.pairing &&
              trellis_count(&TRL, TRELLIS_WAITING) == 0 && strstr(TRL.notice, "Paired"),
          "trellis: the digits match: %s", TRL.notice);
    CHECK(trellis_confirm_sas(&TRL, true) == ZXM_E_STATE, "trellis: no pairing waits");
    TRL_STATUS = "{\"joined\":true,\"self\":\"" TRL_SELF "\",\"admin\":true,"
                 "\"roster_version\":\"5\",\"pairing\":{\"device\":\"" TRL_NEW "\","
                 "\"sas\":\"01234x\"}}";
    CHECK(trellis_read_status(&TRL) == ZXM_E_PROTOCOL && !TRL.pairing,
          "trellis: six digits that are not digits");

    /* TM3: the phones */
    TRL_PHONES =
        "{\"on\":true,\"port\":47410,\"phones\":[{\"phone_id\":\"" TRL_PHONE "\","
        "\"name\":\"pocket\",\"allow_post\":true,\"connected\":1},{\"phone_id\":\"" TRL_LAPTOP
        "\",\"name\":\"tablet\",\"allow_post\":false,\"connected\":0}]}";
    CHECK(trellis_read_phones(&TRL) == ZXM_OK && TRL.phones == 2 && TRL.phone_link_on &&
              TRL.phone[0].allow_post && !TRL.phone[1].allow_post &&
              trellis_count(&TRL, TRELLIS_ACTIVE) == 4,
          "trellis: the phones: %s", TRL.notice);
    CHECK(trellis_phone_may_post(&TRL, 0, 1) && !trellis_phone_may_post(&TRL, 0, 0) &&
              !trellis_phone_may_post(&TRL, 1, 1) && !trellis_phone_may_post(&TRL, 2, 1),
          "trellis: may a phone post (TL5, gatehouse.use_ok)");
    TRL_PAIR = "{\"phone_id\":\"" TRL_OLD "\",\"name\":\"kitchen\",\"allow_post\":false,"
               "\"host\":\"192.168.1.5\",\"port\":47410,\"link\":\"zxv-phone:1?h=192.168.1.5"
               "&p=47410&id=" TRL_OLD "&k=" TRL_OLD TRL_OLD "&n=kitchen\"}";
    before = trl_sent;
    CHECK(trellis_pair_phone(&TRL, "", false, link, sizeof link) == ZXM_E_ARG &&
              trellis_pair_phone(&TRL, "a name that is far too long for one", false, link,
                                 sizeof link) == ZXM_E_ARG &&
              trellis_pair_phone(&TRL, "kitchen", false, link, 64) == ZXM_E_ARG &&
              trl_sent == before,
          "trellis: no name, a long one, no room for the link: nothing asked");
    CHECK(trellis_pair_phone(&TRL, "kitchen", false, link, sizeof link) == ZXM_OK &&
              !strncmp(link, "zxv-phone:1?h=", 14) && TRL.phones == 3 &&
              !strstr(TRL_LINK.resp, "&k=") && !strstr(TRL.notice, "&k=") &&
              !strstr(TRL.notice, "move money"),
          "trellis: paired, the link handed out once and not kept: %s", TRL.notice);
    TRL_PAIR = "{\"phone_id\":\"" TRL_OLD "\",\"name\":\"kitchen\",\"allow_post\":true,"
               "\"link\":\"zxv-phone:1?h=x&p=1&id=" TRL_OLD "&k=" TRL_OLD TRL_OLD "&n=k\"}";
    CHECK(trellis_pair_phone(&TRL, "kitchen", false, link, sizeof link) == ZXM_E_PROTOCOL &&
              !link[0] && TRL.phones == 3 && !strstr(TRL_LINK.resp, "&k="),
          "trellis: a pairing that allows more than asked is not zxvd's answer");

    /* TM4: a phone, twice */
    TRL_PREVOKE = "{\"revoked\":true,\"closed\":1}";
    before = trl_sent;
    CHECK(trellis_revoke_phone(&TRL, 3) == ZXM_E_ARG && trellis_revoke_phone(&TRL, 0) == ZXM_OK &&
              trellis_revoke_device(&TRL, 2) == ZXM_E_STATE &&
              trellis_revoke_confirm(&TRL, true) == ZXM_OK && trl_sent == before &&
              trellis_revoke_confirm(&TRL, true) == ZXM_OK && trl_sent == before + 1 &&
              strstr(trl_req, "\"phone.revoke\"") && TRL.phone[0].revoked &&
              !trellis_phone_may_post(&TRL, 0, 1) && trellis_count(&TRL, TRELLIS_REVOKED) == 3,
          "trellis: a phone revoked after two yeses: %s", TRL.notice);
    CHECK(trellis_revoke_phone(&TRL, 0) == ZXM_OK && TRL.revoke_kind == TRELLIS_NOTHING &&
              strstr(TRL.notice, "already revoked"),
          "trellis: a phone is revoked once");

    /* TM5: the peers and the network, never online */
    TRL_PEERS = "{\"peers\":[{\"id\":\"" TRL_OLD TRL_OLD "\",\"addr\":\"192.168.1.9:7000\","
                "\"bucket\":3,\"ubh\":true,\"last_seen_ms\":\"1700000000000\",\"in_book\":true,"
                "\"member\":true},{\"id\":\"" TRL_NEW TRL_NEW "\",\"addr\":\"\",\"bucket\":9,"
                "\"ubh\":false,\"last_seen_ms\":\"0\",\"in_book\":false,\"member\":false}],"
                "\"count\":2}";
    CHECK(trellis_read_peers(&TRL) == ZXM_OK && TRL.peers == 2 &&
              TRL.peer[0].last_seen_ms == 1700000000000 && TRL.peer[0].member &&
              !TRL.peer[1].in_book && !strcmp(TRL.notice, "2 peers, 1 admitted to the commons."),
          "trellis: the peers: %s", TRL.notice);
    TRL_PEERS = "{\"peers\":[{\"id\":\"" TRL_OLD "\",\"addr\":\"\",\"last_seen_ms\":\"0\","
                "\"in_book\":false,\"member\":false}],\"count\":1}";
    CHECK(trellis_read_peers(&TRL) == ZXM_E_PROTOCOL && TRL.peers == 2,
          "trellis: a NodeID of 16 bytes is no NodeID; nothing kept");
    TRL_NET = "{\"result\":\"lan\"}";
    before = trl_sent;
    CHECK(trellis_net(&TRL, 3) == ZXM_E_ARG && trellis_net(&TRL, -1) == ZXM_E_ARG &&
              trl_sent == before,
          "trellis: no such mode: nothing asked");
    CHECK(trellis_net(&TRL, TRELLIS_NET_LAN) == ZXM_OK && TRL.net_mode == TRELLIS_NET_LAN &&
              strstr(trl_req, "\"command\":\"lan\"") && strstr(TRL.notice, "local network only"),
          "trellis: the local network: %s", TRL.notice);
    TRL_NET = "{\"result\":\"off\"}";
    CHECK(trellis_net(&TRL, TRELLIS_NET_OFF) == ZXM_OK && TRL.net_mode == TRELLIS_NET_OFF &&
              !strcmp(TRL.net_answer, "off") && strstr(trl_req, "\"command\":\"off\""),
          "trellis: off: %s", TRL.notice);
    TRL_NET = "{\"answer\":\"off\"}";
    CHECK(trellis_net(&TRL, TRELLIS_NET_LAN) == ZXM_E_PROTOCOL && TRL.net_mode == TRELLIS_NET_OFF,
          "trellis: an answer without its result is not zxvd's");
    TRL_STATUS = TRL_DEVICES = TRL_INVITE = TRL_JOIN = TRL_SAS = TRL_REVOKE = NULL;
    TRL_PHONES = TRL_PAIR = TRL_PREVOKE = TRL_PEERS = TRL_NET = NULL;
}
