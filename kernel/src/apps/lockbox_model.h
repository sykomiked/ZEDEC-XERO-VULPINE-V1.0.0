/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lockbox_model.h — Lockbox, keys and passwords, as a model (zxm.h): the
 * node's post-quantum identity keys, when each expires, the saved
 * passwords' strength, and the books' seal.
 *
 *   LB1 KEYS ARE READ, NEVER HELD. fab.status gives the ids (SHA-256 of
 *       the public key) of the node's ML-DSA-87 signing key and ML-KEM-1024
 *       key, and whether an air-gap seal check is registered (a sealed
 *       hardware key). The secret seeds never leave zxvd; the model keeps
 *       only the public ids.
 *   LB2 EXPIRY IS THE OWNER'S. zxvd keeps no expiry for its keys, so the
 *       owner sets one per key (lockbox_set_expiry); a key whose id changes
 *       starts again with none. lockbox_due counts the keys due within a
 *       number of days (lockbox_days_left, LR2) and composes the notice.
 *   LB3 NO SECRET BYTES. A saved password is its name and three counts
 *       (lockbox_rules.h); the model never sees, stores or logs its bytes.
 *       zxvd has no method to store a secret, so the items live in the
 *       model, for the view.
 *   LB4 THE OWNER CONFIRMS. lockbox_propose checks an item against the
 *       limits (LR3, and fewer repeats than bytes: outside them it is
 *       refused), scores it (LR1) and holds
 *       it with a notice; a weak one (below LOCKBOX_STRONG) is advice, not
 *       a refusal. Only lockbox_confirm keeps it; lockbox_cancel drops it.
 *   LB5 THE BOOKS' SEAL. books.file gives the SHA-256 chain head of the
 *       owner's books (ledger.read), shown beside the keys.
 *   LB6 Nothing is changed before it is checked, and what is read is
 *       replaced only after a whole answer has been read.
 *
 * zxvd has no notification method, so the model composes the notice text
 * (lockbox_t.notice) for the view to show. No drawing, no allocation,
 * integers only. */
#ifndef LOCKBOX_MODEL_H
#define LOCKBOX_MODEL_H

#include "lockbox_rules.h"
#include "zxm.h"

#define LOCKBOX_ITEMS      64u
#define LOCKBOX_NAME_MAX   63u
#define LOCKBOX_ID_LEN     64u /* hex of a SHA-256 */
#define LOCKBOX_NOTICE_MAX 255u
#define LOCKBOX_STRONG     60 /* LR1 score from which an item is not weak */

enum {
    LOCKBOX_KEY_SIGN = 0, /* ML-DSA-87, fab.status sign_id */
    LOCKBOX_KEY_KEM = 1,  /* ML-KEM-1024, fab.status kem_id */
    LOCKBOX_KEYS = 2,
};

typedef struct {
    bool present;
    char id[LOCKBOX_ID_LEN + 1];
    int64_t expires; /* seconds since 1970; 0 none set (LB2) */
} lockbox_key_t;

typedef struct {
    char name[LOCKBOX_NAME_MAX + 1];
    int64_t length, classes, repeats;
    int64_t strength; /* LR1 */
} lockbox_item_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;   /* fab.status has been read */
    bool fab_open; /* zxvd's keys are open */
    bool sealed;   /* an air-gap seal check is registered */
    lockbox_key_t key[LOCKBOX_KEYS];
    lockbox_item_t item[LOCKBOX_ITEMS];
    uint32_t n;
    bool pending; /* LB4 */
    lockbox_item_t held;
    bool books_saved;
    char books_head[LOCKBOX_ID_LEN + 1];
    int64_t books_records;
    char notice[LOCKBOX_NOTICE_MAX + 1];
} lockbox_t;

void lockbox_init(lockbox_t *l, zxm_link_t *link);
/* LB1: fab.status; the keys' ids and the seal. */
int lockbox_load(lockbox_t *l);
/* LB2: the owner's expiry for a present key, 0 to clear. */
int lockbox_set_expiry(lockbox_t *l, uint32_t kind, int64_t expires);
/* LR2 for one key with an expiry; ZXM_E_STATE when it has none. */
int lockbox_key_days(const lockbox_t *l, uint32_t kind, int64_t now, int64_t *days);
/* LB2: how many keys are due within `within` days (0..LOCKBOX_STAMP_MAX /
 * 86400) at now; the notice names them. */
int lockbox_due(lockbox_t *l, int64_t now, int64_t within, uint32_t *n);
/* LB3, LB4 */
int lockbox_propose(lockbox_t *l, const char *name, int64_t length, int64_t classes,
                    int64_t repeats);
int lockbox_confirm(lockbox_t *l);
int lockbox_cancel(lockbox_t *l);
int lockbox_remove(lockbox_t *l, const char *name);
/* The item named name, or NULL. */
const lockbox_item_t *lockbox_item(const lockbox_t *l, const char *name);
/* How many kept items are weak (strength below LOCKBOX_STRONG). */
uint32_t lockbox_weak(const lockbox_t *l);
/* LB5: books.file. */
int lockbox_books(lockbox_t *l);

#endif
