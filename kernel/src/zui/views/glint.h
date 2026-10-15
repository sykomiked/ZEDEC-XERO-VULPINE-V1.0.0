/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* glint.h — Glint's native screen (zv.h): the folder's pictures, sounds and
 * videos as a strip, the item being viewed, previous / next, and AI that
 * names an album from the file names (the swarm never sees the pictures). */
#ifndef ZXV_VIEW_GLINT_H
#define ZXV_VIEW_GLINT_H

#include "glint_model.h"
#include "zv.h"

/* Turns loaded bytes into pixels (a decoder the host has); NULL shows the
 * item's details instead of the picture. Returns false when it cannot. */
typedef bool (*glint_decode_fn)(void *ctx, const uint8_t *data, uint32_t len, zui_surface_t *dst,
                                zui_rect_t box);

typedef struct {
    zv_shell_t sh;
    glint_t *m;
    zui_id_t items, stage, prev, next, load, caption;
    zui_id_t pane, none; /* the viewer column; W12: the empty state */
    uint8_t *buf; /* the caller's buffer for loaded bytes */
    uint32_t cap, len;
    bool loaded;
    glint_decode_fn decode;
    void *decode_ctx;
} glint_view_t;

void glint_view_build(glint_view_t *v, zui_ui_t *ui, glint_t *m, uint8_t *buf, uint32_t cap);
void glint_view_sync(glint_view_t *v);
bool glint_view_action(glint_view_t *v, zui_id_t w, zui_action_t a, int32_t arg);

#endif /* ZXV_VIEW_GLINT_H */
