/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hamlet_model.h — Hamlet, your social circle's feed, as a model (zxm.h):
 * the circle's posts, replies, reactions and groups, and gifts to a member.
 * Hamlet's own rules are hamlet_rules.h; a group's unread posts are
 * Commons's (commons_rules.h CR3) and a gift's consent is Ramble's
 * (ramble_rules.h RR1), used as they are.
 *
 * zxvd methods used: social.groups, social.join, social.leave,
 * social.feed, social.post, social.react, web4.post and web4.consent. No
 * drawing, no allocation, integers only.
 *
 *   HM1 GROUPS. social.groups is kept as the list of groups, each with the
 *       seat's role; a group keeps its read marker and its mute across
 *       refreshes, matched by its id. Joining and leaving go through
 *       social.join and social.leave, and the owner of a group never
 *       leaves it (zxvd refuses that too).
 *   HM2 THE DAEMON'S RANKING. social.feed's items are kept in the order zxvd
 *       gives them; nothing here re-ranks them by reactions, replies or
 *       score.
 *   HM3 SHOW THIS POST TO THIS SEAT. Each item of the feed is shown only
 *       when HR2 holds: the seat is a member of the item's group (a post to
 *       no group is open to every seat) and none of its mutes covers it
 *       (its author, its group).
 *   HM4 UNREAD AND READ MARKERS. A group's newest counts the shown posts by
 *       others known there, its seen the ones the owner has read; unread is
 *       Commons's room_unread. A post is new when its Lamport time is past
 *       the group's read marker. Marking a group read moves the marker to
 *       the newest Lamport time held, and only when the marker makes its
 *       ^ZXHAMLET record (HR3). zxvd has no mute method, so mutes (of an
 *       author or a group) are the model's own, bounded.
 *   HM5 GIFTS ARE THE OWNER'S ACT. A gift is a Web4 envelope (web4.post,
 *       tool "gift") carrying amount_minor, which zxvd holds unsigned until
 *       web4.consent answers it. It is posted only on the owner's confirmed
 *       word, to a member's 32-byte agent id, for an amount of 1 up to the
 *       owner's gift ceiling (HR1); the ceiling is set only on the owner's
 *       word and starts at 0, so no gift goes before the owner sets one.
 *       The held gift is released (grant) or dropped (deny) by
 *       web4.consent, again only on the owner's word, and a grant is asked
 *       only when Ramble's consent_covers holds for it: the money scope,
 *       the ceiling's room after the amount, and an expiry of 1..86400
 *       seconds. zxvd has no Web4 delivery: nothing reaches the receiver,
 *       and their consent cannot be asked for here.
 *   HM6 NOTICES. zxvd has no notification method, so the model composes
 *       the notice of unread posts only; the view shows it. */
#ifndef HAMLET_MODEL_H
#define HAMLET_MODEL_H

#include "commons_rules.h"
#include "hamlet_rules.h"
#include "ramble_rules.h"
#include "zxm.h"

#define HAMLET_HEX_ID     64u /* a record id, group id, NodeID or agent id: 32 bytes as hex */
#define HAMLET_FEED_MAX   50u /* social.feed: at most 50 */
#define HAMLET_GROUPS     32u
#define HAMLET_MUTES      32u
#define HAMLET_REACTS     8u    /* reactions kept on one item */
#define HAMLET_NAME_MAX   64u   /* a group's name */
#define HAMLET_NOTE_MAX   2048u /* a gift's note: web4.post's payload */
#define HAMLET_NOTICE_MAX 255u
#define HAMLET_GRANT_MAX  86400 /* web4.consent's longest expires_in */

enum { HAMLET_POST, HAMLET_REPLY };
enum { HAMLET_GIFT_NONE, HAMLET_GIFT_HELD, HAMLET_GIFT_SIGNED, HAMLET_GIFT_DENIED };

typedef struct {
    char reaction[COMMONS_REACT_MAX + 1];
    uint32_t count;
    bool mine;
} hamlet_reaction_t;

typedef struct {
    char id[HAMLET_HEX_ID + 1], author[HAMLET_HEX_ID + 1];
    char parent[HAMLET_HEX_ID + 1]; /* "" none */
    char group[HAMLET_HEX_ID + 1];  /* "" a post to no group */
    bool mine;
    uint8_t kind; /* HAMLET_POST, HAMLET_REPLY */
    int64_t lamport;
    char text[COMMONS_TEXT_MAX + 1];
    uint32_t replies;
    hamlet_reaction_t react[HAMLET_REACTS];
    uint32_t nreact;
    bool shown; /* HM3 */
} hamlet_post_t;

