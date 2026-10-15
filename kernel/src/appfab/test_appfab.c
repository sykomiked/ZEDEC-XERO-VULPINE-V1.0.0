/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_appfab.c — the program fabricator's gate (appfab.h) under attack:
 * every way a fabricated app can be incomplete, tampered, unrunnable or
 * wrong must be refused or held, never accepted. Two stub languages (the
 * kron and eve slots) give answers that agree, disagree or refuse on
 * demand; the real decision-engine blocks (apps/blocks/pce_*.zxblock, the
 * `c` runtime) give a real app; and one app goes the whole way: signed
 * recipe, fab hydrate and assemble, then the gate. Run by `make
 * test-appfab`, which passes kernel/src. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appfab.h"
#include "../apps/zxapp_pce.h"
#include "../fab/fab.h"

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

static appfab_t W;
static zxapp_host_t H;

/* ---- two stub languages ---------------------------------------------------- */

/* add: sum of each argument's first value; add1: that plus one; flaky:
 * loads, refuses every call. Any other source is refused at load. */
static int stub_load(void *ctx, const char *src, uint32_t n, void **compiled, char *err,
                     uint32_t cap)
{
    (void) ctx;
    while (n && (src[n - 1] == '\n' || src[n - 1] == ' ')) n--;
    while (n && (*src == '\n' || *src == ' ')) src++, n--;
    if (n == 3 && !memcmp(src, "add", 3))
        *compiled = (void *) 1;
    else if (n == 4 && !memcmp(src, "add1", 4))
        *compiled = (void *) 2;
    else if (n == 5 && !memcmp(src, "flaky", 5))
        *compiled = (void *) 3;
    else {
        snprintf(err, cap, "not a stub program");
        return ZXAPP_E_RUNTIME;
    }
    return ZXAPP_OK;
}

static int stub_call(void *ctx, void *compiled, const zxapp_arg_t *args, uint32_t nargs,
                     zxapp_num_t *out, uint32_t cap, uint32_t *n_out, char *err, uint32_t errcap)
{
    (void) ctx;
    uintptr_t k = (uintptr_t) compiled;
    if (k == 3) {
        snprintf(err, errcap, "refused");
        return ZXAPP_E_RUNTIME;
    }
    int64_t s = k == 2 ? 1 : 0;
    for (uint32_t i = 0; i < nargs; i++) s += args[i].n ? args[i].v[0] : 0;
    *n_out = 1;
    if (cap < 1) return ZXAPP_E_BUFFER;
    out[0].num = s;
    out[0].den = 1;
    return ZXAPP_OK;
}

static void stub_unload(void *ctx, void *compiled)
{
    (void) ctx;
    (void) compiled;
}

static void host(void)
{
    zxapp_host_init(&H);
    zxapp_runtime_t a = {ZXAPP_LANG_KRON, 0, stub_load, stub_call, stub_unload};
    zxapp_runtime_t b = {ZXAPP_LANG_EVE, 0, stub_load, stub_call, stub_unload};
    CHECK(zxapp_host_register(&H, &a) == ZXAPP_OK, "kron stub");
    CHECK(zxapp_host_register(&H, &b) == ZXAPP_OK, "eve stub");
    CHECK(zxapp_pce_register(&H) == ZXAPP_OK, "the c runtime (decision engines)");
}

/* ---- parts ------------------------------------------------------------------ */

#define TXT 4096
static char T[APPFAB_PARTS + 4][TXT];
static appfab_part_t P[APPFAB_PARTS + 4];
static char PIN[APPFAB_PARTS + 4][ZXAPP_CID_MAX];
static char CIDS[APPFAB_PARTS + 4][IPFSN_CID_STR_MAX];

/* part i holds text (a copy, so the bytes outlive W) */
static void part(uint32_t i, const char *text)
{
    snprintf(T[i], TXT, "%s", text);
    P[i].data = (const uint8_t *) T[i];
    P[i].len = (uint32_t) strlen(T[i]);
    P[i].layer = 0;
    P[i].layered = false;
    CHECK(ipfsn_cid_sha256(IPFSN_MC_RAW, P[i].data, P[i].len, &P[i].cid) == IPFSN_OK, "cid");
    zxapp_cid(T[i], P[i].len, PIN[i]);
    ipfsn_cid_to_string(&P[i].cid, CIDS[i], sizeof CIDS[i]);
}

static const char *ADDER = "zxblock 1\nid adder\nversion 1.0.0\n"
                           "provides sum num a b\n"
                           "logic kron sum <<\nadd\n.\n"
                           "logic eve sum <<\nadd\n.\n";

static char APP[TXT];
static lpres_state_t fit_app(const char *fmt, const char *pin, uint32_t nparts)
{
    snprintf(APP, sizeof APP, fmt, pin);
    return appfab_fit(&W, (const uint8_t *) APP, (uint32_t) strlen(APP), P, nparts, &H);
}

static zxapp_arg_t A2[2];
static int64_t VA[1] = {2}, VB[1] = {3};
static zxapp_num_t FIVE = {5, 1}, SIX = {6, 1};
static appfab_check_t chk(const char *entry, const zxapp_num_t *want)
{
    A2[0] = (zxapp_arg_t){"a", VA, 1};
    A2[1] = (zxapp_arg_t){"b", VB, 1};
    appfab_check_t c = {entry, A2, 2, want, 1, false};
    return c;
}

static const char *CALC = "zxapp 1\nid calc\nname Calc\nuse adder 1.0.0 %s\n";

/* ---- fit -------------------------------------------------------------------- */

