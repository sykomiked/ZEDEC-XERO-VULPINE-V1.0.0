/* test_wyverneye.c — intent forged, then judged.
 * The through-line: the sigil ALWAYS forges (speech is free), but the ACTION
 * is judged, and a lone assertion is deferred until corroborated. */
#include <stdio.h>
#include <string.h>
#include "wyverneye.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* A context that is lawful (value conserved), attested TRUE, ordered, routed
 * and healthy — everything except independence is left to the caller. */
static void base_ctx(wyvern_context_t *c)
{
    memset(c, 0, sizeof(*c));
    c->delta[0] = rmag_rational_from_frac(7, 1, false);
    c->delta[1] = rmag_rational_from_frac(7, 1, true);
    c->n_deltas = 2;
    c->evidence = LPRES_STATE_TRUE;
    c->r_min = SR_FROM_FLOAT(1.5); /* need ~2 independent directions */
    c->ordinal = 5;
    c->parent_ordinal = 0;
    c->route_available = true;
    c->phases_healthy = true;
}

int main(void)
{
    printf("=== WyvernEye: intent forged, then judged before it acts ===\n");
    const char *intent = "Open the western gate at dawn";
    wyvern_reading_t r;

    /* ---- a lone intent: forged, but DEFERRED (no corroboration) ---- */
    {
        wyvern_context_t ctx;
        base_ctx(&ctx);
        ctx.n_corroboration = 0; /* only the sigil itself */
        CHECK(wyvern_read(intent, (uint32_t) strlen(intent), ENO_VOICE_AEON, &ctx, &r),
              "the intent is read");
        CHECK(r.card.gematria > 0 && r.card.path_len > 0,
              "the sigil ALWAYS forges — speech is free");
        printf("       lone intent -> %s (%s) R=%.2f\n", wyrm_verdict_name(r.verdict.verdict),
               wyrm_reason_name(r.verdict.reason), (double) r.verdict.R / (double) SR_ONE);
        CHECK(r.verdict.verdict == WYRM_DEFER && r.verdict.reason == WYRM_R_NOT_INDEPENDENT,
              "but a LONE assertion is DEFERRED (S0) — one voice cannot compel");
    }

    /* ---- the same intent, independently corroborated: COMMITS ---- */
    {
        wyvern_context_t ctx;
        base_ctx(&ctx);
        /* one corroborating direction, independent of the sigil's */
        for (uint32_t d = 0; d < CHG_DIM; d++) ctx.corroboration[0][d] = SR_ZERO;
        /* pick an axis unlikely to collide with the sigil's primary */
        ctx.corroboration[0][5] = SR_ONE;
        ctx.corroboration[0][2] = SR_FROM_FLOAT(0.6);
        ctx.n_corroboration = 1;
        wyvern_read(intent, (uint32_t) strlen(intent), ENO_VOICE_AEON, &ctx, &r);
        printf("       corroborated  -> %s R=%.2f distinct=%u\n",
               wyrm_verdict_name(r.verdict.verdict), (double) r.verdict.R / (double) SR_ONE,
               r.verdict.distinct);
        CHECK(r.verdict.verdict == WYRM_COMMIT,
              "the SAME intent, independently corroborated, COMMITS (S+)");
        CHECK(r.verdict.distinct >= 2, "at least two independent directions stood behind it");
    }

    /* ---- forging never depends on lawfulness: an unlawful action is still
     *      spoken (forged), but REJECTED ---- */
    {
        wyvern_context_t ctx;
        base_ctx(&ctx);
        ctx.delta[1] = rmag_rational_from_frac(3, 1, true); /* +7 and -3: not conserved */
        ctx.corroboration[0][5] = SR_ONE;
        ctx.n_corroboration = 1;
        wyvern_read(intent, (uint32_t) strlen(intent), ENO_VOICE_AEON, &ctx, &r);
        CHECK(r.card.gematria > 0, "the sigil still forged (intent is always expressible)");
        CHECK(r.verdict.verdict == WYRM_REJECT && r.verdict.reason == WYRM_R_VALUE_NOT_CONSERVED,
              "but an action that breaks a law is REJECTED (S-) — forging is not acting");
    }

    /* ---- contradiction defers even when corroborated ---- */
    {
        wyvern_context_t ctx;
        base_ctx(&ctx);
        ctx.evidence = LPRES_STATE_BOTH;
        ctx.corroboration[0][5] = SR_ONE;
        ctx.n_corroboration = 1;
        wyvern_read(intent, (uint32_t) strlen(intent), ENO_VOICE_AEON, &ctx, &r);
        CHECK(r.verdict.verdict == WYRM_DEFER && r.verdict.reason == WYRM_R_EVIDENCE_CONTRADICT,
              "contradictory evidence DEFERS (S0), corroboration notwithstanding");
    }

    /* ---- composition preserves the sub-results: the forged card equals
     *      what the Refinery alone would produce ---- */
    {
        wyvern_context_t ctx;
        base_ctx(&ctx);
        ctx.corroboration[0][5] = SR_ONE;
        ctx.n_corroboration = 1;
        wyvern_read(intent, (uint32_t) strlen(intent), ENO_VOICE_AEON, &ctx, &r);
        ref_card_t direct;
        ref_forge(intent, (uint32_t) strlen(intent), ENO_VOICE_AEON, &direct);
        CHECK(r.card.gematria == direct.gematria && r.card.root == direct.root &&
                  memcmp(r.card.digest, direct.digest, 32) == 0,
              "WyvernEye's forged card == the Refinery's — composition preserves it");

        /* determinism of the whole reading */
        wyvern_reading_t r2;
        wyvern_read(intent, (uint32_t) strlen(intent), ENO_VOICE_AEON, &ctx, &r2);
        CHECK(r.verdict.verdict == r2.verdict.verdict && r.verdict.R == r2.verdict.R,
              "the reading is deterministic and replayable");
    }

    /* ---- different intents forge different sigils, same judgment path ---- */
    {
        wyvern_context_t ctx;
        base_ctx(&ctx);
        ctx.corroboration[0][5] = SR_ONE;
        ctx.n_corroboration = 1;
        wyvern_reading_t a, b;
        wyvern_read("Guard the harvest", 17, ENO_VOICE_SOLAR, &ctx, &a);
        wyvern_read("Flood the low road", 18, ENO_VOICE_LUNAR, &ctx, &b);
        CHECK(memcmp(a.card.digest, b.card.digest, 32) != 0,
              "different intents forge different sigils");
        CHECK(a.verdict.verdict == WYRM_COMMIT && b.verdict.verdict == WYRM_COMMIT,
              "and both pass the same six-fold judgment");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
