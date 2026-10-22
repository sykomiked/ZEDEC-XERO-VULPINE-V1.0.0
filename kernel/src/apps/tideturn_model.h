/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tideturn_model.h — Tideturn, software updates, as a model (zxm.h): what
 * version runs (zxvd.version), one signed update check (update.check), an
 * offered release judged by its version and its two signatures, the
 * owner's word on it, and an audit trail of all of it. The rules are
 * tideturn_rules.h.
 *
 * zxvd methods used: zxvd.version, update.check and sys.info. No drawing,
 * no allocation, integers only.
 *
 *   TM1 THE RUNNING VERSION is zxvd's app version, X.Y.Z with each part
 *       0..999 and no leading zeros (zx_upcheck.h's manifest form). An app
 *       version in any other form is kept as text and is not known: no
 *       offer is judged against it (ZXM_E_STATE). libzxv's version must be
 *       in that form, or the answer is not zxvd's (ZXM_E_PROTOCOL).
 *   TM2 A CHECK asks zxvd for one signed update check, which verifies its
 *       buckets itself (IPNS record and manifest signatures). Its status
 *       text is kept and classed (tideturn_class); a status zxvd does not
 *       give is OTHER, an empty one is not an answer. Nothing is installed.
 *   TM3 AN OFFER is a release the view shows from the update listing: its
 *       version, the oldest version it applies over (min-version) and the
 *       result of verifying each of its two signatures (1 verified, 0 not).
 *       It is offered only when it is dual-signed (TR2), newer than the
 *       running version (TR1) and the running version is at least its
 *       min-version; otherwise it is refused and says why, the first that
 *       applies: unsigned, not newer, needs a newer installed version.
 *   TM4 THE OWNER'S WORD: an offered release waits for the owner, who
 *       approves or declines it once (tideturn_confirm). zxvd has no method
 *       that installs an update, so approving records the word and tells
 *       the owner where to install it; nothing is installed here.
 *   TM5 THE TRAIL: every check, offer and word is a ^ZXTIDE record (TR3)
 *       stamped with zxvd's clock (sys.info), linked to the record before it
 *       (witness.chain_ok, WR1), up to TIDETURN_KEEP records, the newest
 *       kept. A record that would not fit is not kept and the action is
 *       refused (ZXM_E_ARG). */
#ifndef TIDETURN_MODEL_H
#define TIDETURN_MODEL_H

#include "tideturn_rules.h"
#include "zxm.h"

#define TIDETURN_KEEP    64u  /* records in the trail */
#define TIDETURN_TEXT    63u  /* a version or status text */
#define TIDETURN_SUMMARY 383u /* the notice */

/* what update.check said (TM2) */
enum {
    TIDETURN_ST_NONE = 0,     /* not checked */
    TIDETURN_ST_CURRENT = 1,  /* up to date */
    TIDETURN_ST_NEWER = 2,    /* update available */
    TIDETURN_ST_NEEDS = 3,    /* update needs a newer installed version */
    TIDETURN_ST_UNSIGNED = 4, /* unsigned bucket contents */
    TIDETURN_ST_RECORD = 5,   /* the IPNS record is invalid, expired or older */
    TIDETURN_ST_MANIFEST = 6, /* the signed manifest is malformed or older */
    TIDETURN_ST_OFFLINE = 7,  /* no network, no clock, disabled, not due, no buckets */
    TIDETURN_ST_OTHER = 8
};

/* a record's outcome (TM5) */
enum {
    TIDETURN_CHECKED = 1,      /* an update check ran */
    TIDETURN_OFFERED = 2,      /* dual-signed, newer and applicable: waits for the owner */
    TIDETURN_NO_SIGNATURE = 3, /* refused: not dual-signed */
    TIDETURN_NOT_NEWER = 4,    /* refused: not newer than the running version */
    TIDETURN_TOO_OLD = 5,      /* refused: the running version is under min-version */
    TIDETURN_APPROVED = 6,     /* the owner's yes */
    TIDETURN_DECLINED = 7      /* the owner's no */
};

typedef struct {
    int64_t major, minor, patch;
} tideturn_version_t;

typedef struct {
    int64_t seq, prev;
    tideturn_version_t v; /* running (a check) or offered (an offer, a word) */
    int64_t sig;          /* 1: the signatures verified (zxvd's check, or both of an offer) */
    int64_t outcome;
    int64_t stamp; /* ms since 1970, zxvd's clock */
} tideturn_record_t;

typedef struct {
    zxm_link_t *link;
    /* zxvd.version (TM1) */
    bool known;
    tideturn_version_t running, lib;
    char app[TIDETURN_TEXT + 1];
    /* update.check (TM2) */
    uint32_t status;
    char status_text[TIDETURN_TEXT + 1];
    /* the offer (TM3, TM4) */
    bool pending;
    tideturn_version_t offer, min;
    int64_t firstsig, secondsig;
    int64_t verdict; /* the last offer's outcome, 0 none */
    /* the trail (TM5) */
    tideturn_record_t rec[TIDETURN_KEEP];
    uint32_t n;
    int64_t next_seq;
    char notice[TIDETURN_SUMMARY + 1];
} tideturn_t;

/* TM1: X.Y.Z, each part 0..999 without leading zeros; 0 or ZXM_E_ARG. */
int tideturn_parse(const char *s, tideturn_version_t *v);
/* TR1 over two parsed versions. */
int64_t tideturn_order(const tideturn_version_t *a, const tideturn_version_t *b);
/* TM2: the class of a status text update.check gives. */
uint32_t tideturn_class(const char *status);

void tideturn_init(tideturn_t *t, zxm_link_t *link);
/* TM1: ask zxvd what version runs. */
int tideturn_read_version(tideturn_t *t);
/* TM2: run one signed update check; nothing is installed. */
int tideturn_check(tideturn_t *t);
/* TM3: judge an offered release; ZXM_E_STATE while the running version is
 * not known or another offer waits, ZXM_E_ARG on a version that does not
 * parse. The outcome is t->verdict. */
int tideturn_offer(tideturn_t *t, const char *release, const char *min_version, int64_t firstsig,
                   int64_t secondsig);
/* TM4: the owner's word on the waiting offer; ZXM_E_STATE when none waits. */
int tideturn_confirm(tideturn_t *t, bool approve);
/* TM5: the first record not linked to the one before it; t->n when intact. */
uint32_t tideturn_trail(const tideturn_t *t);
/* TR3 on record i of the trail (false past the end). */
bool tideturn_record_ok(const tideturn_t *t, uint32_t i);

#endif
