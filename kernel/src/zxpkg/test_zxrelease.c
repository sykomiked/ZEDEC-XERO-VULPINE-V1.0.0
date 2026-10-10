/* test_zxrelease.c — a SIGNED native release: intact triad + Ed25519 over the
 * complete release manifest.
 *
 * The seal proves integrity; a ZSP v2 envelope over that seal, whose signed
 * header carries the version and architecture and whose identity is the
 * canonical manifest digest (zxpkg_release_identity), proves the ROOT KEY
 * released THIS triad as THIS version for THIS architecture. Envelopes are
 * built and signed here at run time with a test-only key from a fixed public
 * seed (src/provenance/test_signer.h): no private key is in the tree. The old
 * offline v1 fixture (release_fixture.inc, signature over the seal only, no
 * version) is kept to show it is now refused.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "zxpkg.h"
#include "sha256.h"
#include "../loader/zsp.h"
#include "../provenance/test_signer.h"
#include "release_fixture.inc" /* REL_PUB[32], REL_ZSP[136]: legacy v1 envelope */

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* the EXACT triad the fixture was signed over — must match genfix.c */
static void fixed_spec(zxpkg_spec_t *s)
{
    static const unsigned char POS[] = "ZXV-RELEASE-TEST-IMAGE-v1";
    static const unsigned char NEG[] = "rollback: restore prior slot (generated, not proven)";
    static const unsigned char NEU[] = "unresolved: release policy pending";
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 32; i++) {
        s->triad_id[i] = (unsigned char) (i + 1);
        s->source_graph_digest[i] = (unsigned char) (0x40 + i);
    }
    s->inverse_kind = TRI_INV_RESTORING;
    s->irreversible = 0;
    s->payload[TRI_POSITIVE] = POS;
    s->payload_len[TRI_POSITIVE] = sizeof POS - 1;
    s->capability[TRI_POSITIVE] = 0x0F;
    s->payload[TRI_NEGATIVE] = NEG;
    s->payload_len[TRI_NEGATIVE] = sizeof NEG - 1;
    s->capability[TRI_NEGATIVE] = 0x03;
    s->generated[TRI_NEGATIVE] = 1;
    s->claims_proven_inverse[TRI_NEGATIVE] = 0;
    s->payload[TRI_NEUTRAL] = NEU;
    s->payload_len[TRI_NEUTRAL] = sizeof NEU - 1;
    s->capability[TRI_NEUTRAL] = 0;
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}
static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* What the offline signer does: a ZSP v2 envelope whose payload is the seal and
 * whose identity is `ident` (normally zxpkg_release_identity). */
static uint32_t make_release(const test_signer_t *s, uint32_t version, uint16_t arch,
                             const uint8_t seal[32], const uint8_t ident[32], uint8_t *out)
{
    memset(out, 0, ZSP2_HEADER_LEN);
    memcpy(out, "ZSP2", 4);
    le16(out + 4, 96);
    le16(out + 6, 1);
    le32(out + 8, version);
    le16(out + 12, arch);
    le32(out + 20, 32);
    uint8_t kd[32];
    sha256(s->pk, 32, kd);
    memcpy(out + 24, kd, 8);
    memcpy(out + 32, ident, 32);
    sha256(seal, 32, out + 64);
    test_signer_sign(s, out, ZSP2_PREIMAGE_LEN, out + ZSP2_PREIMAGE_LEN);
    memcpy(out + ZSP2_HEADER_LEN, seal, 32);
    return ZSP2_HEADER_LEN + 32;
}

