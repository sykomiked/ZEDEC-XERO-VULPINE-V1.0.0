/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hwp.c — host tests for ZHP-1 hardware profiles (hwp.h P1-P3). */
#include <stdio.h>
#include <string.h>
#include "hwp.h"
#include "hwe_ids.h"

static int fails, checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

/* A commodity arm64 laptop, as the host probe would describe it. */
static void laptop(hwp_profile_t *p)
{
    uint16_t i;
    hwp_init(p);
    hwp_node_add(p, 1, HWP_ROOT, HWP_CLASS_MACHINE, &i);
    hwp_node_add(p, 2, 1, HWP_CLASS_CPU_CLUSTER, &i);
    hwp_cap_set(p, i, HWP_CAP_COUNT, 16);
    hwp_cap_set(p, i, HWP_CAP_WORD_BITS, 64);
    hwp_cap_set(p, i, HWP_CAP_ISA, HWP_ISA_ARM64);
    hwp_node_add(p, 3, 1, HWP_CLASS_MEMORY, &i);
    hwp_cap_set(p, i, HWP_CAP_BYTES, 128ull << 30);
    hwp_node_add(p, 4, 2, HWP_CLASS_VECTOR_UNIT, &i);
    hwp_cap_set(p, i, HWP_CAP_INT_WIDTHS, HWP_INT_8 | HWP_INT_16 | HWP_INT_32 | HWP_INT_64);
    hwp_cap_set(p, i, HWP_CAP_SIMD, HWP_SIMD_NEON | HWP_SIMD_DOTPROD);
    hwp_node_add(p, 5, 2, HWP_CLASS_CACHE, &i);
    hwp_cap_set(p, i, HWP_CAP_LINE_BYTES, 128);
}