static void test_fit(void)
{
    part(0, ADDER);
    CHECK(strncmp(PIN[0], "sha256:", 7) == 0 && strlen(PIN[0]) == 71, "zxapp pin: %s", PIN[0]);
    CHECK(strlen(CIDS[0]) == 59 && !strncmp(CIDS[0], "bafkrei", 7), "raw CID: %s", CIDS[0]);
    /* the pin and the CID carry one digest (F1) */
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", P[0].cid.digest[i]);
    CHECK(!strcmp(PIN[0] + 7, hex), "same digest");

    CHECK(fit_app(CALC, PIN[0], 1) == LPRES_STATE_TRUE, "a whole app fits: %s", W.why);
    CHECK(W.app.n == 2 && W.app.logic[0].loaded && W.app.logic[1].loaded, "both languages load");
    appfab_unload(&W, &H);

    /* F3 */
    CHECK(fit_app("zxapp 1\nid calc\nuse adder 1.0.0%s\n", "", 1) == LPRES_STATE_FALSE &&
              strstr(W.why, "not pinned"),
          "unpinned use: %s", W.why);
    CHECK(fit_app(CALC, PIN[0], 0) == LPRES_STATE_FALSE && strstr(W.why, "does not carry"),
          "pinned part missing from the recipe: %s", W.why);
    char other[ZXAPP_CID_MAX];
    snprintf(other, sizeof other, "%s", PIN[0]);
    other[10] = other[10] == 'a' ? 'b' : 'a';
    CHECK(fit_app(CALC, other, 1) == LPRES_STATE_FALSE && strstr(W.why, "does not carry"),
          "a pin one digit off: %s", W.why);
    /* F1: tampered bytes under the old CID, a CID that is not SHA-256 */
    T[0][P[0].len - 3] = 'x';
    CHECK(fit_app(CALC, PIN[0], 1) == LPRES_STATE_FALSE && strstr(W.why, "match its CID"),
          "tampered part: %s", W.why);
    part(0, ADDER);
    ipfsn_cid_t keep = P[0].cid;
    CHECK(ipfsn_cid_identity(IPFSN_MC_RAW, P[0].data, 20, &P[0].cid) == IPFSN_OK, "identity cid");
    P[0].len = 20;
    CHECK(fit_app(CALC, PIN[0], 1) == LPRES_STATE_FALSE && strstr(W.why, "match its CID"),
          "an identity CID is not a part: %s", W.why);
    P[0].cid = keep;
    P[0].len = (uint32_t) strlen(T[0]);
    P[0].cid.digest_len = 20;
    CHECK(fit_app(CALC, PIN[0], 1) == LPRES_STATE_FALSE, "a short digest");
    part(0, ADDER);
    P[0].data = NULL;
    CHECK(fit_app(CALC, PIN[0], 1) == LPRES_STATE_FALSE, "no bytes");
    part(0, ADDER);

    /* F2 */
    CHECK(appfab_fit(&W, P[0].data, P[0].len, P, 1, &H) == LPRES_STATE_FALSE &&
              strstr(W.why, "block, not an app"),
          "a block is not an app: %s", W.why);
    CHECK(fit_app("zxapp 1\nid calc\nbogus line %s\n", "", 1) == LPRES_STATE_FALSE &&
              strstr(W.why, "does not parse"),
          "a broken manifest: %s", W.why);
    CHECK(appfab_fit(&W, NULL, 0, P, 1, &H) == LPRES_STATE_FALSE, "no manifest");
    CHECK(appfab_fit(&W, (const uint8_t *) "x", 1, P, 1, NULL) == LPRES_STATE_FALSE, "no host");
    CHECK(appfab_fit(&W, (const uint8_t *) "x", 1, NULL, 1, &H) == LPRES_STATE_FALSE, "no parts");
    CHECK(appfab_fit(NULL, (const uint8_t *) "x", 1, P, 1, &H) == LPRES_STATE_FALSE, "no work");
    for (uint32_t i = 1; i <= APPFAB_PARTS; i++) P[i] = P[0];
    CHECK(fit_app(CALC, PIN[0], APPFAB_PARTS + 1) == LPRES_STATE_FALSE &&
              strstr(W.why, "more parts"),
          "too many parts: %s", W.why);
    CHECK(fit_app(CALC, PIN[0], APPFAB_PARTS) == LPRES_STATE_TRUE, "as many as an app can use: %s",
          W.why);
    appfab_unload(&W, &H);

    /* a part that is not a manifest, a block that breaks its contract */
    part(1, "\x01\x02 not a manifest");
    CHECK(fit_app(CALC, PIN[1], 2) == LPRES_STATE_FALSE && strstr(W.why, "does not parse"),
          "a used part that is noise: %s", W.why);
    part(1, "zxblock 1\nid adder\nversion 1.0.0\nlogic kron sum <<\nadd\n.\n");
    CHECK(fit_app(CALC, PIN[1], 2) == LPRES_STATE_FALSE && strstr(W.why, "refused"),
          "a block with logic it does not provide: %s", W.why);
    /* two parts, one id and version: never picked between */
    part(1, "zxblock 1\nid adder\nversion 1.0.0\nprovides sum num a b\n"
            "logic kron sum <<\nadd1\n.\n");
    char two[TXT];
    snprintf(two, sizeof two, "zxapp 1\nid calc\nuse adder 1.0.0 %s\nuse adder 1.0.0 %s\n", PIN[0],
             PIN[1]);
    CHECK(appfab_fit(&W, (const uint8_t *) two, (uint32_t) strlen(two), P, 2, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "used twice"),
          "one id used twice: %s", W.why);
    /* one id and version in two parts, reached through two apps: the registry refuses */
    char via[2][TXT];
    for (uint32_t k = 0; k < 2u; k++) {
        snprintf(via[k], sizeof via[k], "zxapp 1\nid via%u\nversion 1.0.0\nuse adder 1.0.0 %.71s\n",
                 (unsigned) k, PIN[k]);
        part(2u + k, via[k]);
    }
    snprintf(two, sizeof two, "zxapp 1\nid calc\nuse via0 1.0.0 %s\nuse via1 1.0.0 %s\n", PIN[2],
             PIN[3]);
    CHECK(appfab_fit(&W, (const uint8_t *) two, (uint32_t) strlen(two), P, 4, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "refused"),
          "one id and version in two parts: %s", W.why);
    /* an unused part is a decoy: it does not change the answer */
    CHECK(fit_app(CALC, PIN[0], 2) == LPRES_STATE_TRUE, "a decoy part: %s", W.why);
    appfab_unload(&W, &H);
    /* the same part named twice is used once */
    snprintf(two, sizeof two, "zxapp 1\nid calc\nuse adder 1.0.0 %s\nuse adder =1.0.0 %s\n", PIN[0],
             PIN[0]);
    lpres_state_t s = appfab_fit(&W, (const uint8_t *) two, (uint32_t) strlen(two), P, 1, &H);
    CHECK(s != LPRES_STATE_TRUE || W.state[0] == 2, "one part, two uses");
    appfab_unload(&W, &H);
}

