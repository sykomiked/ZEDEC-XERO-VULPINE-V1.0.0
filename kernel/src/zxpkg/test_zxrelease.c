/* test_zxrelease.c — a SIGNED native release: intact triad + real Ed25519.
 *
 * This closes the provenance gap zxpkg.h names: the seal proves integrity, and
 * a ZSP envelope over that seal proves the ROOT KEY released THIS triad. The
 * fixture (REL_PUB, REL_ZSP) was produced offline with openssl signing the seal
 * of the fixed triad below, and the kernel's own ed25519_verify accepts it — so
 * this is a real signature check, not a mock, and it runs with no openssl at
 * test time.
 */
#include <stdio.h>
#include <string.h>
#include "zxpkg.h"
#include "release_fixture.inc"   /* REL_PUB[32], REL_ZSP[136] */

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* the EXACT triad the fixture was signed over — must match genfix.c */
static void fixed_spec(zxpkg_spec_t *s) {
    static const unsigned char POS[] = "ZXV-RELEASE-TEST-IMAGE-v1";
    static const unsigned char NEG[] = "rollback: restore prior slot (generated, not proven)";
    static const unsigned char NEU[] = "unresolved: release policy pending";
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 32; i++) { s->triad_id[i]=(unsigned char)(i+1); s->source_graph_digest[i]=(unsigned char)(0x40+i); }
    s->inverse_kind = TRI_INV_RESTORING; s->irreversible = 0;
    s->payload[TRI_POSITIVE]=POS; s->payload_len[TRI_POSITIVE]=sizeof POS-1; s->capability[TRI_POSITIVE]=0x0F;
    s->payload[TRI_NEGATIVE]=NEG; s->payload_len[TRI_NEGATIVE]=sizeof NEG-1; s->capability[TRI_NEGATIVE]=0x03;
    s->generated[TRI_NEGATIVE]=1; s->claims_proven_inverse[TRI_NEGATIVE]=0;
    s->payload[TRI_NEUTRAL]=NEU; s->payload_len[TRI_NEUTRAL]=sizeof NEU-1; s->capability[TRI_NEUTRAL]=0;
}

int main(void) {
    printf("=== signed native release: triad + Ed25519 provenance ===\n");
    zxpkg_spec_t spec; fixed_spec(&spec);

    uint8_t seal[TRI_DIGEST_LEN];
    CHECK(zxpkg_seal(&spec, seal) == TRI_Q_NONE, "the fixed triad seals");

    static uint8_t fp[512], fn[512], fu[512];
    uint32_t lp = zxpkg_write(&spec, TRI_POSITIVE, seal, fp, sizeof fp);
    uint32_t ln = zxpkg_write(&spec, TRI_NEGATIVE, seal, fn, sizeof fn);
    uint32_t lu = zxpkg_write(&spec, TRI_NEUTRAL,  seal, fu, sizeof fu);
    CHECK(lp && ln && lu, "its three member files write");

    /* ---- THE POINT: a valid signed release verifies against the root key ---- */
    CHECK(zxpkg_verify_release(fp,lp, fn,ln, fu,lu, REL_ZSP, sizeof REL_ZSP, REL_PUB) == ZXREL_OK,
          "a genuinely Ed25519-signed release verifies — intact AND from the "
          "root key over THIS triad's seal");

    /* ---- a wrong root key rejects (not our signer) ---- */
    {
        uint8_t wrong[32]; memcpy(wrong, REL_PUB, 32); wrong[0] ^= 0x01;
        CHECK(zxpkg_verify_release(fp,lp, fn,ln, fu,lu, REL_ZSP, sizeof REL_ZSP, wrong) == ZXREL_BAD_SIG,
              "a DIFFERENT root key rejects the release (BAD_SIG)");
    }

    /* ---- a tampered signature rejects ---- */
    {
        static uint8_t z[512]; memcpy(z, REL_ZSP, sizeof REL_ZSP);
        z[45] ^= 0x01;   /* flip a byte inside the 64-byte signature */
        CHECK(zxpkg_verify_release(fp,lp, fn,ln, fu,lu, z, sizeof REL_ZSP, REL_PUB) == ZXREL_BAD_SIG,
              "a flipped signature byte rejects");
    }

    /* ---- a signature over a DIFFERENT seal rejects (SEAL_MISMATCH) ---- */
    {
        /* corrupt the ZSP payload (the trailing seal copy) so it no longer
         * matches the triad's seal, but keep the signature's own hash view
         * consistent by NOT re-signing — zsp_verify will fail hash first, so
         * to isolate SEAL_MISMATCH we instead alter the TRIAD to change its
         * seal while leaving the (correctly-signed) envelope intact. */
        zxpkg_spec_t s2; fixed_spec(&s2);
        s2.capability[TRI_POSITIVE] = 0x07;   /* different -> different seal */
        uint8_t seal2[TRI_DIGEST_LEN];
        zxpkg_seal(&s2, seal2);
        static uint8_t p2[512], n2[512], u2[512];
        uint32_t a=zxpkg_write(&s2,TRI_POSITIVE,seal2,p2,sizeof p2);
        uint32_t b=zxpkg_write(&s2,TRI_NEGATIVE,seal2,n2,sizeof n2);
        uint32_t c=zxpkg_write(&s2,TRI_NEUTRAL, seal2,u2,sizeof u2);
        zxrel_t r = zxpkg_verify_release(p2,a, n2,b, u2,c, REL_ZSP, sizeof REL_ZSP, REL_PUB);
        CHECK(r == ZXREL_SEAL_MISMATCH,
              "the signature does not cover THIS (altered) triad's seal -> "
              "SEAL_MISMATCH: you cannot move a valid signature onto another build");
    }

    /* ---- a tampered triad member rejects before we even reach the signature ---- */
    {
        static uint8_t bad[512]; memcpy(bad, fp, lp);
        bad[ZXPKG_HDR_LEN + 3] ^= 0x01;   /* flip a payload byte */
        CHECK(zxpkg_verify_release(bad,lp, fn,ln, fu,lu, REL_ZSP, sizeof REL_ZSP, REL_PUB) == ZXREL_TRIAD_BAD,
              "a tampered triad member fails as TRIAD_BAD");
    }

    /* ---- a truncated / absent envelope is UNSIGNED, not accepted ---- */
    CHECK(zxpkg_verify_release(fp,lp, fn,ln, fu,lu, REL_ZSP, 10, REL_PUB) == ZXREL_UNSIGNED,
          "a truncated envelope is UNSIGNED");
    CHECK(zxpkg_verify_release(fp,lp, fn,ln, fu,lu, 0, 0, REL_PUB) == ZXREL_UNSIGNED,
          "a missing envelope is UNSIGNED — a release is not trusted by default");

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
