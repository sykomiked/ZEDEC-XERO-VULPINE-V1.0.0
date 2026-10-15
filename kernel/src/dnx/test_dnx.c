/* test_dnx.c — duplex digital DNA tests.
 *
 * The headline checks are the two-way ones: a duplex strand read start to
 * finish and finish to start gives the same verified payload; damage on one
 * side leaves one read TRUE and the other FALSE, which is held as BOTH; and a
 * codon program read backwards (complemented) undoes itself exactly.
 *
 * Build/run: make -C kernel test-dnx
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "dnx.h"

static int failures = 0;
static int passes = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  [FAIL] %s (line %d)\n", msg, __LINE__);                                      \
            failures++;                                                                            \
        } else {                                                                                   \
            passes++;                                                                              \
        }                                                                                          \
    } while (0)

static uint32_t rng = 0x36A9u;
static uint32_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

#define BIG 4096
static uint8_t strand[BIG], strand2[BIG], bases[BIG], out1[BIG], out2[BIG];

static int text_is(const uint8_t *b, uint32_t n, bool rna, const char *want)
{
    char t[64];
    if (n >= sizeof t || dnx_to_text(b, n, rna, t, n) != (int32_t) n) return 0;
    t[n] = 0;
    return strcmp(t, want) == 0;
}

static void test_conversion(void)
{
    uint8_t e3 = 0xE3;
    CHECK(dnx_bytes_to_bases(&e3, 1, bases, 4) == 4, "0xE3 gives four bases");
    CHECK(text_is(bases, 4, false, "TGAT"), "0xE3 = 11 10 00 11 = TGAT (the plan's example)");
    CHECK(text_is(bases, 4, true, "UGAU"), "RNA form writes U for 11");

    uint8_t all[256];
    for (uint32_t i = 0; i < 256; i++) all[i] = (uint8_t) i;
    CHECK(dnx_bytes_to_bases(all, 256, bases, BIG) == 1024, "256 bytes give 1024 bases");
    memset(out1, 0xFF, 256);
    CHECK(dnx_bases_to_bytes(bases, 1024, out1, 256) == 256, "1024 bases give 256 bytes");
    CHECK(memcmp(all, out1, 256) == 0, "every byte round-trips");
    CHECK(text_is(bases, 8, false, "AAAAAAAC"), "0x00 0x01 = AAAA AAAC");

    uint8_t x[4];
    CHECK(dnx_from_text("acgu", 4, x, 4) == 4 && x[0] == DNX_A && x[1] == DNX_C && x[2] == DNX_G &&
              x[3] == DNX_T,
          "lowercase and U parse");
    CHECK(dnx_from_text("ACGX", 4, x, 4) == DNX_E_BASE, "a letter outside ACGTU is refused");
    CHECK(dnx_bases_to_bytes(bases, 6, out1, 8) == DNX_E_ARG, "bases not a multiple of 4");
    uint8_t bad[4] = {0, 1, 4, 2};
    CHECK(dnx_bases_to_bytes(bad, 4, out1, 8) == DNX_E_BASE, "code 4 is not a base");
    CHECK(dnx_to_text(bad, 4, false, (char *) out1, 8) == DNX_E_BASE, "to_text refuses code 4");

    memset(out1, 0xAA, 8);
    CHECK(dnx_bytes_to_bases(all, 2, out1, 7) == DNX_E_CAP, "short capacity refused");
    CHECK(out1[0] == 0xAA && out1[6] == 0xAA, "nothing written on DNX_E_CAP");
    CHECK(dnx_bases_to_bytes(bases, 8, out1, 1) == DNX_E_CAP, "bytes capacity checked");
    CHECK(dnx_from_text("ACGT", 4, out1, 3) == DNX_E_CAP && out1[0] == 0xAA,
          "from_text capacity checked first");
    CHECK(dnx_to_text(bases, 4, false, (char *) out1, 3) == DNX_E_CAP && out1[0] == 0xAA,
          "to_text capacity checked first");
    CHECK(dnx_bytes_to_bases(NULL, 1, bases, 4) == DNX_E_ARG, "NULL input refused");
}

static void test_revcomp(void)
{
    uint8_t s[8];
    dnx_from_text("ATGCA", 5, s, 5);
    CHECK(dnx_revcomp(s, 5, s + 0, 5) == 5, "revcomp in place, odd length");
    CHECK(text_is(s, 5, false, "TGCAT"), "revcomp(ATGCA) = TGCAT");
    dnx_from_text("AACG", 4, s, 4);
    uint8_t r[4];
    CHECK(dnx_revcomp(s, 4, r, 4) == 4 && text_is(r, 4, false, "CGTT"), "revcomp(AACG) = CGTT");
    CHECK(dnx_revcomp(r, 4, r, 4) == 4 && text_is(r, 4, false, "AACG"), "revcomp is an involution");
    CHECK(dnx_revcomp(s, 4, r, 3) == DNX_E_CAP, "revcomp capacity checked");
    for (uint8_t b = 0; b < 4; b++) {
        uint8_t one = b, c;
        dnx_revcomp(&one, 1, &c, 1);
        CHECK((b == DNX_A && c == DNX_T) || (b == DNX_T && c == DNX_A) ||
                  (b == DNX_C && c == DNX_G) || (b == DNX_G && c == DNX_C),
              "complement is Watson-Crick (A-T, C-G)");
    }
}

static void test_metrics(void)
{
    dnx_metrics_t m;
    uint8_t s[16];
    dnx_from_text("ACG", 3, s, 3);
    CHECK(dnx_metrics(s, 3, &m) == DNX_OK && m.gc_permille == 666, "GC permille floors (2/3)");
    dnx_from_text("ATGAAATAAGGGTGA", 15, s, 15);
    CHECK(dnx_metrics(s, 15, &m) == DNX_OK, "metrics run");
    CHECK(m.count[DNX_A] == 7 && m.count[DNX_C] == 0 && m.count[DNX_G] == 5 && m.count[DNX_T] == 3,
          "base counts");
    CHECK(m.longest_run == 3, "longest run AAA / GGG");
    CHECK(m.start_codons == 1 && m.stop_codons == 2, "ATG start, TAA and TGA stops in frame");
    CHECK(m.gc_permille == 333, "GC 5/15");
    CHECK(!m.revcomp_palindrome, "not its own reverse complement");
    dnx_from_text("GAATTC", 6, s, 6);
    CHECK(dnx_metrics(s, 6, &m) == DNX_OK && m.revcomp_palindrome, "EcoRI site is a palindrome");
    dnx_from_text("GAATTG", 6, s, 6);
    CHECK(dnx_metrics(s, 6, &m) == DNX_OK && !m.revcomp_palindrome, "one base off is not");
    dnx_from_text("TAG", 3, s, 3);
    CHECK(dnx_metrics(s, 3, &m) == DNX_OK && m.stop_codons == 1, "TAG is a stop");
    CHECK(dnx_metrics(s, 0, &m) == DNX_OK && m.bases == 0 && m.gc_permille == 0 &&
              m.longest_run == 0 && m.revcomp_palindrome,
          "empty strand");
    s[0] = 7;
    CHECK(dnx_metrics(s, 1, &m) == DNX_E_BASE, "metrics refuse code 7");
}

static void test_crc(void)
{
    CHECK(dnx_crc32((const uint8_t *) "123456789", 9) == 0xCBF43926u, "CRC-32 check value");
    CHECK(dnx_crc32(NULL, 0) == 0, "CRC-32 of nothing");
}

static void test_duplex(void)
{
    const char *msg = "chiglet DNA reads both ways";
    uint32_t len = (uint32_t) strlen(msg);
    int32_t n = dnx_duplex_encode((const uint8_t *) msg, len, strand, BIG);
    CHECK(n == (int32_t) DNX_DUPLEX_BASES(len), "duplex size is 2(40+4L)+6");
    dnx_metrics_t m;
    dnx_metrics(strand, (uint32_t) n, &m);
    CHECK(m.revcomp_palindrome, "a duplex strand is its own reverse complement");
    CHECK(text_is(strand, 8, false, "ATGGTCCA"), "frame opens with the magic");

    dnx_verdict_t v;
    memset(out1, 0, BIG);
    memset(out2, 0, BIG);
    CHECK(dnx_duplex_decode(strand, (uint32_t) n, out1, out2, BIG, &v) == DNX_OK, "decode OK");
    CHECK(v.state == LPRES_STATE_TRUE && v.fwd.state == LPRES_STATE_TRUE &&
              v.rev.state == LPRES_STATE_TRUE && v.payloads_equal,
          "both reads verify and agree: TRUE");
    CHECK(v.fwd.len == len && v.rev.len == len, "both reads find the length");
    CHECK(memcmp(out1, msg, len) == 0 && memcmp(out2, msg, len) == 0,
          "start-to-finish and finish-to-start give the same payload");

    /* The reverse read is the forward read of the partner strand. */
    dnx_revcomp(strand, (uint32_t) n, strand2, BIG);
    CHECK(memcmp(strand, strand2, (size_t) n) == 0, "partner strand is identical");

    /* Damage the forward copy of the body: forward fails its CRC, reverse
     * still verifies; held as BOTH, the good copy still comes back. */
    memcpy(strand2, strand, (size_t) n);
    strand2[DNX_MAGIC_BASES + DNX_LEN_BASES + 5] ^= 1u;
    memset(out1, 0, BIG);
    memset(out2, 0, BIG);
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, out1, out2, BIG, &v) == DNX_E_CRC,
          "forward damage reports the CRC");
    CHECK(v.fwd.state == LPRES_STATE_FALSE && v.fwd.status == DNX_E_CRC, "forward read FALSE");
    CHECK(v.rev.state == LPRES_STATE_TRUE, "reverse read TRUE");
    CHECK(v.state == LPRES_STATE_BOTH && !v.payloads_equal, "disagreement is BOTH");
    CHECK(memcmp(out2, msg, len) == 0 && out1[0] == 0, "only the verified read is written");

    /* Damage the back half: mirror image. */
    memcpy(strand2, strand, (size_t) n);
    strand2[n - 1 - (int32_t) (DNX_MAGIC_BASES + DNX_LEN_BASES + 9)] ^= 2u;
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, out1, out2, BIG, &v) == DNX_E_CRC,
          "reverse damage reports the CRC");
    CHECK(v.fwd.state == LPRES_STATE_TRUE && v.rev.state == LPRES_STATE_FALSE &&
              v.state == LPRES_STATE_BOTH,
          "reverse FALSE, forward TRUE: BOTH");

    /* Damage both copies: FALSE. */
    strand2[DNX_MAGIC_BASES + DNX_LEN_BASES + 5] ^= 1u;
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, out1, out2, BIG, &v) == DNX_E_CRC &&
              v.state == LPRES_STATE_FALSE,
          "both reads fail: FALSE");

    /* The CRC field itself is checked. */
    memcpy(strand2, strand, (size_t) n);
    strand2[DNX_MAGIC_BASES + DNX_LEN_BASES + 4 * len + 15] ^= 3u;
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, NULL, NULL, 0, &v) == DNX_E_CRC &&
              v.fwd.state == LPRES_STATE_FALSE && v.state == LPRES_STATE_BOTH,
          "a damaged CRC field fails that read");

    memcpy(strand2, strand, (size_t) n);
    strand2[3] ^= 1u;
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, NULL, NULL, 0, &v) == DNX_E_MAGIC &&
              v.fwd.status == DNX_E_MAGIC && v.state == LPRES_STATE_BOTH,
          "a damaged magic fails that read");
    memcpy(strand2, strand, (size_t) n);
    strand2[DNX_MAGIC_BASES + 15] ^= 1u;
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, NULL, NULL, 0, &v) == DNX_E_LEN &&
              v.fwd.status == DNX_E_LEN && v.rev.state == LPRES_STATE_TRUE,
          "a damaged length fails that read");
    memcpy(strand2, strand, (size_t) n);
    strand2[(uint32_t) n / 2u] ^= 3u;
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, NULL, NULL, 0, &v) == DNX_E_PIVOT &&
              v.fwd.status == DNX_E_PIVOT && v.rev.status == DNX_E_PIVOT &&
              v.state == LPRES_STATE_FALSE,
          "both reads cross the pivot: a damaged pivot fails both");

    /* Too short to hold any frame: neither read says anything. */
    CHECK(dnx_duplex_decode(strand, DNX_DUPLEX_BASES(0) - 1u, NULL, NULL, 0, &v) == DNX_E_SHORT &&
              v.fwd.state == LPRES_STATE_NEITHER && v.rev.state == LPRES_STATE_NEITHER &&
              v.state == LPRES_STATE_NEITHER,
          "short strand: NEITHER");
    /* The longest strand that is still too short, and the shortest that is not. */
    int32_t n0 = dnx_duplex_encode(NULL, 0, strand2, BIG);
    CHECK(n0 == (int32_t) DNX_DUPLEX_BASES(0) &&
              dnx_duplex_decode(strand2, (uint32_t) n0, NULL, NULL, 0, &v) == DNX_OK &&
              v.state == LPRES_STATE_TRUE && v.fwd.len == 0,
          "empty payload frames and verifies");
    /* A whole frame truncated by one base: the length no longer fits. */
    CHECK(dnx_duplex_decode(strand, (uint32_t) n - 1u, NULL, NULL, 0, &v) == DNX_E_LEN,
          "a truncated strand fails the length");

    /* Splice: front half from one payload, back half from another of the
     * same length. Each read verifies on its own and they differ. */
    const char *other = "chiglet DNA reads both WAYS";
    dnx_duplex_encode((const uint8_t *) other, len, strand2, BIG);
    memcpy(strand2, strand, (size_t) n / 2u);
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, out1, out2, BIG, &v) == DNX_E_SPLIT,
          "spliced strand is a split");
    CHECK(v.fwd.state == LPRES_STATE_TRUE && v.rev.state == LPRES_STATE_TRUE &&
              v.state == LPRES_STATE_BOTH && !v.payloads_equal,
          "two verified, different payloads: BOTH");
    CHECK(memcmp(out1, msg, len) == 0 && memcmp(out2, other, len) == 0,
          "both candidates are handed back");

    memset(out1, 0x5A, BIG);
    CHECK(dnx_duplex_decode(strand, (uint32_t) n, out1, NULL, len - 1u, &v) == DNX_E_CAP &&
              v.state == LPRES_STATE_TRUE && out1[0] == 0x5A,
          "decode capacity: verdict filled, nothing written");
    CHECK(dnx_duplex_encode((const uint8_t *) msg, len, strand2, (uint32_t) n - 1u) == DNX_E_CAP,
          "encode capacity checked");
    CHECK(dnx_duplex_encode((const uint8_t *) msg, DNX_MAX_PAYLOAD + 1u, strand2, BIG) == DNX_E_ARG,
          "payload cap");
    strand2[0] = 9;
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, NULL, NULL, 0, &v) == DNX_E_BASE,
          "decode refuses a non-base");

    /* A chiglet DNA record (chg_kin's encoded size, 152 bytes) carried whole. */
    uint8_t rec[152];
    for (uint32_t i = 0; i < sizeof rec; i++) rec[i] = (uint8_t) next_rand();
    n = dnx_duplex_encode(rec, sizeof rec, strand, BIG);
    CHECK(n == (int32_t) DNX_DUPLEX_BASES(sizeof rec) &&
              dnx_duplex_decode(strand, (uint32_t) n, out1, out2, BIG, &v) == DNX_OK &&
              memcmp(out1, rec, sizeof rec) == 0 && memcmp(out2, rec, sizeof rec) == 0,
          "a 152-byte DNA record round-trips both ways");
}

