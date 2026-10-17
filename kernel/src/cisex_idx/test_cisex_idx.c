/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cisex_idx.c — host test for the CISEX global IPFS index: passive
 * wantlist stubs, dag-pb link harvesting, magic-byte/entropy peeks,
 * paraconsistent 8-region provider health, EpistemicRank search, bounties,
 * and the 168-byte UBH-168 index envelope merge. */

#include <stdio.h>
#include <string.h>
#include "cisex_idx.h"
#include "cisex_rt.h"
#include "../robin_debanks/sha256.h"

static int fails, checks;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
        }                                                                                          \
    } while (0)

/* ---- tiny protobuf writer (PBNode: Links field 2, then Data field 1) --- */
static uint32_t put_varint(uint8_t *p, uint64_t v)
{
    uint32_t n = 0;
    do {
        p[n] = (uint8_t) (v & 0x7F);
        v >>= 7;
        if (v) p[n] |= 0x80;
        n++;
    } while (v);
    return n;
}

static uint32_t put_field_bytes(uint8_t *p, uint8_t field, const uint8_t *d, uint32_t n)
{
    uint32_t o = put_varint(p, (uint64_t) field << 3 | 2u);
    o += put_varint(p + o, n);
    memcpy(p + o, d, n);
    return o + n;
}

static uint32_t put_field_varint(uint8_t *p, uint8_t field, uint64_t v)
{
    uint32_t o = put_varint(p, (uint64_t) field << 3);
    return o + put_varint(p + o, v);
}

static void mk_digest(uint8_t seed, uint8_t d[32])
{
    uint8_t s[4] = {seed, (uint8_t) (seed ^ 0x5A), (uint8_t) (seed + 1), (uint8_t) (seed * 7)};
    sha256(s, sizeof s, d);
}

/* CIDv1 binary: version(1) codec(1) sha2-256(0x12) len(0x20) digest(32) */
static uint32_t put_cidbin(uint8_t *p, uint8_t codec, const uint8_t d[32])
{
    p[0] = 1;
    p[1] = codec;
    p[2] = 0x12;
    p[3] = 0x20;
    memcpy(p + 4, d, 32);
    return 36;
}

/* Build a UnixFS directory block with `n` named links. */
static uint32_t build_dir(uint8_t *blk, const char **names, const uint8_t digs[][32],
                          const uint64_t *tsize, uint32_t n)
{
    uint8_t *p = blk, tmp[512], ufs[2] = {0x08, 0x01};
    uint32_t tl;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *t = tmp;
        uint8_t cbin[36];
        t += put_field_bytes(t, 1, cbin, put_cidbin(cbin, IPFSN_MC_RAW, digs[i]));
        t += put_field_bytes(t, 2, (const uint8_t *) names[i], (uint32_t) strlen(names[i]));
        t += put_field_varint(t, 3, tsize[i]);
        tl = (uint32_t) (t - tmp);
        p += put_field_bytes(p, 2, tmp, tl);
    }
    p += put_field_bytes(p, 1, ufs, 2);
    return (uint32_t) (p - blk);
}

static cisex_t X;
static cisex_ent_t ENTS[64];

static uint32_t idx_of(const cisex_ent_t *e)
{
    return (uint32_t) (e - X.e);
}

