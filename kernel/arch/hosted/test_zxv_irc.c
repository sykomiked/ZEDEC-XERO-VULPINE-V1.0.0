/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_irc.c — the IRC engine (zxv_irc.h E1-E9) with no network: lines
 * in, lines out, a counting signer and a fixed clock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_irc.h"

static int failures = 0, checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static irc_t S;
static uint64_t NOW = 1770000000000ull;
static int SIGNED, SIGN_OK = 1, SERVED, RELAYED;
static char LAST_SIGNED[512], LAST_SERVED[512], LAST_RELAY[512];
static char OUT[IRC_MAX_CLIENTS][IRC_OUT_CAP + 1];

static uint64_t clock_(void *ctx)
{
    (void) ctx;
    return NOW;
}
static bool sign_(void *ctx, const irc_msg_t *m, char *msgid)
{
    (void) ctx;
    if (!SIGN_OK) return false;
    SIGNED++;
    snprintf(LAST_SIGNED, sizeof LAST_SIGNED, "%s|%s|%c|%s", m->target, m->nick, m->truth, m->text);
    snprintf(msgid, IRC_MSGID_LEN + 1, "%032x", SIGNED);
    return true;
}
static void serve_(void *ctx, irc_t *s, const irc_msg_t *m)
{
    (void) ctx;
    SERVED++;
    snprintf(LAST_SERVED, sizeof LAST_SERVED, "%s|%s|%c|%s", m->target, m->nick, m->truth, m->text);
    /* the agent answers where it was asked: the channel, or the asker */
    irc_say(s, m->target[0] == '#' ? m->target : m->nick, "swarm", 'T',
            "answer line one\nline two");
}
static void relay_(void *ctx, irc_t *s, const irc_msg_t *m)
{
    (void) ctx;
    (void) s;
    RELAYED++;
    snprintf(LAST_RELAY, sizeof LAST_RELAY, "%s|%s|%c|%s", m->target, m->nick, m->truth, m->text);
}

static const irc_hooks_t HOOKS = {NULL, sign_, serve_, relay_, clock_};

static void reset(const char *pass)
{
    irc_init(&S, "zxvd.test", pass, &HOOKS);
    irc_reserve(&S, "#swarm", "swarm", "Ask the swarm; answers are advisory");
    SIGNED = SERVED = RELAYED = 0;
    SIGN_OK = 1;
    memset(OUT, 0, sizeof OUT);
}

static int PACE = 1; /* advance the clock a flood step per line (E4) */

static void say(int id, const char *line)
{
    char buf[8192];
    if (PACE) NOW += IRC_FLOOD_MS;
    snprintf(buf, sizeof buf, "%s\r\n", line);
    irc_input(&S, id, buf, (uint32_t) strlen(buf));
}

/* drain a connection's output into OUT[id] (appending) and return it */
static const char *out(int id)
{
    size_t n = strlen(OUT[id]);
    n += irc_output(&S, id, OUT[id] + n, (uint32_t) (IRC_OUT_CAP - n));
    OUT[id][n] = 0;
    return OUT[id];
}
static void clear(int id)
{
    out(id);
    OUT[id][0] = 0;
}
static bool has(int id, const char *needle)
{
    return strstr(out(id), needle) != NULL;
}

static int client(const char *nick, const char *pass)
{
    int id = irc_open(&S, NOW);
    char l[128];
    if (pass) {
        snprintf(l, sizeof l, "PASS %s", pass);
        say(id, l);
    }
    snprintf(l, sizeof l, "NICK %s", nick);
    say(id, l);
    snprintf(l, sizeof l, "USER %s 0 * :Test %s", nick, nick);
    say(id, l);
    return id;
}