/* Edges: every argument guard, each pivot end, a length forged to wrap. */
static void test_edges(void)
{
    const uint32_t huge = 0x80000000u; /* over INT32_MAX: refused before any read */
    uint8_t one = 0;
    dnx_metrics_t m;
    dnx_verdict_t v;
    dnx_state_t st = {{0}};
    CHECK(dnx_bytes_to_bases(&one, DNX_MAX_PAYLOAD + 1u, bases, BIG) == DNX_E_ARG,
          "bytes_to_bases payload cap");
    CHECK(dnx_bytes_to_bases(&one, 1, NULL, 4) == DNX_E_ARG, "bytes_to_bases NULL out");
    CHECK(dnx_bytes_to_bases(NULL, 0, bases, 0) == 0, "zero bytes is zero bases");
    CHECK(dnx_bases_to_bytes(&one, 4u * (DNX_MAX_PAYLOAD + 1u), out1, BIG) == DNX_E_ARG,
          "bases_to_bytes payload cap");
    CHECK(dnx_bases_to_bytes(NULL, 4, out1, 1) == DNX_E_ARG, "bases_to_bytes NULL in");
    CHECK(dnx_bases_to_bytes(bases, 4, NULL, 1) == DNX_E_ARG, "bases_to_bytes NULL out");
    CHECK(dnx_bases_to_bytes(NULL, 0, out1, 0) == 0, "zero bases is zero bytes");
    CHECK(dnx_revcomp(&one, huge, out1, BIG) == DNX_E_ARG, "revcomp length guard");
    CHECK(dnx_revcomp(NULL, 1, out1, 1) == DNX_E_ARG, "revcomp NULL in");
    CHECK(dnx_revcomp(&one, 1, NULL, 1) == DNX_E_ARG, "revcomp NULL out");
    uint8_t four = 4;
    CHECK(dnx_revcomp(&four, 1, out1, 1) == DNX_E_BASE, "revcomp refuses code 4");
    CHECK(dnx_revcomp(NULL, 0, out1, 0) == 0, "revcomp of nothing");
    CHECK(dnx_to_text(&one, huge, false, (char *) out1, BIG) == DNX_E_ARG, "to_text length guard");
    CHECK(dnx_to_text(NULL, 1, false, (char *) out1, 1) == DNX_E_ARG, "to_text NULL in");
    CHECK(dnx_to_text(&one, 1, false, NULL, 1) == DNX_E_ARG, "to_text NULL out");
    CHECK(dnx_from_text("A", huge, out1, BIG) == DNX_E_ARG, "from_text length guard");
    CHECK(dnx_from_text(NULL, 1, out1, 1) == DNX_E_ARG, "from_text NULL in");
    CHECK(dnx_from_text("A", 1, NULL, 1) == DNX_E_ARG, "from_text NULL out");
    CHECK(dnx_metrics(&one, huge, &m) == DNX_E_ARG, "metrics length guard");
    CHECK(dnx_metrics(NULL, 1, &m) == DNX_E_ARG, "metrics NULL in");
    CHECK(dnx_metrics(&one, 1, NULL) == DNX_E_ARG, "metrics NULL out");
    CHECK(dnx_duplex_decode(&one, huge, NULL, NULL, 0, &v) == DNX_E_ARG, "decode length guard");
    CHECK(dnx_duplex_decode(NULL, 1, NULL, NULL, 0, &v) == DNX_E_ARG, "decode NULL strand");
    CHECK(dnx_duplex_decode(&one, 1, NULL, NULL, 0, NULL) == DNX_E_ARG, "decode NULL verdict");
    CHECK(dnx_duplex_encode(NULL, 1, strand2, BIG) == DNX_E_ARG, "encode NULL payload");
    CHECK(dnx_duplex_encode(&one, 1, NULL, BIG) == DNX_E_ARG, "encode NULL strand");
    CHECK(dnx_prog_run(&one, 0x80000001u, &st) == DNX_E_ARG, "program length guard");
    CHECK(dnx_prog_run(NULL, 3, &st) == DNX_E_ARG, "program NULL strand");
    CHECK(dnx_prog_run(NULL, 0, &st) == 0 && dnx_prog_run_reverse(NULL, 0, &st) == 0,
          "the empty program runs");

    /* Odd strands can never be their own reverse complement. */
    uint8_t s[3];
    dnx_from_text("ACT", 3, s, 3);
    CHECK(dnx_metrics(s, 3, &m) == DNX_OK && !m.revcomp_palindrome, "ACT is not a palindrome");
    dnx_from_text("AAT", 3, s, 3);
    CHECK(dnx_metrics(s, 3, &m) == DNX_OK && m.longest_run == 2 && m.count[DNX_T] == 1,
          "run of two");

    /* Each end of the pivot is checked by both reads. */
    const char *msg = "pivot";
    int32_t n = dnx_duplex_encode((const uint8_t *) msg, 5, strand, BIG);
    uint32_t half = (uint32_t) (n - (int32_t) DNX_PIVOT_BASES) / 2u;
    for (uint32_t at = 0; at < DNX_PIVOT_BASES; at++) {
        memcpy(strand2, strand, (size_t) n);
        strand2[half + at] ^= 1u;
        CHECK(dnx_duplex_decode(strand2, (uint32_t) n, NULL, NULL, 0, &v) == DNX_E_PIVOT &&
                  v.state == LPRES_STATE_FALSE,
              "every pivot base fails both reads");
    }
    /* Each end of the magic fails only the read that starts there. */
    for (uint32_t at = 0; at < DNX_MAGIC_BASES; at++) {
        memcpy(strand2, strand, (size_t) n);
        strand2[at] ^= 2u;
        CHECK(dnx_duplex_decode(strand2, (uint32_t) n, NULL, NULL, 0, &v) == DNX_E_MAGIC &&
                  v.rev.state == LPRES_STATE_TRUE,
              "every magic base is checked");
    }
    /* Every body byte of the reverse copy is in its CRC. */
    for (uint32_t i = 0; i < 5u; i++) {
        memcpy(strand2, strand, (size_t) n);
        strand2[(uint32_t) n - 1u - (DNX_MAGIC_BASES + DNX_LEN_BASES + 4u * i + 3u)] ^= 1u;
        CHECK(dnx_duplex_decode(strand2, (uint32_t) n, out1, out2, BIG, &v) == DNX_E_CRC &&
                  v.rev.status == DNX_E_CRC && v.fwd.state == LPRES_STATE_TRUE &&
                  memcmp(out1, msg, 5) == 0,
              "a low bit in any reverse body byte is caught");
    }
    /* A length field forged so 2(40 + 4 len) + 6 wraps to the real size:
     * len = real + 2^29. The payload cap refuses it before any read. */
    memcpy(strand2, strand, (size_t) n);
    uint32_t forged = 5u + (1u << 29);
    for (uint32_t i = 0; i < DNX_LEN_BASES; i++)
        strand2[DNX_MAGIC_BASES + i] = (uint8_t) ((forged >> (2u * (DNX_LEN_BASES - 1u - i))) & 3u);
    CHECK(dnx_duplex_decode(strand2, (uint32_t) n, NULL, NULL, 0, &v) == DNX_E_LEN &&
              v.fwd.status == DNX_E_LEN && v.rev.state == LPRES_STATE_TRUE,
          "a length that wraps 32 bits is refused");
    /* The verdict's lengths and CRCs are the ones each read carried. */
    CHECK(dnx_duplex_decode(strand, (uint32_t) n, NULL, NULL, 0, &v) == DNX_OK &&
              v.fwd.crc == dnx_crc32((const uint8_t *) msg, 5) && v.rev.crc == v.fwd.crc &&
              v.rev.len == 5,
          "reads report the CRC they carried");
    /* Decode cap: exactly enough is enough, for either output alone. */
    CHECK(dnx_duplex_decode(strand, (uint32_t) n, NULL, out2, 5, &v) == DNX_OK, "cap == len ok");
    CHECK(dnx_duplex_decode(strand, (uint32_t) n, NULL, out2, 4, &v) == DNX_E_CAP,
          "reverse output cap checked");
}

