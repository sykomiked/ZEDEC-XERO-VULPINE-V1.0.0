/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_chat.c — zxvd_chat.h C1-C8 through zxvd's JSON-RPC lines. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zxvd_chat.h"
#include "zxvd_rpc.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "  FAIL %s:%d ", __FILE__, __LINE__);                                  \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
        }                                                                                          \
    } while (0)

static zxvd_buf_t OUT;

/* One request line; the reply stays in OUT (NUL-terminated). */
static const char *rpc(const char *method, const char *params)
{
    char line[8192];
    snprintf(line, sizeof line, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\"%s%s%s}", method,
             params ? ",\"params\":" : "", params ? params : "", "");
    free(OUT.p);
    memset(&OUT, 0, sizeof OUT);
    zxvd_rpc_handle_line(line, strlen(line), &OUT);
    zxvd_buf_raw(&OUT, "", 1);
    return OUT.p;
}

static bool has(const char *method, const char *params, const char *want)
{
    return strstr(rpc(method, params), want) != NULL;
}

/* The value of the first "key":"..." string in OUT, copied into out. */
static bool field(const char *key, char *out, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(OUT.p, pat);
    if (!p) return false;
    p += strlen(pat);
    const char *e = strchr(p, '"');
    if (!e || (size_t) (e - p) >= cap) return false;
    memcpy(out, p, (size_t) (e - p));
    out[e - p] = 0;
    return true;
}

static int count(const char *s, const char *sub)
{
    int n = 0;
    for (const char *p = strstr(s, sub); p; p = strstr(p + 1, sub)) n++;
    return n;
}

static uint64_t fixed_now(void)
{
    return 1700000000123ull;
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long) st.st_size : -1;
}

