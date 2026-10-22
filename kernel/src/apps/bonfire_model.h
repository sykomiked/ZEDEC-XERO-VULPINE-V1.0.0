/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bonfire_model.h — Bonfire, events and gatherings, as a model (zxm.h).
 * The rules are bonfire_rules.h.
 *
 * zxvd methods used: place.list (the owner's saved places, a venue),
 * pspace.create, pspace.post and pspace.members (a gathering's public
 * space) and social.group_create (its invite-only group). zxvd has no
 * calendar, invitation, RSVP or notification method; none is invented. No
 * drawing, no allocation, integers only.
 *
 *   BM1 VENUES. place.list gives the owner's saved places (200 at most,
 *       zxv_place.h ZPL_PLACES), each with its point in millionths of a
 *       degree and, when where the device is is known, its distance in
 *       metres. A gathering's venue is a place's index, or -1 for none.
 *   BM2 A GATHERING. A name (1..48 bytes of whole UTF-8, pspace.create's
 *       limit), a start (seconds since 1970, 0..99999999999), seats
 *       (1..9999) and a venue; 8 gatherings at most. It is kept only when
 *       it fits its ^ZXBONFIRE record (BR4). zxvd has no calendar method,
 *       so the model holds it.
 *   BM3 THE INVITE LIST. Up to 100 guests a gathering, each a name (1..32
 *       bytes of whole UTF-8, no two alike), with an answer (none yet, yes,
 *       no, maybe), a party (1..99) and when the answer came (ms since
 *       1970). Each line is kept only when it fits its ^ZXBONFIREINV
 *       record (BR5).
 *   BM4 AN ANSWER. A guest's answer is taken only when it fits its
 *       record, and a yes only when the guest may join (BR1): on the list
 *       and the party fits the seats left, the guest's own earlier yes not
 *       counted. Going is the yes parties' sum (BR3), the seats left BR2,
 *       how full BR6 (used_share.used_x100), how many gave each answer
 *       Witness's kind_count.
 *   BM5 THE NOTICE. zxvd has no notification method, so the notice of a
 *       gathering's seats left is only composed; the view shows it.
 *   BM6 OPENING. A gathering opens as a public space (pspace.create: a
 *       room governed by its owner, with the gathering's name and a topic
 *       of 0..24 of [a-z0-9-]) or an invite-only group
 *       (social.group_create). Either makes a record peers keep, so each
 *       is sent only on the owner's word: without it nothing is sent
 *       (ZXM_E_STATE). The CID answered is checked with Seedbank's rule
 *       (seedbank_rules.h SR2) before it is kept; a group's id is 64 hex
 *       digits. Public spaces are the feature "pspace", off by default:
 *       an answer of -32012 marks it off, and the owner turns it on in
 *       settings; Bonfire never does.
 *   BM7 ANNOUNCING. The announcement (the name, the start, the seats left
 *       and the venue) is posted in the gathering's space only when
 *       Meadow's rule lets this node's seat post it (meadow_rules.h MR2:
 *       this node made the room, so its seat is the public's in a park,
 *       Meadow's MM1, and the text is 1..1024 bytes) and only on the
 *       owner's word: a post cannot be deleted (vna_space.h P5), so
 *       without it the model says so (bonfire_t.notice) and sends nothing.
 *   BM8 WHO IS THERE. pspace.members of the gathering's space: its count,
 *       this node included, at most 33 (Meadow's MR4). */
#ifndef BONFIRE_MODEL_H
#define BONFIRE_MODEL_H

#include "bonfire_rules.h"
#include "zxm.h"

#define BONFIRE_PLACES     200u /* place.list: zxv_place.h ZPL_PLACES */
#define BONFIRE_PLACE_NAME 63u  /* zxv_place.h ZPL_NAME_MAX */
#define BONFIRE_NOTE_MAX   255u /* zxv_place.h ZPL_NOTE_MAX */
#define BONFIRE_GATHERINGS 8u
#define BONFIRE_NAME_MAX   48u /* pspace.create's name */
#define BONFIRE_GUEST_NAME 32u
#define BONFIRE_TOPIC_MAX  24u /* pspace.create's topic */
#define BONFIRE_CID_MAX    79u
#define BONFIRE_GROUP_HEX  64u   /* a group id: 32 bytes as hex */
#define BONFIRE_SPACE_MAX  33    /* pspace.members: 32 members and this node */
#define BONFIRE_ANNOUNCE   1024u /* pspace.post's text */
#define BONFIRE_NOTICE_MAX 255u
#define BONFIRE_LAT_MAX    90000000  /* millionths of a degree */
#define BONFIRE_LON_MAX    180000000 /* millionths of a degree */
#define BONFIRE_E_OFF      (-32012)  /* zxvd: the feature is off */

typedef struct {
    char name[BONFIRE_PLACE_NAME + 1];
    int64_t lat, lon;   /* millionths of a degree */
    int64_t distance_m; /* -1 when where the device is is not known */
} bonfire_place_t;

typedef struct {
    char name[BONFIRE_GUEST_NAME + 1];
    int64_t answer; /* BONFIRE_NONE .. BONFIRE_MAYBE */
    int64_t party;  /* 1..99 */
    int64_t stamp;  /* ms since 1970 when the answer came; 0 none yet */
} bonfire_guest_t;

typedef struct {
    char name[BONFIRE_NAME_MAX + 1];
    int64_t start; /* seconds since 1970 */
    int64_t seats, going;
    int64_t venue; /* an index of bonfire_t.place; -1 none */
    bonfire_guest_t guest[BONFIRE_GUESTS];
    uint32_t nguest;
    char cid[BONFIRE_CID_MAX + 1];     /* its public space; "" none */
    char group[BONFIRE_GROUP_HEX + 1]; /* its group; "" none */
    int64_t present;                   /* pspace.members' count; -1 not read */
} bonfire_gathering_t;

typedef struct {
    zxm_link_t *link;
    bool off; /* BM6: the feature "pspace" is off */
    /* BM1 */
    bonfire_place_t place[BONFIRE_PLACES];
    uint32_t nplace;
    bool reset; /* place.list: the saved places were reset */
    /* BM2 */
    bonfire_gathering_t gathering[BONFIRE_GATHERINGS];
    uint32_t ngathering;
    /* BM5, BM7 */
    char notice[BONFIRE_NOTICE_MAX + 1];
} bonfire_t;

/* BM7's words, shown before an announcement is posted */
extern const char BONFIRE_NO_DELETE[];

void bonfire_init(bonfire_t *b, zxm_link_t *link);

/* BM1: place.list into the venues. */
int bonfire_places(bonfire_t *b);

/* BM2: plan a gathering; its index goes to *at. */
int bonfire_plan(bonfire_t *b, const char *name, int64_t start, int64_t seats, int64_t venue,
                 uint32_t *at);

/* BM3: put a guest on a gathering's invite list; the guest's place goes to *at. */
int bonfire_invite(bonfire_t *b, uint32_t g, const char *guest, uint32_t *at);
/* the guest with this name on the list; -1 none */
int32_t bonfire_guest_of(const bonfire_t *b, uint32_t g, const char *guest);

/* BM4: may this guest join now with a party of `party` (BR1)? */
bool bonfire_may_join(const bonfire_t *b, uint32_t g, int32_t guest, int64_t party);
/* BM4: take a guest's answer. ZXM_E_STATE when a yes does not fit (BR1). */
int bonfire_answer(bonfire_t *b, uint32_t g, uint32_t guest, int64_t answer, int64_t party,
                   int64_t stamp);
/* BM4: the seats left (BR2), how full (BR6), how many gave `answer`
 * (witness.kind_count); BONFIRE_REFUSED for no such gathering */
int64_t bonfire_left(const bonfire_t *b, uint32_t g);
int64_t bonfire_full_x100(const bonfire_t *b, uint32_t g);
int64_t bonfire_answered(const bonfire_t *b, uint32_t g, int64_t answer);

/* BM5: the notice of a gathering's seats left. */
int bonfire_notice(bonfire_t *b, uint32_t g);

/* BM6: open a gathering as a public space, or as an invite-only group. */
int bonfire_open_space(bonfire_t *b, uint32_t g, const char *topic, bool owner_confirmed);
int bonfire_open_group(bonfire_t *b, uint32_t g, bool owner_confirmed);

/* BM7: the announcement's text into out (cap bytes), and posting it. */
int bonfire_announcement(const bonfire_t *b, uint32_t g, char *out, uint32_t cap);
int bonfire_announce(bonfire_t *b, uint32_t g, bool owner_confirmed);

/* BM8: pspace.members of the gathering's space. */
int bonfire_present(bonfire_t *b, uint32_t g);

#endif