/* An independent model of the 32 op pairs, written from the table in dnx.h's
 * D4 note rather than from dnx.c. */
static void ref_exec(uint32_t k, bool inv, uint32_t r[4])
{
    static const uint8_t add[12][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 0}, {1, 2}, {1, 3},
                                       {2, 0}, {2, 1}, {2, 3}, {3, 0}, {3, 1}, {3, 2}};
    static const uint8_t swp[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    if (k < 12) {
        uint32_t d = r[add[k][1]];
        r[add[k][0]] = inv ? r[add[k][0]] - d : r[add[k][0]] + d;
    } else if (k < 16) {
        r[k - 12] += inv ? 0xFFFFFFFFu : 1u;
    } else if (k < 20) {
        uint32_t x = r[k - 16];
        r[k - 16] = inv ? (x >> 7) | (x << 25) : (x << 7) | (x >> 25);
    } else if (k < 24) {
        uint64_t p = (uint64_t) r[k - 20] * (inv ? 0x0E8B2F51u : 0x9E3779B1u);
        r[k - 20] = (uint32_t) p;
    } else if (k < 30) {
        uint32_t t = r[swp[k - 24][0]];
        r[swp[k - 24][0]] = r[swp[k - 24][1]];
        r[swp[k - 24][1]] = t;
    } else if (k == 30) {
        r[1] ^= r[0];
    } else {
        r[3] ^= r[2];
    }
}

