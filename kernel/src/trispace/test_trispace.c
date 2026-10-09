/* test_trispace.c — the Tri-Space artifact triad.
 *
 * Each case pins one of the five hard requirements from
 * ZXV_TRI_SPACE_PROGRAMMING_SPEC_v2. The headline is the SUBSTITUTION ATTACK:
 * you cannot ship today's behaviour with yesterday's undo path.
 */
#include <stdio.h>
#include <string.h>
#include "trispace.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* capability bits for the tests */
#define CAP_WRITE_DISK  (1u<<0)
#define CAP_SEND_NET    (1u<<1)
#define CAP_SPEND_FUNDS (1u<<2)

static void digest(uint8_t d[32], uint8_t seed) {
    for (int i = 0; i < 32; i++) d[i] = (uint8_t)(seed * 31u + i);
}

/* A clean, releasable triad: S+ writes disk, S- can undo (subset of S+),
 * S0 holds nothing. */
static void good_triad(tri_triad_t *t, uint8_t build_seed) {
    uint8_t id[32], src[32], dp[32], dn[32], du[32];
    digest(id, 1); digest(src, build_seed);
    digest(dp, (uint8_t)(build_seed + 10));
    digest(dn, (uint8_t)(build_seed + 20));
    digest(du, (uint8_t)(build_seed + 30));
    tri_init(t, id, src);
    t->inverse_kind = TRI_INV_RESTORING;
    t->effect_is_irreversible = false;
    tri_set_member(t, TRI_POSITIVE, dp, CAP_WRITE_DISK, false, false);
    tri_set_member(t, TRI_NEGATIVE, dn, CAP_WRITE_DISK, false, false);
    tri_set_member(t, TRI_NEUTRAL,  du, 0,              false, false);
}

