/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_chg_name.c — naming the chiglet (chg_name.h N1-N7). */
#include <stdio.h>
#include <string.h>
#include "chg_name.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m);                                      \
        }                                                                                          \
    } while (0)

static uint8_t SALT_A[CHGN_SALT], SALT_B[CHGN_SALT];

static int chk(const char *s)
{
    return chgn_check(s, strlen(s));
}

/* ---- N1 N2 ---- */
static void test_rules(void)
{
    static const char *const good[] = {
        "Pip",      "Lumi",    "Ada Lovelace",
        "Jean-Luc", "O'Neill", "R2",
        "Zoë",      "Ωμέγα",   "さくら",
        "Kai 9",    "x",       "abcdefghijklmnopqrstuvwxyzabcdef",
        "Pet Rock", "Petra",   "Siri-us",
        "Rooty",    "Xiri",    "Rlexa",
    };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++)
        CHECK(chk(good[i]) == CHGN_OK, good[i]);

    static const struct {
        const char *s;
        int rc;
    } bad[] = {
        {"", CHGN_E_EMPTY},
        {"abcdefghijklmnopqrstuvwxyzabcdefg", CHGN_E_EMPTY},
        {"9lives", CHGN_E_START},
        {" Pip", CHGN_E_START},
        {"-Pip", CHGN_E_START},
        {"'Pip", CHGN_E_START},
        {"Pip ", CHGN_E_SEP},
        {"Pip-", CHGN_E_SEP},
        {"Pi  p", CHGN_E_SEP},
        {"Pi -p", CHGN_E_SEP},
        {"Pi'-p", CHGN_E_SEP},
        {"Pi_p", CHGN_E_CHAR},
        {"Pi.p", CHGN_E_CHAR},
        {"Pip!", CHGN_E_CHAR},
        {"Pi\tp", CHGN_E_CHAR},
        {"Pi\x7fp", CHGN_E_CHAR},
        {"Pi@p", CHGN_E_CHAR},
        {"Pi/p", CHGN_E_CHAR},
        {"Pi:p", CHGN_E_CHAR},
        {"Pi[p", CHGN_E_CHAR},
        {"Pi{p", CHGN_E_CHAR},
        {"Pi`p", CHGN_E_CHAR},
        {"\xff", CHGN_E_UTF8},
        {"A\xc3", CHGN_E_UTF8},
        {"A\xc0\x80", CHGN_E_UTF8},         /* overlong NUL */
        {"A\xc1\xbf", CHGN_E_UTF8},         /* overlong */
        {"A\xe0\x80\xaf", CHGN_E_UTF8},     /* overlong 3-byte */
        {"A\xf0\x80\x80\xaf", CHGN_E_UTF8}, /* overlong 4-byte */
        {"A\xed\xa0\x80", CHGN_E_UTF8},     /* surrogate */
        {"A\xf4\x90\x80\x80", CHGN_E_UTF8}, /* past U+10FFFF */
        {"A\xf5\x80\x80\x80", CHGN_E_UTF8},
        {"A\x80", CHGN_E_UTF8},
        {"A\xe2\x82", CHGN_E_UTF8},     /* cut short */
        {"A\xe2\x28\xa1", CHGN_E_UTF8}, /* bad continuation */
        {"A\xf0\x9f\x98", CHGN_E_UTF8},
        {"A\xe0\x9f\xbf", CHGN_E_UTF8},     /* U+07FF in three bytes */
        {"A\xf0\x8f\xbf\xbf", CHGN_E_UTF8}, /* U+FFFF in four bytes */
        {"A\xed\xbf\xbf", CHGN_E_UTF8},     /* U+DFFF, the last surrogate */
        {"A\xf8\x90\x80\x80", CHGN_E_UTF8}, /* no five-byte leads */
        {"A\x80\x90\x80\x80", CHGN_E_UTF8}, /* a continuation is not a lead */
        {"A\xc3"
         "A",
         CHGN_E_UTF8},               /* a lead with no continuation */
        {"9\xc3\xa9", CHGN_E_START}, /* a digit before a multi-byte letter */
        {"\x7fPip", CHGN_E_START},
        {"Chiglet", CHGN_E_RESERVED},
        {"CHIGLET", CHGN_E_RESERVED},
        {"Mega-Man", CHGN_E_RESERVED},
        {"mega man", CHGN_E_RESERVED},
        {"Net Navi", CHGN_E_RESERVED},
        {"PET", CHGN_E_RESERVED},
        {"Siri", CHGN_E_RESERVED},
        {"Alexa", CHGN_E_RESERVED},
        {"Cortana", CHGN_E_RESERVED},
        {"Bixby", CHGN_E_RESERVED},
        {"Copilot", CHGN_E_RESERVED},
        {"Gemini", CHGN_E_RESERVED},
        {"Chat GPT", CHGN_E_RESERVED},
        {"Claude", CHGN_E_RESERVED},
        {"Tiamat", CHGN_E_RESERVED},
        {"ZXV", CHGN_E_RESERVED},
        {"root", CHGN_E_RESERVED},
        {"Admin", CHGN_E_RESERVED},
        {"System", CHGN_E_RESERVED},
        {"Kernel", CHGN_E_RESERVED},
        {"Owner", CHGN_E_RESERVED},
        {"Guest", CHGN_E_RESERVED},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int rc = chk(bad[i].s);
        CHECK(rc == bad[i].rc, bad[i].s);
        if (rc != bad[i].rc) printf("  got %d want %d\n", rc, bad[i].rc);
    }
    CHECK(chgn_check("Pip\0x", 5) == CHGN_E_CHAR, "N1 an embedded NUL is refused");
    CHECK(chgn_check("Pipx", 3) == CHGN_OK, "N1 the length bounds the name");
    CHECK(chgn_check(NULL, 3) == CHGN_E_ARG, "N1 null");
    CHECK(chgn_check("\xe2\x82\xac", 3) == CHGN_OK, "N1 a multi-byte first letter");
    CHECK(chgn_check("A\xf4\x8f\xbf\xbf", 5) == CHGN_OK, "N1 U+10FFFF is the last allowed");
    CHECK(chgn_check("A\xed\x9f\xbf", 4) == CHGN_OK, "N1 U+D7FF is allowed");
    CHECK(chgn_check("A\xee\x80\x80", 4) == CHGN_OK, "N1 U+E000 is allowed");
    CHECK(chgn_check("A\xc2\xa0", 3) == CHGN_OK, "N1 U+00A0 is the first 2-byte form");
    CHECK(chgn_check("A\xe0\xa0\x80", 4) == CHGN_OK, "N1 U+0800 is the first 3-byte form");
    CHECK(chgn_check("A\xf0\x90\x80\x80", 5) == CHGN_OK, "N1 U+10000 is the first 4-byte form");
    CHECK(chgn_check("A\xf1\x80\x80\x80", 5) == CHGN_OK, "N1 U+40000 (lead F1) is allowed");
    CHECK(chgn_check("A\xe2\x82\xac", 3) == CHGN_E_UTF8, "N1 a character cut by the length");
    CHECK(chgn_check("Pip\xff", 3) == CHGN_OK, "N1 nothing past the length is read");
    CHECK(chgn_check("A\xdf\xbf", 3) == CHGN_OK && chgn_check("A\xef\xbf\xbf", 4) == CHGN_OK,
          "N1 the last 2- and 3-byte forms");
    CHECK(chgn_check("09", 2) == CHGN_E_START && chgn_check("A0Z9", 4) == CHGN_OK,
          "N1 digits after the first letter");
    CHECK(chgn_check("Az", 2) == CHGN_OK && chgn_check("aZ", 2) == CHGN_OK, "N1 letter range");

    for (int c = CHGN_OK; c >= CHGN_E_RECORD; c--)
        CHECK(strcmp(chgn_reason(c), "unknown") != 0, "every code has a reason");
    CHECK(strcmp(chgn_reason(1), "unknown") == 0 && strcmp(chgn_reason(-11), "unknown") == 0,
          "out-of-range codes");
    CHECK(strcmp(chgn_reason(CHGN_E_LOCKED),
                 "this chiglet already has its name; a new name means a new chiglet") == 0,
          "the locked reason says how to get a new name");
}

