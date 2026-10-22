/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Toolchest's model against zxvd's real dispatcher (LINK): zxvd.version,
 * rpc.discover and sys.disk as zxvd answers them. update.check and
 * play.catalogue are not linked in this in-process zxvd, so a check, the
 * Playground's samples and a disk of a known size for the offers are
 * answered on a link of its own (TKC_LINK), which passes every other
 * request to the real dispatcher. Nothing is ever installed: zxvd has no
 * method that installs a kit. */
#include "toolchest_model.h"

static toolchest_t TKC, TKC2;
static zxm_link_t TKC_LINK;
static const char *TKC_DISK;    /* sys.disk's result, when set */
static const char *TKC_UPDATE;  /* update.check's result, when set */
static const char *TKC_SAMPLES; /* play.catalogue's result, when set */

static int32_t tkc_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"sys.disk\"")) r = TKC_DISK;
    if (strstr(req, "\"update.check\"")) r = TKC_UPDATE;
    if (strstr(req, "\"play.catalogue\"")) r = TKC_SAMPLES;
    if (r) return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
    return inproc(NULL, req, len, resp, cap);
}

APPS_TEST(toolchest)
{
    toolchest_version_t v, k, d;
    int before;

    /* the catalogue: the eight kits of sdk/kits, libzxv and the language SDKs */
    CHECK(toolchest_catalogue_n() == 20 && toolchest_find("Z-Wasm") == 0 &&
              toolchest_find("ZAB-Logic") == 7 && toolchest_find("libzxv") == 8 &&
              toolchest_find("Solidity SDK") == 19 && toolchest_find("nope") == -1 &&
              toolchest_find(NULL) == -1 && toolchest_catalogue(20) == NULL &&
              toolchest_catalogue(12)->lang == TOOLCHEST_LANG_PYTHON &&
              !strcmp(toolchest_catalogue(4)->folder, "kits/aipi"),
          "toolchest: the catalogue");

    /* TM1: the version form */
    CHECK(toolchest_parse("1.6.0", &v) == ZXM_OK && v.major == 1 && v.minor == 6 && v.patch == 0 &&
              toolchest_parse("999.999.999", &v) == ZXM_OK && v.patch == 999 &&
              toolchest_parse("0.0.0", &v) == ZXM_OK && v.major == 0,
          "toolchest: versions that parse");
    CHECK(toolchest_parse("1000.0.0", &v) == ZXM_E_ARG &&
              toolchest_parse("01.0.0", &v) == ZXM_E_ARG &&
              toolchest_parse("1.2", &v) == ZXM_E_ARG &&
              toolchest_parse("1.2.3.4", &v) == ZXM_E_ARG &&
              toolchest_parse("1..3", &v) == ZXM_E_ARG &&
              toolchest_parse("-1.2.3", &v) == ZXM_E_ARG && toolchest_parse("", &v) == ZXM_E_ARG &&
              toolchest_parse(NULL, &v) == ZXM_E_ARG,
          "toolchest: versions that do not parse");

    /* TC1-TC6 through the model's own C */
    k = (toolchest_version_t){1, 3, 0};
    d = (toolchest_version_t){1, 6, 0};
    CHECK(toolchest_fits(&k, &d) == 1 && toolchest_fits(&d, &k) == 0 &&
              toolchest_fits(&d, &d) == 1 && toolchest_version_fits(1, 0, 0, 2, 0, 0) == 0 &&
              toolchest_version_fits(1, 6, 1, 1, 6, 0) == 0 &&
              toolchest_version_fits(1, 0, 0, 1000, 0, 0) == TOOLCHEST_REFUSED &&
              toolchest_fits(NULL, &d) == TOOLCHEST_REFUSED,
          "toolchest: fits (TC1)");
    CHECK(toolchest_install_allowed(1, 1, 1, 0) && !toolchest_install_allowed(1, 1, 1, -1) &&
              !toolchest_install_allowed(2, 1, 1, 0) && !toolchest_install_allowed(1, 1, 0, 5) &&
              !toolchest_install_allowed(1, 0, 1, 5),
          "toolchest: install allowed (TC2)");
    {
        int64_t b[3] = {2097152, 1, 1000}, bad[2] = {1, 0}, f[3] = {0, 999999999999, 1},
                big[1] = {1000000000000};
        CHECK(toolchest_download_bytes(b, 3) == 2098153 &&
                  toolchest_download_bytes(bad, 2) == TOOLCHEST_REFUSED &&
                  toolchest_download_bytes(b, 0) == TOOLCHEST_REFUSED &&
                  toolchest_download_bytes(NULL, 1) == TOOLCHEST_REFUSED &&
                  toolchest_installed_bytes(f, 3) == 1000000000000 &&
                  toolchest_installed_bytes(big, 1) == TOOLCHEST_REFUSED,
              "toolchest: sizes (TC3, TC4)");
    }
    CHECK(toolchest_kit_record_fits(999, 99, 999, 999, 999, 999999999999999) &&
              !toolchest_kit_record_fits(1000, 0, 0, 0, 0, 0) &&
              !toolchest_kit_record_fits(1, 100, 0, 0, 0, 0) &&
              toolchest_used_x100(1000, 400) == 6000 &&
              toolchest_used_x100(0, 0) == TOOLCHEST_REFUSED &&
              toolchest_used_x100(1000, -1) == TOOLCHEST_REFUSED,
          "toolchest: the record and the disk after (TC5, TC6)");

    /* the real dispatcher: its version, its methods, its samples and its disk */
    toolchest_init(&TKC, &LINK);
    before = sent;
    CHECK(toolchest_offer(&TKC, 0, "0.2.0", "1.2.0", (const int64_t[]){1}, 1, (const int64_t[]){1},
                          1, 1, 1) == ZXM_E_STATE &&
              sent == before && TKC.pending == -1,
          "toolchest: nothing judged before the ABI is known, nothing asked");
    CHECK(toolchest_read_daemon(&TKC) == ZXM_OK && TKC.known && TKC.methods > 100 &&
              TKC.fab_receive && TKC.abi.major >= 1 &&
              strstr(TKC.notice, "no method that installs"),
          "toolchest: zxvd's own version and methods: %s (%s)", TKC.notice, LINK.err);
    CHECK(toolchest_read_samples(&TKC) == ZXM_E_REMOTE && TKC.samples == 0 && TKC.kept == 0,
          "toolchest: no Playground in this harness: %s", LINK.err);
    CHECK(toolchest_read_disk(&TKC) == ZXM_OK && TKC.disk_known && TKC.disk_total > 0 &&
              TKC.disk_avail <= TKC.disk_total,
          "toolchest: the Hoard's volume: %s (%s)", TKC.notice, LINK.err);
    CHECK(toolchest_check_update(&TKC) == ZXM_E_REMOTE && !TKC.status[0],
          "toolchest: no update host in this harness: %s", LINK.err);
    CHECK(toolchest_confirm(&TKC, true) == ZXM_E_STATE, "toolchest: no offer waits");

    /* its own link: a disk of a known size, and update.check answered */
    zxm_link_init(&TKC_LINK, tkc_send, &TKC_LINK);
    toolchest_init(&TKC2, &TKC_LINK);
    TKC_DISK = "{\"path\":\"/h\",\"total\":10000000,\"free\":6000000,\"avail\":5000000}";
    CHECK(toolchest_read_daemon(&TKC2) == ZXM_OK && toolchest_read_disk(&TKC2) == ZXM_OK &&
              TKC2.disk_total == 10000000 && TKC2.disk_avail == 5000000,
          "toolchest: a disk of 10 MB, 5 MB to write");
    TKC_UPDATE = "{\"status\":\"up to date\"}";
    CHECK(toolchest_check_update(&TKC2) == ZXM_OK && !strcmp(TKC2.status, "up to date") &&
              strstr(TKC2.notice, "Nothing is installed"),
          "toolchest: an update check: %s", TKC2.notice);
    TKC_UPDATE = "{\"status\":\"\"}";
    CHECK(toolchest_check_update(&TKC2) == ZXM_E_PROTOCOL && !strcmp(TKC2.status, "up to date"),
          "toolchest: an empty status is no answer; nothing kept");
    TKC_SAMPLES = "[{\"id\":\"treaty-fee\",\"lang\":\"sutra\",\"title\":\"t\",\"files\":[]},"
                  "{\"id\":\"shares\",\"lang\":\"kron\"},{\"id\":\"limits\",\"lang\":\"eve\"},"
                  "{\"id\":\"fee-layout\",\"lang\":\"sutra\"}]";
    CHECK(toolchest_read_samples(&TKC2) == ZXM_OK && TKC2.samples == 4 && TKC2.kept == 4 &&
              !strcmp(TKC2.sample[1].id, "shares") && toolchest_samples_in(&TKC2, "sutra") == 2 &&
              toolchest_samples_in(&TKC2, "kron") == 1 &&
              toolchest_samples_in(&TKC2, "no such language") == 0 &&
              !strcmp(TKC2.notice, "4 Playground samples."),
          "toolchest: the Playground's samples: %s", TKC2.notice);
    TKC_SAMPLES = "[{\"id\":\"a\",\"lang\":\"kron\"},{\"id\":\"\",\"lang\":\"kron\"}]";
    CHECK(toolchest_read_samples(&TKC2) == ZXM_E_PROTOCOL && TKC2.samples == 4 &&
              !strcmp(TKC2.sample[0].id, "treaty-fee"),
          "toolchest: a sample without an id is no sample; nothing kept");
    TKC_SAMPLES = "{\"id\":\"a\"}";
    CHECK(toolchest_read_samples(&TKC2) == ZXM_E_PROTOCOL && TKC2.samples == 4,
          "toolchest: a catalogue that is no list");
    TKC_SAMPLES = NULL;

    /* TM4: offers judged against the running ABI, each refused for the first reason */
    char run[64], old[64], next[64];
    toolchest_version_t r = TKC2.abi;
    snprintf(run, sizeof run, "%lld.%lld.%lld", (long long) r.major, (long long) r.minor,
             (long long) r.patch);
    snprintf(old, sizeof old, "%lld.0.0", (long long) r.major);
    snprintf(next, sizeof next, "%lld.0.0", (long long) r.major + 1);
    static const int64_t blocks[3] = {2097152, 2097152, 805696}; /* 5000000 bytes */
    static const int64_t files[2] = {1000000, 0};
    CHECK(toolchest_offer(&TKC2, 0, "0.2.0", old, blocks, 3, files, 2, 1, 0) == ZXM_OK &&
              TKC2.kit[0].outcome == TOOLCHEST_UNSIGNED && TKC2.pending == -1 &&
              strstr(TKC2.notice, "second not verified"),
          "toolchest: one signature only: %s", TKC2.notice);
    CHECK(toolchest_offer(&TKC2, 0, "0.2.0", old, blocks, 3, files, 2, 2, 1) == ZXM_OK &&
              TKC2.kit[0].outcome == TOOLCHEST_UNSIGNED,
          "toolchest: a verify result of 2 is no result");
    CHECK(toolchest_offer(&TKC2, 1, "0.2.0", next, blocks, 3, files, 2, 1, 1) == ZXM_OK &&
              TKC2.kit[1].outcome == TOOLCHEST_NO_FIT && TKC2.kit[1].fits == 0 &&
              TKC2.pending == -1 && strstr(TKC2.notice, "needs libzxv"),
          "toolchest: a kit for the next major: %s", TKC2.notice);
    CHECK(toolchest_offer(&TKC2, 2, "0.1.0", old, blocks, 3, files, 2, 1, 1) == ZXM_OK &&
              TKC2.kit[2].outcome == TOOLCHEST_NO_ROOM && TKC2.kit[2].need == 6000000 &&
              TKC2.kit[2].room == -1000000 && TKC2.kit[2].used_after_x100 == TOOLCHEST_REFUSED &&
              strstr(TKC2.notice, "needs 6000000 bytes"),
          "toolchest: 6 MB on 5 MB: %s", TKC2.notice);
    before = sent;
    CHECK(toolchest_offer(&TKC2, 20, "0.1.0", old, blocks, 3, files, 2, 1, 1) == ZXM_E_ARG &&
              toolchest_offer(&TKC2, 3, "01.1.0", old, blocks, 3, files, 2, 1, 1) == ZXM_E_ARG &&
              toolchest_offer(&TKC2, 3, "0.1.0", NULL, blocks, 3, files, 2, 1, 1) == ZXM_E_ARG &&
              toolchest_offer(&TKC2, 3, "0.1.0", old, files, 2, files, 2, 1, 1) == ZXM_E_ARG &&
              toolchest_offer(&TKC2, 3, "0.1.0", old, blocks, 3, files, 0, 1, 1) == ZXM_E_ARG &&
              sent == before && !TKC2.kit[3].known,
          "toolchest: offers that are no release: nothing asked, nothing kept");

    /* TM4: signed, fitting and with room: it waits for the owner */
    static const int64_t small[2] = {1048576, 1048576}, sfiles[3] = {1000000, 500000, 0};
    CHECK(toolchest_offer(&TKC2, 12, "1.0.0", run, small, 2, sfiles, 3, 1, 1) == ZXM_OK &&
              TKC2.kit[12].outcome == TOOLCHEST_ALLOWED && TKC2.pending == 12 &&
              TKC2.kit[12].download == 2097152 && TKC2.kit[12].installed == 1500000 &&
              TKC2.kit[12].room == 1402848 && TKC2.kit[12].used_after_x100 == 8597 &&
              strstr(TKC2.notice, "waits for your word"),
          "toolchest: allowed: %s", TKC2.notice);
    CHECK(toolchest_offer(&TKC2, 0, "0.2.0", old, small, 2, sfiles, 3, 1, 1) == ZXM_E_STATE &&
              TKC2.kit[0].outcome == TOOLCHEST_UNSIGNED,
          "toolchest: one offer waits at a time");
    CHECK(toolchest_confirm(&TKC2, true) == ZXM_OK && TKC2.pending == -1 &&
              TKC2.kit[12].outcome == TOOLCHEST_APPROVED &&
              strstr(TKC2.notice, "Toolchest installs nothing") &&
              strstr(TKC2.notice, "sdk/python"),
          "toolchest: approved, nothing installed: %s", TKC2.notice);
    CHECK(toolchest_confirm(&TKC2, true) == ZXM_E_STATE, "toolchest: a word is spent once");
    CHECK(toolchest_offer(&TKC2, 8, run, run, small, 2, sfiles, 3, 1, 1) == ZXM_OK &&
              TKC2.pending == 8 && toolchest_confirm(&TKC2, false) == ZXM_OK &&
              TKC2.kit[8].outcome == TOOLCHEST_DECLINED &&
              strstr(TKC2.notice, "You declined libzxv"),
          "toolchest: declined: %s", TKC2.notice);

    /* TM5: every kept record fits ^ZXTOOLCHEST */
    bool fit = true;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < toolchest_catalogue_n(); i++)
        if (TKC2.kit[i].known) kept++, fit = fit && toolchest_record_ok(&TKC2, i);
    CHECK(fit && kept == 5 && !toolchest_record_ok(&TKC2, 3) && !toolchest_record_ok(&TKC2, 20),
          "toolchest: every record fits ^ZXTOOLCHEST: %u", kept);

    /* answers that are not zxvd's: nothing kept */
    TKC_DISK = "{\"path\":\"/h\",\"total\":100,\"free\":100,\"avail\":200}";
    CHECK(toolchest_read_disk(&TKC2) == ZXM_E_PROTOCOL && TKC2.disk_avail == 5000000,
          "toolchest: more to write than the volume holds is no disk");
    TKC_DISK = NULL;
    TKC_UPDATE = NULL;
    toolchest_init(&TKC2, &FAKE);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"app\":\"x\",\"libzxv\":\"nine\"}}";
    CHECK(toolchest_read_daemon(&TKC2) == ZXM_E_PROTOCOL && !TKC2.known,
          "toolchest: a libzxv version that is none: not zxvd's answer");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":[{\"id\":\"\",\"lang\":\"kron\"}]}";
    CHECK(toolchest_read_samples(&TKC2) == ZXM_E_PROTOCOL && TKC2.samples == 0,
          "toolchest: a sample without an id is no sample");
    CANNED = NULL;
}
