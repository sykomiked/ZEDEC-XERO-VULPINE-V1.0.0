/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* reputation.h — the Pig Badge counterintelligence signal + earnable badges
 *
 * WHAT THIS IS
 * ------------
 * Two reputation surfaces, one honest rule each.
 *
 * (1) THE PIG BADGE. Some accounts are not here to play nice: repeat platform
 *     abusers, infiltrators, saboteurs — an intelligence operative running an
 *     op against the commons. When the counter-intel pipeline CONFIRMS an abuse
 *     occurrence, that account accrues a PIG LEVEL. The pig level is stamped on
 *     their profile icon and is visible EXTERNALLY to EVERYONE ELSE — but NEVER
 *     to the operative themselves. They walk around the commons wearing a badge
 *     the whole room can read and they cannot. It runs 1..9999; once it passes
 *     9000 the badge just reads "over 9000".
 *
 *     It is NOT a ban. Free speech is preserved — this is a WARNING LABEL, not a
 *     gag. It composes with concord.h (the non-coercive social layer): concord
 *     decides who is MATCHED with whom; the pig badge decides what the community
 *     can SEE about someone. There is deliberately no ban/mute/remove function
 *     anywhere in this API. The community gets the truth and makes its own call.
 *
 * (2) EARNABLE BADGES. A tiered set of badges that gamify real productivity, so
 *     the platform plays like its own video game while people do actual work.
 *     Each badge has an id and a level; higher levels are strictly better; a
 *     gamified aggregate score rises as they are earned.
 *
 * The badge itself renders on the profile icon (icon.h + theme.h) over the
 * subject drawn from identity.h; this module owns the LEVELS, not the pixels.
 *
 * OPS BOUNDARY: "confirmed abuse occurrence" is a SUPPLIED determination handed
 * down by the counter-intelligence confirmation pipeline upstream. pig_flag()
 * RECORDS a confirmed occurrence — it does not itself decide guilt. Never call
 * it on suspicion; that would fabricate an accusation, which this module refuses
 * to do on principle.
 *
 * Freestanding: integer only, fixed-size arrays, no libc, no malloc, no float.
 */
#ifndef ZXV_REPUTATION_H
#define ZXV_REPUTATION_H

#include <stdint.h>
#include <stdbool.h>

#define REP_MAX_SUBJECTS        256u
#define REP_MAX_BADGES_PER_SUB   32u
#define REP_PIG_MAX            9999u   /* pig level saturates here            */
#define REP_PIG_OVER           9000u   /* strictly above this: "over 9000"    */

/* Per-subject reputation record. One row per known subject id.
 *
 * INVARIANT — `pig_level` is the RAW accrued count and is NOT a display value.
 * The counter-intelligence design requires that a subject can NEVER see their
 * own pig badge (so they cannot game or perform to it), while everyone else
 * sees it. That rule lives in pig_level_seen_by(): ALL external display MUST go
 * through it. Do NOT read this field directly to show a level to a viewer — a
 * bare read leaks the subject's own badge to themselves and defeats the whole
 * mechanism. Direct access is reserved for reputation.c's own bookkeeping. */
typedef struct {
    bool     used;
    uint32_t subject;
    uint32_t pig_level;                 /* RAW 0..REP_PIG_MAX — display via pig_level_seen_by ONLY */
    uint32_t n_badges;
    struct { uint32_t id; uint32_t level; } badge[REP_MAX_BADGES_PER_SUB];
} rep_entry_t;

typedef struct {
    rep_entry_t entry[REP_MAX_SUBJECTS];
    uint32_t    n_entries;
} rep_state_t;

/* ---- lifecycle ---- */
void rep_init(rep_state_t *s);

/* ---- THE PIG BADGE ---- */

/* Record ONE confirmed abuse occurrence against `subject` (the confirmation is
 * made upstream). Increments the subject's pig level, saturating at 9999.
 * Returns the new pig level, or -1 if the subject table is full. */
int32_t pig_flag(rep_state_t *s, uint32_t subject);

/* The subject's accrued pig level AS SEEN BY `viewer`. Returns 0 when
 * viewer == subject — you never see your OWN pig badge — and the true accrued
 * level for everyone else. */
uint32_t pig_level_seen_by(const rep_state_t *s, uint32_t subject, uint32_t viewer);

/* Render the badge text for a level into `out` (NUL-terminated within `cap`):
 * the literal "over 9000" when level > 9000, otherwise the decimal number.
 * The level is capped at 9999 first. */
void pig_badge_text(uint32_t level, char *out, uint32_t cap);

/* ---- EARNABLE BADGES ---- */

/* Award (or raise) `badge_id` on `subject` to at least `level`. Badge levels
 * are monotonic — an award never lowers a level you already earned. Returns the
 * resulting level, or -1 if the subject/badge tables are full. */
int32_t badge_award(rep_state_t *s, uint32_t subject, uint32_t badge_id, uint32_t level);

/* True if `subject` holds `badge_id` at level >= `min_level`. */
bool badge_has(const rep_state_t *s, uint32_t subject, uint32_t badge_id, uint32_t min_level);

/* Gamified aggregate: the sum of the subject's badge levels. Rises with awards.
 * (The pig level is NOT part of this — shame and score are separate ledgers.) */
uint32_t badge_score(const rep_state_t *s, uint32_t subject);

#endif /* ZXV_REPUTATION_H */
