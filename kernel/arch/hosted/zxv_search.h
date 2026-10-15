/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_search.h — Scent's index of the ZXV folder, behind zxvd's search.*
 * methods (docs/SYSTEM_UTILITIES.md).
 *
 *   S1 WHAT BURROW SEES. The index walks the space in use through zxv_files
 *      (F1-F3), so it holds exactly the files the Banker lists: hidden names,
 *      the Trash and links are left out. Each space has its own index.
 *   S2 INCREMENTAL. A refresh re-reads only the files whose size or time
 *      changed since the last one, and drops the files that are gone.
 *   S3 WORDS. A word is a run of ASCII letters and digits (folded to lower
 *      case) and non-ASCII UTF-8 bytes, 2 to ZSR_WORD_MAX bytes. File names
 *      are matched as text; contents are indexed for files that read as
 *      UTF-8 with no NUL byte, up to ZSR_TEXT_MAX bytes of each.
 *   S4 AS YOU TYPE. Every prefix of a word from 2 bytes up to
 *      ZSR_PREFIX_MAX is indexed too, so "harv" finds "harvest".
 *   S5 ALL TERMS. A hit has every term of the query, each in its name or its
 *      text. Name matches rank first, then how often the terms occur.
 *   S6 BOUNDED. At most ZSR_FILES_MAX files per space (the rest are counted
 *      as left out) and ZSR_TERMS_MAX terms per query.
 *   S7 READ ONLY. The index never changes a file. */
#ifndef ZXV_SEARCH_H
#define ZXV_SEARCH_H

#include <stdbool.h>
#include <stdint.h>

#include "zxv_files.h"

#define ZSR_FILES_MAX  20000u
#define ZSR_TEXT_MAX   (1024u * 1024u)
#define ZSR_WORD_MAX   64u
#define ZSR_PREFIX_MAX 24u
#define ZSR_TERMS_MAX  8u
#define ZSR_QUERY_MAX  256u /* bytes in a query */
#define ZSR_SNIPPET    120u /* bytes of text around the first hit */
#define ZSR_HITS_MAX   100u /* hits in one answer */

typedef struct {
    uint32_t files;      /* in the index */
    uint32_t text_files; /* of those, with indexed contents */
    uint32_t added, changed, removed;
    uint32_t left_out; /* over ZSR_FILES_MAX, or too deep */
} zsr_stats_t;

typedef struct {
    char path[ZXF_PATH_MAX + 1];
    uint64_t size;
    int64_t mtime;
    bool in_name;   /* every term is in the name */
    uint32_t score; /* higher first */
    char snippet[ZSR_SNIPPET + 1];
} zsr_hit_t;

/* Bring the index of the space in use (zxf_use_space) up to date.
 * 0 or a ZXF_E_* code. */
int zsr_refresh(zsr_stats_t *st);

/* The best `cap` (at most ZSR_HITS_MAX) hits for `q` in the space in use, as of its last refresh;
 * *n returned, *total matched. ZXF_E_PATH for an empty query, one with
 * more than ZSR_TERMS_MAX terms, or one over ZSR_QUERY_MAX bytes. */
int zsr_query(const char *q, zsr_hit_t *hits, uint32_t cap, uint32_t *n, uint32_t *total);

/* Forget every index (tests). */
void zsr_reset(void);

#endif