static void test_registration(void)
{
    reset("s3cret");
    int a = client("fox", "s3cret");
    CHECK(has(a, ":zxvd.test 001 fox :Welcome"), "E2 welcome after PASS NICK USER");
    CHECK(has(a, " 005 fox ") && has(a, "NICKLEN=30"), "E2 ISUPPORT");
    CHECK(has(a, " 376 fox "), "E2 MOTD");
    CHECK(has(a, "[N] neither"), "the MOTD explains the truth tag");

    int b = client("hare", "wrong");
    CHECK(has(b, " 464 hare :Password incorrect") && has(b, "ERROR :Closing link (Bad password)"),
          "E2 a wrong password closes");
    CHECK(irc_closing(&S, b), "E2 marked for close");
    irc_drop(&S, b, NULL);
    int c = client("owl", NULL);
    CHECK(has(c, " 464 "), "E2 a missing password closes");
    irc_drop(&S, c, NULL);

    /* CAP holds registration until END */
    int d = irc_open(&S, NOW);
    say(d, "CAP LS 302");
    say(d, "PASS s3cret");
    say(d, "NICK lynx");
    say(d, "USER lynx 0 * :Lynx");
    CHECK(has(d, "CAP * LS :message-tags server-time echo-message") && !has(d, " 001 "),
          "E2 CAP LS holds registration");
    say(d, "CAP REQ :message-tags server-time");
    CHECK(has(d, "CAP lynx ACK :message-tags server-time"), "E2 CAP REQ ACK");
    say(d, "CAP REQ :message-tags sasl");
    CHECK(has(d, "CAP lynx NAK :message-tags sasl"), "E2 CAP REQ is all or nothing");
    say(d, "CAP END");
    CHECK(has(d, " 001 lynx "), "E2 CAP END registers");
    say(d, "CAP LIST");
    CHECK(has(d, "LIST :message-tags server-time"), "CAP LIST");

    /* E2: before registration only a few commands */
    int e = irc_open(&S, NOW);
    say(e, "JOIN #x");
    CHECK(has(e, " 451 * :You have not registered"), "E2 451 before registration");
    say(e, "PING :abc");
    CHECK(has(e, "PONG zxvd.test :abc"), "PING before registration");
    say(e, "QUIT :bye");
    CHECK(irc_closing(&S, e), "QUIT closes");

    /* E3 nicknames */
    int f = irc_open(&S, NOW);
    say(f, "PASS s3cret");
    say(f, "NICK 9lives");
    CHECK(has(f, " 432 * 9lives "), "E3 a leading digit refused");
    say(f, "NICK FOX");
    CHECK(has(f, " 433 * FOX "), "E3 nicknames ignore case");
    say(f, "NICK Swarm");
    CHECK(has(f, " 433 * Swarm "), "E3 an agent's nick is reserved");
    say(f, "NICK abcdefghijabcdefghijabcdefghijX");
    CHECK(has(f, " 432 "), "E3 31 bytes refused");
    say(f, "NICK");
    CHECK(has(f, " 431 "), "E3 no nick");
    say(f, "NICK we{a}sel");
    say(f, "USER w 0 * :W");
    CHECK(has(f, " 001 we{a}sel "), "E3 brace nick accepted");
    say(f, "USER w 0 * :W");
    CHECK(has(f, " 462 "), "no reregistering");
    say(f, "FROB");
    CHECK(has(f, " 421 we{a}sel FROB "), "421 unknown command");
    CHECK(irc_clients(&S) == 4, "clients counted");
}

