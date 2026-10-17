/* zxvd_app.c — the live app testbed: load and run a .zxapp on a running
 * zxvd with no recompile. The interpreted pillars (Eve, KRON, MYRA,
 * Sutra) register against the manifest host; a POST /api/app carries the
 * manifest text, zxapp_load compiles every logic entry with its pillar,
 * and the app's self-test entry runs — the verdict is the reply. This is
 * the develop-then-integrate loop: apps draft, transfer and test here,
 * and only a complete, debugged set ever combines into the OS image.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zxapp.h"
#include "zxapp_eve.h"
#include "zxapp_kron.h"
#include "zxapp_myra.h"
#include "zxapp_sutra.h"

#define ZXVD_APP_MAX (16u * 1024u) /* one manifest, bounded */

static zxapp_host_t GH;
static zxapp_t GA;

/* [status][len u16 BE][text] */
static void put_reply(char **out, size_t *out_len, uint8_t st, const char *txt)
{
    size_t n = strlen(txt);
    char *p;
    if (n > 1400) n = 1400;
    p = (char *) malloc(n + 3);
    if (!p) {
        *out = NULL;
        *out_len = 0;
        return;
    }
    p[0] = (char) st;
    p[1] = (char) (n >> 8);
    p[2] = (char) n;
    memcpy(p + 3, txt, n);
    *out = p;
    *out_len = n + 3;
}

int zxvd_app_init(void)
{
    zxapp_host_init(&GH);
    if (zxapp_eve_register(&GH) != 0) return -1;
    if (zxapp_kron_register(&GH) != 0) return -1;
    if (zxapp_myra_register(&GH) != 0) return -1;
    if (zxapp_sutra_register(&GH) != 0) return -1;
    return 0;
}

/* Handle POST /api/app: body = the .zxapp manifest text. Returns a reply
 * buffer (malloc'd; the caller frees it) or NULL on OOM. The reply is
 * [status][len u16 BE][text]: 0 = loaded and every check line passed; 1 =
 * parse error; 2 = load error; 3 = a check failed (err/err_line in the
 * text); 4 = no check lines. */
int zxvd_app_handle(const char *body, size_t len, char **out, size_t *out_len)
{
    char why[256];
    int st;
    if (!body || len > ZXVD_APP_MAX) {
        put_reply(out, out_len, 1, "manifest too large");
        return 0;
    }
    zxapp_unload(&GA, &GH);
    memset(&GA, 0, sizeof GA);
    st = zxapp_parse(&GA, body, (uint32_t) len);
    if (st != ZXAPP_OK) {
        snprintf(why, sizeof why, "parse failed: %s", GA.err);
        put_reply(out, out_len, 1, why);
        return 0;
    }
    st = zxapp_load(&GA, &GH);
    if (st < 0) {
        snprintf(why, sizeof why, "load failed: %s", GA.err);
        put_reply(out, out_len, 2, why);
        return 0;
    }
    if (st == 0) {
        put_reply(out, out_len, 2, "nothing loaded (no runtime for any entry)");
        return 0;
    }
    /* the app's proof: every check line must pass (K2) */
    st = zxapp_check(&GA, &GH);
    if (st == ZXAPP_E_CHECK || st == ZXAPP_E_NAME || st == ZXAPP_E_CONTRACT) {
        snprintf(why, sizeof why, "check failed (line %d): %s", GA.err_line, GA.err);
        put_reply(out, out_len, 3, why);
        return 0;
    }
    if (st == 0) {
        put_reply(out, out_len, 4, "no check lines");
        return 0;
    }
    snprintf(why, sizeof why, "loaded, %d checks passed", st);
    put_reply(out, out_len, 0, why);
    return 0;
}

void zxvd_app_free(char *p)
{
    free(p);
}