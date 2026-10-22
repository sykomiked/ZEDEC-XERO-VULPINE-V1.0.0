/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* graft_model.h — Graft, version control for folders in the Hoard, as a
 * model (zxm.h): commit a folder (a Keepsake snapshot, backup.run, with a
 * commit record in the folder's history file), list a file's versions
 * (backup.versions) and the commits that hold them, bring a version back
 * as a new item (backup.restore, never over what is there), see whether
 * every commit's snapshot is still kept (backup.list), diff two files and
 * merge two changed copies of a file line by line, all through files.*.
 * No drawing, no allocation; rules in graft_rules.h.
 *
 * zxvd has no commit, branch, diff or merge method, and none is invented:
 * a commit is a snapshot plus a record, and diffs and merges are worked
 * here over files.read.
 *
 *   GM1 HISTORY. A folder's history is the text file
 *       <folder>/graft-history.txt, one commit a line: commit, parent,
 *       author, stamp, snapshot, message length, message, tab-separated,
 *       oldest first. A history that does not read is not loaded
 *       (ZXM_E_PROTOCOL). One whose chain is broken (GR1) is loaded and
 *       shown, `broken` naming the first commit not linked, and no commit
 *       is added to it until the owner repairs it: Graft never rewrites a
 *       history.
 *   GM2 A COMMIT is numbered one past the last, its parent the last (0 for
 *       the first), by an author (1..999999, the household's number for
 *       the person) at a stamp the caller gives (seconds since 1970, zxvd
 *       has no clock method an app may call without sys.read), with a
 *       message of 1..GRAFT_MSG_MAX bytes and no control byte. The record
 *       must fit ^ZXGRAFT (GR5) before anything is asked of zxvd.
 *   GM3 A RESTORE brings a file or folder inside the folder back from a
 *       commit's snapshot as a new item: at `to` when given (zxvd refuses
 *       one that exists), else at its old path when free, else beside it.
 *   GM4 LINES. A line is the bytes up to and including a newline, or the
 *       bytes after the last newline. Two lines are the same when their
 *       lengths and their 64-bit FNV-1a hashes are equal. A file of more
 *       than 16 MiB or 65536 lines is refused (GR4), never cut short; a
 *       file that changes while it is read is refused (ZXM_E_STATE).
 *   GM5 A DIFF keeps the common lines at the start and the end, then the
 *       longest common subsequence of the middle when its table fits
 *       GRAFT_DP_CELLS; a longer middle is one replaced block (a correct
 *       diff, though not always the least). added and removed are GR3.
 *   GM6 A MERGE is three-way: each side is aligned with the base, and
 *       every base line with the lines a side puts before it is one unit
 *       (the last unit holds only what follows the last base line). Each
 *       unit's value is GR2. A TRUE unit takes the side that changed it
 *       (ours when both made the same change); a NEITHER unit keeps the
 *       base line; a BOTH unit keeps both sides between conflict markers,
 *       for the person to pick: Graft never picks. The merged file is
 *       written as a new file (exclusive): never over what is there.
 *   GM7 SNAPSHOTS. Each commit's snapshot is TRUE when backup.list still
 *       lists it, FALSE when it is gone (pruned), NEITHER when it is older
 *       than the snapshots listed; the history is restorable as
 *       lpres_conjoin across its commits (NEITHER with no commit). */
#ifndef GRAFT_MODEL_H
#define GRAFT_MODEL_H

#include "graft_rules.h"
#include "lpres.h"
#include "zxm.h"

#define GRAFT_PATH     1000u /* bytes in a path Graft keeps */
#define GRAFT_DEST     1024u
#define GRAFT_COMMITS  256u
#define GRAFT_VERS     100u
#define GRAFT_HIST_MAX (GRAFT_COMMITS * (6u * 21u + GRAFT_MSG_MAX + 8u))
#define GRAFT_DP_CELLS 262144u /* the LCS table of a diff's middle */
#define GRAFT_PIECE    1500u   /* bytes of text in one files.write or copy read */
#define GRAFT_NOTICE   200u
#define GRAFT_HISTORY  "graft-history.txt"

