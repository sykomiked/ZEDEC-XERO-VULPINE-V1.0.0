/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_irc.c — the IRC protocol engine (zxv_irc.h E1-E9). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#endif
#include "zxv_irc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_PARAMS 15

static const char *const MOTD[] = {
    "This is zxvd's IRC gateway, on this machine only.",
    "Every message carries a truth tag: [T] true, [F] false, [B] both,",
    "[N] neither. Start your text with one to state it; without one it is [N].",
    "The tag is the sender's claim, never proof. Messages are signed by this",
    "node when they pass the gateway (msgid tag; irc.record over zxvd's socket).",
    "No money, keys or Player Card actions ever travel through IRC.",
};

/* the next token of *p split at `sep` (strtok_r is not on every target) */
static char *tok(char **p, char sep)
{
    while (**p == sep) (*p)++;
    if (!**p) return NULL;
    char *t = *p, *e = strchr(t, sep);
    if (e) {
        *e = 0;
        *p = e + 1;
    } else {
        *p = t + strlen(t);
    }
    return t;
}

static uint64_t now_of(irc_t *s)
{
    if (s->h.now_ms) return s->h.now_ms(s->h.ctx);
    return (uint64_t) time(NULL) * 1000u;
}

static char lower(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ? (char) (ch + 32) : ch;
}

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (lower(*a) != lower(*b)) return false;
    return *a == *b;
}

static void scpy(char *d, size_t cap, const char *s)
{
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(d, s, n);
    d[n] = 0;
}

/* ---------------- output ---------------- */

static void close_link(irc_t *s, int id, const char *why);

static void raw(irc_t *s, int id, const char *line, size_t n)
{
    irc_client_t *c = &s->c[id];
    if (!c->used) return;
    if (n + 2 > IRC_OUT_CAP - c->out_len) {
        /* E4: SendQ exceeded. Keep what is queued, add nothing more. */
        if (!c->closing) {
            c->closing = true;
            const char *e = "ERROR :Closing link (SendQ exceeded)\r\n";
            size_t el = strlen(e);
            if (el <= IRC_OUT_CAP - c->out_len) {
                memcpy(c->out + c->out_len, e, el);
                c->out_len += (uint32_t) el;
            }
        }
        return;
    }
    memcpy(c->out + c->out_len, line, n);
    c->out_len += (uint32_t) n;
    c->out[c->out_len++] = '\r';
    c->out[c->out_len++] = '\n';
}

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static void
sendf(irc_t *s, int id, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t) n >= sizeof line) n = (int) sizeof line - 1;
    raw(s, id, line, (size_t) n);
}

static const char *me(const irc_t *s, int id)
{
    return s->c[id].nick[0] ? s->c[id].nick : "*";
}

#define NUM(s, id, code, fmt, ...)                                                                 \
    sendf(s, id, ":%s " code " %s " fmt, (s)->server, me(s, id), __VA_ARGS__)

/* the IRCv3 tags this client asked for */
static void tags(const irc_t *s, int id, const char *msgid, char truth, uint64_t ms, char *out,
                 size_t cap)
{
    const irc_client_t *c = &s->c[id];
    size_t n = 0;
    out[0] = 0;
    if (c->cap_time && ms) {
        time_t t = (time_t) (ms / 1000u);
        struct tm tm;
#if defined(_WIN32)
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        n += (size_t) snprintf(out + n, cap - n, "time=%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
                               tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                               tm.tm_sec, (unsigned) (ms % 1000u));
    }
    if (c->cap_tags && msgid)
        n += (size_t) snprintf(out + n, cap - n, "%smsgid=%s;zxv/truth=%c", n ? ";" : "", msgid,
                               truth);
    if (n && n + 2 < cap) {
        memmove(out + 1, out, n + 1);
        out[0] = '@';
        out[n + 1] = ' ';
        out[n + 2] = 0;
    }
}

/* ---------------- lookups ---------------- */

static int find_nick(const irc_t *s, const char *nick)
{
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++)
        if (s->c[i].used && s->c[i].nick[0] && ieq(s->c[i].nick, nick)) return (int) i;
    return -1;
}

static int find_agent(const irc_t *s, const char *nick)
{
    for (uint32_t i = 0; i < s->n_agents; i++)
        if (ieq(s->agents[i], nick)) return (int) i;
    return -1;
}

static irc_chan_t *find_chan(irc_t *s, const char *name)
{
    for (uint32_t i = 0; i < IRC_MAX_CHANS; i++)
        if (s->ch[i].used && ieq(s->ch[i].name, name)) return &s->ch[i];
    return NULL;
}

static bool chan_valid(const char *n)
{
    size_t l = strlen(n);
    if (l < 2 || l > IRC_CHAN_MAX + 1 || n[0] != '#') return false;
    for (size_t i = 1; i < l; i++) {
        unsigned char ch = (unsigned char) n[i];
        if (ch <= ' ' || ch == ',' || ch == 7 || ch == ':' || ch == 127) return false;
    }
    return true;
}

