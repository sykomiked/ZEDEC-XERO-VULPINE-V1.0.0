/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_swarm_denom.c — known-answer and property tests for token
 * denominations (D1-D9 in swarm_denom.h). Expected values are worked by hand
 * from the rules, not read back from the code under test. */
#include <stdio.h>
#include "swarm_denom.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);                                       \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint64_t val(const swarm_wallet_t *w)
{
    uint64_t v = 0;
    if (!swarm_denom_value(w, &v)) return UINT64_MAX;
    return v;
}

/* counts must equal want[0..n-1] and be zero above. */
static int counts_are(const swarm_wallet_t *w, const uint64_t *want, uint32_t n)
{
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++)
        if (w->count[k] != (k < n ? want[k] : 0)) return 0;
    return 1;
}

static void test_faces(void)
{
    const uint64_t want[10] = {1, 2, 3, 5, 8, 13, 21, 34, 55, 89};
    for (uint32_t k = 0; k < 10; k++) CHECK(swarm_denom_face(k) == want[k], "face F(k+2)");
    CHECK(swarm_denom_face(14) == 987, "PREMIUM face 987");
    CHECK(swarm_denom_face(23) == 75025, "RACING face 75025");
    CHECK(swarm_denom_face(63) == 17167680177565ULL, "top face F(65)");
    CHECK(swarm_denom_face(64) == 0, "tier 64 out of range");
    for (uint32_t k = 0; k < 64; k++)
        CHECK(swarm_denom_face(k) == swarm_fib(k + 2), "face table = F(k+2)");
    CHECK(!swarm_denom_is_super(9), "tier 9 (89) is not super");
    CHECK(swarm_denom_is_super(10), "tier 10 (144) is super");
    CHECK(swarm_denom_is_super(63), "tier 63 is super");
    CHECK(!swarm_denom_is_super(64), "tier 64 is no tier");
    CHECK(swarm_denom_face(SWARM_OCTANE_REGULAR) == 8, "REGULAR grade = face 8");
    CHECK(swarm_denom_face(SWARM_OCTANE_MIDGRADE) == 89, "MIDGRADE grade = face 89");
}

static void test_init_value_pieces(void)
{
    swarm_wallet_t w;
    CHECK(swarm_denom_init(&w, 1, 64) == SWARM_ERR_ARG, "octane 64 rejected");
    CHECK(swarm_denom_init(0, 1, 0) == SWARM_ERR_ARG, "NULL wallet rejected");
    CHECK(swarm_denom_init(&w, 7, 63) == SWARM_OK, "octane 63 accepted");
    CHECK(w.model_id == 7 && w.octane == 63, "init fields");
    CHECK(val(&w) == 0 && swarm_denom_pieces(&w) == 0, "empty wallet");

    w.count[0] = 5;
    w.count[7] = 2; /* 2 x 34 */
    CHECK(val(&w) == 73, "value 5 + 68");
    CHECK(swarm_denom_pieces(&w) == 7, "pieces 7");

    uint64_t v = 99;
    CHECK(!swarm_denom_value(0, &v) && v == 0, "NULL wallet value fails, out zeroed");
    CHECK(!swarm_denom_value(&w, 0), "NULL out fails");

    swarm_denom_init(&w, 7, 63);
    w.count[0] = UINT64_MAX;
    CHECK(val(&w) == UINT64_MAX, "UINT64_MAX plain tokens is a value");
    CHECK(swarm_denom_pieces(&w) == UINT64_MAX, "pieces UINT64_MAX");
    w.count[1] = 1;
    CHECK(!swarm_denom_value(&w, &v) && v == 0, "sum overflow detected, out zeroed");
    CHECK(swarm_denom_pieces(&w) == UINT64_MAX, "pieces saturate");
    swarm_denom_init(&w, 7, 63);
    w.count[63] = 1074504; /* x F(65) just fits under 2^64 */
    CHECK(swarm_denom_value(&w, &v) && v == 1074504ULL * 17167680177565ULL, "large top tier fits");
    w.count[63] = 1074505; /* past UINT64_MAX / F(65) */
    CHECK(!swarm_denom_value(&w, &v), "product overflow detected");
    CHECK(swarm_denom_pieces(0) == 0, "NULL pieces 0");
}

