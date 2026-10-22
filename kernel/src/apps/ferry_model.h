/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ferry_model.h — Ferry, move to a new machine, as a model (zxm.h). No
 * drawing, no allocation, integers only; zxvd only through the link.
 *
 *   FM1 THE CARRIER. dest is a folder both machines reach (a disk carried
 *       over, or a share the new machine mounts): an absolute path outside
 *       the Hoard, where Keepsake keeps its backups (backup.*, K1).
 *   FM2 THE SURVEY. spaces.list gives every space; the ones this node owns
 *       (home among them) are walked with files.list, at most FERRY_DIRS
 *       folders each, for their bytes and files; the ones it joined travel
 *       as their membership only (their files live with their owner).
 *       features.get gives the settings. net.peers lists the nodes the
 *       owner may name as the target (none while networking is off).
 *   FM3 THE PLAN. Spaces and settings the owner keeps on the plan (all, at
 *       first). Its totals are banker.total over the carried spaces (FR4),
 *       each space's share banker.share (FR5). Home is always carried: the
 *       plan itself rides in it, as FERRY_PLAN_FILE.
 *   FM4 THE TARGET. avail is what the target may still write: sys.disk's
 *       avail read there (ferry_measure, on the new machine), or the figure
 *       the owner reads off it (ferry_set_avail). spare is kept free
 *       (FERRY_SPARE unless the owner sets it). room is FR1, fits FR2;
 *       nothing is packed unless it fits.
 *   FM5 PACKING. Each carried owned space is backed up to dest
 *       (backup.run) and read back whole (backup.verify); then the plan
 *       file is written and home is packed last; progress x100 is FR6 over the plan's bytes and the
 * bytes not packed yet (over the spaces when the plan has no bytes), and the link's progress hears
 * every step. A check that does not read back whole stops the packing (FERRY_F_CHECK). FM6 ARRIVING
 * (on the new machine, the same dest). ferry_load_plan restores the plan from home's newest backup
 * and reads it; ferry_arrive makes each carried space again (spaces.create for one this node owned,
 * spaces.join for one it joined, kept when spaces.get finds it) and restores into FERRY_ARRIVED
 * there each owned space's backup that packed it (home's newest, which holds the plan); a space
 * whose backup holds no files is only made again. The plan is written just before home is backed
 * up, so it names every other space's backup and how many files it holds. FM7 SETTINGS.
 * ferry_apply_settings sets each carried feature switch as it was (features.set). A regulated one
 * moves only with the owner's approval (FR3, kind 4): without it, it is held and counted, never
 *       set. Advisory: the owner confirms.
 *   FM8 GATHERING. A loose file is copied into a carried space first
 *       (files.cross); a crossing into a more open space is held by zxvd
 *       and done only when the owner confirms its ticket.
 *
 * Missing from zxvd, so not done (none is invented): exporting or
 * importing a key (keys are not carried; FR3 already says a key needs the
 * owner's approval), reading a peer's disk (the target's room is read on
 * the target), and sending a whole space to a peer (the carrier is a
 * folder both machines reach). */
#ifndef FERRY_MODEL_H
#define FERRY_MODEL_H

#include "ferry_rules.h"
#include "zxm.h"

#define FERRY_SPACES    16u
#define FERRY_SETTINGS  32u
#define FERRY_PEERS     16u
#define FERRY_DIRS      512u /* folders walked in one space */
#define FERRY_DEST      1024u
#define FERRY_PATH      1024u
#define FERRY_NAME      80u /* spaces.create's name */
#define FERRY_WHO       64u /* spaces.join's owner */
#define FERRY_FEATURE   23u /* features.set's name */
#define FERRY_NODE      64u /* a NodeID in hex */
#define FERRY_PLAN_MAX  12288u
#define FERRY_SPARE     1073741824LL /* 1 GiB kept free when the owner gives none */
#define FERRY_PLAN_DIR  "Ferry"
#define FERRY_PLAN_FILE "Ferry/plan.txt"
#define FERRY_PLAN_IN   "Ferry/arrived-plan.txt"
#define FERRY_ARRIVED   "Arrived"

enum {
    FERRY_F_NONE = 0,
    FERRY_F_LIMITS,  /* more spaces, settings or folders than the model holds */
    FERRY_F_TOO_BIG, /* a size or a total past FERRY_BYTES_MAX */
    FERRY_F_NO_ROOM, /* the plan does not fit on the target (FR2) */
    FERRY_F_CHECK,   /* a backup did not read back whole */
    FERRY_F_PLAN,    /* a plan file that does not parse */
    FERRY_F_SPACE,   /* a space made again under another id */
};

typedef struct {
    char id[ZXM_SPACE_MAX + 1]; /* "home" for home */
    char kind[10];              /* home, office, team, cloudtop, park */
    char name[FERRY_NAME + 1];
    char owner[FERRY_WHO + 1]; /* "me", or who invited this node */
    char role[8];              /* owner, member, guest */
    int64_t bytes, files;      /* 0 for a joined space */
    int64_t share_x100;        /* of the plan's bytes (FR5); 0 off the plan */
    bool carry;                /* on the plan */
    bool packed;               /* backed up to dest and read back whole */
    bool arrived;              /* made again (and restored) on the target */
    uint32_t snapshot;         /* the backup that packed it (0 for home in the plan) */
    uint32_t snapfiles;        /* the files that backup holds */
} ferry_space_t;

typedef struct {
    char name[FERRY_FEATURE + 1];
    bool enabled, regulated, carry;
} ferry_setting_t;

typedef struct {
    char id[FERRY_NODE + 1];
    char addr[24];
} ferry_peer_t;

typedef struct {
    zxm_link_t *link;
    char dest[FERRY_DEST + 1]; /* "" until the owner picks one (FM1) */
    ferry_space_t space[FERRY_SPACES];
    uint32_t nspaces;
    ferry_setting_t setting[FERRY_SETTINGS];
    uint32_t nsettings;
    ferry_peer_t peer[FERRY_PEERS];
    uint32_t npeers;
    char target[FERRY_NODE + 1]; /* the node the owner names as the target, or "" */
    int64_t plan_bytes, plan_files;
    int64_t avail; /* -1 until measured (FM4) */
    int64_t spare;
    int64_t room; /* FR1, FERRY_NO_ROOM until measured */
    bool fits;    /* FR2 */
    int64_t packed_bytes;
    int64_t progress_x100;  /* FR6 */
    uint32_t applied, held; /* settings set, and regulated ones held (FM7) */
    uint32_t restored;      /* files restored on arrival */
    uint32_t fault;         /* FERRY_F_* of the last refusal */
} ferry_t;

void ferry_init(ferry_t *f, zxm_link_t *link);
int ferry_set_dest(ferry_t *f, const char *dest); /* an absolute path */
int ferry_set_spare(ferry_t *f, int64_t spare);   /* 0..FERRY_BYTES_MAX */
int ferry_set_avail(ferry_t *f, int64_t avail);   /* read off the target */
int ferry_measure(ferry_t *f);                    /* sys.disk, here (on the target) */

/* FM2: the spaces, their sizes, the settings and the peers, afresh */
int ferry_survey(ferry_t *f);
int ferry_peers(ferry_t *f);
int ferry_set_target(ferry_t *f, uint32_t peer); /* one of the peers listed */
/* FM3: keep a space or a setting on the plan, or leave it out (home stays) */
int ferry_choose_space(ferry_t *f, uint32_t i, bool carry);
int ferry_choose_setting(ferry_t *f, uint32_t i, bool carry);
/* the totals, the shares, the room and the fit again (FM3, FM4) */
int ferry_replan(ferry_t *f);
/* the space with this id, or -1 */
int32_t ferry_find(const ferry_t *f, const char *id);

/* FM8: copy a file into a carried space; *ticket is 0 when done at once,
 * else the held crossing's ticket for ferry_gather_confirm */
int ferry_gather(ferry_t *f, const char *from_space, const char *path, const char *to_space,
                 const char *to, uint32_t *ticket);
int ferry_gather_confirm(ferry_t *f, uint32_t ticket, const char *to_space);

/* The plan as text (one line per item, tab-separated), and back */
int32_t ferry_plan_text(const ferry_t *f, char *out, uint32_t cap);
int ferry_plan_parse(ferry_t *f, const char *text, uint32_t n);

int ferry_pack(ferry_t *f);                          /* FM5 */
int ferry_load_plan(ferry_t *f);                     /* FM6 */
int ferry_arrive(ferry_t *f);                        /* FM6 */
int ferry_apply_settings(ferry_t *f, bool approved); /* FM7 */

#endif