bool irc_nick_valid(const char *n)
{
    size_t l = strlen(n);
    if (l == 0 || l > IRC_NICK_MAX) return false;
    if ((n[0] >= '0' && n[0] <= '9') || n[0] == '-') return false;
    for (size_t i = 0; i < l; i++) {
        char ch = n[i];
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || strchr("[]\\`_^{|}-", ch) != NULL;
        if (!ok) return false;
    }
    return true;
}

static uint32_t bit(int id)
{
    return 1u << (uint32_t) id;
}

static uint32_t popcount(uint32_t v)
{
    uint32_t n = 0;
    for (; v; v &= v - 1) n++;
    return n;
}

/* send a line (same prefix and text for all) to every member, except `skip` */
static void to_members(irc_t *s, const irc_chan_t *ch, int skip, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
static void to_members(irc_t *s, const irc_chan_t *ch, int skip, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t) n >= sizeof line) n = (int) sizeof line - 1;
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++)
        if ((ch->members & bit((int) i)) && (int) i != skip) raw(s, (int) i, line, (size_t) n);
}

/* everyone who shares a channel with id, and id itself, once each */
static uint32_t neighbours(const irc_t *s, int id)
{
    uint32_t m = bit(id);
    for (uint32_t i = 0; i < IRC_MAX_CHANS; i++)
        if (s->ch[i].used && (s->ch[i].members & bit(id))) m |= s->ch[i].members;
    return m;
}

/* ---------------- lifecycle ---------------- */

void irc_init(irc_t *s, const char *server, const char *pass, const irc_hooks_t *h)
{
    memset(s, 0, sizeof *s);
    scpy(s->server, sizeof s->server, server && *server ? server : "zxvd.local");
    if (pass) scpy(s->pass, sizeof s->pass, pass);
    if (h) s->h = *h;
}

int irc_reserve(irc_t *s, const char *chan, const char *agent, const char *topic)
{
    if (!chan_valid(chan) || !agent || !irc_nick_valid(agent) || find_chan(s, chan)) return -1;
    if (find_agent(s, agent) < 0) {
        if (s->n_agents >= sizeof s->agents / sizeof s->agents[0]) return -1;
        scpy(s->agents[s->n_agents++], IRC_NICK_MAX + 1, agent);
    }
    for (uint32_t i = 0; i < IRC_MAX_CHANS; i++)
        if (!s->ch[i].used) {
            irc_chan_t *c = &s->ch[i];
            memset(c, 0, sizeof *c);
            c->used = c->reserved = true;
            scpy(c->name, sizeof c->name, chan);
            scpy(c->agent, sizeof c->agent, agent);
            scpy(c->topic, sizeof c->topic, topic ? topic : "");
            return 0;
        }
    return -1;
}

void irc_unreserve(irc_t *s, const char *chan)
{
    irc_chan_t *c = find_chan(s, chan);
    if (!c || !c->reserved) return;
    c->reserved = false;
    c->topic[0] = 0;
    if (!c->members) c->used = false;
}

int irc_open(irc_t *s, uint64_t now_ms)
{
    s->now = now_ms;
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++)
        if (!s->c[i].used) {
            irc_client_t *c = &s->c[i];
            memset(c, 0, sizeof *c);
            c->used = true;
            c->pass_ok = s->pass[0] == 0;
            c->last_ms = c->tok_ms = now_ms;
            c->tokens = IRC_FLOOD_BURST;
            return (int) i;
        }
    return -1;
}

bool irc_closing(const irc_t *s, int id)
{
    return id >= 0 && id < (int) IRC_MAX_CLIENTS && s->c[id].used && s->c[id].closing;
}

uint32_t irc_clients(const irc_t *s)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++) n += s->c[i].used;
    return n;
}

uint32_t irc_output(irc_t *s, int id, char *out, uint32_t cap)
{
    if (id < 0 || id >= (int) IRC_MAX_CLIENTS || !s->c[id].used) return 0;
    irc_client_t *c = &s->c[id];
    uint32_t n = c->out_len < cap ? c->out_len : cap;
    memcpy(out, c->out, n);
    memmove(c->out, c->out + n, c->out_len - n);
    c->out_len -= n;
    return n;
}

/* leave every channel, telling the others once */
static void part_all(irc_t *s, int id, const char *why)
{
    irc_client_t *c = &s->c[id];
    if (c->registered) {
        uint32_t m = neighbours(s, id) & ~bit(id);
        for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++)
            if (m & bit((int) i))
                sendf(s, (int) i, ":%s!%s@zxv QUIT :%s", c->nick, c->user, why ? why : "Quit");
    }
    for (uint32_t i = 0; i < IRC_MAX_CHANS; i++) {
        irc_chan_t *ch = &s->ch[i];
        if (!ch->used) continue;
        ch->members &= ~bit(id);
        if (!ch->members && !ch->reserved) ch->used = false;
    }
    c->joined = 0;
}