static uint64_t want_seed(const uint8_t *salt, const char *name)
{
    sha256_ctx_t c;
    uint8_t h[32];
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/chiglet/name/v1", 19);
    sha256_update(&c, salt, CHGN_SALT);
    sha256_update(&c, (const uint8_t *) name, strlen(name));
    sha256_final(&c, h);
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t) h[i] << (8 * i);
    return v;
}

static void want_confirm(const uint8_t *salt, const char *name, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/chiglet/name/confirm/v1", 27);
    sha256_update(&c, salt, CHGN_SALT);
    sha256_update(&c, (const uint8_t *) name, strlen(name));
    sha256_final(&c, out);
}

static int preview(const chgn_t *n, const char *name, chgn_preview_t *p)
{
    return chgn_preview(n, name, strlen(name), p);
}

/* ---- N3 N4 ---- */
static void test_personality(void)
{
    chgn_t a, b;
    chgn_init(&a, SALT_A);
    chgn_init(&b, SALT_B);
    chgn_preview_t p, q;
    CHECK(preview(&a, "Lumi", &p) == CHGN_OK && p.seed == want_seed(SALT_A, "Lumi"), "N3 seed");
    CHECK(preview(&b, "Lumi", &q) == CHGN_OK && q.seed != p.seed,
          "N3 the same name for two people is two chiglets");
    CHECK(preview(&a, "lumi", &q) == CHGN_OK && q.seed != p.seed, "N3 the spelling counts");

    /* Lumi: 4 letters, all soft (l u m i), none hard */
    swarm_dna_t base = swarm_dna_from_seed(p.seed);
    CHECK(p.dna.seed == p.seed && p.dna.generation == 0 && p.dna.parent_a == 0 &&
              p.dna.parent_b == 0,
          "N4 a founder");
    CHECK(p.dna.gene[SWARM_GENE_WARMTH] == (base.gene[SWARM_GENE_WARMTH] + 255u) / 2u,
          "N4 soft sounds read warm");
    CHECK(p.dna.gene[SWARM_GENE_TEMPERAMENT] == base.gene[SWARM_GENE_TEMPERAMENT] / 2u,
          "N4 no hard sounds: calm");
    CHECK(p.dna.gene[SWARM_GENE_PACE] == (base.gene[SWARM_GENE_PACE] + 12u * 255u / 15u) / 2u,
          "N4 a short name is quick");
    static const uint8_t KEPT[] = {SWARM_GENE_EMOTION, SWARM_GENE_CURIOSITY, SWARM_GENE_CAUTION,
                                   SWARM_GENE_VERBOSITY, SWARM_GENE_SPECIALTY};
    for (size_t i = 0; i < sizeof KEPT; i++)
        CHECK(p.dna.gene[KEPT[i]] == base.gene[KEPT[i]], "N4 the other genes are the seed's");

    /* Tak-Kaz: 6 letters; soft a, a (2); hard t, k, k, z (4) */
    CHECK(preview(&a, "Tak-Kaz", &p) == CHGN_OK, "Tak-Kaz");
    base = swarm_dna_from_seed(p.seed);
    CHECK(p.dna.gene[SWARM_GENE_WARMTH] == (base.gene[SWARM_GENE_WARMTH] + 2u * 255u / 6u) / 2u &&
              p.dna.gene[SWARM_GENE_TEMPERAMENT] ==
                  (base.gene[SWARM_GENE_TEMPERAMENT] + 4u * 255u / 6u) / 2u &&
              p.dna.gene[SWARM_GENE_PACE] == (base.gene[SWARM_GENE_PACE] + 10u * 255u / 15u) / 2u,
          "N4 hard sounds read lively; separators are not letters");
    /* 16 letters and more: slowest */
    CHECK(preview(&a, "Abcdefghijklmnop", &p) == CHGN_OK, "16 letters");
    base = swarm_dna_from_seed(p.seed);
    CHECK(p.dna.gene[SWARM_GENE_PACE] == base.gene[SWARM_GENE_PACE] / 2u, "N4 sixteen: slowest");
    CHECK(preview(&a, "Abcdefghijklmno", &p) == CHGN_OK, "15 letters");
    base = swarm_dna_from_seed(p.seed);
    CHECK(p.dna.gene[SWARM_GENE_PACE] == (base.gene[SWARM_GENE_PACE] + 17u) / 2u,
          "N4 fifteen: one step quicker");
    /* one letter: the quickest; every letter of the alphabet */
    for (char c = 'a'; c <= 'z'; c++) {
        char one[2] = {c, 0};
        CHECK(preview(&a, one, &p) == CHGN_OK, "one letter");
        base = swarm_dna_from_seed(p.seed);
        CHECK(p.dna.gene[SWARM_GENE_PACE] == (base.gene[SWARM_GENE_PACE] + 255u) / 2u,
              "N4 one letter: quickest");
    }
    /* the length bounds what is scored */
    CHECK(preview(&a, "Lumi", &p) == CHGN_OK && chgn_preview(&a, "Lumix", 4, &q) == CHGN_OK &&
              memcmp(&p, &q, sizeof p) == 0,
          "N4 nothing past the length is scored");
    /* digits and capitals: R2D2 has 2 letters r (soft), d (hard) */
    CHECK(preview(&a, "R2D2", &p) == CHGN_OK, "R2D2");
    base = swarm_dna_from_seed(p.seed);
    CHECK(p.dna.gene[SWARM_GENE_WARMTH] == (base.gene[SWARM_GENE_WARMTH] + 127u) / 2u &&
              p.dna.gene[SWARM_GENE_TEMPERAMENT] ==
                  (base.gene[SWARM_GENE_TEMPERAMENT] + 127u) / 2u &&
              p.dna.gene[SWARM_GENE_PACE] == (base.gene[SWARM_GENE_PACE] + 14u * 255u / 15u) / 2u,
          "N4 capitals count, digits do not");
    /* a name with no ASCII letters keeps the seed's genes */
    CHECK(preview(&a, "さくら", &p) == CHGN_OK, "kana");
    base = swarm_dna_from_seed(p.seed);
    CHECK(memcmp(p.dna.gene, base.gene, SWARM_DNA_GENES) == 0, "N4 unscored scripts: seed genes");
    /* every soft and hard letter class */
    CHECK(preview(&a, "Aeioulmnrwy", &p) == CHGN_OK, "all soft");
    base = swarm_dna_from_seed(p.seed);
    CHECK(p.dna.gene[SWARM_GENE_WARMTH] == (base.gene[SWARM_GENE_WARMTH] + 255u) / 2u &&
              p.dna.gene[SWARM_GENE_TEMPERAMENT] == base.gene[SWARM_GENE_TEMPERAMENT] / 2u,
          "N4 every soft letter");
    CHECK(preview(&a, "Bdgkpqtxz", &p) == CHGN_OK, "all hard");
    base = swarm_dna_from_seed(p.seed);
    CHECK(p.dna.gene[SWARM_GENE_WARMTH] == base.gene[SWARM_GENE_WARMTH] / 2u &&
              p.dna.gene[SWARM_GENE_TEMPERAMENT] == (base.gene[SWARM_GENE_TEMPERAMENT] + 255u) / 2u,
          "N4 every hard letter");
    CHECK(preview(&a, "Cfhjsv", &p) == CHGN_OK, "neither");
    base = swarm_dna_from_seed(p.seed);
    CHECK(p.dna.gene[SWARM_GENE_WARMTH] == base.gene[SWARM_GENE_WARMTH] / 2u &&
              p.dna.gene[SWARM_GENE_TEMPERAMENT] == base.gene[SWARM_GENE_TEMPERAMENT] / 2u,
          "N4 letters that are neither soft nor hard");
}

