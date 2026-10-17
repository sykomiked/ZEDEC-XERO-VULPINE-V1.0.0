/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_smic.c — SMIC manifest frames, ML-DSA-87 attestations and
 * paraconsistent ring admission, on fake time. */
#include <stdio.h>
#include <string.h>
#include "smic_manifest.h"
#include "smic_verify.h"
#include "smic_core.h"
#include "../pqsec/pq_matrix_algs.h"
#include "../ubh/ubh.h"
#include "../robin_debanks/sha256.h"

static int passes, failures;
#define CHECK(x) do { if (x) passes++; else { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static void mk_hw(smic_hw_t *h, uint8_t node_tag, uint8_t region, uint8_t role)
{
    memset(h, 0, sizeof *h);
    memset(h->node_id, node_tag, 32);
    h->node_id[31] = region;
    memset(h->hw_digest, 0xA0 + node_tag, 32);
    h->caps = SMIC_CAP_VMXNET3 | SMIC_CAP_VERIFIED | SMIC_CAP_INDEX;
    if (role == SMIC_ROLE_MAINFRAME) h->caps |= SMIC_CAP_DISK_900G | SMIC_CAP_NET_1G;
    h->provisioned_at = 1760000000;
    h->disk_mb = role == SMIC_ROLE_MAINFRAME ? 921600 : 204800;
    h->net_mbps = role == SMIC_ROLE_MAINFRAME ? 1000 : 200;
    h->region = region;
    h->role = role;
    h->octane = 9;
    h->lpx = lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    h->arch = SMIC_ARCH_X86_64;
    h->flags = 0;
    h->fw_crc = 0x5A585601;
}