int main(void)
{
    uint8_t blk[2048], dA[32], dB[32], dC[32], dD[32], dW[32], hdr[4096];
    ipfsn_cid_t root;
    const char *names[4] = {"quantum-gravity.pdf", "scheduler.c", "dataset.csv",
                            "paper.TEX"};
    uint8_t digs[4][32];
    uint64_t tsz[4] = {500000, 4200, 9000000, 31415};
    uint32_t nl = 0, out[8];
    cisex_ent_t *eA, *eB, *eC, *eD, *eW;
    uint8_t k = 0;
    uint16_t ent = 0;

    /* ---- entropy: 0 for silence, exactly 8 bits for uniform 256 symbols - */
    memset(hdr, 0, sizeof hdr);
    CHECK(cisex_entropy_x1024(hdr, 4096) == 0, "entropy of zeros is 0");
    for (uint32_t i = 0; i < 4096; i++) hdr[i] = (uint8_t) (i & 0xFF);
    CHECK(cisex_entropy_x1024(hdr, 4096) == 8192, "uniform 256-symbol entropy is 8192 x1024");

    /* ---- keyword hashing is deterministic and discriminative ----------- */
    CHECK(cisex_kw_hash("quantum", 7) != 0, "kw hash nonzero");
    CHECK(cisex_kw_hash("quantum", 7) == cisex_kw_hash("quantum", 7), "kw hash deterministic");
    CHECK(cisex_kw_hash("quantum", 7) != cisex_kw_hash("gravity", 7), "kw hash discriminates");

    mk_digest(0x11, dA);
    mk_digest(0x22, dB);
    mk_digest(0x33, dC);
    mk_digest(0x44, dD);
    mk_digest(0x55, dW);
    memcpy(digs[0], dA, 32);
    memcpy(digs[1], dB, 32);
    memcpy(digs[2], dC, 32);
    memcpy(digs[3], dD, 32);

    cisex_init(&X, ENTS, 64);

    /* ---- passive wantlist sniffing: a NEITHER/POTENTIAL stub ----------- */
    CHECK(cisex_want(&X, dW, 2) == IPFSN_OK, "want ok");
    eW = cisex_find(&X, dW);
    CHECK(eW && eW->wants == 1, "want stub has one demand");
    CHECK(lpx_base(eW->lpx) == LPX_B_NEITHER && lpx_phase(eW->lpx) == LPX_PH_POTENTIAL,
          "want stub is NEITHER/POTENTIAL");
    CHECK(cisex_want(&X, dW, 2) == IPFSN_OK && eW->wants == 2, "demand accumulates");

    /* ---- shallow dag-pb harvesting: links are citations ----------------- */
    uint32_t blen = build_dir(blk, names, digs, tsz, 4);
    CHECK(ipfsn_cid_sha256(IPFSN_MC_DAG_PB, blk, blen, &root) == IPFSN_OK, "root cid built");
    CHECK(cisex_index_dagpb(&X, &root, blk, blen, 0, &nl) == IPFSN_OK && nl == 4,
          "dag-pb block harvested, 4 links");

    cisex_ent_t *parent = cisex_find(&X, root.digest);
    CHECK(parent && parent->kind == CISEX_K_DIR && parent->crawled, "parent is a crawled DIR");
    CHECK(lpx_phase(parent->lpx) == LPX_PH_EMERGING, "parent walks to EMERGING");

    eA = cisex_find(&X, dA);
    eB = cisex_find(&X, dB);
    eC = cisex_find(&X, dC);
    eD = cisex_find(&X, dD);
    CHECK(eA && eB && eC && eD, "all four children indexed");
    CHECK(eA->cites == 1 && eB->cites == 1 && eC->cites == 1 && eD->cites == 1,
          "each child cited once");
    CHECK(eA->size == 500000 && eD->size == 31415, "link Tsize harvested");
    CHECK(strcmp(eA->name, "quantum-gravity.pdf") == 0, "child name harvested");
    CHECK(eA->n_kw >= 2 && eA->kw[0] == cisex_kw_hash("quantum", 7), "keyword harvested");

    /* idempotent: re-scanning the same block must not double-cite */
    CHECK(cisex_index_dagpb(&X, &root, blk, blen, 0, &nl) == IPFSN_OK,
          "re-scan is accepted");
    CHECK(eA->cites == 1 && eD->cites == 1, "re-scan does not double-cite");

    /* a block that does not hash to its CID is refused and cites nothing */
    blk[40] ^= 0xFF;
    CHECK(cisex_index_dagpb(&X, &root, blk, blen, 0, &nl) == IPFSN_ERR_HASH,
          "forged block refused (hash mismatch)");
    blk[40] ^= 0xFF;
    CHECK(eA->cites == 1, "forged block cites nothing");

    /* ---- first-chunk peek: magic bytes + extension fallback ------------- */
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "%PDF-1.7 stream", 15);
    CHECK(cisex_peek(&X, dA, hdr, 15, 0, &k, &ent) == IPFSN_OK && k == CISEX_K_PDF,
          "magic byte peek classifies PDF");
    CHECK(eA->entropy_x1024 > 0, "peek records entropy");

    memcpy(hdr, "plain words with no magic signature at all", 43);
    CHECK(cisex_peek(&X, dD, hdr, 43, 1, &k, &ent) == IPFSN_OK && k == CISEX_K_TEX,
          "extension .TEX classifies without magic");

    /* ---- paraconsistent 8-region provider health ------------------------ */
    CHECK(cisex_observe(&X, dA, 0, LPRES_STATE_TRUE) == IPFSN_OK, "region 0 verifies A");
    CHECK(eA->armada == 0x01 && lpx_base(eA->lpx) == LPX_B_TRUE, "armada bit + TRUE base");
    CHECK(lpx_phase(eA->lpx) == LPX_PH_EMERGING, "first observation walks to EMERGING");

    uint8_t addr[6] = {203, 0, 113, 7, 0x22, 0x0C}; /* 203.0.113.7:8724 */
    CHECK(cisex_provide(&X, dA, 3, addr) == IPFSN_OK, "provider record joins TRUE");
    CHECK(eA->armada == 0x09 && eA->n_prov == 1, "two regions, one endpoint");
    CHECK(lpx_phase(eA->lpx) == LPX_PH_MANIFEST, "multi-region TRUE walks to MANIFEST");

    /* region 5 disagrees: a real BOTH, never silently resolved */
    CHECK(cisex_observe(&X, dA, 5, LPRES_STATE_FALSE) == IPFSN_OK, "region 5 dissents");
    CHECK(lpx_base(eA->lpx) == LPX_B_BOTH, "regional disagreement is BOTH");
    CHECK(eA->armada == 0x09, "armada mask still reports the two TRUE regions");

    /* a healthy cited entry: TRUE in two regions, no contradiction */
    CHECK(cisex_observe(&X, dC, 1, LPRES_STATE_TRUE) == IPFSN_OK, "C verified r1");
    CHECK(cisex_observe(&X, dC, 6, LPRES_STATE_TRUE) == IPFSN_OK, "C verified r6");
    CHECK(lpx_base(eC->lpx) == LPX_B_TRUE && lpx_phase(eC->lpx) == LPX_PH_MANIFEST,
          "C is manifest TRUE");

    /* ---- EpistemicRank + search ---------------------------------------- */
    CHECK(cisex_rank(eC) > cisex_rank(eB), "served+cited outranks cited-only");
    CHECK(cisex_rank(eB) == 13, "cited-only rank is the citation term");
    eB = cisex_find(&X, dB); /* (re-alias for clarity below) */

    uint32_t got = cisex_search(&X, "quantum", 7, out, 8);
    CHECK(got >= 1 && out[0] == idx_of(eA), "search 'quantum' hits the PDF");
    got = cisex_search(&X, "scheduler", 9, out, 8);
    CHECK(got >= 1 && out[0] == idx_of(eB), "search 'scheduler' hits the C file");

    /* ---- missing-block bounties ----------------------------------------- */
    uint32_t nb = cisex_bounty(&X, out, 8);
    CHECK(nb >= 2, "unserved cited entries are bounties");
    bool b_is_bounty = false, c_is_bounty = false;
    for (uint32_t i = 0; i < nb; i++) {
        b_is_bounty |= out[i] == idx_of(eB);
        c_is_bounty |= out[i] == idx_of(eC);
    }
    CHECK(b_is_bounty, "cited-but-unserved B is a bounty");
    CHECK(!c_is_bounty, "healthy C is not a bounty");

    /* ---- the 168-byte UBH-168 index envelope ---------------------------- */
    uint8_t env[CISEX_ENV_BYTES];
    cisex_wire_t w;
    CHECK(cisex_pack_idx(eC, 1, env) == IPFSN_OK, "pack index envelope");
    CHECK(cisex_unpack_idx(env, &w) == IPFSN_OK, "unpack index envelope");
    CHECK(memcmp(w.digest, dC, 32) == 0 && w.cites == eC->cites && w.armada == eC->armada,
          "wire record round-trips");
    env[60] ^= 0x01;
    CHECK(cisex_unpack_idx(env, &w) != IPFSN_OK, "tampered envelope refused");
    env[60] ^= 0x01;

    /* merge into a second node's table: region-1 view joins, scalars max */
    cisex_t Y;
    cisex_ent_t ents2[32];
    cisex_init(&Y, ents2, 32);
    CHECK(cisex_merge(&Y, env) == IPFSN_OK, "armada record merges");
    cisex_ent_t *eC2 = cisex_find(&Y, dC);
    CHECK(eC2 && eC2->cites == eC->cites && eC2->kind == eC->kind, "merged scalar fields");
    CHECK(eC2->armada == eC->armada, "merged armada mask");
    CHECK(lpx_base(eC2->lpx) == LPX_B_TRUE, "merged health is TRUE");
    CHECK(eC2->n_kw == eC->n_kw, "merged keywords");
    /* idempotent: a second merge of the same record changes nothing */
    CHECK(cisex_merge(&Y, env) == IPFSN_OK, "re-merge accepted");
    CHECK(eC2->cites == eC->cites && eC2->armada == eC->armada &&
              lpx_base(eC2->lpx) == LPX_B_TRUE,
          "re-merge is idempotent");

    /* contradictory merge: a peer claiming FALSE for a TRUE CID -> BOTH */
    cisex_ent_t fake = *eC;
    uint8_t env2[CISEX_ENV_BYTES];
    cisex_wire_t w2;
    fake.lpx = lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    fake.armada = 0;
    CHECK(cisex_pack_idx(&fake, 4, env2) == IPFSN_OK, "pack dissenting record");
    CHECK(cisex_merge(&Y, env2) == IPFSN_OK, "dissent merges");
    CHECK(cisex_unpack_idx(env2, &w2) == IPFSN_OK && w2.region == 4, "origin is region 4");
    CHECK(lpx_base(eC2->lpx) == LPX_B_BOTH, "disagreement surfaces as BOTH");
    CHECK(eC2->armada == eC->armada, "TRUE regions survive the dispute");

    /* ---- delegated-routing responder (cisex_rt) ----------------------- */
    {
        uint8_t q[256], rsp[1024];
        uint32_t rlen;
        const char kw[] = "quantum";

        /* WANT: passive wantlist sniff acks */
        q[0] = CISEX_OP_WANT;
        q[1] = 2;
        mk_digest(0x77, q + 2);
        {
            uint32_t w0 = X.n_wants;
            CHECK(cisex_rt_handle(&X, q, 34, 0, rsp, sizeof rsp, &rlen) == IPFSN_OK &&
                      rlen == 1 && rsp[0] == CISEX_ACK_OK && X.n_wants > w0,
                  "WANT acks and records demand");
        }

        /* KQ: keyword query returns ranked envelopes */
        q[0] = CISEX_OP_KQ;
        q[1] = 0;
        q[2] = 7;
        memcpy(q + 3, kw, 7);
        CHECK(cisex_rt_handle(&X, q, 10, 0, rsp, sizeof rsp, &rlen) == IPFSN_OK &&
                  rsp[0] == CISEX_RSP_KQ && rsp[1] >= 1 && rlen == 2 + rsp[1] * CISEX_ENV_BYTES,
              "KQ carries envelopes");
        {
            cisex_wire_t wr;
            CHECK(cisex_unpack_idx(rsp + 2, &wr) == IPFSN_OK && memcmp(wr.digest, dA, 32) == 0,
                  "top KQ hit is the quantum PDF");
        }

        /* DQ hit */
        q[0] = CISEX_OP_DQ;
        q[1] = 5;
        memcpy(q + 2, dC, 32);
        CHECK(cisex_rt_handle(&X, q, 34, 1, rsp, sizeof rsp, &rlen) == IPFSN_OK &&
                  rsp[0] == CISEX_RSP_DQ && rsp[1] == 1 && rlen == 2 + CISEX_ENV_BYTES,
              "DQ hit carries one envelope");
        {
            cisex_wire_t wr;
            CHECK(cisex_unpack_idx(rsp + 2, &wr) == IPFSN_OK && memcmp(wr.digest, dC, 32) == 0 &&
                      wr.region == 1,
                  "DQ envelope signed by the serving region");
        }

        /* DQ miss: empty reply, demand recorded, no fabricated entry */
        {
            uint8_t dz[32];
            uint32_t w0;
            mk_digest(0x99, dz);
            w0 = X.n_wants;
            q[0] = CISEX_OP_DQ;
            q[1] = 3;
            memcpy(q + 2, dz, 32);
            CHECK(cisex_rt_handle(&X, q, 34, 1, rsp, sizeof rsp, &rlen) == IPFSN_OK &&
                      rsp[0] == CISEX_RSP_DQ && rsp[1] == 0 && rlen == 2,
                  "DQ miss is an empty reply, not an error");
            {
                const cisex_ent_t *stub = cisex_find(&X, dz);
                CHECK(stub && stub->cites == 0 && stub->wants == 1 &&
                          lpx_base(stub->lpx) == LPX_B_NEITHER,
                      "DQ miss leaves a NEITHER want stub, not a fabricated entry");
            }
            CHECK(X.n_wants == w0 + 1, "DQ miss is learned as demand");
        }

        /* IDX: armada sync merges into the responder's index */
        {
            uint8_t dz2[32], ev[CISEX_ENV_BYTES];
            cisex_ent_t fe;
            mk_digest(0x55, dz2);
            memset(&fe, 0, sizeof fe);
            fe.used = 1;
            memcpy(fe.digest, dz2, 32);
            fe.size = 1234;
            fe.cites = 2;
            fe.kind = CISEX_K_PDF;
            fe.lpx = lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
            fe.armada = 0x40;
            CHECK(cisex_pack_idx(&fe, 6, ev) == IPFSN_OK, "pack remote record");
            q[0] = CISEX_OP_IDX;
            q[1] = 6;
            memcpy(q + 2, ev, CISEX_ENV_BYTES);
            CHECK(cisex_rt_handle(&X, q, 2 + CISEX_ENV_BYTES, 0, rsp, sizeof rsp, &rlen) ==
                          IPFSN_OK &&
                      rsp[0] == CISEX_ACK_OK && rlen == 1,
                  "IDX merge acks");
            CHECK(cisex_find(&X, dz2) != NULL, "IDX record lands in the index");
            /* a corrupted envelope is refused with ACK_BAD */
            q[2 + 40] ^= 0xFF;
            CHECK(cisex_rt_handle(&X, q, 2 + CISEX_ENV_BYTES, 0, rsp, sizeof rsp, &rlen) !=
                          IPFSN_OK &&
                      rsp[0] == CISEX_ACK_BAD,
                  "forged IDX refused with ACK_BAD");
        }

        /* BOUNTY: the cited-but-unserved surface */
        q[0] = CISEX_OP_BOUNTY;
        q[1] = 0;
        CHECK(cisex_rt_handle(&X, q, 2, 0, rsp, sizeof rsp, &rlen) == IPFSN_OK &&
                  rsp[0] == CISEX_RSP_BOUNTY && rsp[1] >= 1,
              "bounty list is nonempty");
        {
            int found_b = 0;
            for (uint32_t i = 0; i < rsp[1]; i++) {
                cisex_wire_t wr;
                if (cisex_unpack_idx(rsp + 2 + i * CISEX_ENV_BYTES, &wr) == IPFSN_OK &&
                    memcmp(wr.digest, dB, 32) == 0)
                    found_b = 1;
            }
            CHECK(found_b, "cited-but-unserved B surfaces as a bounty");
        }

        /* malformed datagrams are dropped without a reply */
        q[0] = 0xEE;
        q[1] = 0;
        CHECK(cisex_rt_handle(&X, q, 2, 0, rsp, sizeof rsp, &rlen) != IPFSN_OK && rlen == 0,
              "unknown op dropped");
        CHECK(cisex_rt_handle(&X, q, 1, 0, rsp, sizeof rsp, &rlen) != IPFSN_OK,
              "truncated datagram dropped");
        q[0] = CISEX_OP_WANT;
        q[1] = 9; /* region out of range */
        CHECK(cisex_rt_handle(&X, q, 34, 0, rsp, sizeof rsp, &rlen) != IPFSN_OK,
              "bad region dropped");
        /* a reply buffer too small for one envelope is an error, not a
         * truncated reply a peer could misparse */
        q[0] = CISEX_OP_KQ;
        q[1] = 0;
        q[2] = 7;
        memcpy(q + 3, kw, 7);
        CHECK(cisex_rt_handle(&X, q, 10, 0, rsp, CISEX_ENV_BYTES, &rlen) != IPFSN_OK,
              "undersized reply buffer refused");
    }

    printf("%s: %d checks, %d failures\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