static void test_memory(void)
{
    CHECK(has("chat.me", NULL, "\"method not found\""), "no chat before it opens: %s", OUT.p);
    CHECK(zxvd_chat_open(NULL) == 0, "C3: in memory");
    CHECK(has("chat.me", NULL, "\"saved\":false,\"records\":0,\"following\":[]"), "C3: %s", OUT.p);
    char me[65];
    CHECK(field("node_id", me, sizeof me) && strlen(me) == 64, "C1: a 64-hex NodeID");
    CHECK(has("rpc.discover", NULL,
              "\"chat.me\",\"chat.post\",\"chat.react\",\"chat.follow\",\"chat.feed\","
              "\"chat.thread\"]"),
          "X1: listed after the built-ins: %s", OUT.p);

    /* C6: text rules */
    CHECK(has("chat.post", "{}", "missing string parameter \\\"text\\\""), "%s", OUT.p);
    CHECK(has("chat.post", "{\"text\":\" \\n\\t\\r\"}", "-32602"), "only spaces: %s", OUT.p);
    CHECK(has("chat.post", "{\"text\":\"\"}", "-32602"), "empty: %s", OUT.p);
    CHECK(has("chat.post", "{\"text\":\"\\ud800x\"}", "-32602") ||
              has("chat.post", "{\"text\":\"\\ud800x\"}", "-32700") ||
              has("chat.post", "{\"text\":\"\\ud800x\"}", "-32600"),
          "a lone surrogate is refused: %s", OUT.p);
    char big[1100];
    memset(big, 'a', 1024);
    big[1024] = 0;
    char params[1200];
    snprintf(params, sizeof params, "{\"text\":\"%s\"}", big);
    CHECK(has("chat.post", params, "\"kind\":\"post\""), "1024 bytes is the most: %s", OUT.p);
    snprintf(params, sizeof params, "{\"text\":\"%sb\"}", big);
    CHECK(has("chat.post", params, "-32602"), "1025 bytes is too many");
    CHECK(has("chat.post", "{\"text\":\"\\u00e9t\\u00e9 \\\"q\\\" \\\\ \\u0001\"}",
              "\"text\":\"\xc3\xa9t\xc3\xa9 \\\"q\\\" \\\\ \\u0001\""),
          "C6: escaped back: %s", OUT.p);
    CHECK(has("chat.post", "{\"text\":\"\\ud83d\\ude00 x\"}", "\"text\":\"\xf0\x9f\x98\x80 x\""),
          "a 4-byte character: %s", OUT.p);

    /* C5: post, reply, react */
    CHECK(has("chat.post", "{\"text\":\"hello #zxv\"}", "\"mine\":true,\"kind\":\"post\""), "%s",
          OUT.p);
    char post[65];
    CHECK(field("id", post, sizeof post), "the post's id");
    CHECK(strstr(OUT.p, "\"wall_ms\":\"0\"") && strstr(OUT.p, "\"reply_to\":null") &&
              strstr(OUT.p, "\"replies\":0,\"reactions\":[]}"),
          "no wall clock, no parent: %s", OUT.p);
    char lam1[24], lam2[24];
    CHECK(field("lamport", lam1, sizeof lam1), "lamport");
    snprintf(params, sizeof params, "{\"text\":\"a reply\",\"reply_to\":\"%s\"}", post);
    CHECK(has("chat.post", params, "\"kind\":\"reply\""), "C5: a reply: %s", OUT.p);
    char want[200];
    snprintf(want, sizeof want, "\"reply_to\":\"%s\"", post);
    CHECK(strstr(OUT.p, want) != NULL, "its parent: %s", OUT.p);
    CHECK(field("lamport", lam2, sizeof lam2) && strtoull(lam2, 0, 10) > strtoull(lam1, 0, 10),
          "C8: later than the post");
    char reply[65];
    CHECK(field("id", reply, sizeof reply), "the reply's id");
    char upper[65];
    for (int i = 0; i < 65; i++) upper[i] = (char) (post[i] >= 'a' ? post[i] - 32 : post[i]);
    snprintf(params, sizeof params, "{\"text\":\"upper\",\"reply_to\":\"%s\"}", upper);
    CHECK(has("chat.post", params, "\"kind\":\"reply\""), "upper-case hex is read: %s", OUT.p);
    snprintf(params, sizeof params, "{\"text\":\"x\",\"reply_to\":\"%.63s\"}", post);
    CHECK(has("chat.post", params, "64 hex digits"), "63 digits: %s", OUT.p);
    snprintf(params, sizeof params, "{\"text\":\"x\",\"reply_to\":\"%.63sg\"}", post);
    CHECK(has("chat.post", params, "64 hex digits"), "a non-hex digit: %s", OUT.p);
    snprintf(params, sizeof params, "{\"text\":\"x\",\"reply_to\":\"g%.63s\"}", post);
    CHECK(has("chat.post", params, "64 hex digits"), "a non-hex first digit: %s", OUT.p);
    CHECK(has("chat.post",
              "{\"text\":\"x\",\"reply_to\":"
              "\"0000000000000000000000000000000000000000000000000000000000000000\"}",
              "no record with that"),
          "an unknown parent: %s", OUT.p);
    CHECK(has("chat.post", "{\"text\":\"x\",\"reply_to\":\"\"}", "\"kind\":\"post\""),
          "an empty reply_to is a post: %s", OUT.p);
    CHECK(has("chat.post", "{\"text\":\"x\",\"reply_to\":7}", "must be a string"), "%s", OUT.p);

    snprintf(params, sizeof params, "{\"to\":\"%s\",\"emoji\":\"\\u2764\"}", post);
    CHECK(has("chat.react", params, "\"kind\":\"reaction\""), "C5: a reaction: %s", OUT.p);
    snprintf(params, sizeof params, "{\"to\":\"%s\",\"emoji\":\"\\u2764\"}", post);
    CHECK(has("chat.react", params, "\"kind\":\"reaction\""), "another, same emoji");
    snprintf(params, sizeof params, "{\"to\":\"%s\",\"emoji\":\"ok\"}", post);
    CHECK(has("chat.react", params, "\"kind\":\"reaction\""), "a word");
    snprintf(params, sizeof params, "{\"to\":\"%s\",\"emoji\":\"\"}", post);
    CHECK(has("chat.react", params, "1 to 32 bytes"), "empty emoji: %s", OUT.p);
    snprintf(params, sizeof params, "{\"to\":\"%s\",\"emoji\":\"%033d\"}", post, 0);
    CHECK(has("chat.react", params, "-32602"), "33 bytes: %s", OUT.p);
    snprintf(params, sizeof params, "{\"to\":\"%s\",\"emoji\":\"%032d\"}", post, 0);
    CHECK(has("chat.react", params, "\"kind\":\"reaction\""), "32 bytes: %s", OUT.p);
    CHECK(has("chat.react",
              "{\"to\":\"0000000000000000000000000000000000000000000000000000000000000000\","
              "\"emoji\":\"x\"}",
              "no record with that"),
          "%s", OUT.p);
    CHECK(has("chat.react", "{\"emoji\":\"x\"}", "missing string parameter \\\"to\\\""), "%s",
          OUT.p);
    snprintf(params, sizeof params, "{\"to\":\"%s\"}", post);
    CHECK(has("chat.react", params, "missing string parameter \\\"emoji\\\""), "%s", OUT.p);

    /* C5: thread and reaction counts */
    snprintf(params, sizeof params, "{\"id\":\"%s\"}", post);
    rpc("chat.thread", params);
    CHECK(strstr(OUT.p, "\"replies\":2,\"reactions\":[{\"emoji\":\"\xe2\x9d\xa4\",\"count\":2,"
                        "\"mine\":true},{\"emoji\":\"ok\",\"count\":1,\"mine\":true},") != NULL,
          "C5: counts by emoji: %s", OUT.p);
    CHECK(count(OUT.p, "\"kind\":\"reply\"") == 2 && count(OUT.p, "\"kind\":\"reaction\"") == 0,
          "only replies listed: %s", OUT.p);
    CHECK(strstr(OUT.p, "\"text\":\"a reply\"") < strstr(OUT.p, "\"text\":\"upper\""),
          "in Lamport order: %s", OUT.p);
    snprintf(params, sizeof params, "{\"id\":\"%s\"}", reply);
    CHECK(has("chat.thread", params, "\"replies\":[]}"), "a reply has no replies: %s", OUT.p);
    CHECK(has("chat.thread",
              "{\"id\":\"0000000000000000000000000000000000000000000000000000000000000000\"}",
              "no record with that"),
          "%s", OUT.p);
    CHECK(has("chat.thread", "{}", "missing string parameter \\\"id\\\""), "%s", OUT.p);

    /* C5: the feed holds posts only */
    rpc("chat.feed", NULL);
    CHECK(count(OUT.p, "\"kind\":\"post\"") == 5 && !strstr(OUT.p, "\"kind\":\"reply\""),
          "5 posts, no replies: %s", OUT.p);
    CHECK(has("chat.me", NULL, "\"records\":11"), "11 records held: %s", OUT.p);

    /* C7: follow */
    const char *other = "1111111111111111111111111111111111111111111111111111111111111111";
    snprintf(params, sizeof params, "{\"node_id\":\"%s\"}", other);
    CHECK(has("chat.follow", params, "\"following\":true}"), "%s", OUT.p);
    CHECK(has("chat.me", NULL, "\"records\":12,\"following\":[\"1111"), "C7: one record: %s",
          OUT.p);
    CHECK(has("chat.follow", params, "\"following\":true}"), "again");
    CHECK(has("chat.me", NULL, "\"records\":12,"), "C7: a repeat writes nothing: %s", OUT.p);
    snprintf(params, sizeof params, "{\"node_id\":\"%s\",\"follow\":false}", other);
    CHECK(has("chat.follow", params, "\"following\":false}"), "%s", OUT.p);
    CHECK(has("chat.me", NULL, "\"records\":13,\"following\":[]}"), "unfollowed: %s", OUT.p);
    CHECK(has("chat.follow", params, "\"following\":false}") &&
              has("chat.me", NULL, "\"records\":13,"),
          "a repeat unfollow writes nothing");
    snprintf(params, sizeof params, "{\"node_id\":\"%s\",\"follow\":1}", other);
    CHECK(has("chat.follow", params, "must be true or false"), "%s", OUT.p);
    snprintf(params, sizeof params, "{\"node_id\":\"%s\"}", me);
    CHECK(has("chat.follow", params, "cannot follow yourself"), "%s", OUT.p);
    CHECK(has("chat.follow", "{}", "missing string parameter \\\"node_id\\\""), "%s", OUT.p);
    zxvd_chat_close();
    CHECK(has("chat.me", NULL, "\"method not found\""), "gone after close");
}

