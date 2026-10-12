/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_seats.c — zxvd_seats.c (seats.*) behind the real RPC layer on
 * the real seats module and ident's Argon2id. Replies are compared exactly;
 * ids and session tokens come from a counting random source, the clock is
 * the test's, and Argon2id runs with small parameters except in the test
 * that checks zxvd keeps the module default (64 MiB, 3 passes). */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "zxvd_groups.h"
#include "seats.h"

static int fails, checks;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;
static char REQB[4000], WANT[8000];

static const char *rpc(const char *line)
{
    CHECK(zxvd_rpc_handle_line(line, strlen(line), &OUT), "a reply for %.200s", line);
    return OUT.p ? OUT.p : "";
}

static void expect(const char *req, const char *want, int line)
{
    const char *got = rpc(req);
    checks++;
    if (strcmp(got, want)) {
        fails++;
        printf("  FAIL line %d\n    req  %.300s\n    want %.400s\n    got  %.400s\n", line, req,
               want, got);
    }
}
#define EXPECT(req, want) expect(req, want, __LINE__)

static const char *req(const char *method, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *req(const char *method, const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(REQB, sizeof REQB,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":", method);
    va_start(ap, fmt);
    k += vsnprintf(REQB + k, sizeof REQB - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(REQB + k, sizeof REQB - (size_t) k, "}");
    return REQB;
}

static const char *ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *ok(const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(WANT, sizeof WANT, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":");
    va_start(ap, fmt);
    k += vsnprintf(WANT + k, sizeof WANT - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(WANT + k, sizeof WANT - (size_t) k, "}");
    return WANT;
}

static const char *errc(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static const char *errc(int code, const char *fmt, ...)
{
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(WANT, sizeof WANT,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":%d,\"message\":\"%s\"}}", code,
             msg);
    return WANT;
}
#define ERR(st, why) errc(-32080, "%s: %s", st, why)
#define PERR(...)    errc(-32602, __VA_ARGS__)
#define DENIED       ERR("SEAT_ERR_DENIED", "this session may not do that")
#define NOSESS       errc(-32080, "SEAT_ERR_DENIED: unknown or ended session")
#define NOTFOUND     ERR("SEAT_ERR_NOTFOUND", "no seat with that id")
#define WRONG(w)     ok("{\"ok\":false,\"status\":\"SEAT_ERR_WRONG\",\"wait_ms\":%d}", w)

/* ----- fixtures ----- */

static uint8_t RCOUNT;
static bool RFAIL, RLAST; /* RLAST: 0x55 bytes, then the counter in the last */
static int fake_random(uint8_t *p, size_t n)
{
    if (RFAIL) return -1;
    for (size_t i = 0; i < n; i++) p[i] = RLAST && i + 1 < n ? 0x55 : RCOUNT++;
    return 0;
}

static uint64_t NOW;
static uint64_t clock_fn(void)
{
    return NOW;
}

static char DIR_[256], PATH[300];

static void fresh(const char *path)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_seats_group()) == 0, "add the seats group");
    zxvd_seats_set_random(fake_random);
    zxvd_seats_set_clock(clock_fn);
    zxvd_seats_set_kdf(64, 1);
    RCOUNT = 0;
    RFAIL = RLAST = false;
    NOW = 1000000000000ull;
    CHECK(zxvd_seats_open(path) == 0, "open %s", path ? path : "(memory)");
}

/* a string field of the last reply */
static const char *field(const char *key)
{
    static char v[4][200];
    static int k;
    k = (k + 1) & 3;
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = OUT.p ? strstr(OUT.p, pat) : NULL;
    v[k][0] = 0;
    if (!p) return v[k];
    p += strlen(pat);
    size_t n = 0;
    while (p[n] && p[n] != '"' && n < sizeof v[k] - 1) n++;
    memcpy(v[k], p, n);
    v[k][n] = 0;
    return v[k];
}

static char *slurp(const char *path)
{
    static char b[20000];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(b, 1, sizeof b - 1, f) : 0;
    if (f) fclose(f);
    b[n] = 0;
    return b;
}

static void spit(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

/* the seat line of a fresh unlocked seat with this id and name */
static const char *seat_json(const char *id, const char *name, const char *role, const char *lock,
                             unsigned color, unsigned wait)
{
    static char b[4][400];
    static int k;
    k = (k + 1) & 3;
    snprintf(b[k], sizeof b[k],
             "{\"id\":\"%s\",\"name\":\"%s\",\"role\":\"%s\",\"lock\":\"%s\",\"color\":%u,"
             "\"wait_ms\":%u}",
             id, name, role, lock, color, wait);
    return b[k];
}

/* add a seat and return its id */
static const char *add(const char *params)
{
    rpc(req("seats.add", "%s", params));
    return field("id");
}

/* unlock and return the session ("" on failure) */
static const char *unlock(const char *id, const char *pass)
{
    if (pass)
        rpc(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"%s\"}", id, pass));
    else
        rpc(req("seats.unlock", "{\"id\":\"%s\"}", id));
    return field("session");
}

/* ----- tests ----- */

static void test_group(void)
{
    fresh(NULL);
    const zxvd_group_t *g = zxvd_seats_group();
    CHECK(!g->regulated && !strcmp(g->feature, "seats") && g->n_methods == 8, "group shape");
    for (uint32_t i = 0; i < g->n_methods; i++)
        CHECK(g->methods[i].always, "L1: %s is always on", g->methods[i].name);
    const char *d = rpc(req("rpc.discover", "{}"));
    CHECK(strstr(d, "\"seats.list\",\"seats.add\",\"seats.remove\",\"seats.set_lock\","
                    "\"seats.unlock\",\"seats.lock\",\"seats.auto\",\"seats.set_auto\"]") != NULL,
          "discover: %.300s", d);
    EXPECT(req("features.get", "{}"),
           ok("{\"features\":[{\"name\":\"seats\",\"enabled\":false,\"regulated\":false,"
              "\"groups\":[{\"note\":\"the people who use this device and the login after boot "
              "(kernel/src/seats): Argon2id passphrases, sessions in memory; always on\","
              "\"methods\":8}]}],\"saved\":false}"));
    /* the switch is off and every method still answers */
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[],\"auto_enter\":true}"));
    EXPECT(req("seats.auto", "{}"), ok("{\"seat\":null}"));
}

