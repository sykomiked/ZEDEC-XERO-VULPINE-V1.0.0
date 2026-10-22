/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pavise_rules.h — Pavise's rules in C, the reference its Eve, KRON,
 * Sutra, Trebago and MYRA blocks are checked against (blocks/pavise), with
 * the rules it composes from nearer the core. Pure integer functions, no
 * state: the model (pavise_model.c) and the language test
 * (tests/pavise_zxapp.inc) both include this.
 *
 *   PR1 ALLOW (perimeter_allow): one use of one capability by one app is
 *       allowed when Gatehouse allows it (gate, gatehouse.use_ok as a flag),
 *       Hedgerow's path for it is reachable (reach, hedgerow.path_open as a
 *       flag; 1 for a capability that opens no network path), its key is
 *       valid (keyok 1: registered and not revoked) and not expired (keydays,
 *       lockbox.days_left, 1..PAVISE_KEYDAYS_MAX) and no policy denies it
 *       (deny 0). Each flag exactly 0 or 1; anything else is no.
 *   PR2 THE POLICY CLAUSE is four-valued: each policy source says allow (T),
 *       deny (F) or nothing (N) about one app and capability, and the
 *       sources are joined with lpres_kjoin, so one allowing and one denying
 *       is BOTH. No source saying anything is no denial (T). The owner's
 *       ruling (1 allow, 2 deny) settles it. The decision is PR1's truth
 *       (deny = 1 when the clause is F) joined with the clause by
 *       lpres_conjoin, so BOTH or NEITHER is never TRUE: denied until the
 *       owner rules.
 *   PR3 THE ACCESS LEDGER (access_ledger_ok): one decision filed as a fixed
 *       record: decision number 1..999999999 (tallystick.next_doc_number:
 *       last + 1, never reused or wrapped), app 1..9999, capability
 *       0..18 (zxapp.c R3's index) and verdict 0..3 (the lpres state:
 *       NEITHER 0, TRUE 1, FALSE 2, BOTH 3).
 *   PR4 EXPOSURE (perimeter_exposure): 2 points for each path open from
 *       beyond the local network (paths, one flag each), 3 for each key with
 *       PAVISE_SOON days left or fewer (keydays, lockbox.days_left each;
 *       0 is due or past), 5 for each unsigned app (signed, one flag each);
 *       exact. A flag that is no flag or keydays outside
 *       0..PAVISE_KEYDAYS_MAX is refused. The weights are defaults taken:
 *       an unsigned app runs code nobody vouched for, a key near its end
 *       locks the owner out or is replaced in a hurry, an open path is
 *       only a door.
 *   PR5 THE PERMISSION MATRIX (perm_app_total): app by capability, one row
 *       per (app, capability) cell with its allow flag; each row's answer
 *       is its app's row total, the capabilities that app is allowed
 *       (gatehouse.holders is the column total).
 *   PR6 POLICY BOUNDS (policy_rule_ok): a declared policy rule is a source
 *       1..99, an app 1..9999, a capability 0..18 and an effect 0 (deny)
 *       or 1 (allow), each on its own range.
 *   PR7 THE TRUST TREE (trust_node_ok) fits ^ZXPAVISE(device, app, key) =
 *       trust ^ since: device, app and key 4 digits each, trust 1, since
 *       (seconds since 1970) 11, sign aside. */
#ifndef PAVISE_RULES_H
#define PAVISE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "gatehouse_rules.h"
#include "hedgerow_rules.h"
#include "lockbox_rules.h"
#include "lpres.h"
#include "tallystick_rules.h"
#include "witness_rules.h"

#define PAVISE_REFUSED      (-1)
#define PAVISE_KEYDAYS_MAX  INT64_C(2932896) /* lockbox.days_left at most: year 9999 */
#define PAVISE_SOON         30               /* PR4: days */
#define PAVISE_W_PATH       2                /* PR4 */
#define PAVISE_W_KEY        3
#define PAVISE_W_UNSIGNED   5
#define PAVISE_APP_MAX      9999            /* PR3, PR6: 9(4) */
#define PAVISE_CAP_MAX      18              /* PR3, PR6: zxapp.c R3, 19 capabilities */
#define PAVISE_SOURCE_MAX   99              /* PR6 */
#define PAVISE_DECISION_MAX TSTK_NUMBER_MAX /* PR3: 9(9) */