static uint8_t huge_a[DNX_MAX_BASES], huge_b[DNX_MAX_BASES];

static void test_more(void)
{
    /* Every codon against the reference model. */
    int ok = 1;
    for (uint32_t c = 0; c < 64; c++) {
        uint8_t p[3] = {(uint8_t) (c >> 4), (uint8_t) ((c >> 2) & 3u), (uint8_t) (c & 3u)};
        uint8_t op = dnx_codon_op((uint8_t) c);
        for (int t = 0; t < 4; t++) {
            dnx_state_t s = {{next_rand(), next_rand(), next_rand(), next_rand()}};
            uint32_t r[4] = {s.r[0], s.r[1], s.r[2], s.r[3]};
            dnx_prog_run(p, 3, &s);
            ref_exec(op >> 1, (op & 1u) != 0, r);
            if (memcmp(s.r, r, sizeof r) != 0) ok = 0;
        }
    }
    CHECK(ok, "all 64 codons match the reference model");
    uint8_t s3[3] = {DNX_A, DNX_C, DNX_A};
    dnx_state_t z = {{1, 2, 3, 4}};
    CHECK(dnx_prog_run_reverse(s3, 3, &z) == 1, "reverse run returns codons run");
    uint8_t s6[6] = {0, 1, 2, 3, 0, 1};
    CHECK(dnx_prog_run_reverse(s6, 6, &z) == 2 && dnx_prog_run(s6, 6, &z) == 2, "two codons");

    /* A bad base first or last is found by every validator. */
    uint8_t bf[4] = {4, 0, 0, 0}, bl[4] = {0, 0, 0, 4};
    char tf[4] = {'X', 'A', 'A', 'A'}, tl[4] = {'A', 'A', 'A', 'X'};
    dnx_metrics_t m;
    dnx_verdict_t v;
    dnx_state_t st = {{0}};
    CHECK(dnx_bases_to_bytes(bf, 4, out1, 1) == DNX_E_BASE &&
              dnx_bases_to_bytes(bl, 4, out1, 1) == DNX_E_BASE,
          "bases_to_bytes: first and last");
    CHECK(dnx_revcomp(bf, 4, out1, 4) == DNX_E_BASE && dnx_revcomp(bl, 4, out1, 4) == DNX_E_BASE,
          "revcomp: first and last");
    CHECK(dnx_to_text(bf, 4, false, (char *) out1, 4) == DNX_E_BASE &&
              dnx_to_text(bl, 4, false, (char *) out1, 4) == DNX_E_BASE,
          "to_text: first and last");
    CHECK(dnx_from_text(tf, 4, out1, 4) == DNX_E_BASE &&
              dnx_from_text(tl, 4, out1, 4) == DNX_E_BASE,
          "from_text: first and last");
    CHECK(dnx_metrics(bf, 4, &m) == DNX_E_BASE && dnx_metrics(bl, 4, &m) == DNX_E_BASE,
          "metrics: first and last");
    CHECK(dnx_duplex_decode(bf, 4, NULL, NULL, 0, &v) == DNX_E_BASE &&
              dnx_duplex_decode(bl, 4, NULL, NULL, 0, &v) == DNX_E_BASE,
          "decode: first and last");
    uint8_t pf[3] = {4, 0, 0}, pl[3] = {0, 0, 4};
    CHECK(dnx_prog_run(pf, 3, &st) == DNX_E_BASE && dnx_prog_run(pl, 3, &st) == DNX_E_BASE &&
              dnx_prog_run_reverse(pf, 3, &st) == DNX_E_BASE &&
              dnx_prog_run_reverse(pl, 3, &st) == DNX_E_BASE,
          "programs: first and last");
    CHECK(dnx_bases_to_bytes(bf, 5, out1, 2) == DNX_E_ARG, "five bases are not whole bytes");

    /* Metrics edges. */
    uint8_t g1[1] = {DNX_G}, cg[2] = {DNX_C, DNX_G};
    CHECK(dnx_metrics(g1, 1, &m) == DNX_OK && m.gc_permille == 1000, "one G is 1000 permille");
    CHECK(dnx_metrics(cg, 2, &m) == DNX_OK && m.gc_permille == 1000 && m.revcomp_palindrome,
          "CG: all GC, a palindrome");
    uint8_t tail5[5];
    dnx_from_text("TAATA", 5, tail5, 5);
    CHECK(dnx_metrics(tail5, 5, &m) == DNX_OK && m.stop_codons == 1, "a partial codon is not read");
    uint8_t six[6];
    dnx_from_text("ATGTAA", 6, six, 6);
    CHECK(dnx_metrics(six, 6, &m) == DNX_OK && m.start_codons == 1 && m.stop_codons == 1,
          "codons are read in steps of three");
    dnx_from_text("GAAATC", 6, six, 6);
    CHECK(dnx_metrics(six, 6, &m) == DNX_OK && !m.revcomp_palindrome,
          "the innermost pair is checked");

    /* Duplex edges. */
    const char *msg = "edges";
    uint32_t need = DNX_DUPLEX_BASES(5);
    memset(strand, 1, BIG);
    CHECK(dnx_duplex_encode((const uint8_t *) msg, 5, strand, need) == (int32_t) need,
          "cap == need encodes");
    CHECK(dnx_duplex_decode(strand, need, NULL, NULL, 0, &v) == DNX_OK, "every base was written");
    memset(out1, 0x77, 16);
    memset(out2, 0x77, 16);
    CHECK(dnx_duplex_decode(strand, need, out1, out2, 5, &v) == DNX_OK && out1[5] == 0x77 &&
              out2[5] == 0x77,
          "cap == len decodes, nothing past it");
    CHECK(dnx_duplex_decode(strand, need, out1, NULL, 4, &v) == DNX_E_CAP,
          "forward output cap checked");
    memset(strand2, 0, BIG);
    dnx_duplex_encode((const uint8_t *) "Edges", 5, strand2, BIG);
    memcpy(strand2, strand, need / 2u);
    CHECK(dnx_duplex_decode(strand2, need, NULL, NULL, 0, &v) == DNX_E_SPLIT,
          "a splice differing only in byte 0 is caught");
    memcpy(strand2, strand, need);
    strand2[2] ^= 1u;
    CHECK(dnx_duplex_decode(strand2, need, NULL, NULL, 0, &v) == DNX_E_MAGIC && v.fwd.len == 0 &&
              v.fwd.crc == 0,
          "a failed read reports no length or CRC");

    /* The empty program: x == y both ways. */
    dnx_state_t x = {{9, 8, 7, 6}};
    CHECK(dnx_prog_bidi(NULL, 0, &x, &x, NULL, NULL) == LPRES_STATE_TRUE, "empty program");
    dnx_state_t y = x;
    y.r[0]++;
    CHECK(dnx_prog_bidi(NULL, 0, &x, &y, NULL, NULL) == LPRES_STATE_FALSE,
          "register 0 is compared");

    /* The largest frame and strand are accepted; one past is refused. */
    for (uint32_t i = 0; i < DNX_MAX_PAYLOAD; i++) huge_b[i] = (uint8_t) i;
    int32_t hn = dnx_duplex_encode(huge_b, DNX_MAX_PAYLOAD, huge_a, DNX_MAX_BASES);
    CHECK(hn == (int32_t) DNX_MAX_BASES, "largest payload encodes");
    CHECK(dnx_duplex_decode(huge_a, DNX_MAX_BASES, NULL, NULL, 0, &v) == DNX_OK &&
              v.fwd.len == DNX_MAX_PAYLOAD,
          "largest frame decodes");
    CHECK(dnx_bytes_to_bases(huge_b, DNX_MAX_PAYLOAD, huge_a, DNX_MAX_BASES) ==
              (int32_t) (4u * DNX_MAX_PAYLOAD),
          "largest payload converts");
    CHECK(dnx_bases_to_bytes(huge_a, 4u * DNX_MAX_PAYLOAD, huge_b, DNX_MAX_PAYLOAD) ==
              (int32_t) DNX_MAX_PAYLOAD,
          "largest payload converts back");
    memset(huge_a, 0, DNX_MAX_BASES);
    CHECK(dnx_revcomp(huge_a, DNX_MAX_BASES, huge_b, DNX_MAX_BASES) == (int32_t) DNX_MAX_BASES,
          "largest strand reverse-complements");
    CHECK(dnx_to_text(huge_a, DNX_MAX_BASES, false, (char *) huge_b, DNX_MAX_BASES) ==
              (int32_t) DNX_MAX_BASES,
          "largest strand as text");
    CHECK(dnx_from_text((const char *) huge_b, DNX_MAX_BASES, huge_a, DNX_MAX_BASES) ==
              (int32_t) DNX_MAX_BASES,
          "largest text as a strand");
    CHECK(dnx_metrics(huge_a, DNX_MAX_BASES, &m) == DNX_OK && m.count[DNX_A] == DNX_MAX_BASES,
          "largest strand measured");
    CHECK(dnx_metrics(huge_a, DNX_MAX_BASES + 1u, &m) == DNX_E_ARG &&
              dnx_revcomp(huge_a, DNX_MAX_BASES + 1u, huge_b, DNX_MAX_BASES) == DNX_E_ARG &&
              dnx_duplex_decode(huge_a, DNX_MAX_BASES + 1u, NULL, NULL, 0, &v) == DNX_E_ARG,
          "one base past the limit is refused");
}

