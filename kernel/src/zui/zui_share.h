/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_share.h — the share sheet: hand what is in front of the person to
 * another app that can take it.
 *
 * Apps say what they can do in their manifests ("intent share text/plain
 * append", zxapp.h I1). The share sheet asks which installed apps handle
 * the verb for the content's type, shows them, and runs the one the person
 * picks through zxapp_call.
 *
 *   SH1 INSTALLED APPS. zui_share_add registers an app (a parsed, loaded
 *       zxapp_t the caller keeps alive). Up to ZUI_SHARE_APPS; an app is
 *       added once.
 *   SH2 THE SHEET for content of a type lists every app that handles the
 *       verb for it (zxapp_handlers: most specific first, each app once),
 *       the targets used most recently first, then the rest in that
 *       order. The app the content comes from is left out.
 *   SH3 RECENTS. A pick that runs remembers its app, most recent first,
 *       ZUI_SHARE_RECENT of them, each once. An app is a recent target
 *       whatever entry the content's type leads to in it.
 *   SH4 THE PERSON PICKS. Nothing is sent until zui_share_pick is called
 *       with the person's choice; the sheet never picks for them, and it
 *       never runs a target that is not on the sheet. The pick runs the
 *       target's entry through zxapp_call (so its contract is checked,
 *       zxapp.h B3) with the caller's arguments, and returns its code.
 *   SH5 Integer only, no allocation, deterministic: the same apps,
 *       recents and content give the same sheet.
 */
#ifndef ZXV_ZUI_SHARE_H
#define ZXV_ZUI_SHARE_H

#include <stdbool.h>
#include <stdint.h>

#include "zxapp.h"

#define ZUI_SHARE_APPS    64u
#define ZUI_SHARE_TARGETS 32u
#define ZUI_SHARE_RECENT  8u

typedef struct {
    zxapp_t *app;
    const zxapp_intent_t *in; /* its verb, type and entry */
    int32_t rank;             /* zxapp_mime_rank: 2 exact, 1 major, 0 anything */
    bool recent;              /* SH3: listed first because it was used lately */
} zui_share_target_t;

typedef struct {
    char verb[ZXAPP_VERB_MAX];
    char type[ZXAPP_MIME_MAX];
    zui_share_target_t t[ZUI_SHARE_TARGETS];
    uint32_t n;
    uint32_t more; /* targets that did not fit */
    int32_t sel;   /* the highlighted target, -1 none */
} zui_share_sheet_t;

typedef struct {
    zxapp_t *app[ZUI_SHARE_APPS];
    uint32_t napps;
    char recent[ZUI_SHARE_RECENT][ZXAPP_ID_MAX]; /* app ids, most recent first (SH3) */
    uint32_t nrecent;
} zui_share_t;

void zui_share_init(zui_share_t *sh);
/* SH1: false when full, NULL, or already added. */
bool zui_share_add(zui_share_t *sh, zxapp_t *app);
/* SH1: an app uninstalled; its recents go too. */
bool zui_share_remove(zui_share_t *sh, const zxapp_t *app);

/* SH2: fill `sheet` for content of `type` from app `from` (NULL or "" for
 * none). Returns how many targets are listed. */
uint32_t zui_share_open(const zui_share_t *sh, zui_share_sheet_t *sheet, const char *verb,
                        const char *type, const char *from);

/* SH4: run target i with the caller's arguments. ZXAPP_E_ARG when i is not
 * on the sheet; otherwise zxapp_call's code. On ZXAPP_OK the target becomes
 * the most recent (SH3). */
int zui_share_pick(zui_share_t *sh, const zui_share_sheet_t *sheet, uint32_t i, zxapp_host_t *h,
                   const zxapp_arg_t *args, uint32_t nargs, zxapp_num_t *out, uint32_t cap,
                   uint32_t *n_out);

/* Move the highlight (keyboard / remote): +1 next, -1 previous, wrapping. */
void zui_share_move(zui_share_sheet_t *sheet, int32_t step);

#endif /* ZXV_ZUI_SHARE_H */
