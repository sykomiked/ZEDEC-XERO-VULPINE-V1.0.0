/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* commons_model.h — Commons, chat, as a model (zxm.h): rooms on this node,
 * groups, rooms across nodes and the IRC bridge. The rules are
 * commons_rules.h.
 *
 * zxvd methods used: chat.me, chat.post, chat.react, chat.follow,
 * chat.feed, chat.thread, social.groups, social.group_create, social.join,
 * social.leave, social.post, pspace.create, pspace.join, pspace.list,
 * pspace.post, pspace.events, pspace.members, irc.status, irc.bridge and
 * irc.record. No drawing, no allocation, integers only.
 *
 *   CM1 ROOMS. The model keeps one list of rooms: room 0 is this node's
 *       chat, then the groups social.groups holds and the spaces
 *       pspace.list names. Every room fits its ^ZXCOMMONS record (CR4)
 *       before it is kept; a refresh whose answer does not fit keeps the
 *       list before. A room keeps its read count across refreshes, matched
 *       by its group id or space CID.
 *   CM2 THE DAEMON'S RANKING. chat.feed's posts are kept in the order zxvd
 *       gives them; nothing here re-ranks them by likes, replies or views.
 *       Every post fits its ^ZXCOMMONSMSG record (CR5) before the page is
 *       kept.
 *   CM3 MAY I POST HERE. A post goes out only when the room's three flags
 *       pass CR1 (member, joined, switch on) and its text CR2 with
 *       something besides spaces; a reaction only when it is 1..32 bytes.
 *       zxvd checks the same again on its side.
 *   CM4 UNREAD. A room's newest counts the posts by others known there,
 *       its seen the ones the owner has read; unread is CR3. On this node
 *       a post is new when its Lamport time is past the last one read; in
 *       a space, the new events (pspace.events after the last index) are
 *       coded 0 mine, 1 a post, 2 a watch-hall change and counted with
 *       Witness's kind_count (WR2). Groups have no feed among the methods
 *       Commons uses, so a group's unread stays 0.
 *   CM5 SPACES. pspace.create makes a room (kind "room", the owner
 *       governing); a CID is checked as text (b + base32) before it is
 *       sent.
 *   CM6 THE BRIDGE IS THE OWNER'S ACT. irc.bridge carries local lines to a
 *       public network, so it is called only on the owner's confirmed word,
 *       for a host, port, two channels and a nick inside zxvd's limits;
 *       #swarm is never bridged. The network's password is passed on and
 *       never kept.
 *   CM7 NOTICES. zxvd has no notification method, so the model composes
 *       the notice of unread posts only; the view shows it.
 *
 * zxvd has no direct-message method (a private post to one NodeID) and no
 * exchange of chat records with peers yet; none is invented. */
#ifndef COMMONS_MODEL_H
#define COMMONS_MODEL_H

#include "commons_rules.h"
#include "zxm.h"

#define COMMONS_HEX_ID     64u /* a record id or NodeID: 32 bytes as hex */
#define COMMONS_FEED_MAX   50u /* chat.feed: at most 50 */
#define COMMONS_THREAD_MAX 32u /* replies kept of one thread */
#define COMMONS_REACTS     8u  /* reactions kept on one post */
#define COMMONS_FOLLOW_MAX 64u
#define COMMONS_GROUPS     32u
#define COMMONS_SPACES     8u /* pspace.list: at most 8 */
#define COMMONS_ROOMS      (1u + COMMONS_GROUPS + COMMONS_SPACES)
#define COMMONS_EVENTS     32u /* pspace.events: the last 32 at most */
#define COMMONS_MEMBERS    64u
#define COMMONS_CID_MAX    79u /* a space CID */
#define COMMONS_NAME_MAX   64u /* a group's name (a space's is 48) */
#define COMMONS_SPACE_NAME 48u
#define COMMONS_TOPIC_MAX  24u
#define COMMONS_HOST_MAX   255u
#define COMMONS_CHAN_MAX   50u /* an IRC channel name */
#define COMMONS_NICK_MAX   30u
#define COMMONS_PASS_MAX   79u
#define COMMONS_ADDR_MAX   63u
#define COMMONS_STATE_MAX  15u
#define COMMONS_ERROR_MAX  127u
#define COMMONS_NOTICE_MAX 255u

enum { COMMONS_ROOM_HERE, COMMONS_ROOM_GROUP, COMMONS_ROOM_SPACE };
enum { COMMONS_POST, COMMONS_REPLY, COMMONS_REACTION };
enum {
    COMMONS_ROLE_NONE,
    COMMONS_ROLE_OBSERVER,
    COMMONS_ROLE_MEMBER,
    COMMONS_ROLE_ADMIN,
    COMMONS_ROLE_OWNER
};
enum { COMMONS_SEARCHING, COMMONS_JOINING, COMMONS_IN, COMMONS_NOT_FOUND };
enum { COMMONS_EV_MINE, COMMONS_EV_POST, COMMONS_EV_HALL }; /* CM4's codes */

typedef struct {
    char emoji[COMMONS_REACT_MAX + 1];
    uint32_t count;
    bool mine;
} commons_reaction_t;

typedef struct {
    char id[COMMONS_HEX_ID + 1], author[COMMONS_HEX_ID + 1];
    bool mine;
    uint8_t kind;
    int64_t lamport, wall_ms;
    char text[COMMONS_TEXT_MAX + 1];
    char reply_to[COMMONS_HEX_ID + 1]; /* "" none */
    uint32_t replies;
    commons_reaction_t react[COMMONS_REACTS];
    uint32_t nreact;
} commons_post_t;

