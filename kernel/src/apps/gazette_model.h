/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gazette_model.h — Gazette, news and feeds, as a model (zxm.h): one
 * reading list of what nodes and people post, by source. The rules are
 * gazette_rules.h; a source's unread is Commons's (commons_rules.h CR3)
 * and a space CID's text Seedbank's (seedbank_rules.h SR2).
 *
 * zxvd methods used: chat.me, chat.follow, chat.feed, social.feed,
 * web4.feed, pspace.events and irc.record. No drawing, no allocation,
 * integers only.
 *
 *   GM1 SOURCES. A source is a person or node (a NodeID: chat.feed's and
 *       social.feed's authors), a Web4 companion (web4.feed's "from"), a
 *       space (its CID) or an IRC channel (irc.record's target). People
 *       are followed through zxvd only (chat.me, chat.follow: the newest
 *       FOLLOW record decides), and chat.follow is sent only on the
 *       owner's word: without it ZXM_E_STATE, nothing sent, the list as
 *       it was. A companion, a space or a channel is
 *       followed in Gazette (gazette_keep). Any source may be muted. A
 *       source first seen in a feed is not followed.
 *   GM2 ITEMS. Every item fits its ^ZXGAZETTE record (GR3) before a page
 *       is kept; a page with one that does not fit is refused whole and
 *       the list before is kept. An item already held (same source, same
 *       feed, same id) is not added again, and one at or below its
 *       source's mark (the highest order key let go by gazette_drop_read)
 *       was read and let go. The owner's own posts are not news and are
 *       never added. A text longer than GAZETTE_TEXT_MAX is kept empty
 *       and marked cut.
 *   GM3 THE DAEMON'S RANKING. Each page is added in the order zxvd gives
 *       it, and the reading list (gazette_view) is the held order: nothing
 *       here re-ranks, and replies, reactions and scores are never kept,
 *       so nothing can rank by engagement.
 *   GM4 SHOWN. An item is shown when its source passes GR1 (followed,
 *       not muted); the rest are held but not in the view.
 *   GM5 UNREAD. A source's newest counts the items by others it has
 *       brought, its seen the ones the owner has read; its unread is
 *       Commons's CR3, and the badge is GR2 over every source.
 *   GM6 WEB4. Only signed envelopes are news (held and denied ones wait
 *       on the owner's consent, web4.consent) and never a CONSENT entry;
 *       the owner's own (from its agent_id) are left out.
 *   GM7 NOTICES. Only when the owner asked for them (docs/ENGAGEMENT.md:
 *       notifications only when asked for, no "we miss you"). zxvd has no
 *       notification method, so the notice is composed only; the view
 *       shows it.
 *
 * zxvd has no method that fetches a feed from outside the mesh (RSS,
 * Atom), none that lists the IRC gateway's relayed messages (irc.record
 * takes one msgid) and none that keeps a mute or a read state; none is
 * invented. */
#ifndef GAZETTE_MODEL_H
#define GAZETTE_MODEL_H

#include "gazette_rules.h"
#include "seedbank_rules.h"
#include "zxm.h"

#define GAZETTE_HEX_ID     64u /* a record id, NodeID or companion: 32 bytes as hex */
#define GAZETTE_KEY_MAX    64u /* a source's key: hex id, CID (59) or channel (50) */
#define GAZETTE_ID_MAX     64u /* an item's id within its feed */
#define GAZETTE_BY_MAX     64u /* who wrote it: a NodeID, a companion or a nick */
#define GAZETTE_TEXT_MAX   1024u
#define GAZETTE_SOURCES    64u
#define GAZETTE_ITEMS      128u
#define GAZETTE_PAGE       50u /* chat.feed, social.feed, web4.feed: at most 50 */
#define GAZETTE_EVENTS     32u /* pspace.events: the last 32 at most */
#define GAZETTE_FOLLOW_MAX 64u
#define GAZETTE_CHAN_MAX   50u /* an IRC channel name */
#define GAZETTE_NOTICE_MAX 255u

enum { GAZETTE_SRC_NODE, GAZETTE_SRC_AGENT, GAZETTE_SRC_SPACE, GAZETTE_SRC_IRC };
enum { GAZETTE_CHAT, GAZETTE_SOCIAL, GAZETTE_WEB4, GAZETTE_SPACE, GAZETTE_IRC }; /* feeds */

typedef struct {
    uint8_t kind; /* GAZETTE_SRC_* */
    char key[GAZETTE_KEY_MAX + 1];
    bool followed, muted; /* GM1 */
    int64_t newest, seen; /* GM5 */
    int64_t mark;         /* GM2: the highest order key let go */
    int64_t next;         /* a space: pspace.events' since */
} gazette_source_t;

typedef struct {
    uint32_t source;
    uint8_t feed; /* GAZETTE_CHAT .. GAZETTE_IRC */
    char id[GAZETTE_ID_MAX + 1];
    char by[GAZETTE_BY_MAX + 1];
    int64_t order; /* the Lamport time, envelope id, event index or relay ms */
    int64_t stamp; /* ms since 1970, 0 unknown */
    char text[GAZETTE_TEXT_MAX + 1];
    bool cut; /* GM2: longer than GAZETTE_TEXT_MAX */
    bool read;
} gazette_item_t;

typedef struct {
    zxm_link_t *link;
    char me[GAZETTE_HEX_ID + 1]; /* chat.me's NodeID, "" before */
    gazette_source_t src[GAZETTE_SOURCES];
    uint32_t nsrc;
    gazette_item_t item[GAZETTE_ITEMS];
    uint32_t nitem;
    bool notices; /* GM7: the owner asked for them */
    char notice[GAZETTE_NOTICE_MAX + 1];
} gazette_t;

void gazette_init(gazette_t *g, zxm_link_t *link);

/* GM1: chat.me: who the owner is and the people followed. */
int gazette_me(gazette_t *g);
/* GM1: chat.follow: follow (true) or stop following a person (a NodeID),
 * only when the owner confirmed it. */
int gazette_follow(gazette_t *g, const char *node_id, bool follow, bool owner_confirmed);
/* GM1: add a space (its CID) or an IRC channel as a followed source; the
 * source's index in *at. */
int gazette_add_space(gazette_t *g, const char *cid, uint32_t *at);
int gazette_add_channel(gazette_t *g, const char *chan, uint32_t *at);
/* GM1: follow or stop following a companion, space or channel here (a
 * person goes through gazette_follow). */
int gazette_keep(gazette_t *g, uint32_t src, bool follow);
/* GM1: mute or unmute any source. */
int gazette_mute(gazette_t *g, uint32_t src, bool muted);
/* The source of this kind and key; -1 none. */
int32_t gazette_find(const gazette_t *g, uint8_t kind, const char *key);

/* GM2, GM3: one page of each feed into the list, in zxvd's order. */
int gazette_chat(gazette_t *g);
int gazette_social(gazette_t *g, uint32_t limit);
int gazette_web4(gazette_t *g, uint32_t limit);
int gazette_space(gazette_t *g, uint32_t src);
int gazette_irc(gazette_t *g, const char *msgid);

/* GM4: is this item shown (GR1)? */
bool gazette_shown(const gazette_t *g, uint32_t item);
/* GM3: the shown items' indexes in held order into out[0..cap); how many. */
uint32_t gazette_view(const gazette_t *g, uint32_t *out, uint32_t cap);

/* GM5: a source's unread (Commons's CR3), GAZETTE_REFUSED for no source;
 * the badge over every source (GR2). */
int64_t gazette_unread(const gazette_t *g, uint32_t src);
int64_t gazette_unread_total(const gazette_t *g);
/* GM5: mark one item, or every item of a source, read. */
int gazette_mark_read(gazette_t *g, uint32_t item);
int gazette_mark_source_read(gazette_t *g, uint32_t src);
/* GM2: let read items go to make room; how many were let go. */
uint32_t gazette_drop_read(gazette_t *g);

/* GM7: the owner turns notices on or off; the notice of the badge. */
void gazette_notices(gazette_t *g, bool on);
int gazette_notice(gazette_t *g);

#endif
