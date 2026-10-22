/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* moraine_rules.h — Moraine's rules in C, the reference its Eve, KRON,
 * Sutra, Trebago and MYRA blocks are checked against (blocks/moraine),
 * with the rules it composes from nearer the core. Pure integer
 * functions, no state: the model (moraine_model.c) and the language test
 * (tests/moraine_zxapp.inc) both include this.
 *
 *   MR1 RELEASE NOW (key_release_now): Heirloom's release due (due 1,
 *       heirloom.heir_release_due as a flag) AND the requester's access
 *       window not over (left, seconds to the window's end,
 *       1..MORAINE_STAMP_MAX) AND the requester allowed (allowed 1,
 *       gatehouse.use_ok as a flag). Any other value is no: a flag that
 *       is no flag is never read as yes.
 *   MR2 THE CUSTODY REGISTER (escrow_custody_ok): a release's number is
 *       exactly last + 1 (never reused and no gap), the guardian whose
 *       signature completed the quorum is 0..4 (zxvd's index), and its
 *       time is not before the release before it (stamp >= prior). Each
 *       field inside Sutra's picture (number and last 9(9), guardian 9,
 *       stamp and prior 9(12)), else refused (MORAINE_REFUSED). Warding's
 *       key register (serial > last) lets a serial be skipped; a custody
 *       register may not, so this is its own rule.
 *   MR3 KEY STRENGTH (escrow_key_strength), 0..100, from lengths and
 *       counts only: 15 * floor(min(bits, 256) / 64) + 10 * min(quorum, 3)
 *       + 5 * min(guardians - quorum, 2). bits 0..MORAINE_BITS_MAX, quorum
 *       1..5, guardians quorum..5, else refused. Advice only.
 *   MR4 A GUARDIAN NODE (guardian_node_ok) fits
 *       ^ZXMORAINE(escrow, guardian) = parent ^ rank ^ added: escrow 9
 *       digits, guardian, parent and rank 1, added (seconds) 12, sign
 *       aside. The hierarchy itself (moraine_hierarchy) is a tree: each
 *       guardian 1..5 under the owner (0) or another guardian, never under
 *       itself or one of its own, its rank one past its parent's.
 *   MR5 THE GUARDIANS' WORD on a claim: each guardian says TRUE (approved),
 *       FALSE (objected within the waiting period) or NEITHER (silent); the
 *       claim's word is lpres_kjoin of all five. One approval and one
 *       objection is BOTH: held, the key neither released nor lost. Only
 *       TRUE may release.
 *   Composed, as the parts' own rules: heirloom_release_due (HR3),
 *   gatehouse_use_ok (GR3), lockbox_days_left and lockbox_secret_ok (LR2,
 *   LR3), witness_linked and witness_record_fits (WR1, WR3),
 *   warding_entropy_bits and warding_grid_cell (WR4, WR5). */
#ifndef MORAINE_RULES_H
#define MORAINE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "gatehouse_rules.h"
#include "heirloom_rules.h"
#include "lockbox_rules.h"
#include "lpres.h"
#include "warding_rules.h"
#include "witness_rules.h"

#define MORAINE_REFUSED    (-1)
#define MORAINE_STAMP_MAX  INT64_C(253402300799) /* MR1: lockbox's year 9999 */
#define MORAINE_NUMBER_MAX INT64_C(999999999)    /* MR2: 9(9) */
#define MORAINE_TIME_MAX   INT64_C(999999999999) /* MR2: 9(12) */
#define MORAINE_BITS_MAX   65536                 /* MR3 */
#define MORAINE_GUARDIANS  5                     /* zxvd recovery.h REC_GUARDIANS */
#define MORAINE_ESCROW_MAX INT64_C(999999999)    /* MR4: 9 digits */
#define MORAINE_KIND       7                     /* witness.audit_ok's kind for a release */

/* MR1 */
static inline bool moraine_release_now(int64_t due, int64_t left, int64_t allowed)
{
    return due == 1 && left >= 1 && left <= MORAINE_STAMP_MAX && allowed == 1;
}

/* MR2: 1 a fitting entry, 0 not, MORAINE_REFUSED outside the pictures */
static inline int64_t moraine_custody_ok(int64_t number, int64_t last, int64_t guardian,
                                         int64_t stamp, int64_t prior)
{
    if (number < 0 || number > MORAINE_NUMBER_MAX || last < 0 || last > MORAINE_NUMBER_MAX ||
        guardian < 0 || guardian > 9 || stamp < 0 || stamp > MORAINE_TIME_MAX || prior < 0 ||
        prior > MORAINE_TIME_MAX)
        return MORAINE_REFUSED;
    return number == last + 1 && guardian < MORAINE_GUARDIANS && stamp >= prior ? 1 : 0;
}

/* MR3 */
static inline int64_t moraine_key_strength(int64_t bits, int64_t quorum, int64_t guardians)
{
    if (bits < 0 || bits > MORAINE_BITS_MAX || quorum < 1 || quorum > MORAINE_GUARDIANS ||
        guardians < quorum || guardians > MORAINE_GUARDIANS)
        return MORAINE_REFUSED;
    int64_t b = bits < 256 ? bits : 256, spare = guardians - quorum;
    return 15 * (b / 64) + 10 * (quorum < 3 ? quorum : 3) + 5 * (spare < 2 ? spare : 2);
}

/* MR4: the record fits */
static inline bool moraine_node_ok(int64_t escrow, int64_t guardian, int64_t parent, int64_t rank,
                                   int64_t added)
{
    return witness_digits_fit(escrow, 9) && witness_digits_fit(guardian, 1) &&
           witness_digits_fit(parent, 1) && witness_digits_fit(rank, 1) &&
           witness_digits_fit(added, 12);
}

/* MR4: parent[g] is guardian g+1's parent (0 the owner, else 1..5); each
 * rank into rank[g]. true when it is a tree under the owner. */
static inline bool moraine_hierarchy(const int64_t parent[MORAINE_GUARDIANS],
                                     int64_t rank[MORAINE_GUARDIANS])
{
    if (!parent || !rank) return false;
    for (int g = 0; g < MORAINE_GUARDIANS; g++) {
        int64_t at = g + 1, r = 0;
        while (at != 0) {
            int64_t p = parent[at - 1];
            if (p < 0 || p > MORAINE_GUARDIANS || p == at || ++r > MORAINE_GUARDIANS) return false;
            at = p;
        }
        rank[g] = r;
    }
    return true;
}

/* MR5: the claim's word over the guardians' words */
static inline lpres_state_t moraine_word(const lpres_state_t word[MORAINE_GUARDIANS])
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    for (int g = 0; word && g < MORAINE_GUARDIANS; g++) s = lpres_kjoin(s, word[g]);
    return s;
}

#endif
