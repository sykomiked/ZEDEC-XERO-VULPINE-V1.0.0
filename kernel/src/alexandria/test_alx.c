/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_alx.c — the Alexandria index and booklets read back from what the
 * build script wrote (argv[1]: the 05_alexandria directory; argv[2]:
 * kernel/src), every way an index can be wrong, references, TECTON plans
 * and AXION checks (whose gates must match lpres exactly). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx.h"
#include "alx_slots.h"
#include "../lpres/lpres.h"
#include "../robin_debanks/sha256.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint8_t T[1u << 20], M[sizeof T], B510[ALX_BOOK_MAX], B930[ALX_BOOK_MAX], F[1u << 16];
static uint32_t TN, B510N, B930N;
static const char *SRCDIR;
static alx_t X; /* the index as built */

static uint32_t slurp(const char *dir, const char *name, uint8_t *buf, uint32_t cap)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("[FAIL] cannot read %s\n", path);
        exit(2);
    }
    uint32_t n = (uint32_t) fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static void rehash(uint8_t *b, uint32_t n)
{
    sha256(b, n - 32u, b + n - 32u);
}

/* the records, at the offsets alx.h gives, for the built index's counts */
static uint8_t *book(uint8_t *b, uint32_t i)
{
    return b + ALX_HEAD + i * ALX_BOOK;
}
static uint8_t *slot(uint8_t *b, uint32_t s)
{
    return b + ALX_HEAD + X.books * ALX_BOOK + (s - 1u) * ALX_SLOTREC;
}
static uint8_t *target(uint8_t *b, uint32_t i)
{
    return b + ALX_HEAD + X.books * ALX_BOOK + X.slots * ALX_SLOTREC + i * ALX_TARGET;
}

static int32_t find(const alx_t *t, const char *s)
{
    return alx_find(t, s, (uint32_t) strlen(s));
}

static int resolve(const alx_t *t, const char *s, alx_ref_t *r)
{
    return alx_resolve(t, s, (uint32_t) strlen(s), r);
}

static void b32(const uint8_t *in, uint32_t n, char *out)
{
    static const char A[] = "abcdefghijklmnopqrstuvwxyz234567";
    uint32_t acc = 0, bits = 0, k = 0;
    out[k++] = 'b';
    for (uint32_t i = 0; i < n; i++) {
        acc = (acc << 8 | in[i]) & 0xFFFFu;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[k++] = A[(acc >> bits) & 31u];
        }
    }
    if (bits) out[k++] = A[(acc << (5u - bits)) & 31u];
    out[k] = 0;
}

static void test_read(void)
{
    alx_t t;
    CHECK(alx_open(&t, T, TN) == ALX_OK, "the built index opens");
    CHECK(t.slots == ALX_SLOT__COUNT && t.books == 2 && t.targets == 7,
          "%u slots, %u booklets, %u targets", t.slots, t.books, t.targets);
    alx_entry_t e;
    CHECK(alx_get(&t, ALX_SLOT_NUM_GCD, &e) == ALX_OK, "slot 1");
    CHECK(strcmp(e.shorthand, "num.gcd") == 0 && strcmp(e.target, "number_classics.gcd") == 0,
          "names %s %s", e.shorthand, e.target);
    CHECK(e.kind == ALX_NUM && e.inputs == 2 && e.languages == 4 && e.flags == ALX_F_WITNESSED,
          "gcd: num of two inputs in four languages");
    CHECK(e.major == 1 && e.minor == 0 && e.patch == 0, "version 1.0.0");
    CHECK(e.place == 512 && e.item == 1 && e.booklet == 0 && e.first_target == 0, "place 512.01");
    CHECK(alx_get(&t, ALX_SLOT_SWARM_WITHIN_LIMITS, &e) == ALX_OK && e.kind == ALX_TRUTH &&
              e.inputs == 2 && e.languages == 1 && e.flags == 0 && e.place == 932 &&
              e.booklet == 1 && e.first_target == 6,
          "within_limits: truth, one language, in booklet 930");
    CHECK(alx_get(&t, 0, &e) == ALX_E_SLOT && alx_get(&t, ALX_SLOT__COUNT + 1u, &e) == ALX_E_SLOT &&
              alx_get(&t, 1, 0) == ALX_E_SLOT && alx_get(0, 1, &e) == ALX_E_SLOT,
          "no slot 0, past the end, or without somewhere to put it");

    /* booklets */
    alx_booklet_t b;
    CHECK(alx_booklet(&t, 0, &b) == ALX_OK && b.division == 510 && b.length == B510N,
          "booklet 510");
    CHECK(alx_booklet(&t, 1, &b) == ALX_OK && b.division == 930 && b.length == B930N,
          "booklet 930");
    CHECK(alx_booklet(&t, 2, &b) == ALX_E_SLOT && alx_booklet(&t, 0, 0) == ALX_E_SLOT, "only two");
    CHECK(alx_booklet_check(&t, 0, B510, B510N) == ALX_OK &&
              alx_booklet_check(&t, 1, B930, B930N) == ALX_OK,
          "both booklets match their CIDs");
    CHECK(alx_booklet_check(&t, 0, B930, B930N) == ALX_E_FORM &&
              alx_booklet_check(&t, 1, B510, B510N) == ALX_E_FORM,
          "each is not the other (their lengths differ)");
    CHECK(alx_booklet_check(&t, 0, B510, B510N - 64u) == ALX_E_FORM &&
              alx_booklet_check(&t, 0, 0, B510N) == ALX_E_FORM &&
              alx_booklet_check(&t, 5, B510, B510N) == ALX_E_SLOT,
          "short, missing, no such booklet");
    for (uint32_t i = 0; i < B510N; i += 97) {
        B510[i] ^= 1;
        CHECK(alx_booklet_check(&t, 0, B510, B510N) == ALX_E_HASH, "booklet byte %u", i);
        B510[i] ^= 1;
    }

    /* every language's logic is in its booklet at its offset, byte for byte */
    static const struct {
        uint32_t slot, lang;
        const char *file;
    } L[] = {{1, 1, "apps/blocks/number_classics/gcd.f"},
             {1, 2, "apps/blocks/number_classics/gcd.bas"},
             {1, 3, "apps/blocks/number_classics/gcd.fs"},
             {1, 120, "apps/blocks/number_classics/gcd.zlir"},
             {2, 104, "apps/blocks/swarm_shares/cycle_share.kron"},
             {3, 104, "apps/blocks/swarm_shares/busiest.kron"},
             {4, 103, "apps/blocks/swarm_shares/limits.eve"}};
    for (size_t i = 0; i < sizeof L / sizeof L[0]; i++) {
        uint32_t off = 0, len = 0, fn = slurp(SRCDIR, L[i].file, F, sizeof F);
        CHECK(alx_target(&t, L[i].slot, L[i].lang, &off, &len) == ALX_OK, "%s", L[i].file);
        const uint8_t *bk = L[i].slot == 1 ? B510 : B930;
        CHECK(len == fn && off % 64 == 0 && memcmp(bk + off, F, fn) == 0, "%s at %u", L[i].file,
              off);
    }
    CHECK(alx_get(&t, 1, &e) == ALX_OK &&
              e.manifest_len == slurp(SRCDIR, "apps/blocks/number_classics.zxblock", F, sizeof F) &&
              memcmp(B510 + e.manifest_off, F, e.manifest_len) == 0,
          "the manifest too");
    uint8_t h[32];
    sha256(F, e.manifest_len, h);
    CHECK(memcmp(h, e.content, 32) == 0, "its content id is the manifest's SHA-256");
    CHECK(alx_target(&t, 1, 104, 0, 0) == ALX_E_LANG &&
              alx_target(&t, 4, 104, 0, 0) == ALX_E_LANG &&
              alx_target(&t, 1, 0, 0, 0) == ALX_E_LANG && alx_target(&t, 9, 1, 0, 0) == ALX_E_SLOT,
          "no gcd in kron, no limits in kron, no language 0, no slot 9");
    CHECK(alx_target(&t, 1, 1, 0, 0) == ALX_OK, "offsets are optional");

    char out[40];
    for (uint32_t s = 1; s <= t.slots; s++) {
        int32_t n = alx_shorthand(&t, s, out, sizeof out);
        CHECK(n > 1 && alx_find(&t, out, (uint32_t) n) == (int32_t) s, "slot %u round trips", s);
    }
    CHECK(alx_shorthand(&t, 1, out, 8) == 8 && memcmp(out, "@num.gcd", 8) == 0, "exact fit");
    CHECK(alx_shorthand(&t, 1, out, 7) == ALX_E_LIMIT &&
              alx_shorthand(&t, 1, 0, 40) == ALX_E_LIMIT &&
              alx_shorthand(&t, 9, out, 40) == ALX_E_SLOT,
          "one short, no buffer, no slot");

    /* composing */
    CHECK(alx_dock(&t, ALX_SLOT_NUM_GCD, ALX_SLOT_SWARM_CYCLE_SHARE) == ALX_OK,
          "a number feeds a one-input block");
    CHECK(alx_dock(&t, ALX_SLOT_SWARM_BUSIEST, ALX_SLOT_NUM_GCD) == ALX_E_DOCK,
          "one answer cannot fill two inputs");
    CHECK(alx_dock(&t, 1, 5) == ALX_E_SLOT && alx_dock(&t, 0, 1) == ALX_E_SLOT, "docking nothing");
    uint32_t p[] = {1, 2, 3, 4}, at = 99;
    CHECK(alx_pipeline(&t, p, 3, &at) == ALX_OK && at == 0, "gcd, cycle_share, busiest");
    CHECK(alx_pipeline(&t, p, 4, &at) == ALX_E_DOCK && at == 3, "within_limits takes two (%u)", at);
    CHECK(alx_pipeline(&t, p, 1, 0) == ALX_OK && alx_pipeline(&t, p, 0, &at) == ALX_E_SLOT &&
              alx_pipeline(&t, 0, 2, &at) == ALX_E_SLOT,
          "one block, none, no list");
    uint32_t q[] = {7};
    CHECK(alx_pipeline(&t, q, 1, &at) == ALX_E_SLOT && at == 0, "an unknown first block");
}