static void test_canonical(void)
{
    swarm_wallet_t w;
    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    CHECK(swarm_denom_set(&w, 100) == SWARM_OK, "set 100");
    { /* 100 = 89 + 8 + 3 */
        const uint64_t c[10] = {0, 0, 1, 0, 1, 0, 0, 0, 0, 1};
        CHECK(counts_are(&w, c, 10), "Zeckendorf 100 = 89+8+3");
    }
    CHECK(swarm_denom_pieces(&w) == 3, "100 in 3 pieces at MAX");
    CHECK(swarm_denom_is_canonical(&w), "canonical");

    swarm_denom_init(&w, 1, SWARM_OCTANE_REGULAR);
    swarm_denom_set(&w, 100);
    { /* 12 x 8 = 96, rest 4 = 3 + 1 */
        const uint64_t c[5] = {1, 0, 1, 0, 12};
        CHECK(counts_are(&w, c, 5), "REGULAR 100 = 12x8 + 3 + 1");
    }
    CHECK(swarm_denom_pieces(&w) == 14, "100 in 14 pieces at REGULAR");

    swarm_denom_init(&w, 1, SWARM_OCTANE_PLAIN);
    swarm_denom_set(&w, 100);
    CHECK(w.count[0] == 100 && swarm_denom_pieces(&w) == 100, "PLAIN 100 = 100 plain tokens");

    swarm_denom_init(&w, 1, 3);
    swarm_denom_set(&w, 100);
    {
        const uint64_t c[4] = {0, 0, 0, 20};
        CHECK(counts_are(&w, c, 4), "octane 3: 100 = 20 x 5");
    }

    swarm_denom_init(&w, 1, SWARM_OCTANE_RACING);
    swarm_denom_set(&w, 1000000);
    { /* 13 x 75025 = 975325; 24675 = 17711 + 6765 + 144 + 55 */
        uint64_t c[24] = {0};
        c[23] = 13;
        c[20] = 1;
        c[18] = 1;
        c[10] = 1;
        c[8] = 1;
        CHECK(counts_are(&w, c, 24), "RACING 1,000,000");
    }
    CHECK(swarm_denom_pieces(&w) == 17, "1,000,000 in 17 pieces at RACING");

    /* is_canonical rejects */
    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.count[0] = 1;
    w.count[1] = 1;
    CHECK(!swarm_denom_is_canonical(&w), "adjacent 1+2 is not canonical (=3)");
    w.count[0] = 0;
    w.count[1] = 0;
    w.count[2] = 1;
    CHECK(swarm_denom_is_canonical(&w), "single 3 is canonical");
    w.count[2] = 2;
    CHECK(!swarm_denom_is_canonical(&w), "two 3s below the top is not canonical");
    w.count[2] = 1;
    w.count[1] = 1;
    CHECK(!swarm_denom_is_canonical(&w), "2+3 is not canonical (=5), plain tier agrees");
    w.count[1] = 0;
    swarm_denom_init(&w, 1, 2);
    w.count[1] = 1;
    w.count[2] = 1; /* 5 at octane 2 = 1x3 + 2 */
    CHECK(swarm_denom_is_canonical(&w), "remainder may sit next to the top tier");
    w.count[2] = 0;
    w.count[3] = 1;
    CHECK(!swarm_denom_is_canonical(&w), "a token above the octane is not canonical");
    w.octane = 64;
    CHECK(!swarm_denom_is_canonical(&w), "bad octane not canonical");
    CHECK(!swarm_denom_is_canonical(0), "NULL not canonical");
    CHECK(swarm_denom_set(&w, 1) == SWARM_ERR_ARG, "set with bad octane fails");
    CHECK(swarm_denom_set(0, 1) == SWARM_ERR_ARG, "set NULL fails");
    w.octane = 0;
    w.count[0] = UINT64_MAX;
    w.count[3] = 0;
    w.count[1] = 1;
    CHECK(!swarm_denom_is_canonical(&w), "overflowing wallet not canonical");

    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    swarm_denom_set(&w, 0);
    CHECK(val(&w) == 0 && swarm_denom_pieces(&w) == 0, "set 0 empties");
    swarm_denom_set(&w, UINT64_MAX);
    CHECK(val(&w) == UINT64_MAX && swarm_denom_is_canonical(&w), "UINT64_MAX canonical at MAX");
}

