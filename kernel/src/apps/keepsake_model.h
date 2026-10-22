/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* keepsake_model.h — Keepsake, backups with versions, as a model (zxm.h):
 * back a space up to a folder on another disk, list the backups, see the
 * versions a file has had, check that a backup reads back whole, bring a
 * file or folder back as a new item, and let old backups go (zxvd's
 * backup.*, kernel/arch/hosted/zxv_keepsake.h). AI can explain a failed
 * check in plain words, held until read (M3). No drawing, no allocation. */
#ifndef KEEPSAKE_MODEL_H
#define KEEPSAKE_MODEL_H

#include "zxm.h"

#define KEEPSAKE_DEST  1024u
#define KEEPSAKE_SNAPS 50u
#define KEEPSAKE_VERS  50u
#define KEEPSAKE_PATH  1024u

typedef struct {
    uint32_t id;
    int64_t time;
    uint32_t files;
    uint64_t bytes;
    bool damaged;
} keepsake_snap_t;

typedef struct {
    uint32_t snapshot;
    int64_t time;
    uint64_t size;
    int64_t mtime;
    char hash[65];
} keepsake_version_t;

typedef struct {
    bool done; /* a check has run since the last backup */
    uint32_t snapshot, files, bad, missing;
    uint64_t bytes;
    char first_bad[KEEPSAKE_PATH + 1];
} keepsake_check_t;

typedef struct {
    uint32_t snapshot;
    int64_t time;
    uint32_t files, new_files, skipped;
    uint64_t bytes, new_bytes;
} keepsake_run_t;

typedef struct {
    zxm_link_t *link;
    char space[49];
    char dest[KEEPSAKE_DEST + 1];         /* "" until the owner picks one */
    keepsake_run_t last;                  /* the last backup made here */
    keepsake_snap_t snap[KEEPSAKE_SNAPS]; /* newest first */
    uint32_t nsnaps, total;
    char vpath[KEEPSAKE_PATH + 1]; /* whose versions are listed */
    keepsake_version_t ver[KEEPSAKE_VERS];
    uint32_t nvers;
    keepsake_check_t check;
    char landed[KEEPSAKE_PATH + 1]; /* where the last restore put things */
    uint32_t restored;
    uint32_t pruned_snaps, pruned_objects;
    uint64_t freed;
    zxm_held_t held;
} keepsake_t;

enum { KEEPSAKE_AI_EXPLAIN, KEEPSAKE_AI_COUNT };
extern const zxm_ai_action_t KEEPSAKE_AI[KEEPSAKE_AI_COUNT];

void keepsake_init(keepsake_t *k, zxm_link_t *link);
int keepsake_set_space(keepsake_t *k, const char *space); /* clears what was listed */
int keepsake_set_dest(keepsake_t *k, const char *dest);   /* an absolute path */

/* Back up now (full: read every file again and repair kept copies), then
 * list the backups. Backup, restore and prune change what is kept, so each
 * sends only on the owner's word (owner_confirmed); without it ZXM_E_STATE
 * and nothing is sent. */
int keepsake_backup(keepsake_t *k, bool full, bool owner_confirmed);
int keepsake_refresh(keepsake_t *k);
int keepsake_versions(keepsake_t *k, const char *path);
/* snapshot 0: the newest */
int keepsake_check(keepsake_t *k, uint32_t snapshot);
/* Whether the newest backup has been checked and read back whole. */
bool keepsake_tested(const keepsake_t *k);
/* to: NULL or "" puts it back where it was, or beside it when that is taken. */
int keepsake_restore(keepsake_t *k, uint32_t snapshot, const char *path, const char *to,
                     bool owner_confirmed);
/* Keep the newest `keep` backups of this space; the owner confirms first. */
int keepsake_prune(keepsake_t *k, uint32_t keep, bool owner_confirmed);

/* AI: what a failed check means and what to do (held, M3). */
int keepsake_ai(keepsake_t *k, int32_t action);
int keepsake_ai_accept(keepsake_t *k);
int keepsake_ai_reject(keepsake_t *k);

#endif