static void test_channels(void)
{
    reset(NULL);
    int a = client("fox", NULL), b = client("hare", NULL);
    clear(a);
    clear(b);
    say(a, "JOIN #den");
    CHECK(has(a, ":fox!fox@zxv JOIN #den"), "E5 JOIN echoed");
    CHECK(has(a, " 331 fox #den ") && has(a, " 353 fox = #den :fox") && has(a, " 366 fox #den "),
          "E5 topic and names on join");
    say(b, "join #DEN,#swarm");
    CHECK(has(a, ":hare!hare@zxv JOIN #den"), "E5 members see a join (case-insensitive name)");
    CHECK(has(b, " 332 hare #swarm :Ask the swarm") && has(b, "353 hare = #swarm :swarm hare"),
          "E8 reserved channel lists its agent and topic");
    say(b, "TOPIC #swarm :mine");
    CHECK(has(b, " 482 hare #swarm "), "E8 reserved topic is fixed");
    say(a, "TOPIC #den :burrow talk");
    CHECK(has(b, ":fox!fox@zxv TOPIC #den :burrow talk"), "TOPIC set and broadcast");
    say(b, "TOPIC #den");
    CHECK(has(b, " 332 hare #den :burrow talk"), "TOPIC read");
    say(a, "LIST");
    CHECK(has(a, " 322 fox #den 2 :burrow talk") && has(a, " 322 fox #swarm 2 :"), "LIST");
    say(a, "WHO #swarm");
    CHECK(has(a, " 352 fox #swarm agent zxv zxvd.test swarm H ") && has(a, " 315 "), "WHO");
    say(a, "WHOIS swarm");
    CHECK(has(a, " 311 fox swarm agent ") && has(a, " 318 "), "WHOIS agent");
    say(a, "WHOIS nobody");
    CHECK(has(a, " 401 fox nobody "), "WHOIS unknown");
    say(a, "MODE #den");
    CHECK(has(a, " 324 fox #den +nt"), "MODE channel");
    say(a, "MODE #den +o hare");
    CHECK(has(a, " 482 fox #den "), "E5 no modes to change");
    say(a, "MODE fox");
    CHECK(has(a, " 221 fox +i"), "MODE self");
    say(a, "ISON hare swarm ghost");
    CHECK(has(a, " 303 fox :hare swarm"), "ISON");
    say(a, "USERHOST hare");
    CHECK(has(a, " 302 fox :hare=+hare@zxv"), "USERHOST");
    say(b, "AWAY :gone");
    CHECK(has(b, " 306 "), "AWAY");
    say(a, "JOIN bad");
    CHECK(has(a, " 403 fox bad "), "E5 a name without # refused");
    say(a, "PART #nowhere");
    CHECK(has(a, " 403 fox #nowhere "), "PART unknown");
    say(b, "PART #den :off");
    CHECK(has(a, ":hare!hare@zxv PART #den :off"), "PART broadcast");
    say(b, "PART #den");
    CHECK(has(b, " 442 hare #den "), "PART when not a member");
    say(a, "NICK vixen");
    CHECK(has(a, ":fox!fox@zxv NICK :vixen"), "NICK change seen by self");
    /* the channel goes when its last member leaves; reserved ones stay */
    say(a, "JOIN 0");
    say(a, "LIST");
    CHECK(!has(a, " 322 vixen #den "), "E5 an empty channel is gone");
    say(b, "PART #swarm");
    say(a, "LIST");
    CHECK(has(a, " 322 vixen #swarm 1 "), "E8 a reserved channel stays");
    /* E5 limits */
    char l[64];
    for (unsigned i = 0; i < IRC_MAX_JOIN + 1; i++) {
        snprintf(l, sizeof l, "JOIN #c%u", i);
        say(a, l);
    }
    CHECK(has(a, " 405 vixen #c16 "), "E5 IRC_MAX_JOIN per connection");
    irc_drop(&S, a, NULL);
}

