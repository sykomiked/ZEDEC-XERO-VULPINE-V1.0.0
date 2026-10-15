/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* playground.h — the Playground's native screen (zv.h): a scratch pad, a
 * Run button and what the snippet printed, through an evaluator the caller
 * injects.
 *
 *   P1 THE EVALUATOR IS THE CALLER'S. Run (or Cmd-Enter in the pad) hands
 *      the pad's text to pg_eval_fn, which writes what the snippet printed
 *      into out and returns 0, or an error code with its message in out.
 *      The view runs nothing itself; with no evaluator Run is disabled and
 *      the status says so.
 *   P2 OUTPUT. Before the first run the output pane is the empty state
 *      (zui_widget.h W12). After one it is a read-only text area holding
 *      the last result, the run count and the outcome in the status line,
 *      and an error in the shell's error line (in the alert colour).
 *   P3 NOTHING IS APPLIED FOR THE PERSON. The pad changes only by their own
 *      typing: an evaluator's output never goes back into it, and Use output
 *      copies the output in only when pressed.
 *   P4 Fixed size, no allocation. */
#ifndef ZXV_VIEW_PLAYGROUND_H
#define ZXV_VIEW_PLAYGROUND_H

#include "zv.h"

#define PG_SRC_MAX 2048u
#define PG_OUT_MAX 2048u

/* P1: run src (len bytes); write up to cap - 1 bytes and a NUL to out, the
 * length to *out_len. 0 on success, else the evaluator's own error code. */
typedef int (*pg_eval_fn)(void *ctx, const char *src, uint32_t len, char *out, uint32_t cap,
                          uint32_t *out_len);

typedef struct {
    zv_shell_t sh;
    zui_id_t pad, run, clear, use, out, none, lang;
    char src[PG_SRC_MAX + 1];
    char out_buf[PG_OUT_MAX + 1];
    uint32_t out_len;
    int last_rc;
    uint32_t runs;
    pg_eval_fn eval;
    void *eval_ctx;
} playground_view_t;

/* lang names the evaluator for the status line ("" for none). */
void playground_view_build(playground_view_t *v, zui_ui_t *ui, pg_eval_fn eval, void *ctx,
                           const char *lang);
void playground_view_sync(playground_view_t *v);
bool playground_view_action(playground_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);
/* What Run does, without a press (tests, a host's own key binding). */
int playground_run(playground_view_t *v);

#endif /* ZXV_VIEW_PLAYGROUND_H */