static void test_refs(void)
{
    alx_t t = X;
    alx_ref_t r;
    static const struct {
        const char *in;
        int st;
        uint32_t slot, lang;
    } c[] = {
        {"@num.gcd", 0, 1, 0},
        {"num.gcd", 0, 1, 0},
        {"@S:0001", 0, 1, 0},
        {"@S:0004", 0, 4, 0},
        {"@512.01", 0, 1, 0},
        {"512.01", 0, 1, 0},
        {"@932.01", 0, 4, 0},
        {"@num.gcd-001", 0, 1, 1},
        {"@num.gcd:120", 0, 1, 120},
        {"512.01-003", 0, 1, 3},
        {"@S:0002:104", 0, 2, 104},
        {"@931.01-104", 0, 2, 104},
        {"@num.gcd-104", ALX_E_LANG, 0, 0},
        {"@num.gcd-100", ALX_E_LANG, 0, 0},
        {"@num.gcd-000", ALX_E_LANG, 0, 0},
        {"@num.gcd-121", ALX_E_LANG, 0, 0},
        {"@num.gcd-01", ALX_E_NAME, 0, 0},
        {"@num.gc", ALX_E_NAME, 0, 0},
        {"@num.gcdx", ALX_E_NAME, 0, 0},
        {"@", ALX_E_NAME, 0, 0},
        {"@NUM.GCD", ALX_E_NAME, 0, 0},
        {"@S:001", ALX_E_NAME, 0, 0},
        {"@S:00001", ALX_E_NAME, 0, 0},
        {"@S:000g", ALX_E_NAME, 0, 0},
        {"@S:000a", ALX_E_NAME, 0, 0},
        {"@s:0001", ALX_E_NAME, 0, 0},
        {"@@num.gcd", ALX_E_NAME, 0, 0},
        {"@S:0000", ALX_E_SLOT, 0, 0},
        {"@S:0005", ALX_E_SLOT, 0, 0},
        {"@512.1", ALX_E_NAME, 0, 0},
        {"@512.00", ALX_E_NAME, 0, 0},
        {"@012.01", ALX_E_NAME, 0, 0},
        {"@512.02", ALX_E_NAME, 0, 0},
        {"@51a.01", ALX_E_NAME, 0, 0},
        {"@512x01", ALX_E_NAME, 0, 0},
        {"@512.010", ALX_E_NAME, 0, 0},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        int s = resolve(&t, c[i].in, &r);
        CHECK(s == c[i].st && (s || (r.slot == c[i].slot && r.language == c[i].lang)),
              "%s: %s (%u %u)", c[i].in, alx_status_name(s), r.slot, r.language);
    }
    CHECK(find(&t, "@num.gcd-001") == ALX_E_NAME && find(&t, "512.01") == 1 &&
              find(&t, "") == ALX_E_NAME && alx_find(&t, 0, 3) == ALX_E_NAME &&
              alx_find(0, "num.gcd", 7) == ALX_E_NAME,
          "find takes no language");
    CHECK(alx_find(&t, "num.gcd", 5) == ALX_E_NAME, "a prefix is not a name");
    char long_name[40];
    memset(long_name, 'a', sizeof long_name);
    CHECK(alx_find(&t, long_name, 32) == ALX_E_NAME, "too long");
    CHECK(alx_resolve(&t, "num.gcd", 7, 0) == ALX_E_NAME && alx_resolve(&t, 0, 3, &r) == ALX_E_NAME,
          "nowhere to put it, no text");

    /* addenda: named by the CID they extend */
    alx_booklet_t b;
    alx_booklet(&t, 0, &b);
    char cid[64], other[64], s[200];
    b32(b.cid, 36, cid);
    uint8_t parsed[36];
    CHECK(strlen(cid) == 59 && alx_cid_parse(cid, 59, parsed) == ALX_OK &&
              memcmp(parsed, b.cid, 36) == 0,
          "a CID parses back (%s)", cid);
    uint8_t dagpb[36];
    memcpy(dagpb, b.cid, 36);
    dagpb[1] = 0x70;
    b32(dagpb, 36, other);
    CHECK(alx_cid_parse(other, 59, parsed) == ALX_OK && parsed[1] == 0x70, "dag-pb too");
    static uint8_t roots[2][36];
    memcpy(roots[1], b.cid, 36);
    alx_set_roots(&t, (const uint8_t(*)[36]) roots, 2);
    snprintf(s, sizeof s, "%s/512.01.42.01:001", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_OVERLAY && r.language == 1 &&
              strcmp(r.path, "512.01.42.01") == 0 && memcmp(r.parent, b.cid, 36) == 0,
          "in the lineage, not mounted");
    snprintf(s, sizeof s, "%s/512.01.42.01.b:001", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_OVERLAY && strcmp(r.path, "512.01.42.01.b") == 0,
          "a fork mark");
    snprintf(s, sizeof s, "%s/512.01.42:001", other);
    CHECK(resolve(&t, s, &r) == ALX_E_LINEAGE, "not in the lineage");
    alx_set_roots(&t, 0, 0);
    snprintf(s, sizeof s, "%s/512.01.42:001", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_LINEAGE, "no roots, no lineage");
    alx_set_roots(&t, (const uint8_t(*)[36]) roots, 2);
    static const char *const bad[] = {
        "/512.01.42:001",  "/512.01:001",   "/512.01.42-001",        "/512.01.42:01",
        "/512.01.42",      "/512.01.:001",  "/512.01..4:001",        "/512.01.42.:001",
        "/512.01.ABC:001", "/51.01.42:001", "/512.01.123456789:001", "/512.01-42:001"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        snprintf(s, sizeof s, "%s%s", cid, bad[i]);
        CHECK(resolve(&t, i ? s : bad[0], &r) == ALX_E_SYNTAX, "%s", bad[i]);
    }
    snprintf(s, sizeof s, "%s/512.01.42:104", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_OVERLAY, "any valid language");
    snprintf(s, sizeof s, "%s/512.01.42:000", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_LANG, "language 000");
    snprintf(s, sizeof s, "%s/512.01.42:099", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_OVERLAY, "language 099");
    snprintf(s, sizeof s, "%s/512.01.42:100", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_LANG, "language 100");
    snprintf(s, sizeof s, "%s/512.01.12345678.12345678.12345678.12345678.1234:001", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_OVERLAY && strlen(r.path) == 47, "the longest path");
    snprintf(s, sizeof s, "%s/512.01.12345678.12345678.12345678.12345678.12345:001", cid);
    CHECK(resolve(&t, s, &r) == ALX_E_SYNTAX, "one longer");
    CHECK(find(&t, s) == ALX_E_NAME, "find takes no addendum");

    /* CIDs that are not */
    char bc[64];
    strcpy(bc, cid);
    bc[0] = 'B';
    CHECK(alx_cid_parse(bc, 59, parsed) == ALX_E_SYNTAX, "upper-case multibase");
    strcpy(bc, cid);
    bc[10] = '1';
    CHECK(alx_cid_parse(bc, 59, parsed) == ALX_E_SYNTAX, "a 1 is not base32");
    strcpy(bc, cid);
    bc[10] = 'A';
    CHECK(alx_cid_parse(bc, 59, parsed) == ALX_E_SYNTAX, "upper case");
    CHECK(alx_cid_parse(cid, 58, parsed) == ALX_E_SYNTAX &&
              alx_cid_parse(0, 59, parsed) == ALX_E_SYNTAX &&
              alx_cid_parse(cid, 59, 0) == ALX_E_SYNTAX,
          "short, none, nowhere to put it");
    uint8_t v0[36];
    for (int k = 0; k < 4; k++) {
        memcpy(v0, b.cid, 36);
        v0[k] ^= 0x40;
        b32(v0, 36, bc);
        CHECK(alx_cid_parse(bc, 59, parsed) == ALX_E_SYNTAX, "CID prefix byte %d", k);
    }
    /* the last character carries 3 data bits and 2 padding bits */
    strcpy(bc, cid);
    char *last = &bc[58];
    const char *A = "abcdefghijklmnopqrstuvwxyz234567";
    int v = (int) (strchr(A, *last) - A);
    *last = A[v ^ 1];
    CHECK(alx_cid_parse(bc, 59, parsed) == ALX_E_SYNTAX, "a padding bit set");
}