int main(void)
{
    printf("=== signed native release: triad + Ed25519 provenance ===\n");
    zxpkg_spec_t spec;
    fixed_spec(&spec);

    uint8_t seal[TRI_DIGEST_LEN];
    CHECK(zxpkg_seal(&spec, seal) == TRI_Q_NONE, "the fixed triad seals");

    static uint8_t fp[512], fn[512], fu[512];
    uint32_t lp = zxpkg_write(&spec, TRI_POSITIVE, seal, fp, sizeof fp);
    uint32_t ln = zxpkg_write(&spec, TRI_NEGATIVE, seal, fn, sizeof fn);
    uint32_t lu = zxpkg_write(&spec, TRI_NEUTRAL, seal, fu, sizeof fu);
    CHECK(lp && ln && lu, "its three member files write");

    test_signer_t root;
    test_signer_init(&root, 0x81);
    uint8_t ident[32];
    CHECK(zxpkg_release_identity(spec.triad_id, 7, ZSP_ARCH_ARM64, seal, ident),
          "release identity = canonical digest of (triad id, version, arch, seal)");
    static uint8_t rel[256];
    uint32_t rl = make_release(&root, 7, ZSP_ARCH_ARM64, seal, ident, rel);
    uint32_t ver = 0;

    /* ---- THE POINT: a valid signed release verifies against the root key ---- */
    CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, rel, rl, root.pk, 7, ZSP_ARCH_ARM64, &ver) ==
                  ZXREL_OK &&
              ver == 7,
          "a genuinely Ed25519-signed v2 release verifies — intact AND from the root key "
          "over THIS triad's seal, version and arch");

    /* ---- version floor and architecture are enforced ---- */
    CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, rel, rl, root.pk, 8, ZSP_ARCH_ARM64, 0) ==
              ZXREL_ROLLBACK,
          "a release below the caller's version floor is ROLLBACK");
    CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, rel, rl, root.pk, 0, ZSP_ARCH_X86_64, 0) ==
              ZXREL_ARCH,
          "a release for another architecture is refused");

    /* ---- every manifest field is bound: edit the header and re-sign it, keeping the
     * identity, and the identity no longer matches ---- */
    {
        static uint8_t r2[256];
        uint32_t n2 = make_release(&root, 9, ZSP_ARCH_ARM64, seal, ident, r2);
        CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, r2, n2, root.pk, 0, ZSP_ARCH_ANY, 0) ==
                  ZXREL_MANIFEST_MISMATCH,
              "version changed under an identity for another version: MANIFEST_MISMATCH");
        n2 = make_release(&root, 7, ZSP_ARCH_X86_64, seal, ident, r2);
        CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, r2, n2, root.pk, 0, ZSP_ARCH_ANY, 0) ==
                  ZXREL_MANIFEST_MISMATCH,
              "arch changed under an identity for another arch: MANIFEST_MISMATCH");
        uint8_t other_id[32];
        uint8_t tid[32];
        memcpy(tid, spec.triad_id, 32);
        tid[0] ^= 1;
        zxpkg_release_identity(tid, 7, ZSP_ARCH_ARM64, seal, other_id);
        n2 = make_release(&root, 7, ZSP_ARCH_ARM64, seal, other_id, r2);
        CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, r2, n2, root.pk, 0, ZSP_ARCH_ANY, 0) ==
                  ZXREL_MANIFEST_MISMATCH,
              "an identity for another package id: MANIFEST_MISMATCH");
        /* editing the signed header WITHOUT re-signing breaks the signature */
        memcpy(r2, rel, rl);
        r2[8] = 8; /* version 7 -> 8 */
        CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, r2, rl, root.pk, 0, ZSP_ARCH_ANY, 0) ==
                  ZXREL_BAD_SIG,
              "a version edited in a signed envelope breaks the signature");
        memcpy(r2, rel, rl);
        r2[12] = ZSP_ARCH_X86_64;
        CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, r2, rl, root.pk, 0, ZSP_ARCH_ANY, 0) ==
                  ZXREL_BAD_SIG,
              "an arch edited in a signed envelope breaks the signature");
    }

    /* ---- a wrong root key rejects (not our signer) ---- */
    {
        test_signer_t other;
        test_signer_init(&other, 0x82);
        CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, rel, rl, other.pk, 0, ZSP_ARCH_ANY, 0) ==
                  ZXREL_BAD_SIG,
              "a DIFFERENT root key rejects the release (BAD_SIG)");
    }

    /* ---- a tampered signature rejects ---- */
    {
        static uint8_t z[256];
        memcpy(z, rel, rl);
        z[ZSP2_PREIMAGE_LEN + 5] ^= 0x01;
        CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, z, rl, root.pk, 0, ZSP_ARCH_ANY, 0) ==
                  ZXREL_BAD_SIG,
              "a flipped signature byte rejects");
    }

    /* ---- a signature over a DIFFERENT seal rejects (SEAL_MISMATCH) ---- */
    {
        zxpkg_spec_t s2;
        fixed_spec(&s2);
        s2.capability[TRI_POSITIVE] = 0x07; /* different -> different seal */
        uint8_t seal2[TRI_DIGEST_LEN];
        zxpkg_seal(&s2, seal2);
        static uint8_t p2[512], n2[512], u2[512];
        uint32_t a = zxpkg_write(&s2, TRI_POSITIVE, seal2, p2, sizeof p2);
        uint32_t b = zxpkg_write(&s2, TRI_NEGATIVE, seal2, n2, sizeof n2);
        uint32_t c = zxpkg_write(&s2, TRI_NEUTRAL, seal2, u2, sizeof u2);
        zxrel_t r = zxpkg_verify_release(p2, a, n2, b, u2, c, rel, rl, root.pk, 0, ZSP_ARCH_ANY, 0);
        CHECK(r == ZXREL_SEAL_MISMATCH,
              "the signature does not cover THIS (altered) triad's seal -> "
              "SEAL_MISMATCH: you cannot move a valid signature onto another build");
    }

    /* ---- a tampered triad member rejects before we even reach the signature ---- */
    {
        static uint8_t bad[512];
        memcpy(bad, fp, lp);
        bad[ZXPKG_HDR_LEN + 3] ^= 0x01; /* flip a payload byte */
        CHECK(zxpkg_verify_release(bad, lp, fn, ln, fu, lu, rel, rl, root.pk, 0, ZSP_ARCH_ANY, 0) ==
                  ZXREL_TRIAD_BAD,
              "a tampered triad member fails as TRIAD_BAD");
    }

    /* ---- legacy, truncated or absent envelopes are UNSIGNED, not accepted ---- */
    CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, REL_ZSP, sizeof REL_ZSP, REL_PUB, 0,
                               ZSP_ARCH_ANY, 0) == ZXREL_UNSIGNED,
          "the legacy v1 envelope (seal only, no signed version) is UNSIGNED");
    CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, rel, 10, root.pk, 0, ZSP_ARCH_ANY, 0) ==
              ZXREL_UNSIGNED,
          "a truncated envelope is UNSIGNED");
    CHECK(zxpkg_verify_release(fp, lp, fn, ln, fu, lu, 0, 0, root.pk, 0, ZSP_ARCH_ANY, 0) ==
              ZXREL_UNSIGNED,
          "a missing envelope is UNSIGNED — a release is not trusted by default");

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
