/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_share.c — the share sheet (zui_share.h SH1..SH5) over app
 * intents (zxapp.h I1..I3), with a small test runtime standing in for a
 * language. */
#include <stdio.h>
#include <string.h>

#include "zui_share.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* ---- a test runtime: each entry's inline source is one integer; a call
 * answers that integer plus the sum of the first argument, and records what
 * ran ---------------------------------------------------------------------- */

typedef struct {
    int64_t base;
    char name[16];
} prog_t;
static prog_t PROGS[128];
static uint32_t nprogs, ncalls;
static char last_ran[16];
static int64_t last_arg;

static int t_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err, uint32_t cap)
{
    (void) ctx;
    if (nprogs >= 128u) return ZXAPP_E_FULL;
    prog_t *p = &PROGS[nprogs++];
    p->base = 0;
    uint32_t i = 0, k = 0;
    for (; i < n && src[i] >= '0' && src[i] <= '9'; i++) p->base = p->base * 10 + (src[i] - '0');
    for (; i < n && (src[i] == ' '); i++) {
    }
    for (; i < n && src[i] != '\n' && k + 1u < sizeof p->name; i++) p->name[k++] = src[i];
    p->name[k] = 0;
    if (!k) {
        snprintf(err, cap, "no name");
        return ZXAPP_E_RUNTIME;
    }
    *compiled = p;
    return ZXAPP_OK;
}

static int t_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                  zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    (void) err;
    (void) errcap;
    const prog_t *p = compiled;
    int64_t sum = 0;
    if (nargs)
        for (uint32_t i = 0; i < args[0].n; i++) sum += args[0].v[i];
    ncalls++;
    snprintf(last_ran, sizeof last_ran, "%s", p->name);
    last_arg = sum;
    *n_out = 1;
    if (cap < 1u) return ZXAPP_E_BUFFER;
    out[0] = (zxapp_num_t){p->base + sum, 1};
    return ZXAPP_OK;
}

static void t_unload(void *ctx, void *compiled)
{
    (void) ctx;
    (void) compiled;
}

static zxapp_host_t H;
static zxapp_t NOTES, MAIL, PAINT, KEEP, CHAT, SRC, STRICT;
static zui_share_t SH;
static zui_share_sheet_t S;

static int load(zxapp_t *a, const char *m)
{
    int rc = zxapp_parse(a, m, (uint32_t) strlen(m));
    if (rc != ZXAPP_OK) {
        printf("  parse: %s (line %u)\n", a->err, a->err_line);
        return rc;
    }
    return zxapp_load(a, &H);
}

static const char M_NOTES[] = "zxapp 1\nid notes\nname Notes\n"
                              "logic c append <<\n100 notes.append\n.\n"
                              "logic c append_any <<\n200 notes.any\n.\n"
                              "intent share text/plain append\n"
                              "intent share text/* append_any\n";
static const char M_MAIL[] = "zxapp 1\nid mail\nname Mail\n"
                             "logic c compose <<\n300 mail.compose\n.\n"
                             "logic c attach <<\n400 mail.attach\n.\n"
                             "intent share text/* compose\n"
                             "intent share image/* attach\n";
static const char M_PAINT[] = "zxapp 1\nid paint\nname Paint\n"
                              "logic c layer <<\n500 paint.layer\n.\n"
                              "intent share image/png layer\nintent edit image/* layer\n";
static const char M_KEEP[] = "zxapp 1\nid keep\nname Keep\n"
                             "logic c keep <<\n600 keep.keep\n.\n"
                             "intent share */* keep\n";
static const char M_CHAT[] = "zxapp 1\nid chat\nname Chat\n"
                             "logic c post <<\n700 chat.post\n.\n"
                             "intent share text/plain post\n";
static const char M_SRC[] = "zxapp 1\nid writer\nname Writer\n"
                            "logic c take <<\n800 writer.take\n.\n"
                            "intent share text/plain take\n";
/* promises one input named "doc": a call with anything else breaks it */
static const char M_STRICT[] = "zxapp 1\nid strict\nname Strict\nversion 1\n"
                               "provides take num doc\n"
                               "logic c take <<\n900 strict.take\n.\n"
                               "intent share text/plain take\n";