static void test_tecton(void)
{
    alx_t t = X;
    alx_plan_t p;
    uint32_t at;
    const char *ok = "/* a plan */\nBIND telemetry.v1 {\n  SOURCE : @num.gcd-001\n"
                     "  PIPE: @931.01 /* shares */\n  SINK :@swarm.busiest:104\n}\n";
    CHECK(alx_tecton(&t, ok, (uint32_t) strlen(ok), &p, &at) == ALX_OK, "a plan (%u)", at);
    CHECK(strcmp(p.name, "telemetry.v1") == 0 && p.n == 3 && p.slot[0] == 1 && p.slot[1] == 2 &&
              p.slot[2] == 3 && p.language[0] == 1 && p.language[1] == 0 && p.language[2] == 104,
          "its stages");
    const char *two = "BIND a{SOURCE:@num.gcd}";
    CHECK(alx_tecton(&t, two, (uint32_t) strlen(two), &p, 0) == ALX_OK && p.n == 1,
          "one block, tight");
    static const struct {
        const char *in;
        int st;
        uint32_t at;
    } c[] = {
        {"", ALX_E_SYNTAX, 0},
        {"BIND a {}", ALX_E_SYNTAX, 8},
        {"bind a { SOURCE : @num.gcd }", ALX_E_SYNTAX, 0},
        {"BINDa { SOURCE : @num.gcd }", ALX_E_SYNTAX, 0},
        {"BIND A { SOURCE : @num.gcd }", ALX_E_SYNTAX, 5},
        {"BIND 1a { SOURCE : @num.gcd }", ALX_E_SYNTAX, 5},
        {"BIND a. { SOURCE : @num.gcd }", ALX_E_SYNTAX, 7},
        {"BIND a SOURCE : @num.gcd }", ALX_E_SYNTAX, 7},
        {"BIND a { PIPE : @num.gcd }", ALX_E_SYNTAX, 9},
        {"BIND a { SOURCE @num.gcd }", ALX_E_SYNTAX, 16},
        {"BIND a { SOURCE : }", ALX_E_SYNTAX, 18},
        {"BIND a { SOURCE : @num.gcd SOURCE : @num.gcd }", ALX_E_SYNTAX, 27},
        {"BIND a { SOURCE : @num.gcd SINK : @931.01 PIPE : @931.02 }", ALX_E_SYNTAX, 42},
        {"BIND a { SOURCE : @num.gcd SINK : @931.01 SINK : @931.02 }", ALX_E_SYNTAX, 42},
        {"BIND a { SOURCE : @num.gcd PIPELINE : @931.01 }", ALX_E_SYNTAX, 27},
        {"BIND a { SOURCE : @num.gcd } x", ALX_E_SYNTAX, 29},
        {"BIND a { SOURCE : @num.gcd", ALX_E_SYNTAX, 26},
        {"BIND a { SOURCE : @num.gcd } /* open", ALX_E_SYNTAX, 29},
        {"/* open BIND a { SOURCE : @num.gcd }", ALX_E_SYNTAX, 0},
        {"BIND a { SOURCE : @num.gc }", ALX_E_NAME, 18},
        {"BIND a { SOURCE : @num.gcd-104 }", ALX_E_LANG, 18},
        {"BIND a { SOURCE : @swarm.busiest PIPE : @num.gcd }", ALX_E_DOCK, 40},
        {"BIND a { SOURCE : @num.gcd PIPE : @931.01 SINK : @932.01 }", ALX_E_DOCK, 49},
        {"BIND aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa { SOURCE : @num.gcd }", ALX_E_LIMIT, 36},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        at = 999;
        int s = alx_tecton(&t, c[i].in, (uint32_t) strlen(c[i].in), &p, &at);
        CHECK(s == c[i].st && at == c[i].at && p.n == 0, "%s: %s at %u", c[i].in,
              alx_status_name(s), at);
    }
    char big[1024];
    int k = snprintf(big, sizeof big, "BIND a { SOURCE : @num.gcd");
    for (int i = 0; i < 15; i++) k += snprintf(big + k, sizeof big - (size_t) k, " PIPE : @931.01");
    snprintf(big + k, sizeof big - (size_t) k, " }");
    CHECK(alx_tecton(&t, big, (uint32_t) strlen(big), &p, &at) == ALX_OK && p.n == 16, "16 stages");
    snprintf(big + k, sizeof big - (size_t) k, " PIPE : @931.01 }");
    CHECK(alx_tecton(&t, big, (uint32_t) strlen(big), &p, &at) == ALX_E_LIMIT &&
              at == (uint32_t) k + 8,
          "17 is too many (%u)", at);
    CHECK(alx_tecton(&t, ok, (uint32_t) strlen(ok), 0, &at) == ALX_E_SYNTAX &&
              alx_tecton(&t, 0, 3, &p, &at) == ALX_E_SYNTAX,
          "no plan, no text");
    alx_t none;
    memset(&none, 0, sizeof none);
    CHECK(alx_tecton(&none, ok, (uint32_t) strlen(ok), &p, &at) == ALX_E_SLOT, "no index");
    CHECK(alx_tecton(&t, ok, (uint32_t) strlen(ok), &p, 0) == ALX_OK, "no position wanted");
}