/* F4, F5 */
static void test_sources(void)
{
    part(0, ADDER);
    part(1, "add\n");
    char m[TXT];
    snprintf(
        m, sizeof m,
        "zxapp 1\nid calc\nprovides own num a b\nlogic kron own cid:%s\nlogic eve own <<\nadd\n"
        ".\n",
        CIDS[1]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) == LPRES_STATE_TRUE,
          "a source read from a part: %s", W.why);
    CHECK(H.read_file == NULL, "the host's reader is put back");
    appfab_unload(&W, &H);
    appfab_part_t first[2] = {P[1], P[0]}; /* the source as part 0 */
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), first, 2, &H) ==
              LPRES_STATE_TRUE,
          "a source in the first part: %s", W.why);
    appfab_check_t c = chk("own", &FIVE);
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_TRUE, "and it runs: %s", W.why);
    appfab_unload(&W, &H);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 1, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "not read from a part"),
          "a cid: source the recipe lacks: %s", W.why);
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own apps/x.kron\n");
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "not read from a part"),
          "a disk path: %s", W.why);
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own cid:nope\n");
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "not read from a part"),
          "a malformed cid: path");
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own cad:%s\n",
             CIDS[1]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "not read from a part"),
          "cad: is not cid:");
    static const char *bent[] = {"xid:", "cxd:", "cix:", "cid;"};
    for (uint32_t b = 0; b < 4u; b++) {
        snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own %s%s\n",
                 bent[b], CIDS[1]);
        CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) ==
                      LPRES_STATE_FALSE &&
                  strstr(W.why, "not read from a part"),
              "%s is not cid: (%s)", bent[b], W.why);
    }
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own cID:%s\n",
             CIDS[1]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "not read from a part"),
          "cID: is not cid:");
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own cia:%s\n",
             CIDS[1]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "not read from a part"),
          "cia: is not cid:");
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own cidx%s\n",
             CIDS[1]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "not read from a part"),
          "cidx is not cid:");
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own did:%s\n",
             CIDS[1]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 2, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "not read from a part"),
          "did: is not cid:");
    /* a part larger than the host's source buffer is refused, not cut */
    static char big[sizeof H.file_buf + 2];
    memset(big, ' ', sizeof big - 1);
    memcpy(big, "add", 3);
    big[sizeof big - 1] = 0;
    part(2, "");
    P[2].data = (const uint8_t *) big;
    P[2].len = (uint32_t) sizeof H.file_buf + 1u;
    CHECK(ipfsn_cid_sha256(IPFSN_MC_RAW, P[2].data, P[2].len, &P[2].cid) == IPFSN_OK, "big");
    ipfsn_cid_to_string(&P[2].cid, CIDS[2], sizeof CIDS[2]);
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own cid:%s\n",
             CIDS[2]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 3, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "cannot read"),
          "a source past the host buffer: %s", W.why);
    P[2].len = (uint32_t) sizeof H.file_buf;
    CHECK(ipfsn_cid_sha256(IPFSN_MC_RAW, P[2].data, P[2].len, &P[2].cid) == IPFSN_OK, "big");
    ipfsn_cid_to_string(&P[2].cid, CIDS[2], sizeof CIDS[2]);
    snprintf(
        m, sizeof m,
        "zxapp 1\nid calc\nprovides own num a b\nlogic kron own cid:%s\nlogic eve own <<\nadd\n"
        ".\n",
        CIDS[2]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 3, &H) == LPRES_STATE_TRUE,
          "exactly the host buffer: %s", W.why);
    appfab_unload(&W, &H);

    /* F5: a language with no runtime here, a source its runtime refuses */
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic sutra own <<\nadd\n.\n");
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 0, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "no runtime"),
          "no runtime for sutra here: %s", W.why);
    snprintf(m, sizeof m,
             "zxapp 1\nid calc\nprovides own num a b\nlogic kron own <<\nadd\n.\n"
             "logic sutra own <<\nadd\n.\n");
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 0, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "no runtime"),
          "one language of two has no runtime: %s", W.why);
    snprintf(m, sizeof m, "zxapp 1\nid calc\nprovides own num a b\nlogic kron own <<\nmul\n.\n");
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 0, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "does not load"),
          "a source the runtime refuses: %s", W.why);
    /* a promise with no logic: the parser or the gate refuses it */
    snprintf(m, sizeof m,
             "zxapp 1\nid calc\nprovides own num a b\nprovides gone num a\n"
             "logic kron own <<\nadd\n.\n");
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 0, &H) == LPRES_STATE_FALSE,
          "provides with no logic: %s", W.why);
    /* a host with nothing registered fits nothing */
    zxapp_host_t bare;
    zxapp_host_init(&bare);
    part(0, ADDER);
    CHECK(fit_app(CALC, PIN[0], 1) == LPRES_STATE_TRUE, "baseline: %s", W.why);
    appfab_unload(&W, &H);
    snprintf(APP, sizeof APP, CALC, PIN[0]);
    CHECK(appfab_fit(&W, (const uint8_t *) APP, (uint32_t) strlen(APP), P, 1, &bare) ==
              LPRES_STATE_FALSE,
          "a bare host");
}