static void test_add(void)
{
    fresh(NULL);
    EXPECT(req("seats.add", "{}"), PERR("missing string parameter \\\"name\\\""));
    EXPECT(req("seats.add", "{\"name\":\"A\",\"role\":\"boss\"}"),
           PERR("\\\"role\\\" must be \\\"owner\\\", \\\"person\\\" or \\\"guest\\\""));
    EXPECT(req("seats.add", "{\"name\":\"A\",\"role\":\"\"}"),
           PERR("\\\"role\\\" must be \\\"owner\\\", \\\"person\\\" or \\\"guest\\\""));
    EXPECT(req("seats.add", "{\"name\":\"A\",\"lock\":\"face\"}"),
           PERR("\\\"lock\\\" must be \\\"none\\\", \\\"pass\\\" or \\\"passkey\\\""));
    EXPECT(req("seats.add", "{\"name\":\"A\",\"color\":-1}"),
           PERR("\\\"color\\\" is out of range"));
    char big[300];
    memset(big, 'p', 257);
    big[257] = 0;
    EXPECT(req("seats.add", "{\"name\":\"A\",\"lock\":\"pass\",\"pass\":\"%s\"}", big),
           PERR("\\\"pass\\\" must be a string of at most 256 bytes"));
    EXPECT(req("seats.add", "{\"name\":\"A\",\"lock\":\"passkeypasskeyp\"}"),
           PERR("\\\"lock\\\" must be \\\"none\\\", \\\"pass\\\" or \\\"passkey\\\""));
    EXPECT(req("seats.add", "{\"name\":\"A\",\"lock\":\"passkeypasskeypa\"}"),
           PERR("\\\"lock\\\" must be a string of at most 15 bytes"));
    {
        char n[300];
        memset(n, 'n', 256);
        n[256] = 0;
        EXPECT(req("seats.add", "{\"name\":\"%s\"}", n),
               PERR("\\\"name\\\" must be a string of at most 255 bytes"));
        n[255] = 0;
        EXPECT(req("seats.add", "{\"name\":\"%s\"}", n),
               ERR("SEAT_ERR_NAME", "the name is empty, longer than 47 bytes or already used"));
    }
    EXPECT(req("seats.add", "{\"name\":\"\"}"),
           ERR("SEAT_ERR_NAME", "the name is empty, longer than 47 bytes or already used"));
    EXPECT(req("seats.add", "{\"name\":\"A\",\"lock\":\"pass\",\"pass\":\"abc\"}"),
           ERR("SEAT_ERR_ARG", "bad argument (a passphrase is 4 to 256 bytes)"));
    EXPECT(req("seats.add", "{\"name\":\"A\",\"lock\":\"pass\"}"),
           ERR("SEAT_ERR_ARG", "bad argument (a passphrase is 4 to 256 bytes)"));
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[],\"auto_enter\":true}"));

    /* the first seat needs no session and becomes the owner whatever role
     * was asked for; ids come from the random source */
    RCOUNT = 0;
    EXPECT(req("seats.add", "{\"name\":\"Ma \\u00e9\",\"role\":\"guest\",\"lock\":\"pass\","
                            "\"pass\":\"hunter22\",\"color\":7}"),
           ok("%s",
              seat_json("000102030405060708090a0b0c0d0e0f", "Ma \xc3\xa9", "owner", "pass", 7, 0)));
    CHECK(RCOUNT == 32, "id and salt drawn (%u)", RCOUNT);
    const char *owner = "000102030405060708090a0b0c0d0e0f";
    EXPECT(req("seats.add", "{\"name\":\"B\"}"), PERR("missing hex parameter \\\"session\\\""));
    EXPECT(req("seats.add", "{\"name\":\"B\",\"session\":\"00\"}"),
           PERR("\\\"session\\\" must be 32 bytes of hex"));
    EXPECT(req("seats.add", "{\"name\":\"B\",\"session\":\"%064d\"}", 0), NOSESS);

    RCOUNT = 0x40;
    char so[80];
    snprintf(so, sizeof so, "%s", unlock(owner, "hunter22"));
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"hunter22\"}", owner),
           ok("{\"ok\":true,\"wait_ms\":0,\"session\":\"606162636465666768696a6b6c6d6e6f707172737"
              "475767778797a7b7c7d7e7f\",\"id\":\"%s\"}",
              owner));
    CHECK(!strcmp(so, "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"),
          "the session is 32 random bytes: %s", so);

    /* a guest never has a lock; the default role is person, lock none */
    RCOUNT = 0x80;
    EXPECT(req("seats.add",
               "{\"name\":\"Guest\",\"role\":\"guest\",\"lock\":\"pass\",\"pass\":"
               "\"whatever\",\"session\":\"%s\"}",
               so),
           ok("%s", seat_json("808182838485868788898a8b8c8d8e8f", "Guest", "guest", "none", 0, 0)));
    RCOUNT = 0x90;
    EXPECT(req("seats.add", "{\"name\":\"Kid\",\"session\":\"%s\"}", so),
           ok("%s", seat_json("909192939495969798999a9b9c9d9e9f", "Kid", "person", "none", 0, 0)));
    EXPECT(req("seats.add", "{\"name\":\"Kid\",\"session\":\"%s\"}", so),
           ERR("SEAT_ERR_NAME", "the name is empty, longer than 47 bytes or already used"));
    char n47[60], n48[60];
    memset(n47, 'n', 47);
    n47[47] = 0;
    memset(n48, 'n', 48);
    n48[48] = 0;
    EXPECT(req("seats.add", "{\"name\":\"%s\",\"session\":\"%s\"}", n48, so),
           ERR("SEAT_ERR_NAME", "the name is empty, longer than 47 bytes or already used"));
    RCOUNT = 0xa0;
    EXPECT(req("seats.add", "{\"name\":\"%s\",\"lock\":\"passkey\",\"session\":\"%s\"}", n47, so),
           ok("%s", seat_json("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf", n47, "person", "passkey", 0, 0)));

    /* a person may not add */
    const char *kid = "909192939495969798999a9b9c9d9e9f";
    RCOUNT = 0xb0;
    char sk[80];
    snprintf(sk, sizeof sk, "%s", unlock(kid, NULL));
    CHECK(strlen(sk) == 64, "a seat with no lock unlocks with no passphrase");
    EXPECT(req("seats.add", "{\"name\":\"Other\",\"session\":\"%s\"}", sk), DENIED);
    EXPECT(req("seats.set_auto", "{\"auto_enter\":false,\"session\":\"%s\"}", sk), DENIED);

    /* L3: list shows no hash, salt or parameters, in the order added */
    const char *l = rpc(req("seats.list", "{}"));
    CHECK(!strstr(l, "hash") && !strstr(l, "salt") && !strstr(l, "kdf") && !strstr(l, "65536"),
          "L3: %s", l);
    char want[2000];
    snprintf(want, sizeof want, "{\"seats\":[%s,%s,%s,",
             seat_json(owner, "Ma \xc3\xa9", "owner", "pass", 7, 0),
             seat_json("808182838485868788898a8b8c8d8e8f", "Guest", "guest", "none", 0, 0),
             seat_json(kid, "Kid", "person", "none", 0, 0));
    snprintf(want + strlen(want), sizeof want - strlen(want), "%s],\"auto_enter\":true}",
             seat_json("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf", n47, "person", "passkey", 0, 0));
    EXPECT(req("seats.list", "{}"), ok("%s", want));
    zxvd_seats_set_kdf(1, 1); /* below Argon2id's minimum: the module refuses */
    EXPECT(req("seats.add",
               "{\"name\":\"K\",\"lock\":\"pass\",\"pass\":\"abcd\",\"session\":\"%s\"}", so),
           ERR("SEAT_ERR_KDF", "Argon2id failed"));
    zxvd_seats_set_kdf(64, 1);
    RFAIL = true;
    EXPECT(req("seats.add", "{\"name\":\"K\",\"session\":\"%s\"}", so),
           errc(-32603, "no random bytes from the OS"));
    RFAIL = false;
    EXPECT(req("seats.list", "{}"), ok("%s", want));
}

