/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_alx.c — the Alexandria index read back from the table the build
 * script wrote (argv[1]), then every way a table can be wrong. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx.h"
#include "alx_slots.h"
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

static uint8_t T[ALX_HEAD + ALX_MAX * ALX_ENTRY + 32], M[sizeof T];
static uint32_t TN;

static void rehash(uint8_t *b, uint32_t n)
{
    sha256(b, n - 32u, b + n - 32u);
}

static uint8_t *ent(uint8_t *b, uint32_t slot)
{
    return b + ALX_HEAD + (slot - 1u) * ALX_ENTRY;
}

static int32_t find(const alx_t *t, const char *s)
{
    return alx_find(t, s, (uint32_t) strlen(s));
}

static void test_read(void)
{
    alx_t t;
    CHECK(alx_open(&t, T, TN) == ALX_OK && t.count == ALX_SLOT__COUNT, "the built table opens");
    alx_entry_t e;
    CHECK(alx_get(&t, ALX_SLOT_NUM_GCD, &e) == ALX_OK, "slot 1");
    CHECK(strcmp(e.shorthand, "num.gcd") == 0 && strcmp(e.target, "number_classics.gcd") == 0,
          "names %s %s", e.shorthand, e.target);
    CHECK(e.kind == ALX_NUM && e.inputs == 2 && e.languages == 4 && e.flags == ALX_F_WITNESSED,
          "gcd: num of two inputs in four languages (%u %u %u %u)", e.kind, e.inputs, e.languages,
          e.flags);
    CHECK(e.major == 1 && e.minor == 0 && e.patch == 0, "version 1.0.0");
    CHECK(e.cid[0] == 1 && e.cid[1] == 0x55 && e.cid[2] == 0x12 && e.cid[3] == 0x20, "a raw CID");
    CHECK(alx_get(&t, ALX_SLOT_SWARM_WITHIN_LIMITS, &e) == ALX_OK && e.kind == ALX_TRUTH &&
              e.inputs == 2 && e.languages == 1 && e.flags == 0,
          "within_limits: truth, one language, not witnessed");
    CHECK(alx_get(&t, 0, &e) == ALX_E_SLOT && alx_get(&t, ALX_SLOT__COUNT + 1u, &e) == ALX_E_SLOT,
          "no slot 0 or past the end");

    /* names */
    CHECK(find(&t, "@num.gcd") == 1 && find(&t, "num.gcd") == 1, "shorthand with or without @");
    CHECK(find(&t, "@S:0001") == 1 && find(&t, "@S:0004") == 4, "slot numbers in hex");
    static const char *const bad[] = {"@num.gc", "@num.gcdx", "",          "@",
                                      "num",     "@NUM.GCD",  "@S:001",    "@S:00001",
                                      "@S:000g", "@S:000a",   "@@num.gcd", "@s:0001"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(find(&t, bad[i]) == ALX_E_NAME, "%s is no name (%d)", bad[i], find(&t, bad[i]));
    CHECK(find(&t, "@S:0000") == ALX_E_SLOT && find(&t, "@S:0005") == ALX_E_SLOT &&
              find(&t, "@S:FFFF") == ALX_E_SLOT,
          "hex slots outside the table");
    char long_name[40];
    memset(long_name, 'a', sizeof long_name);
    CHECK(alx_find(&t, long_name, 32) == ALX_E_NAME && alx_find(&t, long_name, 40) == ALX_E_NAME,
          "too long");
    CHECK(alx_find(&t, 0, 0) == ALX_E_NAME && alx_find(&t, 0, 3) == ALX_E_NAME, "no text");
    /* a prefix of a stored name: "num.gcd" holds "num.g" at its start */
    CHECK(alx_find(&t, "num.gcd", 5) == ALX_E_NAME, "a prefix is not a name");

    char out[40];
    for (uint32_t s = 1; s <= t.count; s++) {
        int32_t n = alx_shorthand(&t, s, out, sizeof out);
        CHECK(n > 1 && alx_find(&t, out, (uint32_t) n) == (int32_t) s, "slot %u round trips", s);
    }
    CHECK(alx_shorthand(&t, 1, out, 8) == 8 && memcmp(out, "@num.gcd", 8) == 0, "exact fit");
    CHECK(alx_shorthand(&t, 1, out, 7) == ALX_E_LIMIT, "one short");
    CHECK(alx_shorthand(&t, 1, 0, 40) == ALX_E_LIMIT, "no buffer");
    CHECK(alx_shorthand(&t, 9, out, 40) == ALX_E_SLOT, "no slot");

    /* composing */
    CHECK(alx_dock(&t, ALX_SLOT_NUM_GCD, ALX_SLOT_SWARM_CYCLE_SHARE) == ALX_OK,
          "a number feeds a one-input block");
    CHECK(alx_dock(&t, ALX_SLOT_SWARM_BUSIEST, ALX_SLOT_NUM_GCD) == ALX_E_DOCK,
          "one answer cannot fill two inputs");
    CHECK(alx_dock(&t, 1, 5) == ALX_E_SLOT && alx_dock(&t, 0, 1) == ALX_E_SLOT, "docking nothing");
    uint32_t p[] = {ALX_SLOT_NUM_GCD, ALX_SLOT_SWARM_CYCLE_SHARE, ALX_SLOT_SWARM_BUSIEST,
                    ALX_SLOT_SWARM_WITHIN_LIMITS},
             at = 99;
    CHECK(alx_pipeline(&t, p, 3, &at) == ALX_OK && at == 0, "gcd, cycle_share, busiest");
    CHECK(alx_pipeline(&t, p, 4, &at) == ALX_E_DOCK && at == 3, "within_limits takes two (%u)", at);
    CHECK(alx_pipeline(&t, p, 1, 0) == ALX_OK && alx_pipeline(&t, p, 0, &at) == ALX_E_SLOT &&
              alx_pipeline(&t, 0, 2, &at) == ALX_E_SLOT,
          "one block, none, no list");
    uint32_t q[] = {7};
    CHECK(alx_pipeline(&t, q, 1, &at) == ALX_E_SLOT && at == 0, "an unknown first block");
}

/* change one table and expect a status from alx_open */
static int open_copy(void (*edit)(uint8_t *, uint32_t *), bool hash_again)
{
    uint32_t n = TN;
    memcpy(M, T, TN);
    edit(M, &n);
    if (hash_again) rehash(M, n);
    alx_t t;
    int s = alx_open(&t, M, n);
    if (s) CHECK(t.tbl == 0 && t.count == 0, "a refused table leaves nothing open");
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
EDIT(e_rsv20, b[20] = 1)
EDIT(e_rsv31, b[31] = 1)
EDIT(e_size, b[15] = 64)
EDIT(e_base, b[18] = 1)
EDIT(e_count_up, b[11]++)
EDIT(e_count_down, b[11]--)
EDIT(e_short, *n -= 1)
EDIT(e_slot, ent(b, 2)[3] = 3)
EDIT(e_kind0, ent(b, 1)[4] = 0)
EDIT(e_kind3, ent(b, 1)[4] = 3)
EDIT(e_inputs, ent(b, 1)[5] = 16)
EDIT(e_flag4, ent(b, 1)[6] |= 4)
EDIT(e_witness_off, ent(b, 1)[6] = 0)
EDIT(e_witness_on, ent(b, 4)[6] = ALX_F_WITNESSED)
EDIT(e_nolang, ent(b, 4)[7] = 0)
EDIT(e_vrsv, ent(b, 1)[15] = 1)
EDIT(e_upper, ent(b, 1)[16] = 'N')
EDIT(e_lead_digit, ent(b, 1)[16] = '1')
EDIT(e_nodot, ent(b, 1)[19] = '_')
EDIT(e_twodots, ent(b, 1)[17] = '.')
EDIT(e_dotend, memcpy(ent(b, 1) + 16, "num.", 5))
EDIT(e_dotstart, ent(b, 1)[48] = '.')
EDIT(e_dotdot, memcpy(ent(b, 1) + 48, "number..lassics", 15))
EDIT(e_pad, ent(b, 1)[16 + 31] = 'x')
EDIT(e_pad_mid, ent(b, 1)[16 + 20] = 'x')
EDIT(e_empty, memset(ent(b, 1) + 16, 0, 32))
EDIT(e_full, memset(ent(b, 1) + 16, 'a', 32))
EDIT(e_tfull, memset(ent(b, 1) + 48, 'a', 44))
EDIT(e_dup_short, memcpy(ent(b, 3) + 16, ent(b, 2) + 16, 32))
EDIT(e_dup_target, memcpy(ent(b, 4) + 48, ent(b, 1) + 48, 44))
EDIT(e_cid, ent(b, 2)[92 + 1] = 0x70)
EDIT(e_cid0, ent(b, 2)[92] = 0)
EDIT(e_cid2, ent(b, 2)[92 + 2] = 0x13)
EDIT(e_cid3, ent(b, 2)[92 + 3] = 0x40)
static void retire4(uint8_t *b, uint32_t *n)
{
    (void) n;
    uint8_t *p = ent(b, 4);
    p[4] = p[5] = p[7] = 0;
    p[6] = ALX_F_RETIRED;
    memset(p + 8, 0, 6);
    memset(p + 92, 0, 36);
}
EDIT(e_ret_kind, retire4(b, n); ent(b, 4)[4] = 1)
EDIT(e_ret_inputs, retire4(b, n); ent(b, 4)[5] = 1)
EDIT(e_ret_lang, retire4(b, n); ent(b, 4)[7] = 1)
EDIT(e_ret_flags, retire4(b, n); ent(b, 4)[6] = ALX_F_RETIRED | ALX_F_WITNESSED)
EDIT(e_ret_ver, retire4(b, n); ent(b, 4)[13] = 1)
EDIT(e_ret_cid, retire4(b, n); ent(b, 4)[127] = 1)

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
        {e_rsv20, "header byte 20"},
        {e_rsv31, "header byte 31"},
        {e_size, "entry size"},
        {e_base, "base end"},
        {e_count_up, "a count past the bytes"},
        {e_count_down, "a count short of the bytes"},
        {e_short, "a byte missing"},
        {e_slot, "a slot out of place"},
        {e_kind0, "kind 0"},
        {e_kind3, "kind 3"},
        {e_inputs, "16 inputs"},
        {e_flag4, "an unknown flag"},
        {e_witness_off, "four languages, not witnessed"},
        {e_witness_on, "one language, witnessed"},
        {e_nolang, "no language"},
        {e_vrsv, "version padding"},
        {e_upper, "upper case"},
        {e_lead_digit, "a leading digit"},
        {e_nodot, "no dot"},
        {e_twodots, "two dots in a shorthand"},
        {e_dotend, "a trailing dot"},
        {e_dotstart, "a leading dot"},
        {e_dotdot, "two dots together"},
        {e_pad, "no NUL"},
        {e_pad_mid, "text after the NUL"},
        {e_empty, "an empty shorthand"},
        {e_full, "a shorthand with no NUL"},
        {e_tfull, "a target with no NUL"},
        {e_dup_short, "a repeated shorthand"},
        {e_dup_target, "a repeated target"},
        {e_cid, "a dag-pb CID"},
        {e_cid0, "CID version 0"},
        {e_cid2, "not sha2-256"},
        {e_cid3, "a 64-byte digest"},
        {e_ret_kind, "retired with a kind"},
        {e_ret_inputs, "retired with inputs"},
        {e_ret_lang, "retired with a language"},
        {e_ret_flags, "retired and witnessed"},
        {e_ret_ver, "retired with a version"},
        {e_ret_cid, "retired with a CID"},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        int s = open_copy(c[i].edit, true);
        CHECK(s == ALX_E_FORM, "%s: FORM, got %s", c[i].what, alx_status_name(s));
    }
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
          "too short, no table, nowhere to open");
    alx_entry_t e;
    CHECK(alx_get(&t, 1, &e) == ALX_E_SLOT && find(&t, "@num.gcd") == ALX_E_NAME,
          "an unopened table answers nothing");
    CHECK(alx_get(&t, 1, 0) == ALX_E_SLOT && alx_get(0, 1, &e) == ALX_E_SLOT, "null arguments");

    /* a retired slot keeps its name and number, and never docks */
    CHECK(open_copy(retire4, true) == ALX_OK, "a retired slot is well formed");
    CHECK(alx_open(&t, M, TN) == ALX_OK, "reopen");
    CHECK(find(&t, "@swarm.within_limits") == 4 && alx_get(&t, 4, &e) == ALX_OK &&
              e.flags == ALX_F_RETIRED && e.kind == 0,
          "still named");
    CHECK(alx_dock(&t, 4, 2) == ALX_E_RETIRED && alx_dock(&t, 1, 4) == ALX_E_RETIRED,
          "retired never docks");
    uint32_t p[] = {4, 2}, at = 9;
    CHECK(alx_pipeline(&t, p, 2, &at) == ALX_E_RETIRED && at == 0, "nor starts a pipeline");

    /* an empty table is valid */
    static uint8_t E[ALX_HEAD + 32];
    memcpy(E, T, ALX_HEAD);
    memset(E + 8, 0, 4);
    rehash(E, sizeof E);
    CHECK(alx_open(&t, E, sizeof E) == ALX_OK && t.count == 0 && find(&t, "@num.gcd") == ALX_E_NAME,
          "an empty table");
    CHECK(strcmp(alx_status_name(ALX_E_DOCK), "DOCK") == 0 && strcmp(alx_status_name(5), "?") == 0,
          "status names");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    TN = (uint32_t) fread(T, 1, sizeof T, f);
    fclose(f);
    test_read();
    test_refusals();
    printf("%u checks, %u failures\n", checks, fails);
    return fails ? 1 : 0;
}