static void close_link(irc_t *s, int id, const char *why)
{
    irc_client_t *c = &s->c[id];
    if (c->closing) return;
    part_all(s, id, why);
    sendf(s, id, "ERROR :Closing link (%s)", why);
    c->closing = true;
}

void irc_drop(irc_t *s, int id, const char *why)
{
    if (id < 0 || id >= (int) IRC_MAX_CLIENTS || !s->c[id].used) return;
    /* after close_link the channels are already told; after a SendQ close
     * (raw) they are not, so part_all runs either way and finds who is left */
    part_all(s, id, why ? why : "Connection closed");
    memset(&s->c[id], 0, sizeof s->c[id]);
}

void irc_tick(irc_t *s, uint64_t now_ms)
{
    s->now = now_ms;
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++) {
        irc_client_t *c = &s->c[i];
        if (!c->used || c->closing) continue;
        uint64_t quiet = now_ms > c->last_ms ? now_ms - c->last_ms : 0;
        if (c->ping_out && quiet >= 2ull * IRC_PING_MS)
            close_link(s, (int) i, "Ping timeout");
        else if (!c->ping_out && quiet >= IRC_PING_MS) {
            sendf(s, (int) i, "PING :%s", s->server);
            c->ping_out = true;
        }
    }
}

/* ---------------- registration ---------------- */

static void welcome(irc_t *s, int id)
{
    irc_client_t *c = &s->c[id];
    c->registered = true;
    NUM(s, id, "001", ":Welcome to the ZXV gateway, %s", c->nick);
    NUM(s, id, "002", ":Your host is %s, zxvd's IRC gateway", s->server);
    NUM(s, id, "003", ":%s", "This gateway runs only on this machine");
    NUM(s, id, "004", "%s zxvd-irc i nt", s->server);
    NUM(s, id, "005",
        "NETWORK=ZXV CASEMAPPING=ascii CHANTYPES=# PREFIX=() CHANMODES=,,,nt NICKLEN=%u "
        "CHANNELLEN=%u TOPICLEN=%u CHANLIMIT=#:%u MAXTARGETS=1 :are supported by this server",
        IRC_NICK_MAX, IRC_CHAN_MAX + 1, IRC_TOPIC_MAX, IRC_MAX_JOIN);
    NUM(s, id, "375", ":- %s message of the day -", s->server);
    for (size_t i = 0; i < sizeof MOTD / sizeof MOTD[0]; i++) NUM(s, id, "372", ":- %s", MOTD[i]);
    NUM(s, id, "376", ":%s", "End of /MOTD command");
}

static void try_register(irc_t *s, int id)
{
    irc_client_t *c = &s->c[id];
    if (c->registered || c->cap_neg || !c->nick[0] || !c->user[0]) return;
    if (!c->pass_ok) {
        NUM(s, id, "464", ":%s", "Password incorrect");
        close_link(s, id, "Bad password");
        return;
    }
    welcome(s, id);
}

static void cmd_cap(irc_t *s, int id, int argc, char **argv)
{
    irc_client_t *c = &s->c[id];
    if (argc < 1) {
        NUM(s, id, "461", "CAP :%s", "Not enough parameters");
        return;
    }
    if (ieq(argv[0], "LS")) {
        if (!c->registered) c->cap_neg = true;
        sendf(s, id, ":%s CAP %s LS :message-tags server-time echo-message", s->server, me(s, id));
    } else if (ieq(argv[0], "LIST")) {
        sendf(s, id, ":%s CAP %s LIST :%s%s%s", s->server, me(s, id),
              c->cap_tags ? "message-tags " : "", c->cap_time ? "server-time " : "",
              c->cap_echo ? "echo-message" : "");
    } else if (ieq(argv[0], "REQ")) {
        if (!c->registered) c->cap_neg = true;
        const char *want = argc > 1 ? argv[1] : "";
        /* all or nothing (IRCv3 CAP REQ) */
        bool tags_ = c->cap_tags, time_ = c->cap_time, echo = c->cap_echo, ok = true;
        char buf[256];
        scpy(buf, sizeof buf, want);
        char *cur = buf;
        for (char *t = tok(&cur, ' '); t; t = tok(&cur, ' ')) {
            bool off = *t == '-';
            const char *n = off ? t + 1 : t;
            if (!strcmp(n, "message-tags"))
                tags_ = !off;
            else if (!strcmp(n, "server-time"))
                time_ = !off;
            else if (!strcmp(n, "echo-message"))
                echo = !off;
            else
                ok = false;
        }
        if (ok) {
            c->cap_tags = tags_;
            c->cap_time = time_;
            c->cap_echo = echo;
        }
        sendf(s, id, ":%s CAP %s %s :%s", s->server, me(s, id), ok ? "ACK" : "NAK", want);
    } else if (ieq(argv[0], "END")) {
        c->cap_neg = false;
        try_register(s, id);
    } else {
        NUM(s, id, "410", "%s :Invalid CAP command", argv[0]);
    }
}

