/* test_zxpkg.c — the native package format: a compiled artifact as a bound
 * Tri-Space triad on disk.
 *
 * The properties that matter: a payload cannot be altered without detection,
 * a member cannot be lifted from another build without breaking the seal, and
 * the five Tri-Space requirements are enforced at pack time — a triad whose
 * undo path over-reaches, or whose neutral half can act on the world, does not
 * seal at all.
 */
#include <stdio.h>
#include <string.h>
#include "zxpkg.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* a well-formed triad: S+ carries an image, S- is a generated rollback with a
 * subset capability and an honest (constraining) inverse, S0 is empty. */
static void good_spec(zxpkg_spec_t *s, const uint8_t *pos, uint32_t posn, const uint8_t *neg,
                      uint32_t negn)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 32; i++) {
        s->triad_id[i] = (uint8_t) (i + 1);
        s->source_graph_digest[i] = (uint8_t) (0x40 + i);
    }
    s->inverse_kind = TRI_INV_CONSTRAINING; /* honest for a kernel image */
    s->irreversible = false;
    s->payload[TRI_POSITIVE] = pos;
    s->payload_len[TRI_POSITIVE] = posn;
    s->capability[TRI_POSITIVE] = 0x0F; /* the action's powers */
    s->payload[TRI_NEGATIVE] = neg;
    s->payload_len[TRI_NEGATIVE] = negn;
    s->capability[TRI_NEGATIVE] = 0x03; /* subset of S+ */
    s->generated[TRI_NEGATIVE] = true;
    s->claims_proven_inverse[TRI_NEGATIVE] = false; /* a generated draft */
    s->payload[TRI_NEUTRAL] = (const uint8_t *) "unresolved: signing policy";
    s->payload_len[TRI_NEUTRAL] = 26;
    s->capability[TRI_NEUTRAL] = 0; /* no production effect */
}