static void test_messages(void)
{
    reset(NULL);
    int a = client("fox", NULL), b = client("hare", NULL), c = client("owl", NULL);
    say(b, "CAP REQ :message-tags server-time");
    say(a, "JOIN #den");
    say(b, "JOIN #den");
    say(c, "JOIN #den");
    clear(a);
    clear(b);
    clear(c);
    say(a, "PRIVMSG #den :hello all");
    CHECK(SIGNED == 1 && !strcmp(LAST_SIGNED, "#den|fox|N|hello all"), "E7 signed before delivery");
    CHECK(has(c, ":fox!fox@zxv PRIVMSG #den :[N] hello all"), "E6 no claim is [N]");
    CHECK(has(b, "@time=2026-02-02T02:4") &&
              has(b, "Z;msgid=00000000000000000000000000000001;"
                     "zxv/truth=N :fox!fox@zxv PRIVMSG #den :[N] hello all"),
          "E6 E7 tags for a client that asked");
    CHECK(!has(a, "hello all"), "no echo without echo-message");
    CHECK(RELAYED == 1 && !strcmp(LAST_RELAY, "#den|fox|N|hello all"), "E8 relay hook");
    say(a, "CAP REQ echo-message");
    say(a, "PRIVMSG #den :[T] the den is warm");
    CHECK(has(a, ":fox!fox@zxv PRIVMSG #den :[T] the den is warm"), "echo-message");
    CHECK(has(c, "PRIVMSG #den :[T] the den is warm"), "E6 a stated T");
    CHECK(!strcmp(LAST_SIGNED, "#den|fox|T|the den is warm"),
          "E6 the body is signed apart from the tag");
    say(a, "PRIVMSG #den :[B] cold and warm");
    CHECK(has(c, ":[B] cold and warm"), "E6 B");
    say(a, "PRIVMSG #den :[F] snow in june");
    CHECK(has(c, ":[F] snow in june"), "E6 F");
    say(a, "PRIVMSG #den :[X] not a tag");
    CHECK(has(c, ":[N] [X] not a tag"), "E6 an unknown tag is text");
    say(a, "PRIVMSG #den :[T]nospace");
    CHECK(has(c, ":[N] [T]nospace"), "E6 the tag needs its space");
    say(a, "PRIVMSG #den :\001ACTION [T] waves\001");
    CHECK(has(c, ":\001ACTION [T] waves\001"), "E6 inside ACTION");
    say(a, "PRIVMSG #den :\001ACTION stretches\001");
    CHECK(has(c, ":\001ACTION [N] stretches\001"), "E6 ACTION without a tag");
    say(a, "PRIVMSG #den :\001VERSION\001");
    CHECK(has(a, "only CTCP ACTION crosses"), "E6 other CTCP refused");
    CHECK(!has(c, "VERSION"), "E6 other CTCP not relayed");
    say(a, "PRIVMSG hare :[T] psst");
    CHECK(has(b, "PRIVMSG hare :[T] psst") && !has(c, "psst"), "private message");
    say(a, "NOTICE #den :[F] notice me");
    CHECK(has(c, ":fox!fox@zxv NOTICE #den :[F] notice me"), "NOTICE tagged");
    /* refusals */
    say(a, "PRIVMSG #nowhere :x");
    CHECK(has(a, " 403 fox #nowhere "), "unknown channel");
    say(a, "PRIVMSG ghost :x");
    CHECK(has(a, " 401 fox ghost "), "unknown nick");
    say(a, "PRIVMSG #den");
    CHECK(has(a, " 412 "), "no text");
    say(a, "PRIVMSG #den :[T] ");
    CHECK(has(a, " 412 "), "only a tag is no text");
    say(a, "PRIVMSG");
    CHECK(has(a, " 411 "), "no recipient");
    say(a, "PRIVMSG #den,hare :x");
    CHECK(has(a, " 407 "), "one target at a time");
    char big[IRC_TEXT_MAX + 32];
    memset(big, 'z', sizeof big);
    memcpy(big, "PRIVMSG #den :", 14);
    big[14 + IRC_TEXT_MAX + 1] = 0;
    say(a, big);
    CHECK(has(a, " 417 fox :Text longer"), "E9 text limit");
    big[14 + IRC_TEXT_MAX] = 0;
    int before = SIGNED;
    say(a, big);
    CHECK(SIGNED == before + 1, "E9 exactly the limit is fine");
    say(b, "PART #den");
    say(b, "PRIVMSG #den :x");
    CHECK(has(b, " 404 hare #den "), "must join to speak");
    say(b, "NOTICE #nowhere :x");
    CHECK(!has(b, " 403 hare #nowhere "), "NOTICE never gets an error reply");
    /* E7: no signature, no delivery */
    SIGN_OK = 0;
    clear(c);
    say(a, "PRIVMSG #den :unsigned");
    CHECK(!has(c, "unsigned") && has(a, "NOTICE fox :Not delivered"),
          "E7 unsigned is not delivered");
    CHECK(S.refused == 1, "refusals counted");
    SIGN_OK = 1;
}

