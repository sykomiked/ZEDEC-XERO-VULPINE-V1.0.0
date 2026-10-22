/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* margins_rules.h — Margins' rules in C, the reference its Eve, Sutra,
 * KRON, Trebago and MYRA blocks are checked against (blocks/margins), with
 * the rules it uses from nearer the core. Pure integer functions, no
 * state: the model (margins_model.c) and the language test
 * (tests/margins_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   MR1 SIZE LIMITS (note_size_ok). A note's title, its file name without
 *       ".txt", is 1..120 bytes; its body 0..8000 bytes, so that one
 *       files.write request carries it whole within zxvd's 16384-byte
 *       request line (zxm.h ZXM_REQ_MAX) even with every byte escaped to
 *       two; and it carries 0..16 tags. Each is held to its own range.
 *   MR2 ORDER KEY (pinned_recent_key): pinned * MARGINS_PIN_STEP + stamp,
 *       pinned 0..1 and stamp (seconds since 1970) 0..MARGINS_STAMP_MAX,
 *       else refused. MARGINS_PIN_STEP is one past the last stamp, so
 *       every pinned key is above every unpinned one.
 *   MR3 PLACE: a note's place in the list is assay.score_rank of its key
 *       among all the keys (assay_rules.h AY3): 1 + the keys strictly
 *       greater, so pinned notes come first, each group newest first, and
 *       notes of one key share a place.
 *   MR4 TAG INDEX RECORD (note_tag_row_ok) fits ^ZXMARGINS: tag (its
 *       number in the tag table) at most 4 digits, note 4 and stamp 12,
 *       sign aside.
 *   MR5 COUNTS: the notes of a tag, and the pinned notes, are
 *       witness.kind_count (witness_rules.h WR2) over the tag index's tag
 *       column and over the pin flags.
 *   MR6 THE TRASH: a note put in the Trash at `stamp` is offered for
 *       removal after 30 days; its days left are Lockbox's days_left of
 *       stamp + 30 days at now (lockbox_rules.h LR2). Advice only.
 *   MR7 FULLNESS: how much of MR1's 8000 bytes a body takes, in
 *       hundredths of a percent, is used_share.used_x100 with total 8000
 *       and left 8000 - bytes: floor(10000 * bytes / 8000). */
#ifndef MARGINS_RULES_H
#define MARGINS_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "assay_rules.h"
#include "lockbox_rules.h"
#include "witness_rules.h"

#define MARGINS_REFUSED    (-1)
#define MARGINS_TITLE_MIN  1
#define MARGINS_TITLE_MAX  120                   /* MR1: bytes */
#define MARGINS_BODY_MAX   8000                  /* MR1: bytes */
#define MARGINS_TAGS_MAX   16                    /* MR1: tags on one note */
#define MARGINS_STAMP_MAX  INT64_C(253402300799) /* MR2: the last second of year 9999 */
#define MARGINS_PIN_STEP   (MARGINS_STAMP_MAX + 1)
#define MARGINS_INDEX_MAX  INT64_C(9999)         /* MR4: 4 digits */
#define MARGINS_ISTAMP_MAX INT64_C(999999999999) /* MR4: 12 digits */
#define MARGINS_TRASH_DAYS 30                    /* MR6 */

/* MR1 */
static inline bool margins_size_ok(int64_t title, int64_t body, int64_t tags)
{
    return title >= MARGINS_TITLE_MIN && title <= MARGINS_TITLE_MAX && body >= 0 &&
           body <= MARGINS_BODY_MAX && tags >= 0 && tags <= MARGINS_TAGS_MAX;
}

/* MR2 */
static inline int64_t margins_key(int64_t pinned, int64_t stamp)
{
    if (pinned < 0 || pinned > 1 || stamp < 0 || stamp > MARGINS_STAMP_MAX) return MARGINS_REFUSED;
    return pinned * MARGINS_PIN_STEP + stamp;
}

/* MR4 */
static inline bool margins_tag_record_ok(int64_t tag, int64_t note, int64_t stamp)
{
    return tag >= -MARGINS_INDEX_MAX && tag <= MARGINS_INDEX_MAX && note >= -MARGINS_INDEX_MAX &&
           note <= MARGINS_INDEX_MAX && stamp >= -MARGINS_ISTAMP_MAX && stamp <= MARGINS_ISTAMP_MAX;
}

/* MR6: LOCKBOX_REFUSED when stamp + 30 days or now is past Lockbox's stamps */
static inline int64_t margins_trash_days(int64_t stamp, int64_t now)
{
    const int64_t keep = (int64_t) MARGINS_TRASH_DAYS * LOCKBOX_DAY;
    if (stamp < 0 || stamp > LOCKBOX_STAMP_MAX - keep) return LOCKBOX_REFUSED;
    return lockbox_days_left(stamp + keep, now);
}

/* MR7: used_share.used_x100 of MR1's body, as the model works it out;
 * MARGINS_REFUSED for a body outside 0..8000 bytes. */
static inline int64_t margins_fill_x100(int64_t bytes)
{
    if (bytes < 0 || bytes > MARGINS_BODY_MAX) return MARGINS_REFUSED;
    return bytes * 10000 / MARGINS_BODY_MAX;
}

#endif
