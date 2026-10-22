/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lifeboat_model.h — Lifeboat, recovery, as a model (zxm.h): start safe
 * with every feature switch off (features.get, features.set), check the
 * system (journal.verify, ledger.check, sys.disk, backup.list,
 * backup.verify), restore from Keepsake's backups (backup.restore) and
 * check for a signed update (update.check). The rules are lifeboat_rules.h
 * (LB1-LB6), the same ones the app's Eve, KRON, Forth, BASIC, Sutra and
 * MYRA blocks run. No drawing, no allocation, integers only.
 *
 *   LM1 THE OWNER'S WORD. Whether the owner approved an action is the
 *       view's word (the approved argument): zxvd has no method that says
 *       whose session it is. Without it nothing is sent.
 *   LM2 SAFE START turns every feature switch that is on off, one by one,
 *       only with the owner's approval. Each change is taken only when
 *       zxvd's reply names the same switch, off; at the first that fails
 *       Lifeboat stops, and what it turned off stays off.
 *   LM3 A CHECK reads zxvd's clock (sys.info), the journal
 *       (journal.verify), the books (ledger.check), the disk (sys.disk)
 *       and, when a Keepsake destination is set, the newest backup
 *       (backup.list, then backup.verify of that snapshot). From those it
 *       works out the four results (LB1) and with them start_ok (LB2) and
 *       the repair step (LB4), and files a recovery record (LB6). It is all
 *       or nothing: a reply out of bounds leaves the last check as it was.
 *       Defaults taken: zxvd keeping no journal is nothing broken (journal
 *       1, said in the notice); books that do not answer (not open, or a
 *       failed check) do not hold (books 0); no destination, no snapshot
 *       or a destination zxvd refuses is no tested backup (tested 0). The
 *       room is what this user may still write (sys.disk avail) minus the
 *       floor, held at 18 digits.
 *   LM4 A RESTORE is taken only with the owner's approval, only from the
 *       snapshot the last check read back whole, and only when it fits
 *       (LB5: the snapshot's bytes with the floor kept free, at most what
 *       may be written). zxvd brings it back as a new item (K5), never over
 *       one; afterwards the check is stale and runs again.
 *   LM5 AN UPDATE CHECK runs update.check and keeps its status. It installs
 *       nothing: zxvd has no install, reinstall or Hoard repair method.
 *   LM6 The notice is composed for the view; zxvd has no method that
 *       posts one. */
#ifndef LIFEBOAT_MODEL_H
#define LIFEBOAT_MODEL_H

#include "lifeboat_rules.h"
#include "zxm.h"

#define LIFEBOAT_SWITCHES  32u   /* zxvd_ext.h ZXVD_EXT_MAX_FEATURES */
#define LIFEBOAT_NAME_MAX  23u   /* a feature name, ^[a-z0-9_]{1,23}$ */
#define LIFEBOAT_DEST      1024u /* backup.* dest, maxLength */
#define LIFEBOAT_PATH      1024u /* backup.restore path and to, maxLength */
#define LIFEBOAT_WHY_MAX   159u
#define LIFEBOAT_STATUS    63u
#define LIFEBOAT_RECORDS   16u /* recovery records kept, newest last */
#define LIFEBOAT_NOTICE    767u
#define LIFEBOAT_FLOOR     1073741824LL /* default floor kept free: 1 GiB */
#define LIFEBOAT_FLOOR_MAX LIFEBOAT_BYTES_MAX

typedef struct {
    char name[LIFEBOAT_NAME_MAX + 1];
    bool enabled;
    bool regulated;
} lifeboat_switch_t;

typedef struct {
    bool done; /* a check ran and nothing changed since */
    int64_t time_ms;
    bool journal_on, journal_ok;
    uint64_t journal_entries, broken_at;
    char journal_why[LIFEBOAT_WHY_MAX + 1];
    bool books_ok;
    char books_why[LIFEBOAT_WHY_MAX + 1];
    uint64_t total, free, avail; /* bytes, sys.disk */
    int64_t used_x100;           /* used_share.used_x100; LIFEBOAT_REFUSED unknown */
    uint32_t snapshots;          /* in the destination */
    uint32_t snapshot;           /* the one verified; 0 none */
    uint64_t snap_bytes;
    uint32_t bad, missing;
    char backup_why[LIFEBOAT_WHY_MAX + 1];
    int64_t journal, books, room, tested; /* LB1 */
    bool start_ok;                        /* LB2 */
    int64_t step;                         /* LB4 */
} lifeboat_check_t;

typedef struct {
    int64_t seq, step, journal, books, tested, room, stamp;
} lifeboat_record_t;

typedef struct {
    zxm_link_t *link;
    char dest[LIFEBOAT_DEST + 1]; /* "" until the view sets one */
    int64_t floor;                /* bytes kept free */
    lifeboat_switch_t sw[LIFEBOAT_SWITCHES];
    uint32_t nsw;
    bool listed;           /* the switches were read */
    uint32_t switched_off; /* by the last safe start */
    bool safe;             /* every switch off after it */
    lifeboat_check_t check;
    lifeboat_record_t rec[LIFEBOAT_RECORDS];
    uint32_t nrec;
    int64_t seq;
    char landed[LIFEBOAT_PATH + 1]; /* where the last restore put things */
    uint32_t restored;
    char update[LIFEBOAT_STATUS + 1]; /* update.check's status, "" none yet */
    char notice[LIFEBOAT_NOTICE + 1];
} lifeboat_t;

void lifeboat_init(lifeboat_t *b, zxm_link_t *link);
/* An absolute folder outside the Hoard (zxv_keepsake.h K1); the last check
 * goes stale. ZXM_E_ARG for anything else. */
int lifeboat_set_dest(lifeboat_t *b, const char *dest);
/* The floor kept free, 0..LIFEBOAT_FLOOR_MAX bytes; the last check goes
 * stale. */
int lifeboat_set_floor(lifeboat_t *b, int64_t floor);
/* features.get */
int lifeboat_switches(lifeboat_t *b);
/* LM2: ZXM_E_STATE without the owner's approval, nothing sent. */
int lifeboat_safe_start(lifeboat_t *b, bool approved);
/* LM3 */
int lifeboat_check(lifeboat_t *b);
/* LM4: path NULL or "" is everything; to NULL or "" lets zxvd choose (its
 * old place when free, else beside it). */
int lifeboat_restore(lifeboat_t *b, const char *path, const char *to, bool approved);
/* LM5 */
int lifeboat_update_check(lifeboat_t *b);
/* LB6: whether record i (0 oldest kept) fits ^ZXLIFEBOAT. */
bool lifeboat_record(const lifeboat_t *b, uint32_t i);
const lifeboat_switch_t *lifeboat_find(const lifeboat_t *b, const char *name);

#endif
