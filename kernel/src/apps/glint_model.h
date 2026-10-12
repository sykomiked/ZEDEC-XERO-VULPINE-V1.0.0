/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* glint_model.h — Glint, photos and media, as a model (zxm.h): the images,
 * audio and video in one folder of the ZXV folder, the one being viewed,
 * and its bytes for the view's decoder. No drawing, no allocation: the view
 * gives the buffer the bytes go into. */
#ifndef GLINT_MODEL_H
#define GLINT_MODEL_H

#include "burrow_model.h" /* the folder listing and kinds */

#define GLINT_MAX 1024u

typedef struct {
    char name[BURROW_NAME_MAX + 1];
    uint8_t kind; /* BURROW_K_IMAGE, _AUDIO or _VIDEO */
    uint64_t size;
    int64_t mtime;
} glint_item_t;

typedef struct {
    burrow_t *folder; /* the listing comes from a Burrow model */
    glint_item_t item[GLINT_MAX];
    uint32_t n;
    int32_t cur;     /* the item being viewed, or -1 */
    zxm_held_t held; /* the last AI answer (M3) */
} glint_t;

enum { GLINT_AI_NAME_ALBUM, GLINT_AI_COUNT };
extern const zxm_ai_action_t GLINT_AI[GLINT_AI_COUNT];

/* The MIME type for an item's name ("application/octet-stream" if unknown). */
const char *glint_mime(const char *name);

/* Take the media out of the folder `folder` has open (call after any
 * burrow_open/refresh). */
void glint_scan(glint_t *g, burrow_t *folder);
int glint_view(glint_t *g, int32_t i); /* -1 closes the viewer */
int glint_next(glint_t *g);
int glint_prev(glint_t *g);
/* The current item's bytes into buf (cap bytes). *len = the bytes written;
 * ZXM_E_TOOBIG when the file is larger than cap (nothing partial is kept). */
int glint_load(glint_t *g, uint8_t *buf, uint32_t cap, uint32_t *len);

/* AI from the file names only: the swarm does not see the pictures. */
int glint_ai(glint_t *g, int32_t action);
int glint_ai_accept(glint_t *g);
int glint_ai_reject(glint_t *g);

#endif