static void test_unlock(void)
{
    fresh(NULL);
    RCOUNT = 0;
    const char *o = add("{\"name\":\"O\",\"lock\":\"pass\",\"pass\":\"right one\"}");
    char owner[40];
    snprintf(owner, sizeof owner, "%s", o);
    EXPECT(req("seats.unlock", "{}"), PERR("missing hex parameter \\\"id\\\""));
    EXPECT(req("seats.unlock", "{\"id\":\"%032d\"}", 0), NOTFOUND);
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"passkey_ok\":1}", owner),
           PERR("\\\"passkey_ok\\\" must be true or false"));
    /* L7: four free tries, then 30 s, doubling */
    for (int i = 0; i < 4; i++)
        EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"wrong\"}", owner), WRONG(0));
    EXPECT(req("seats.unlock", "{\"id\":\"%s\"}", owner), WRONG(30000));
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[%s],\"auto_enter\":true}",
                                       seat_json(owner, "O", "owner", "pass", 0, 30000)));
    NOW += 10000;
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"right one\"}", owner),
           ok("{\"ok\":false,\"status\":\"SEAT_ERR_WAIT\",\"wait_ms\":20000}"));
    NOW += 20000;
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"wrong\"}", owner), WRONG(60000));
    NOW += 60000;
    /* a clock set back: the wait is cut to the module's maximum */
    NOW -= 100000000;
    EXPECT(req("seats.list", "{}"),
           ok("{\"seats\":[%s],\"auto_enter\":true}",
              seat_json(owner, "O", "owner", "pass", 0, SEAT_WAIT_MAX_MS)));
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"right one\"}", owner),
           ok("{\"ok\":false,\"status\":\"SEAT_ERR_WAIT\",\"wait_ms\":%u}", SEAT_WAIT_MAX_MS));
    NOW += SEAT_WAIT_MAX_MS;
    char s1[80];
    snprintf(s1, sizeof s1, "%s", unlock(owner, "right one"));
    CHECK(strlen(s1) == 64, "the right passphrase after the wait");
    /* a success clears the count */
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"wrong\"}", owner), WRONG(0));
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[%s],\"auto_enter\":true}",
                                       seat_json(owner, "O", "owner", "pass", 0, 0)));

    /* passkey: the host's verdict */
    char pk[40];
    rpc(req("seats.add", "{\"name\":\"P\",\"lock\":\"passkey\",\"session\":\"%s\"}", s1));
    snprintf(pk, sizeof pk, "%s", field("id"));
    EXPECT(req("seats.unlock", "{\"id\":\"%s\"}", pk), WRONG(0));
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"passkey_ok\":false}", pk), WRONG(0));
    RCOUNT = 0x10;
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"passkey_ok\":true}", pk),
           ok("{\"ok\":true,\"wait_ms\":0,\"session\":\"101112131415161718191a1b1c1d1e1f20212"
              "2232425262728292a2b2c2d2e2f\",\"id\":\"%s\"}",
              pk));
    RFAIL = true;
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"passkey_ok\":true}", pk),
           errc(-32603, "no random bytes"));
    RFAIL = false;

    /* seats.lock ends one session */
    EXPECT(req("seats.lock", "{}"), PERR("missing hex parameter \\\"session\\\""));
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", s1), ok("{\"ended\":true}"));
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", s1), ok("{\"ended\":false}"));
    EXPECT(req("seats.add", "{\"name\":\"Q\",\"session\":\"%s\"}", s1), NOSESS);
}