static int ax(const char *src, const int64_t *r, uint32_t n, uint64_t present)
{
    alx_axion_t p;
    uint32_t at;
    int s = alx_axion_parse(src, (uint32_t) strlen(src), &p, &at);
    return s ? 100 + s : alx_axion_run(&p, r, n, present);
}

static void test_axion(void)
{
    /* the gates are lpres's gates, all sixteen pairs */
    static const char *const TV[4] = {"neither", "true", "false", "both"};
    for (int a = 0; a < 4; a++) {
        char src[64];
        snprintf(src, sizeof src, "%s not", TV[a]);
        CHECK(ax(src, 0, 0, 0) == (int) lpres_negate((lpres_state_t) a), "not %s", TV[a]);
        for (int b = 0; b < 4; b++) {
            snprintf(src, sizeof src, "%s %s and", TV[a], TV[b]);
            CHECK(ax(src, 0, 0, 0) == (int) lpres_conjoin((lpres_state_t) a, (lpres_state_t) b),
                  "%s", src);
            snprintf(src, sizeof src, "%s %s or", TV[a], TV[b]);
            CHECK(ax(src, 0, 0, 0) == (int) lpres_disjoin((lpres_state_t) a, (lpres_state_t) b),
                  "%s", src);
            snprintf(src, sizeof src, "%s %s join", TV[a], TV[b]);
            CHECK(ax(src, 0, 0, 0) == (a | b), "%s", src); /* evidence for and against */
        }
    }
    /* a drive that reports ready (reading 0 = 1) while its bus reads idle
     * (reading 1 = 0) is BOTH, and the caller falls back */
    const char *drive = "# ready, and the bus agrees\n"
                        "in 0 int 1 eq\n"
                        "int 1 in 1 eq\n"
                        "join";
    int64_t r[3] = {1, 0, 7};
    CHECK(ax(drive, r, 2, 3) == LPRES_STATE_BOTH, "a contradiction is BOTH");
    r[1] = 1;
    CHECK(ax(drive, r, 2, 3) == LPRES_STATE_TRUE, "agreement is TRUE");
    r[0] = 0;
    r[1] = 0;
    CHECK(ax(drive, r, 2, 3) == LPRES_STATE_FALSE, "both say not ready");
    CHECK(ax(drive, r, 2, 1) == LPRES_STATE_FALSE, "a missing reading adds nothing");
    CHECK(ax(drive, r, 2, 0) == LPRES_STATE_NEITHER, "no readings: NEITHER");
    CHECK(ax(drive, r, 1, 3) == ALX_E_LIMIT && ax(drive, 0, 2, 3) == ALX_E_LIMIT,
          "a reading not given");
    CHECK(ax("in 2 int 7 le", r, 3, 4) == LPRES_STATE_TRUE &&
              ax("in 2 int 7 lt", r, 3, 4) == LPRES_STATE_FALSE &&
              ax("int 6 in 2 lt", r, 3, 4) == LPRES_STATE_TRUE &&
              ax("in 2 int 8 eq", r, 3, 4) == LPRES_STATE_FALSE,
          "le lt eq");
    CHECK(ax("in 63 in 63 eq", r, 3, ~0ull) == ALX_E_LIMIT, "reading 63 when 3 are given");
    CHECK(ax("int -9223372036854775808 int 9223372036854775807 lt", 0, 0, 0) == LPRES_STATE_TRUE,
          "the whole range");
    static const struct {
        const char *in;
        int st;
        uint32_t at;
    } bad[] = {
        {"", ALX_E_SYNTAX, 0},
        {"   # nothing\n", ALX_E_SYNTAX, 13},
        {"maybe", ALX_E_SYNTAX, 0},
        {"TRUE", ALX_E_SYNTAX, 0},
        {"truth", ALX_E_SYNTAX, 0},
        {"tru", ALX_E_SYNTAX, 0},
        {"int 1", ALX_E_SYNTAX, 5},
        {"true true", ALX_E_SYNTAX, 9},
        {"and", ALX_E_SYNTAX, 0},
        {"true and", ALX_E_SYNTAX, 5},
        {"int 1 not", ALX_E_SYNTAX, 6},
        {"true int 1 eq", ALX_E_SYNTAX, 11},
        {"int 1 true and", ALX_E_SYNTAX, 11},
        {"in", ALX_E_SYNTAX, 0},
        {"in x", ALX_E_SYNTAX, 3},
        {"in 64 in 0 eq", ALX_E_SYNTAX, 3},
        {"in -1 in 0 eq", ALX_E_SYNTAX, 3},
        {"int 01 int 1 eq", ALX_E_SYNTAX, 4},
        {"int -0 int 1 eq", ALX_E_SYNTAX, 4},
        {"int - int 1 eq", ALX_E_SYNTAX, 4},
        {"int 9223372036854775808 int 1 eq", ALX_E_SYNTAX, 4},
        {"int -9223372036854775809 int 1 eq", ALX_E_SYNTAX, 4},
        {"int 99999999999999999999 int 1 eq", ALX_E_SYNTAX, 4},
        {"int 1x int 1 eq", ALX_E_SYNTAX, 4},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        alx_axion_t p;
        uint32_t at = 999;
        int s = alx_axion_parse(bad[i].in, (uint32_t) strlen(bad[i].in), &p, &at);
        CHECK(s == bad[i].st && at == bad[i].at && p.n == 0, "%s: %s at %u", bad[i].in,
              alx_status_name(s), at);
    }
    /* the bounds: 32 cells, 256 words */
    char src[4096];
    int k = 0;
    for (int i = 0; i < 32; i++) k += snprintf(src + k, sizeof src - (size_t) k, "true ");
    for (int i = 0; i < 31; i++) k += snprintf(src + k, sizeof src - (size_t) k, "and ");
    CHECK(ax(src, 0, 0, 0) == LPRES_STATE_TRUE, "32 deep");
    k = 0;
    for (int i = 0; i < 33; i++) k += snprintf(src + k, sizeof src - (size_t) k, "true ");
    alx_axion_t p;
    uint32_t at;
    CHECK(alx_axion_parse(src, (uint32_t) k, &p, &at) == ALX_E_LIMIT && at == 160, "33 deep (%u)",
          at);
    k = snprintf(src, sizeof src, "true ");
    for (int i = 0; i < 255; i++) k += snprintf(src + k, sizeof src - (size_t) k, "not ");
    CHECK(ax(src, 0, 0, 0) == LPRES_STATE_FALSE, "256 words");
    k += snprintf(src + k, sizeof src - (size_t) k, "not ");
    CHECK(alx_axion_parse(src, (uint32_t) k, &p, &at) == ALX_E_LIMIT && at == 5 + 255 * 4, "257");
    CHECK(alx_axion_parse(0, 3, &p, &at) == ALX_E_SYNTAX &&
              alx_axion_parse("true", 4, 0, &at) == ALX_E_SYNTAX &&
              alx_axion_parse("true", 4, &p, 0) == ALX_OK,
          "no text, nowhere to put it, no position wanted");

    /* a program built by hand is checked as the parser checks text */
    CHECK(alx_axion_parse("true false and", 14, &p, &at) == ALX_OK &&
              alx_axion_run(&p, 0, 0, 0) == 2,
          "parsed");
    alx_axion_t h = p;
    h.op[2] = 99;
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_SYNTAX, "an unknown op");
    h = p;
    h.arg[0] = 4;
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_SYNTAX, "truth 4");
    h = p;
    h.n = 2;
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_SYNTAX, "two left");
    alx_axion_t in;
    CHECK(alx_axion_parse("int 1", 5, &in, &at) == ALX_E_SYNTAX, "(int alone does not parse)");
    CHECK(alx_axion_parse("int 1 int 1 eq", 14, &in, &at) == ALX_OK, "an int word");
    h = p;
    h.op[0] = in.op[0]; /* int 1 where a truth must be */
    h.arg[0] = 1;
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_SYNTAX, "an integer into and");
    h = p;
    h.op[0] = h.op[2]; /* and with nothing under it */
    h.arg[0] = 0;
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_SYNTAX, "underflow");
    h.n = 0;
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_SYNTAX && alx_axion_run(0, 0, 0, 0) == ALX_E_SYNTAX,
          "empty, none");
    h.n = ALX_AXION_WORDS + 1u;
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_SYNTAX, "too long");
    CHECK(alx_axion_parse("in 0 int 1 eq", 13, &h, &at) == ALX_OK, "in");
    h.arg[0] = 64;
    CHECK(alx_axion_run(&h, r, 3, 7) == ALX_E_SYNTAX, "reading 64 by hand");
    for (uint32_t i = 0; i < 33; i++) {
        h.op[i] = p.op[0];
        h.arg[i] = p.arg[0];
    }
    h.n = 33;
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_LIMIT, "33 deep by hand");
    h.n = 1;
    CHECK(alx_axion_run(&h, 0, 0, 0) == 1, "one truth");
    h.op[0] = in.op[0];
    CHECK(alx_axion_run(&h, 0, 0, 0) == ALX_E_SYNTAX, "one integer is no answer");
}