/* PR1 */
static inline bool pavise_allow(int64_t gate, int64_t reach, int64_t keyok, int64_t keydays,
                                int64_t deny)
{
    return gate == 1 && reach == 1 && keyok == 1 && keydays >= 1 && keydays <= PAVISE_KEYDAYS_MAX &&
           deny == 0;
}

/* PR2: what one source says (0 nothing, 1 allow, 2 deny) as a state */
static inline lpres_state_t pavise_said(int64_t effect)
{
    return effect == 1 ? LPRES_STATE_TRUE : effect == 2 ? LPRES_STATE_FALSE : LPRES_STATE_NEITHER;
}

/* PR2: the clause from every source's word, already joined with
 * lpres_kjoin, and the owner's ruling */
static inline lpres_state_t pavise_clause_of(lpres_state_t joined, int64_t ruling)
{
    if (ruling == 1) return LPRES_STATE_TRUE;
    if (ruling == 2) return LPRES_STATE_FALSE;
    return joined == LPRES_STATE_NEITHER ? LPRES_STATE_TRUE : joined;
}

/* PR2: the clause from what sources a and b say and the owner's ruling */
static inline lpres_state_t pavise_clause(int64_t a, int64_t b, int64_t ruling)
{
    return pavise_clause_of(lpres_kjoin(pavise_said(a), pavise_said(b)), ruling);
}

/* PR2: the decision */
static inline lpres_state_t pavise_decision(int64_t gate, int64_t reach, int64_t keyok,
                                            int64_t keydays, lpres_state_t clause)
{
    bool rule = pavise_allow(gate, reach, keyok, keydays, clause == LPRES_STATE_FALSE ? 1 : 0);
    return lpres_conjoin(rule ? LPRES_STATE_TRUE : LPRES_STATE_FALSE, clause);
}

/* PR3 */
static inline bool pavise_ledger_ok(int64_t decision, int64_t app, int64_t cap, int64_t verdict)
{
    return decision >= 1 && decision <= PAVISE_DECISION_MAX && app >= 1 && app <= PAVISE_APP_MAX &&
           cap >= 0 && cap <= PAVISE_CAP_MAX && verdict >= 0 && verdict <= 3;
}

/* PR4: PAVISE_REFUSED for a flag that is no flag or keydays off its range */
static inline int64_t pavise_exposure(const int64_t *paths, uint32_t np, const int64_t *keydays,
                                      uint32_t nk, const int64_t *signd, uint32_t na)
{
    int64_t s = 0;
    if ((!paths && np) || (!keydays && nk) || (!signd && na)) return PAVISE_REFUSED;
    for (uint32_t i = 0; i < np; i++) {
        if (paths[i] != 0 && paths[i] != 1) return PAVISE_REFUSED;
        s += PAVISE_W_PATH * paths[i];
    }
    for (uint32_t i = 0; i < nk; i++) {
        if (keydays[i] < 0 || keydays[i] > PAVISE_KEYDAYS_MAX) return PAVISE_REFUSED;
        s += keydays[i] <= PAVISE_SOON ? PAVISE_W_KEY : 0;
    }
    for (uint32_t i = 0; i < na; i++) {
        if (signd[i] != 0 && signd[i] != 1) return PAVISE_REFUSED;
        s += PAVISE_W_UNSIGNED * (1 - signd[i]);
    }
    return s;
}

/* PR5: row i's app total */
static inline int64_t pavise_app_total(const int64_t *app, const int64_t *allow, uint32_t n,
                                       uint32_t i)
{
    int64_t c = 0;
    if (!app || !allow || i >= n) return 0;
    for (uint32_t j = 0; j < n; j++) c += app[j] == app[i] ? allow[j] : 0;
    return c;
}

/* PR6 */
static inline bool pavise_policy_ok(int64_t source, int64_t app, int64_t cap, int64_t effect)
{
    return source >= 1 && source <= PAVISE_SOURCE_MAX && app >= 1 && app <= PAVISE_APP_MAX &&
           cap >= 0 && cap <= PAVISE_CAP_MAX && effect >= 0 && effect <= 1;
}

/* PR7 */
static inline bool pavise_node_ok(int64_t device, int64_t app, int64_t key, int64_t trust,
                                  int64_t since)
{
    return witness_digits_fit(device, 4) && witness_digits_fit(app, 4) &&
           witness_digits_fit(key, 4) && witness_digits_fit(trust, 1) &&
           witness_digits_fit(since, 11);
}

#endif