static void test_codon_table(void)
{
    uint32_t rank = 0;
    int table_ok = 1, pair_ok = 1;
    for (uint32_t c = 0; c < 64; c++) {
        uint8_t r = dnx_codon_revcomp((uint8_t) c);
        if (r == c || dnx_codon_revcomp(r) != c) pair_ok = 0;
        if (c < r) {
            if (dnx_codon_op((uint8_t) c) != (uint8_t) (rank << 1) ||
                dnx_codon_op(r) != (uint8_t) ((rank << 1) | 1u))
                table_ok = 0;
            rank++;
        }
    }
    CHECK(pair_ok, "no codon is its own reverse complement; rc is an involution");
    CHECK(rank == DNX_OPS, "64 codons make 32 pairs");
    CHECK(table_ok, "op table matches its definition: pair k, inverse on the larger codon");
    uint8_t atg[3] = {DNX_A, DNX_T, DNX_G};
    CHECK(dnx_codon_value(atg) == 14 && dnx_codon_revcomp(14) == 19, "rc(ATG) = CAT");
}

static void test_ops_invert(void)
{
    /* Each of the 64 single-codon programs followed by its partner. */
    int ok = 1, moves = 0;
    for (uint32_t c = 0; c < 64; c++) {
        uint8_t prog[3] = {(uint8_t) (c >> 4), (uint8_t) ((c >> 2) & 3u), (uint8_t) (c & 3u)};
        for (int t = 0; t < 8; t++) {
            dnx_state_t x = {{next_rand(), next_rand(), next_rand(), next_rand()}};
            dnx_state_t y = x;
            dnx_prog_run(prog, 3, &y);
            if (memcmp(&x, &y, sizeof x) != 0) moves++;
            dnx_prog_run_reverse(prog, 3, &y);
            if (memcmp(&x, &y, sizeof x) != 0) ok = 0;
        }
    }
    CHECK(ok, "every codon is undone by its reverse complement");
    CHECK(moves == 64 * 8, "every codon changes a random state");

    /* Spot values pin each op family to its definition: the forward codon,
     * then its partner undoing it. */
    const struct {
        const char *fwd, *inv;
        dnx_state_t want;
    } spot[] = {
        {"AAA", "TTT", {{11, 7, 5, 3}}},          /* k0: r0 += r1 */
        {"ATC", "GAT", {{5, 7, 5, 3}}},           /* k12: r0 += 1 */
        {"CAG", "CTG", {{4 << 7, 7, 5, 3}}},      /* k16: r0 rotl 7 */
        {"CGA", "TCG", {{0x78DDE6C4u, 7, 5, 3}}}, /* k20: r0 *= 0x9E3779B1 */
        {"GAA", "TTC", {{7, 4, 5, 3}}},           /* k24: swap r0 r1 */
        {"TAA", "TTA", {{4, 7 ^ 4, 5, 3}}},       /* k30: r1 ^= r0 */
        {"TCA", "TGA", {{4, 7, 5, 3 ^ 5}}},       /* k31: r3 ^= r2 */
    };
    for (uint32_t i = 0; i < sizeof spot / sizeof spot[0]; i++) {
        uint8_t p[3];
        dnx_state_t s = {{4, 7, 5, 3}};
        dnx_from_text(spot[i].fwd, 3, p, 3);
        dnx_prog_run(p, 3, &s);
        CHECK(memcmp(&s, &spot[i].want, sizeof s) == 0, spot[i].fwd);
        dnx_from_text(spot[i].inv, 3, p, 3);
        dnx_prog_run(p, 3, &s);
        CHECK(s.r[0] == 4 && s.r[1] == 7 && s.r[2] == 5 && s.r[3] == 3, spot[i].inv);
    }
    dnx_state_t s = {{0x80000001u, 0, 0, 0}};
    uint8_t p[3];
    dnx_from_text("CAG", 3, p, 3);
    dnx_prog_run(p, 3, &s);
    CHECK(s.r[0] == 0xC0u, "rotl carries the top bit round");
    dnx_from_text("CTG", 3, p, 3);
    dnx_prog_run(p, 3, &s);
    CHECK(s.r[0] == 0x80000001u, "rotr carries it back");
}