/* change one copy of the index and expect a status from alx_open */
static int open_copy(void (*edit)(uint8_t *, uint32_t *))
{
    uint32_t n = TN;
    memcpy(M, T, TN);
    edit(M, &n);
    rehash(M, n);
    alx_t t;
    int s = alx_open(&t, M, n);
    if (s) CHECK(t.tbl == 0 && t.slots == 0 && t.books == 0, "a refused index leaves nothing open");
    return s;
}

#define EDIT(name, body)                                                                           \
    static void name(uint8_t *b, uint32_t *n)                                                      \
    {                                                                                              \
        (void) b;                                                                                  \
        (void) n;                                                                                  \
        body;                                                                                      \
    }
EDIT(e_magic, b[3] = 'U')
EDIT(e_version, b[4] = 2)
EDIT(e_rsv5, b[5] = 1)
EDIT(e_rsv7, b[7] = 1)
EDIT(e_rsv24, b[24] = 1)
EDIT(e_rsv31, b[31] = 1)
EDIT(e_base, b[22] = 2)
EDIT(e_slots_up, b[11]++)
EDIT(e_books_up, b[15]++)
EDIT(e_targets_up, b[19]++)
EDIT(e_targets_down, b[19]--)
EDIT(e_short, *n -= 1)
EDIT(e_div_low, book(b, 0)[1] = 90)
EDIT(e_div_odd, book(b, 0)[1]++)
EDIT(e_div_order, memcpy(book(b, 0), book(b, 1), 2))
EDIT(e_div_high, book(b, 1)[0] = 3; book(b, 1)[1] = 0xE8)
EDIT(e_blen_small, memset(book(b, 0) + 4, 0, 3); book(b, 0)[7] = 32)
EDIT(e_blen_odd, book(b, 0)[7] |= 1)
EDIT(e_blen_big, book(b, 0)[5] = 0x10; book(b, 0)[6] = 0; book(b, 0)[7] = 0x40)
EDIT(e_brsv, book(b, 0)[3] = 1)
EDIT(e_brsv8, book(b, 0)[8] = 1)
EDIT(e_bcid, book(b, 0)[13] = 0x70)
EDIT(e_bcid0, book(b, 0)[12] = 0)
EDIT(e_slot_no, slot(b, 2)[3] = 3)
EDIT(e_kind0, slot(b, 1)[4] = 0)
EDIT(e_kind3, slot(b, 1)[4] = 3)
EDIT(e_inputs, slot(b, 1)[5] = 16)
EDIT(e_flag4, slot(b, 1)[6] |= 4)
EDIT(e_witness_off, slot(b, 1)[6] = 0)
EDIT(e_witness_on, slot(b, 4)[6] = ALX_F_WITNESSED)
EDIT(e_vrsv, slot(b, 1)[15] = 1)
EDIT(e_upper, slot(b, 1)[16] = 'N')
EDIT(e_nodot, slot(b, 1)[19] = '_')
EDIT(e_twodots, slot(b, 1)[17] = '.')
EDIT(e_dotend, memcpy(slot(b, 1) + 16, "num.", 5))
EDIT(e_dotdot, memcpy(slot(b, 1) + 48, "number..lassics", 15))
EDIT(e_pad, slot(b, 1)[16 + 31] = 'x')
EDIT(e_pad_mid, slot(b, 1)[16 + 20] = 'x')
EDIT(e_empty, memset(slot(b, 1) + 16, 0, 32))
EDIT(e_tfull, memset(slot(b, 1) + 48, 'a', 44))
EDIT(e_ns, memcpy(slot(b, 1) + 16, "nun", 3))
EDIT(e_section, slot(b, 1)[93] = 0x01)                         /* 513: same division */
EDIT(e_division, slot(b, 1)[93] = 0x10)                        /* 528: not booklet 510 */
EDIT(e_class_wrong, slot(b, 1)[92] = 2; slot(b, 1)[93] = 0x58) /* 600 */
EDIT(e_place_low, slot(b, 1)[92] = 0; slot(b, 1)[93] = 99)
EDIT(e_place_high, slot(b, 1)[92] = 3; slot(b, 1)[93] = 0xE8)
EDIT(e_item0, slot(b, 1)[95] = 0)
EDIT(e_item100, slot(b, 1)[95] = 100)
EDIT(e_dup_short, memcpy(slot(b, 3) + 16, slot(b, 2) + 16, 32))
EDIT(e_dup_target, memcpy(slot(b, 4) + 48, slot(b, 3) + 48, 44))
EDIT(e_dup_place, slot(b, 3)[95] = 1)
EDIT(e_book_idx, slot(b, 1)[97] = 2)
EDIT(e_book_wrong, slot(b, 1)[97] = 1)
EDIT(e_srsv98, slot(b, 1)[98] = 1)
EDIT(e_first, slot(b, 2)[103]++)
EDIT(e_moff_odd, slot(b, 1)[107] += 1)
EDIT(e_moff_head, memset(slot(b, 1) + 104, 0, 4))
EDIT(e_mlen0, memset(slot(b, 1) + 108, 0, 4))
EDIT(e_mlen_past, slot(b, 1)[109] = 0x10)
EDIT(e_t_slot, target(b, 0)[3] = 2)
EDIT(e_t_lang0, target(b, 0)[5] = 0)
EDIT(e_t_lang100, target(b, 0)[5] = 100)
EDIT(e_t_lang121, target(b, 3)[5] = 121)
EDIT(e_t_order, target(b, 1)[5] = 1)
EDIT(e_t_rsv, target(b, 0)[7] = 1)
EDIT(e_t_off, target(b, 0)[11] += 4)
EDIT(e_t_len0, memset(target(b, 0) + 12, 0, 4))
EDIT(e_t_past, target(b, 0)[13] = 0x10)
EDIT(e_t_offpast, target(b, 0)[9] = 0x40)
EDIT(e_langs_more, slot(b, 4)[7] = 2; slot(b, 4)[6] = ALX_F_WITNESSED)
static void retire4(uint8_t *b, uint32_t *n)
{
    uint8_t *p = slot(b, 4);
    p[4] = p[5] = p[7] = 0;
    p[6] = ALX_F_RETIRED;
    memset(p + 8, 0, 6);
    p[96] = p[97] = 0xFF;
    memset(p + 100, 0, 44);
    *n -= ALX_TARGET; /* its one target, the last, goes */
    b[19]--;
}
EDIT(e_ret_kind, retire4(b, n); slot(b, 4)[4] = 1)
EDIT(e_ret_inputs, retire4(b, n); slot(b, 4)[5] = 1)
EDIT(e_ret_lang, retire4(b, n); slot(b, 4)[7] = 1)
EDIT(e_ret_flags, retire4(b, n); slot(b, 4)[6] = ALX_F_RETIRED | ALX_F_WITNESSED)
EDIT(e_ret_ver, retire4(b, n); slot(b, 4)[13] = 1)
EDIT(e_ret_book, retire4(b, n); slot(b, 4)[97] = 1)
EDIT(e_ret_first, retire4(b, n); slot(b, 4)[103] = 1)
EDIT(e_ret_moff, retire4(b, n); slot(b, 4)[107] = 64)
EDIT(e_ret_mlen, retire4(b, n); slot(b, 4)[111] = 1)
EDIT(e_ret_hash, retire4(b, n); slot(b, 4)[143] = 1)