static void test_sessions(void)
{
    fresh(NULL);
    RCOUNT = 0;
    char owner[40], first[80], s[80];
    snprintf(owner, sizeof owner, "%s", add("{\"name\":\"O\"}"));
    RCOUNT = 1;
    snprintf(first, sizeof first, "%s", unlock(owner, NULL));
    /* L4: 64 sessions; the 65th replaces the oldest */
    for (int i = 0; i < 64; i++) {
        RCOUNT = (uint8_t) (2 + i);
        snprintf(s, sizeof s, "%s", unlock(owner, NULL));
    }
    EXPECT(req("seats.add", "{\"name\":\"Q\",\"session\":\"%s\"}", first), NOSESS);
    RCOUNT = 0xf0;
    EXPECT(req("seats.add", "{\"name\":\"Q\",\"session\":\"%s\"}", s),
           ok("%s", seat_json("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", "Q", "person", "none", 0, 0)));
    /* the second session of the loop (bytes 3..34) is still held */
    char again[80];
    {
        uint8_t b[32];
        for (int i = 0; i < 32; i++) b[i] = (uint8_t) (3 + i);
        for (int i = 0; i < 32; i++) snprintf(again + 2 * i, 3, "%02x", b[i]);
    }
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", again), ok("{\"ended\":true}"));
    /* after the lock the freed slot is used before any eviction */
    RCOUNT = 0x60;
    char fresh_s[80];
    snprintf(fresh_s, sizeof fresh_s, "%s", unlock(owner, NULL));
    RCOUNT = 4;
    for (int i = 0; i < 32; i++) snprintf(again + 2 * i, 3, "%02x", (unsigned) (4 + i));
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", again), ok("{\"ended\":true}"));
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", fresh_s), ok("{\"ended\":true}"));
}

static void test_last_byte(void)
{
    fresh(NULL);
    RLAST = true;
    RCOUNT = 1;
    char a[40], b[40], sa[80], sb[80], sb2[80];
    snprintf(a, sizeof a, "%s", add("{\"name\":\"A\",\"role\":\"owner\"}"));
    snprintf(sa, sizeof sa, "%s", unlock(a, NULL)); /* slot 0 */
    rpc(req("seats.add", "{\"name\":\"B\",\"role\":\"owner\",\"session\":\"%s\"}", sa));
    snprintf(b, sizeof b, "%s", field("id"));
    CHECK(!strncmp(a, b, 30) && strcmp(a, b), "ids differ in the last byte: %s %s", a, b);
    snprintf(sb, sizeof sb, "%s", unlock(b, NULL));
    CHECK(!strcmp(field("id"), b), "the right seat opens");
    snprintf(sb2, sizeof sb2, "%s", unlock(b, NULL));
    CHECK(!strncmp(sb, sb2, 62) && strcmp(sb, sb2), "tokens differ in the last byte");
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", sb2), ok("{\"ended\":true}"));
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", sb2), ok("{\"ended\":false}"));
    /* B removes A: A's session (slot 0) ends, B's stays */
    EXPECT(req("seats.remove", "{\"id\":\"%s\",\"session\":\"%s\"}", a, sb),
           ok("{\"removed\":\"%s\"}", a));
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", sa), ok("{\"ended\":false}"));
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", sb), ok("{\"ended\":true}"));
    RLAST = false;
}

