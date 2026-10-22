/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ferry's model against zxvd's real dispatcher: spaces.*, files.* and
 * files.cross on the scratch Hoard, features.*, sys.disk, and Keepsake's
 * backup.* to a carrier folder outside it; then the arrival from that
 * carrier, as the new machine would make it. */
#include "ferry_model.h"

static uint32_t ferry_steps;
static uint64_t ferry_done, ferry_whole;

static void ferry_on_progress(void *ctx, uint64_t done, uint64_t total)
{
    (void) ctx;
    ferry_steps++;
    ferry_done = done;
    ferry_whole = total;
}

/* files.stat in a space: 0 when the item is there */
static int ferry_stat(const char *space, const char *path)
{
    char p[256];
    snprintf(p, sizeof p, "{\"space\":\"%s\",\"path\":\"%s\"}", space, path);
    return raw_call("files.stat", p);
}

APPS_TEST(ferry)
{
    static ferry_t F, G;
    static char text[FERRY_PLAN_MAX], dest[256];
    const int64_t M = FERRY_BYTES_MAX;
    snprintf(dest, sizeof dest, "%s/zxv-ferry-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(dest) != NULL, "ferry: a carrier folder");

    /* FM1, FM4: nothing asked before there is something to ask */
    ferry_init(&F, &LINK);
    int before = sent;
    CHECK(ferry_pack(&F) == ZXM_E_STATE && ferry_arrive(&F) == ZXM_E_STATE &&
              ferry_load_plan(&F) == ZXM_E_STATE && ferry_set_dest(&F, "relative") == ZXM_E_ARG &&
              ferry_set_dest(&F, NULL) == ZXM_E_ARG && ferry_set_spare(&F, -1) == ZXM_E_ARG &&
              ferry_set_avail(&F, M + 1) == ZXM_E_ARG && F.fault == FERRY_F_TOO_BIG &&
              F.avail == -1 && F.spare == FERRY_SPARE && F.room == FERRY_NO_ROOM && !F.fits &&
              sent == before,
          "ferry: no carrier, no plan, bad sizes: nothing asked");

    /* FM2: an office this node owns, two files in it */
    CHECK(raw_call("spaces.create", "{\"kind\":\"office\",\"name\":\"Ferry Hold\"}") == ZXM_OK &&
              raw_call("files.write",
                       "{\"space\":\"ferry-hold\",\"path\":\"a.txt\",\"text\":\"hello\"}") == 0 &&
              raw_call("files.mkdir", "{\"space\":\"ferry-hold\",\"path\":\"sub\"}") == 0 &&
              raw_call("files.write",
                       "{\"space\":\"ferry-hold\",\"path\":\"sub/b.txt\",\"text\":\"goodbye\"}") ==
                  0 &&
              raw_call("files.write", "{\"path\":\"ferry-loose.txt\",\"text\":\"0123456789\"}") ==
                  0,
          "ferry: the office and its files: %s", LINK.err);
    int rc = ferry_survey(&F);
    int32_t hold = ferry_find(&F, "ferry-hold"), home = ferry_find(&F, "home"),
            crew = ferry_find(&F, "crew");
    CHECK(rc == ZXM_OK && hold >= 0 && home >= 0 && F.space[hold].bytes == 12 &&
              F.space[hold].files == 2 && F.space[hold].carry && F.space[home].files > 0 &&
              F.nsettings > 0 && F.setting[0].carry,
          "ferry: surveyed: %d %s", rc, LINK.err);
    if (hold < 0 || home < 0) return;
    {
        int64_t sum = 0, files = 0, shares = 0;
        for (uint32_t i = 0; i < F.nspaces; i++) {
            sum += F.space[i].bytes;
            files += F.space[i].files;
            shares += F.space[i].share_x100;
        }
        CHECK(F.plan_bytes == sum && F.plan_files == files && shares <= 10000 &&
                  shares > 10000 - (int64_t) F.nspaces &&
                  F.space[hold].share_x100 == 12 * 10000 / sum,
              "ferry: the totals (banker.total) and the shares (banker.share): %lld %lld",
              (long long) sum, (long long) shares);
    }
    CHECK(crew < 0 || (F.space[crew].bytes == 0 && F.space[crew].files == 0),
          "ferry: a joined space travels as its membership");

    /* FM3: home stays on the plan; another space comes off and back */
    int64_t all = F.plan_bytes;
    CHECK(ferry_choose_space(&F, (uint32_t) home, false) == ZXM_E_STATE && F.space[home].carry,
          "ferry: home stays");
    CHECK(ferry_choose_space(&F, (uint32_t) hold, false) == ZXM_OK && F.plan_bytes == all - 12 &&
              F.space[hold].share_x100 == 0 &&
              ferry_choose_space(&F, (uint32_t) hold, true) == ZXM_OK && F.plan_bytes == all &&
              ferry_choose_space(&F, 99, true) == ZXM_E_ARG,
          "ferry: off the plan and on again");

    /* FM4: the room and the fit (FR1, FR2) */
    CHECK(ferry_set_dest(&F, dest) == ZXM_OK && ferry_pack(&F) == ZXM_E_STATE &&
              F.fault == FERRY_F_NO_ROOM,
          "ferry: not measured: not packed");
    CHECK(ferry_set_avail(&F, all + FERRY_SPARE - 1) == ZXM_OK && F.room == -1 && !F.fits &&
              ferry_pack(&F) == ZXM_E_STATE && F.fault == FERRY_F_NO_ROOM,
          "ferry: one byte short: not packed");
    CHECK(ferry_set_avail(&F, all + FERRY_SPARE) == ZXM_OK && F.room == 0 && F.fits &&
              ferry_set_spare(&F, 0) == ZXM_OK && F.room == FERRY_SPARE && F.fits,
          "ferry: exactly full, then no spare: %lld", (long long) F.room);
    CHECK(ferry_measure(&F) == ZXM_OK && F.avail >= 0 && F.avail <= M &&
              F.room == F.avail - F.plan_bytes,
          "ferry: sys.disk's avail: %lld %s", (long long) F.avail, LINK.err);
    CHECK(ferry_set_avail(&F, M) == ZXM_OK && F.fits, "ferry: room enough");

    /* FM8: a loose file into the office: held, then the owner confirms */
    uint32_t ticket = 7;
    before = sent;
    CHECK(ferry_gather(&F, "home", "ferry-loose.txt", "nowhere", "x.txt", &ticket) == ZXM_E_STATE &&
              ticket == 0 &&
              ferry_gather(&F, "home", "", "ferry-hold", "x.txt", &ticket) == ZXM_E_ARG &&
              (crew < 0 || ferry_gather(&F, "home", "ferry-loose.txt", "crew", "x.txt", &ticket) ==
                               ZXM_E_STATE) &&
              ferry_gather_confirm(&F, 0, "ferry-hold") == ZXM_E_ARG && sent == before,
          "ferry: not into a space off the plan or not owned, nor with no path");
    CHECK(ferry_gather(&F, "home", "ferry-loose.txt", "ferry-hold", "loose.txt", &ticket) ==
                  ZXM_OK &&
              ticket > 0 && F.space[hold].bytes == 12,
          "ferry: into a more open space: held (%u) %s", ticket, LINK.err);
    CHECK(ferry_gather_confirm(&F, ticket, "ferry-hold") == ZXM_OK && F.space[hold].bytes == 22 &&
              F.space[hold].files == 3 && F.plan_bytes == all + 10,
          "ferry: confirmed: %lld %s", (long long) F.space[hold].bytes, LINK.err);
    CHECK(ferry_gather_confirm(&F, ticket, "ferry-hold") == ZXM_E_REMOTE, "ferry: only once");

    /* the plan as text, and back (FM6) */
    int32_t n = ferry_plan_text(&F, text, sizeof text);
    ferry_init(&G, &LINK);
    CHECK(n > 0 && ferry_plan_parse(&G, text, (uint32_t) n) == ZXM_OK && G.nspaces == F.nspaces &&
              G.nsettings == F.nsettings && G.plan_bytes == F.plan_bytes &&
              G.plan_files == F.plan_files && !strcmp(G.space[hold].id, "ferry-hold") &&
              G.space[hold].bytes == 22 && !strcmp(G.space[hold].owner, "me") &&
              !strcmp(G.space[hold].name, "Ferry Hold") &&
              !strcmp(G.setting[0].name, F.setting[0].name) &&
              G.setting[0].regulated == F.setting[0].regulated,
          "ferry: the plan's round trip: %d", n);
    CHECK(ferry_plan_text(&F, text, 20) == -1, "ferry: a plan that does not fit");
    {
        static const char *const BAD[] = {
            "ferry 2\nend\n",
            "ferry 1\n",
            "ferry 1\nend\nend\n",
            "ferry 1\nspace\tx\toffice\towner\t1\t12\t1\t0\t0\tme\nend\n",
            "ferry 1\nspace\tx\toffice\towner\t2\t12\t1\t0\t0\tme\tX\nend\n",
            "ferry 1\nspace\tx\toffice\towner\t1\t-12\t1\t0\t0\tme\tX\nend\n",
            "ferry 1\nspace\tx\toffice\towner\t1\t1000000000000000\t1\t0\t0\tme\tX\nend\n",
            "ferry 1\nspace\tx\toffice\towner\t1\t12\t1\t4294967296\t0\tme\tX\nend\n",
            "ferry 1\nspace\tx\toffice\towner\t1\t12\t1\t0\t0\tme\tX\textra\nend\n",
            "ferry 1\nsetting\tfab\t1\t0\t1\textra\nend\n",
            "ferry 1\nwhat\nend\n",
            "ferry 1\nend",
        };
        bool all_bad = true;
        for (uint32_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++)
            all_bad &= ferry_plan_parse(&G, BAD[i], (uint32_t) strlen(BAD[i])) == ZXM_E_ARG &&
                       G.fault == FERRY_F_PLAN;
        CHECK(all_bad && G.nspaces == F.nspaces && G.space[hold].bytes == 22,
              "ferry: bad plans refused, the plan held stays");
    }

    /* FM5: packing, home last, every step heard */
    zxm_link_progress(&LINK, ferry_on_progress, NULL);
    rc = ferry_pack(&F);
    zxm_link_progress(&LINK, NULL, NULL);
    {
        bool packed = true;
        for (uint32_t i = 0; i < F.nspaces; i++) packed &= F.space[i].packed;
        CHECK(rc == ZXM_OK && packed && F.packed_bytes == F.plan_bytes &&
                  F.progress_x100 == 10000 && F.space[hold].snapshot >= 1 &&
                  F.space[hold].snapfiles == 3 && F.space[home].snapshot >= 1 &&
                  ferry_steps == F.nspaces && ferry_done == (uint64_t) F.plan_bytes &&
                  ferry_whole == (uint64_t) F.plan_bytes &&
                  ferry_stat("home", FERRY_PLAN_FILE) == 0,
              "ferry: packed: %d %u %s", rc, ferry_steps, LINK.err);
    }

    /* FM6: on the new machine, the same carrier */
    ferry_init(&G, &LINK);
    CHECK(ferry_set_dest(&G, dest) == ZXM_OK && ferry_load_plan(&G) == ZXM_OK &&
              G.nspaces == F.nspaces && G.plan_bytes == F.plan_bytes && G.space[hold].bytes == 22 &&
              G.progress_x100 == 0 && G.space[hold].snapshot == F.space[hold].snapshot &&
              G.space[hold].snapfiles == 3 && G.space[home].snapshot == 0,
          "ferry: the plan read back from the carrier: %s", LINK.err);
    CHECK(ferry_arrive(&G) == ZXM_OK && G.space[hold].arrived && G.restored >= 3 &&
              ferry_stat("ferry-hold", FERRY_ARRIVED "/a.txt") == 0 &&
              ferry_stat("ferry-hold", FERRY_ARRIVED "/sub/b.txt") == 0 &&
              ferry_stat("ferry-hold", FERRY_ARRIVED "/loose.txt") == 0,
          "ferry: arrived, every file restored: %u %s", G.restored, LINK.err);
    CHECK(ferry_arrive(&G) == ZXM_E_REMOTE, "ferry: never over what arrived before");
    {
        /* a space joined elsewhere is joined here; one made under another id is refused */
        static const char JOIN[] =
            "ferry 1\nto\t\nspace\tfar-shore\tteam\tmember\t1\t0\t0\t0\t0\tbob\tFar "
            "Shore\nend\n";
        static const char MOVED[] =
            "ferry 1\nto\t\nspace\telsewhere\toffice\towner\t1\t0\t0\t0\t0\tme\t"
            "Ferry Moved\nend\n";
        CHECK(ferry_plan_parse(&G, JOIN, sizeof JOIN - 1u) == ZXM_OK &&
                  ferry_arrive(&G) == ZXM_OK && G.restored == 0 && G.space[0].arrived &&
                  raw_call("spaces.get", "{\"space\":\"far-shore\"}") == ZXM_OK,
              "ferry: joined again: %s", LINK.err);
        CHECK(ferry_plan_parse(&G, MOVED, sizeof MOVED - 1u) == ZXM_OK &&
                  ferry_arrive(&G) == ZXM_E_STATE && G.fault == FERRY_F_SPACE &&
                  !G.space[0].arrived,
              "ferry: made under another id: refused");
    }

    /* FM7: settings, a regulated one only with the owner's word (FR3) */
    {
        uint32_t carried = 0, regulated = 0;
        for (uint32_t i = 0; i < F.nsettings; i++) regulated += F.setting[i].regulated;
        carried = F.nsettings;
        CHECK(ferry_apply_settings(&F, false) == ZXM_OK && F.held == regulated &&
                  F.applied == carried - regulated,
              "ferry: settings without approval: %u set, %u held %s", F.applied, F.held, LINK.err);
        CHECK(ferry_choose_setting(&F, 0, false) == ZXM_OK &&
                  ferry_choose_setting(&F, 99, false) == ZXM_E_ARG &&
                  ferry_apply_settings(&F, true) == ZXM_OK && F.held == 0 &&
                  F.applied == carried - 1,
              "ferry: approved, one left off: %u set %s", F.applied, LINK.err);
    }

    /* FM2: the peers (networking is off here), then a made-up peer list */
    CHECK(ferry_peers(&F) == ZXM_E_REMOTE && F.npeers == 0 && ferry_set_target(&F, 0) == ZXM_E_ARG,
          "ferry: no peers while networking is off");
    {
        zxm_link_t *real = F.link;
        F.link = &FAKE;
        CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"peers\":[{\"id\":"
                 "\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\",\"addr\":"
                 "\"10.0.0.2:9000\"}],\"count\":1}}";
        CHECK(ferry_peers(&F) == ZXM_OK && F.npeers == 1 && ferry_set_target(&F, 0) == ZXM_OK &&
                  !strcmp(F.peer[0].addr, "10.0.0.2:9000"),
              "ferry: a peer named as the target");
        CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"peers\":[{\"id\":\"abc\",\"addr\":"
                 "\"\"}]}}";
        CHECK(ferry_peers(&F) == ZXM_E_PROTOCOL && F.npeers == 1, "ferry: a short NodeID refused");
        F.link = real;
        n = ferry_plan_text(&F, text, sizeof text);
        CHECK(n > 0 && strstr(text, "to\t00112233445566778899aabbccddeeff") != NULL,
              "ferry: the target rides in the plan");
    }

    /* FM5: a copy on the carrier damaged: the check stops the packing. Its content is
     * new, so no later copy of the same bytes repairs it first. */
    CHECK(raw_call("files.write",
                   "{\"space\":\"ferry-hold\",\"path\":\"late.txt\",\"text\":\"ferry-late\"}") ==
                  0 &&
              ferry_pack(&F) == ZXM_OK,
          "ferry: a late file, packed: %s", LINK.err);
    {
        char hex[65], obj[600];
        uint8_t h[32];
        sha256((const uint8_t *) "ferry-late", 10, h);
        for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
        snprintf(obj, sizeof obj, "%s/ZXV Keepsake/objects/%.2s/%s", dest, hex, hex);
        FILE *of = fopen(obj, "wb");
        CHECK(of && fwrite("FERRY-LATE", 1, 10, of) == 10 && fclose(of) == 0,
              "ferry: damage a copy");
        CHECK(ferry_pack(&F) == ZXM_E_STATE && F.fault == FERRY_F_CHECK && !F.space[home].packed,
              "ferry: a copy that does not read back stops the packing: %s", LINK.err);
    }
    CHECK(rm_tree(dest) == 0, "ferry: the carrier removed");
}
