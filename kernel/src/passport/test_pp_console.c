/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pp_console.c -- the console login (pp_console.h): a typed seed logs
 * in as the identity pp_hydrate builds from that seed; a typo is refused by
 * the checksum; whoami and logout report and forget it. Also prints the seed
 * text of TEST_SEED so build_system/golden_login_check.sh can type it. */
#include <stdio.h>
#include <string.h>
#include "pp_console.h"
#include "passport.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s\n", msg);                                                           \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("  [PASS] %s\n", msg);                                                          \
        }                                                                                          \
    } while (0)

static char g_out[512];
static char g_all[1024]; /* every line since the last clear_all */
static void clear_all(void)
{
    g_all[0] = 0;
}
static void capture(const char *s)
{
    strncpy(g_out, s, sizeof g_out - 1);
    strncat(g_all, s, sizeof g_all - strlen(g_all) - 1);
}

int main(void)
{
    uint8_t seed[PP_SEED];
    for (uint32_t i = 0; i < PP_SEED; i++) seed[i] = (uint8_t) (0x5Au ^ (i * 37u));
    char text[PP_SEED_TEXT];
    CHECK(pp_seed_to_text(seed, text) == PP_OK, "the test seed has a text form");
    printf("  seed text %s\n", text);

    static pp_passport_t ref;
    CHECK(pp_hydrate(&ref, PQM_LEVEL_MATRIX, seed) == PP_OK,
          "pp_hydrate builds the reference identity");
    char did[ID_DID_CHARS], want[256];
    id_did_string(ref.root.did, did);
    printf("  did %s\n", did); /* golden_login_check.sh expects this on the disc */
    snprintf(want, sizeof want, "[LOGIN_OK] %s\n", did);

    CHECK(!pp_console_logged_in(), "nobody is logged in at first");
    pp_console_whoami(capture);
    CHECK(strcmp(g_out, "[WHOAMI] nobody (log in: login <seed text>)\n") == 0,
          "whoami says nobody");

    CHECK(pp_console_login(text, (uint32_t) strlen(text), capture) == 0, "the seed text logs in");
    CHECK(strcmp(g_out, want) == 0, "[LOGIN_OK] names the identity the seed builds");
    CHECK(pp_console_logged_in(), "... and the console is logged in");
    pp_console_whoami(capture);
    snprintf(want, sizeof want, "[WHOAMI] %s\n", did);
    CHECK(strcmp(g_out, want) == 0, "whoami names the same identity");

    /* Lower case, no dashes: the same person. */
    char loose[PP_SEED_TEXT];
    uint32_t n = 0;
    for (const char *p = text; *p; p++)
        if (*p != '-') loose[n++] = (char) ((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
    CHECK(pp_console_login(loose, n, capture) == 0 && strstr(g_out, did),
          "lower case without dashes is the same identity");

    /* One character changed: refused by the checksum, and logged out. */
    char typo[PP_SEED_TEXT];
    memcpy(typo, text, sizeof typo);
    typo[3] = typo[3] == '7' ? '8' : '7';
    CHECK(pp_console_login(typo, (uint32_t) strlen(typo), capture) == PP_ERR_CHECKSUM,
          "a typo is refused by the checksum");
    CHECK(strcmp(g_out, "[LOGIN_FAIL] seed checksum wrong\n") == 0, "... with [LOGIN_FAIL]");
    CHECK(!pp_console_logged_in(), "... and nobody is logged in after a failed login");
    CHECK(pp_console_login("hello", 5, capture) != 0 && strncmp(g_out, "[LOGIN_FAIL] ", 13) == 0,
          "text that is not a seed is refused");

    CHECK(pp_console_login(text, (uint32_t) strlen(text), capture) == 0, "logs in again");
    pp_console_logout(capture);
    snprintf(want, sizeof want, "[LOGOUT] %s\n", did);
    CHECK(strcmp(g_out, want) == 0 && !pp_console_logged_in(), "logout names who left and forgets");
    pp_console_logout(capture);
    CHECK(strcmp(g_out, "[LOGOUT] nobody was logged in\n") == 0, "a second logout says nobody");

    /* First time: create from random bytes, confirm by typing it back. */
    uint8_t zero[PP_SEED] = {0};
    CHECK(pp_console_create(zero, capture) != 0 && strstr(g_out, "[CREATE_FAIL]"),
          "all-zero random bytes are refused");
    CHECK(pp_console_confirm(text, (uint32_t) strlen(text), capture) != 0 &&
              !pp_console_logged_in(),
          "confirm with nothing pending is refused");
    clear_all();
    CHECK(pp_console_create(seed, capture) == 0, "create takes 32 random bytes");
    snprintf(want, sizeof want, "[NEW_SEED] %s\n", text);
    CHECK(strncmp(g_all, want, strlen(want)) == 0 && strstr(g_all, "Write this down"),
          "create shows the seed text once and asks to write it down");
    CHECK(!pp_console_logged_in(), "... and nobody is logged in until it is typed back");
    CHECK(pp_console_confirm(typo, (uint32_t) strlen(typo), capture) != 0 &&
              !pp_console_logged_in(),
          "a mistyped confirmation is refused");
    uint8_t other[PP_SEED];
    char other_text[PP_SEED_TEXT];
    for (uint32_t i = 0; i < PP_SEED; i++) other[i] = (uint8_t) (seed[i] ^ 0x11u);
    pp_seed_to_text(other, other_text);
    CHECK(pp_console_confirm(other_text, (uint32_t) strlen(other_text), capture) == PP_ERR_AUTH &&
              !pp_console_logged_in(),
          "a valid seed that is not the one shown is refused");
    clear_all();
    CHECK(pp_console_confirm(loose, n, capture) == 0 && pp_console_logged_in(),
          "typing the shown seed back logs in");
    snprintf(want, sizeof want, "[CREATED] %s\n[LOGIN_OK] %s\n", did, did);
    CHECK(strcmp(g_all, want) == 0, "... as the identity a later login with that seed builds");
    CHECK(pp_console_confirm(text, (uint32_t) strlen(text), capture) != 0,
          "the seed can be confirmed only once");
    CHECK(pp_console_create(seed, capture) == 0 && !pp_console_logged_in() &&
              pp_console_login(text, (uint32_t) strlen(text), capture) == 0 &&
              pp_console_confirm(text, (uint32_t) strlen(text), capture) != 0,
          "create logs out; a login drops the pending seed");
    pp_console_logout(capture);

    pp_wipe(&ref);
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
