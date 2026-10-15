/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* burrow.h — the Banker's native screen (zv.h): the folder as a list, the
 * selected item's details and actions, the Trash, and AI help, over the
 * Banker model (burrow_model.h). */
#ifndef ZXV_VIEW_BURROW_H
#define ZXV_VIEW_BURROW_H

#include "burrow_model.h"
#include "zv.h"

/* Opening a file belongs to another app (Quillon for text, Glint for
 * media): the view hands its path to whoever hosts the apps. */
typedef void (*burrow_open_fn)(void *ctx, uint8_t kind, const char *space, const char *path);

typedef struct {
    zv_shell_t sh;
    burrow_t *m;
    zui_id_t list_pane, list, path, up, new_dir, new_text, trash_view, empty;
    zui_id_t none; /* W12: the empty state in place of the list */
    zui_id_t detail, back, d_name, d_kind, d_size, name_field, open, rename, trash, put_back;
    char name_buf[BURROW_NAME_MAX + 1];
    bool naming;      /* the name field is for a new folder (or a rename) */
    uint8_t name_for; /* 0 rename, 1 new folder, 2 new text file */
    bool show_detail; /* compact: the detail pane instead of the list */
    burrow_open_fn open_fn;
    void *open_ctx;
} burrow_view_t;

void burrow_view_build(burrow_view_t *v, zui_ui_t *ui, burrow_t *m);
void burrow_view_sync(burrow_view_t *v);
/* Handle a widget action; true when it was the view's. */
bool burrow_view_action(burrow_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);

#endif /* ZXV_VIEW_BURROW_H */