static void cmd_nick(irc_t *s, int id, int argc, char **argv)
{
    irc_client_t *c = &s->c[id];
    if (argc < 1 || !argv[0][0]) {
        NUM(s, id, "431", ":%s", "No nickname given");
        return;
    }
    const char *n = argv[0];
    if (!irc_nick_valid(n)) {
        NUM(s, id, "432", "%s :Erroneous nickname", n);
        return;
    }
    int other = find_nick(s, n);
    if ((other >= 0 && other != id) || find_agent(s, n) >= 0) {
        NUM(s, id, "433", "%s :Nickname is already in use", n);
        return;
    }
    if (c->registered) {
        uint32_t m = neighbours(s, id);
        for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++)
            if (m & bit((int) i)) sendf(s, (int) i, ":%s!%s@zxv NICK :%s", c->nick, c->user, n);
    }
    scpy(c->nick, sizeof c->nick, n);
    try_register(s, id);
}

/* ---------------- channels ---------------- */

static void names(irc_t *s, int id, const irc_chan_t *ch)
{
    char line[512];
    size_t n = 0;
    line[0] = 0;
    if (ch->reserved) n += (size_t) snprintf(line + n, sizeof line - n, "%s", ch->agent);
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++) {
        if (!(ch->members & bit((int) i))) continue;
        if (n + IRC_NICK_MAX + 2 >= sizeof line - 80) {
            NUM(s, id, "353", "= %s :%s", ch->name, line);
            n = 0;
            line[0] = 0;
        }
        n += (size_t) snprintf(line + n, sizeof line - n, "%s%s", n ? " " : "", s->c[i].nick);
    }
    if (n) NUM(s, id, "353", "= %s :%s", ch->name, line);
    NUM(s, id, "366", "%s :End of /NAMES list", ch->name);
}

static void join_one(irc_t *s, int id, const char *name)
{
    irc_client_t *c = &s->c[id];
    if (!chan_valid(name)) {
        NUM(s, id, "403", "%s :No such channel", name);
        return;
    }
    irc_chan_t *ch = find_chan(s, name);
    if (ch && (ch->members & bit(id))) return;
    if (popcount(c->joined) >= IRC_MAX_JOIN) {
        NUM(s, id, "405", "%s :You have joined too many channels", name);
        return;
    }
    if (!ch) {
        for (uint32_t i = 0; i < IRC_MAX_CHANS && !ch; i++)
            if (!s->ch[i].used) {
                ch = &s->ch[i];
                memset(ch, 0, sizeof *ch);
                ch->used = true;
                scpy(ch->name, sizeof ch->name, name);
            }
        if (!ch) {
            NUM(s, id, "403", "%s :%s", name, "No room for another channel");
            return;
        }
    }
    ch->members |= bit(id);
    c->joined |= bit((int) (ch - s->ch));
    to_members(s, ch, -1, ":%s!%s@zxv JOIN %s", c->nick, c->user, ch->name);
    if (ch->topic[0])
        NUM(s, id, "332", "%s :%s", ch->name, ch->topic);
    else
        NUM(s, id, "331", "%s :No topic is set", ch->name);
    names(s, id, ch);
}

static void part_one(irc_t *s, int id, const char *name, const char *why)
{
    irc_client_t *c = &s->c[id];
    irc_chan_t *ch = find_chan(s, name);
    if (!ch) {
        NUM(s, id, "403", "%s :No such channel", name);
        return;
    }
    if (!(ch->members & bit(id))) {
        NUM(s, id, "442", "%s :You're not on that channel", name);
        return;
    }
    to_members(s, ch, -1, ":%s!%s@zxv PART %s :%s", c->nick, c->user, ch->name, why ? why : "");
    ch->members &= ~bit(id);
    c->joined &= ~bit((int) (ch - s->ch));
    if (!ch->members && !ch->reserved) ch->used = false;
}

static void cmd_join(irc_t *s, int id, int argc, char **argv)
{
    if (argc < 1) {
        NUM(s, id, "461", "JOIN :%s", "Not enough parameters");
        return;
    }
    if (!strcmp(argv[0], "0")) {
        for (uint32_t i = 0; i < IRC_MAX_CHANS; i++)
            if (s->ch[i].used && (s->ch[i].members & bit(id)))
                part_one(s, id, s->ch[i].name, "Left all channels");
        return;
    }
    char buf[600];
    scpy(buf, sizeof buf, argv[0]);
    char *cur = buf;
    for (char *t = tok(&cur, ','); t; t = tok(&cur, ',')) join_one(s, id, t);
}

static void cmd_part(irc_t *s, int id, int argc, char **argv)
{
    if (argc < 1) {
        NUM(s, id, "461", "PART :%s", "Not enough parameters");
        return;
    }
    char buf[600];
    scpy(buf, sizeof buf, argv[0]);
    char *cur = buf;
    for (char *t = tok(&cur, ','); t; t = tok(&cur, ','))
        part_one(s, id, t, argc > 1 ? argv[1] : NULL);
}

