/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* spindle.h — Spindle's native screen (zv.h): zxvd's methods, a call with
 * JSON parameters, the replies newest first, and AI that explains a reply
 * in plain words, over the Spindle model (spindle_model.h). */
#ifndef ZXV_VIEW_SPINDLE_H
#define ZXV_VIEW_SPINDLE_H

#include "spindle_model.h"
#include "zv.h"

typedef struct {
    zv_shell_t sh;
    spindle_t *m;
    zui_id_t methods, method, params, call, history, reply;
    char method_buf[SPINDLE_METHOD_MAX + 1];
    char params_buf[SPINDLE_PARAMS_MAX + 1];
    int32_t shown; /* history entry in the reply pane, -1 none */
} spindle_view_t;

void spindle_view_build(spindle_view_t *v, zui_ui_t *ui, spindle_t *m);
void spindle_view_sync(spindle_view_t *v);
bool spindle_view_action(spindle_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);

#endif /* ZXV_VIEW_SPINDLE_H */