/* apps used as blocks (zxapp.h C1), nested to the limit */
static void test_nesting(void)
{
    part(0, ADDER);
    char prev[ZXAPP_CID_MAX], prov[ZXAPP_ENTRY_MAX] = "adder.sum", id[16] = "adder";
    snprintf(prev, sizeof prev, "%s", PIN[0]);
    char m[TXT];
    for (uint32_t k = 1; k <= APPFAB_DEPTH; k++) {
        snprintf(m, sizeof m,
                 "zxapp 1\nid m%u\nversion 1.0.0\nuse %s 1.0.0 %s\nprovides %s num a b\n",
                 (unsigned) k, id, prev, prov);
        part(k, m);
        snprintf(prev, sizeof prev, "%s", PIN[k]);
        snprintf(id, sizeof id, "m%u", (unsigned) k);
        char np[ZXAPP_ENTRY_MAX + 16];
        snprintf(np, sizeof np, "m%u.%s", (unsigned) k, prov);
        CHECK(strlen(np) < sizeof prov, "nested entry name fits: %s", np);
        if (strlen(np) >= sizeof prov) return;
        memcpy(prov, np, strlen(np) + 1);
    }
    /* the top app uses m(depth-1): the adder sits at the deepest allowed level */
    snprintf(m, sizeof m, "zxapp 1\nid top\nuse m%u 1.0.0 %s\n", (unsigned) (APPFAB_DEPTH - 1),
             PIN[APPFAB_DEPTH - 1]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, APPFAB_DEPTH + 1, &H) ==
              LPRES_STATE_TRUE,
          "apps nested %u deep: %s", (unsigned) APPFAB_DEPTH, W.why);
    char deep[ZXAPP_ENTRY_MAX];
    snprintf(deep, sizeof deep, "m%u.", (unsigned) (APPFAB_DEPTH - 1));
    bool found = false;
    for (uint32_t i = 0; i < W.app.n; i++)
        found |= !strncmp(W.app.logic[i].entry, deep, strlen(deep));
    CHECK(found, "the nested entry is composed");
    appfab_check_t c;
    c = chk(W.app.n ? W.app.logic[0].entry : "x", &FIVE);
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_TRUE, "and answers through every level: %s",
          W.why);
    appfab_unload(&W, &H);
    snprintf(m, sizeof m, "zxapp 1\nid top\nuse m%u 1.0.0 %s\n", (unsigned) APPFAB_DEPTH,
             PIN[APPFAB_DEPTH]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, APPFAB_DEPTH + 1, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "too deep"),
          "one level deeper: %s", W.why);
    /* a diamond: two parts use one block, which is resolved and registered once */
    char side[2][TXT];
    for (uint32_t k = 0; k < 2u; k++) {
        snprintf(side[k], sizeof side[k],
                 "zxapp 1\nid side%u\nversion 1.0.0\nuse adder 1.0.0 %.71s\n"
                 "provides adder.sum num a b\n",
                 (unsigned) k, PIN[0]);
        part(1u + k, side[k]);
    }
    snprintf(m, sizeof m, "zxapp 1\nid top\nuse side0 1.0.0 %.71s\nuse side1 1.0.0 %.71s\n", PIN[1],
             PIN[2]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 3, &H) == LPRES_STATE_TRUE,
          "a diamond: %s", W.why);
    CHECK(W.reg.n == 3u, "three parts registered, the shared one once (%u)", (unsigned) W.reg.n);
    appfab_unload(&W, &H);
    /* a middle level missing from the recipe */
    snprintf(m, sizeof m, "zxapp 1\nid top\nuse m3 1.0.0 %s\n", PIN[3]);
    appfab_part_t gap[4] = {P[0], P[1], P[3]};
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), gap, 3, &H) ==
                  LPRES_STATE_FALSE &&
              strstr(W.why, "does not carry"),
          "a missing middle part: %s", W.why);
}

/* C1-C3 */
static void test_function(void)
{
    part(0, ADDER);
    appfab_check_t c = chk("adder.sum", &FIVE);
    W.fit = LPRES_STATE_FALSE;
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_NEITHER, "no function before fit");
    CHECK(fit_app(CALC, PIN[0], 1) == LPRES_STATE_TRUE, "fit: %s", W.why);
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_TRUE, "two languages agree: %s", W.why);
    CHECK(appfab_verdict(W.fit, LPRES_STATE_TRUE) == LPRES_STATE_TRUE, "accepted");
    CHECK(appfab_function(&W, &H, NULL, 0) == LPRES_STATE_NEITHER && strstr(W.why, "no checks"),
          "no checks: unwitnessed");
    CHECK(appfab_function(&W, &H, NULL, 1) == LPRES_STATE_NEITHER, "no check array");
    CHECK(appfab_function(&W, NULL, &c, 1) == LPRES_STATE_NEITHER, "no host");
    CHECK(appfab_function(NULL, &H, &c, 1) == LPRES_STATE_NEITHER, "no work");
    appfab_check_t bad = chk("adder.sum", &SIX);
    CHECK(appfab_function(&W, &H, &bad, 1) == LPRES_STATE_FALSE && strstr(W.why, "check fails"),
          "a wrong answer: %s", W.why);
    appfab_check_t pair[2] = {c, bad};
    CHECK(appfab_function(&W, &H, pair, 2) == LPRES_STATE_FALSE, "one of two wrong");
    appfab_check_t pair2[2] = {bad, c};
    CHECK(appfab_function(&W, &H, pair2, 2) == LPRES_STATE_FALSE, "wrong first");
    appfab_check_t gone = chk("adder.nothing", &FIVE);
    CHECK(appfab_function(&W, &H, &gone, 1) == LPRES_STATE_FALSE, "an entry that is not there");
    appfab_check_t none = chk(NULL, &FIVE);
    CHECK(appfab_function(&W, &H, &none, 1) == LPRES_STATE_FALSE, "a check with no entry");
    appfab_check_t wrongarg = c;
    zxapp_arg_t one[1] = {{"a", VA, 1}};
    wrongarg.args = one;
    wrongarg.nargs = 1;
    CHECK(appfab_function(&W, &H, &wrongarg, 1) == LPRES_STATE_FALSE, "arguments off contract");
    appfab_check_t longer = c;
    zxapp_num_t two[2] = {{5, 1}, {0, 1}};
    longer.want = two;
    longer.nwant = 2;
    CHECK(appfab_function(&W, &H, &longer, 1) == LPRES_STATE_FALSE, "an answer of another length");
    appfab_check_t expect_both = c;
    expect_both.want_both = true;
    CHECK(appfab_function(&W, &H, &expect_both, 1) == LPRES_STATE_FALSE, "= both, but they agree");
    appfab_unload(&W, &H);
    CHECK(W.fit == LPRES_STATE_FALSE, "unload forgets the fit");
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_NEITHER, "no function after unload");
    CHECK(fit_app(CALC, PIN[0], 1) == LPRES_STATE_TRUE, "fit again: %s", W.why);
    appfab_unload(&W, NULL);
    CHECK(W.fit == LPRES_STATE_FALSE, "unload with no host still forgets the fit");
    appfab_unload(NULL, &H);
    appfab_unload(&W, &H);

    /* the languages disagree: held as BOTH, never accepted */
    part(1, "zxblock 1\nid adder\nversion 1.0.0\nprovides sum num a b\n"
            "logic kron sum <<\nadd\n.\nlogic eve sum <<\nadd1\n.\n");
    CHECK(fit_app(CALC, PIN[1], 2) == LPRES_STATE_TRUE, "a split adder fits: %s", W.why);
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_BOTH && strstr(W.why, "disagree"),
          "the languages disagree: %s", W.why);
    CHECK(appfab_function(&W, &H, &expect_both, 1) == LPRES_STATE_TRUE, "= both, and they do");
    CHECK(appfab_verdict(W.fit, LPRES_STATE_BOTH) == LPRES_STATE_BOTH, "held, not accepted");
    CHECK(appfab_function(&W, &H, &bad, 1) == LPRES_STATE_BOTH,
          "a split adder answers no check with one voice");
    appfab_check_t mixed[2] = {c, chk("adder.none", &FIVE)};
    CHECK(appfab_function(&W, &H, mixed, 2) == LPRES_STATE_FALSE && strstr(W.why, "disagree"),
          "BOTH and FALSE: FALSE, and why names the first: %s", W.why);
    appfab_unload(&W, &H);
    /* one language refuses what the other answers: BOTH */
    part(1, "zxblock 1\nid adder\nversion 1.0.0\nprovides sum num a b\n"
            "logic kron sum <<\nadd\n.\nlogic eve sum <<\nflaky\n.\n");
    CHECK(fit_app(CALC, PIN[1], 2) == LPRES_STATE_TRUE, "fit: %s", W.why);
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_BOTH, "half refuses: held");
    appfab_unload(&W, &H);
    /* every language refuses: FALSE */
    part(1, "zxblock 1\nid adder\nversion 1.0.0\nprovides sum num a b\n"
            "logic kron sum <<\nflaky\n.\nlogic eve sum <<\nflaky\n.\n");
    CHECK(fit_app(CALC, PIN[1], 2) == LPRES_STATE_TRUE, "fit: %s", W.why);
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_FALSE, "all refuse");
    appfab_unload(&W, &H);
    /* one language only: right, but unwitnessed */
    part(1,
         "zxblock 1\nid adder\nversion 1.0.0\nprovides sum num a b\nlogic kron sum <<\nadd\n.\n");
    CHECK(fit_app(CALC, PIN[1], 2) == LPRES_STATE_TRUE, "fit: %s", W.why);
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_NEITHER && strstr(W.why, "one language"),
          "one language: NEITHER: %s", W.why);
    CHECK(appfab_function(&W, &H, &bad, 1) == LPRES_STATE_FALSE, "one language, wrong: FALSE");
    appfab_unload(&W, &H);

    /* the verdict is the kernel's conjoin: only TRUE and TRUE accept */
    static const lpres_state_t S[4] = {LPRES_STATE_NEITHER, LPRES_STATE_TRUE, LPRES_STATE_FALSE,
                                       LPRES_STATE_BOTH};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            CHECK((appfab_verdict(S[i], S[j]) == LPRES_STATE_TRUE) ==
                      (S[i] == LPRES_STATE_TRUE && S[j] == LPRES_STATE_TRUE),
                  "verdict %d %d", (int) S[i], (int) S[j]);
}