static void test_roles(void)
{
    fresh(NULL);
    RCOUNT = 0;
    char owner[40], kid[40], so[80], sk[80], o2[40], s2[80];
    snprintf(owner, sizeof owner, "%s",
             add("{\"name\":\"O\",\"lock\":\"pass\",\"pass\":\"oooo\"}"));
    snprintf(so, sizeof so, "%s", unlock(owner, "oooo"));
    rpc(req("seats.add", "{\"name\":\"Kid\",\"session\":\"%s\"}", so));
    snprintf(kid, sizeof kid, "%s", field("id"));
    snprintf(sk, sizeof sk, "%s", unlock(kid, NULL));

    /* set_lock: params */
    EXPECT(req("seats.set_lock", "{\"id\":\"%s\",\"lock\":\"pass\"}", kid),
           PERR("missing hex parameter \\\"session\\\""));
    EXPECT(req("seats.set_lock", "{\"id\":\"%s\",\"session\":\"%s\"}", kid, sk),
           PERR("missing string parameter \\\"lock\\\""));
    EXPECT(req("seats.set_lock", "{\"id\":\"%032d\",\"lock\":\"none\",\"session\":\"%s\"}", 0, sk),
           NOTFOUND);
    /* a person sets their own lock, then must prove the old passphrase */
    EXPECT(req("seats.set_lock",
               "{\"id\":\"%s\",\"lock\":\"pass\",\"pass\":\"kkk\",\"session\":"
               "\"%s\"}",
               kid, sk),
           ERR("SEAT_ERR_ARG", "bad argument (a passphrase is 4 to 256 bytes)"));
    EXPECT(req("seats.set_lock",
               "{\"id\":\"%s\",\"lock\":\"pass\",\"pass\":\"kid-1\",\"session\":\"%s\"}", kid, sk),
           ok("%s", seat_json(kid, "Kid", "person", "pass", 0, 0)));
    /* changing your own passphrase lock without the old one is a wrong try */
    EXPECT(req("seats.set_lock", "{\"id\":\"%s\",\"lock\":\"none\",\"session\":\"%s\"}", kid, sk),
           ERR("SEAT_ERR_WRONG", "wrong passphrase or passkey"));
    EXPECT(req("seats.set_lock",
               "{\"id\":\"%s\",\"lock\":\"pass\",\"old_pass\":\"kid-0\","
               "\"pass\":\"kid-2\",\"session\":\"%s\"}",
               kid, sk),
           ERR("SEAT_ERR_WRONG", "wrong passphrase or passkey"));
    EXPECT(req("seats.set_lock",
               "{\"id\":\"%s\",\"lock\":\"pass\",\"old_pass\":\"kid-1\","
               "\"pass\":\"kid-2\",\"session\":\"%s\"}",
               kid, sk),
           ok("%s", seat_json(kid, "Kid", "person", "pass", 0, 0)));
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"kid-1\"}", kid), WRONG(0));
    CHECK(strlen(unlock(kid, "kid-2")) == 64, "the new passphrase opens the seat");
    /* a person may not touch another's lock, nor remove */
    EXPECT(req("seats.set_lock", "{\"id\":\"%s\",\"lock\":\"none\",\"session\":\"%s\"}", owner, sk),
           DENIED);
    EXPECT(req("seats.remove", "{\"id\":\"%s\",\"session\":\"%s\"}", owner, sk), DENIED);
    /* an owner resets anyone's lock without the old passphrase */
    EXPECT(
        req("seats.set_lock", "{\"id\":\"%s\",\"lock\":\"passkey\",\"session\":\"%s\"}", kid, so),
        ok("%s", seat_json(kid, "Kid", "person", "passkey", 0, 0)));
    EXPECT(req("seats.set_lock", "{\"id\":\"%s\",\"lock\":\"none\",\"session\":\"%s\"}", kid, so),
           ok("%s", seat_json(kid, "Kid", "person", "none", 0, 0)));
    /* the owner's own change needs the old one */
    EXPECT(req("seats.set_lock",
               "{\"id\":\"%s\",\"lock\":\"none\",\"old_pass\":\"oops\","
               "\"session\":\"%s\"}",
               owner, so),
           ERR("SEAT_ERR_WRONG", "wrong passphrase or passkey"));

    /* guests stay unlocked */
    rpc(req("seats.add", "{\"name\":\"G\",\"role\":\"guest\",\"session\":\"%s\"}", so));
    char g[40];
    snprintf(g, sizeof g, "%s", field("id"));
    EXPECT(req("seats.set_lock",
               "{\"id\":\"%s\",\"lock\":\"pass\",\"pass\":\"gggg\",\"session\":"
               "\"%s\"}",
               g, so),
           DENIED);

    /* remove: the last owner stays; the kid's sessions end with the seat */
    EXPECT(req("seats.remove", "{\"id\":\"%s\",\"session\":\"%s\"}", owner, so),
           ERR("SEAT_ERR_LAST_OWNER", "the device would be left with no owner"));
    EXPECT(req("seats.remove", "{\"session\":\"%s\"}", so),
           PERR("missing hex parameter \\\"id\\\""));
    EXPECT(req("seats.remove", "{\"id\":\"%s\",\"session\":\"%s\"}", kid, so),
           ok("{\"removed\":\"%s\"}", kid));
    EXPECT(req("seats.lock", "{\"session\":\"%s\"}", sk), ok("{\"ended\":false}"));
    EXPECT(req("seats.remove", "{\"id\":\"%s\",\"session\":\"%s\"}", kid, so), NOTFOUND);
    /* a second owner removes the first, whose session then ends */
    rpc(req("seats.add", "{\"name\":\"O2\",\"role\":\"owner\",\"session\":\"%s\"}", so));
    snprintf(o2, sizeof o2, "%s", field("id"));
    snprintf(s2, sizeof s2, "%s", unlock(o2, NULL));
    EXPECT(req("seats.remove", "{\"id\":\"%s\",\"session\":\"%s\"}", owner, s2),
           ok("{\"removed\":\"%s\"}", owner));
    EXPECT(req("seats.add", "{\"name\":\"Z\",\"session\":\"%s\"}", so), NOSESS);
    EXPECT(req("seats.list", "{}"),
           ok("{\"seats\":[%s,%s],\"auto_enter\":true}", seat_json(g, "G", "guest", "none", 0, 0),
              seat_json(o2, "O2", "owner", "none", 0, 0)));

    /* auto-enter: one seat with no lock, unless the owner turned it off */
    EXPECT(req("seats.auto", "{}"), ok("{\"seat\":null}"));
    EXPECT(req("seats.remove", "{\"id\":\"%s\",\"session\":\"%s\"}", g, s2),
           ok("{\"removed\":\"%s\"}", g));
    EXPECT(req("seats.auto", "{}"), ok("{\"seat\":\"%s\"}", o2));
    EXPECT(req("seats.set_auto", "{\"session\":\"%s\"}", s2),
           PERR("missing boolean parameter \\\"auto_enter\\\""));
    EXPECT(req("seats.set_auto", "{\"auto_enter\":false}"),
           PERR("missing hex parameter \\\"session\\\""));
    EXPECT(req("seats.set_auto", "{\"auto_enter\":false,\"session\":\"%s\"}", s2),
           ok("{\"auto_enter\":false}"));
    EXPECT(req("seats.auto", "{}"), ok("{\"seat\":null}"));
    EXPECT(req("seats.list", "{}"),
           ok("{\"seats\":[%s],\"auto_enter\":false}", seat_json(o2, "O2", "owner", "none", 0, 0)));
    EXPECT(req("seats.set_auto", "{\"auto_enter\":true,\"session\":\"%s\"}", s2),
           ok("{\"auto_enter\":true}"));
    EXPECT(req("seats.auto", "{}"), ok("{\"seat\":\"%s\"}", o2));
}

static void test_many(void)
{
    fresh(NULL);
    char so[80], name[16];
    RCOUNT = 0;
    add("{\"name\":\"O\"}");
    char owner[40];
    snprintf(owner, sizeof owner, "%s", field("id"));
    snprintf(so, sizeof so, "%s", unlock(owner, NULL));
    /* the roster grows past its first arrays; ids repeat with an 8-bit
     * counter, so draw them from a wider source */
    int okc = 0;
    for (int i = 0; i < 40; i++) {
        snprintf(name, sizeof name, "p%d", i);
        RCOUNT = (uint8_t) (i * 5 + 7);
        rpc(req("seats.add", "{\"name\":\"%s\",\"session\":\"%s\"}", name, so));
        okc += strstr(OUT.p, "\"result\"") != NULL;
    }
    CHECK(okc == 40, "40 seats added (%d)", okc);
    const char *l = rpc(req("seats.list", "{}"));
    CHECK(strstr(l, "\"name\":\"p0\"") && strstr(l, "\"name\":\"p39\""), "all listed");
}