typedef struct {
    uint8_t kind;                  /* COMMONS_ROOM_* */
    char key[COMMONS_CID_MAX + 1]; /* a group id or a space CID; "" this node */
    char name[COMMONS_NAME_MAX + 1];
    int64_t newest, seen; /* CM4 */
    int64_t mark;         /* this node: the last Lamport time read */
    int64_t next; /* a space: pspace.events' since; this node: the newest Lamport time held */
    int64_t members;
    uint8_t role;  /* a group: COMMONS_ROLE_* */
    uint8_t phase; /* a space: COMMONS_SEARCHING .. COMMONS_NOT_FOUND */
    bool open;     /* a group anyone may join */
} commons_room_t;

typedef struct {
    int64_t index, lamport, created_ms;
    uint8_t code; /* COMMONS_EV_* */
    char author[COMMONS_HEX_ID + 1];
    char text[COMMONS_TEXT_MAX + 1];
} commons_event_t;

typedef struct {
    bool on, listening, available;
    int64_t port, clients, delivered, refused;
    bool bridged;
    char state[COMMONS_STATE_MAX + 1]; /* the bridge's, "" none */
    char host[COMMONS_HOST_MAX + 1], remote[COMMONS_CHAN_MAX + 1], local[COMMONS_CHAN_MAX + 1];
    char nick[COMMONS_NICK_MAX + 1];
    int64_t sent, received, dropped, queued;
    char error[COMMONS_ERROR_MAX + 1];
} commons_irc_t;

typedef struct {
    char msgid[COMMONS_HEX_ID + 1], target[COMMONS_CHAN_MAX + 1], nick[COMMONS_NICK_MAX + 1];
    char truth; /* T, F, B or N: what the sender claims */
    int64_t ms;
    char text[COMMONS_TEXT_MAX + 1];
} commons_irc_record_t;

typedef struct {
    zxm_link_t *link;
    /* chat.me */
    char me[COMMONS_HEX_ID + 1];
    bool saved;
    int64_t records;
    char following[COMMONS_FOLLOW_MAX][COMMONS_HEX_ID + 1];
    uint32_t nfollowing;
    /* CM2 */
    commons_post_t feed[COMMONS_FEED_MAX];
    uint32_t nfeed;
    commons_post_t root, thread[COMMONS_THREAD_MAX];
    uint32_t nthread, thread_total; /* replies kept, and how many zxvd holds */
    /* CM1 */
    commons_room_t room[COMMONS_ROOMS];
    uint32_t nroom;
    bool online; /* pspace.list: the network is up */
    /* CM4, CM5 */
    commons_event_t events[COMMONS_EVENTS];
    uint32_t nevents;
    char member[COMMONS_MEMBERS][COMMONS_HEX_ID + 1];
    uint32_t nmember;
    /* CM6 */
    commons_irc_t irc;
    commons_irc_record_t irc_record;
    /* CM7 */
    char notice[COMMONS_NOTICE_MAX + 1];
} commons_t;

void commons_init(commons_t *c, zxm_link_t *link);

/* chat.me: who the owner is and whom they follow. */
int commons_me(commons_t *c);
/* chat.follow: follow (true) or stop following a NodeID. */
int commons_follow(commons_t *c, const char *node_id, bool follow);

/* CM2: chat.feed, in zxvd's order; room 0's newest moves on (CM4). */
int commons_feed(commons_t *c);
/* chat.thread: a held record and its replies. */
int commons_thread(commons_t *c, const char *id);

/* CM1: social.groups and pspace.list into the room list. */
int commons_groups(commons_t *c);
int commons_spaces(commons_t *c);
/* The room with this key ("" this node); -1 none. */
int32_t commons_find(const commons_t *c, const char *key);

/* Groups: found one (name 1..64 bytes), join or leave one held. */
int commons_group_create(commons_t *c, const char *name, bool open, char group[COMMONS_HEX_ID + 1]);
int commons_join(commons_t *c, const char *group);
int commons_leave(commons_t *c, const char *group);

/* CM5: make a room across nodes, or join one by its CID. */
int commons_space_create(commons_t *c, const char *name, const char *topic,
                         char cid[COMMONS_CID_MAX + 1]);
int commons_space_join(commons_t *c, const char *cid);
/* CM4: a space's new events (c->events), counted into its newest. */
int commons_space_events(commons_t *c, uint32_t room);
/* pspace.members: who this node knows in a space (c->member). */
int commons_space_members(commons_t *c, uint32_t room);

/* CM3: may the owner post in room now (CR1)? */
bool commons_may_post(const commons_t *c, uint32_t room);
/* CM3: post text in room (reply_to NULL or a record id, this node only). */
int commons_post(commons_t *c, uint32_t room, const char *text, const char *reply_to);
/* CM3: react to a held record on this node. */
int commons_react(commons_t *c, const char *to, const char *emoji);

/* CM4: a room's unread posts (CR3), COMMONS_REFUSED for no room; the read
 * mark moves to what is known; the notice of every room's unread (CM7). */
int64_t commons_unread(const commons_t *c, uint32_t room);
int commons_mark_read(commons_t *c, uint32_t room);
int commons_notice(commons_t *c);

/* CM6: the IRC gateway and its bridge. */
int commons_irc_status(commons_t *c);
int commons_irc_bridge(commons_t *c, bool on, const char *host, int64_t port, const char *remote,
                       const char *local, const char *nick, const char *password,
                       bool owner_confirmed);
int commons_irc_record(commons_t *c, const char *msgid);

#endif
