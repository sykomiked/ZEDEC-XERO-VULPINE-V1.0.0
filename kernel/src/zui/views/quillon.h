/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quillon.h — Quillon's native screen (zv.h): the document as a text area
 * whose every edit goes through the Quillon model (quillon_model.h), the
 * file's place and saved state, word counts, and AI on the selection. */
#ifndef ZXV_VIEW_QUILLON_H
#define ZXV_VIEW_QUILLON_H

#include "quillon_model.h"
#include "zv.h"

typedef struct {
    zv_shell_t sh;
    quillon_t *m;
    zui_id_t where, save, save_as, as_field, text, counts;
    char as_buf[QUILLON_PATH_MAX + 1];
    bool asking_path;
} quillon_view_t;

void quillon_view_build(quillon_view_t *v, zui_ui_t *ui, quillon_t *m);
void quillon_view_sync(quillon_view_t *v);
bool quillon_view_action(quillon_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);

#endif /* ZXV_VIEW_QUILLON_H */