/* a real app: the decision-engine blocks, pinned, in the c runtime */
static const char *ROOT;
static void test_real(void)
{
    static const char *const ids[3] = {"pce_prob", "pce_strat", "pce_fin"};
    for (uint32_t i = 0; i < 3; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/apps/blocks/%s.zxblock", ROOT, ids[i]);
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL, "open %s", path);
        if (!f) return;
        static char buf[TXT];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        part(i, buf);
    }
    char m[TXT];
    snprintf(m, sizeof m,
             "zxapp 1\nid deciders\nname Deciders\nuse pce_prob 1.0.0 %s\nuse pce_strat 1.0.0 %s\n"
             "use pce_fin 1.0.0 %s\n",
             PIN[0], PIN[1], PIN[2]);
    CHECK(appfab_fit(&W, (const uint8_t *) m, (uint32_t) strlen(m), P, 3, &H) == LPRES_STATE_TRUE,
          "the shipped decision blocks fit: %s", W.why);
    static const int64_t sa[4] = {99, 1, 0, 0}, sb[4] = {1, 99, 0, 0};
    zxapp_arg_t j[2] = {{"a", sa, 4}, {"b", sb, 4}};
    static const zxapp_num_t want[4] = {{99, 10000}, {99, 10000}, {4901, 5000}, {0, 1}};
    appfab_check_t c = {"pce_prob.join", j, 2, want, 4, false};
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_NEITHER, "right, one language: %s", W.why);
    static const zxapp_num_t off[4] = {{99, 10000}, {99, 10000}, {4900, 5000}, {0, 1}};
    c.want = off;
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_FALSE, "one count off: FALSE");
    appfab_unload(&W, &H);
}

/* the matrix's golden table (docs/spec/ZECKENDORF_ANCESTORS.tsv, written by
 * pack_matrix.py): every cell of layers 0..144 against the rule */
static void test_golden(void)
{
    char path[512];
    snprintf(path, sizeof path, "%s/../../docs/spec/ZECKENDORF_ANCESTORS.tsv", ROOT);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL, "the golden table: %s", path);
    if (!f) return;
    static char line[4096];
    uint32_t rows = 0, cells = 0, wrong = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char *tab[5], *p = line;
        uint32_t k = 0;
        for (; k < 5u && p; k++) {
            tab[k] = p;
            p = strchr(p, '\t');
            if (p) *p++ = 0;
        }
        if (k != 5u) {
            wrong++;
            continue;
        }
        uint32_t n = (uint32_t) strtoul(tab[0], NULL, 10);
        bool in[145] = {false};
        for (char *q = strtok(tab[4], " \n"); q; q = strtok(NULL, " \n"))
            if (*q != '-' && strtoul(q, NULL, 10) <= 144u) in[strtoul(q, NULL, 10)] = true;
        in[n] = true; /* a layer may always draw on itself */
        for (uint32_t m = 0; m <= 144u; m++, cells++)
            if (appfab_zeck_may_use(n, m) != in[m]) {
                if (wrong < 5u)
                    printf("  layer %u, part %u: rule %d, table %d\n", (unsigned) n, (unsigned) m,
                           appfab_zeck_may_use(n, m), in[m]);
                wrong++;
            }
        rows++;
    }
    fclose(f);
    CHECK(rows == 145u && cells == 145u * 145u && wrong == 0,
          "the rule is the matrix's table: %u rows, %u cells, %u differ", (unsigned) rows,
          (unsigned) cells, (unsigned) wrong);
}

/* ---- the matrix: Zeckendorf ancestors and empty scaffolds (F6, F7) ---------- */

static bool in_anc(const uint32_t *anc, uint32_t k, uint32_t m)
{
    for (uint32_t i = 0; i < k; i++)
        if (anc[i] == m) return true;
    return false;
}

