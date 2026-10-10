/* test_chronicle.c — the hash-chained, self-resolving ledger.
 * Pins: memory (commit + reject both chained), tamper-evidence, replay
 * determinism, and the self-resolving pending set (S0 is not a black hole). */
#include <stdio.h>
#include <string.h>
#include "chronicle.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static void good_event(wyrm_event_t *e)
{
    memset(e, 0, sizeof(*e));
    e->ordinal = 10;
    e->delta[0] = rmag_rational_from_frac(5, 1, false);
    e->delta[1] = rmag_rational_from_frac(5, 1, true);
    e->n_deltas = 2;
    e->evidence = LPRES_STATE_TRUE;
    e->ev[0][0] = SR_ONE;
    e->ev[1][1] = SR_ONE;
    e->n_ev = 2;
    e->r_min = SR_FROM_FLOAT(1.5);
    e->route_available = true;
    e->phases_healthy = true;
}
static bool head_zero(const chronicle_t *c)
{
    for (uint32_t i = 0; i < CHRON_HASH_LEN; i++)
        if (chronicle_head(c)[i]) return false;
    return true;
}

int main(void)
{
    printf("=== the Chronicle: hash-chained, self-resolving ledger ===\n");
    chronicle_t c;
    chronicle_init(&c);
    CHECK(head_zero(&c) && chronicle_length(&c) == 0, "genesis: empty, zero head");

    /* ---- a commit is recorded and moves the head ---- */
    wyrm_event_t e;
    good_event(&e);
    chronicle_result_t r = chronicle_submit(&c, &e);
    CHECK(r.verdict == WYRM_COMMIT && r.chained && r.seq == 1,
          "a committable event is chained at seq 1");
    CHECK(!head_zero(&c) && chronicle_length(&c) == 1, "the head advanced");
    CHECK(chronicle_verify(&c), "chain verifies");

    /* ---- a REJECT is also recorded (history cannot hide a refusal) ---- */
    wyrm_event_t bad;
    good_event(&bad);
    bad.delta[1] = rmag_rational_from_frac(4, 1, true); /* not conserved */
    r = chronicle_submit(&c, &bad);
    CHECK(r.verdict == WYRM_REJECT && r.chained && r.seq == 2,
          "a rejected event is ALSO chained — the audit trail keeps refusals");
    CHECK(chronicle_length(&c) == 2 && chronicle_verify(&c), "two entries, verified");

    /* ---- tamper-evidence: alter a past entry, verify fails ---- */
    {
        chronicle_t t = c;
        t.entry[0].reason ^= 0xFF; /* rewrite history */
        CHECK(!chronicle_verify(&t),
              "altering a past entry is DETECTED (chain no longer verifies)");
        chronicle_t t2 = c;
        t2.entry[1].event_digest[0] ^= 0x01; /* tamper the event record */
        CHECK(!chronicle_verify(&t2), "tampering an event digest is detected too");
    }

    /* ---- replay determinism: same sequence -> same head ---- */
    {
        chronicle_t a, b;
        chronicle_init(&a);
        chronicle_init(&b);
        wyrm_event_t ev;
        for (int k = 0; k < 4; k++) {
            good_event(&ev);
            ev.ordinal = (uint64_t) (10 + k);
            chronicle_submit(&a, &ev);
            chronicle_submit(&b, &ev);
        }
        CHECK(memcmp(chronicle_head(&a), chronicle_head(&b), CHRON_HASH_LEN) == 0,
              "two ledgers fed the same sequence reach the SAME head (replayable)");
    }

    /* ============ the self-resolving pending set ============ */
    {
        chronicle_t p;
        chronicle_init(&p);
        /* a lawful event but with only ONE evidence direction -> S0 DEFER */
        wyrm_event_t lone;
        good_event(&lone);
        lone.ev[1][0] = SR_ONE;
        lone.ev[1][1] = SR_ZERO; /* second == first (echo) */
        chronicle_result_t d = chronicle_submit(&p, &lone);
        printf("       lone event -> %s, pending_id=%d\n", wyrm_verdict_name(d.verdict),
               d.pending_id);
        CHECK(d.verdict == WYRM_DEFER && d.pending_id >= 0 && !d.chained,
              "a deferred event is HELD in pending, not chained, not lost");
        CHECK(chronicle_length(&p) == 0 && chronicle_pending_count(&p) == 1,
              "nothing in history yet; one pending");

        /* poking now changes nothing — the world hasn't moved */
        CHECK(chronicle_poke(&p) == 0 && chronicle_pending_count(&p) == 1,
              "a poke with no change resolves nothing (still patient)");

        /* the world changes: independent corroboration arrives */
        wyrm_event_t *pe = chronicle_pending_event(&p, (uint32_t) d.pending_id);
        CHECK(pe != 0, "the pending event is reachable for update");
        pe->ev[1][0] = SR_ZERO;
        pe->ev[1][1] = SR_ONE; /* now truly independent */

        uint32_t resolved = chronicle_poke(&p);
        printf("       after corroboration: %u resolved, %u chained, %u pending\n", resolved,
               chronicle_length(&p), chronicle_pending_count(&p));
        CHECK(resolved == 1, "the poke RESOLVES the once-deferred event");
        CHECK(chronicle_length(&p) == 1 && chronicle_pending_count(&p) == 0,
              "S0 was NOT a black hole — it auto-committed when its blocker cleared");
        CHECK(chronicle_verify(&p), "and the resolved entry is properly chained");
    }

    /* ---- pending event that resolves to REJECT is also chained ---- */
    {
        chronicle_t p;
        chronicle_init(&p);
        wyrm_event_t noroute;
        good_event(&noroute);
        noroute.route_available = false; /* S0: no route */
        chronicle_result_t d = chronicle_submit(&p, &noroute);
        CHECK(d.verdict == WYRM_DEFER, "no-route event defers");
        /* the world changes for the worse: it turns out value was wrong */
        wyrm_event_t *pe = chronicle_pending_event(&p, (uint32_t) d.pending_id);
        pe->route_available = true;                         /* route came up ... */
        pe->delta[1] = rmag_rational_from_frac(1, 1, true); /* ... but now unlawful */
        chronicle_poke(&p);
        CHECK(chronicle_length(&p) == 1 && chronicle_pending_count(&p) == 0,
              "a pending event that becomes unlawful resolves to a chained REJECT");
        CHECK(p.entry[0].verdict == WYRM_REJECT, "and it is recorded as a reject");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