static void test_agents(void)
{
    reset(NULL);
    int a = client("fox", NULL), b = client("hare", NULL);
    say(a, "JOIN #swarm");
    say(b, "JOIN #swarm");
    clear(a);
    clear(b);
    say(a, "PRIVMSG #swarm :[T] what is a fox?");
    CHECK(SERVED == 1 && !strcmp(LAST_SERVED, "#swarm|fox|T|what is a fox?"),
          "E8 served after delivery");
    CHECK(has(b, ":swarm!agent@zxv PRIVMSG #swarm :[T] answer line one") &&
              has(b, ":swarm!agent@zxv PRIVMSG #swarm :[T] line two"),
          "E8 the answer, line by line");
    CHECK(has(a, "[T] answer line one"), "the asker hears the answer too");
    CHECK(SIGNED == 3, "E7 the agent's lines are signed too");
    say(b, "PRIVMSG swarm :private question");
    CHECK(SERVED == 2 && !strcmp(LAST_SERVED, "swarm|hare|N|private question"),
          "E8 private to agent");
    CHECK(has(b, ":swarm!agent@zxv PRIVMSG hare :[T] answer line one"), "E8 answered privately");
    say(b, "NOTICE swarm :hi");
    CHECK(SERVED == 2, "E8 a NOTICE is not served");
    /* irc_say splitting */
    char longtxt[1200];
    memset(longtxt, 'a', sizeof longtxt);
    memcpy(longtxt + IRC_TEXT_MAX - 1, "\xc3\xa9", 2); /* é across the cut */
    longtxt[sizeof longtxt - 1] = 0;
    int before = SIGNED;
    CHECK(irc_say(&S, "#swarm", "swarm", 'B', longtxt) == 3, "E9 split at the limit");
    CHECK(SIGNED == before + 3, "every piece signed");
    CHECK(has(a, ":[B] aaa"), "split pieces keep the tag");
    CHECK(strstr(OUT[a], "\xc3\r") == NULL, "E9 never splits a UTF-8 sequence");
    CHECK(irc_say(&S, "#nowhere", "swarm", 'T', "x") == -1, "say to an unknown channel");
    CHECK(irc_say(&S, "#swarm", "swarm", 'Q', "x") == -1, "say with a bad truth");
    CHECK(irc_say(&S, "fox", "swarm", 'N', "\n\nhi\r\n") == 1, "blank lines skipped");
    CHECK(irc_reserve(&S, "#swarm", "swarm", "x") == -1, "reserve twice");
    CHECK(irc_reserve(&S, "nohash", "x", "x") == -1, "reserve a bad name");
    CHECK(irc_truth_of("[B] x", NULL) == 'B', "truth_of");
}