/* ---- N5 ---- */
static void test_lock(void)
{
    chgn_t n;
    chgn_init(&n, SALT_A);
    swarm_dna_t d;
    CHECK(!n.locked && n.len == 0 && n.name[0] == 0 && !chgn_dna(&n, &d), "N5 not named yet");
    chgn_preview_t p;
    CHECK(preview(&n, "Pip", &p) == CHGN_OK && !n.locked, "N5 a preview keeps nothing");
    uint8_t want[32];
    want_confirm(SALT_A, "Pip", want);
    CHECK(memcmp(p.confirm, want, 32) == 0, "N5 confirm digest");
    CHECK(preview(&n, "Chiglet", &p) == CHGN_E_RESERVED, "N5 a preview checks the rules");

    chgn_preview_t other;
    preview(&n, "Pipa", &other);
    CHECK(chgn_confirm(&n, "Pip", 3, other.confirm) == CHGN_E_DIGEST && !n.locked,
          "N5 the digest of another name does not lock this one");
    uint8_t bad[32];
    memcpy(bad, want, 32);
    bad[31] ^= 1;
    CHECK(chgn_confirm(&n, "Pip", 3, bad) == CHGN_E_DIGEST, "N5 every digest byte counts");
    bad[31] ^= 1;
    bad[0] ^= 0x80;
    CHECK(chgn_confirm(&n, "Pip", 3, bad) == CHGN_E_DIGEST, "N5 the first byte too");
    CHECK(chgn_confirm(&n, "Pip ", 4, want) == CHGN_E_SEP, "N5 confirm checks the rules");
    CHECK(chgn_confirm(NULL, "Pip", 3, want) == CHGN_E_ARG &&
              chgn_confirm(&n, NULL, 3, want) == CHGN_E_ARG &&
              chgn_confirm(&n, "Pip", 3, NULL) == CHGN_E_ARG && !n.locked,
          "N5 null");
    CHECK(chgn_confirm(&n, "Pip", 3, want) == CHGN_OK && n.locked && n.len == 3 &&
              strcmp(n.name, "Pip") == 0 && n.seed == want_seed(SALT_A, "Pip"),
          "N5 confirmed and locked");
    CHECK(chgn_dna(&n, &d) && d.seed == n.seed && memcmp(d.gene, p.dna.gene, 8) == 0,
          "N5 the personality of the name");
    chgn_preview_t again;
    {
        chgn_t fresh;
        chgn_init(&fresh, SALT_A);
        preview(&fresh, "Pip", &again);
        CHECK(memcmp(d.gene, again.dna.gene, 8) == 0 && d.seed == again.seed,
              "N5 the named chiglet is the one the preview showed");
    }
    preview(&n, "Pip", &p);
    CHECK(chgn_confirm(&n, "Pipa", 4, other.confirm) == CHGN_E_LOCKED && strcmp(n.name, "Pip") == 0,
          "N5 the name can never change");
    CHECK(chgn_confirm(&n, "Pip", 3, want) == CHGN_E_LOCKED, "N5 not even to itself");
    CHECK(preview(&n, "Pipa", &p) == CHGN_E_LOCKED, "N5 no previews once named");
    CHECK(chgn_preview(NULL, "Pip", 3, &p) == CHGN_E_ARG &&
              chgn_preview(&n, NULL, 3, &p) == CHGN_E_ARG &&
              chgn_preview(&n, "Pip", 3, NULL) == CHGN_E_ARG,
          "N5 preview null");
    CHECK(!chgn_dna(NULL, &d) && !chgn_dna(&n, NULL), "chgn_dna null");

    /* a 32-byte name fills the buffer and stays terminated */
    chgn_t m;
    chgn_init(&m, SALT_B);
    const char *full = "abcdefghijklmnopqrstuvwxyzabcdef";
    preview(&m, full, &p);
    CHECK(chgn_confirm(&m, full, 32, p.confirm) == CHGN_OK && m.len == 32 && m.name[32] == 0 &&
              strcmp(m.name, full) == 0,
          "N5 a full-length name");
    /* init with no salt is all zeros; init wipes a used record */
    chgn_init(&m, NULL);
    uint8_t zero[CHGN_SALT] = {0};
    CHECK(!m.locked && m.len == 0 && m.seed == 0 && memcmp(m.salt, zero, CHGN_SALT) == 0 &&
              m.name[0] == 0 && m.name[31] == 0 && m.name[32] == 0,
          "init wipes");
    chgn_init(NULL, SALT_A); /* no crash */
}