static void test_file(void)
{
    struct stat st;
    snprintf(PATH, sizeof PATH, "%s/seats", DIR_);
    unlink(PATH);
    fresh(PATH);
    zxvd_seats_set_kdf(64, 2);
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[],\"auto_enter\":true}"));
    CHECK(stat(PATH, &st) != 0, "no file until a change");
    RCOUNT = 0;
    char owner[40], so[80], kid[40];
    snprintf(owner, sizeof owner, "%s",
             add("{\"name\":\"O w\",\"lock\":\"pass\",\"pass\":\"pw-owner\",\"color\":16766720}"));
    CHECK(stat(PATH, &st) == 0 && (st.st_mode & 0777) == 0600, "L8: the file is 0600 (%o)",
          (unsigned) (st.st_mode & 0777));
    char tmp[320];
    snprintf(tmp, sizeof tmp, "%s.tmp", PATH);
    CHECK(stat(tmp, &st) != 0, "no temporary file left");
    CHECK(!strncmp(slurp(PATH), "zxv-seats 1 1 1\n", 16), "auto_enter on is saved: %s",
          slurp(PATH));
    snprintf(so, sizeof so, "%s", unlock(owner, "pw-owner"));
    rpc(req("seats.add", "{\"name\":\"K\",\"role\":\"guest\",\"session\":\"%s\"}", so));
    snprintf(kid, sizeof kid, "%s", field("id"));
    rpc(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"nope\"}", owner));
    rpc(req("seats.set_auto", "{\"auto_enter\":false,\"session\":\"%s\"}", so));
    const char *f = slurp(PATH);
    char want[2000];
    char salt[33], hash[65];
    /* the salt came from the counter after the id; the hash is Argon2id's */
    snprintf(salt, sizeof salt, "101112131415161718191a1b1c1d1e1f");
    const char *line2 = strchr(f, '\n');
    CHECK(line2 && strlen(line2) > 120, "a seat line: %s", f);
    memcpy(hash, line2 ? line2 + 1 + 33 + 24 + 33 : "", 64);
    hash[64] = 0;
    snprintf(want, sizeof want,
             "zxv-seats 1 2 0\n"
             "%s 0 1 16766720 64 2 1 1 0 %s %s %064d 4f2077\n"
             "%s 2 0 0 0 0 0 0 0 %032d %064d %064d 4b\n"
             "end\n",
             owner, salt, hash, 0, kid, 0, 0, 0);
    CHECK(!strcmp(f, want), "L8: the file\n%s\nwant\n%s", f, want);
    const char *before = ok("{\"seats\":[%s,%s],\"auto_enter\":false}",
                            seat_json(owner, "O w", "owner", "pass", 16766720, 0),
                            seat_json(kid, "K", "guest", "none", 0, 0));
    char saved[1000];
    snprintf(saved, sizeof saved, "%s", before);
    EXPECT(req("seats.list", "{}"), saved);
    /* reopen: the roster comes back, the sessions do not */
    CHECK(zxvd_seats_open(PATH) == 0, "L9: reopen");
    EXPECT(req("seats.list", "{}"), saved);
    EXPECT(req("seats.add", "{\"name\":\"Z\",\"session\":\"%s\"}", so), NOSESS);
    /* the saved hash still verifies, and the failed try was saved */
    for (int i = 0; i < 4; i++)
        rpc(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"nope\"}", owner));
    CHECK(zxvd_seats_open(PATH) == 0, "reopen");
    EXPECT(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"pw-owner\"}", owner),
           ok("{\"ok\":false,\"status\":\"SEAT_ERR_WAIT\",\"wait_ms\":30000}"));
    NOW += 30000;
    snprintf(so, sizeof so, "%s", unlock(owner, "pw-owner"));
    CHECK(strlen(so) == 64, "the saved hash verifies after a restart");
    CHECK(zxvd_seats_open(PATH) == 0, "reopen");
    EXPECT(req("seats.list", "{}"), saved);

    /* L8: a change that cannot be saved is undone */
    char bad[340];
    snprintf(bad, sizeof bad, "%s/no-such-dir/seats", DIR_);
    fresh(bad);
    EXPECT(req("seats.add", "{\"name\":\"O\"}"),
           errc(-32603, "the seats file could not be written; nothing changed"));
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[],\"auto_enter\":true}"));
    CHECK(zxvd_seats_open(PATH) == 0, "reopen");
    snprintf(so, sizeof so, "%s", unlock(owner, "pw-owner"));
    snprintf(tmp, sizeof tmp, "%s.tmp", PATH);
    mkdir(tmp, 0700); /* the temporary name is taken by a folder: writes fail */
    EXPECT(req("seats.add", "{\"name\":\"Z\",\"session\":\"%s\"}", so),
           errc(-32603, "the seats file could not be written; nothing changed"));
    EXPECT(req("seats.set_auto", "{\"auto_enter\":true,\"session\":\"%s\"}", so),
           errc(-32603, "the seats file could not be written; nothing changed"));
    EXPECT(req("seats.remove", "{\"id\":\"%s\",\"session\":\"%s\"}", kid, so),
           errc(-32603, "the seats file could not be written; nothing changed"));
    EXPECT(req("seats.set_lock", "{\"id\":\"%s\",\"lock\":\"none\",\"session\":\"%s\"}", kid, so),
           errc(-32603, "the seats file could not be written; nothing changed"));
    EXPECT(req("seats.list", "{}"), saved);
    rmdir(tmp);
    CHECK(zxvd_seats_open(PATH) == 0, "reopen");
    EXPECT(req("seats.list", "{}"), saved);
    /* the rename fails when a folder holds the name: undone, no .tmp left */
    unlink(PATH);
    CHECK(zxvd_seats_open(PATH) == 0, "a missing file is an empty roster");
    mkdir(PATH, 0700);
    EXPECT(req("seats.add", "{\"name\":\"O\"}"),
           errc(-32603, "the seats file could not be written; nothing changed"));
    CHECK(stat(tmp, &st) != 0, "no temporary file left after a failed rename");
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[],\"auto_enter\":true}"));
    rmdir(PATH);
    /* a file that cannot be opened for another reason than being absent */
    char under[340];
    snprintf(under, sizeof under, "%s/seats.x/seats", DIR_);
    snprintf(tmp, sizeof tmp, "%s/seats.x", DIR_);
    spit(tmp, "a file, not a folder\n");
    CHECK(zxvd_seats_open(under) == -1, "a path under a file cannot be read");
    unlink(tmp);
    /* the longest path: 1099 bytes */
    char lp[1200];
    int k = snprintf(lp, sizeof lp, "%s", DIR_);
    while (k < 1099 - 8) k += snprintf(lp + k, sizeof lp - (size_t) k, "/d");
    while (k < 1099) lp[k++] = 'z';
    lp[k] = 0;
    CHECK(strlen(lp) == 1099 && zxvd_seats_open(lp) == 0, "a 1099-byte path (%u)",
          (unsigned) strlen(lp));
    lp[1099] = 'z';
    lp[1100] = 0;
    CHECK(zxvd_seats_open(lp) == -1, "a 1100-byte path is too long");
    CHECK(zxvd_seats_open("") == 0, "in memory");
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[],\"auto_enter\":true}"));
    char longp[1200];
    memset(longp, 'a', sizeof longp - 1);
    longp[sizeof longp - 1] = 0;
    CHECK(zxvd_seats_open(longp) == -1, "a path too long");
}