static void cmd_topic(irc_t *s, int id, int argc, char **argv)
{
    if (argc < 1) {
        NUM(s, id, "461", "TOPIC :%s", "Not enough parameters");
        return;
    }
    irc_chan_t *ch = find_chan(s, argv[0]);
    if (!ch) {
        NUM(s, id, "403", "%s :No such channel", argv[0]);
        return;
    }
    if (argc < 2) {
        if (ch->topic[0])
            NUM(s, id, "332", "%s :%s", ch->name, ch->topic);
        else
            NUM(s, id, "331", "%s :No topic is set", ch->name);
        return;
    }
    if (!(ch->members & bit(id))) {
        NUM(s, id, "442", "%s :You're not on that channel", ch->name);
        return;
    }
    if (ch->reserved) {
        NUM(s, id, "482", "%s :%s", ch->name, "This channel's topic is fixed");
        return;
    }
    scpy(ch->topic, sizeof ch->topic, argv[1]);
    to_members(s, ch, -1, ":%s!%s@zxv TOPIC %s :%s", s->c[id].nick, s->c[id].user, ch->name,
               ch->topic);
}

/* ---------------- messages (E6-E9) ---------------- */

char irc_truth_of(const char *text, const char **body)
{
    if (body) *body = text;
    if (text[0] == '[' && strchr("TFBN", text[1]) && text[1] && text[2] == ']' && text[3] == ' ') {
        if (body) *body = text + 4;
        return text[1];
    }
    return 'N';
}

/* the delivered text: the truth prefix, inside an ACTION after "ACTION " */
static void prefixed(char truth, const char *body, char *out, size_t cap)
{
    if (!strncmp(body, "\001ACTION ", 8))
        snprintf(out, cap, "\001ACTION [%c] %s", truth, body + 8);
    else
        snprintf(out, cap, "[%c] %s", truth, body);
}

/* sign and deliver one line. from_id < 0: an agent or bridged nick `from`.
 * 1 delivered, 0 refused. */
static int deliver(irc_t *s, int from_id, const char *from, const char *user, const char *target,
                   char truth, const char *body, bool notice)
{
    irc_msg_t m = {target, from, body, truth, notice, now_of(s)};
    char msgid[IRC_MSGID_LEN + 1];
    msgid[0] = 0;
    if (!s->h.sign || !s->h.sign(s->h.ctx, &m, msgid)) {
        s->refused++;
        if (from_id >= 0)
            sendf(s, from_id, ":%s NOTICE %s :Not delivered: this node could not sign it",
                  s->server, me(s, from_id));
        return 0;
    }
    msgid[IRC_MSGID_LEN] = 0;
    char text[IRC_TEXT_MAX + 32], tg[160];
    prefixed(truth, body, text, sizeof text);
    const char *verb = notice ? "NOTICE" : "PRIVMSG";
    uint32_t to = 0;
    if (target[0] == '#') {
        irc_chan_t *ch = find_chan(s, target);
        if (ch) to = ch->members;
    } else {
        int t = find_nick(s, target);
        if (t >= 0) to = bit(t);
    }
    if (from_id >= 0 && s->c[from_id].cap_echo)
        to |= bit(from_id);
    else if (from_id >= 0)
        to &= ~bit(from_id);
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++) {
        if (!(to & bit((int) i))) continue;
        tags(s, (int) i, msgid, truth, m.ms, tg, sizeof tg);
        sendf(s, (int) i, "%s:%s!%s@zxv %s %s :%s", tg, from, user, verb, target, text);
    }
    s->delivered++;
    if (target[0] == '#' && s->h.relay) s->h.relay(s->h.ctx, s, &m);
    return 1;
}