/* declares an intent whose entry it does not have */
static zxapp_t BROKEN;
static const char M_BROKEN[] = "zxapp 1\nid broken\nintent share text/plain missing\n";

static uint32_t index_of(const zui_share_sheet_t *s, const char *id)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (strcmp(s->t[i].app->id, id) == 0) return i;
    return 999u;
}

static void test_setup(void)
{
    zxapp_host_init(&H);
    zxapp_runtime_t rt = {ZXAPP_LANG_C, 0, t_load, t_call, t_unload};
    CHECK(zxapp_host_register(&H, &rt) == ZXAPP_OK, "test runtime");
    CHECK(load(&NOTES, M_NOTES) == 2, "notes loads");
    CHECK(load(&MAIL, M_MAIL) == 2, "mail loads");
    CHECK(load(&PAINT, M_PAINT) == 1, "paint loads");
    CHECK(load(&KEEP, M_KEEP) == 1, "keep loads");
    CHECK(load(&CHAT, M_CHAT) == 1, "chat loads");
    CHECK(load(&SRC, M_SRC) == 1, "writer loads");
    CHECK(load(&STRICT, M_STRICT) == 1, "strict loads");
    CHECK(load(&BROKEN, M_BROKEN) == 0, "broken parses, nothing to load");

    /* SH1 */
    zui_share_init(&SH);
    CHECK(SH.napps == 0 && SH.nrecent == 0, "empty");
    CHECK(zui_share_add(&SH, &KEEP) && zui_share_add(&SH, &MAIL) && zui_share_add(&SH, &NOTES) &&
              zui_share_add(&SH, &PAINT) && zui_share_add(&SH, &CHAT) && zui_share_add(&SH, &SRC),
          "six apps installed");
    CHECK(!zui_share_add(&SH, &NOTES), "an app is added once");
    CHECK(!zui_share_add(&SH, 0) && !zui_share_add(0, &NOTES), "NULL");
    CHECK(SH.napps == 6, "six");
}

static void test_sheet(void)
{
    /* SH2: most specific first */
    uint32_t n = zui_share_open(&SH, &S, "share", "text/plain", 0);
    CHECK(n == 5 && S.n == 5 && S.more == 0, "five take plain text (%u)", n);
    CHECK(strcmp(S.t[0].app->id, "notes") == 0 && strcmp(S.t[0].in->entry, "append") == 0 &&
              S.t[0].rank == 2 && !S.t[0].recent,
          "notes first, exact, its exact entry");
    CHECK(strcmp(S.t[1].app->id, "chat") == 0 && S.t[1].rank == 2, "chat: exact, after notes");
    CHECK(strcmp(S.t[2].app->id, "writer") == 0 && S.t[2].rank == 2, "writer: exact");
    CHECK(strcmp(S.t[3].app->id, "mail") == 0 && S.t[3].rank == 1, "mail: text/*");
    CHECK(strcmp(S.t[4].app->id, "keep") == 0 && S.t[4].rank == 0, "keep: anything, last");
    CHECK(S.sel == 0 && strcmp(S.verb, "share") == 0 && strcmp(S.type, "text/plain") == 0,
          "the first is highlighted; the sheet says what it is for");

    /* the source app is left out */
    n = zui_share_open(&SH, &S, "share", "text/plain", "writer");
    CHECK(n == 4 && index_of(&S, "writer") == 999u, "writer shares, so writer is not listed");
    n = zui_share_open(&SH, &S, "share", "text/plain", "");
    CHECK(n == 5, "an empty source leaves no one out");

    n = zui_share_open(&SH, &S, "share", "image/png", 0);
    CHECK(n == 3 && strcmp(S.t[0].app->id, "paint") == 0 && strcmp(S.t[1].app->id, "mail") == 0 &&
              strcmp(S.t[1].in->entry, "attach") == 0 && strcmp(S.t[2].app->id, "keep") == 0,
          "png: paint, then mail's attach, then keep");
    n = zui_share_open(&SH, &S, "share", "image/*", 0);
    CHECK(n == 2 && strcmp(S.t[0].app->id, "mail") == 0 && strcmp(S.t[1].app->id, "keep") == 0,
          "any image: paint takes only png");
    n = zui_share_open(&SH, &S, "edit", "image/jpeg", 0);
    CHECK(n == 1 && strcmp(S.t[0].app->id, "paint") == 0, "another verb");
    n = zui_share_open(&SH, &S, "share", "application/pdf", 0);
    CHECK(n == 1 && strcmp(S.t[0].app->id, "keep") == 0, "only keep takes anything");
    n = zui_share_open(&SH, &S, "print", "text/plain", 0);
    CHECK(n == 0 && S.sel == -1, "no one prints: an empty sheet, nothing highlighted");
    n = zui_share_open(&SH, &S, "share", "plain", 0);
    CHECK(n == 0, "not a type");
    CHECK(zui_share_open(0, &S, "share", "text/plain", 0) == 0 && S.n == 0, "NULL share");
    CHECK(zui_share_open(&SH, 0, "share", "text/plain", 0) == 0, "NULL sheet");
    CHECK(zui_share_open(&SH, &S, 0, "text/plain", 0) == 0 &&
              zui_share_open(&SH, &S, "share", 0, 0) == 0,
          "NULL verb or type");

    /* the highlight moves and wraps */
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    zui_share_move(&S, 1);
    CHECK(S.sel == 1, "next");
    zui_share_move(&S, -2);
    CHECK(S.sel == 4, "back past the first wraps to the last");
    zui_share_move(&S, 1);
    CHECK(S.sel == 0, "past the last wraps to the first");
    S.sel = -1;
    zui_share_move(&S, -1);
    CHECK(S.sel == 4, "from none, back is the last");
    S.sel = -1;
    zui_share_move(&S, 1);
    CHECK(S.sel == 0, "from none, forward is the first");
    zui_share_move(0, 1);
    zui_share_sheet_t empty = {0};
    zui_share_move(&empty, 1);
    CHECK(empty.sel == 0, "an empty sheet keeps no highlight");
}