int main(void) {
    printf("=== Tri-Space artifact triad (ZXV's native file format) ===\n");

    /* ---- the file formats themselves ---- */
    CHECK(strcmp(tri_source_extension(TRI_POSITIVE), ".n9n63") == 0 &&
          strcmp(tri_source_extension(TRI_NEGATIVE), ".9n63")  == 0 &&
          strcmp(tri_source_extension(TRI_NEUTRAL),  ".0n0")   == 0,
          "source extensions: .n9n63 / .9n63 / .0n0");
    CHECK(strcmp(tri_compiled_extension(TRI_POSITIVE), ".zxvc")  == 0 &&
          strcmp(tri_compiled_extension(TRI_NEGATIVE), ".cedez") == 0 &&
          strcmp(tri_compiled_extension(TRI_NEUTRAL),  ".cedec") == 0,
          "compiled extensions: .zxvc / .cedez / .cedec");

    /* ---- a complete, consistent triad binds and may be released ---- */
    tri_triad_t t; good_triad(&t, 100);
    CHECK(tri_bind(&t), "a complete, consistent triad binds");
    CHECK(tri_verify(&t), "the seal verifies");
    CHECK(tri_may_release(&t), "and it may be released");
    CHECK(t.quarantine == TRI_Q_NONE, "not quarantined");

    /* ================= THE SUBSTITUTION ATTACK =================
     * Take a bound triad and swap in the S- (undo path) from a DIFFERENT
     * build. Every member's digest is inside the seal, so this is caught. */
    {
        tri_triad_t a; good_triad(&a, 100); tri_bind(&a);
        tri_triad_t b; good_triad(&b, 200);   /* a different build */
        uint8_t stale_neg[32];
        memcpy(stale_neg, b.member[TRI_NEGATIVE].content_digest, 32);

        tri_triad_t attack = a;               /* start from the released triad */
        memcpy(attack.member[TRI_NEGATIVE].content_digest, stale_neg, 32);
        /* seal NOT recomputed — the attacker keeps the old, valid-looking seal */
        CHECK(!tri_verify(&attack),
              "SUBSTITUTION DETECTED: a stale S- from another build breaks the seal "
              "(you cannot ship today's behaviour with yesterday's undo)");
        CHECK(!tri_may_release(&attack), "and the substituted triad cannot be released");

        /* even re-binding does not launder it: the seal now differs from the
         * one the release was signed against */
        tri_bind(&attack);
        CHECK(memcmp(attack.seal, a.seal, 32) != 0,
              "re-binding produces a DIFFERENT seal — the swap remains visible");
    }

    /* ---- (1) a missing member quarantines into S0, never releases ---- */
    {
        tri_triad_t m; uint8_t id[32], src[32], dp[32];
        digest(id,1); digest(src,7); digest(dp,8);
        tri_init(&m, id, src);
        tri_set_member(&m, TRI_POSITIVE, dp, CAP_WRITE_DISK, false, false);
        CHECK(!tri_bind(&m), "a triad missing S- and S0 does NOT bind");
        CHECK(m.quarantine == TRI_Q_MISSING_MEMBER, "it is quarantined: missing member");
        CHECK(!tri_may_release(&m),
              "the positive half ALONE can never be released (no silent S+-only ship)");
    }

    /* ---- (2) S- must not out-power S+ (the undo path is not a back door) ---- */
    {
        tri_triad_t o; good_triad(&o, 100);
        uint8_t dn[32]; digest(dn, 55);
        /* S- asks for funds-spending that S+ never had */
        tri_set_member(&o, TRI_NEGATIVE, dn, CAP_WRITE_DISK | CAP_SPEND_FUNDS, false, false);
        CHECK(!tri_bind(&o), "S- with MORE capabilities than S+ is refused");
        CHECK(o.quarantine == TRI_Q_NEG_OVER_CAPABLE,
              "quarantined: S- claims capabilities S+ never had");
        /* a proper subset is fine */
        tri_set_member(&o, TRI_NEGATIVE, dn, CAP_WRITE_DISK, false, false);
        CHECK(tri_bind(&o), "an S- whose capabilities are a SUBSET of S+ binds");
    }

    /* ---- (3) S0 may hold no production effect ---- */
    {
        tri_triad_t n; good_triad(&n, 100);
        uint8_t du[32]; digest(du, 66);
        tri_set_member(&n, TRI_NEUTRAL, du, CAP_SEND_NET, false, false);
        CHECK(!tri_bind(&n), "S0 holding a production capability is refused");
        CHECK(n.quarantine == TRI_Q_NEUTRAL_HAS_EFFECT,
              "quarantined: the unresolved remainder cannot act on the world");
    }

    /* ---- (4) an irreversible effect cannot claim an exact inverse ---- */
    {
        tri_triad_t ir; good_triad(&ir, 100);
        ir.effect_is_irreversible = true;
        ir.inverse_kind = TRI_INV_EXACT;
        CHECK(!tri_bind(&ir), "irreversible effect claiming EXACT inverse is refused");
        CHECK(ir.quarantine == TRI_Q_BAD_INVERSE_CLAIM, "quarantined: bad inverse claim");

        ir.inverse_kind = TRI_INV_COMPENSATING;
        CHECK(tri_bind(&ir),
              "the same effect declared COMPENSATING binds (honest semantics accepted)");
        ir.inverse_kind = TRI_INV_CONSTRAINING;
        CHECK(tri_bind(&ir), "CONSTRAINING is also accepted for irreversible effects");
    }

    /* ---- (5) generated S- is a draft, not a proof ---- */
    {
        tri_triad_t g; good_triad(&g, 100);
        uint8_t dn[32]; digest(dn, 77);
        tri_set_member(&g, TRI_NEGATIVE, dn, CAP_WRITE_DISK, true, true); /* generated AND claimed proven */
        CHECK(!tri_bind(&g), "auto-generated S- presented as a PROVEN inverse is refused");
        CHECK(g.quarantine == TRI_Q_UNPROVEN_INVERSE, "quarantined: unproven inverse");
        /* generated but honestly labelled is fine */
        tri_set_member(&g, TRI_NEGATIVE, dn, CAP_WRITE_DISK, true, false);
        CHECK(tri_bind(&g), "generated S- that does NOT claim proof binds (labelled draft)");
    }

    /* ---- tampering with any member after binding is caught ---- */
    {
        tri_triad_t v; good_triad(&v, 100); tri_bind(&v);
        tri_triad_t x = v; x.member[TRI_POSITIVE].content_digest[0] ^= 0x01;
        CHECK(!tri_verify(&x), "altering S+ after binding is detected");
        tri_triad_t y = v; y.member[TRI_NEUTRAL].content_digest[31] ^= 0xFF;
        CHECK(!tri_verify(&y), "altering S0 after binding is detected");
        tri_triad_t z = v; z.member[TRI_NEGATIVE].capability_set = 0;
        CHECK(!tri_verify(&z),
              "even changing a CAPABILITY SET is detected (capabilities are sealed)");
    }

    /* ---- determinism: the same triad always seals identically ---- */
    {
        tri_triad_t a, b; good_triad(&a, 100); good_triad(&b, 100);
        tri_bind(&a); tri_bind(&b);
        CHECK(memcmp(a.seal, b.seal, 32) == 0,
              "the same triad seals identically (reproducible builds)");
        tri_triad_t c; good_triad(&c, 101); tri_bind(&c);
        CHECK(memcmp(a.seal, c.seal, 32) != 0, "a different source graph seals differently");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