int main(void)
{
    /* ---- manifest pack/unpack round-trip ---- */
    smic_hw_t hw, hw2;
    uint8_t env[SMIC_ENV_BYTES];
    uint16_t type = 0;
    uint32_t i;

    mk_hw(&hw, 0x11, SMIC_REGION_MAINFRAME, SMIC_ROLE_MAINFRAME);
    CHECK(smic_manifest_pack(SMIC_T_MANIFEST, &hw, env) == SMIC_OK);
    CHECK(env[0] == 0x5A && env[1] == 0x58 && env[2] == 0x56); /* "ZXV" */
    memset(&hw2, 0, sizeof hw2); /* padding must compare equal */
    CHECK(smic_manifest_unpack(env, &type, &hw2) == SMIC_OK);
    CHECK(type == SMIC_T_MANIFEST);
    CHECK(!memcmp(&hw, &hw2, sizeof hw));
    CHECK(hw2.region == SMIC_REGION_MAINFRAME && hw2.role == SMIC_ROLE_MAINFRAME);
    CHECK(hw2.caps & SMIC_CAP_DISK_900G);

    /* malformed frames are refused: magic, schema, integrity, fields */
    env[0] ^= 0xFF;
    CHECK(smic_manifest_unpack(env, &type, &hw2) == SMIC_ERR_MALFORMED);
    env[0] ^= 0xFF;
    env[14] = 0; /* schema id */
    CHECK(smic_manifest_unpack(env, &type, &hw2) == SMIC_ERR_MALFORMED);
    mk_hw(&hw, 0x11, SMIC_REGION_MAINFRAME, SMIC_ROLE_MAINFRAME);
    smic_manifest_pack(SMIC_T_MANIFEST, &hw, env);
    env[SMIC_FRAME + 40] ^= 0x01; /* payload flip: digest no longer matches */
    CHECK(smic_manifest_unpack(env, &type, &hw2) == SMIC_ERR_MALFORMED);
    /* field checks: a well-framed, integrity-valid envelope whose payload
     * holds an out-of-range region is still refused. The packer refuses
     * bad fields, so craft it by hand: corrupt the region, then rebuild
     * the integrity trailer over the corrupted bytes. */
    smic_manifest_pack(SMIC_T_MANIFEST, &hw, env);
    env[SMIC_FRAME + 90] = 9; /* region out of range */
    {
        ubh_168_header_t h;
        uint8_t d[32];
        sha256(env, SMIC_FRAME + SMIC_PAYLOAD, d);
        ubh_168_header_init(&h, UBH_FRAME_INTEGRITY);
        h.source_format_id = UBH_FMT_UBH_168;
        h.target_type = SMIC_T_MANIFEST;
        h.payload_length = SMIC_PAYLOAD;
        h.schema_id = SMIC_PAYLOAD / SMIC_FRAME;
        for (i = 0; i < 3; i++) h.integrity_ref[i] = d[i];
        ubh_168_header_pack(&h, env + SMIC_FRAME + SMIC_PAYLOAD);
    }
    CHECK(smic_manifest_unpack(env, &type, &hw2) == SMIC_ERR_MALFORMED);

    /* bad args */
    CHECK(smic_manifest_pack(0x1234, &hw, env) == SMIC_ERR_ARG);
    hw.region = 42;
    CHECK(smic_manifest_pack(SMIC_T_MANIFEST, &hw, env) == SMIC_ERR_ARG);
    mk_hw(&hw, 0x11, 0, SMIC_ROLE_RELAY);
    hw.arch = SMIC_ARCH_NONE;
    CHECK(smic_manifest_pack(SMIC_T_MANIFEST, &hw, env) == SMIC_ERR_ARG);
    mk_hw(&hw, 0x11, 0, SMIC_ROLE_RELAY);

    /* ---- ML-DSA-87 attestation round-trip ---- */
    uint8_t pk[PQM_MLDSA87_PK_BYTES], sk[PQM_MLDSA87_SK_BYTES];
    uint8_t dg[SMIC_ATTEST_BYTES];
    uint8_t seed[32];
    uint32_t dlen = 0;

    memset(seed, 0x42, sizeof seed);
    pqm_mldsa87_keygen(seed, pk, sk);
    CHECK(smic_attest(&hw, SMIC_T_MANIFEST, pk, sk, dg, sizeof dg, &dlen) == SMIC_OK);
    CHECK(dlen == SMIC_ATTEST_BYTES);
    memset(&hw2, 0, sizeof hw2);
    CHECK(smic_attest_verify(dg, dlen, &type, &hw2) == SMIC_OK);
    CHECK(!memcmp(&hw, &hw2, sizeof hw));
    CHECK(type == SMIC_T_MANIFEST);

    /* a tampered envelope byte is caught by its own integrity trailer
     * before the signature is even checked */
    dg[SMIC_FRAME + 70] ^= 0x80;
    CHECK(smic_attest_verify(dg, dlen, &type, &hw2) == SMIC_ERR_MALFORMED);
    /* a tampered signature byte reaches the verifier and fails there */
    dg[SMIC_FRAME + 70] ^= 0x80;
    dg[SMIC_ATTEST_BYTES - 1] ^= 0x01;
    CHECK(smic_attest_verify(dg, dlen, &type, &hw2) == SMIC_ERR_SIG);
    CHECK(smic_attest_verify(dg, dlen - 1, &type, &hw2) == SMIC_ERR_ARG);
    CHECK(smic_attest(&hw, SMIC_T_MANIFEST, pk, sk, dg, SMIC_ENV_BYTES, &dlen) ==
          SMIC_ERR_SPACE);

    /* a different key's attestation does not verify under this node id:
     * the datagram binds pk+env, so re-signing the same envelope with a
     * second key verifies only with THAT key embedded — tamper proves it */
    {
        uint8_t pk2[PQM_MLDSA87_PK_BYTES], sk2[PQM_MLDSA87_SK_BYTES];
        uint8_t dg2[SMIC_ATTEST_BYTES];
        memset(seed, 0x43, sizeof seed);
        pqm_mldsa87_keygen(seed, pk2, sk2);
        CHECK(smic_attest(&hw, SMIC_T_MANIFEST, pk2, sk2, dg2, sizeof dg2, &dlen) ==
              SMIC_OK);
        /* swap the embedded pk: signature no longer verifies */
        memcpy(dg2 + SMIC_ENV_BYTES, pk, PQM_MLDSA87_PK_BYTES);
        CHECK(smic_attest_verify(dg2, dlen, &type, &hw2) == SMIC_ERR_SIG);
    }

    /* ---- ring admission ---- */
    smic_node_t ents[16];
    smic_t reg;
    smic_init(&reg, ents, 16);
    CHECK(reg.cap == 16 && reg.n == 0);

    /* the nine-node fleet admits: 8 relays + SIN mainframe */
    for (i = 0; i < SMIC_REGIONS; i++) {
        mk_hw(&hw, (uint8_t) (0x20 + i), (uint8_t) i,
              i == SMIC_REGION_MAINFRAME ? SMIC_ROLE_MAINFRAME : SMIC_ROLE_RELAY);
        CHECK(smic_admit(&reg, &hw, 1000 + i) == LPX_ACT_PROCEED);
    }
    CHECK(reg.n == 9);
    {
        uint8_t ring[SMIC_REGIONS];
        uint32_t total = 0;
        smic_ring(&reg, ring, &total);
        CHECK(total == 9);
        for (i = 0; i < SMIC_REGIONS; i++) CHECK(ring[i] == 1);
        CHECK(ring[SMIC_REGION_MAINFRAME] == 1);
    }

    /* a contradiction on node 5: verified then a failing probe lands
     * BOTH -> quarantine + escalate, out of the ring count */
    {
        uint8_t id5[32];
        uint8_t ring[SMIC_REGIONS];
        uint32_t total = 0;
        memset(id5, 0x25, 32);
        id5[31] = 5;
        CHECK(smic_health(&reg, id5, 0, 2000) == LPX_ACT_ESCALATE);
        CHECK(smic_find(&reg, id5)->quarantined == 1);
        smic_ring(&reg, ring, &total);
        CHECK(total == 8);
        CHECK(ring[5] == 0);
        /* the same probe agreeing again keeps BOTH (conflict on record):
         * the node waits out of the ring while evidence resolves */
        CHECK(smic_health(&reg, id5, 1, 2001) == LPX_ACT_WAIT);
        CHECK(smic_find(&reg, id5)->admitted == LPRES_STATE_BOTH);
        CHECK(smic_find(&reg, id5)->quarantined == 1);
    }

    /* an unattested node that only fails goes FALSE -> refuse */
    {
        uint8_t bad[32];
        memset(bad, 0xEE, 32);
        CHECK(smic_health(&reg, bad, 0, 3000) == LPX_ACT_WAIT); /* unknown: wait */
        CHECK(smic_admit(&reg, &(smic_hw_t){0}, 3001) == LPX_ACT_REFUSE); /* invalid */
    }

    /* operator forbid -> rollback; a fresh attestation is not a pardon */
    {
        uint8_t id0[32];
        memset(id0, 0x20, 32);
        id0[31] = 0;
        CHECK(smic_forbid(&reg, id0) == LPX_ACT_ROLLBACK);
        mk_hw(&hw, 0x20, 0, SMIC_ROLE_RELAY);
        CHECK(smic_admit(&reg, &hw, 4000) == LPX_ACT_ROLLBACK);
        /* operator clears -> pending again; next attestation re-admits */
        CHECK(smic_clear(&reg, id0) == LPX_ACT_WAIT);
        CHECK(smic_admit(&reg, &hw, 4001) == LPX_ACT_PROCEED);
    }

    /* staleness: node 8 silent past the window fades entropic -> wait */
    {
        uint8_t ring[SMIC_REGIONS];
        const smic_node_t *n8;
        uint8_t id8[32];
        uint32_t total = 0;
        memset(id8, 0x28, 32);
        id8[31] = 8;
        CHECK(smic_reap(&reg, 9000, 4000) >= 1);
        n8 = smic_find(&reg, id8);
        CHECK(n8 && lpx_pol(n8->health) == LPX_P_ENTROPIC);
        (void) ring;
        (void) total;
    }

    /* bounded: a tenth region or a full table refuses without touching */
    {
        smic_node_t two[2];
        smic_t tiny;
        smic_init(&tiny, two, 2);
        mk_hw(&hw, 0x51, 0, SMIC_ROLE_RELAY);
        CHECK(smic_admit(&tiny, &hw, 1) == LPX_ACT_PROCEED);
        mk_hw(&hw, 0x52, 1, SMIC_ROLE_RELAY);
        CHECK(smic_admit(&tiny, &hw, 2) == LPX_ACT_PROCEED);
        mk_hw(&hw, 0x53, 2, SMIC_ROLE_RELAY);
        CHECK(smic_admit(&tiny, &hw, 3) == LPX_ACT_REFUSE);
        CHECK(tiny.n == 2);
    }

    printf("smic: %d pass, %d fail\n", passes, failures);
    return failures ? 1 : 0;
}
