/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gatehouse's model against zxvd's real dispatcher (LINK): what each app
 * asked, the owner's confirmed grants and revokes, location kept by zxvd
 * (place.apps, place.permit) and zxvd's clock (sys.info). */
#include "gatehouse_model.h"

static gatehouse_t GH, GH2;

static int gh_place_ask(const char *app)
{
    zxm_begin(&LINK, "place.ask");
    zxm_p_str(&LINK, "app", app);
    return zxm_call(&LINK);
}

APPS_TEST(gatehouse)
{
    /* the rules (GR2-GR4) */
    static const int64_t flags[4] = {1, 0, 1, 1}, none[1] = {0}, bad[3] = {1, 2, 0},
                         neg[2] = {1, -1};
    CHECK(gatehouse_use_ok(1, 1, 0) && !gatehouse_use_ok(0, 1, 0) && !gatehouse_use_ok(1, 0, 0) &&
              !gatehouse_use_ok(1, 1, 1) && !gatehouse_use_ok(2, 1, 0) &&
              !gatehouse_use_ok(1, 1, -1),
          "use_ok: asked and granted and not revoked; a flag that is no flag is no");
    CHECK(gatehouse_use_allowed(1, 1, 0) == 1 && gatehouse_use_allowed(1, 0, 0) == 0 &&
              gatehouse_use_allowed(0, 0, 1) == 0 &&
              gatehouse_use_allowed(2, 1, 0) == GATEHOUSE_REFUSED &&
              gatehouse_use_allowed(1, -1, 0) == GATEHOUSE_REFUSED &&
              gatehouse_use_allowed(1, 1, INT64_MAX) == GATEHOUSE_REFUSED,
          "use_allowed: 1 or 0, refused past a flag");
    CHECK(gatehouse_holders(flags, 4) == 3 && gatehouse_holders(none, 1) == 0 &&
              gatehouse_holders(flags, 0) == 0 && gatehouse_holders(bad, 3) == GATEHOUSE_REFUSED &&
              gatehouse_holders(neg, 2) == GATEHOUSE_REFUSED &&
              gatehouse_holders(NULL, 1) == GATEHOUSE_REFUSED,
          "holders: a count of flags, refused past a flag");
    CHECK(gatehouse_cap_index("net") == 0 &&
              gatehouse_cap_index("location") == GATEHOUSE_LOCATION &&
              gatehouse_cap_index("notify") == 18 &&
              gatehouse_cap_index("proc.control") == 24 &&
              gatehouse_cap_index("power.read") == (int32_t) GATEHOUSE_CAPS - 1 &&
              gatehouse_cap_index("teleport") == -1 && gatehouse_cap_index("") == -1 &&
              gatehouse_cap_index(NULL) == -1,
          "capability names (GR1)");

    /* what the manifests ask (GH1): sends nothing */
    CHECK(zpl_open(NULL) == 0, "places in memory");
    gatehouse_init(&GH, &LINK);
    int before = sent;
    CHECK(gatehouse_ask(&GH, "glint", "camera") == ZXM_OK &&
              gatehouse_ask(&GH, "glint", "fs.read") == ZXM_OK &&
              gatehouse_ask(&GH, "birdsong", "mic") == ZXM_OK &&
              gatehouse_ask(&GH, "birdsong", "fs.read") == ZXM_OK &&
              gatehouse_ask(&GH, "birdsong", "mic") == ZXM_OK && GH.napps == 2 && GH.waiting == 4 &&
              sent == before,
          "asked, nothing sent");
    CHECK(!gatehouse_allowed(&GH, "glint", "camera") && gatehouse_count(&GH, "camera") == 0 &&
              strstr(GH.notice, "4 asks wait for your answer: glint (fs.read) glint (camera) "
                                "birdsong (fs.read) birdsong (mic). Nothing changes"),
          "asked is not allowed: %s", GH.notice);
    CHECK(gatehouse_ask(&GH, "glint", "teleport") == ZXM_E_ARG &&
              gatehouse_ask(&GH, "", "camera") == ZXM_E_ARG &&
              gatehouse_ask(&GH, "two words", "camera") == ZXM_E_ARG &&
              gatehouse_ask(&GH, "a23456789012345678901234567890123", "net") == ZXM_E_ARG &&
              gatehouse_ask(&GH, NULL, "net") == ZXM_E_ARG && GH.napps == 2,
          "bad asks refused");

    /* the owner's word (GH2): confirmed, and only for what was asked */
    before = sent;
    CHECK(gatehouse_grant(&GH, "glint", "camera", false) == ZXM_E_STATE &&
              gatehouse_grant(&GH, "glint", "mic", true) == ZXM_E_STATE &&
              gatehouse_grant(&GH, "nobody", "mic", true) == ZXM_E_STATE &&
              gatehouse_revoke(&GH, "nobody", "mic", true) == ZXM_E_STATE &&
              gatehouse_grant(&GH, "glint", "teleport", true) == ZXM_E_ARG && sent == before &&
              !gatehouse_allowed(&GH, "glint", "camera"),
          "not confirmed, not asked or unknown: nothing sent, nothing changed");
    CHECK(gatehouse_grant(&GH, "glint", "camera", true) == ZXM_OK &&
              gatehouse_grant(&GH, "birdsong", "mic", true) == ZXM_OK &&
              gatehouse_grant(&GH, "birdsong", "fs.read", true) == ZXM_OK && sent == before + 3 &&
              GH.app[0].changed_ms > 0 && GH.now_ms >= GH.app[0].changed_ms,
          "granted, timed by zxvd's clock: %s", LINK.err);
    CHECK(gatehouse_allowed(&GH, "glint", "camera") && gatehouse_allowed(&GH, "birdsong", "mic") &&
              !gatehouse_allowed(&GH, "glint", "fs.read") && gatehouse_count(&GH, "fs.read") == 1 &&
              gatehouse_count(&GH, "camera") == 1 && gatehouse_count(&GH, "net") == 0 &&
              gatehouse_count(&GH, "teleport") == -1 && GH.waiting == 1 &&
              strstr(GH.notice, "1 ask waits for your answer: glint (fs.read). Nothing"),
          "granted vs asked: %s", GH.notice);
    CHECK(gatehouse_revoke(&GH, "birdsong", "mic", true) == ZXM_OK &&
              !gatehouse_allowed(&GH, "birdsong", "mic") && gatehouse_count(&GH, "mic") == 0 &&
              (gatehouse_find(&GH, "birdsong")->granted & (1u << 8)),
          "revoked: the grant stays on record, the use is not allowed");
    CHECK(gatehouse_revoke(&GH, "glint", "fs.read", true) == ZXM_OK && GH.waiting == 0 &&
              !GH.notice[0],
          "an ask refused: nothing waits");
    CHECK(gatehouse_grant(&GH, "birdsong", "mic", true) == ZXM_OK &&
              gatehouse_allowed(&GH, "birdsong", "mic") && gatehouse_count(&GH, "mic") == 1,
          "granted again: the revoke is cleared");

    /* location is zxvd's (GH3): an ask waits there, the answer goes there */
    CHECK(gh_place_ask("waymark") == ZXM_OK && gatehouse_refresh(&GH) == ZXM_OK && GH.napps == 3 &&
              gatehouse_find(&GH, "waymark")->asked == (1u << GATEHOUSE_LOCATION) &&
              !gatehouse_allowed(&GH, "waymark", "location") && GH.waiting == 1 &&
              strstr(GH.notice, "waymark (location)"),
          "a location ask from zxvd: %s", LINK.err);
    CHECK(gatehouse_grant(&GH, "waymark", "location", true) == ZXM_OK &&
              gatehouse_allowed(&GH, "waymark", "location") &&
              gatehouse_count(&GH, "location") == 1,
          "granted: %s", LINK.err);
    CHECK(gh_place_ask("waymark") == ZXM_OK && strstr(LINK.resp, "\"answer\":\"unknown\""),
          "zxvd took the owner's answer (allowed; no location is set to give): %s", LINK.resp);
    CHECK(gatehouse_revoke(&GH, "waymark", "location", true) == ZXM_OK &&
              !gatehouse_allowed(&GH, "waymark", "location") && gh_place_ask("waymark") == ZXM_OK &&
              strstr(LINK.resp, "\"answer\":\"never\""),
          "revoked in zxvd too: %s", LINK.resp);
    gatehouse_init(&GH2, &LINK);
    CHECK(gatehouse_refresh(&GH2) == ZXM_OK && GH2.napps == 1 &&
              !gatehouse_allowed(&GH2, "waymark", "location") &&
              GH2.app[0].revoked == (1u << GATEHOUSE_LOCATION) && GH2.now_ms > 0,
          "a second model reads the location answers back: %s", LINK.err);
    CHECK(gatehouse_ask(&GH2, "glint", "location") == ZXM_OK &&
              gatehouse_grant(&GH2, "glint", "location", true) == ZXM_OK &&
              gatehouse_refresh(&GH) == ZXM_OK && GH.napps == 3 &&
              (gatehouse_find(&GH, "glint")->granted & (1u << GATEHOUSE_LOCATION)) &&
              !gatehouse_allowed(&GH, "glint", "location") &&
              gatehouse_allowed(&GH2, "glint", "location"),
          "granted elsewhere: granted here, but not asked here, so not allowed");

    /* made up replies: nothing out of bounds changes what was shown (GH4) */
    gatehouse_t *F = &GH2;
    gatehouse_init(F, &FAKE);
    CHECK(gatehouse_ask(F, "glint", "camera") == ZXM_OK, "one ask");
    static const char *const replies[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":5,\"apps\":[{\"app\":\"x\","
        "\"mode\":\"sometimes\",\"pending\":false,\"asked\":1}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":5,\"apps\":[{\"app\":\"x\","
        "\"mode\":\"always\",\"pending\":true,\"asked\":1}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":5,\"apps\":[{\"app\":\"x y\","
        "\"mode\":\"ask\",\"pending\":true,\"asked\":1}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":5,\"apps\":[{\"app\":\"x\","
        "\"mode\":\"ask\",\"pending\":true,\"asked\":-1}]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":-5,\"apps\":[]}}",
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":5,\"apps\":{}}}",
    };
    for (uint32_t i = 0; i < sizeof replies / sizeof replies[0]; i++) {
        CANNED = replies[i];
        CHECK(gatehouse_refresh(F) == ZXM_E_PROTOCOL && F->napps == 1 && F->now_ms == 0 &&
                  F->waiting == 1 && !strcmp(F->app[0].app, "glint"),
              "reply %u refused, nothing changed", i);
    }
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":7,\"apps\":[{\"app\":"
             "\"glint\",\"mode\":\"always\",\"pending\":false,\"asked\":0}],\"mode\":\"ask\"}}";
    CHECK(gatehouse_refresh(F) == ZXM_OK && F->now_ms == 7 && F->napps == 1 &&
              !gatehouse_allowed(F, "glint", "location") &&
              gatehouse_grant(F, "glint", "camera", true) == ZXM_OK &&
              gatehouse_allowed(F, "glint", "camera"),
          "a good reply: %s", LINK.err);
    CHECK(gatehouse_ask(F, "glint", "location") == ZXM_OK &&
              gatehouse_revoke(F, "glint", "location", true) == ZXM_E_PROTOCOL &&
              (gatehouse_find(F, "glint")->granted & (1u << GATEHOUSE_LOCATION)) &&
              !(gatehouse_find(F, "glint")->revoked & (1u << GATEHOUSE_LOCATION)),
          "zxvd says another mode back: the revoke is not taken");
    CANNED = NULL;

    /* a full table refuses another app */
    gatehouse_init(F, &LINK);
    char name[8];
    bool ok = true;
    for (uint32_t i = 0; ok && i < GATEHOUSE_APPS; i++) {
        name[0] = 'a', name[1] = (char) ('a' + i / 26u), name[2] = (char) ('a' + i % 26u),
        name[3] = 0;
        ok = gatehouse_ask(F, name, "net") == ZXM_OK;
    }
    CHECK(ok && F->napps == GATEHOUSE_APPS && gatehouse_ask(F, "zzz", "net") == ZXM_E_TOOBIG &&
              gatehouse_ask(F, "aaa", "mic") == ZXM_OK && F->waiting == GATEHOUSE_APPS + 1u &&
              strstr(F->notice, " ...") && strlen(F->notice) < GATEHOUSE_NOTICE_MAX,
          "full: %u, %s", F->napps, F->notice);
    zpl_close();
}