static void test_saved(void)
{
    char tmpl[] = "/tmp/zxvdchat.XXXXXX";
    CHECK(mkdtemp(tmpl) != NULL, "temp dir");
    char prefix[256], seed[300], logp[300];
    snprintf(prefix, sizeof prefix, "%s/social", tmpl);
    snprintf(seed, sizeof seed, "%s.seed", prefix);
    snprintf(logp, sizeof logp, "%s.log", prefix);
    zxvd_chat_set_clock(fixed_now);
    CHECK(zxvd_chat_open(prefix) == 0, "C1: a new key and log");
    struct stat st;
    CHECK(stat(seed, &st) == 0 && st.st_size == 32 && (st.st_mode & 077) == 0,
          "C1: 32 bytes, 0600");
    CHECK(file_size(logp) == 8, "C2: the header only");
    CHECK(has("chat.me", NULL, "\"saved\":true,\"records\":0"), "%s", OUT.p);
    char me[65];
    field("node_id", me, sizeof me);
    CHECK(has("chat.post", "{\"text\":\"kept\"}", "\"wall_ms\":\"1700000000123\""),
          "a wall clock hint: %s", OUT.p);
    char post[65];
    field("id", post, sizeof post);
    long one = file_size(logp);
    CHECK(one > 8 + 4 + 3000, "C2: appended: %ld", one);
    const char *other = "2222222222222222222222222222222222222222222222222222222222222222";
    char params[300];
    snprintf(params, sizeof params, "{\"node_id\":\"%s\"}", other);
    CHECK(has("chat.follow", params, "true"), "follow");
    snprintf(params, sizeof params, "{\"node_id\":\"%s\",\"follow\":false}", other);
    CHECK(has("chat.follow", params, "false"), "and unfollow");
    snprintf(params, sizeof params, "{\"node_id\":\"%s\"}",
             "3333333333333333333333333333333333333333333333333333333333333333");
    CHECK(has("chat.follow", params, "true"), "follow another");
    zxvd_chat_close();

    /* C2: everything comes back, with the same key */
    CHECK(zxvd_chat_open(prefix) == 0, "reopens");
    char me2[65];
    CHECK(has("chat.me", NULL, "\"records\":4,\"following\":[\"3333") &&
              field("node_id", me2, sizeof me2) && !strcmp(me, me2),
          "C2, C7: the same NodeID and following: %s", OUT.p);
    snprintf(params, sizeof params, "{\"id\":\"%s\"}", post);
    CHECK(has("chat.thread", params, "\"text\":\"kept\""), "the post is back");
    /* C8: the clock moved past what was loaded */
    CHECK(has("chat.post", "{\"text\":\"after\"}", "\"lamport\":\"5\""), "C8: %s", OUT.p);
    zxvd_chat_close();

    /* C2: a torn last frame is cut off */
    long whole = file_size(logp);
    FILE *f = fopen(logp, "ab");
    fwrite("\x00\x00\x10", 1, 3, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == 0 && file_size(logp) == whole, "a torn length is cut");
    zxvd_chat_close();
    f = fopen(logp, "ab");
    fwrite("\x00\x00\x00\x10"
           "abc",
           1, 7, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == 0 && file_size(logp) == whole, "a torn record is cut");
    CHECK(has("chat.me", NULL, "\"records\":5"), "nothing lost: %s", OUT.p);
    zxvd_chat_close();

    /* C2: damage stops the start */
    char *keep = malloc((size_t) whole);
    f = fopen(logp, "rb");
    CHECK(fread(keep, 1, (size_t) whole, f) == (size_t) whole, "read");
    fclose(f);
    f = fopen(logp, "ab");
    fwrite("\x00\x00\x00\x00", 1, 4, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == -1, "a zero length stops it");
    CHECK(has("chat.me", NULL, "method not found"), "and registers nothing");
    f = fopen(logp, "wb");
    fwrite(keep, 1, (size_t) whole, f);
    fwrite("\xff\xff\xff\xff", 1, 4, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == -1, "a huge length stops it");
    f = fopen(logp, "wb");
    keep[whole - 100] ^= 1; /* inside the last record's signature */
    fwrite(keep, 1, (size_t) whole, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == -1, "a record that does not verify stops it");
    keep[whole - 100] ^= 1;
    keep[0] = 'Y';
    f = fopen(logp, "wb");
    fwrite(keep, 1, (size_t) whole, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == -1, "a bad header stops it");
    f = fopen(logp, "wb");
    fwrite(keep + 1, 1, 6, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == -1, "a short header stops it");
    keep[0] = 'Z';
    f = fopen(logp, "wb");
    fwrite(keep, 1, (size_t) whole, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == 0 && has("chat.me", NULL, "\"records\":5"), "repaired");
    zxvd_chat_close();
    free(keep);

    /* C1: a seed file of the wrong size stops it */
    f = fopen(seed, "ab");
    fwrite("x", 1, 1, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == -1, "33 bytes");
    f = fopen(seed, "wb");
    fwrite("short", 1, 5, f);
    fclose(f);
    CHECK(zxvd_chat_open(prefix) == -1, "5 bytes");
    unlink(seed);

    /* C2: a record that cannot be saved is not kept; the log goes read-only */
    unlink(logp);
    CHECK(zxvd_chat_open(prefix) == 0, "a fresh key and log");
    CHECK(chmod(logp, 0400) == 0, "make the log read-only");
    if (geteuid() != 0) {
        CHECK(has("chat.post", "{\"text\":\"lost\"}", "could not be written: this record is not"),
              "C2: %s", OUT.p);
        CHECK(has("chat.me", NULL, "\"records\":0"), "and not held: %s", OUT.p);
        chmod(logp, 0600);
        CHECK(has("chat.post", "{\"text\":\"still\"}", "read-only until zxvd restarts"), "%s",
              OUT.p);
    }
    chmod(logp, 0600);
    zxvd_chat_close();
    CHECK(file_size(logp) == 8, "nothing was written");

    /* paths */
    char longp[1100];
    memset(longp, 'a', 1020);
    longp[0] = '/';
    longp[1020] = 0;
    CHECK(zxvd_chat_open(longp) == -1, "a path too long for the seed name");
    CHECK(zxvd_chat_open("/nonexistent-dir/x/social") == -1, "a folder that does not exist");
    unlink(seed);
    unlink(logp);
    rmdir(tmpl);
    zxvd_chat_set_clock(NULL);
}