typedef struct {
    char id[HAMLET_HEX_ID + 1], name[HAMLET_NAME_MAX + 1];
    uint8_t role; /* HAMLET_ROLE_* */
    bool open;    /* anyone may join */
    bool muted;   /* HM4: the owner's own mute */
    int64_t members;
    int64_t newest, seen; /* HM4 */
    int64_t mark;         /* the read marker: the last Lamport time read */
    int64_t top;          /* the newest Lamport time held of a post by others */
    int64_t stamp;        /* when the marker last moved, seconds since 1970 */
} hamlet_group_t;

typedef struct {
    uint32_t id; /* its feed entry; 0 none */
    char to[HAMLET_HEX_ID + 1];
    int64_t amount;
    uint8_t status; /* HAMLET_GIFT_* */
} hamlet_gift_t;

typedef struct {
    zxm_link_t *link;
    char me[HAMLET_HEX_ID + 1];
    /* HM2, HM3 */
    hamlet_post_t feed[HAMLET_FEED_MAX];
    uint32_t nfeed, nshown;
    /* HM1, HM4 */
    hamlet_group_t group[HAMLET_GROUPS];
    uint32_t ngroup;
    char muted[HAMLET_MUTES][HAMLET_HEX_ID + 1];
    uint32_t nmuted;
    /* HM5 */
    int64_t ceiling;
    hamlet_gift_t gift;
    /* HM6 */
    char notice[HAMLET_NOTICE_MAX + 1];
} hamlet_t;

void hamlet_init(hamlet_t *h, zxm_link_t *link);

/* HM1: social.groups into the group list; the group with this id, -1 none. */
int hamlet_groups(hamlet_t *h);
int32_t hamlet_find_group(const hamlet_t *h, const char *id);
/* HM1: join or leave a held group. */
int hamlet_join(hamlet_t *h, const char *group);
int hamlet_leave(hamlet_t *h, const char *group);

/* HM2-HM4: social.feed (limit 1..50), in zxvd's order, each item's shown
 * decided (HR2) and every group's newest moved on. */
int hamlet_feed(hamlet_t *h, uint32_t limit);
/* HM3: whether item i of the feed is shown to the seat now (HR2). */
bool hamlet_shown(const hamlet_t *h, uint32_t i);

/* A post or reply (reply_to NULL or a record id) to a group the owner is a
 * member of (group = its place in the list), or to no group (group -1). */
int hamlet_post(hamlet_t *h, int32_t group, const char *text, const char *reply_to);
/* A reaction of 1..32 bytes to a held post or reply. */
int hamlet_react(hamlet_t *h, const char *post, const char *reaction);

/* HM4: mute or unmute an author (a NodeID) or a group; shown is decided
 * again on the feed held. */
int hamlet_mute_author(hamlet_t *h, const char *author, bool mute);
int hamlet_mute_group(hamlet_t *h, uint32_t group, bool mute);
/* HM4: a group's unread posts (CR3), HAMLET_REFUSED for no group; marking
 * it read at now (seconds since 1970) moves its marker (HR3). */
int64_t hamlet_unread(const hamlet_t *h, uint32_t group);
int hamlet_mark_read(hamlet_t *h, uint32_t group, int64_t now);
/* HM6: the notice of every group's unread posts. */
int hamlet_notice(hamlet_t *h);

/* HM5: the owner's gift ceiling (0..HAMLET_MINOR_MAX minor units), and a
 * gift of amount to the member with this agent id, with a note of at most
 * 2048 bytes; now is unix seconds, or -1 for zxvd's clock. */
int hamlet_set_ceiling(hamlet_t *h, int64_t ceiling, bool owner_confirmed);
int hamlet_gift(hamlet_t *h, const char *to, int64_t amount, const char *note, int64_t now,
                bool owner_confirmed);
/* HM5: the owner's answer to the held gift: grant it for expires_in
 * (1..86400) seconds, or deny it. */
int hamlet_gift_answer(hamlet_t *h, bool grant, int64_t expires_in, int64_t now,
                       bool owner_confirmed);

#endif
