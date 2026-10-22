/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* moraine_model.h — Moraine, key escrow, as a model (zxm.h): keys held in
 * escrow with guardians, released only on their word, through zxvd's
 * recovery engine. No drawing, no allocation, integers only. The rules it
 * runs are moraine_rules.h (MR1-MR5 and the parts it composes).
 *
 * zxvd methods used: recovery.setup, recovery.status, recovery.claim,
 * recovery.message, recovery.approve, recovery.veto, recovery.finalize,
 * recovery.cancel and journal.query.
 *
 *   MM1 GUARDIANS. recovery.setup with the account's root, exactly five
 *       guardian keys (zxvd's rule), the gateway, the first device and the
 *       waiting period (timelock, seconds). The guardians' hierarchy is
 *       the model's: each guardian under the owner or another guardian, a
 *       tree (moraine_hierarchy) whose every node fits
 *       guardian_node_ok before anything is sent.
 *   MM2 KEYS. Up to MORAINE_KEYS keys, each by its public id (64 hex), a
 *       name, its length in symbols and its alphabet (its bits,
 *       warding.key_entropy_bits), its phrase's length, classes and repeats
 *       (lockbox.secret_ok) and when it expires (lockbox.days_left). Never
 *       a key's bytes: the model has no field for them. Its strength is
 *       escrow_key_strength over its bits, the quorum and the guardians.
 *   MM3 GRANTS. Time-limited access: a requester (1..9999) to a key, with
 *       gatehouse.use_ok's three flags (asked, granted, revoked) and the
 *       window's end. A grant is live while its use is allowed and its
 *       window is not over; the access grid is warding.access_grid_cell
 *       over every grant (requester by key, live).
 *   MM4 A CLAIM asks to release one key to one requester (recovery.claim):
 *       only while no claim is open, for a key still in escrow, by a
 *       requester whose grant is live.
 *   MM5 THE GUARDIANS' WORD. An approval is the guardian's signature
 *       (recovery.approve, over the bytes recovery.message answers): TRUE.
 *       An objection within the waiting period is FALSE; zxvd has no
 *       method for a guardian's objection (recovery.veto is an old
 *       device's), so the model keeps it. The claim's word is MR5: one
 *       approval and one objection is BOTH, held, and the key is neither
 *       released nor lost. A device's veto (recovery.veto) is zxvd's: 4
 *       approvals and 14 days.
 *   MM6 RELEASE NOW is MR1 over heirloom.heir_release_due (the claim open,
 *       zxvd's quorum, the approvals past it, the wait over, no standing
 *       objection), the grant's window and gatehouse.use_ok; and the
 *       claim's word TRUE.
 *   MM7 A RELEASE (recovery.finalize) only when MM6 holds, and its entry
 *       in the custody register fits escrow_custody_ok (number last + 1,
 *       the guardian who completed the quorum, its time) and
 *       witness.audit_ok, all checked before it is sent. It is stamped
 *       with the journal's newest seq (journal.query; 0 with no journal).
 *       The register is intact when each entry is linked to the one
 *       before it (witness.chain_ok).
 *   MM8 A CANCEL (recovery.cancel) closes the claim; the key stays in
 *       escrow. Every action that changes what is kept or sent takes the
 *       owner's confirmed word, checked after its arguments and before
 *       anything is sent: without it, ZXM_E_STATE and nothing changes. */
#ifndef MORAINE_MODEL_H
#define MORAINE_MODEL_H

#include "moraine_rules.h"
#include "zxm.h"

#define MORAINE_KEYS      8u
#define MORAINE_GRANTS    32u
#define MORAINE_CUSTODY   64u
#define MORAINE_KEY_HEX   64u
#define MORAINE_SIG_HEX   128u
#define MORAINE_MSG_HEX   512u
#define MORAINE_NAME_MAX  47u
#define MORAINE_STATE_MAX 15u
#define MORAINE_NOTICE    240u
#define MORAINE_REQ_MAX   9999 /* a requester's number: Warding's holder, 4 digits */
#define MORAINE_QUORUM    3    /* recovery.h REC_QUORUM */
#define MORAINE_VETO_Q    4    /* recovery.h REC_VETO_QUORUM */
#define MORAINE_VETO_WAIT (INT64_C(14) * 86400) /* recovery.h REC_VETO_DAYS */

typedef struct {
    char id[MORAINE_KEY_HEX + 1];
    char name[MORAINE_NAME_MAX + 1];
    int64_t chars, alphabet, bits, length, classes, repeats, expires;
    bool released;
} moraine_key_t;

typedef struct {
    int64_t requester;
    uint32_t key;
    int64_t asked, granted, revoked; /* gatehouse.use_ok's flags */
    int64_t until;                   /* the window's end, seconds since 1970 */
} moraine_grant_t;

typedef struct {
    int64_t number, prev, guardian, stamp, seq, requester;
    uint32_t key;
} moraine_custody_t;

typedef struct {
    zxm_link_t *link;
    int64_t escrow;
    bool set_up;
    int64_t timelock, added;
    int64_t parent[MORAINE_GUARDIANS], rank[MORAINE_GUARDIANS];
    char state[MORAINE_STATE_MAX + 1]; /* zxvd's: idle, pending or thawing */
    int64_t claim, opened;
    uint32_t approvals, needed;
    bool vetoed; /* a device's veto (zxvd) */
    lpres_state_t word[MORAINE_GUARDIANS];
    int32_t last_approver, claim_grant;
    moraine_key_t key[MORAINE_KEYS];
    uint32_t nkey;
    moraine_grant_t grant[MORAINE_GRANTS];
    uint32_t ngrant;
    moraine_custody_t custody[MORAINE_CUSTODY];
    uint32_t ncustody;
    char notice[MORAINE_NOTICE];
} moraine_t;

void moraine_init(moraine_t *m, zxm_link_t *link);
/* MM1: parent[g] is guardian g+1's parent (0 the owner) */
int moraine_setup(moraine_t *m, int64_t escrow, const char *root, const char *guardians,
                  const char *gateway, const char *first_device, int64_t timelock,
                  const int64_t parent[MORAINE_GUARDIANS], int64_t now, bool owner_confirmed);
int moraine_refresh(moraine_t *m, int64_t now);
/* MM2 */
int moraine_add_key(moraine_t *m, const char *id, const char *name, int64_t chars, int64_t alphabet,
                    int64_t length, int64_t classes, int64_t repeats, int64_t expires,
                    bool owner_confirmed, uint32_t *index);
int64_t moraine_key_score(const moraine_t *m, uint32_t key);
int64_t moraine_key_days(const moraine_t *m, uint32_t key, int64_t now);
/* MM3 */
int moraine_grant(moraine_t *m, int64_t requester, uint32_t key, int64_t asked, int64_t granted,
                  int64_t revoked, int64_t until, bool owner_confirmed);
bool moraine_grant_live(const moraine_t *m, uint32_t grant, int64_t now);
/* the access grid's cell: live grants that requester holds to that key */
int64_t moraine_live_grants(const moraine_t *m, int64_t requester, uint32_t key, int64_t now);
/* MM4 */
int moraine_claim(moraine_t *m, uint32_t key, int64_t requester, const char *new_key, int64_t now,
                  bool owner_confirmed);
/* the bytes a signer signs (approve, veto, cancel), as hex into out */
int moraine_message(moraine_t *m, const char *kind, char *out, uint32_t cap);
/* MM5 */
int moraine_approve(moraine_t *m, uint32_t guardian, const char *sig, bool owner_confirmed);
int moraine_object(moraine_t *m, uint32_t guardian, int64_t now, bool owner_confirmed);
int moraine_veto(moraine_t *m, const char *device, const char *sig, bool owner_confirmed);
lpres_state_t moraine_claim_word(const moraine_t *m);
/* MM6 */
bool moraine_due(const moraine_t *m, int64_t now);
bool moraine_release_ready(const moraine_t *m, int64_t now);
/* MM7 */
int moraine_release(moraine_t *m, int64_t now, bool owner_confirmed);
lpres_state_t moraine_register_intact(const moraine_t *m);
/* MM8 */
int moraine_cancel(moraine_t *m, const char *device, const char *sig, bool owner_confirmed);

#endif