static void test_limits(void)
{
    reset(NULL);
    int ids[IRC_MAX_CLIENTS];
    for (unsigned i = 0; i < IRC_MAX_CLIENTS; i++) ids[i] = irc_open(&S, NOW);
    CHECK(ids[IRC_MAX_CLIENTS - 1] >= 0 && irc_open(&S, NOW) == -1, "E1 connections limited");
    for (unsigned i = 0; i < IRC_MAX_CLIENTS; i++) irc_drop(&S, ids[i], NULL);
    CHECK(irc_clients(&S) == 0, "drops forget");

    /* E1: an over-long line is refused, the next one read */
    int a = client("fox", NULL);
    static char huge[IRC_LINE_IN + 100];
    memset(huge, 'x', sizeof huge);
    huge[sizeof huge - 1] = 0;
    say(a, huge);
    CHECK(has(a, " 417 fox :Input line was too long"), "E1 long line");
    say(a, "PING :after");
    CHECK(has(a, "PONG zxvd.test :after"), "E1 the next line is read");
    /* client tags and a prefix are read past */
    say(a, "@+typing=active;label=x :fox PING :tagged");
    CHECK(has(a, "PONG zxvd.test :tagged"), "E1 client tags skipped");
    /* split input */
    irc_input(&S, a, "PI", 2);
    irc_input(&S, a, "NG :sp\r", 7);
    irc_input(&S, a, "\n", 1);
    CHECK(has(a, "PONG zxvd.test :sp"), "lines split across reads");

    /* E4 flood */
    int b = client("hare", NULL);
    PACE = 0;
    for (int i = 0; i < 40; i++) say(b, "PING :f");
    PACE = 1;
    CHECK(irc_closing(&S, b) && has(b, "Excess Flood"), "E4 flood closes");
    irc_drop(&S, b, NULL);
    int c = client("owl", NULL);
    for (int i = 0; i < 40; i++) {
        NOW += IRC_FLOOD_MS;
        say(c, "PING :slow");
    }
    CHECK(!irc_closing(&S, c), "E4 a steady pace is fine");

    /* E4 ping and timeout */
    clear(c);
    NOW += IRC_PING_MS;
    irc_tick(&S, NOW);
    CHECK(has(c, "PING :zxvd.test"), "E4 a quiet connection is pinged");
    say(c, "PONG :zxvd.test");
    NOW += IRC_PING_MS - 1;
    irc_tick(&S, NOW);
    CHECK(!irc_closing(&S, c), "E4 answered in time");
    NOW += 2ull * IRC_PING_MS;
    irc_tick(&S, NOW);
    irc_tick(&S, NOW + 2ull * IRC_PING_MS);
    CHECK(irc_closing(&S, c) && has(c, "Ping timeout"), "E4 no answer closes");

    /* E4 SendQ: a client that never reads */
    reset(NULL);
    int r = client("reader", NULL), w = client("writer", NULL);
    say(r, "JOIN #flood");
    say(w, "JOIN #flood");
    char msg[IRC_TEXT_MAX + 20];
    memset(msg, 'q', sizeof msg);
    memcpy(msg, "PRIVMSG #flood :", 16);
    msg[16 + IRC_TEXT_MAX] = 0;
    for (int i = 0; i < 400 && !irc_closing(&S, r); i++) {
        NOW += IRC_FLOOD_MS;
        say(w, msg);
    }
    CHECK(irc_closing(&S, r), "E4 SendQ exceeded closes");
    char tail[256];
    uint32_t got = 0, n;
    static char sink[IRC_OUT_CAP];
    while ((n = irc_output(&S, r, sink, sizeof sink))) got = n;
    memcpy(tail, sink + (got > 60 ? got - 60 : 0), got > 60 ? 60 : got);
    tail[got > 60 ? 60 : got] = 0;
    CHECK(strstr(tail, "SendQ exceeded") != NULL, "E4 the last line says why");
    /* a dropped member's QUIT reaches the others */
    clear(w);
    irc_drop(&S, r, "gone");
    CHECK(has(w, ":reader!reader@zxv QUIT :gone"), "QUIT on drop");
    say(w, "QUIT");
    CHECK(has(w, "ERROR :Closing link (Quit)"), "QUIT answered with ERROR");
}

int main(void)
{
    test_registration();
    test_channels();
    test_messages();
    test_agents();
    test_limits();
    printf("%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_zxv_irc\n");
        return 1;
    }
    printf("[PASS] test_zxv_irc\n");
    return 0;
}