static void test_pick(void)
{
    zxapp_num_t out[4];
    uint32_t got = 99;
    int64_t doc[] = {1, 2, 3};
    zxapp_arg_t arg = {"doc", doc, 3};

    zui_share_open(&SH, &S, "share", "text/plain", 0);
    uint32_t mail = index_of(&S, "mail");
    ncalls = 0;
    int rc = zui_share_pick(&SH, &S, mail, &H, &arg, 1, out, 4, &got);
    CHECK(rc == ZXAPP_OK && got == 1 && out[0].num == 306 && out[0].den == 1,
          "mail's compose ran with the content (%d, %lld)", rc, (long long) out[0].num);
    CHECK(ncalls == 1 && strcmp(last_ran, "mail.compose") == 0 && last_arg == 6,
          "exactly that entry ran, once");
    /* SH3: mail is now first */
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    CHECK(strcmp(S.t[0].app->id, "mail") == 0 && S.t[0].recent && !S.t[1].recent,
          "the recent target first, marked");
    CHECK(strcmp(S.t[1].app->id, "notes") == 0 && strcmp(S.t[4].app->id, "keep") == 0,
          "then the rest, most specific first");
    /* a recent app with another entry for another type is still recent */
    zui_share_open(&SH, &S, "share", "image/png", 0);
    CHECK(strcmp(S.t[0].app->id, "mail") == 0 && strcmp(S.t[0].in->entry, "attach") == 0 &&
              S.t[0].recent,
          "mail is recent for images too, through its image entry");
    rc = zui_share_pick(&SH, &S, 0, &H, &arg, 1, out, 4, &got);
    CHECK(rc == ZXAPP_OK && strcmp(last_ran, "mail.attach") == 0 && out[0].num == 406,
          "and attach runs");
    /* a recent app that does not take this type is not listed */
    zui_share_open(&SH, &S, "edit", "image/png", 0);
    CHECK(S.n == 1 && strcmp(S.t[0].app->id, "paint") == 0 && !S.t[0].recent,
          "recents are only of those that can");

    /* several recents in order */
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    CHECK(zui_share_pick(&SH, &S, index_of(&S, "keep"), &H, &arg, 1, out, 4, &got) == ZXAPP_OK,
          "keep");
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    CHECK(zui_share_pick(&SH, &S, index_of(&S, "chat"), &H, &arg, 1, out, 4, &got) == ZXAPP_OK,
          "chat");
    CHECK(SH.nrecent == 3 && strcmp(SH.recent[0], "chat") == 0 &&
              strcmp(SH.recent[1], "keep") == 0 && strcmp(SH.recent[2], "mail") == 0,
          "chat, keep, mail");
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    CHECK(strcmp(S.t[0].app->id, "chat") == 0 && strcmp(S.t[1].app->id, "keep") == 0 &&
              strcmp(S.t[2].app->id, "mail") == 0 && strcmp(S.t[3].app->id, "notes") == 0 &&
              strcmp(S.t[4].app->id, "writer") == 0,
          "recents in recency order, then the rest");
    CHECK(S.t[0].recent && S.t[1].recent && S.t[2].recent && !S.t[3].recent, "marked");
    /* picking a recent again moves it, once */
    CHECK(zui_share_pick(&SH, &S, 2, &H, &arg, 1, out, 4, &got) == ZXAPP_OK, "mail again");
    CHECK(SH.nrecent == 3 && strcmp(SH.recent[0], "mail") == 0 &&
              strcmp(SH.recent[1], "chat") == 0 && strcmp(SH.recent[2], "keep") == 0,
          "mail moves to the front, listed once");
    /* the source app is left out even when recent */
    zui_share_open(&SH, &S, "share", "text/plain", "mail");
    CHECK(S.n == 4 && strcmp(S.t[0].app->id, "chat") == 0, "a recent source is still left out");

    /* SH4: only targets on the sheet */
    ncalls = 0;
    CHECK(zui_share_pick(&SH, &S, S.n, &H, &arg, 1, out, 4, &got) == ZXAPP_E_ARG && got == 0,
          "past the sheet");
    CHECK(zui_share_pick(&SH, &S, 0, 0, &arg, 1, out, 4, &got) == ZXAPP_E_ARG, "no host");
    CHECK(zui_share_pick(0, &S, 0, &H, &arg, 1, out, 4, &got) == ZXAPP_E_ARG, "no share");
    CHECK(zui_share_pick(&SH, 0, 0, &H, &arg, 1, out, 4, &got) == ZXAPP_E_ARG, "no sheet");
    CHECK(ncalls == 0, "nothing ran");
    zui_share_sheet_t empty = {0};
    CHECK(zui_share_pick(&SH, &empty, 0, &H, &arg, 1, out, 4, &got) == ZXAPP_E_ARG,
          "an empty sheet has nothing to pick");

    /* the runtime's answer is passed back as is */
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    CHECK(zui_share_pick(&SH, &S, index_of(&S, "notes"), &H, &arg, 1, out, 0, &got) ==
                  ZXAPP_E_BUFFER &&
              got == 1,
          "no room for the answer: ZXAPP_E_BUFFER, and how many it needed");
    CHECK(strcmp(SH.recent[0], "mail") == 0, "a pick that did not run is not remembered");

    /* B3 through the sheet: a target's contract is checked */
    CHECK(zui_share_add(&SH, &STRICT), "strict installed");
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    uint32_t st = index_of(&S, "strict");
    CHECK(st < S.n && S.t[st].rank == 2, "strict listed");
    zxapp_arg_t wrong = {"text", doc, 3};
    ncalls = 0;
    CHECK(zui_share_pick(&SH, &S, st, &H, &wrong, 1, out, 4, &got) == ZXAPP_E_CONTRACT &&
              ncalls == 0,
          "an argument the entry does not promise is refused before anything runs");
    CHECK(zui_share_pick(&SH, &S, st, &H, &arg, 1, out, 4, &got) == ZXAPP_OK && out[0].num == 906 &&
              strcmp(SH.recent[0], "strict") == 0,
          "its promised input runs");

    /* an intent naming an entry that is not there */
    CHECK(zui_share_add(&SH, &BROKEN), "broken installed");
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    uint32_t br = index_of(&S, "broken");
    CHECK(br < S.n && zui_share_pick(&SH, &S, br, &H, &arg, 1, out, 4, &got) == ZXAPP_E_NAME,
          "ZXAPP_E_NAME, not a crash");
    CHECK(strcmp(SH.recent[0], "strict") == 0, "and not remembered");

    /* uninstalling: off the sheet, out of the recents, and a stale sheet
     * cannot run it */
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    uint32_t sti = index_of(&S, "strict");
    CHECK(zui_share_remove(&SH, &STRICT), "strict removed");
    CHECK(!zui_share_remove(&SH, &STRICT) && !zui_share_remove(&SH, 0), "only once");
    CHECK(strcmp(SH.recent[0], "mail") == 0 && SH.nrecent == 3, "its recent went with it");
    ncalls = 0;
    CHECK(zui_share_pick(&SH, &S, sti, &H, &arg, 1, out, 4, &got) == ZXAPP_E_ARG && ncalls == 0,
          "a sheet opened before cannot run an app since removed");
    zui_share_open(&SH, &S, "share", "text/plain", 0);
    CHECK(index_of(&S, "strict") == 999u, "and a new sheet does not list it");
    CHECK(zui_share_remove(&SH, &BROKEN), "broken removed");
}