/* ---- N6 ---- */
static void test_addressed(void)
{
    chgn_t n;
    chgn_init(&n, SALT_A);
    size_t rest = 99;
    CHECK(!chgn_addressed(&n, "Pip, hi", 7, &rest) && rest == 99, "N6 an unnamed chiglet");
    chgn_preview_t p;
    preview(&n, "Pip", &p);
    chgn_confirm(&n, "Pip", 3, p.confirm);
    static const struct {
        const char *t;
        bool yes;
        size_t rest;
    } cases[] = {
        {"Pip, open my notes", true, 5},
        {"pip open my notes", true, 4},
        {"PIP: what time is it", true, 5},
        {"Pip!", true, 4},
        {"Pip?", true, 4},
        {"Pip.", true, 4},
        {"Pip; go", true, 5},
        {"Pip", true, 3},
        {"Pip ,  go", true, 7},
        {"Pippa, hi", false, 0},
        {"Pi", false, 0},
        {"Hey Pip", false, 0},
        {"Pop, hi", false, 0},
        {"Xip, hi", false, 0},
        {"Pip-squeak", false, 0},
        {"Pip's here", false, 0},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        rest = 99;
        bool y = chgn_addressed(&n, cases[i].t, strlen(cases[i].t), &rest);
        CHECK(y == cases[i].yes && (!y || rest == cases[i].rest), cases[i].t);
    }
    CHECK(chgn_addressed(&n, "Pip go", 6, NULL), "N6 rest may be NULL");
    CHECK(!chgn_addressed(NULL, "Pip", 3, &rest) && !chgn_addressed(&n, NULL, 3, &rest), "N6 null");
    CHECK(!chgn_addressed(&n, "Pip, hi", 2, &rest), "N6 the length bounds the text");
    CHECK(chgn_addressed(&n, "Pip, hi", 4, &rest) && rest == 4, "N6 and the separators");

    chgn_t k;
    chgn_init(&k, SALT_B);
    preview(&k, "Zoë", &p);
    chgn_confirm(&k, "Zoë", strlen("Zoë"), p.confirm);
    CHECK(chgn_addressed(&k, "zoë, hi", strlen("zoë, hi"), &rest) && rest == 6,
          "N6 a multi-byte name");
    CHECK(!chgn_addressed(&k, "Zoe, hi", 7, &rest), "N6 other bytes compare exactly");
}