typedef struct {
    int64_t commit, parent, author, stamp;
    uint32_t snapshot, msglen;
    char msg[GRAFT_MSG_MAX + 1];
} graft_commit_t;

typedef struct {
    uint32_t snapshot; /* the newest snapshot holding this content */
    int64_t time;
    uint64_t size;
    int64_t mtime;
    char hash[65];
    int64_t commit; /* the commit made by that snapshot, 0 for none */
} graft_version_t;

typedef struct {
    uint64_t hash;     /* FNV-1a of the line's bytes, newline aside */
    uint32_t off, end; /* its bytes in the file, newline included */
    uint32_t len;      /* without the newline */
} graft_line_t;

typedef struct {
    uint64_t bytes;
    uint32_t n;
    graft_line_t line[GRAFT_LINES_MAX];
} graft_text_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1];
    char dest[GRAFT_DEST + 1];
    char folder[GRAFT_PATH + 1];
    bool opened;
    graft_commit_t c[GRAFT_COMMITS]; /* oldest first (GM1) */
    uint32_t n;
    uint32_t broken; /* the first commit not linked; n when intact */
    graft_version_t ver[GRAFT_VERS];
    uint32_t nvers;
    char vpath[GRAFT_PATH + 1];
    char landed[GRAFT_PATH + 1];
    uint32_t restored;
    /* the last diff (GM5) */
    int64_t added, removed, kept;
    /* the last merge (GM6) */
    lpres_state_t merged;
    uint32_t units, untouched, ours_only, theirs_only, same, clashes;
    char notice[GRAFT_NOTICE + 1];
    /* working space, kept here so nothing is allocated */
    graft_text_t text[3];
    int32_t match[2][GRAFT_LINES_MAX]; /* base line -> side line, -1 none */
    int32_t back[2][GRAFT_LINES_MAX];  /* side line -> base line, -1 none */
    uint32_t from[2][GRAFT_LINES_MAX + 1], to[2][GRAFT_LINES_MAX + 1];
    lpres_state_t state[GRAFT_LINES_MAX + 1];
    uint16_t dp[GRAFT_DP_CELLS];
    char io[48u * 1024u + 1u];
    char hist[GRAFT_HIST_MAX + 1];
    char out[GRAFT_PIECE + 64u];
    uint32_t out_len;
    bool out_begun, out_nl;
} graft_t;

void graft_init(graft_t *g, zxm_link_t *link);
int graft_set_space(graft_t *g, const char *space); /* closes the folder */
int graft_set_dest(graft_t *g, const char *dest);   /* an absolute path */

/* GM1: open a folder of the Hoard and read its history. */
int graft_open(graft_t *g, const char *folder);
/* GM2: commit the folder; *id is the new commit's number. */
int graft_commit(graft_t *g, int64_t author, int64_t stamp, const char *message, int64_t *id);
/* The commit numbered `commit`, or NULL. */
const graft_commit_t *graft_find(const graft_t *g, int64_t commit);
/* A file's versions across the snapshots, each with its commit. */
int graft_versions(graft_t *g, const char *path);
/* GM3: to NULL or "": its old path when free, else beside it. */
int graft_restore(graft_t *g, int64_t commit, const char *path, const char *to);
/* GM7: is every commit's snapshot still kept? */
int graft_snapshots(graft_t *g, lpres_state_t *out);

/* GM5: lines added and removed from older to newer. */
int graft_diff(graft_t *g, const char *older, const char *newer);
/* GM6: merge ours and theirs, both changed from base, into a new file. */
int graft_merge(graft_t *g, const char *base, const char *ours, const char *theirs,
                const char *out);

#endif
