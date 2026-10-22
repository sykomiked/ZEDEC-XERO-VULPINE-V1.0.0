/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* graft_rules.h — Graft's rules in C, the reference its Eve, KRON, Trebago
 * and MYRA blocks are checked against (blocks/graft). Pure integer
 * functions, no state: the model (graft_model.c) and the language test
 * (tests/graft_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   GR1 HISTORY. A commit is linked to the one before it when its parent
 *       is that commit's number and its own number is past it: Witness's
 *       chain link (witness_rules.h WR1, witness.chain_ok), used, never
 *       written again. The first commit's parent is 0.
 *   GR2 A MERGED LINE has four values. Each side is a source about the
 *       line: a side that did not change it says nothing (NEITHER), a side
 *       that changed it says TRUE, and theirs says FALSE when ours changed
 *       it too and the two changes are apart. The two are joined with the
 *       knowledge join (lpres_kjoin): one side only TRUE, the same change
 *       TRUE, changes apart BOTH (both kept for the person, never picked),
 *       untouched NEITHER. The file's merge is lpres_conjoin across the
 *       lines that changed (graft_merge_join); untouched lines keep the
 *       base and join nothing, since the chain F < N < B < T would let one
 *       NEITHER hide every BOTH. clash and untouched are the two truths
 *       the blocks witness: the line is BOTH, the line is NEITHER.
 *   GR3 LINES ADDED AND REMOVED, exact: after - kept and before - kept,
 *       kept the lines both files keep in order; refused unless
 *       0 <= kept <= that side <= GRAFT_LINES_MAX.
 *   GR4 LIMITS of a diff or a merge: a file of at most 16 MiB and at most
 *       65536 lines, each on its own range.
 *   GR5 A COMMIT RECORD of ^ZXGRAFT holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/graft/commit.myra): commit
 *       and parent 9, author 6, stamp 11, message length 4. */
#ifndef GRAFT_RULES_H
#define GRAFT_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"
#include "witness_rules.h"

#define GRAFT_BYTES_MAX INT64_C(16777216) /* 16 MiB */
#define GRAFT_LINES_MAX 65536u
#define GRAFT_MSG_MAX   240u                 /* bytes in a commit message (GM2) */
#define GRAFT_AUTHOR    999999               /* the highest author number */
#define GRAFT_STAMP     INT64_C(99999999999) /* 11 digits of seconds */

/* GR1: is commit `commit` with parent `parent` linked to `last`? */
static inline bool graft_linked(int64_t commit, int64_t parent, int64_t last)
{
    return witness_linked(commit, parent, last);
}

/* GR2: the two truths the blocks witness (flags 0 or 1 only). */
static inline bool graft_line_clash(int64_t ours, int64_t theirs, int64_t apart)
{
    return ours == 1 && theirs == 1 && apart == 1;
}

static inline bool graft_line_untouched(int64_t ours, int64_t theirs)
{
    return ours == 0 && theirs == 0;
}

/* GR2: one line's four values from the two sides, joined with
 * lpres_kjoin. Flags outside 0..1 say nothing (NEITHER). */
static inline lpres_state_t graft_line_state(int64_t ours, int64_t theirs, int64_t apart)
{
    if (ours < 0 || ours > 1 || theirs < 0 || theirs > 1 || apart < 0 || apart > 1)
        return LPRES_STATE_NEITHER;
    lpres_state_t o = ours ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
    lpres_state_t t = LPRES_STATE_NEITHER;
    if (theirs) t = ours && apart ? LPRES_STATE_FALSE : LPRES_STATE_TRUE;
    return lpres_kjoin(o, t);
}

/* GR2: a whole file's merge from its lines' values. */
static inline lpres_state_t graft_merge_join(const lpres_state_t *line, uint32_t n)
{
    lpres_state_t acc = LPRES_STATE_TRUE;
    bool any = false;
    if (!line) return LPRES_STATE_NEITHER;
    for (uint32_t i = 0; i < n; i++) {
        if (line[i] == LPRES_STATE_NEITHER) continue;
        acc = lpres_conjoin(acc, line[i]);
        any = true;
    }
    return any ? acc : LPRES_STATE_NEITHER;
}

/* GR3: the lines one side has that the other does not; false refuses. */
static inline bool graft_lines_beyond(int64_t side, int64_t kept, int64_t *out)
{
    if (!out || kept < 0 || kept > side || side > (int64_t) GRAFT_LINES_MAX) return false;
    *out = side - kept;
    return true;
}

static inline bool graft_lines_added(int64_t after, int64_t kept, int64_t *out)
{
    return graft_lines_beyond(after, kept, out);
}

static inline bool graft_lines_removed(int64_t before, int64_t kept, int64_t *out)
{
    return graft_lines_beyond(before, kept, out);
}

/* GR4: may a file of `bytes` and `lines` be compared? */
static inline bool graft_diff_fits(int64_t bytes, int64_t lines)
{
    return bytes >= 0 && bytes <= GRAFT_BYTES_MAX && lines >= 0 &&
           lines <= (int64_t) GRAFT_LINES_MAX;
}

/* GR5: does a commit record fit ^ZXGRAFT? */
static inline bool graft_record_fits(int64_t commit, int64_t parent, int64_t author, int64_t stamp,
                                     int64_t msglen)
{
    return witness_digits_fit(commit, 9) && witness_digits_fit(parent, 9) &&
           witness_digits_fit(author, 6) && witness_digits_fit(stamp, 11) &&
           witness_digits_fit(msglen, 4);
}

#endif