/* L9: a damaged file stops the open */
static void test_damaged(void)
{
    static const char *const GOOD_SEAT =
        "00112233445566778899aabbccddeeff 0 0 5 0 0 0 0 0 00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f";
    static const char *const BAD[] = {
        "",
        "zxv-seats 1 0 1\n",
        "zxv-seats 1 0 1\nend",
        "zxv-seats 2 0 1\nend\n",
        "zxv-seat 1 0 1\nend\n",
        "zxv-seats 1 0 2\nend\n",
        "zxv-seats 1 00 1\nend\n",
        "zxv-seats 1 0 1 x\nend\n",
        "zxv-seats 1 4097 1\nend\n",
        "zxv-seats 1 1 1\nend\n",
        "zxv-seats 1 0 1\nend\nmore\n",
        "zxv-seats 1 0 1\nended\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 0 0 0 0 0\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeFF 0 0 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 3 0 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 3 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 0 0 0 0 0 "
        "0000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "000000000000000000000000000000000000000000000000000000000000000g "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 \nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f00\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f 1\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 01 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 8 1 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 5 0 1 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 1 5 7 1 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 1 5 8 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 1 5 8 17 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 1 5 2097153 1 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 0 0 4294967296 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 1 0 5 0 0 0 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
        "zxv-seats 1 1 1\n00112233445566778899aabbccddeeff 2 1 5 8 1 1 0 0 "
        "00000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 "
        "0000000000000000000000000000000000000000000000000000000000000000 4f\nend\n",
    };
    char text[3000];
    snprintf(PATH, sizeof PATH, "%s/seats-bad", DIR_);
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        spit(PATH, BAD[i]);
        CHECK(zxvd_seats_open(PATH) == -1, "L9: damaged file %u refused:\n%s", (unsigned) i,
              BAD[i]);
    }
    /* a 48-byte name */
    char n48[120];
    for (int i = 0; i < 48; i++) memcpy(n48 + 2 * i, "6e", 3);
    snprintf(text, sizeof text, "zxv-seats 1 1 1\n%.*s%s\nend\n", (int) strlen(GOOD_SEAT) - 2,
             GOOD_SEAT, n48);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "a 48-byte name is refused");
    n48[94] = 0;
    snprintf(text, sizeof text, "zxv-seats 1 1 1\n%.*s%s\nend\n", (int) strlen(GOOD_SEAT) - 2,
             GOOD_SEAT, n48);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == 0, "a 47-byte name opens");
    /* more damage: a long salt, an odd name, "00", a letter in a number, a
     * trailer with no newline, a guest with a lock beside an owner */
    static const char *const ZERO32 = "00000000000000000000000000000000";
    static const char *const ZERO64 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    snprintf(text, sizeof text,
             "zxv-seats 1 1 1\nffeeddccbbaa99887766554433221100 0 0 5 0 0 0 0 0 "
             "%s00 %s %s 4f\nend\n",
             ZERO32, ZERO64, ZERO64);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "a 17-byte salt is refused");
    snprintf(text, sizeof text,
             "zxv-seats 1 1 1\nffeeddccbbaa99887766554433221100 0 0 5 0 0 0 0 0 "
             "%s %s %s 4f0\nend\n",
             ZERO32, ZERO64, ZERO64);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "an odd-length name is refused");
    snprintf(text, sizeof text,
             "zxv-seats 1 1 1\nffeeddccbbaa99887766554433221100 0 0 5 00 0 0 0 "
             "%s %s %s 4f\nend\n",
             ZERO32, ZERO64, ZERO64);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "\"00\" is refused");
    spit(PATH, "zxv-seats 1 00 1\nend\n");
    CHECK(zxvd_seats_open(PATH) == -1, "a count of \"00\" is refused");
    snprintf(text, sizeof text,
             "zxv-seats 1 1 1\nffeeddccbbaa99887766554433221100 0 0 5 0 0 0 0 5: "
             "%s %s %s 4f\nend\n",
             ZERO32, ZERO64, ZERO64);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "a colon in a number is refused");
    spit(PATH, "zxv-seats 1 0 1\nendx");
    CHECK(zxvd_seats_open(PATH) == -1, "a trailer with no newline is refused");
    snprintf(text, sizeof text,
             "zxv-seats 1 2 1\n%s\nffeeddccbbaa99887766554433221100 2 1 5 8 1 1 0 0 "
             "%s %s %s 47\nend\n",
             GOOD_SEAT, ZERO32, ZERO64, ZERO64);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "a guest with a lock beside an owner is refused");
    /* three seats, the last two sharing an id (with different names), then a name */
    snprintf(text, sizeof text,
             "zxv-seats 1 3 1\n%s\n10112233445566778899aabbccddeeff 1 0 0 0 0 0 0 0 %s %s %s 41\n"
             "10112233445566778899aabbccddeeff 1 0 0 0 0 0 0 0 %s %s %s 42\nend\n",
             GOOD_SEAT, ZERO32, ZERO64, ZERO64, ZERO32, ZERO64, ZERO64);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "a repeated id among later seats is refused");
    snprintf(text, sizeof text,
             "zxv-seats 1 3 1\n%s\n10112233445566778899aabbccddeeff 1 0 0 0 0 0 0 0 %s %s %s 41\n"
             "20112233445566778899aabbccddeeff 1 0 0 0 0 0 0 0 %s %s %s 41\nend\n",
             GOOD_SEAT, ZERO32, ZERO64, ZERO64, ZERO32, ZERO64, ZERO64);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "a repeated name among later seats is refused");
    /* good: ids differing only in the last byte, the largest color, a nine,
     * a passphrase seat at Argon2id's smallest memory */
    snprintf(text, sizeof text,
             "zxv-seats 1 3 1\n%s\n00112233445566778899aabbccddee00 1 0 4294967295 0 0 0 0 0 %s %s "
             "%s 41\n00112233445566778899aabbccddee01 1 1 9 8 1 1 0 0 %s %s %s 42\nend\n",
             GOOD_SEAT, ZERO32, ZERO64, ZERO64, ZERO32, ZERO64, ZERO64);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == 0, "good:\n%s", text);
    EXPECT(req("seats.list", "{}"),
           ok("{\"seats\":[%s,%s,%s],\"auto_enter\":true}",
              seat_json("00112233445566778899aabbccddeeff", "O", "owner", "none", 5, 0),
              seat_json("00112233445566778899aabbccddee00", "A", "person", "none", 4294967295u, 0),
              seat_json("00112233445566778899aabbccddee01", "B", "person", "pass", 9, 0)));
    rpc(req("seats.unlock", "{\"id\":\"00112233445566778899aabbccddee00\"}"));
    CHECK(!strcmp(field("id"), "00112233445566778899aabbccddee00"),
          "the seat whose id differs in the last byte: %s", OUT.p);
    /* two seats sharing an id, then a name */
    snprintf(text, sizeof text, "zxv-seats 1 2 1\n%s\n%s\nend\n", GOOD_SEAT, GOOD_SEAT);
    text[strlen(text) - 5] = '0'; /* the second is named "@" */
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "a repeated id is refused");
    snprintf(text, sizeof text, "zxv-seats 1 2 1\n%s\n%s\nend\n", GOOD_SEAT, GOOD_SEAT);
    text[16] = '1'; /* the first id differs, both are named "O" */
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == -1, "a repeated name is refused");
    snprintf(text, sizeof text, "zxv-seats 1 2 0\n%s\n%s\nend\n", GOOD_SEAT, GOOD_SEAT);
    char *second = strchr(strchr(text, '\n') + 1, '\n') + 1;
    second[0] = 'f';
    second[strlen(second) - 6] = '0'; /* "4f" -> "40" ('@') */
    spit(PATH, text);
    fresh(NULL);
    CHECK(zxvd_seats_open(PATH) == 0, "a good file opens:\n%s", text);
    EXPECT(req("seats.list", "{}"),
           ok("{\"seats\":[%s,%s],\"auto_enter\":false}",
              seat_json("00112233445566778899aabbccddeeff", "O", "owner", "none", 5, 0),
              seat_json("f0112233445566778899aabbccddeeff", "@", "owner", "none", 5, 0)));
    EXPECT(req("seats.auto", "{}"), ok("{\"seat\":null}"));
    /* a lone seat with no lock enters directly */
    snprintf(text, sizeof text, "zxv-seats 1 1 1\n%s\nend\n", GOOD_SEAT);
    spit(PATH, text);
    CHECK(zxvd_seats_open(PATH) == 0, "one seat");
    EXPECT(req("seats.auto", "{}"), ok("{\"seat\":\"00112233445566778899aabbccddeeff\"}"));
    /* a folder where the file should be cannot be read */
    char d2[300];
    snprintf(d2, sizeof d2, "%s/adir", DIR_);
    mkdir(d2, 0700);
    CHECK(zxvd_seats_open(d2) == -1, "a folder is not a seats file");
    rmdir(d2);
    /* too large */
    FILE *f = fopen(PATH, "wb");
    if (f) {
        for (uint32_t i = 0; i < (4096u * 400u + 65u) / 64u + 1u; i++)
            fputs("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz\n", f);
        fclose(f);
    }
    CHECK(zxvd_seats_open(PATH) == -1, "too large");
    unlink(PATH);
    /* missing: an empty roster */
    CHECK(zxvd_seats_open(PATH) == 0, "a missing file is an empty roster");
    EXPECT(req("seats.list", "{}"), ok("{\"seats\":[],\"auto_enter\":true}"));
}