/* ---- the grid ---- */

static alx_grid_t G;

static uint64_t gw(uint32_t domain, uint32_t place, uint32_t item, uint32_t ext, uint32_t format,
                   uint32_t lang)
{
    return (uint64_t) domain << 56 | (uint64_t) place << 40 | (uint64_t) item << 32 |
           (uint64_t) ext << 16 | (uint64_t) format << 8 | lang;
}

static int gr(const alx_grid_t *g, uint64_t w)
{
    alx_grid_hit_t h;
    return alx_grid_resolve(g, w, &h);
}

EDIT(e_cell, memset(slot(b, 2) + 16, 0, 32); memcpy(slot(b, 2) + 16, "cell.spinewyrm", 14))
EDIT(e_cells, memset(slot(b, 2) + 16, 0, 32); memcpy(slot(b, 2) + 16, "cells.spinewyrm", 15))
EDIT(e_cell_class, memset(slot(b, 1) + 16, 0, 32); memcpy(slot(b, 1) + 16, "cell.gcd", 8))
EDIT(e_swap_items, slot(b, 2)[95] = 2; slot(b, 3)[95] = 1)

static void test_grid(void)
{
    CHECK(open_copy(e_cell) == ALX_OK, "cell. is a class-900 namespace");
    CHECK(open_copy(e_cells) == ALX_E_FORM, "cells. is not a namespace");
    CHECK(open_copy(e_cell_class) == ALX_E_FORM, "cell. is not in class 500");
    CHECK(alx_grid_build(&G, &X) == ALX_OK, "the radix table builds");
    uint64_t w = 0;
    CHECK(alx_grid_word(&X, ALX_SLOT_NUM_GCD, ALX_G_LOGIC, 1, &w) == ALX_OK &&
              w == 0x1002000100000001ull,
          "gcd in FORTRAN is 0x1002000100000001, not %016llx", (unsigned long long) w);
    CHECK(alx_grid_word(&X, ALX_SLOT_NUM_GCD, ALX_G_MANIFEST, 0, &w) == ALX_OK &&
              w == 0x1002000100000100ull,
          "gcd's manifest word");
    CHECK(alx_grid_word(&X, ALX_SLOT_SWARM_WITHIN_LIMITS, ALX_G_LOGIC, 103, &w) == ALX_OK &&
              w == 0x1003A40100000067ull,
          "932.01 in eve");

    /* every slot and language: word to bytes, the same bytes the index gives */
    unsigned words = 0;
    for (uint32_t s = 1; s <= X.slots; s++) {
        alx_entry_t e;
        alx_get(&X, s, &e);
        for (uint32_t l = 0; l <= 120; l++) {
            uint32_t format = l ? ALX_G_LOGIC : ALX_G_MANIFEST, off, len;
            int want = l ? alx_target(&X, s, l, &off, &len) : ALX_OK;
            if (!l) off = e.manifest_off, len = e.manifest_len;
            int got = alx_grid_word(&X, s, format, l, &w);
            if (l && !(l <= 99 || l >= 101)) want = ALX_E_LANG;
            CHECK(got == want, "slot %u language %u: word %d, index %d", s, l, got, want);
            if (got) continue;
            words++;
            alx_grid_hit_t h;
            CHECK(alx_grid_resolve(&G, w, &h) == ALX_OK && h.slot == s && h.language == l &&
                      h.format == format && h.booklet == e.booklet && h.off == off && h.len == len,
                  "slot %u language %u resolves to its own bytes", s, l);
        }
    }
    CHECK(words == X.slots + X.targets, "one word per manifest and per language (%u)", words);

    /* every place and item: exactly the slots answer */
    unsigned hits = 0;
    for (uint32_t p = 100; p <= 999; p++)
        for (uint32_t i = 1; i <= 99; i++) {
            alx_grid_hit_t h;
            int s = alx_grid_resolve(&G, gw(ALX_G_CODE, p, i, 0, ALX_G_MANIFEST, 0), &h);
            if (s == ALX_OK) {
                alx_entry_t e;
                alx_get(&X, h.slot, &e);
                CHECK(e.place == p && e.item == i, "%u.%02u gives slot %u", p, i, h.slot);
                hits++;
            } else
                CHECK(s == ALX_E_NAME, "%u.%02u: %d", p, i, s);
        }
    CHECK(hits == X.slots, "every slot found once by place (%u)", hits);

    /* refusals */
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 0, 2)) == ALX_OK, "BASIC gcd");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 0, 4)) == ALX_E_LANG, "no gcd in language 4");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 0, 101)) == ALX_E_LANG, "no gcd in c");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 2, 0, 1, 0)) == ALX_E_NAME, "nothing at 512.02");
    CHECK(gr(&G, gw(ALX_G_CODE, 513, 1, 0, 1, 0)) == ALX_E_NAME, "nothing at 513.01");
    CHECK(gr(&G, gw(ALX_G_CODE, 931, 3, 0, 1, 0)) == ALX_E_NAME, "nothing at 931.03");
    CHECK(gr(&G, gw(ALX_G_CODE, 999, 99, 0, 1, 0)) == ALX_E_NAME, "nothing at 999.99");
    CHECK(gr(&G, gw(ALX_G_CODE, 100, 1, 0, 1, 0)) == ALX_E_NAME, "nothing at 100.01");
    static const uint32_t reserved[] = {ALX_G_DATA, ALX_G_SENSOR, ALX_G_EMBED, ALX_G_ATTEST};
    for (uint32_t i = 0; i < 4; i++)
        CHECK(gr(&G, gw(reserved[i], 512, 1, 0, 0, 1)) == ALX_E_DOMAIN, "domain %x is reserved",
              reserved[i]);
    static const uint32_t bad[] = {0x00, 0x0F, 0x11, 0x1F, 0x21, 0x30, 0x40, 0x4F,
                                   0x51, 0x5F, 0x61, 0x6F, 0x71, 0x80, 0xFF};
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(gr(&G, gw(bad[i], 512, 1, 0, 0, 1)) == ALX_E_FORM, "domain %x is no domain", bad[i]);
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 1, 0, 1)) == ALX_E_OVERLAY, "an addendum is not mounted");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0xFFFF, 1, 0)) == ALX_E_OVERLAY, "extension 0xFFFF");
    CHECK(gr(&G, gw(ALX_G_SENSOR, 512, 1, 1, 0, 1)) == ALX_E_DOMAIN, "domain before extension");
    CHECK(gr(&G, gw(ALX_G_SENSOR, 99, 1, 0, 0, 1)) == ALX_E_FORM, "form before domain");
    CHECK(gr(&G, gw(ALX_G_CODE, 99, 1, 0, 1, 0)) == ALX_E_FORM, "place 99");
    CHECK(gr(&G, gw(ALX_G_CODE, 1000, 1, 0, 1, 0)) == ALX_E_FORM, "place 1000");
    CHECK(gr(&G, gw(ALX_G_CODE, 0xFFFF, 1, 0, 1, 0)) == ALX_E_FORM, "place 0xFFFF");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 0, 0, 1, 0)) == ALX_E_FORM, "item 0");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 100, 0, 1, 0)) == ALX_E_FORM, "item 100");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 2, 1)) == ALX_E_FORM, "format 2");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 0xFF, 0)) == ALX_E_FORM, "format 0xFF");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 1, 1)) == ALX_E_FORM, "a manifest has no language");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 0, 0)) == ALX_E_FORM, "logic needs a language");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 0, 100)) == ALX_E_FORM, "language 100");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 0, 121)) == ALX_E_FORM, "language 121");
    CHECK(gr(&G, gw(ALX_G_CODE, 512, 1, 0, 0, 255)) == ALX_E_FORM, "language 255");
    alx_grid_hit_t h;
    CHECK(alx_grid_resolve(&G, gw(ALX_G_CODE, 512, 1, 0, 0, 1), 0) == ALX_E_FORM, "no hit");
    CHECK(alx_grid_resolve(0, 0x1002000100000001ull, &h) == ALX_E_FORM, "no table");
    alx_grid_t g0;
    g0.t = 0;
    CHECK(alx_grid_resolve(&g0, 0x1002000100000001ull, &h) == ALX_E_FORM, "an unbuilt table");
    CHECK(alx_grid_build(0, &X) == ALX_E_FORM && alx_grid_build(&g0, 0) == ALX_E_FORM,
          "build without a table or index");
    alx_t closed = {0};
    CHECK(alx_grid_build(&g0, &closed) == ALX_E_FORM && g0.t == 0, "build on a closed index");

    CHECK(alx_grid_word(&X, 0, ALX_G_LOGIC, 1, &w) == ALX_E_SLOT, "slot 0");
    CHECK(alx_grid_word(&X, X.slots + 1, ALX_G_LOGIC, 1, &w) == ALX_E_SLOT, "slot past the end");
    CHECK(alx_grid_word(&X, 1, 2, 1, &w) == ALX_E_FORM, "word with format 2");
    CHECK(alx_grid_word(&X, 1, ALX_G_MANIFEST, 1, &w) == ALX_E_LANG,
          "a manifest word with a language");
    CHECK(alx_grid_word(&X, 1, ALX_G_LOGIC, 0, &w) == ALX_E_LANG, "a logic word without one");
    CHECK(alx_grid_word(&X, 1, ALX_G_LOGIC, 1, 0) == ALX_E_LIMIT, "nowhere to write");

    /* items out of order in the index still resolve by item */
    alx_t t2;
    uint32_t n = TN;
    static alx_grid_t G2;
    memcpy(M, T, TN);
    e_swap_items(M, &n);
    rehash(M, n);
    CHECK(alx_open(&t2, M, n) == ALX_OK && alx_grid_build(&G2, &t2) == ALX_OK, "swapped items");
    CHECK(alx_grid_resolve(&G2, gw(ALX_G_CODE, 931, 1, 0, 1, 0), &h) == ALX_OK && h.slot == 3 &&
              alx_grid_resolve(&G2, gw(ALX_G_CODE, 931, 2, 0, 1, 0), &h) == ALX_OK && h.slot == 2,
          "931.01 is slot 3 and 931.02 slot 2");

    /* a retired slot keeps its place and refuses */
    n = TN;
    memcpy(M, T, TN);
    retire4(M, &n);
    rehash(M, n);
    CHECK(alx_open(&t2, M, n) == ALX_OK && alx_grid_build(&G2, &t2) == ALX_OK, "retired copy");
    CHECK(gr(&G2, gw(ALX_G_CODE, 932, 1, 0, 1, 0)) == ALX_E_RETIRED &&
              gr(&G2, gw(ALX_G_CODE, 932, 1, 0, 0, 103)) == ALX_E_RETIRED,
          "932.01 is retired");
    CHECK(alx_grid_word(&t2, 4, ALX_G_MANIFEST, 0, &w) == ALX_E_RETIRED, "no word for it");
    CHECK(gr(&G2, gw(ALX_G_CODE, 931, 2, 0, 0, 104)) == ALX_OK, "its neighbours still resolve");
}

