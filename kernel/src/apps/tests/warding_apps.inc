/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Warding's model against zxvd. This in-process zxvd links no seats store
 * (it needs Argon2id), no passkey store (ML-DSA) and no device mesh, so
 * seats.list, seats.auto, passkeys.list, passkeys.revoke, devmesh.devices
 * and devmesh.revoke are answered in their shapes (api/zxvd.openrpc.json,
 * zxvd_people.c m_dm_devices and m_dm_revoke) on a link of its own
 * (WD_LINK), with their state kept here, so a revoke shows in the next
 * list; every other request goes to the real dispatcher. The ids are fixed
 * test values, public ids, never a key's bytes. Then key 19 is revoked on
 * device A and still active on device B: BOTH, locked out everywhere. */
#include "warding_model.h"

static warding_t WD;
static zxm_link_t WD_LINK;
static int wd_calls, wd_revokes;
static bool wd_keys_off, wd_k1_revoked, wd_b_revoked;
static char wd_buf[4096];

#define WD_OWNER  "0000000000000000000000000000000a"
#define WD_PERSON "0000000000000000000000000000000b"
#define WD_GUEST  "0000000000000000000000000000000c"
#define WD_K1     "00112233445566778899aabbccddeeff"
#define WD_K2     "ffeeddccbbaa99887766554433221100"
#define WD_DEV_A  "a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0"
#define WD_DEV_B  "b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0"
#define WD_DEV_C  "c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0"
#define WD_NOW    INT64_C(1760000000)
#define WD_SEAT(id, name, role, lock)                                                              \
    "{\"id\":\"" id "\",\"name\":\"" name "\",\"role\":\"" role "\",\"lock\":\"" lock              \
    "\",\"color\":0,\"wait_ms\":0}"

static const char *const WD_SEATS =
    "{\"seats\":[" WD_SEAT(WD_OWNER, "Ada", "owner", "pass") "," WD_SEAT(
        WD_PERSON, "Bo", "person", "passkey") "," WD_SEAT(WD_GUEST, "Cy", "guest",
                                                          "none") "],\"auto_enter\":false}";

static const char *wd_key(char *out, size_t cap, const char *id, bool revoked)
{
    snprintf(out, cap,
             "{\"cred_id\":\"%s\",\"user\":\"%s\",\"rp_id\":\"zxv.local\",\"sign_count\":3,"
             "\"revoked\":%s,\"registered\":1700000000,\"last_used\":null}",
             id, WD_K2, revoked ? "true" : "false");
    return out;
}

static const char *wd_dev(char *out, size_t cap, const char *id, bool revoked, bool self)
{
    snprintf(out, cap,
             "{\"id\":\"%s\",\"name\":\"d\",\"role\":\"home\",\"admin\":%s,\"held\":false,"
             "\"status\":\"%s\",\"self\":%s,\"up\":true,\"since\":\"1700000000\"}",
             id, self ? "true" : "false", revoked ? "revoked" : "active", self ? "true" : "false");
    return out;
}

/* the answer to one stood-in method, or NULL for the real dispatcher;
 * *code is set for an error */
