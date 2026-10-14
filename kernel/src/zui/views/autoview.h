/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* autoview.h — the screen for an app written without C ("view auto" in its
 * manifest). A person builds an app from blocks and pillar or wasm logic;
 * this screen is made from the app's contracts, so nobody writes a view.
 *
 *   AV1 FROM THE CONTRACTS. The list holds every entry the app can call
 *       that has a contract (its own provides lines and the blocks it
 *       uses), in manifest order. Choosing one shows one field per input,
 *       named as the contract names it, and a Run button (the one gold
 *       element).
 *   AV2 WHOLE NUMBERS IN. A field takes whole numbers separated by spaces
 *       or commas ("3" or "1 2 3"); anything else is refused in words and
 *       nothing runs. Integers only, as everywhere in the kernel.
 *   AV3 WITNESSED OUT. Run calls the entry with zxapp_call_witnessed. The
 *       answer shows as numbers ("1/3" when not whole) or truth words; the
 *       status says how many languages agreed. When they disagree (BOTH)
 *       the answer is not shown: the screen says it is held and why.
 *   AV4 No AI here: an app built this way gets the Ask palette from the
 *       shell only when it declares actions. */
#ifndef ZXV_VIEW_AUTOVIEW_H
#define ZXV_VIEW_AUTOVIEW_H

#include "zv.h"
#include "zxapp.h"

#define AV_ENTRIES   ZXAPP_LOGIC_MAX
#define AV_FIELD_MAX 128u
#define AV_VALUES    64u /* numbers a field takes, and an answer shows */

typedef struct {
    zv_shell_t sh;
    zxapp_t *app;
    zxapp_host_t *host;
    const char *entry[AV_ENTRIES]; /* callable entries with a contract (AV1) */
    uint32_t n;
    int32_t sel;
    zui_id_t entries, name, inputs, field[ZXAPP_INPUTS], label[ZXAPP_INPUTS], run, answer;
    char buf[ZXAPP_INPUTS][AV_FIELD_MAX];
    char answer_text[ZUI_LABEL_MAX];
    int32_t lp; /* the last run's join, or -1 */
} av_view_t;

void av_view_build(av_view_t *v, zui_ui_t *ui, zxapp_t *app, zxapp_host_t *host);
void av_view_sync(av_view_t *v);
bool av_view_action(av_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);
/* AV2: parse whole numbers; returns the count, or -1 when the text is not
 * whole numbers or there are more than cap. */
int32_t av_parse_ints(const char *s, int64_t *out, uint32_t cap);

#endif /* ZXV_VIEW_AUTOVIEW_H */