/* ---- layer lineage ---- */

static void cid_of(const uint8_t *m, uint32_t n, uint8_t out[36])
{
    out[0] = 0x01, out[1] = 0x55, out[2] = 0x12, out[3] = 0x20;
    sha256(m, n, out + 4);
}

static void test_lineage(void)
{
    static uint8_t man[64][16];
    static alx_layer_t L[65];
    uint8_t leaf[36], want[36];
    uint32_t at;
    for (uint32_t i = 0; i < 64; i++) {
        memset(man[i], 0, 16);
        snprintf((char *) man[i], 16, "layer %u", i);
        L[i].level = (uint8_t) (i < 4 ? i : 4);
        L[i].manifest = man[i];
        L[i].len = 16;
        if (i)
            cid_of(man[i - 1], 16, L[i].parent);
        else
            memset(L[i].parent, 0, 36);
    }
    for (uint32_t n = 1; n <= 64; n++) {
        memset(leaf, 0xAA, 36);
        cid_of(man[n - 1], 16, want);
        CHECK(alx_lineage(L, n, leaf, &at) == ALX_OK && memcmp(leaf, want, 36) == 0 && at == n - 1,
              "a chain of %u layers", n);
    }
    CHECK(alx_lineage(L, 5, 0, 0) == ALX_OK, "no leaf or position wanted");
    static alx_layer_t big;
    memset(&big, 0, sizeof big);
    big.manifest = M;
    big.len = ALX_BOOK_MAX;
    CHECK(alx_lineage(&big, 1, 0, 0) == ALX_OK, "a 1 MiB root");
    L[5].level = 3;
    CHECK(alx_lineage(L, 6, leaf, &at) == ALX_E_FORM && at == 5, "L3 over an L4");
    L[5].level = 4;
    CHECK(alx_lineage(L, 65, leaf, &at) == ALX_E_FORM && at == 0, "65 layers");
    CHECK(alx_lineage(L, 0, leaf, &at) == ALX_E_FORM, "no layers");
    CHECK(alx_lineage(0, 1, leaf, &at) == ALX_E_FORM, "no array");

    static alx_layer_t C[5];
#define LCASE(edit, n, st, pos, what)                                                              \
    do {                                                                                           \
        memcpy(C, L, sizeof C);                                                                    \
        edit;                                                                                      \
        memset(leaf, 0xAA, 36);                                                                    \
        int s_ = alx_lineage(C, n, leaf, &at);                                                     \
        CHECK(s_ == (st) && at == (pos) && leaf[0] == 0xAA, "%s: %d at %u", what, s_, at);         \
    } while (0)
    LCASE(C[0].level = 1, 5, ALX_E_FORM, 0, "a root above L0");
    LCASE(C[0].parent[35] = 1, 5, ALX_E_LINEAGE, 0, "a root with a parent");
    LCASE(C[0].parent[0] = 1, 1, ALX_E_LINEAGE, 0, "a root with a parent's first byte");
    LCASE(C[2].level = 3, 5, ALX_E_FORM, 2, "L1 to L3");
    LCASE(C[2].level = 1, 5, ALX_E_FORM, 2, "L1 twice");
    LCASE(C[1].level = 0, 5, ALX_E_FORM, 1, "L0 twice");
    LCASE(C[4].level = 5, 5, ALX_E_FORM, 4, "L5");
    LCASE(C[3].parent[35] ^= 1, 5, ALX_E_LINEAGE, 3, "the wrong parent");
    LCASE(C[3].parent[4] ^= 0x80, 5, ALX_E_LINEAGE, 3, "the wrong parent digest");
    LCASE(C[3].parent[1] = 0x70, 5, ALX_E_LINEAGE, 3, "a dag-pb parent");
    LCASE(C[3].parent[0] = 0, 5, ALX_E_LINEAGE, 3, "a parent's first byte");
    LCASE(C[3].manifest = 0, 5, ALX_E_FORM, 3, "no manifest");
    LCASE(C[3].len = 0, 5, ALX_E_FORM, 3, "an empty manifest");
    LCASE(C[3].len = ALX_BOOK_MAX + 1, 5, ALX_E_FORM, 3, "a manifest over 1 MiB");
    LCASE(man[2][15] = 1, 5, ALX_E_LINEAGE, 3, "a changed manifest breaks its child");
    man[2][15] = 0;
    man[4][15] = 1;
    cid_of(man[4], 16, want);
    CHECK(alx_lineage(L, 5, leaf, &at) == ALX_OK && memcmp(leaf, want, 36) == 0,
          "a changed top layer gives a new leaf");
    man[4][15] = 0;
#undef LCASE
}