static void test_wrap_unwrap(void)
{
    swarm_wallet_t w;
    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.count[0] = 1;
    w.count[1] = 1;
    CHECK(swarm_denom_fuse(&w, 0) == SWARM_OK, "fuse 1+2");
    {
        const uint64_t c[3] = {0, 0, 1};
        CHECK(counts_are(&w, c, 3), "fuse 1+2 -> 3");
    }
    CHECK(swarm_denom_fuse(&w, 0) == SWARM_ERR_ARG, "fuse without pieces fails");
    CHECK(counts_are(&w, (const uint64_t[]){0, 0, 1}, 3), "failed fuse changes nothing");

    w.count[4] = 1; /* 8 */
    w.count[5] = 1; /* 13 */
    CHECK(swarm_denom_fuse(&w, 4) == SWARM_OK && w.count[6] == 1 && w.count[4] == 0 &&
              w.count[5] == 0,
          "fuse 8+13 -> 21");
    CHECK(val(&w) == 24, "fuse conserves");

    swarm_denom_init(&w, 1, 2);
    w.count[0] = 1;
    w.count[1] = 1;
    w.count[2] = 1;
    CHECK(swarm_denom_fuse(&w, 1) == SWARM_ERR_ARG && w.count[3] == 0, "fuse one past the octane");
    w.count[2] = 0;
    CHECK(swarm_denom_fuse(&w, 0) == SWARM_OK, "fuse up to exactly the octane");
    swarm_denom_init(&w, 1, 1);
    w.count[0] = 1;
    w.count[1] = 1;
    CHECK(swarm_denom_fuse(&w, 0) == SWARM_ERR_ARG, "fuse past the octane fails");
    CHECK(w.count[0] == 1 && w.count[1] == 1 && w.count[2] == 0, "and changes nothing");

    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.count[61] = 1;
    w.count[62] = 1;
    CHECK(swarm_denom_fuse(&w, 61) == SWARM_OK && w.count[63] == 1, "fuse into the top tier");
    CHECK(swarm_denom_fuse(&w, 62) == SWARM_ERR_ARG, "no tier above the top");
    w.count[0] = 1;
    w.count[1] = 1;
    w.count[2] = UINT64_MAX;
    CHECK(swarm_denom_fuse(&w, 0) == SWARM_ERR_ARG, "fuse into a full count fails");
    CHECK(w.count[0] == 1 && w.count[1] == 1, "and changes nothing");
    CHECK(swarm_denom_fuse(0, 0) == SWARM_ERR_ARG, "fuse NULL");
    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.octane = 64; /* corrupted */
    w.count[61] = 1;
    w.count[62] = 1;
    CHECK(swarm_denom_fuse(&w, 61) == SWARM_ERR_ARG && w.count[63] == 0, "fuse with a bad octane");
    /* tier + 2 must not wrap past 32 bits into a small, in-octane tier */
    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.count[0] = 1;
    w.count[1] = 1;
    CHECK(swarm_denom_fuse(&w, 0xFFFFFFFEu) == SWARM_ERR_ARG, "fuse tier 2^32-2 (wraps to 0)");
    CHECK(swarm_denom_fuse(&w, 0xFFFFFFFFu) == SWARM_ERR_ARG, "fuse tier 2^32-1 (wraps to 1)");
    CHECK(swarm_denom_fuse(&w, SWARM_DENOM_TIERS) == SWARM_ERR_ARG, "fuse tier 64");
    CHECK(w.count[0] == 1 && w.count[1] == 1 && w.count[2] == 0, "wrapped fuses change nothing");

    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.count[1] = 1;
    CHECK(swarm_denom_split(&w, 1) == SWARM_OK && w.count[0] == 2 && w.count[1] == 0,
          "split 2 -> 1+1");
    CHECK(swarm_denom_split(&w, 0) == SWARM_ERR_ARG, "plain token is atomic");
    CHECK(w.count[0] == 2, "atomic split changes nothing");
    w.count[5] = 1;
    CHECK(swarm_denom_split(&w, 5) == SWARM_OK && w.count[5] == 0 && w.count[4] == 1 &&
              w.count[3] == 1,
          "split 13 -> 8+5");
    CHECK(val(&w) == 15, "split conserves");
    CHECK(swarm_denom_split(&w, 5) == SWARM_ERR_ARG, "split without a piece fails");
    CHECK(swarm_denom_split(&w, 64) == SWARM_ERR_ARG, "split tier 64 fails");
    w.count[63] = 1;
    CHECK(swarm_denom_split(&w, 63) == SWARM_OK && w.count[62] == 1 && w.count[61] == 1,
          "split the top tier");
    CHECK(swarm_denom_split(0, 2) == SWARM_ERR_ARG, "split NULL");

    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.count[1] = 1;
    w.count[0] = UINT64_MAX - 1;
    CHECK(swarm_denom_split(&w, 1) == SWARM_ERR_ARG, "split 2 into a nearly full count fails");
    CHECK(w.count[1] == 1 && w.count[0] == UINT64_MAX - 1, "and changes nothing");
    w.count[0] = UINT64_MAX - 2;
    CHECK(swarm_denom_split(&w, 1) == SWARM_OK && w.count[0] == UINT64_MAX, "split 2 just fits");
    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.count[4] = 1;
    w.count[3] = UINT64_MAX;
    CHECK(swarm_denom_split(&w, 4) == SWARM_ERR_ARG, "split into full k-1 fails");
    w.count[3] = 0;
    w.count[2] = UINT64_MAX;
    CHECK(swarm_denom_split(&w, 4) == SWARM_ERR_ARG, "split into full k-2 fails");
    CHECK(w.count[4] == 1 && w.count[3] == 0, "and changes nothing");

    /* compress = canonical, unchanged value */
    swarm_denom_init(&w, 1, SWARM_OCTANE_MAX);
    w.count[0] = 100;
    CHECK(swarm_denom_compress(&w) == SWARM_OK, "compress");
    CHECK(counts_are(&w, (const uint64_t[]){0, 0, 1, 0, 1, 0, 0, 0, 0, 1}, 10),
          "100 plain tokens compress to 89+8+3");
    w.count[0] = UINT64_MAX;
    CHECK(swarm_denom_compress(&w) == SWARM_ERR_ARG, "overflowing wallet will not compress");
    CHECK(w.count[0] == UINT64_MAX, "and is left as is");
}