static void cmd_msg(irc_t *s, int id, int argc, char **argv, bool notice)
{
    const char *verb = notice ? "NOTICE" : "PRIVMSG";
    if (argc < 1) {
        if (!notice) NUM(s, id, "411", ":No recipient given (%s)", verb);
        return;
    }
    if (argc < 2 || !argv[1][0]) {
        if (!notice) NUM(s, id, "412", ":%s", "No text to send");
        return;
    }
    const char *target = argv[0], *body;
    if (strchr(target, ',')) {
        if (!notice) NUM(s, id, "407", "%s :%s", target, "One recipient at a time");
        return;
    }
    char truth, action[IRC_LINE_IN + 1];
    if (!strncmp(argv[1], "\001ACTION ", 8)) { /* E6: the tag inside an ACTION */
        truth = irc_truth_of(argv[1] + 8, &body);
        snprintf(action, sizeof action, "\001ACTION %s", body);
        body = action;
    } else if (argv[1][0] == '\001') { /* E6: other CTCP is not relayed */
        if (!notice)
            sendf(s, id, ":%s NOTICE %s :Not delivered: only CTCP ACTION crosses this gateway",
                  s->server, me(s, id));
        return;
    } else {
        truth = irc_truth_of(argv[1], &body);
    }
    if (strlen(body) > IRC_TEXT_MAX) {
        NUM(s, id, "417", ":Text longer than %u bytes", IRC_TEXT_MAX);
        return;
    }
    if (!body[0]) {
        if (!notice) NUM(s, id, "412", ":%s", "No text to send");
        return;
    }
    irc_chan_t *ch = NULL;
    int agent = -1;
    if (target[0] == '#') {
        ch = find_chan(s, target);
        if (!ch) {
            if (!notice) NUM(s, id, "403", "%s :No such channel", target);
            return;
        }
        if (!(ch->members & bit(id))) {
            if (!notice) NUM(s, id, "404", "%s :Cannot send to channel (join it first)", target);
            return;
        }
        target = ch->name;
    } else if ((agent = find_agent(s, target)) >= 0) {
        target = s->agents[agent];
    } else {
        int t = find_nick(s, target);
        if (t < 0) {
            if (!notice) NUM(s, id, "401", "%s :No such nick", target);
            return;
        }
        target = s->c[t].nick;
    }
    if (!deliver(s, id, s->c[id].nick, s->c[id].user, target, truth, body, notice)) return;
    /* E8: a reserved channel or agent hears it (never a NOTICE: no replies to those) */
    if (!notice && s->h.serve && ((ch && ch->reserved) || agent >= 0)) {
        irc_msg_t m = {target, s->c[id].nick, body, truth, false, now_of(s)};
        s->h.serve(s->h.ctx, s, &m);
    }
}

int irc_say(irc_t *s, const char *target, const char *from, char truth, const char *text)
{
    if (!s || !target || !from || !text || !strchr("TFBN", truth) || !truth) return -1;
    if (target[0] == '#' ? !find_chan(s, target) : find_nick(s, target) < 0) return -1;
    int lines = 0;
    const char *p = text;
    char chunk[IRC_TEXT_MAX + 1];
    while (*p) {
        size_t n = strcspn(p, "\r\n");
        const char *next = p + n;
        while (n > 0) {
            size_t take = n;
            if (take > IRC_TEXT_MAX) {
                take = IRC_TEXT_MAX;
                /* back off to a UTF-8 boundary */
                while (take > 0 && ((unsigned char) p[take] & 0xC0) == 0x80) take--;
                if (take == 0) take = IRC_TEXT_MAX;
            }
            memcpy(chunk, p, take);
            chunk[take] = 0;
            if (!deliver(s, -1, from, "agent", target, truth, chunk, false))
                return lines ? lines : -1;
            lines++;
            p += take;
            n -= take;
        }
        p = next;
        while (*p == '\r' || *p == '\n') p++;
    }
    return lines;
}

/* ---------------- queries ---------------- */

static void cmd_names(irc_t *s, int id, int argc, char **argv)
{
    if (argc >= 1) {
        irc_chan_t *ch = find_chan(s, argv[0]);
        if (ch)
            names(s, id, ch);
        else
            NUM(s, id, "366", "%s :End of /NAMES list", argv[0]);
        return;
    }
    for (uint32_t i = 0; i < IRC_MAX_CHANS; i++)
        if (s->ch[i].used) names(s, id, &s->ch[i]);
}

static void cmd_list(irc_t *s, int id)
{
    NUM(s, id, "321", "Channel :%s", "Users  Name");
    for (uint32_t i = 0; i < IRC_MAX_CHANS; i++) {
        const irc_chan_t *ch = &s->ch[i];
        if (!ch->used) continue;
        NUM(s, id, "322", "%s %u :%s", ch->name, popcount(ch->members) + (ch->reserved ? 1u : 0u),
            ch->topic);
    }
    NUM(s, id, "323", ":%s", "End of /LIST");
}

static void who_line(irc_t *s, int id, const char *chan, int who)
{
    const irc_client_t *c = &s->c[who];
    NUM(s, id, "352", "%s %s zxv %s %s %c :0 %s", chan, c->user, s->server, c->nick,
        c->away ? 'G' : 'H', c->real);
}

static void cmd_who(irc_t *s, int id, int argc, char **argv)
{
    const char *mask = argc >= 1 ? argv[0] : "*";
    irc_chan_t *ch = mask[0] == '#' ? find_chan(s, mask) : NULL;
    if (ch) {
        if (ch->reserved)
            NUM(s, id, "352", "%s agent zxv %s %s H :0 %s", ch->name, s->server, ch->agent,
                "ZXV agent");
        for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++)
            if (ch->members & bit((int) i)) who_line(s, id, ch->name, (int) i);
    } else if (mask[0] != '#') {
        int t = find_nick(s, mask);
        if (t >= 0) who_line(s, id, "*", t);
    }
    NUM(s, id, "315", "%s :End of /WHO list", mask);
}