static void test_limits(void)
{
    /* SH3: at most ZUI_SHARE_RECENT, oldest forgotten */
    static zxapp_t many[ZUI_SHARE_APPS];
    static char text[ZUI_SHARE_APPS][160];
    zui_share_t sh;
    zui_share_init(&sh);
    for (uint32_t i = 0; i < ZUI_SHARE_APPS; i++) {
        snprintf(text[i], sizeof text[i],
                 "zxapp 1\nid app%u\nlogic c go <<\n%u app%u\n.\nintent share */* go\n", i, i, i);
        CHECK(load(&many[i], text[i]) == 1, "app %u", i);
        CHECK(zui_share_add(&sh, &many[i]), "add %u", i);
    }
    CHECK(!zui_share_add(&sh, &NOTES), "full at ZUI_SHARE_APPS");
    uint32_t n = zui_share_open(&sh, &S, "share", "text/plain", 0);
    CHECK(n == ZUI_SHARE_TARGETS && S.more == ZUI_SHARE_APPS - ZUI_SHARE_TARGETS,
          "the sheet holds ZUI_SHARE_TARGETS and counts the rest (%u, %u)", n, S.more);
    zxapp_num_t out[1];
    uint32_t got;
    for (uint32_t i = 0; i < ZUI_SHARE_RECENT + 2u; i++) {
        zui_share_open(&sh, &S, "share", "text/plain", 0);
        uint32_t at = index_of(&S, many[i].id);
        CHECK(zui_share_pick(&sh, &S, at, &H, 0, 0, out, 1, &got) == ZXAPP_OK &&
                  out[0].num == (int64_t) i,
              "pick app%u", i);
    }
    CHECK(sh.nrecent == ZUI_SHARE_RECENT, "recents capped");
    CHECK(strcmp(sh.recent[0], "app9") == 0 &&
              strcmp(sh.recent[ZUI_SHARE_RECENT - 1u], "app2") == 0,
          "newest first, the two oldest forgotten (%s .. %s)", sh.recent[0],
          sh.recent[ZUI_SHARE_RECENT - 1u]);
    zui_share_open(&sh, &S, "share", "text/plain", 0);
    CHECK(strcmp(S.t[0].app->id, "app9") == 0 && S.t[ZUI_SHARE_RECENT - 1u].recent &&
              !S.t[ZUI_SHARE_RECENT].recent && strcmp(S.t[ZUI_SHARE_RECENT].app->id, "app0") == 0,
          "recents first, then the rest in installed order");
    /* deterministic: the same state gives the same sheet */
    zui_share_sheet_t again;
    zui_share_open(&sh, &again, "share", "text/plain", 0);
    CHECK(memcmp(&again.t, &S.t, sizeof S.t) == 0 && again.n == S.n, "SH5: the same sheet");
    for (uint32_t i = 0; i < ZUI_SHARE_APPS; i++) zxapp_unload(&many[i], &H);
}

int main(void)
{
    test_setup();
    test_sheet();
    test_pick();
    test_limits();
    printf("%s test_zui_share: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
