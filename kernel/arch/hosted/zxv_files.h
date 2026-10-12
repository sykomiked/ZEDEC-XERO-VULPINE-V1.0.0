/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_files.h — the ZXV home folder behind the Burrow (files), Quillon (text)
 * and Glint (photos) apps (zxvd's files.* methods, docs/STAPLE_APPS.md).
 *
 *   F1 ONE FOLDER. Every path is relative to one root folder; nothing outside
 *      it can be named. The root is ZXV_FILES_ROOT when set (absolute), else
 *        macOS, Linux  ~/Documents/ZXV  (~/ZXV when there is no ~/Documents)
 *        Windows       %USERPROFILE%\Documents\ZXV
 *      created 0700 when missing.
 *   F2 NAMES. A path is segments joined by '/'. A segment is 1..255 bytes of
 *      valid UTF-8 with no '/', '\', ':', control character, leading '.',
 *      or trailing '.' or space; "." and ".." never pass. The one exception
 *      is ".Trash" as the first segment (the Trash, F5). "" is the root.
 *   F3 NO LINKS. A symbolic link (a reparse point on Windows) anywhere below
 *      the root refuses the path, so a link cannot lead outside it.
 *   F4 WRITES ARE WHOLE. "replace" writes a staging file, flushes it and
 *      renames it over the target: a reader sees the old or the new file,
 *      never half. A file too long for one request goes "begin", "add"...,
 *      "end": the parts gather in the staging file and "end" renames it.
 *   F5 TRASH, NOT DELETE. zxf_trash moves an item into <root>/.Trash (a
 *      unique name, " 2", " 3"... on a clash). Only an item already in the
 *      Trash is removed for good.
 *   F6 SIZES. A read returns at most ZXF_READ_MAX bytes; text reads stop on
 *      a UTF-8 character boundary and refuse invalid UTF-8 (ZXF_E_BINARY).
 *      A file is at most ZXF_FILE_MAX bytes.
 *   F7 SPACES. Besides home (the root itself), each space (zxv_spaces.h) is
 *      a folder <root>/.spaces/<id> that F1-F6 apply to on their own: paths
 *      are relative to the space in use (zxf_use_space), and it has its own
 *      Trash and staging. Home paths cannot reach .spaces (F2 refuses a
 *      leading '.'). A space's manifest is <root>/.spaces/<id>.space, out of
 *      reach of every path, read and written only through zxf_meta_*. */
#ifndef ZXV_FILES_H
#define ZXV_FILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZXF_PATH_MAX     1024u         /* bytes in a relative path */
#define ZXF_NAME_MAX     255u          /* bytes in one segment */
#define ZXF_READ_MAX     (48u * 1024u) /* bytes in one read */
#define ZXF_FILE_MAX     (256u * 1024u * 1024u)
#define ZXF_LIST_MAX     10000u /* entries in one listing */
#define ZXF_TRASH        ".Trash"
#define ZXF_SPACES       ".spaces"
#define ZXF_SPACE_ID_MAX 48u /* [a-z0-9-], not "home" */

enum {
    ZXF_OK = 0,
    ZXF_E_PATH = -1, /* not a valid path (F2), or a link on it (F3) */
    ZXF_E_NOTFOUND = -2,
    ZXF_E_EXISTS = -3,
    ZXF_E_NOTDIR = -4,
    ZXF_E_ISDIR = -5,
    ZXF_E_IO = -6,
    ZXF_E_TOOBIG = -7,
    ZXF_E_ROOT = -8,   /* no root folder (no HOME, or it cannot be made) */
    ZXF_E_BINARY = -9, /* a text read of bytes that are not UTF-8 */
    ZXF_E_STAGE = -10, /* "add" or "end" with no "begin" for this path */
    ZXF_E_DEPTH = -11, /* a tree too deep to remove */
};

enum { ZXF_W_REPLACE, ZXF_W_BEGIN, ZXF_W_ADD, ZXF_W_END };

typedef struct {
    char name[ZXF_NAME_MAX + 1];
    bool dir;
    uint64_t size; /* bytes; 0 for a folder */
    int64_t mtime; /* seconds since 1970 */
} zxf_entry_t;

/* The root (F1): set it (tests, or a --files flag), or read it, creating it
 * on first use. 0 or a ZXF_E_* code. */
int zxf_set_root(const char *abs);
int zxf_root(char *out, size_t cap);

/* Is `rel` a valid path (F2)? */
bool zxf_valid(const char *rel);

/* One folder's entries, folders first, then by name; hidden names (leading
 * '.') are left out. *out is malloc'd (free it); *n entries. */
int zxf_list(const char *rel, zxf_entry_t **out, uint32_t *n);
int zxf_stat(const char *rel, zxf_entry_t *e);

/* Up to `cap` bytes (at most ZXF_READ_MAX) from `off`; *size is the file's
 * size. text: stop at a character boundary and refuse non-UTF-8 (F6). */
int zxf_read(const char *rel, uint64_t off, uint8_t *buf, uint32_t cap, bool text, uint32_t *got,
             uint64_t *size);
/* mode ZXF_W_*; exclusive: refuse when the target exists (a new file).
 * *size: the file's size after the write ("replace"/"end") or the staged
 * size so far ("begin"/"add"). */
int zxf_write(const char *rel, const uint8_t *d, uint32_t n, int mode, bool exclusive,
              uint64_t *size);
int zxf_mkdir(const char *rel);
/* Refuses when `to` exists; `to`'s folder must exist. */
int zxf_rename(const char *from, const char *to);
/* F5: into the Trash (its new path in `trashed`), or, for an item already in
 * the Trash, removed for good (trashed = ""). */
int zxf_trash(const char *rel, char *trashed, size_t cap);
/* Remove everything in the Trash. *removed: top-level items removed. */
int zxf_empty_trash(uint32_t *removed);

/* F7: the space later calls work in; NULL, "" or "home" = home. A bad id
 * is refused (ZXF_E_PATH) and the space in use stays. */
int zxf_use_space(const char *id);
const char *zxf_space(void); /* "home" or the id */
bool zxf_space_id_ok(const char *id);
/* Manifests: read (NUL-terminated, < cap bytes), write whole (exclusive:
 * refuse when it exists), and call fn for each space id that has one. */
int zxf_meta_read(const char *id, char *buf, uint32_t cap, uint32_t *n);
int zxf_meta_write(const char *id, const char *buf, uint32_t n, bool exclusive);
typedef int (*zxf_meta_fn)(void *ctx, const char *id);
int zxf_meta_each(zxf_meta_fn fn, void *ctx);

const char *zxf_strerror(int code);

#endif