/* The bidirectional theorem over random programs. */
static void test_prog_bidi(void)
{
    int ok = 1, changed = 0, bidi_ok = 1;
    for (uint32_t trial = 0; trial < 400; trial++) {
        uint32_t m = 1u + next_rand() % 200u;
        for (uint32_t i = 0; i < 3u * m; i++) bases[i] = (uint8_t) (next_rand() & 3u);
        dnx_state_t x = {{next_rand(), next_rand(), next_rand(), next_rand()}};
        dnx_state_t y = x;
        if (dnx_prog_run(bases, 3u * m, &y) != (int32_t) m) ok = 0;
        if (memcmp(&x, &y, sizeof x) != 0) changed++;
        lpres_state_t f, b;
        if (dnx_prog_bidi(bases, 3u * m, &x, &y, &f, &b) != LPRES_STATE_TRUE ||
            f != LPRES_STATE_TRUE || b != LPRES_STATE_TRUE)
            bidi_ok = 0;
        dnx_state_t z = y;
        dnx_prog_run_reverse(bases, 3u * m, &z);
        if (memcmp(&x, &z, sizeof x) != 0) ok = 0;
        /* Reading backwards equals running the reverse-complement strand forwards. */
        dnx_state_t w = y;
        dnx_revcomp(bases, 3u * m, strand, BIG);
        dnx_prog_run(strand, 3u * m, &w);
        if (memcmp(&w, &z, sizeof w) != 0) ok = 0;
    }
    CHECK(ok, "run(revcomp(P), run(P, x)) == x for 400 random programs");
    CHECK(changed > 390, "random programs compute something");
    CHECK(bidi_ok, "bidi verifies forward and backward");

    /* A wrong claim fails both ways, and they still agree. */
    dnx_from_text("AAACCCGGG", 9, bases, 9);
    dnx_state_t x = {{1, 2, 3, 4}}, y = x;
    dnx_prog_run(bases, 9, &y);
    y.r[2] ^= 1u;
    lpres_state_t f, b;
    CHECK(dnx_prog_bidi(bases, 9, &x, &y, &f, &b) == LPRES_STATE_FALSE && f == LPRES_STATE_FALSE &&
              b == LPRES_STATE_FALSE,
          "a wrong output is FALSE both ways");
    CHECK(dnx_prog_bidi(bases, 8, &x, &y, &f, &b) == LPRES_STATE_NEITHER &&
              f == LPRES_STATE_NEITHER && b == LPRES_STATE_NEITHER,
          "a strand that is not whole codons says nothing");
    CHECK(dnx_prog_bidi(bases, 9, NULL, &y, NULL, NULL) == LPRES_STATE_NEITHER, "NULL state");
    CHECK(dnx_prog_run(bases, 9, NULL) == DNX_E_ARG, "run needs a state");
    bases[4] = 5;
    CHECK(dnx_prog_run(bases, 9, &x) == DNX_E_BASE &&
              dnx_prog_run_reverse(bases, 9, &x) == DNX_E_BASE,
          "programs refuse non-bases");
}

