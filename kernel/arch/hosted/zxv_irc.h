/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_irc.h — the IRC protocol engine behind zxvd's gateway (zxvd_irc.h,
 * docs/IRC_GATEWAY.md). No sockets, no TLS and no clock of its own: the host
 * feeds each connection's decrypted bytes in and drains its lines out, so
 * every rule below is tested without a network.
 *
 * CONNECTIONS
 *   E1  At most IRC_MAX_CLIENTS connections (irc_open answers -1 when full).
 *       Lines end in LF (a CR before it is dropped). A line longer than
 *       IRC_LINE_IN bytes, tags included, is answered 417 and discarded.
 *       Client tags (IRCv3 message-tags) are read past and never relayed.
 *   E2  Registration needs NICK and USER, and the gateway password first
 *       when one is set (PASS; a wrong or missing one is 464, ERROR and
 *       close). CAP LS / REQ / END negotiate message-tags, server-time and
 *       echo-message; registration waits for CAP END once CAP LS was sent.
 *       Before registration only CAP, PASS, NICK, USER, PING and QUIT are
 *       served (451 otherwise).
 *   E3  Nicknames: 1..IRC_NICK_MAX of letters, digits and [ ] \ ` _ ^ { | }
 *       or '-', not starting with a digit or '-', unique without regard to
 *       ASCII case (433), and never a reserved agent's nick (E7).
 *   E4  Flood: a bucket of IRC_FLOOD_BURST lines refilled one line per
 *       IRC_FLOOD_MS; a connection that empties it is closed ("Excess
 *       Flood"). An output queue over IRC_OUT_CAP closes it ("SendQ
 *       exceeded"). A connection silent for IRC_PING_MS is pinged, and
 *       closed IRC_PING_MS after that without an answer.
 *
 * CHANNELS AND MESSAGES
 *   E5  Channels are "#" and 1..IRC_CHAN_MAX more bytes (no space, comma,
 *       BEL or ':'), created by the first JOIN, gone when the last member
 *       leaves (reserved ones stay). At most IRC_MAX_CHANS channels and
 *       IRC_MAX_JOIN per connection. No channel operators and no modes: on
 *       this machine everyone who knows the password is the owner's guest.
 *   E6  THE TRUTH TAG. Every PRIVMSG and NOTICE carries one of the four
 *       values of lpres.h: T, F, B (both) or N (neither). A sender states it
 *       by starting the text with "[T] ", "[F] ", "[B] " or "[N] "; a text
 *       with none states nothing, so it is N. Every delivered copy starts
 *       with the tag ("[N] hello"; inside a CTCP ACTION after "ACTION "),
 *       and clients that asked for message-tags also get it as the tag
 *       zxv/truth. The tag says what the sender claims, never that the text
 *       is true. CTCP other than ACTION (VERSION, PING, DCC...) is not
 *       relayed: a PRIVMSG gets a NOTICE saying so, a NOTICE is dropped.
 *   E7  SIGNED AT THE EDGE. Before a message is delivered the host signs a
 *       record of it (target, sender, truth, time, text: irc_hooks_t.sign)
 *       and names it with a msgid, sent as the msgid tag. A message the
 *       host cannot sign is not delivered (the sender gets a NOTICE). The
 *       signature says this gateway relayed it from that nick at that time;
 *       it does not prove who typed it.
 *   E8  RESERVED CHANNELS AND AGENTS. irc_reserve makes a channel (e.g.
 *       #swarm) whose topic is fixed and whose agent nick is always listed
 *       in it. A message to the channel or to the agent's nick goes to
 *       irc_hooks_t.serve after delivery; the host answers with irc_say.
 *       Every channel message also goes to irc_hooks_t.relay (the bridge).
 *   E9  Text limit: IRC_TEXT_MAX bytes from a client (417 when longer), so
 *       every line the gateway writes, tag prefix included, stays inside
 *       IRC's 512 bytes before message tags. irc_say splits a longer text
 *       at line breaks and at IRC_TEXT_MAX bytes on UTF-8 boundaries.
 *
 * COMMANDS: CAP PASS NICK USER PING PONG QUIT JOIN PART PRIVMSG NOTICE TOPIC
 * NAMES LIST WHO WHOIS MODE MOTD AWAY USERHOST ISON; anything else is 421.
 */
#ifndef ZXV_IRC_H
#define ZXV_IRC_H

#include <stdbool.h>
#include <stdint.h>

#define IRC_MAX_CLIENTS 16u
#define IRC_MAX_CHANS   32u
#define IRC_MAX_JOIN    16u
#define IRC_NICK_MAX    30u
#define IRC_CHAN_MAX    50u /* after the '#' */
#define IRC_TOPIC_MAX   300u
#define IRC_TEXT_MAX    400u
#define IRC_LINE_IN     4608u /* 4096 bytes of tags + 512 */
#define IRC_OUT_CAP     65536u
#define IRC_FLOOD_BURST 20u
#define IRC_FLOOD_MS    250u
#define IRC_PING_MS     120000u
#define IRC_MSGID_LEN   32u /* hex */

typedef struct irc_s irc_t;

/* One message as the host signs and serves it. target is "#chan" or a nick;
 * text is without the truth prefix. */
typedef struct {
    const char *target, *nick, *text;
    char truth; /* 'T', 'F', 'B' or 'N' */
    bool notice;
    uint64_t ms; /* wall clock, ms since 1970 */
} irc_msg_t;

typedef struct {
    void *ctx;
    /* E7: sign a record of m and write its id (IRC_MSGID_LEN lower-case hex
     * digits and a NUL); false refuses the message */
    bool (*sign)(void *ctx, const irc_msg_t *m, char *msgid);
    /* E8: a message to a reserved channel or agent, after delivery; may be NULL */
    void (*serve)(void *ctx, irc_t *s, const irc_msg_t *m);
    /* E8: every channel message after delivery; may be NULL */
    void (*relay)(void *ctx, irc_t *s, const irc_msg_t *m);
    /* the wall clock in ms (NULL: time()) */
    uint64_t (*now_ms)(void *ctx);
} irc_hooks_t;

typedef struct {
    bool used, registered, cap_neg, pass_ok, closing, cap_tags, cap_time, cap_echo, away;
    bool discard; /* E1: inside an over-long line */
    char nick[IRC_NICK_MAX + 1], user[16], real[64];
    char in[IRC_LINE_IN + 1];
    uint32_t in_len;
    char out[IRC_OUT_CAP];
    uint32_t out_len;
    uint64_t last_ms, tok_ms;
    uint32_t tokens;
    bool ping_out;
    uint32_t joined;
} irc_client_t;

typedef struct {
    bool used, reserved;
    char name[IRC_CHAN_MAX + 2];
    char topic[IRC_TOPIC_MAX + 1];
    char agent[IRC_NICK_MAX + 1]; /* E8: the reserved channel's agent */
    uint32_t members;             /* bit i: client i */
} irc_chan_t;

struct irc_s {
    irc_client_t c[IRC_MAX_CLIENTS];
    irc_chan_t ch[IRC_MAX_CHANS];
    char server[64];
    char pass[80];
    char agents[4][IRC_NICK_MAX + 1]; /* E8: reserved nicks */
    uint32_t n_agents;
    irc_hooks_t h;
    uint64_t now;
    uint64_t delivered, refused;
};

/* server: the name in every reply ("zxvd.local"); pass: the gateway
 * password (NULL or "" for none, tests only). */
void irc_init(irc_t *s, const char *server, const char *pass, const irc_hooks_t *h);
/* E8: 0, or -1 (bad name, table full) */
int irc_reserve(irc_t *s, const char *chan, const char *agent, const char *topic);
/* the channel becomes an ordinary one (its agent's nick stays reserved) */
void irc_unreserve(irc_t *s, const char *chan);
/* E1: a new connection's id, or -1 when full */
int irc_open(irc_t *s, uint64_t now_ms);
/* bytes the connection sent */
void irc_input(irc_t *s, int id, const char *p, uint32_t n);
/* take up to cap bytes of what the connection should be sent */
uint32_t irc_output(irc_t *s, int id, char *out, uint32_t cap);
/* the connection should be closed once its output is sent */
bool irc_closing(const irc_t *s, int id);
/* the transport is gone: tell its channels, forget it */
void irc_drop(irc_t *s, int id, const char *why);
/* E4: pings and timeouts */
void irc_tick(irc_t *s, uint64_t now_ms);
/* E8: say text in a channel or to a nick as `from` (an agent, or a bridged
 * "nick|net"). Signed and tagged like any message. The lines delivered, or
 * -1 (no such target, or signing refused). */
int irc_say(irc_t *s, const char *target, const char *from, char truth, const char *text);
/* E6: the truth a text states and where its body starts */
char irc_truth_of(const char *text, const char **body);
uint32_t irc_clients(const irc_t *s);
/* E3 */
bool irc_nick_valid(const char *n);

#endif
