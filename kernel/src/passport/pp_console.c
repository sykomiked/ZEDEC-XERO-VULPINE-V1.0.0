/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pp_console.c -- a console's passport login. See pp_console.h. */
#include "pp_console.h"
#include "passport.h"

static pp_passport_t g_pp;
static bool g_in;
static char g_line[128];
/* A seed made by pp_console_create, held until it is typed back. */
static uint8_t g_new[PP_SEED];
static bool g_pending;

static void say(void (*out)(const char *), const char *a, const char *b)
{
    uint32_t n = 0;
    while (*a && n < sizeof g_line - 2) g_line[n++] = *a++;
    while (b && *b && n < sizeof g_line - 2) g_line[n++] = *b++;
    g_line[n++] = '\n';
    g_line[n] = 0;
    if (out) out(g_line);
}

static void wipe(void *p, uint32_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) v[i] = 0;
}

int32_t pp_console_login(const char *text, uint32_t len, void (*out)(const char *))
{
    uint8_t seed[PP_SEED];
    char did[ID_DID_CHARS];
    if (g_in) {
        pp_wipe(&g_pp);
        g_in = false;
    }
    wipe(g_new, sizeof g_new); /* a login drops a seed still waiting to be confirmed */
    g_pending = false;
    pp_status_t st = pp_seed_from_text(text, len, seed);
    if (st == PP_OK) st = pp_hydrate(&g_pp, PQM_LEVEL_MATRIX, seed);
    wipe(seed, sizeof seed);
    if (st != PP_OK) {
        pp_wipe(&g_pp);
        say(out, "[LOGIN_FAIL] ", pp_status_name(st));
        return (int32_t) st;
    }
    g_in = true;
    id_did_string(g_pp.root.did, did);
    say(out, "[LOGIN_OK] ", did);
    return 0;
}

void pp_console_logout(void (*out)(const char *))
{
    char did[ID_DID_CHARS];
    if (!g_in) {
        say(out, "[LOGOUT] nobody was logged in", 0);
        return;
    }
    id_did_string(g_pp.root.did, did);
    pp_wipe(&g_pp);
    g_in = false;
    say(out, "[LOGOUT] ", did);
}

void pp_console_whoami(void (*out)(const char *))
{
    char did[ID_DID_CHARS];
    if (!g_in) {
        say(out, "[WHOAMI] nobody (log in: login <seed text>)", 0);
        return;
    }
    id_did_string(g_pp.root.did, did);
    say(out, "[WHOAMI] ", did);
}

int32_t pp_console_create(const uint8_t rnd[PP_SEED], void (*out)(const char *))
{
    char text[PP_SEED_TEXT];
    if (g_in) {
        pp_wipe(&g_pp);
        g_in = false;
    }
    uint8_t any = 0;
    for (uint32_t i = 0; i < PP_SEED; i++) {
        g_new[i] = rnd[i];
        any |= rnd[i];
    }
    if (!any) {
        wipe(g_new, sizeof g_new);
        g_pending = false;
        say(out, "[CREATE_FAIL] the random source gave all zeros", 0);
        return (int32_t) PP_ERR_ARG;
    }
    pp_status_t st = pp_seed_to_text(g_new, text);
    if (st != PP_OK) {
        wipe(g_new, sizeof g_new);
        g_pending = false;
        say(out, "[CREATE_FAIL] ", pp_status_name(st));
        return (int32_t) st;
    }
    g_pending = true;
    say(out, "[NEW_SEED] ", text);
    say(out,
        "  Write this down: it is the only way back in. Then type it back: confirm <seed text>", 0);
    wipe(text, sizeof text);
    return 0;
}

int32_t pp_console_confirm(const char *text, uint32_t len, void (*out)(const char *))
{
    uint8_t seed[PP_SEED];
    char did[ID_DID_CHARS];
    if (!g_pending) {
        say(out, "[CREATE_FAIL] no new seed to confirm (make one: create)", 0);
        return (int32_t) PP_ERR_ARG;
    }
    pp_status_t st = pp_seed_from_text(text, len, seed);
    uint8_t diff = 0;
    if (st == PP_OK)
        for (uint32_t i = 0; i < PP_SEED; i++) diff |= (uint8_t) (seed[i] ^ g_new[i]);
    if (st != PP_OK || diff) {
        wipe(seed, sizeof seed);
        say(out, "[CREATE_FAIL] ",
            st != PP_OK ? pp_status_name(st) : "that is not the seed shown; type it again");
        return st != PP_OK ? (int32_t) st : (int32_t) PP_ERR_AUTH;
    }
    st = pp_hydrate(&g_pp, PQM_LEVEL_MATRIX, seed);
    wipe(seed, sizeof seed);
    wipe(g_new, sizeof g_new);
    g_pending = false;
    if (st != PP_OK) {
        pp_wipe(&g_pp);
        say(out, "[CREATE_FAIL] ", pp_status_name(st));
        return (int32_t) st;
    }
    g_in = true;
    id_did_string(g_pp.root.did, did);
    say(out, "[CREATED] ", did);
    say(out, "[LOGIN_OK] ", did);
    return 0;
}

bool pp_console_logged_in(void)
{
    return g_in;
}