static void test_amino(void)
{
    uint32_t stops = 0, met = 0, trp = 0;
    for (uint32_t c = 0; c < 64; c++) {
        char a = dnx_codon_amino((uint8_t) c);
        stops += a == '*';
        met += a == 'M';
        trp += a == 'W';
    }
    CHECK(stops == 3 && met == 1 && trp == 1, "3 stops, one Met, one Trp");
    uint8_t s[3];
    const struct {
        const char *codon;
        char aa;
    } want[] = {{"ATG", 'M'}, {"TAA", '*'}, {"TAG", '*'}, {"TGA", '*'}, {"TGG", 'W'},
                {"GCT", 'A'}, {"TTT", 'F'}, {"AAA", 'K'}, {"CAT", 'H'}, {"GGC", 'G'},
                {"AGA", 'R'}, {"CTG", 'L'}, {"ATA", 'I'}, {"TCC", 'S'}, {"GAC", 'D'}};
    int ok = 1;
    for (uint32_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        dnx_from_text(want[i].codon, 3, s, 3);
        if (dnx_codon_amino(dnx_codon_value(s)) != want[i].aa) ok = 0;
    }
    CHECK(ok, "standard genetic code letters");
}

int main(void)
{
    test_conversion();
    test_revcomp();
    test_metrics();
    test_crc();
    test_duplex();
    test_edges();
    test_more();
    test_codon_table();
    test_ops_invert();
    test_prog_bidi();
    test_amino();
    printf("%d checks, %d failed\n", passes + failures, failures);
    return failures ? 1 : 0;
}