static void test_matrix(void)
{
    /* self and lin(n), written out by hand (the matrix thread's worked
     * examples for 4, 12, 100 and 143), not by the rule */
    static const struct {
        uint32_t n, k, anc[16];
    } V[] = {
        {0, 1, {0}},
        {1, 2, {1, 0}},
        {2, 3, {2, 1, 0}},
        {4, 5, {4, 3, 2, 1, 0}},            /* 3 + 1 */
        {5, 5, {5, 3, 2, 1, 0}},            /* an anchor: the anchors below */
        {7, 6, {7, 5, 3, 2, 1, 0}},         /* 5 + 2 */
        {12, 8, {12, 8, 5, 4, 3, 2, 1, 0}}, /* 8 + 4, 4 = 3 + 1 */
        {100, 13, {100, 0, 1, 2, 3, 5, 8, 11, 13, 21, 34, 55, 89}}, /* 89 + 11 */
        {143, 15, {143, 0, 1, 2, 3, 5, 7, 8, 13, 20, 21, 34, 54, 55, 89}},
        {144, 12, {144, 89, 55, 34, 21, 13, 8, 5, 3, 2, 1, 0}},
    };
    for (uint32_t v = 0; v < sizeof V / sizeof V[0]; v++)
        for (uint32_t m = 0; m <= 150u; m++)
            CHECK(appfab_zeck_may_use(V[v].n, m) == in_anc(V[v].anc, V[v].k, m),
                  "layer %u may use %u: %d", (unsigned) V[v].n, (unsigned) m,
                  appfab_zeck_may_use(V[v].n, m));
    /* over the whole matrix: never upward, always the core, and closed (an
     * ancestor's ancestors are ancestors, so checking each use against its
     * own user's layer is enough) */
    uint32_t bad = 0;
    for (uint32_t n = 0; n <= 144u; n++) {
        if (!appfab_zeck_may_use(n, 0) || !appfab_zeck_may_use(n, n)) bad++;
        for (uint32_t m = n + 1u; m <= 150u; m++) bad += appfab_zeck_may_use(n, m);
        for (uint32_t m = 0; m < n; m++)
            for (uint32_t k = 0; k < m && appfab_zeck_may_use(n, m); k++)
                if (appfab_zeck_may_use(m, k) && !appfab_zeck_may_use(n, k)) bad++;
    }
    CHECK(bad == 0, "the ancestor rule is downward, rooted and closed (%u breaks)", (unsigned) bad);
    CHECK(appfab_zeck_may_use(0xFFFFFFFFu, 0) && !appfab_zeck_may_use(0xFFFFFFFEu, 0xFFFFFFFFu),
          "no overflow at the top of the range");
    CHECK(appfab_zeck_may_use(2971215073u, 1836311903u) &&
              !appfab_zeck_may_use(2971215073u, 1836311904u),
          "F(47), the largest 32-bit Fibonacci number, is an anchor over F(46)");

    /* layered fits */
    part(0, ADDER);
    P[0].layer = 8;
    P[0].layered = true;
    snprintf(APP, sizeof APP, CALC, PIN[0]);
    uint32_t len = (uint32_t) strlen(APP);
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 12, P, 1, &H) == LPRES_STATE_TRUE,
          "layer 12 uses layer 8: %s", W.why);
    appfab_unload(&W, &H);
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 8, P, 1, &H) == LPRES_STATE_TRUE,
          "its own layer: %s", W.why);
    appfab_unload(&W, &H);
    P[0].layer = 7;
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 12, P, 1, &H) == LPRES_STATE_FALSE &&
              strstr(W.why, "Zeckendorf"),
          "layer 12 cannot reach layer 7: %s", W.why);
    P[0].layer = 5;
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 12, P, 1, &H) == LPRES_STATE_TRUE,
          "but reaches anchor 5: %s", W.why);
    appfab_unload(&W, &H);
    P[0].layer = 13;
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 12, P, 1, &H) == LPRES_STATE_FALSE,
          "nor a higher layer");
    P[0].layer = 0;
    P[0].layered = false;
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 12, P, 1, &H) == LPRES_STATE_FALSE &&
              strstr(W.why, "no matrix layer"),
          "a part with no layer is not the core: %s", W.why);
    CHECK(appfab_fit(&W, (const uint8_t *) APP, len, P, 1, &H) == LPRES_STATE_TRUE,
          "the same part outside the matrix: %s", W.why);
    appfab_unload(&W, &H);
    CHECK(W.layer == APPFAB_NO_LAYER, "appfab_fit is unlayered");
    P[0].layered = true;
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 12, P, 1, &H) == LPRES_STATE_TRUE,
          "the core serves every layer: %s", W.why);
    appfab_unload(&W, &H);

    /* nested: each use is checked against the layer of the part that makes
     * it. Layer 7 = 5 + 2 may use 5 and 2; a part at 2 may not use 5. */
    char mid[TXT];
    snprintf(mid, sizeof mid,
             "zxapp 1\nid mid\nversion 1.0.0\nuse adder 1.0.0 %.71s\n"
             "provides adder.sum num a b\n",
             PIN[0]);
    part(1, mid);
    P[0].layer = 5;
    P[0].layered = true;
    P[1].layer = 2;
    P[1].layered = true;
    snprintf(APP, sizeof APP, "zxapp 1\nid top\nuse mid 1.0.0 %.71s\nuse adder 1.0.0 %.71s\n",
             PIN[1], PIN[0]);
    len = (uint32_t) strlen(APP);
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 7, P, 2, &H) == LPRES_STATE_FALSE &&
              strstr(W.why, "Zeckendorf"),
          "layer 2 reaching layer 5 through layer 7: %s", W.why);
    P[0].layer = 2;
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 7, P, 2, &H) == LPRES_STATE_TRUE,
          "every edge downward: %s", W.why);
    appfab_unload(&W, &H);
    P[1].layer = 5;
    P[0].layer = 5;
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 7, P, 2, &H) == LPRES_STATE_TRUE,
          "a part may use its own layer: %s", W.why);
    appfab_unload(&W, &H);
    P[1].layer = 7;
    CHECK(appfab_fit_layered(&W, (const uint8_t *) APP, len, 5, P, 2, &H) == LPRES_STATE_FALSE,
          "a part above its user");

    /* empty scaffolds: a glue node, marked empty, never accepted */
    const char *glue = "zxapp 1\nid glue\nversion 1.0.0\n";
    len = (uint32_t) strlen(glue);
    CHECK(appfab_fit(&W, (const uint8_t *) glue, len, P, 0, &H) == LPRES_STATE_NEITHER &&
              strstr(W.why, "empty scaffold"),
          "an empty app is NEITHER: %s", W.why);
    CHECK(appfab_fit_layered(&W, (const uint8_t *) glue, len, 21, P, 2, &H) == LPRES_STATE_NEITHER,
          "an empty layer is NEITHER, decoy parts or not");
    appfab_check_t c = chk("glue.x", &FIVE);
    CHECK(appfab_function(&W, &H, &c, 1) == LPRES_STATE_NEITHER, "no function on a scaffold");
    CHECK(appfab_verdict(W.fit, LPRES_STATE_TRUE) == LPRES_STATE_NEITHER,
          "never accepted, whatever function says");
    const char *named = "zxapp 1\nid glue\nversion 1.0.0\nname Glue\nprovides x num a\n";
    CHECK(appfab_fit(&W, (const uint8_t *) named, (uint32_t) strlen(named), P, 0, &H) ==
              LPRES_STATE_FALSE,
          "a promise with no logic is not a scaffold: it is FALSE");
    /* the closure: a sender's check, with no host and nothing loaded */
    part(0, ADDER);
    part(1, "zxblock 1\nid adder\nversion 1.0.0\nprovides sum num a b\n"
            "logic sutra sum <<\nadd\n.\n");
    snprintf(APP, sizeof APP, CALC, PIN[1]);
    len = (uint32_t) strlen(APP);
    CHECK(appfab_closure(&W, (const uint8_t *) APP, len, APPFAB_NO_LAYER, P, 2) == LPRES_STATE_TRUE,
          "a closure needs no runtime: %s", W.why);
    CHECK(W.fit == LPRES_STATE_FALSE, "a closure is not a fit");
    appfab_check_t cc = chk("adder.sum", &FIVE);
    CHECK(appfab_function(&W, &H, &cc, 1) == LPRES_STATE_NEITHER, "so no function follows");
    CHECK(appfab_fit(&W, (const uint8_t *) APP, len, P, 2, &H) == LPRES_STATE_FALSE,
          "while the fit on this host refuses it");
    CHECK(appfab_closure(&W, (const uint8_t *) APP, len, 12, P, 2) == LPRES_STATE_FALSE &&
              strstr(W.why, "no matrix layer"),
          "the closure keeps the matrix rule: %s", W.why);
    P[1].layer = 4;
    P[1].layered = true;
    CHECK(appfab_closure(&W, (const uint8_t *) APP, len, 12, P, 2) == LPRES_STATE_TRUE,
          "and passes it: %s", W.why);
    CHECK(appfab_closure(&W, (const uint8_t *) APP, len, 13, P, 2) == LPRES_STATE_FALSE, "or not");
    snprintf(APP, sizeof APP, "zxapp 1\nid calc\nuse adder 1.0.0\n");
    CHECK(appfab_closure(&W, (const uint8_t *) APP, (uint32_t) strlen(APP), APPFAB_NO_LAYER, P,
                         2) == LPRES_STATE_FALSE &&
              strstr(W.why, "not pinned"),
          "an unpinned use: %s", W.why);
    CHECK(appfab_closure(&W, (const uint8_t *) glue, (uint32_t) strlen(glue), 21, P, 0) ==
              LPRES_STATE_NEITHER,
          "an empty scaffold");
    CHECK(appfab_closure(0, (const uint8_t *) glue, 1, 0, P, 0) == LPRES_STATE_FALSE &&
              appfab_closure(&W, 0, 0, 0, P, 0) == LPRES_STATE_FALSE &&
              appfab_closure(&W, (const uint8_t *) glue, 1, 0, 0, 1) == LPRES_STATE_FALSE,
          "missing arguments");
    const char *eblock = "zxblock 1\nid glue\nversion 1.0.0\n";
    CHECK(appfab_fit(&W, (const uint8_t *) eblock, (uint32_t) strlen(eblock), P, 0, &H) ==
              LPRES_STATE_FALSE,
          "an empty block is not an app");
}

