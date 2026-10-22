/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* meadow_model.h — Meadow, the park, as a model (zxm.h): find, join and use
 * public spaces on the peer network. The rules are meadow_rules.h.
 *
 * zxvd methods used: spaces.list (the rights record: each space's kind and
 * this node's role), pspace.find, pspace.join, pspace.leave, pspace.list,
 * pspace.members, pspace.events and pspace.post. No drawing, no
 * allocation, integers only.
 *
 *   MM1 THE RIGHTS RECORD. spaces.list names every space this node owns or
 *       was invited into, with its kind, this node's role and, for a
 *       published cloud top or park, the pspace CID it lives under. A
 *       public space with no such entry was found or joined by its CID
 *       alone, so its seat is the public's in a park (the default taken:
 *       nobody invited this node there, and anyone may find it).
 *   MM2 FIND. pspace.find walks a directory topic (1..24 of [a-z0-9-]);
 *       the first call starts a search, the next ones read on until done.
 *       Every CID found is checked as text with Seedbank's rule
 *       (seedbank_rules.h SR2) before it is kept.
 *   MM3 JOINED SPACES. pspace.list names the spaces this node is in or
 *       joining (8 at most); each must fit its ^ZXMEADOW record (MR5)
 *       before the list is kept, and a space keeps its last event index
 *       read across refreshes, matched by CID. The people across them are
 *       MR4's sum.
 *   MM4 EVENTS. pspace.events from the last index read: the new ones are
 *       MR3's count; past the 32 zxvd keeps, the rest were missed and the
 *       model says how many.
 *   MM5 MAY THIS SEAT POST HERE. A post goes out only when the seat passes
 *       MR2 (the kind's rights and 1..1024 bytes of whole UTF-8), the
 *       network is up and the space is joined ("in").
 *   MM6 POSTS CANNOT BE DELETED. pspace.post has no delete, ban or
 *       silence (vna_space.h P5): before each send the model says so
 *       (meadow_t.notice) and sends nothing; it sends only once the owner
 *       has seen that notice and confirms, and the next post is warned
 *       again.
 *   MM7 THE SWITCH. Public spaces are the feature "pspace", off by
 *       default: an answer of -32012 marks it off, and the owner turns it
 *       on in settings; Meadow never turns it on itself.
 *   MM8 NOTICES. zxvd has no notification method, so the notice of new
 *       events is only composed; the view shows it. */
#ifndef MEADOW_MODEL_H
#define MEADOW_MODEL_H

#include "meadow_rules.h"
#include "zxm.h"

#define MEADOW_PLACES     32u /* spaces.list entries kept */
#define MEADOW_SPACES     8u  /* pspace.list: 8 at most */
#define MEADOW_FOUND      16u /* pspace.find: 16 at most */
#define MEADOW_EVENTS     32u /* pspace.events: the last 32 at most */
#define MEADOW_MEMBERS    32u /* pspace.members: 32 at most besides this node */
#define MEADOW_CID_MAX    79u
#define MEADOW_ID_MAX     48u /* a space id, zxv_files.h ZXF_SPACE_ID_MAX */
#define MEADOW_PLACE_NAME 80u /* a space's name, zxv_spaces.h ZXS_NAME_MAX */
#define MEADOW_NAME_MAX   48u /* a public space's name, vna_space.h */
#define MEADOW_TOPIC_MAX  24u
#define MEADOW_HEX_ID     64u /* a NodeID: 32 bytes as hex */
#define MEADOW_ADDR_MAX   31u
#define MEADOW_NOTICE_MAX 255u
#define MEADOW_E_OFF      (-32012) /* zxvd: the feature is off */

enum { MEADOW_ROOM = 1, MEADOW_HALL = 2 }; /* a public space's kind */
enum { MEADOW_SEARCHING = 1, MEADOW_JOINING, MEADOW_IN, MEADOW_NOT_FOUND };

typedef struct {
    char id[MEADOW_ID_MAX + 1];
    char name[MEADOW_PLACE_NAME + 1];
    uint8_t kind, role;           /* MEADOW_HOME .., MEADOW_OWNER .. */
    bool write;                   /* what zxvd itself says of this seat */
    char cid[MEADOW_CID_MAX + 1]; /* "" when not published */
} meadow_place_t;

typedef struct {
    char cid[MEADOW_CID_MAX + 1];
    char name[MEADOW_NAME_MAX + 1], topic[MEADOW_TOPIC_MAX + 1];
    uint8_t kind;  /* MEADOW_ROOM, MEADOW_HALL; 0 before its genesis is known */
    uint8_t phase; /* MEADOW_SEARCHING .. MEADOW_NOT_FOUND */
    int64_t members;
    int64_t last;                 /* the last event index read (MR3's since) */
    int64_t stamp;                /* when that event was made, ms since 1970 */
    uint8_t seat_kind, seat_role; /* MM1 */
    bool warned;                  /* MM6: the owner has seen that posts stay */
} meadow_space_t;

typedef struct {
    char cid[MEADOW_CID_MAX + 1];
    char name[MEADOW_NAME_MAX + 1];
    uint8_t kind;
    int64_t members;
    char host[MEADOW_HEX_ID + 1];
} meadow_found_t;

typedef struct {
    int64_t index, lamport, created_ms;
    bool post; /* false: a watch-hall change */
    char author[MEADOW_HEX_ID + 1];
    char text[MEADOW_POST_MAX + 1];
} meadow_event_t;

typedef struct {
    char id[MEADOW_HEX_ID + 1];
    char addr[MEADOW_ADDR_MAX + 1];
    int64_t last_seen_ms;
} meadow_member_t;

typedef struct {
    zxm_link_t *link;
    bool off;    /* MM7 */
    bool online; /* pspace.list: the network is up */
    /* MM1 */
    meadow_place_t place[MEADOW_PLACES];
    uint32_t nplace;
    /* MM3 */
    meadow_space_t space[MEADOW_SPACES];
    uint32_t nspace;
    int64_t people; /* MR4 */
    /* MM2 */
    char topic[MEADOW_TOPIC_MAX + 1];
    bool find_done;
    meadow_found_t found[MEADOW_FOUND];
    uint32_t nfound;
    /* MM4 */
    meadow_event_t event[MEADOW_EVENTS];
    uint32_t nevent;
    int64_t fresh, missed; /* the last read: new events, and those past the 32 kept */
    meadow_member_t member[MEADOW_MEMBERS];
    uint32_t nmember;
    /* MM6, MM8 */
    char notice[MEADOW_NOTICE_MAX + 1];
} meadow_t;

/* MM6's words, shown before the first post to a space */
extern const char MEADOW_NO_DELETE[];

void meadow_init(meadow_t *m, zxm_link_t *link);

/* MM1: spaces.list into the rights record. */
int meadow_places(meadow_t *m);
/* MM2: pspace.find, started or read on. */
int meadow_find(meadow_t *m, const char *topic);
/* MM3: pspace.list into the joined spaces. */
int meadow_spaces(meadow_t *m);
/* The joined space with this CID; -1 none. */
int32_t meadow_space_of(const meadow_t *m, const char *cid);
/* pspace.join a space by its CID, or leave one joined. */
int meadow_join(meadow_t *m, const char *cid);
int meadow_leave(meadow_t *m, uint32_t space);
/* pspace.members of a joined space (m->member, its members count). */
int meadow_members(meadow_t *m, uint32_t space);
/* MM4: the events after the last read (m->event, m->fresh, m->missed). */
int meadow_events(meadow_t *m, uint32_t space);

/* MM5: may this node's seat post `bytes` bytes in the space now? */
bool meadow_may_post(const meadow_t *m, uint32_t space, int64_t bytes);
/* MM5, MM6: post text. Until the notice that posts cannot be deleted has
 * been shown for this post, only the notice is given (ZXM_E_STATE, nothing
 * sent); after it, owner_confirmed sends, and anything else gives the
 * notice again. */
int meadow_post(meadow_t *m, uint32_t space, const char *text, bool owner_confirmed);

/* MM8: the notice of the last read's new events. */
int meadow_notice(meadow_t *m, uint32_t space);

#endif
