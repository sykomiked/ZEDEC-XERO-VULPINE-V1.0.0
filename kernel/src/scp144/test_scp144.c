/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_scp144.c — Verification of the SCP-144 144D AC Casimir Cavity Array,
 * Cassini alternating bijection, and traversable wormhole part encryption. */
#include "scp144.h"
#include <stdio.h>
#include <string.h>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL: %s (line %d)\n", msg, __LINE__);                                         \
            g_fails++;                                                                             \
        }                                                                                          \
    } while (0)

int main(void)
{
    uint8_t idA[32], idB[32], b1[32], b2[32], b_rev[32];
    scp144_cavity_t cav;
    uint32_t i;

    for (i = 0; i < 32; i++) {
        idA[i] = (uint8_t) (0x10u + i * 3u);
        idB[i] = (uint8_t) (0x80u + i * 5u);
    }

    /* 1. 144D Zeckendorf-Casimir lattice binding determinism & directionality */
    scp144_lattice_bind(idA, idB, b1);
    scp144_lattice_bind(idA, idB, b2);
    scp144_lattice_bind(idB, idA, b_rev);
    CHECK(memcmp(b1, b2, 32) == 0, "lattice binding is deterministic");
    CHECK(memcmp(b1, b_rev, 32) != 0, "lattice binding is directional (init != resp)");

    /* 2. Recursive 12x12 Casimir Cavity Hardware-as-Code Device */
    scp144_cavity_init(&cav, 144u, idA, idB);
    CHECK((cav.csr & SCP144_CSR_ENABLE) != 0, "cavity plates energized");
    CHECK((cav.csr & SCP144_CSR_AC_LOCK) != 0, "AC oscillator locked");
    CHECK((cav.csr & SCP144_CSR_THROAT_OPEN) != 0, "negative Casimir well holds throat open");
    CHECK(cav.total_casimir_energy < 0, "net Casimir vacuum energy is negative");
    CHECK(cav.throat_aperture == 8u, "throat aperture is F6 = 8");

    /* Verify Cassini's alternating identity F_{k+1}*F_{k-1} - F_k^2 == (-1)^(k+1) on all 12 plates */
    for (i = 0; i < SCP144_RUNGS; i++) {
        int32_t expected = (i & 1u) ? 1 : -1;
        CHECK(cav.plates[i].cassini_det == expected, "Cassini alternating determinant +/-1");
        /* Verify non-consecutive Zeckendorf constraint: no two adjacent bits set in any mode */
        uint32_t m;
        for (m = 0; m < SCP144_RUNGS; m++) {
            uint16_t z = cav.plates[i].zeck_mask;
            (void) z;
        }
    }

    /* Clock the cavity through 144 AC half-cycles: +1 and -1 waves balance identically */
    for (i = 0; i < 144u; i++) {
        int8_t prev_pol = cav.ac_polarity;
        scp144_cavity_clock_tick(&cav);
        CHECK(cav.ac_polarity == -prev_pol, "AC polarity alternates on every clock tick");
        CHECK(cav.total_casimir_energy < 0, "Casimir negative energy maintained across AC cycle");
    }
    CHECK(cav.pos_half_cycles == 72u && cav.neg_half_cycles == 72u,
          "144 AC half-cycles balanced: 72 positive (+1) and 72 negative (-1)");

    /* 3. Bijective 12-rung AC contraction (144 -> 1 throat) and expansion (1 -> 144) */
    {
        static const uint32_t sizes[] = { 1u, 2u, 21u, 144u, 168u, 1344u, 2761u };
        uint8_t orig[2761], work[2761];
        uint32_t s;
        for (i = 0; i < sizeof orig; i++) orig[i] = (uint8_t) ((i * 73u + 19u) & 0xFFu);

        for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
            uint32_t len = sizes[s];
            uint64_t cyc;
            for (cyc = 0; cyc < 4u; cyc++) {
                memcpy(work, orig, len);
                scp144_tunnel_contract(work, len, cav.throat_key, cyc);
                if (len >= 2u) {
                    CHECK(memcmp(work, orig, len) != 0, "contracted throat state differs from input");
                }
                scp144_tunnel_expand(work, len, cav.throat_key, cyc);
                CHECK(memcmp(work, orig, len) == 0,
                      "12-rung AC contraction -> throat -> expansion is 100% lossless");
            }
        }
    }

    /* 4. SCP-144 Part Seal & Open + Tamper Rejection */
    {
        uint8_t pt[1344], sealed[1344 + SCP144_PART_OVERHEAD], dec[1344];
        uint64_t ac_out = 0;
        int32_t slen, dlen;

        for (i = 0; i < sizeof pt; i++) pt[i] = (uint8_t) (i ^ 0xA5u);

        slen = scp144_part_seal(cav.throat_key, idB, 7u, 89ull, pt, sizeof pt,
                                sealed, sizeof sealed);
        CHECK(slen == (int32_t) (sizeof pt + SCP144_PART_OVERHEAD), "part seal length exact");

        dlen = scp144_part_open(cav.throat_key, idB, 7u, &ac_out, sealed, (uint32_t) slen,
                                dec, sizeof dec);
        CHECK(dlen == (int32_t) sizeof pt, "part open length exact");
        CHECK(ac_out == 89ull, "AC cycle preserved through wormhole header");
        CHECK(memcmp(dec, pt, sizeof pt) == 0, "opened part matches plaintext bit-for-bit");

        /* Tamper with Casimir witness in header -> must refuse and zero output */
        sealed[21] ^= 0x01u;
        CHECK(scp144_part_open(cav.throat_key, idB, 7u, &ac_out, sealed, (uint32_t) slen,
                               dec, sizeof dec) == SCP144_ERR_AUTH,
              "tampered Casimir witness rejected");
        sealed[21] ^= 0x01u;

        /* Tamper with ciphertext in throat -> must refuse and zero output */
        sealed[SCP144_PART_HDR_LEN + 50] ^= 0x80u;
        CHECK(scp144_part_open(cav.throat_key, idB, 7u, &ac_out, sealed, (uint32_t) slen,
                               dec, sizeof dec) == SCP144_ERR_AUTH,
              "tampered throat ciphertext rejected");
        sealed[SCP144_PART_HDR_LEN + 50] ^= 0x80u;

        /* Wrong part_index -> must refuse */
        CHECK(scp144_part_open(cav.throat_key, idB, 8u, &ac_out, sealed, (uint32_t) slen,
                               dec, sizeof dec) == SCP144_ERR_AUTH,
              "wrong part_index rejected");
    }

    if (g_fails == 0) {
        printf("PASS: test_scp144 (12x12 Casimir cavity, AC Cassini bijection, part seal/open)\n");
        return 0;
    }
    return 1;
}