/* zxvd keeps the module's own Argon2id parameters */
static void test_default_kdf(void)
{
    snprintf(PATH, sizeof PATH, "%s/seats-kdf", DIR_);
    unlink(PATH);
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_seats_group()) == 0, "add the seats group");
    zxvd_seats_set_kdf(0, 0);
    zxvd_seats_set_random(NULL);
    zxvd_seats_set_clock(NULL);
    CHECK(zxvd_seats_open(PATH) == 0, "open");
    rpc(req("seats.add", "{\"name\":\"O\",\"lock\":\"pass\",\"pass\":\"real one\"}"));
    const char *id = field("id");
    char sid[40];
    snprintf(sid, sizeof sid, "%s", id);
    CHECK(strlen(sid) == 32, "added with OS random bytes");
    const char *f = slurp(PATH);
    char want[80];
    snprintf(want, sizeof want, "%s 0 1 0 65536 3 1 0 0 ", sid);
    CHECK(strstr(f, want) != NULL, "64 MiB, 3 passes: %s", f);
    rpc(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"real one\"}", sid));
    CHECK(strlen(field("session")) == 64, "unlocks with the default parameters");
    rpc(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"wrong\"}", sid));
    CHECK(strstr(OUT.p, "\"ok\":false,\"status\":\"SEAT_ERR_WRONG\",\"wait_ms\":0") != NULL,
          "a wrong try: %s", OUT.p);
    for (int i = 0; i < 4; i++) rpc(req("seats.unlock", "{\"id\":\"%s\",\"pass\":\"wrong\"}", sid));
    unsigned long long until = 0, t0 = (unsigned long long) time(NULL) * 1000ull;
    f = slurp(PATH);
    snprintf(want, sizeof want, "%s 0 1 0 65536 3 1 5 ", sid);
    const char *w = strstr(f, want);
    if (w) until = strtoull(w + strlen(want), NULL, 10);
    CHECK(until + 10000u >= t0 + 30000u && until <= t0 + 30000u,
          "the back-off is on the wall clock in ms: %llu vs %llu", until, t0);
    zxvd_rpc_groups_reset();
    unlink(PATH);
}

int main(void)
{
    const char *t = getenv("ZXV_TMP");
    if (!t) t = getenv("TMPDIR");
    snprintf(DIR_, sizeof DIR_, "%s/zxvd-seats-XXXXXX", t ? t : "/tmp");
    if (!mkdtemp(DIR_)) {
        printf("[FAIL] test_zxvd_seats: no temporary folder\n");
        return 1;
    }
    test_group();
    test_add();
    test_unlock();
    test_sessions();
    test_roles();
    test_last_byte();
    test_many();
    test_file();
    test_damaged();
    test_default_kdf();
    zxvd_rpc_groups_reset();
    snprintf(PATH, sizeof PATH, "%s/seats", DIR_);
    unlink(PATH);
    rmdir(DIR_);
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_seats: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_seats: %d checks\n", checks);
    return 0;
}