static void test_octane(void)
{
    swarm_wallet_t w;
    swarm_denom_init(&w, 1, SWARM_OCTANE_REGULAR);
    swarm_denom_set(&w, 100);
    CHECK(swarm_denom_set_octane(&w, SWARM_OCTANE_MAX) == SWARM_OK, "up to racing fuel");
    CHECK(w.octane == SWARM_OCTANE_MAX && val(&w) == 100 && swarm_denom_pieces(&w) == 3,
          "octane up: same value, fewer pieces");
    CHECK(swarm_denom_set_octane(&w, SWARM_OCTANE_PLAIN) == SWARM_OK, "down to plain");
    CHECK(w.count[0] == 100 && swarm_denom_pieces(&w) == 100, "octane down: 100 plain tokens");
    CHECK(swarm_denom_set_octane(&w, 64) == SWARM_ERR_ARG, "octane 64 rejected");
    CHECK(w.octane == SWARM_OCTANE_PLAIN && w.count[0] == 100, "and changes nothing");
    w.count[1] = UINT64_MAX;
    CHECK(swarm_denom_set_octane(&w, 5) == SWARM_ERR_ARG, "overflowing wallet keeps its octane");
    CHECK(w.octane == SWARM_OCTANE_PLAIN, "octane kept");
    CHECK(swarm_denom_set_octane(0, 1) == SWARM_ERR_ARG, "NULL");
}

