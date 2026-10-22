/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cellar_model.h — Cellar, the disk utility, as a model (zxm.h). No
 * drawing, no allocation, integers only; zxvd only through the link.
 *
 *   CM1 THE VOLUME. sys.disk gives the volume holding the Hoard: total,
 *       free and what this user may still write (avail), in bytes. How
 *       full it is, x100, is CR1 (used_share.used_x100) of total and free.
 *       zxvd reports this one volume only.
 *   CM2 SPACE BY FOLDER. The Hoard (or one folder in it, the base) is
 *       walked with files.list, at most CELLAR_DIRS folders. Each folder
 *       directly in the base gets the bytes and files below it; files
 *       directly in the base are its loose bytes. A folder's share of what
 *       the base holds is CR2 (banker.share) and its share of the volume
 *       CR1 with left = total - its bytes; the base's own bytes are CR3
 *       (banker.total).
 *   CM3 THE TRASH. files.list on ".Trash", walked the same way: its items
 *       (what lies directly in it) and their bytes, CR3, and its share of
 *       the volume, CR1. Emptying it (files.empty_trash) removes them for
 *       good, so it is done only with the owner's yes.
 *   CM4 CHECK THE HOARD. A walk of the base reading every name and date:
 *       a name the Hoard's paths cannot hold (zxv_files.h F2: '\', ':', a
 *       control character, a trailing '.' or space) cannot be opened,
 *       moved or trashed through zxvd, and a date later than sys.info's
 *       clock plus CELLAR_SKEW seconds is from a clock gone wrong. Each is
 *       counted with the first one's path; a misnamed folder is not
 *       walked into (zxvd refuses its path).
 *   CM5 SAFE TO REPAIR. room is avail less CELLAR_REPAIR_RESERVE (scratch
 *       a repair keeps free; the default taken, since no zxvd method says
 *       what a repair needs), fill is CM1. CR4 (repair_safe_ok) says
 *       whether a repair may run now, and the advice says what to do, item
 *       by item. Advisory and owner-confirmed: zxvd has no method to check
 *       or repair the Hoard's structure (none is invented), so the model
 *       repairs nothing itself; the owner renames a misnamed item outside
 *       ZXV and resaves a misdated file.
 *
 * Missing from zxvd, so not done: a list of every volume (sys.disk gives
 * the Hoard's), a check or repair of the Hoard's structure, the bytes
 * below a folder in one call (the model walks), and making, opening or
 * checking a disc image. */
#ifndef CELLAR_MODEL_H
#define CELLAR_MODEL_H

#include "cellar_rules.h"
#include "zxm.h"

#define CELLAR_FOLDERS        64u  /* folders directly in the base */
#define CELLAR_DIRS           512u /* folders walked in one pass */
#define CELLAR_PATH           1024u
#define CELLAR_NAME           255u
#define CELLAR_ADVICE         1024u
#define CELLAR_SKEW           86400LL      /* a day of clock drift is allowed */
#define CELLAR_REPAIR_RESERVE 1073741824LL /* 1 GiB kept free for a repair */
#define CELLAR_TRASH          ".Trash"

enum {
    CELLAR_F_NONE = 0,
    CELLAR_F_LIMITS,  /* more folders, or a longer path, than the model holds */
    CELLAR_F_TOO_BIG, /* a size or a total past CELLAR_BYTES_MAX */
};

typedef struct {
    char name[CELLAR_NAME + 1];
    int64_t bytes, files;
    int64_t share_x100; /* of the base's bytes (CR2) */
    int64_t disk_x100;  /* of the volume (CR1), -1 until it is measured */
} cellar_folder_t;

typedef struct {
    zxm_link_t *link;
    char base[CELLAR_PATH + 1]; /* "" for the whole Hoard */
    /* CM1 */
    bool measured;
    int64_t total, free, avail;
    int64_t used_x100;
    /* CM2 */
    bool surveyed;
    cellar_folder_t folder[CELLAR_FOLDERS];
    uint32_t nfolders;
    int64_t loose_bytes, loose_files;
    int64_t base_bytes, base_files; /* CR3 over the folders and the loose bytes */
    /* CM3 */
    bool trash_known;
    int64_t trash_items, trash_bytes, trash_x100; /* trash_x100 -1 until measured */
    /* CM4 */
    bool checked;
    int64_t now; /* sys.info's clock, seconds since 1970 */
    uint32_t misnamed, misdated;
    char first_misnamed[CELLAR_PATH + 1], first_misdated[CELLAR_PATH + 1];
    /* CM5 */
    int64_t room;
    bool repair_safe;
    char advice[CELLAR_ADVICE + 1];
    uint32_t fault; /* CELLAR_F_* of the last refusal */
} cellar_t;

void cellar_init(cellar_t *c, zxm_link_t *link);
/* the folder surveyed and checked, a path in the Hoard ("" the Hoard) */
int cellar_set_base(cellar_t *c, const char *base);
int cellar_measure(cellar_t *c);    /* CM1 */
int cellar_survey(cellar_t *c);     /* CM2 */
int cellar_trash_size(cellar_t *c); /* CM3 */
/* CM3: empty the Trash for good, only when approved; *removed is how many items went */
int cellar_empty_trash(cellar_t *c, bool approved, uint32_t *removed);
int cellar_check(cellar_t *c); /* CM4 */
/* CM5: after cellar_measure and cellar_check; approved is the owner's yes */
int cellar_repair_advice(cellar_t *c, bool approved);
/* the folder with this name directly in the base, or -1 */
int32_t cellar_find(const cellar_t *c, const char *name);
/* a name the Hoard's paths can hold (zxv_files.h F2), as CM4 reads it */
bool cellar_name_ok(const char *name);

#endif