int main(void)
{
    printf("=== zxpkg — a compiled artifact as a bound Tri-Space triad ===\n");

    const uint8_t image[] = "\x7f"
                            "ELF...this stands in for a compiled kernel image...";
    const uint8_t undo[] = "rollback: restore prior A/B slot; revoke installed seals";
    uint32_t imgn = (uint32_t) sizeof image - 1, undon = (uint32_t) sizeof undo - 1;

    zxpkg_spec_t spec;
    good_spec(&spec, image, imgn, undo, undon);

    /* ---- seal ---- */
    uint8_t seal[TRI_DIGEST_LEN];
    CHECK(zxpkg_seal(&spec, seal) == TRI_Q_NONE, "a well-formed triad seals cleanly");
    bool nonzero = false;
    for (int i = 0; i < 32; i++)
        if (seal[i]) nonzero = true;
    CHECK(nonzero, "the seal is not all-zero");

    /* ---- extensions ---- */
    CHECK(strcmp(zxpkg_extension(TRI_POSITIVE), ".zxvc") == 0, "S+ compiles to .zxvc");
    CHECK(strcmp(zxpkg_extension(TRI_NEGATIVE), ".cedez") == 0, "S- compiles to .cedez");
    CHECK(strcmp(zxpkg_extension(TRI_NEUTRAL), ".cedec") == 0, "S0 compiles to .cedec");

    /* ---- write the three member files ---- */
    static uint8_t fpos[512], fneg[512], fneu[512];
    uint32_t lp = zxpkg_write(&spec, TRI_POSITIVE, seal, fpos, sizeof fpos);
    uint32_t ln = zxpkg_write(&spec, TRI_NEGATIVE, seal, fneg, sizeof fneg);
    uint32_t lu = zxpkg_write(&spec, TRI_NEUTRAL, seal, fneu, sizeof fneu);
    CHECK(lp == ZXPKG_HDR_LEN + imgn, "the S+ file is header + payload");
    CHECK(ln > ZXPKG_HDR_LEN && lu > ZXPKG_HDR_LEN, "S- and S0 files are written");

    /* ---- read one back ---- */
    zxpkg_member_t m;
    CHECK(zxpkg_read(fpos, lp, &m), "the S+ member reads back");
    CHECK(m.role == TRI_POSITIVE && m.payload_len == imgn, "role and length survive");
    CHECK(memcmp(m.payload, image, imgn) == 0, "the payload is byte-identical");
    CHECK(m.capability_set == 0x0F, "the capability set survives");
    CHECK(memcmp(m.seal, seal, 32) == 0, "the member carries the triad seal");

    /* ---- verify the whole triad ---- */
    CHECK(zxpkg_verify_triad(fpos, lp, fneg, ln, fneu, lu) == TRI_Q_NONE,
          "the intact triad verifies and is releasable");

    /* ================= tampering ================= */

    /* a single flipped payload byte is caught by the content digest */
    {
        static uint8_t bad[512];
        memcpy(bad, fpos, lp);
        bad[ZXPKG_HDR_LEN + 5] ^= 0x01;
        zxpkg_member_t mm;
        CHECK(!zxpkg_read(bad, lp, &mm),
              "a flipped payload byte fails the content-digest check on read");
        CHECK(zxpkg_verify_triad(bad, lp, fneg, ln, fneu, lu) != TRI_Q_NONE,
              "and the triad no longer verifies");
    }

    /* SUBSTITUTION: swap S- for one from a DIFFERENT build (different undo).
     * Its content digest differs, so the rebuilt seal differs from the stored
     * seal — the swap is detected even though the file itself is internally
     * consistent. */
    {
        const uint8_t undo2[] = "rollback: DO NOTHING (a friendlier, weaker undo)";
        zxpkg_spec_t s2;
        good_spec(&s2, image, imgn, undo2, (uint32_t) sizeof undo2 - 1);
        uint8_t seal2[32];
        /* seal2 is this other build's seal; but we splice the file under the
         * ORIGINAL seal to simulate an attacker keeping the original seal */
        zxpkg_seal(&s2, seal2);
        static uint8_t stale_neg[512];
        uint32_t sl = zxpkg_write(&s2, TRI_NEGATIVE, seal, stale_neg, sizeof stale_neg);
        CHECK(zxpkg_verify_triad(fpos, lp, stale_neg, sl, fneu, lu) == TRI_Q_SEAL_MISMATCH,
              "a stale S- lifted from another build breaks the seal — you cannot "
              "ship today's behaviour with yesterday's undo");
    }

    /* ================= the five requirements, at pack time ================= */

    /* (2) S- must not out-power S+ */
    {
        zxpkg_spec_t s2;
        good_spec(&s2, image, imgn, undo, undon);
        s2.capability[TRI_NEGATIVE] = 0xFF; /* superset of S+'s 0x0F */
        uint8_t sl[32];
        CHECK(zxpkg_seal(&s2, sl) == TRI_Q_NEG_OVER_CAPABLE,
              "an undo path that claims MORE powers than the action never seals");
    }
    /* (3) S0 must have no production effect */
    {
        zxpkg_spec_t s2;
        good_spec(&s2, image, imgn, undo, undon);
        s2.capability[TRI_NEUTRAL] = 0x01;
        uint8_t sl[32];
        CHECK(zxpkg_seal(&s2, sl) == TRI_Q_NEUTRAL_HAS_EFFECT,
              "an unresolved remainder that can act on the world never seals");
    }
    /* (4) an irreversible effect cannot claim an exact inverse */
    {
        zxpkg_spec_t s2;
        good_spec(&s2, image, imgn, undo, undon);
        s2.irreversible = true;
        s2.inverse_kind = TRI_INV_EXACT;
        uint8_t sl[32];
        CHECK(zxpkg_seal(&s2, sl) == TRI_Q_BAD_INVERSE_CLAIM,
              "claiming an EXACT undo for an irreversible effect never seals");
    }
    /* (5) generated S- may not claim to be a proven inverse */
    {
        zxpkg_spec_t s2;
        good_spec(&s2, image, imgn, undo, undon);
        s2.claims_proven_inverse[TRI_NEGATIVE] = true; /* while generated */
        uint8_t sl[32];
        CHECK(zxpkg_seal(&s2, sl) == TRI_Q_UNPROVEN_INVERSE,
              "an auto-derived undo presented as a PROVEN inverse never seals");
    }

    /* ================= malformed input ================= */
    {
        zxpkg_member_t mm;
        CHECK(!zxpkg_read(fpos, ZXPKG_HDR_LEN - 1, &mm), "a truncated header is refused");
        static uint8_t junk[256];
        for (int s = 0; s < 256; s++) {
            uint32_t x = (uint32_t) s * 2654435761u;
            for (int i = 0; i < 256; i++) {
                x = x * 1103515245u + 12345u;
                junk[i] = (uint8_t) (x >> 16);
            }
            (void) zxpkg_read(junk, sizeof junk, &mm); /* must not crash */
        }
        CHECK(1, "256 randomised buffers parse without crashing");
        /* a wrong magic is refused */
        static uint8_t wm[512];
        memcpy(wm, fpos, lp);
        wm[0] = 'X';
        CHECK(!zxpkg_read(wm, lp, &mm), "a wrong magic is refused");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