static void cmd_whois(irc_t *s, int id, int argc, char **argv)
{
    if (argc < 1) {
        NUM(s, id, "431", ":%s", "No nickname given");
        return;
    }
    const char *n = argv[argc - 1];
    int a = find_agent(s, n), t = find_nick(s, n);
    if (a >= 0) {
        NUM(s, id, "311", "%s agent zxv * :%s", s->agents[a],
            "ZXV agent: its answers are advisory");
    } else if (t >= 0) {
        NUM(s, id, "311", "%s %s zxv * :%s", s->c[t].nick, s->c[t].user, s->c[t].real);
        if (s->c[t].away) NUM(s, id, "301", "%s :%s", s->c[t].nick, "Away");
    } else {
        NUM(s, id, "401", "%s :No such nick", n);
    }
    NUM(s, id, "318", "%s :End of /WHOIS list", n);
}

static void cmd_mode(irc_t *s, int id, int argc, char **argv)
{
    if (argc < 1) {
        NUM(s, id, "461", "MODE :%s", "Not enough parameters");
        return;
    }
    if (argv[0][0] == '#') {
        irc_chan_t *ch = find_chan(s, argv[0]);
        if (!ch)
            NUM(s, id, "403", "%s :No such channel", argv[0]);
        else if (argc == 1)
            NUM(s, id, "324", "%s +nt", ch->name);
        else if (!strcmp(argv[1], "b") || !strcmp(argv[1], "+b"))
            NUM(s, id, "368", "%s :End of channel ban list", ch->name);
        else
            NUM(s, id, "482", "%s :%s", ch->name, "This gateway has no channel modes to change");
        return;
    }
    if (!ieq(argv[0], s->c[id].nick)) {
        NUM(s, id, "502", ":%s", "Cannot change mode for other users");
        return;
    }
    NUM(s, id, "221", "%s", "+i");
}

static void cmd_ison(irc_t *s, int id, int argc, char **argv)
{
    char out[512];
    size_t n = 0;
    out[0] = 0;
    for (int a = 0; a < argc; a++) {
        char buf[512];
        scpy(buf, sizeof buf, argv[a]);
        char *cur = buf;
        for (char *t = tok(&cur, ' '); t; t = tok(&cur, ' ')) {
            int k = find_nick(s, t), g = find_agent(s, t);
            const char *nm = k >= 0 ? s->c[k].nick : g >= 0 ? s->agents[g] : NULL;
            if (nm && n + strlen(nm) + 2 < sizeof out)
                n += (size_t) snprintf(out + n, sizeof out - n, "%s%s", n ? " " : "", nm);
        }
    }
    NUM(s, id, "303", ":%s", out);
}

static void cmd_userhost(irc_t *s, int id, int argc, char **argv)
{
    char out[512];
    size_t n = 0;
    out[0] = 0;
    for (int a = 0; a < argc && a < 5; a++) {
        int k = find_nick(s, argv[a]);
        if (k >= 0)
            n += (size_t) snprintf(out + n, sizeof out - n, "%s%s=%c%s@zxv", n ? " " : "",
                                   s->c[k].nick, s->c[k].away ? '-' : '+', s->c[k].user);
    }
    NUM(s, id, "302", ":%s", out);
}

/* ---------------- dispatch ---------------- */

