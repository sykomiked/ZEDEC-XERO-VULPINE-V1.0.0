/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_keepsake.h — Keepsake, backups with versions of a space's files, behind
 * zxvd's backup.* methods.
 *
 *   K1 DESTINATION. A backup goes to a folder the owner names by its
 *      absolute path, usually on another disk. It must exist, must not be a
 *      link, and must not be inside the ZXV folder. Everything Keepsake
 *      writes is under "<dest>/ZXV Keepsake"; nothing else there is touched.
 *   K2 CONTENT ADDRESSED. Each file's bytes are kept once, under
 *      objects/<2 hex>/<SHA-256 hex>, written to a temporary name, flushed,
 *      then renamed. A file whose size and time match the last backup and
 *      whose object is there is not read again. Unchanged files cost nothing,
 *      which is what makes keeping many versions cheap.
 *   K3 SNAPSHOTS. Each backup of a space is spaces/<space>/<N>.snap, N from
 *      1: a text list of every file (hash, size, time, path) ending in a line
 *      that carries the SHA-256 of all the lines before it. It is written
 *      whole (temporary name, flush, rename); a snapshot without a good last
 *      line is damaged and is never trusted.
 *   K4 TESTED. Each new object is read back and its hash checked before the
 *      snapshot that names it is written. zks_verify reads every object a
 *      snapshot names and checks it: a backup is tested when that passes.
 *   K5 RESTORE NEVER OVERWRITES. A file or folder comes back as a new item:
 *      at `to` when given (it must not exist), else at its old path when that
 *      is free, else beside it as "name (restored).ext", "(restored 2)"...
 *      Each object is checked before it is written.
 *   K6 SPACES. The space in use (zxv_files.h F7) is the one backed up or
 *      restored into; hidden names and the Trash are never backed up.
 *   K7 PRUNE. Only the owner's call removes anything: the oldest snapshots
 *      of the space beyond `keep`, then objects no snapshot of any space
 *      names. A damaged snapshot stops the object sweep, so nothing it might
 *      name is lost.
 *
 * Not yet: encryption (it waits for Lockbox's keys), and copying a store to
 * a peer or IPFS. A backup runs within one call. */
#ifndef ZXV_KEEPSAKE_H
#define ZXV_KEEPSAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zxv_files.h"

#define ZKS_DIR       "ZXV Keepsake"
#define ZKS_DEST_MAX  1024u   /* bytes in a destination path */
#define ZKS_FILES_MAX 100000u /* files in one snapshot */
#define ZKS_SNAPS_MAX 100000u /* snapshot numbers per space */
#define ZKS_ERR       200u

enum {
    ZKS_OK = 0,
    ZKS_E_DEST = -1,     /* not a usable destination (K1) */
    ZKS_E_IO = -2,       /* the destination could not be read or written */
    ZKS_E_NOTFOUND = -3, /* no such snapshot, or no such path in it */
    ZKS_E_DAMAGED = -4,  /* a snapshot or an object fails its check (K3, K4) */
    ZKS_E_FILE = -5,     /* the ZXV folder refused; *zxf holds its code */
    ZKS_E_FULL = -6,     /* more than ZKS_FILES_MAX files */
};

typedef struct {
    uint32_t snapshot;
    int64_t time;
    uint32_t files, new_files, skipped; /* skipped: gone or unreadable mid-backup */
    uint64_t bytes, new_bytes;
} zks_run_t;

typedef struct {
    uint32_t id;
    int64_t time;
    uint32_t files;
    uint64_t bytes;
    bool damaged;
} zks_snap_t;

typedef struct {
    uint32_t snapshot; /* the newest snapshot holding this content */
    int64_t time;
    uint64_t size;
    int64_t mtime;
    char hash[65];
} zks_version_t;

typedef struct {
    uint32_t snapshot, files, bad, missing;
    uint64_t bytes;
    char first_bad[ZXF_PATH_MAX + 1]; /* "" when all are good */
} zks_verify_t;

/* Back up the space in use. full: read every file again, even unchanged
 * ones, and replace any kept copy that no longer matches (the repair after a
 * failed check). A ZXV-folder refusal is ZKS_E_FILE with its code in *zxf;
 * err gets a plain reason. */
int zks_run(const char *dest, bool full, zks_run_t *r, int *zxf, char *err, size_t cap);
/* Snapshots of the space in use, newest first; *total of them in all. */
int zks_list(const char *dest, zks_snap_t *out, uint32_t cap, uint32_t *n, uint32_t *total,
             char *err, size_t errcap);
/* The distinct contents `path` has had, newest first. */
int zks_versions(const char *dest, const char *path, zks_version_t *out, uint32_t cap, uint32_t *n,
                 char *err, size_t errcap);
/* snapshot 0: the newest. */
int zks_verify(const char *dest, uint32_t snapshot, zks_verify_t *v, char *err, size_t cap);
/* Restore `path` ("" = everything) from a snapshot (0: the newest); where it
 * landed in `landed`, and how many files. */
int zks_restore(const char *dest, uint32_t snapshot, const char *path, const char *to,
                char landed[ZXF_PATH_MAX + 1], uint32_t *files, int *zxf, char *err, size_t cap);
/* K7: keep the newest `keep` (>= 1) snapshots of the space in use. */
int zks_prune(const char *dest, uint32_t keep, uint32_t *snaps, uint32_t *objects, uint64_t *freed,
              char *err, size_t cap);

#endif