/* ---- N7 ---- */
static void test_record(void)
{
    chgn_t n, m;
    uint8_t r[CHGN_RECORD];
    CHECK(CHGN_RECORD == 79, "N7 record size");

    chgn_init(&n, SALT_A);
    chgn_encode(&n, r);
    CHECK(r[0] == 'Z' && r[1] == 'X' && r[2] == 'C' && r[3] == 'N' && r[4] == 1 && r[5] == 0 &&
              r[6] == 0 && memcmp(r + 39, SALT_A, CHGN_SALT) == 0,
          "N7 layout, not named");
    uint8_t h[32];
    sha256(r, CHGN_RECORD - 8, h);
    CHECK(memcmp(r + CHGN_RECORD - 8, h, 8) == 0, "N7 checksum");
    memset(&m, 0x55, sizeof m);
    CHECK(chgn_decode(&m, r) == CHGN_OK && !m.locked && m.len == 0 &&
              memcmp(m.salt, SALT_A, CHGN_SALT) == 0 && m.name[0] == 0,
          "N7 an unnamed chiglet keeps its salt");

    chgn_preview_t p;
    preview(&n, "Ada Lovelace", &p);
    chgn_confirm(&n, "Ada Lovelace", 12, p.confirm);
    chgn_encode(&n, r);
    CHECK(r[5] == 1 && r[6] == 12 && memcmp(r + 7, "Ada Lovelace", 12) == 0 && r[19] == 0 &&
              r[38] == 0,
          "N7 layout, named");
    memset(&m, 0x55, sizeof m);
    CHECK(chgn_decode(&m, r) == CHGN_OK && m.locked && m.len == 12 &&
              strcmp(m.name, "Ada Lovelace") == 0 && m.seed == n.seed && m.name[12] == 0,
          "N7 round trip");
    swarm_dna_t d1, d2;
    CHECK(chgn_dna(&n, &d1) && chgn_dna(&m, &d2) && memcmp(&d1, &d2, sizeof d1) == 0,
          "N7 the same chiglet after a restart");

    /* every byte is covered */
    int caught = 0;
    for (size_t i = 0; i < CHGN_RECORD; i++) {
        uint8_t t[CHGN_RECORD];
        memcpy(t, r, sizeof t);
        t[i] ^= 0x01;
        if (chgn_decode(&m, t) == CHGN_E_RECORD) caught++;
    }
    CHECK(caught == (int) CHGN_RECORD, "N7 any flipped byte is refused");

    /* well-checksummed but inconsistent records */
    struct {
        size_t at;
        uint8_t v;
    } edits[] = {
        {0, 'Y'}, {3, 'M'}, {4, 2}, {5, 2}, {6, 33}, {6, 13}, {6, 0}, {7, '9'}, {20, 'x'},
    };
    for (size_t e = 0; e < sizeof edits / sizeof edits[0]; e++) {
        uint8_t t[CHGN_RECORD];
        memcpy(t, r, sizeof t);
        t[edits[e].at] = edits[e].v;
        sha256(t, CHGN_RECORD - 8, h);
        memcpy(t + CHGN_RECORD - 8, h, 8);
        CHECK(chgn_decode(&m, t) == CHGN_E_RECORD, "N7 an inconsistent record is refused");
    }
    /* a resealed reserved name, and an unlocked record that carries a name */
    {
        uint8_t t[CHGN_RECORD];
        memcpy(t, r, sizeof t);
        memset(t + 7, 0, CHGN_NAME_MAX);
        memcpy(t + 7, "Siri", 4);
        t[6] = 4;
        sha256(t, CHGN_RECORD - 8, h);
        memcpy(t + CHGN_RECORD - 8, h, 8);
        CHECK(chgn_decode(&m, t) == CHGN_E_RECORD, "N7 a reserved name never loads");
        t[5] = 0;
        sha256(t, CHGN_RECORD - 8, h);
        memcpy(t + CHGN_RECORD - 8, h, 8);
        CHECK(chgn_decode(&m, t) == CHGN_E_RECORD, "N7 an unnamed record holds no name");
    }
    /* an unnamed record with a bad locked flag */
    {
        chgn_t u;
        uint8_t t[CHGN_RECORD];
        chgn_init(&u, SALT_A);
        chgn_encode(&u, t);
        t[5] = 2;
        sha256(t, CHGN_RECORD - 8, h);
        memcpy(t + CHGN_RECORD - 8, h, 8);
        CHECK(chgn_decode(&m, t) == CHGN_E_RECORD, "N7 the locked flag is 0 or 1");
    }
    /* a full-length name round-trips */
    chgn_init(&n, SALT_B);
    const char *full = "abcdefghijklmnopqrstuvwxyzabcdef";
    preview(&n, full, &p);
    chgn_confirm(&n, full, 32, p.confirm);
    chgn_encode(&n, r);
    CHECK(chgn_decode(&m, r) == CHGN_OK && m.len == 32 && strcmp(m.name, full) == 0,
          "N7 a full-length name");
    CHECK(chgn_decode(NULL, r) == CHGN_E_ARG && chgn_decode(&m, NULL) == CHGN_E_ARG, "N7 null");
}

int main(void)
{
    for (int i = 0; i < (int) CHGN_SALT; i++) {
        SALT_A[i] = (uint8_t) (i * 7 + 1);
        SALT_B[i] = (uint8_t) (i * 13 + 5);
    }
    test_rules();
    test_personality();
    test_lock();
    test_addressed();
    test_record();
    printf("%d/%d checks passed\n", pass, pass + fail);
    return fail ? 1 : 0;
}