static void line(irc_t *s, int id, char *l)
{
    irc_client_t *c = &s->c[id];
    /* E1: client tags are read past */
    if (*l == '@') {
        char *sp = strchr(l, ' ');
        if (!sp) return;
        l = sp + 1;
    }
    while (*l == ' ') l++;
    if (*l == ':') { /* a client's own prefix is ignored */
        char *sp = strchr(l, ' ');
        if (!sp) return;
        l = sp + 1;
    }
    while (*l == ' ') l++;
    if (!*l) return;
    char *argv[MAX_PARAMS];
    int argc = 0;
    char *cmd = l;
    char *p = strchr(l, ' ');
    if (p) {
        *p++ = 0;
        while (p && *p && argc < MAX_PARAMS) {
            while (*p == ' ') p++;
            if (!*p) break;
            if (*p == ':') {
                argv[argc++] = p + 1;
                break;
            }
            argv[argc++] = p;
            p = strchr(p, ' ');
            if (p) *p++ = 0;
        }
    }
    for (char *q = cmd; *q; q++)
        if (*q >= 'a' && *q <= 'z') *q = (char) (*q - 32);

    if (!strcmp(cmd, "PONG")) {
        c->ping_out = false;
        return;
    }
    if (!strcmp(cmd, "PING")) {
        sendf(s, id, ":%s PONG %s :%s", s->server, s->server, argc ? argv[0] : "");
        return;
    }
    if (!strcmp(cmd, "QUIT")) {
        close_link(s, id, argc ? argv[0] : "Quit");
        return;
    }
    if (!strcmp(cmd, "CAP")) {
        cmd_cap(s, id, argc, argv);
        return;
    }
    if (!strcmp(cmd, "PASS")) {
        if (c->registered) {
            NUM(s, id, "462", ":%s", "You may not reregister");
        } else if (argc < 1) {
            NUM(s, id, "461", "PASS :%s", "Not enough parameters");
        } else {
            /* the gateway password, compared without an early exit */
            size_t a = strlen(argv[0]), b = strlen(s->pass);
            unsigned diff = (unsigned) (a ^ b);
            for (size_t i = 0; i < b; i++)
                diff |= (unsigned) (s->pass[i] ^ (i < a ? argv[0][i] : 0));
            c->pass_ok = s->pass[0] == 0 || diff == 0;
        }
        return;
    }
    if (!strcmp(cmd, "NICK")) {
        cmd_nick(s, id, argc, argv);
        return;
    }
    if (!strcmp(cmd, "USER")) {
        if (c->registered) {
            NUM(s, id, "462", ":%s", "You may not reregister");
        } else if (argc < 4) {
            NUM(s, id, "461", "USER :%s", "Not enough parameters");
        } else {
            char u[16];
            size_t k = 0;
            for (const char *q = argv[0]; *q && k < sizeof u - 1; q++)
                if ((unsigned char) *q > ' ' && *q != '@' && *q != '!') u[k++] = *q;
            u[k] = 0;
            scpy(c->user, sizeof c->user, k ? u : "user");
            scpy(c->real, sizeof c->real, argv[3]);
            try_register(s, id);
        }
        return;
    }
    if (!c->registered) {
        NUM(s, id, "451", ":%s", "You have not registered");
        return;
    }
    if (!strcmp(cmd, "PRIVMSG"))
        cmd_msg(s, id, argc, argv, false);
    else if (!strcmp(cmd, "NOTICE"))
        cmd_msg(s, id, argc, argv, true);
    else if (!strcmp(cmd, "JOIN"))
        cmd_join(s, id, argc, argv);
    else if (!strcmp(cmd, "PART"))
        cmd_part(s, id, argc, argv);
    else if (!strcmp(cmd, "TOPIC"))
        cmd_topic(s, id, argc, argv);
    else if (!strcmp(cmd, "NAMES"))
        cmd_names(s, id, argc, argv);
    else if (!strcmp(cmd, "LIST"))
        cmd_list(s, id);
    else if (!strcmp(cmd, "WHO"))
        cmd_who(s, id, argc, argv);
    else if (!strcmp(cmd, "WHOIS"))
        cmd_whois(s, id, argc, argv);
    else if (!strcmp(cmd, "MODE"))
        cmd_mode(s, id, argc, argv);
    else if (!strcmp(cmd, "MOTD")) {
        NUM(s, id, "375", ":- %s message of the day -", s->server);
        for (size_t i = 0; i < sizeof MOTD / sizeof MOTD[0]; i++)
            NUM(s, id, "372", ":- %s", MOTD[i]);
        NUM(s, id, "376", ":%s", "End of /MOTD command");
    } else if (!strcmp(cmd, "AWAY")) {
        c->away = argc >= 1 && argv[0][0];
        if (c->away)
            NUM(s, id, "306", ":%s", "You have been marked as being away");
        else
            NUM(s, id, "305", ":%s", "You are no longer marked as being away");
    } else if (!strcmp(cmd, "ISON"))
        cmd_ison(s, id, argc, argv);
    else if (!strcmp(cmd, "USERHOST"))
        cmd_userhost(s, id, argc, argv);
    else
        NUM(s, id, "421", "%s :Unknown command", cmd);
}

/* E4: one line costs one token */
static bool flood_ok(irc_t *s, irc_client_t *c)
{
    uint64_t now = s->now;
    if (now > c->tok_ms) {
        uint64_t add = (now - c->tok_ms) / IRC_FLOOD_MS;
        if (add) {
            c->tokens =
                (uint32_t) (c->tokens + add > IRC_FLOOD_BURST ? IRC_FLOOD_BURST : c->tokens + add);
            c->tok_ms += add * IRC_FLOOD_MS;
        }
    } else {
        c->tok_ms = now;
    }
    if (c->tokens == 0) return false;
    c->tokens--;
    return true;
}

void irc_input(irc_t *s, int id, const char *p, uint32_t n)
{
    if (id < 0 || id >= (int) IRC_MAX_CLIENTS || !s->c[id].used) return;
    irc_client_t *c = &s->c[id];
    s->now = now_of(s);
    c->last_ms = s->now;
    for (uint32_t i = 0; i < n && !c->closing; i++) {
        char ch = p[i];
        if (ch == '\n') {
            if (c->discard) {
                c->discard = false;
                c->in_len = 0;
                continue;
            }
            if (c->in_len && c->in[c->in_len - 1] == '\r') c->in_len--;
            c->in[c->in_len] = 0;
            c->in_len = 0;
            /* a NUL inside a line ends it: what follows is not read */
            if (!flood_ok(s, c)) {
                close_link(s, id, "Excess Flood");
                return;
            }
            line(s, id, c->in);
            continue;
        }
        if (c->discard) continue;
        if (c->in_len >= IRC_LINE_IN) {
            NUM(s, id, "417", ":%s", "Input line was too long");
            c->discard = true;
            continue;
        }
        c->in[c->in_len++] = ch;
    }
}