/* ---- the whole way: signed recipe, fab, gate ---------------------------------- */

static uint8_t spk[FAB_SIG_PK], ssk[FAB_SIG_SK], opk[FAB_SIG_PK], osk[FAB_SIG_SK];
static fab_work_t FW;
static fab_ient_t TABLE[1u << 14];
static uint8_t OPS[FAB_OP_REC * 256], DIFF[TXT], PLAN[1u << 16], REC[1u << 17], PB[1u << 16];
static uint8_t OUT[TXT], DIFFB[TXT];
static uint32_t STORE_N, PROG;
static const appfab_part_t *STORE;

static int get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    (void) ctx;
    for (uint32_t i = 0; i < STORE_N; i++)
        if (ipfsn_cid_equal(cid, &STORE[i].cid)) {
            if (STORE[i].len > cap) return -1;
            memcpy(buf, STORE[i].data, STORE[i].len);
            *len = STORE[i].len;
            return 0;
        }
    return -1;
}

static void on_progress(void *ctx, const fab_progress_t *pr)
{
    (void) ctx;
    if (pr->phase == FAB_PHASE_FETCH) PROG++;
}

/* A recipe for app text over parts src[0..n) (all listed, none sliced
 * unless the app shares bytes with them). */
static uint32_t pack(const char *app, const appfab_part_t *src, uint32_t n, const uint8_t *pk,
                     const uint8_t *sk)
{
    static fab_src_t fs[APPFAB_PARTS];
    for (uint32_t i = 0; i < n; i++)
        fs[i] = (fab_src_t){src[i].data, src[i].len, src[i].cid, false};
    uint32_t tl = (uint32_t) strlen(app), no = 0, dl = 0, pl = 0, rl = 0;
    ipfsn_cid_t tc;
    CHECK(fab_build(fs, n, (const uint8_t *) app, tl, 32, TABLE, 14, OPS, 256, &no, DIFF,
                    sizeof DIFF, &dl) == FAB_OK,
          "build");
    CHECK(fab_target_cid((const uint8_t *) app, tl, &FW, &tc) == FAB_OK, "target cid");
    CHECK(fab_plan_write(&tc, tl, 0, fs, n, OPS, no, PLAN, sizeof PLAN, &pl) == FAB_OK, "plan");
    fab_seal_args_t a = {FAB_KIND_FILE, FAB_MODE_PUBLIC, pk, sk, 0, 0, 0};
    CHECK(fab_recipe_seal(&a, PLAN, pl, DIFF, dl, 0, REC, sizeof REC, &rl) == FAB_OK, "seal");
    return rl;
}