static const char *wd_answer(const char *req, int *code)
{
    char k1[256], k2[256], da[256], db[256], dc[256];
    *code = 0;
    if (strstr(req, "\"seats.list\"")) return WD_SEATS;
    if (strstr(req, "\"seats.auto\"")) return "{\"seat\":null}";
    if (strstr(req, "\"passkeys.list\"")) {
        if (wd_keys_off) {
            *code = -32012;
            return "passkeys: the feature is off";
        }
        snprintf(wd_buf, sizeof wd_buf, "{\"credentials\":[%s,%s]}",
                 wd_key(k1, sizeof k1, WD_K1, wd_k1_revoked), wd_key(k2, sizeof k2, WD_K2, true));
        return wd_buf;
    }
    if (strstr(req, "\"passkeys.revoke\"")) {
        wd_revokes++;
        if (!strstr(req, WD_K1)) {
            *code = -32041;
            return "IDENT_ERR_NOTFOUND: no such credential";
        }
        wd_k1_revoked = true;
        snprintf(wd_buf, sizeof wd_buf, "%s", wd_key(k1, sizeof k1, WD_K1, true));
        return wd_buf;
    }
    if (strstr(req, "\"devmesh.devices\"")) {
        snprintf(wd_buf, sizeof wd_buf, "{\"roster_version\":\"2\",\"devices\":[%s,%s,%s]}",
                 wd_dev(da, sizeof da, WD_DEV_A, false, true),
                 wd_dev(db, sizeof db, WD_DEV_B, wd_b_revoked, false),
                 wd_dev(dc, sizeof dc, WD_DEV_C, true, false));
        return wd_buf;
    }
    if (strstr(req, "\"devmesh.revoke\"")) {
        wd_revokes++;
        if (!strstr(req, WD_DEV_B)) {
            *code = -32050;
            return "not revoked: unknown device";
        }
        wd_b_revoked = true;
        return "{\"revoked\":true,\"roster_version\":\"3\"}";
    }
    return NULL;
}

static int32_t wd_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    int code = 0;
    wd_calls++;
    const char *r = wd_answer(req, &code);
    if (!r) return inproc(NULL, req, len, resp, cap);
    if (code)
        return snprintf(
            resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
            l->seq, code, r);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

