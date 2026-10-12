/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* burrow_model.h — Burrow, the file manager, as a model (zxm.h): one folder
 * of the ZXV folder at a time (zxvd's files.*, zxv_files.h F1-F6), the
 * selection, the Trash, and its AI actions. No drawing, no allocation. */
#ifndef BURROW_MODEL_H
#define BURROW_MODEL_H

#include "zxm.h"

#define BURROW_MAX      1024u /* entries kept for one folder */
#define BURROW_PATH_MAX 1024u
#define BURROW_NAME_MAX 255u

enum {
    BURROW_K_DIR,
    BURROW_K_TEXT,
    BURROW_K_IMAGE,
    BURROW_K_AUDIO,
    BURROW_K_VIDEO,
    BURROW_K_OTHER
};

typedef struct {
    char name[BURROW_NAME_MAX + 1];
    bool dir;
    uint8_t kind; /* BURROW_K_* */
    uint64_t size;
    int64_t mtime;
} burrow_entry_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1]; /* the space this folder is in; "" = home */
    char root[1024];               /* where the ZXV folder is on this machine */
    char dir[BURROW_PATH_MAX + 1]; /* "" = the ZXV folder, ".Trash..." = the Trash */
    burrow_entry_t e[BURROW_MAX];
    uint32_t n;
    bool truncated;  /* the folder holds more than BURROW_MAX */
    int32_t sel;     /* selected entry, or -1 */
    zxm_held_t held; /* the last AI answer (M3) */
} burrow_t;

/* The palette (M5). */
enum { BURROW_AI_DESCRIBE, BURROW_AI_SUGGEST_FOLDERS, BURROW_AI_COUNT };
extern const zxm_ai_action_t BURROW_AI[BURROW_AI_COUNT];

/* The kind of a file from its name (folders are BURROW_K_DIR). */
uint8_t burrow_kind(const char *name);

int burrow_init(burrow_t *b, zxm_link_t *link); /* reads the root, opens home's "" */
/* Go to another space's top folder (spaces.list names them; "" = home).
 * On failure the model stays where it was. */
int burrow_set_space(burrow_t *b, const char *space);
int burrow_open(burrow_t *b, const char *dir); /* a folder, or ".Trash" */
int burrow_refresh(burrow_t *b);
int burrow_up(burrow_t *b);                /* the enclosing folder */
int burrow_enter(burrow_t *b, uint32_t i); /* a folder entry */
int burrow_select(burrow_t *b, int32_t i); /* -1 clears */
bool burrow_in_trash(const burrow_t *b);
/* The path of entry i in the ZXV folder (for Quillon or Glint). */
int burrow_path(const burrow_t *b, uint32_t i, char *out, uint32_t cap);

int burrow_mkdir(burrow_t *b, const char *name);
int burrow_new_text(burrow_t *b, const char *name); /* an empty file; never overwrites */
int burrow_rename(burrow_t *b, uint32_t i, const char *name);
int burrow_trash(burrow_t *b, uint32_t i);                    /* in the Trash: removes for good */
int burrow_put_back(burrow_t *b, uint32_t i, const char *to); /* from the Trash */
int burrow_empty_trash(burrow_t *b, uint32_t *removed);
/* Copy entry i into another space's folder path `to` (files.cross). When
 * that would show it to more people, zxvd holds it: *ticket is then its
 * number for burrow_cross_confirm, which only the owner's yes should call;
 * 0 when it was copied at once. */
int burrow_cross(burrow_t *b, uint32_t i, const char *to_space, const char *to, uint32_t *ticket);
int burrow_cross_confirm(burrow_t *b, uint32_t ticket);
/* Bring bytes in (an import), split into as many requests as they need. */
int burrow_import(burrow_t *b, const char *name, const uint8_t *data, uint32_t len);

/* AI: ask about this folder's names; the answer is held (M3). Accepting a
 * BURROW_AI_SUGGEST_FOLDERS answer only marks it accepted: the owner makes
 * the folders, the model never moves files on the swarm's word. */
int burrow_ai(burrow_t *b, int32_t action);
int burrow_ai_accept(burrow_t *b);
int burrow_ai_reject(burrow_t *b);

#endif