/* receive: check, open, hydrate, assemble, then the gate over the plan's blocks */
static appfab_part_t GOT[APPFAB_PARTS];
static int receive(uint32_t rl, const uint8_t (*trust)[FAB_ID], uint32_t nt, lpres_state_t *fit,
                   uint32_t *nparts)
{
    fab_recipe_t r;
    fab_plan_t p;
    uint32_t olen = 0;
    int rc = fab_recipe_parse(REC, rl, &r);
    if (!rc) rc = fab_recipe_check(&r, trust, nt);
    if (!rc) rc = fab_open_plan(&r, 0, PB, sizeof PB, &p);
    fab_io_t io = {get, 0, 0, 0, 0, 0, DIFFB, sizeof DIFFB, OUT, sizeof OUT, &FW, on_progress, 0};
    if (!rc) rc = fab_hydrate(&p, &io, 0);
    if (!rc) rc = fab_assemble(&r, &p, 0, &io, 0, &olen);
    if (rc) return rc;
    /* the gate takes the parts fab fetched: each block the plan lists */
    *nparts = p.n_blocks;
    for (uint32_t i = 0; i < p.n_blocks && i < APPFAB_PARTS; i++) {
        uint32_t want = 0;
        CHECK(fab_plan_block(&p, i, &GOT[i].cid, &want) == FAB_OK, "plan block");
        GOT[i].data = 0;
        GOT[i].len = want;
        for (uint32_t k = 0; k < STORE_N; k++)
            if (ipfsn_cid_equal(&GOT[i].cid, &STORE[k].cid)) GOT[i].data = STORE[k].data;
    }
    *fit = appfab_fit(&W, OUT, olen, GOT, p.n_blocks, &H);
    return FAB_OK;
}

static void test_whole_way(void)
{
    uint8_t s1[32], s2[32];
    memset(s1, 1, 32);
    memset(s2, 2, 32);
    fab_sig_keypair(s1, spk, ssk);
    fab_sig_keypair(s2, opk, osk);
    uint8_t id[1][FAB_ID];
    fab_key_id(spk, FAB_SIG_PK, id[0]);
    FW.loaded = -1;

    part(0, ADDER);
    part(1, "zxblock 1\nid decoy\nversion 1.0.0\nprovides x num a\nlogic kron x <<\nadd\n.\n");
    snprintf(APP, sizeof APP, CALC, PIN[0]);
    STORE = P;
    STORE_N = 2;
    uint32_t rl = pack(APP, P, 2, spk, ssk), np = 0;
    lpres_state_t f = LPRES_STATE_NEITHER;
    PROG = 0;
    CHECK(receive(rl, (const uint8_t(*)[FAB_ID]) id, 1, &f, &np) == FAB_OK, "received");
    CHECK(PROG == 2 && np == 2, "both parts fetched and checked (%u)", (unsigned) PROG);
    CHECK(f == LPRES_STATE_TRUE, "the fabricated app fits: %s", W.why);
    appfab_check_t c = chk("adder.sum", &FIVE);
    CHECK(appfab_verdict(f, appfab_function(&W, &H, &c, 1)) == LPRES_STATE_TRUE,
          "and is accepted: %s", W.why);
    appfab_unload(&W, &H);

    /* deterministic: the same app and parts give the same recipe bytes */
    static uint8_t first[sizeof REC];
    memcpy(first, REC, rl);
    uint32_t rl2 = pack(APP, P, 2, spk, ssk);
    CHECK(rl2 == rl && !memcmp(first, REC, rl), "the pack is deterministic");

    /* forged: another key signs it, the trust list holds only ours */
    rl = pack(APP, P, 2, opk, osk);
    CHECK(receive(rl, (const uint8_t(*)[FAB_ID]) id, 1, &f, &np) == FAB_ERR_TRUST,
          "a recipe from another signer");
    /* tampered after signing: one byte of the plan */
    rl = pack(APP, P, 2, spk, ssk);
    REC[FAB_HDR + 3] ^= 1;
    CHECK(receive(rl, 0, 0, &f, &np) == FAB_ERR_SIGN, "a tampered recipe");
    /* a part missing from the store: never reaches the gate */
    rl = pack(APP, P, 2, spk, ssk);
    STORE_N = 1;
    CHECK(receive(rl, 0, 0, &f, &np) == FAB_ERR_FETCH, "a block not in the store");
    STORE_N = 2;
    /* a part that lies about its bytes */
    T[0][5] ^= 1;
    CHECK(receive(rl, 0, 0, &f, &np) == FAB_ERR_BLOCK, "a block that does not match");
    T[0][5] ^= 1;
    /* the sender left the pinned block out of the recipe: fab rebuilds the
     * manifest, the gate refuses it */
    rl = pack(APP, P + 1, 1, spk, ssk);
    CHECK(receive(rl, 0, 0, &f, &np) == FAB_OK && f == LPRES_STATE_FALSE &&
              strstr(W.why, "does not carry"),
          "a recipe without its pinned block: %s", W.why);
    /* a signed recipe for an app that does not run here */
    part(2,
         "zxblock 1\nid adder\nversion 1.0.0\nprovides sum num a b\nlogic sutra sum <<\nadd\n.\n");
    snprintf(APP, sizeof APP, CALC, PIN[2]);
    STORE_N = 3;
    rl = pack(APP, P + 2, 1, spk, ssk);
    CHECK(receive(rl, 0, 0, &f, &np) == FAB_OK && f == LPRES_STATE_FALSE &&
              strstr(W.why, "no runtime"),
          "a signed app this host cannot run: %s", W.why);
    /* a signed recipe whose app answers wrong: fits, then fails its check */
    part(2, "zxblock 1\nid adder\nversion 1.0.0\nprovides sum num a b\n"
            "logic kron sum <<\nadd1\n.\nlogic eve sum <<\nadd1\n.\n");
    snprintf(APP, sizeof APP, CALC, PIN[2]);
    rl = pack(APP, P + 2, 1, spk, ssk);
    CHECK(receive(rl, 0, 0, &f, &np) == FAB_OK && f == LPRES_STATE_TRUE, "it fits: %s", W.why);
    CHECK(appfab_verdict(f, appfab_function(&W, &H, &c, 1)) == LPRES_STATE_FALSE,
          "a signed app that answers wrong is refused");
    appfab_unload(&W, &H);
    /* round trip: unpack, fit and check again give the same verdict */
    rl = pack(APP, P + 2, 1, spk, ssk);
    CHECK(receive(rl, 0, 0, &f, &np) == FAB_OK && f == LPRES_STATE_TRUE, "again: %s", W.why);
    CHECK(appfab_verdict(f, appfab_function(&W, &H, &c, 1)) == LPRES_STATE_FALSE, "same verdict");
    appfab_unload(&W, &H);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ROOT = argc > 1 ? argv[1] : "src";
    host();
    test_fit();
    test_sources();
    test_nesting();
    test_matrix();
    test_golden();
    test_function();
    test_real();
    test_whole_way();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