APPS_TEST(warding)
{
    /* the rules (WR1-WR7) */
    CHECK(warding_key_active(1, 0, 1) && warding_key_active(1, 0, 3650) &&
              !warding_key_active(1, 0, 0) && !warding_key_active(1, 1, 5) &&
              !warding_key_active(0, 0, 5) && !warding_key_active(2, 0, 5) &&
              !warding_key_active(1, -1, 5) && !warding_key_active(1, 0, 3651),
          "warding: a key is active: issued, revoked on no device, not expired");
    CHECK(warding_expiry_ok(1) && warding_expiry_ok(3650) && !warding_expiry_ok(0) &&
              !warding_expiry_ok(3651),
          "warding: expiry limits 1..3650 days");
    CHECK(warding_serial_ok(1, 0) == 1 && warding_serial_ok(19, 19) == 0 &&
              warding_serial_ok(18, 19) == 0 &&
              warding_serial_ok(WARDING_SERIAL_MAX, WARDING_SERIAL_MAX - 1) == 1 &&
              warding_serial_ok(WARDING_SERIAL_MAX + 1, 1) == WARDING_REFUSED &&
              warding_serial_ok(-1, 0) == WARDING_REFUSED,
          "warding: the register rises strictly and refuses past 9(9)");
    CHECK(warding_entropy_bits(32, 256) == 256 && warding_entropy_bits(16, 62) == 95 &&
              warding_entropy_bits(20, 10) == 66 && warding_entropy_bits(4096, 256) == 32768 &&
              warding_entropy_bits(9, 1) == 0 && warding_entropy_bits(4097, 2) == WARDING_REFUSED &&
              warding_entropy_bits(1, 0) == WARDING_REFUSED &&
              warding_entropy_bits(1, 257) == WARDING_REFUSED,
          "warding: entropy from two lengths, one integer table");
    {
        /* the table is floor(1000 * log2(a)): 2^m <= a^1000 < 2^(m+1),
         * checked through powers of two: a^k for whole k */
        bool ok = WARDING_LOG2_MILLI[2] == 1000 && WARDING_LOG2_MILLI[4] == 2000 &&
                  WARDING_LOG2_MILLI[16] == 4000 && WARDING_LOG2_MILLI[256] == 8000 &&
                  WARDING_LOG2_MILLI[1] == 0;
        for (uint32_t a = 3; a <= WARDING_ALPHABET_MAX; a++)
            ok = ok && WARDING_LOG2_MILLI[a] > WARDING_LOG2_MILLI[a - 1];
        CHECK(ok, "warding: the log2 table rises and is exact at the powers of two");
    }
    {
        static const int64_t h[4] = {1, 1, 2, 1}, it[4] = {7, 7, 7, 9}, lv[4] = {1, 1, 1, 0};
        CHECK(warding_grid_cell(h, it, lv, 4, 0) == 2 && warding_grid_cell(h, it, lv, 4, 2) == 1 &&
                  warding_grid_cell(h, it, lv, 4, 3) == 0 &&
                  warding_grid_cell(h, it, lv, 4, 4) == WARDING_REFUSED,
              "warding: the access grid");
    }
    CHECK(warding_node_ok(1, 1, 7, 19, 1760000000) && !warding_node_ok(10000, 1, 1, 1, 1) &&
              !warding_node_ok(1, 10, 1, 1, 1) && !warding_node_ok(1, 1, 1, 1, 1000000000000),
          "warding: a holder node fits ^ZXWARD");

    /* WM2: the seats, the passkeys and the roster, all read and checked */
    zxm_link_init(&WD_LINK, wd_send, &WD_LINK);
    warding_init(&WD, &WD_LINK);
    CHECK(warding_load(&WD) == ZXM_OK && WD.seats.nseats == 3 && WD.seats.nkeys == 2 &&
              WD.ndev == 3 && WD.self == 0 && WD.dev[2].revoked && !WD.dev[1].revoked,
          "warding: loaded: 3 seats, 2 passkeys, 3 devices (%s)", WD_LINK.err);
    wd_keys_off = true;
    CHECK(warding_load(&WD) == ZXM_OK && WD.seats.nkeys == 0 && WD.ndev == 3,
          "warding: passkeys switched off: none listed, the rest read");
    wd_keys_off = false;
    CHECK(warding_load(&WD) == ZXM_OK && WD.seats.nkeys == 2, "warding: passkeys back on");

    /* WM3: issued only on the owner's word; nothing sent, nothing kept */
    int before = wd_calls;
    CHECK(warding_issue(&WD, WD_OWNER, 19, WD_PERSON, WARDING_DOOR, 1, NULL, 365, WD_NOW, 32, 256,
                        false) == ZXM_E_STATE &&
              WD.nkeys == 0 && WD.last_serial == 0 && wd_calls == before,
          "warding: issue without the owner's word: refused, nothing sent or kept");
    CHECK(warding_issue(&WD, WD_OWNER, 19, WD_PERSON, WARDING_DOOR, 1, NULL, 365, WD_NOW, 32, 256,
                        true) == ZXM_OK &&
              WD.nkeys == 1 && WD.last_serial == 19 && !WD.key[0].weak && WD.key[0].bits == 256 &&
              WD.key[0].place == 2 && WD.key[0].expires == WD_NOW + 365 * 86400 &&
              warding_active(&WD, 19, WD_NOW) && wd_calls == before,
          "warding: key 19, door 1, to Bo: active (%s)", WD.notice);
    CHECK(warding_issue(&WD, WD_OWNER, 19, WD_OWNER, WARDING_DOOR, 2, NULL, 30, WD_NOW, 32, 256,
                        true) == ZXM_E_ARG &&
              warding_issue(&WD, WD_OWNER, 18, WD_OWNER, WARDING_DOOR, 2, NULL, 30, WD_NOW, 32, 256,
                            true) == ZXM_E_ARG &&
              warding_issue(&WD, WD_OWNER, WARDING_SERIAL_MAX + 1, WD_OWNER, WARDING_DOOR, 2, NULL,
                            30, WD_NOW, 32, 256, true) == ZXM_E_ARG &&
              warding_issue(&WD, WD_OWNER, 20, WD_OWNER, WARDING_DOOR, 2, NULL, 3651, WD_NOW, 32,
                            256, true) == ZXM_E_ARG &&
              warding_issue(&WD, WD_OWNER, 20, WD_OWNER, WARDING_DOOR, 2, NULL, 0, WD_NOW, 32, 256,
                            true) == ZXM_E_ARG &&
              warding_issue(&WD, WD_OWNER, 20, WD_OWNER, WARDING_DEVICE, 2, WD_DEV_C, 30, WD_NOW,
                            32, 256, true) == ZXM_E_ARG &&
              warding_issue(&WD, WD_OWNER, 20, WD_OWNER, WARDING_ACCOUNT, 2, WD_K2, 30, WD_NOW, 32,
                            256, true) == ZXM_E_ARG &&
              warding_issue(&WD, WD_OWNER, 20, WD_OWNER, WARDING_DOOR, 2, NULL, 30, WD_NOW, 4097,
                            256, true) == ZXM_E_ARG &&
              warding_issue(&WD, WD_OWNER, 20, WD_OWNER, WARDING_DOOR, 10000, NULL, 30, WD_NOW, 32,
                            256, true) == ZXM_E_ARG &&
              WD.nkeys == 1 && WD.last_serial == 19,
          "warding: a reused or falling serial, past 9(9), an expiry past the limits, a revoked "
          "device or passkey, too long, an item past 4 digits: refused");
    CHECK(
        warding_issue(&WD, WD_OWNER, 20, WD_GUEST, WARDING_DOOR, 2, NULL, 30, WD_NOW, 32, 256,
                      true) == ZXM_E_STATE &&
            warding_issue(&WD, WD_PERSON, 20, WD_OWNER, WARDING_DOOR, 2, NULL, 30, WD_NOW, 32, 256,
                          true) == ZXM_E_STATE &&
            warding_issue(&WD, WD_GUEST, 20, WD_GUEST, WARDING_DOOR, 2, NULL, 30, WD_NOW, 32, 256,
                          true) == ZXM_E_STATE &&
            WD.nkeys == 1,
        "warding: role rights (threshold.rights_ok): no key to a guest, a person not for another");
    CHECK(warding_issue(&WD, WD_OWNER, 20, WD_OWNER, WARDING_DEVICE, 2, WD_DEV_B, 90, WD_NOW, 32,
                        256, true) == ZXM_OK &&
              warding_issue(&WD, WD_OWNER, 21, WD_PERSON, WARDING_ACCOUNT, 1, WD_K1, 90, WD_NOW, 32,
                            256, true) == ZXM_OK &&
              warding_issue(&WD, WD_PERSON, 22, WD_PERSON, WARDING_DOOR, 3, NULL, 10, WD_NOW, 8, 10,
                            true) == ZXM_OK &&
              WD.nkeys == 4 && WD.key[3].weak && WD.key[3].bits == 26 &&
              strstr(WD.notice, "below 128 bits") && warding_active(&WD, 22, WD_NOW),
          "warding: a device key, a passkey, and a weak door key issued with advice (%s)",
          WD.notice);

    /* WM5: expiry through lockbox.days_left */
    CHECK(warding_active(&WD, 22, WD_NOW + 9 * 86400) &&
              !warding_active(&WD, 22, WD_NOW + 10 * 86400) && !warding_active(&WD, 99, WD_NOW),
          "warding: key 22 lives 10 days: on day 10 it is expired");

    /* WM4, the BOTH test: key 19 revoked on device A (this one), still
     * active on device B: locked out everywhere */
    CHECK(warding_device_says(&WD, 19, 1, false) == ZXM_OK &&
              warding_state(&WD, 19) == LPRES_STATE_TRUE && warding_active(&WD, 19, WD_NOW),
          "warding: key 19: device B says active");
    before = wd_calls;
    CHECK(warding_revoke(&WD, WD_OWNER, 19, false) == ZXM_E_STATE && wd_calls == before &&
              warding_state(&WD, 19) == LPRES_STATE_TRUE,
          "warding: a revoke without the owner's word: refused, nothing sent, nothing changed");
    CHECK(warding_revoke(&WD, WD_OWNER, 19, true) == ZXM_OK && wd_calls == before &&
              warding_state(&WD, 19) == LPRES_STATE_BOTH && !warding_active(&WD, 19, WD_NOW) &&
              strstr(WD.notice, "locked out everywhere"),
          "warding: key 19 revoked on A, active on B: BOTH, locked out (%s)", WD.notice);
    CHECK(lpres_conjoin(warding_state(&WD, 19), LPRES_STATE_TRUE) == LPRES_STATE_BOTH &&
              warding_door(&WD, WD_PERSON, 19, 0, false, WD_NOW, true) == ZXM_E_STATE &&
              !WD.door_held,
          "warding: a BOTH key opens no door");
    CHECK(warding_device_says(&WD, 19, 1, true) == ZXM_OK &&
              warding_state(&WD, 19) == LPRES_STATE_FALSE && !warding_active(&WD, 19, WD_NOW) &&
              warding_device_says(&WD, 19, 3, true) == ZXM_E_ARG &&
              warding_device_says(&WD, 77, 0, true) == ZXM_E_ARG,
          "warding: the revocation reaches B: revoked everywhere (FALSE)");

    /* WM6: a passkey and a device revoked through zxvd, on the owner's word */
    before = wd_calls;
    CHECK(warding_revoke(&WD, WD_OWNER, 21, false) == ZXM_E_STATE && wd_calls == before &&
              !wd_k1_revoked && warding_revoke(&WD, WD_GUEST, 21, true) == ZXM_E_STATE &&
              wd_calls == before,
          "warding: a passkey revoke without the word or the rights: nothing sent");
    CHECK(warding_revoke(&WD, WD_OWNER, 21, true) == ZXM_OK && wd_k1_revoked &&
              wd_calls == before + 1 && warding_state(&WD, 21) == LPRES_STATE_FALSE &&
              !warding_active(&WD, 21, WD_NOW),
          "warding: passkey K1 revoked (passkeys.revoke): %s", WD_LINK.err);
    CHECK(warding_revoke(&WD, WD_OWNER, 20, true) == ZXM_OK && wd_b_revoked && WD.dev[1].revoked &&
              warding_state(&WD, 20) == LPRES_STATE_FALSE && wd_revokes == 2,
          "warding: device B revoked (devmesh.revoke)");
    CHECK(warding_load(&WD) == ZXM_OK && WD.seats.key[0].revoked && WD.dev[1].revoked &&
              warding_state(&WD, 21) == LPRES_STATE_FALSE &&
              warding_state(&WD, 20) == LPRES_STATE_FALSE,
          "warding: read again, zxvd lists both revoked");

    /* WM7: a door needs the owner; an AI's suggestion is held, never run */
    CHECK(warding_issue(&WD, WD_OWNER, 23, WD_OWNER, WARDING_DOOR, 1, NULL, 365, WD_NOW, 32, 256,
                        true) == ZXM_OK,
          "warding: key 23, door 1, to Ada");
    CHECK(
        warding_door(&WD, WD_OWNER, 23, 0, false, WD_NOW, false) == ZXM_E_STATE && !WD.door_held &&
            warding_door(&WD, WD_OWNER, 23, 0, true, WD_NOW, true) == ZXM_E_STATE &&
            !WD.door_held && strstr(WD.notice, "held for the owner") &&
            warding_door(&WD, WD_PERSON, 22, 0, false, WD_NOW, true) == ZXM_E_STATE &&
            !WD.door_held && warding_door(&WD, WD_OWNER, 23, 2, false, WD_NOW, true) == ZXM_E_ARG &&
            warding_door(&WD, WD_OWNER, 20, 0, false, WD_NOW, true) == ZXM_E_ARG,
        "warding: no word, an AI's suggestion, a person's seat, no setpoint, not a door: refused");
    before = wd_calls;
    CHECK(warding_door(&WD, WD_OWNER, 23, 0, false, WD_NOW, true) == ZXM_OK && WD.door_held &&
              WD.door_serial == 23 && WD.door_item == 1 && WD.door_setpoint == 0 &&
              wd_calls == before,
          "warding: the owner unlocks door 1: kept for the bridge, nothing sent (%s)", WD.notice);

    /* WM8: the access grid: Ada holds key 23 on door 1, Bo key 22 on door 3
     * (live); 19, 20, 21 are revoked */
    {
        int64_t cell[WARDING_KEYS];
        uint32_t n = warding_grid(&WD, WD_NOW, cell);
        CHECK(n == 5 && cell[0] == 0 && cell[1] == 0 && cell[2] == 0 && cell[3] == 1 &&
                  cell[4] == 1,
              "warding: the grid, holders by doors and devices");
    }
}