static void test_refusals(void)
{
    static const struct {
        void (*edit)(uint8_t *, uint32_t *);
        const char *what;
    } c[] = {
        {e_magic, "magic"},
        {e_version, "version"},
        {e_rsv5, "header byte 5"},
        {e_rsv7, "header byte 7"},
        {e_rsv24, "header byte 24"},
        {e_rsv31, "header byte 31"},
        {e_base, "base end"},
        {e_slots_up, "a slot count past the bytes"},
        {e_books_up, "a booklet count past the bytes"},
        {e_targets_up, "a target count past the bytes"},
        {e_targets_down, "a target count short of the bytes"},
        {e_short, "a byte missing"},
        {e_div_low, "division 90"},
        {e_div_odd, "division 511"},
        {e_div_order, "divisions out of order"},
        {e_div_high, "division 1000"},
        {e_blen_small, "a booklet of 32 bytes"},
        {e_blen_odd, "a booklet length not a multiple of 64"},
        {e_blen_big, "a booklet over 1 MiB"},
        {e_brsv, "booklet byte 3"},
        {e_brsv8, "booklet byte 8"},
        {e_bcid, "a dag-pb booklet"},
        {e_bcid0, "booklet CID version 0"},
        {e_slot_no, "a slot out of place"},
        {e_kind0, "kind 0"},
        {e_kind3, "kind 3"},
        {e_inputs, "16 inputs"},
        {e_flag4, "an unknown flag"},
        {e_witness_off, "four languages, not witnessed"},
        {e_witness_on, "one language, witnessed"},
        {e_vrsv, "version padding"},
        {e_upper, "upper case"},
        {e_nodot, "no dot"},
        {e_twodots, "two dots in a shorthand"},
        {e_dotend, "a trailing dot"},
        {e_dotdot, "two dots together"},
        {e_pad, "no NUL"},
        {e_pad_mid, "text after the NUL"},
        {e_empty, "an empty shorthand"},
        {e_tfull, "a target with no NUL"},
        {e_ns, "an unknown namespace"},
        {e_division, "a place outside its booklet's division"},
        {e_class_wrong, "num in class 600"},
        {e_place_low, "place 99"},
        {e_place_high, "place 1000"},
        {e_item0, "item 0"},
        {e_item100, "item 100"},
        {e_dup_short, "a repeated shorthand"},
        {e_dup_target, "a repeated target"},
        {e_dup_place, "a repeated place"},
        {e_book_idx, "booklet 2 of 2"},
        {e_book_wrong, "gcd in booklet 930"},
        {e_srsv98, "slot byte 98"},
        {e_first, "a first target out of step"},
        {e_moff_odd, "a manifest off the 64-byte grid"},
        {e_moff_head, "a manifest in the booklet header"},
        {e_mlen0, "an empty manifest"},
        {e_mlen_past, "a manifest past the booklet"},
        {e_t_slot, "a target for another slot"},
        {e_t_lang0, "language 0"},
        {e_t_lang100, "language 100"},
        {e_t_lang121, "language 121"},
        {e_t_order, "languages not rising"},
        {e_t_rsv, "target byte 7"},
        {e_t_off, "a target off the grid"},
        {e_t_len0, "an empty target"},
        {e_t_past, "a target past the booklet"},
        {e_t_offpast, "a target starting past the booklet"},
        {e_langs_more, "more languages than targets"},
        {e_ret_kind, "retired with a kind"},
        {e_ret_inputs, "retired with inputs"},
        {e_ret_lang, "retired with a language"},
        {e_ret_flags, "retired and witnessed"},
        {e_ret_ver, "retired with a version"},
        {e_ret_book, "retired in a booklet"},
        {e_ret_first, "retired with a first target"},
        {e_ret_moff, "retired with a manifest"},
        {e_ret_mlen, "retired with a manifest length"},
        {e_ret_hash, "retired with a content id"},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        int s = open_copy(c[i].edit);
        CHECK(s == ALX_E_FORM, "%s: FORM, got %s", c[i].what, alx_status_name(s));
    }
    CHECK(open_copy(e_section) == ALX_OK, "another section in the same division is fine");
    /* every single changed byte is caught, by the hash if nothing else */
    for (uint32_t i = 0; i < TN; i++) {
        memcpy(M, T, TN);
        M[i] ^= 0x20;
        alx_t t;
        int s = alx_open(&t, M, TN);
        CHECK(s == ALX_E_HASH || (s == ALX_E_FORM && i < ALX_HEAD), "byte %u: %s", i,
              alx_status_name(s));
    }
    alx_t t;
    CHECK(alx_open(&t, T, ALX_HEAD + 31u) == ALX_E_FORM && alx_open(&t, 0, TN) == ALX_E_FORM &&
              alx_open(0, T, TN) == ALX_E_FORM,
          "too short, no index, nowhere to open");
    alx_entry_t e;
    CHECK(alx_get(&t, 1, &e) == ALX_E_SLOT && find(&t, "@num.gcd") == ALX_E_NAME,
          "an unopened index answers nothing");

    /* a retired slot keeps its name, number and place, and never docks */
    CHECK(open_copy(retire4) == ALX_OK, "a retired slot is well formed");
    CHECK(alx_open(&t, M, TN - ALX_TARGET) == ALX_OK, "reopen");
    alx_ref_t r;
    CHECK(find(&t, "@swarm.within_limits") == 4 && find(&t, "@932.01") == 4 &&
              alx_get(&t, 4, &e) == ALX_OK && e.flags == ALX_F_RETIRED && e.kind == 0 &&
              e.booklet == 0xFFFF,
          "still named and placed");
    CHECK(resolve(&t, "@932.01-103", &r) == ALX_E_RETIRED &&
              alx_target(&t, 4, 103, 0, 0) == ALX_E_RETIRED,
          "but has no logic");
    CHECK(alx_dock(&t, 4, 2) == ALX_E_RETIRED && alx_dock(&t, 1, 4) == ALX_E_RETIRED,
          "retired never docks");
    uint32_t p[] = {4, 2}, at = 9;
    CHECK(alx_pipeline(&t, p, 2, &at) == ALX_E_RETIRED && at == 0, "nor starts a pipeline");

    /* an empty index is valid */
    static uint8_t E[ALX_HEAD + 32];
    memcpy(E, T, ALX_HEAD);
    memset(E + 8, 0, 12);
    rehash(E, sizeof E);
    CHECK(alx_open(&t, E, sizeof E) == ALX_OK && t.slots == 0 && find(&t, "@num.gcd") == ALX_E_NAME,
          "an empty index");
    CHECK(strcmp(alx_status_name(ALX_E_DOCK), "DOCK") == 0 &&
              strcmp(alx_status_name(ALX_E_OVERLAY), "OVERLAY") == 0 &&
              strcmp(alx_status_name(ALX_E_DOMAIN), "DOMAIN") == 0 &&
              strcmp(alx_status_name(-15), "SIGN") == 0 && strcmp(alx_status_name(-16), "?") == 0 && strcmp(alx_status_name(5), "?") == 0,
          "status names");
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    SRCDIR = argv[2];
    TN = slurp(argv[1], "ALEXANDRIA.TBL", T, sizeof T);
    B510N = slurp(argv[1], "B510.booklet", B510, sizeof B510);
    B930N = slurp(argv[1], "B930.booklet", B930, sizeof B930);
    if (alx_open(&X, T, TN) != ALX_OK) {
        printf("[FAIL] the built index does not open\n");
        return 1;
    }
    test_read();
    test_refs();
    test_tecton();
    test_axion();
    test_refusals();
    test_grid();
    test_lineage();
    printf("%u checks, %u failures\n", checks, fails);
    return fails ? 1 : 0;
}