static void test_spend_transfer(void)
{
    swarm_wallet_t a, b;
    swarm_denom_init(&a, 1, SWARM_OCTANE_MAX);
    swarm_denom_set(&a, 100);
    CHECK(swarm_denom_spend(&a, 11) == SWARM_OK, "spend 11");
    CHECK(counts_are(&a, (const uint64_t[]){0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 10),
          "change 89 is one token");
    CHECK(swarm_denom_spend(&a, 90) == SWARM_ERR_ARG, "overspend fails");
    CHECK(val(&a) == 89, "and changes nothing");
    CHECK(swarm_denom_spend(&a, 89) == SWARM_OK && val(&a) == 0, "spend exactly all");
    CHECK(swarm_denom_spend(0, 0) == SWARM_ERR_ARG, "spend NULL");

    swarm_denom_init(&a, 1, SWARM_OCTANE_MAX);
    swarm_denom_init(&b, 2, SWARM_OCTANE_PLAIN);
    swarm_denom_set(&a, 100);
    swarm_denom_set(&b, 5);
    CHECK(swarm_denom_transfer(&a, &b, 30) == SWARM_OK, "transfer 30");
    /* 70 = 55 + 13 + 2 at the payer's octane; 35 plain at the receiver's */
    CHECK(counts_are(&a, (const uint64_t[]){0, 1, 0, 0, 0, 1, 0, 0, 1}, 9), "payer change 55+13+2");
    CHECK(b.count[0] == 35 && swarm_denom_pieces(&b) == 35, "receiver holds plain 35");
    CHECK(swarm_denom_transfer(&a, &b, 71) == SWARM_ERR_ARG, "transfer more than held fails");
    CHECK(val(&a) == 70 && val(&b) == 35, "and changes neither");
    CHECK(swarm_denom_transfer(&a, &b, 70) == SWARM_OK && val(&a) == 0 && val(&b) == 105,
          "transfer everything");
    CHECK(swarm_denom_transfer(&b, &b, 105) == SWARM_OK && val(&b) == 105, "self transfer no-op");
    CHECK(swarm_denom_transfer(&b, &b, 106) == SWARM_ERR_ARG, "self overspend still fails");

    swarm_denom_init(&a, 1, SWARM_OCTANE_MAX);
    swarm_denom_init(&b, 2, SWARM_OCTANE_MAX);
    swarm_denom_set(&a, 10);
    swarm_denom_set(&b, UINT64_MAX - 9);
    CHECK(swarm_denom_transfer(&a, &b, 10) == SWARM_ERR_ARG, "receiver overflow fails");
    CHECK(val(&a) == 10 && val(&b) == UINT64_MAX - 9, "and changes neither");
    CHECK(swarm_denom_transfer(&a, &b, 9) == SWARM_OK && val(&b) == UINT64_MAX, "fills to the top");
    b.count[0] = UINT64_MAX;
    CHECK(swarm_denom_transfer(&a, &b, 1) == SWARM_ERR_ARG, "overflowing receiver refused");
    CHECK(swarm_denom_transfer(&b, &a, 0) == SWARM_ERR_ARG, "overflowing payer refused");
    CHECK(swarm_denom_transfer(0, &a, 0) == SWARM_ERR_ARG, "NULL payer");
    CHECK(swarm_denom_transfer(&a, 0, 0) == SWARM_ERR_ARG, "NULL receiver");
}

static void test_code(void)
{
    /* Fibonacci codes of n = v + 1, bits lowest first (A. Fraenkel and S. Klein) */
    static const char *want[12] = {"11",    "011",    "0011",   "1011",   "00011",  "10011",
                                   "01011", "000011", "100011", "010011", "001011", "101011"};
    uint8_t buf[32] = {0};
    uint32_t pos = 0;
    for (uint64_t v = 0; v < 12; v++) {
        uint32_t start = pos;
        CHECK(swarm_denom_code_put(buf, 256, &pos, v) == SWARM_OK, "put small");
        uint32_t len = 0;
        while (want[v][len]) len++;
        CHECK(pos - start == len, "code length");
        for (uint32_t i = 0; i < len && i < pos - start; i++)
            CHECK(((buf[(start + i) >> 3] >> ((start + i) & 7)) & 1) ==
                      (uint32_t) (want[v][i] - '0'),
                  "code bit");
    }
    uint32_t rpos = 0;
    for (uint64_t v = 0; v < 12; v++) {
        uint64_t got = 99;
        CHECK(swarm_denom_code_get(buf, pos, &rpos, &got) == SWARM_OK && got == v, "get back");
    }
    CHECK(rpos == pos, "read every bit");
    uint64_t got;
    CHECK(swarm_denom_code_get(buf, pos, &rpos, &got) == SWARM_ERR_ARG, "nothing left to read");

    uint8_t one[2] = {0, 0};
    pos = 0;
    swarm_denom_code_put(one, 16, &pos, 10);
    CHECK(one[0] == 52 && pos == 6, "v=10 -> 001011 = byte 52");
    one[0] = 0xFF;
    pos = 0;
    swarm_denom_code_put(one, 16, &pos, 0);
    CHECK(one[0] == 0xFF && pos == 2, "v=0 -> 11");
    one[0] = 0xFF;
    pos = 0;
    swarm_denom_code_put(one, 16, &pos, 1);
    CHECK((one[0] & 7) == 6 && pos == 3, "put clears a 0 bit over old data");

    pos = 1;
    CHECK(swarm_denom_code_put(one, 6, &pos, 10) == SWARM_ERR_ARG && pos == 1, "no room fails");
    pos = 0;
    CHECK(swarm_denom_code_put(one, 6, &pos, 10) == SWARM_OK && pos == 6, "exact room fits");
    pos = 7;
    CHECK(swarm_denom_code_put(one, 6, &pos, 0) == SWARM_ERR_ARG && pos == 7, "pos past cap fails");
    CHECK(swarm_denom_code_put(0, 6, &pos, 0) == SWARM_ERR_ARG, "NULL buf");
    CHECK(swarm_denom_code_put(one, 6, 0, 0) == SWARM_ERR_ARG, "NULL pos");

    pos = 0;
    swarm_denom_code_put(one, 16, &pos, 1); /* 011 */
    rpos = 0;
    CHECK(swarm_denom_code_get(one, 2, &rpos, &got) == SWARM_ERR_ARG && rpos == 0,
          "truncated code fails, pos kept");
    CHECK(swarm_denom_code_get(one, 3, &rpos, &got) == SWARM_OK && got == 1,
          "with the last bit it reads");
    CHECK(swarm_denom_code_get(0, 3, &rpos, &got) == SWARM_ERR_ARG, "get NULL buf");
    CHECK(swarm_denom_code_get(one, 3, 0, &got) == SWARM_ERR_ARG, "get NULL pos");
    CHECK(swarm_denom_code_get(one, 3, &rpos, 0) == SWARM_ERR_ARG, "get NULL v");

    /* the extremes */
    uint8_t big[16] = {0};
    pos = 0;
    CHECK(swarm_denom_code_put(big, 128, &pos, UINT64_MAX) == SWARM_ERR_ARG,
          "UINT64_MAX not codable");
    CHECK(swarm_denom_code_put(big, 128, &pos, UINT64_MAX - 1) == SWARM_OK && pos == 93,
          "largest code is 93 bits");
    rpos = 0;
    CHECK(swarm_denom_code_get(big, 128, &rpos, &got) == SWARM_OK && got == UINT64_MAX - 1 &&
              rpos == 93,
          "largest round trip");
    pos = 0;
    CHECK(swarm_denom_code_put(big, 128, &pos, 12200160415121876737ULL) == SWARM_OK && pos == 93,
          "F(93) - 1 needs 93 bits");
    pos = 0;
    CHECK(swarm_denom_code_put(big, 128, &pos, 12200160415121876736ULL) == SWARM_OK && pos == 92,
          "F(93) - 2 needs 92 bits");

    /* F(89) + F(91) + F(93) overflows 64 bits */
    uint8_t bad[16] = {0};
    bad[87 >> 3] |= (uint8_t) (1u << (87 & 7));
    bad[89 >> 3] |= (uint8_t) (1u << (89 & 7));
    bad[91 >> 3] |= (uint8_t) (1u << (91 & 7));
    bad[92 >> 3] |= (uint8_t) (1u << (92 & 7));
    rpos = 0;
    CHECK(swarm_denom_code_get(bad, 128, &rpos, &got) == SWARM_ERR_ARG && rpos == 0,
          "overflowing code rejected");
    /* a 1 at F(94) is past 64 bits */
    uint8_t far[16] = {0};
    far[92 >> 3] |= (uint8_t) (1u << (92 & 7));
    far[93 >> 3] |= (uint8_t) (1u << (93 & 7));
    rpos = 0;
    CHECK(swarm_denom_code_get(far, 128, &rpos, &got) == SWARM_ERR_ARG, "F(94) bit rejected");
}

static uint64_t lcg(uint64_t *s)
{
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 11;
}

static void test_properties(void)
{
    uint64_t seed = 0x5EEDu;
    swarm_wallet_t w[4];
    for (uint32_t i = 0; i < 4; i++) swarm_denom_init(&w[i], i, (i * 17u) % SWARM_DENOM_TIERS);
    uint64_t total = 0;
    for (uint32_t i = 0; i < 4; i++) {
        uint64_t v = lcg(&seed) >> (lcg(&seed) % 50);
        swarm_denom_set(&w[i], v);
        total += v;
    }
    int bad_canon = 0, bad_sum = 0, bad_code = 0, bad_op = 0;
    for (uint32_t it = 0; it < 4000; it++) {
        uint32_t i = (uint32_t) (lcg(&seed) % 4), j = (uint32_t) (lcg(&seed) % 4);
        uint64_t vi = val(&w[i]);
        switch (lcg(&seed) % 4) {
        case 0:
            swarm_denom_transfer(&w[i], &w[j], vi ? lcg(&seed) % (vi + 1) : 0);
            break;
        case 1:
            swarm_denom_set_octane(&w[i], (uint32_t) (lcg(&seed) % SWARM_DENOM_TIERS));
            break;
        case 2: {
            uint32_t k = (uint32_t) (lcg(&seed) % SWARM_DENOM_TIERS);
            if (swarm_denom_split(&w[i], k) == SWARM_OK && val(&w[i]) != vi) bad_op++;
            if (swarm_denom_compress(&w[i]) != SWARM_OK) bad_op++;
            break;
        }
        default: {
            uint32_t k = (uint32_t) (lcg(&seed) % SWARM_DENOM_TIERS);
            swarm_denom_split(&w[i], k + 2);
            swarm_denom_split(&w[i], k + 1);
            if (swarm_denom_fuse(&w[i], k) == SWARM_OK && val(&w[i]) != vi) bad_op++;
            if (swarm_denom_compress(&w[i]) != SWARM_OK) bad_op++;
        }
        }
        uint64_t sum = 0;
        for (uint32_t m = 0; m < 4; m++) {
            if (!swarm_denom_is_canonical(&w[m])) bad_canon++;
            sum += val(&w[m]);
        }
        if (sum != total) bad_sum++;
        uint8_t buf[16];
        uint32_t p = 0, r = 0;
        uint64_t back = 0, v = val(&w[i]);
        if (swarm_denom_code_put(buf, 128, &p, v) != SWARM_OK ||
            swarm_denom_code_get(buf, 128, &r, &back) != SWARM_OK || back != v || r != p)
            bad_code++;
    }
    CHECK(bad_canon == 0, "every wallet stays canonical");
    CHECK(bad_sum == 0, "total value conserved across 4000 random operations");
    CHECK(bad_code == 0, "every value round-trips through its code");
    CHECK(bad_op == 0, "every fuse and split conserves");
}

static void test_budget_lockstep(void)
{
    swarm_budget_t b;
    swarm_wallet_t w[3];
    swarm_budget_init(&b, 2, 100);
    swarm_budget_register(&b, 7, 0);
    swarm_budget_register(&b, 8, 1);
    swarm_budget_register(&b, 9, 1);
    swarm_denom_init(&w[0], 0, SWARM_OCTANE_MAX);
    swarm_denom_init(&w[1], 0, SWARM_OCTANE_PLAIN);
    swarm_denom_init(&w[2], 0, SWARM_OCTANE_REGULAR);
    CHECK(swarm_denom_payout(&b, w, 3) == SWARM_ERR_NO_CYCLE, "payout needs an open cycle");
    swarm_budget_begin_cycle(&b);
    CHECK(swarm_denom_payout(&b, w, 2) == SWARM_ERR_ARG, "payout needs one wallet per slot");
    CHECK(swarm_denom_payout(0, w, 3) == SWARM_ERR_ARG, "payout NULL budget");
    CHECK(swarm_denom_payout(&b, 0, 3) == SWARM_ERR_ARG, "payout NULL wallets");
    w[2].octane = 64;
    CHECK(swarm_denom_payout(&b, w, 3) == SWARM_ERR_ARG, "payout bad octane");
    CHECK(w[0].model_id == 0, "a refused payout writes nothing");
    w[2].octane = SWARM_OCTANE_REGULAR;
    w[0].octane = 64;
    CHECK(swarm_denom_payout(&b, w, 3) == SWARM_ERR_ARG, "payout bad octane in the first wallet");
    w[0].octane = SWARM_OCTANE_MAX;
    CHECK(swarm_denom_payout(&b, w, 3) == SWARM_OK, "payout");
    /* weights 2:1 -> 67 / 33; level 1 split 17 / 16 */
    CHECK(w[0].model_id == 7 && w[1].model_id == 8 && w[2].model_id == 9, "wallet ids from slots");
    CHECK(counts_are(&w[0], (const uint64_t[]){1, 0, 1, 0, 1, 0, 0, 0, 1}, 9), "67 = 55+8+3+1");
    CHECK(w[1].count[0] == 17 && swarm_denom_pieces(&w[1]) == 17, "plain 17");
    CHECK(counts_are(&w[2], (const uint64_t[]){0, 0, 0, 0, 2}, 5), "REGULAR 16 = 2 x 8");
    CHECK(swarm_denom_reconciles(&b, w, 3), "reconciles after payout");

    uint64_t g = 0;
    CHECK(swarm_denom_consume(&b, w, 3, 7, 10, &g) == SWARM_OK && g == 10, "consume 10");
    CHECK(counts_are(&w[0], (const uint64_t[]){0, 1, 0, 0, 0, 0, 0, 0, 1}, 9), "57 = 55+2");
    CHECK(swarm_budget_remaining(&b, 7) == 57, "budget moved with the wallet");
    CHECK(swarm_denom_consume(&b, w, 3, 8, 100, &g) == SWARM_OK && g == 17, "consume capped");
    CHECK(val(&w[1]) == 0, "wallet emptied");
    CHECK(swarm_denom_reconciles(&b, w, 3), "reconciles after consume");
    CHECK(swarm_denom_payout(&b, w, 3) == SWARM_OK && val(&w[0]) == 57 && val(&w[1]) == 0 &&
              val(&w[2]) == 16,
          "a second payout pays only what is left");

    /* D6: going up to racing fuel changes no allotment */
    swarm_denom_set_octane(&w[2], SWARM_OCTANE_RACING);
    CHECK(swarm_budget_remaining(&b, 9) == 16 && swarm_denom_reconciles(&b, w, 3),
          "octane change buys no tokens");

    g = 5;
    CHECK(swarm_denom_consume(&b, w, 3, 99, 1, &g) == SWARM_ERR_NO_MODEL && g == 0,
          "unknown model");
    CHECK(swarm_denom_consume(&b, w, 2, 7, 1, &g) == SWARM_ERR_ARG, "consume wrong n");
    CHECK(swarm_denom_consume(&b, w, 3, 7, 1, 0) == SWARM_ERR_ARG, "consume NULL granted");
    CHECK(swarm_denom_consume(0, w, 3, 7, 1, &g) == SWARM_ERR_ARG, "consume NULL budget");
    CHECK(swarm_denom_consume(&b, 0, 3, 7, 1, &g) == SWARM_ERR_ARG, "consume NULL wallets");

    /* a tampered wallet is caught and stops the budget moving */
    w[0].count[0] += 1;
    CHECK(!swarm_denom_reconciles(&b, w, 3), "tampered wallet does not reconcile");
    CHECK(swarm_denom_consume(&b, w, 3, 7, 1, &g) == SWARM_ERR_ARG && g == 0, "consume refuses");
    CHECK(swarm_budget_remaining(&b, 7) == 57, "budget untouched");
    w[0].count[0] -= 1;
    w[0].count[0] = UINT64_MAX;
    w[0].count[1] = 1;
    CHECK(swarm_denom_consume(&b, w, 3, 7, 1, &g) == SWARM_ERR_ARG, "overflowing wallet refused");
    CHECK(!swarm_denom_reconciles(&b, w, 3), "overflowing wallet does not reconcile");
    swarm_denom_set(&w[0], 57);
    CHECK(swarm_denom_reconciles(&b, w, 3), "repaired");
    w[1].model_id = 9;
    CHECK(!swarm_denom_reconciles(&b, w, 3), "wrong owner does not reconcile");
    w[1].model_id = 8;
    CHECK(!swarm_denom_reconciles(&b, w, 2), "reconcile wrong n");
    CHECK(!swarm_denom_reconciles(0, w, 3), "reconcile NULL budget");
    CHECK(!swarm_denom_reconciles(&b, 0, 3), "reconcile NULL wallets");

    CHECK(swarm_denom_end_cycle(&b, w, 2) == SWARM_ERR_ARG, "end wrong n");
    CHECK(swarm_denom_end_cycle(0, w, 3) == SWARM_ERR_ARG, "end NULL budget");
    CHECK(swarm_denom_end_cycle(&b, 0, 3) == SWARM_ERR_ARG, "end NULL wallets");
    CHECK(val(&w[0]) == 57, "refused end keeps tokens");
    CHECK(swarm_denom_end_cycle(&b, w, 3) == SWARM_OK, "end cycle");
    CHECK(val(&w[0]) == 0 && val(&w[2]) == 0, "wallets expire (R6)");
    CHECK(w[2].octane == SWARM_OCTANE_RACING, "octane kept for the next cycle");
    CHECK(b.last_unused == 57 + 16, "budget recorded the expiry");
    CHECK(swarm_denom_end_cycle(&b, w, 3) == SWARM_ERR_NO_CYCLE, "no cycle to end");
    CHECK(swarm_denom_consume(&b, w, 3, 7, 1, &g) == SWARM_ERR_NO_CYCLE && g == 0,
          "consume outside a cycle");
    w[1].count[0] = 3;
    CHECK(swarm_denom_end_cycle(&b, w, 3) == SWARM_ERR_NO_CYCLE && w[1].count[0] == 3,
          "refused end clears nothing");
}

int main(void)
{
    printf("=== swarm_denom: token denominations and octane ===\n");
    test_faces();
    test_init_value_pieces();
    test_canonical();
    test_wrap_unwrap();
    test_octane();
    test_spend_transfer();
    test_code();
    test_properties();
    test_budget_lockstep();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all swarm_denom tests passed\n");
    return 0;
}