int main(void)
{
    static hwp_profile_t a, b, c;
    static uint8_t buf[8192], buf2[8192];
    uint32_t n, n2;
    uint16_t idx;

    /* P1: order and parents are enforced as the profile is built. */
    hwp_init(&c);
    CHECK(hwp_node_add(&c, 5, HWP_ROOT, HWP_CLASS_MACHINE, &idx) == HWP_OK);
    CHECK(hwp_node_add(&c, 4, 5, HWP_CLASS_CPU_CORE, 0) == HWP_E_CANONICAL);
    CHECK(hwp_node_add(&c, 6, 9, HWP_CLASS_CPU_CORE, 0) == HWP_E_CANONICAL);
    CHECK(hwp_node_add(&c, 6, 5, 0, 0) == HWP_E_ARG);
    /* P2: caps land sorted whatever order they are set in. */
    CHECK(hwp_cap_set(&c, idx, HWP_CAP_BYTES, 1) == HWP_OK);
    CHECK(hwp_cap_set(&c, idx, HWP_CAP_WORD_BITS, 64) == HWP_OK);
    CHECK(c.node[0].cap[0].key == HWP_CAP_WORD_BITS);
    CHECK(hwp_cap_set(&c, idx, (uint16_t) (HWP_CMP_EXACT | 1u), 64) == HWP_E_DUPLICATE);

    /* Round trip, one encoding, one digest. */
    laptop(&a);
    CHECK(hwp_encode(&a, buf, sizeof buf, &n) == HWP_OK && n == hwp_encoded_len(&a));
    CHECK(hwp_decode(buf, n, &b) == HWP_OK);
    CHECK(hwp_encode(&b, buf2, sizeof buf2, &n2) == HWP_OK && n2 == n && !memcmp(buf, buf2, n));
    uint8_t d1[32], d2[32];
    hwp_digest(&a, d1);
    hwp_digest(&b, d2);
    CHECK(!memcmp(d1, d2, 32));
    CHECK(hwp_encode(&a, buf2, n - 1, &n2) == HWP_E_BUFFER && n2 == n);

    /* State blobs travel with the node and change the digest. */
    static const uint8_t st[] = {1, 2, 3};
    CHECK(hwp_state_set(&b, 1, st, 3) == HWP_OK);
    hwp_digest(&b, d2);
    CHECK(memcmp(d1, d2, 32) != 0);
    CHECK(hwp_encode(&b, buf2, sizeof buf2, &n2) == HWP_OK && hwp_decode(buf2, n2, &c) == HWP_OK);
    CHECK(c.node[1].state_len == 3 && c.node[1].state[2] == 3);
    /* So does the private extension, which ZXV never interprets. */
    static const uint8_t ext[] = {0xEE, 0x01};
    uint8_t d3[32];
    CHECK(hwp_ext_set(&b, 2, ext, 2) == HWP_OK);
    CHECK(hwp_ext_set(&b, 2, ext, HWP_MAX_EXT + 1) == HWP_E_ARG);
    hwp_digest(&b, d3);
    CHECK(memcmp(d2, d3, 32) != 0);
    CHECK(hwp_encode(&b, buf2, sizeof buf2, &n2) == HWP_OK && hwp_decode(buf2, n2, &c) == HWP_OK);
    CHECK(c.node[2].ext_len == 2 && c.node[2].ext[0] == 0xEE && c.node[1].state_len == 3);
    /* The node's reserved field must be zero (P3). */
    buf2[HWP_HEADER + 14] = 1;
    CHECK(hwp_decode(buf2, n2, &c) == HWP_E_CANONICAL);

    /* P3 and format errors. */
    CHECK(hwp_decode(buf, n - 1, &c) == HWP_E_CANONICAL);
    memcpy(buf2, buf, n);
    buf2[3] = '2';
    CHECK(hwp_decode(buf2, n, &c) == HWP_E_FORMAT);
    memcpy(buf2, buf, n);
    buf2[6] = 1; /* flags must be zero */
    CHECK(hwp_decode(buf2, n, &c) == HWP_E_CANONICAL);
    /* Swap the first two cap keys of node 2 (P2). */
    memcpy(buf2, buf, n);
    {
        uint32_t o = HWP_HEADER + 16 + 0 /* node 1 has no caps */ + 16;
        uint8_t t[10];
        memcpy(t, buf2 + o, 10);
        memcpy(buf2 + o, buf2 + o + 10, 10);
        memcpy(buf2 + o + 10, t, 10);
        CHECK(hwp_decode(buf2, n, &c) == HWP_E_CANONICAL);
    }
    /* Truncated mid-node. */
    memcpy(buf2, buf, n);
    buf2[12] = (uint8_t) (HWP_HEADER + 5);
    buf2[13] = buf2[14] = buf2[15] = 0;
    CHECK(hwp_decode(buf2, HWP_HEADER + 5, &c) == HWP_E_FORMAT);

    /* Matching against R1: a laptop runs CPU, memory and vectors natively
     * and emulates the tensor, logic, ternary, hash, gcd, crypto, fabric,
     * clock, secure island and wire units. */
    const hwp_profile_t *r1 = hwp_reference_r1();
    CHECK(r1->n == 16);
    static hwp_match_t mt;
    CHECK(hwp_match(&a, r1, &mt) == HWP_OK);
    CHECK(mt.native == 3 && mt.emulated == 11);
    CHECK(mt.pair[0].kind == HWP_M_GROUP && mt.pair[1].kind == HWP_M_GROUP);
    CHECK(mt.pair[2].kind == HWP_M_NATIVE && mt.pair[2].have_id == 2);
    CHECK(mt.pair[4].kind == HWP_M_NATIVE && mt.pair[4].have_id == 4);
    CHECK(mt.pair[5].kind == HWP_M_EMULATE && mt.pair[5].unmet_key == 0);
    /* Engine nodes name their contracts. */
    {
        uint64_t v = 0;
        CHECK(hwp_cap_get(&r1->node[5], HWP_CAP_CONTRACT_ID, &v) && v == HWE_FN_TENSOR);
        CHECK(hwp_cap_get(&r1->node[11], HWP_CAP_CONTRACT_ID, &v) && v == HWE_FN_MAC);
    }
    /* Presented on the laptop: every running node says what serves it. */
    {
        static hwp_profile_t pr;
        uint64_t k = 99;
        CHECK(hwp_present(&a, r1, &pr) == HWP_OK && pr.n == r1->n);
        CHECK(!hwp_cap_get(&pr.node[0], HWP_CAP_KIND, &k) &&
              !hwp_cap_get(&pr.node[1], HWP_CAP_KIND, &k));
        CHECK(hwp_cap_get(&pr.node[2], HWP_CAP_KIND, &k) && k == HWP_KIND_HARD);
        CHECK(hwp_cap_get(&pr.node[5], HWP_CAP_KIND, &k) && k == HWP_KIND_SOFT);
        CHECK(hwp_present(&a, r1, (hwp_profile_t *) r1) == HWP_E_ARG);
        /* KIND is INFO: a presented profile still matches like R1 */
        CHECK(hwp_match(&a, &pr, &mt) == HWP_OK && mt.native == 3 && mt.emulated == 11);
    }

    /* A machine built to R1 (here a profile copied from R1 with bigger
     * numbers) runs everything natively. A private class and a private,
     * non-critical cap ride along without being understood. */
    static hwp_profile_t chip;
    memcpy(&chip, r1, sizeof chip);
    hwp_cap_set(&chip, 5, HWP_CAP_MACS_PER_CYCLE, 4096);
    hwp_cap_set(&chip, 5, (uint16_t) (HWP_CMP_INFO | 0x1234u), 77);
    CHECK(hwp_node_add(&chip, 200, 1, HWP_CLASS_PRIVATE | 7u, &idx) == HWP_OK);
    CHECK(hwp_ext_set(&chip, idx, ext, 2) == HWP_OK);
    CHECK(hwp_match(&chip, r1, &mt) == HWP_OK && mt.native == 14 && mt.emulated == 0);
    /* Too few MACs: emulated, and the report says which cap. */
    hwp_cap_set(&chip, 5, HWP_CAP_MACS_PER_CYCLE, 8);
    CHECK(hwp_match(&chip, r1, &mt) == HWP_OK && mt.pair[5].kind == HWP_M_EMULATE &&
          mt.pair[5].unmet_key == HWP_CAP_MACS_PER_CYCLE);
    hwp_cap_set(&chip, 5, HWP_CAP_MACS_PER_CYCLE, 4096);
    /* An older contract version does not satisfy a newer requirement. */
    hwp_cap_set(&chip, 6, HWP_CAP_CONTRACT_VER, 0);
    CHECK(hwp_match(&chip, r1, &mt) == HWP_OK && mt.pair[6].kind == HWP_M_EMULATE &&
          mt.pair[6].unmet_key == HWP_CAP_CONTRACT_VER);
    hwp_cap_set(&chip, 6, HWP_CAP_CONTRACT_VER, 1);
    /* A critical cap this reader does not know: not used blind. */
    hwp_cap_set(&chip, 7, (uint16_t) (HWP_KEY_CRITICAL | HWP_CMP_INFO | 0x1001u), 1);
    CHECK(hwp_match(&chip, r1, &mt) == HWP_OK && mt.pair[7].kind == HWP_M_EMULATE);
    /* Binary hardware never passes for ternary. */
    hwp_profile_t *tw = &c;
    memcpy(tw, r1, sizeof *tw);
    hwp_cap_set(tw, 7, HWP_CAP_RADIX, 2);
    CHECK(hwp_match(tw, r1, &mt) == HWP_OK && mt.pair[7].kind == HWP_M_EMULATE);

    CHECK(!strcmp(hwp_class_name(HWP_CLASS_TERNARY_UNIT), "engine.ternary"));
    CHECK(!strcmp(hwp_class_name(HWP_CLASS_SECURE_ISLAND), "secure.island"));
    CHECK(!strcmp(hwp_class_name(HWP_CLASS_MATRIX_UNIT), "engine.tensor.int"));
    CHECK(!strcmp(hwp_class_name(HWP_CLASS_PUBLIC_MAX + 1u), "unknown"));
    CHECK(!strcmp(hwp_class_name(HWP_CLASS_PRIVATE | 3u), "private"));

    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
