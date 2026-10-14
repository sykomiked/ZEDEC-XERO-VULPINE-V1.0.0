/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zv.h — what every built-in app's native screen shares: the shell around
 * the app's own panes.
 *
 * A view (burrow.c, quillon.c, glint.c, spindle.c, murmuration.c) draws its
 * app model (kernel/src/apps/<app>_model.h) with the zui widget set. It
 * builds its tree once, copies the model into the widgets after every model
 * call (<app>_view_sync), and turns widget actions into model calls
 * (<app>_view_action). It never talks to zxvd itself and never applies AI
 * output: the model does both (zxm.h M1, M3).
 *
 *   V1 THE SHELL. A title strip (the app's name, a status line at the
 *      right), the body (the app's panes, a row), the held AI answer, a
 *      line for the last refusal in zxvd's own words (zxm.h M2), and the
 *      ZXV progress bar along the bottom.
 *   V2 THE PALETTE. The app's AI table (zxm M5) is the Ask palette's list;
 *      a pick reaches the view as ZUI_ACT_PICK on the shell's root.
 *   V3 SIZE CLASSES. Compact screens show one pane at a time with a Back
 *      button; wider ones show the panes side by side (zui_layout.h).
 *   V4 TOKENS ONLY. Colours come from zui_tokens.h; one gold element per
 *      screen (the primary action), green everywhere else.
 */
#ifndef ZXV_ZV_H
#define ZXV_ZV_H

#include "zui_widget.h"
#include "zxm.h"

typedef struct {
    zui_ui_t *ui;
    zui_id_t root, top, title, status, body, held, error, bar;
    zui_palette_item_t pal[ZUI_PALETTE_MAX];
    uint32_t npal;
    const zxm_ai_action_t *ai; /* the app's table */
    char status_text[ZUI_LABEL_MAX];
    char error_text[ZUI_LABEL_MAX];
} zv_shell_t;

void zv_shell_build(zv_shell_t *s, zui_ui_t *ui, const char *title);
void zv_shell_palette(zv_shell_t *s, const zxm_ai_action_t *table, uint32_t n);
void zv_shell_status(zv_shell_t *s, const char *text);
/* M2: rc from a model call. ZXM_OK clears the line; ZXM_E_REMOTE shows
 * zxvd's message as is; the rest say what went wrong locally. */
void zv_shell_result(zv_shell_t *s, int rc, const zxm_link_t *link);
/* M3/W4: show a model's held answer (hidden when there is none). */
void zv_shell_held(zv_shell_t *s, const zxm_held_t *h);
bool zv_compact(const zv_shell_t *s);

/* Small formatting helpers for rows and details (no libc). */
uint32_t zv_u64(char *out, uint32_t cap, uint64_t v);                   /* "12345" */
uint32_t zv_i64(char *out, uint32_t cap, int64_t v);                    /* "-12" */
uint32_t zv_bytes(char *out, uint32_t cap, uint64_t v);                 /* "12 KB", "3.4 MB" */
uint32_t zv_minor(char *out, uint32_t cap, int64_t v, uint32_t places); /* "-12.34" */
uint32_t zv_cat(char *out, uint32_t cap, const char *a, const char *b); /* a then b */

#endif /* ZXV_ZV_H */