/* Records from a peer (here, a second identity's log) are shown with bad
 * UTF-8 replaced: built by writing a record through another open. */
static void test_ranking(void)
{
    CHECK(zxvd_chat_open("") == 0, "\"\" is memory too");
    CHECK(has("chat.me", NULL, "\"saved\":false"), "%s", OUT.p);
    const char *texts[] = {"cats cats cats cats", "cats cats cats cats", "rust compilers today",
                           "garden tomatoes ripe"};
    for (unsigned i = 0; i < 4; i++) {
        char p[200];
        snprintf(p, sizeof p, "{\"text\":\"%s\"}", texts[i]);
        CHECK(has("chat.post", p, "\"kind\":\"post\""), "post %u", i);
    }
    rpc("chat.feed", NULL);
    const char *a = strstr(OUT.p, "\"text\":\"cats");
    const char *b = a ? strstr(a + 1, "\"text\":\"cats") : NULL;
    const char *r = strstr(OUT.p, "\"text\":\"rust");
    const char *g = strstr(OUT.p, "\"text\":\"garden");
    CHECK(a && b && r && g && b > r && b > g, "F2-F3: the duplicate sinks to the end: %s", OUT.p);
    zxvd_chat_close();
}

int main(void)
{
    test_memory();
    test_saved();
    test_ranking();
    free(OUT.p);
    if (fails) {
        printf("[FAIL] test_zxvd_chat: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zxvd_chat: %d checks\n", checks);
    return 0;
}
