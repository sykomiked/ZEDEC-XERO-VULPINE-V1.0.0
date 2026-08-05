/* test_mage.c — The Mage's Hats: the authorization boundary is the whole
 * point, so the tests are mostly attempts to CROSS it that must fail.
 *
 * The safety-critical properties, each with a test that fails if it regresses:
 *   - an offensive capability with no engagement is REFUSED, for every hat
 *   - self-authorization works ONLY over assets you own
 *   - an out-of-scope target is refused even with a valid engagement
 *   - an expired engagement is refused
 *   - a forged external authorization is refused
 *   - the emulator cannot be used to reach an unauthorized target
 *   - the audit chain detects tampering
 */
#include <stdio.h>
#include <string.h>
#include "mage.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* A verifier stub: accepts a signature whose first byte is 0xAB, over a
 * well-formed (non-empty) message. Enough to test the gating without linking
 * Ed25519; a bad signature must be refused. */
static bool verify_stub(const uint8_t *msg, uint32_t len,
                        const uint8_t sig[64], const uint8_t pk[32]) {
    (void)pk;
    return msg && len > 0 && sig && sig[0] == 0xAB;
}

/* A defense that always catches its technique (the defender wins). */
static bool defense_catches(mage_technique_t t, uint64_t target, void *ctx) {
    (void)t; (void)target; (void)ctx; return true;
}
/* A defense that never catches (present but blind). */
static bool defense_blind(mage_technique_t t, uint64_t target, void *ctx) {
    (void)t; (void)target; (void)ctx; return false;
}

int main(void) {
    printf("=== The Mage's Hats — authorization boundary ===\n");
    static mage_ctx_t m;
    mage_init(&m);
    mage_defenses_reset();

    uint64_t OWN = 0x1111111111111111ull;   /* an asset we control */
    uint64_t OTHER = 0x2222222222222222ull;  /* an asset we do NOT control */
    CHECK(mage_asset_add(&m, OWN, true) == OWN, "register an owned asset");
    CHECK(mage_asset_add(&m, OTHER, false) == OTHER, "register a not-owned asset");

    /* ---------- hat grants ---------- */
    CHECK(mage_hat_grants(MAGE_HAT_WHITE) & MAGE_ROE_BIT(MAGE_CAP_EXPLOIT),
          "WHITE may hold offensive capability");
    CHECK(!(mage_hat_grants(MAGE_HAT_BLUE) & MAGE_ROE_BIT(MAGE_CAP_EXPLOIT)),
          "BLUE may NOT hold offensive capability");
    CHECK(!(mage_hat_grants(MAGE_HAT_GREY) & MAGE_ROE_BIT(MAGE_CAP_EXPLOIT)),
          "GREY may NOT hold offensive capability");
    CHECK(mage_hat_grants(MAGE_HAT_CLEAR) == (MAGE_ROE_BIT(MAGE_CAP_RECON_PASSIVE)|
          MAGE_ROE_BIT(MAGE_CAP_MONITOR)|MAGE_ROE_BIT(MAGE_CAP_AUDIT)|
          MAGE_ROE_BIT(MAGE_CAP_EDUCATE)),
          "CLEAR is read-only: recon/monitor/audit/educate ONLY");

    /* ---------- open defensive capabilities need no engagement ---------- */
    CHECK(mage_authorize(&m, MAGE_HAT_BLUE, MAGE_CAP_HARDEN, OWN, 100) == MAGE_ALLOW,
          "BLUE may HARDEN with no engagement (open capability)");
    CHECK(mage_authorize(&m, MAGE_HAT_CLEAR, MAGE_CAP_AUDIT, OWN, 100) == MAGE_ALLOW,
          "CLEAR may AUDIT (read-only, open)");
    CHECK(mage_authorize(&m, MAGE_HAT_CLEAR, MAGE_CAP_HARDEN, OWN, 100) == MAGE_DENY,
          "CLEAR may NOT HARDEN — it touches nothing");

    /* ---------- THE CORE SAFETY PROPERTY: offense needs an engagement ---------- */
    CHECK(mage_authorize(&m, MAGE_HAT_WHITE, MAGE_CAP_EXPLOIT, OWN, 100) == MAGE_NEEDS_ENGAGEMENT,
          "WHITE cannot EXPLOIT without an engagement — not even on our own asset");
    CHECK(mage_authorize(&m, MAGE_HAT_RED, MAGE_CAP_SCAN, OWN, 100) == MAGE_NEEDS_ENGAGEMENT,
          "RED cannot SCAN without an engagement");
    CHECK(mage_authorize(&m, MAGE_HAT_BLACK, MAGE_CAP_EXPLOIT, OWN, 100) == MAGE_NEEDS_ENGAGEMENT,
          "BLACK cannot EXPLOIT without an engagement — the hat is not a licence");

    /* ---------- self-authorize testing of our OWN architecture ---------- */
    uint64_t scope[1] = { OWN };
    mage_roe_t roe = MAGE_ROE_BIT(MAGE_CAP_SCAN) | MAGE_ROE_BIT(MAGE_CAP_EXPLOIT) |
                     MAGE_ROE_BIT(MAGE_CAP_EXFIL);
    uint32_t eng = mage_engage_self(&m, scope, 1, roe, 50, 150);
    CHECK(eng != 0, "we may self-authorize an engagement over our OWN asset");
    CHECK(mage_authorize(&m, MAGE_HAT_WHITE, MAGE_CAP_EXPLOIT, OWN, 100) == MAGE_ALLOW,
          "now WHITE may EXPLOIT the owned asset inside the window");
    CHECK(mage_authorize(&m, MAGE_HAT_BLACK, MAGE_CAP_EXPLOIT, OWN, 100) == MAGE_ALLOW,
          "and BLACK may too — this is 'attack my own system on purpose'");

    /* ---------- but ONLY our own asset, ONLY in window, ONLY permitted caps ---------- */
    CHECK(mage_engage_self(&m, (uint64_t[]){OTHER}, 1, roe, 50, 150) == 0,
          "we may NOT self-authorize an engagement over an asset we do not own "
          "(the boundary that stops this being a weapon)");
    CHECK(mage_authorize(&m, MAGE_HAT_WHITE, MAGE_CAP_EXPLOIT, OTHER, 100) == MAGE_OUT_OF_SCOPE,
          "the engagement does NOT cover OTHER -> OUT_OF_SCOPE");
    CHECK(mage_authorize(&m, MAGE_HAT_WHITE, MAGE_CAP_EXPLOIT, OWN, 200) == MAGE_EXPIRED,
          "past the window -> EXPIRED");
    CHECK(mage_authorize(&m, MAGE_HAT_WHITE, MAGE_CAP_EXPLOIT, OWN, 40) == MAGE_EXPIRED,
          "before the window -> EXPIRED (not yet valid)");
    CHECK(mage_authorize(&m, MAGE_HAT_WHITE, MAGE_CAP_C2, OWN, 100) == MAGE_NEEDS_ENGAGEMENT,
          "a capability the ROE does not permit (C2) is still refused");

    /* ---------- signed external authorization ---------- */
    {
        static mage_ctx_t s; mage_init(&s);
        mage_asset_add(&s, OTHER, false);          /* not ours, but authorized externally */
        uint8_t pk[32]; memset(pk, 0x07, 32);
        uint8_t goodsig[64]; memset(goodsig, 0, 64); goodsig[0] = 0xAB;
        uint8_t badsig[64];  memset(badsig, 0, 64); badsig[0] = 0x00;

        CHECK(mage_engage_signed(&s, (uint64_t[]){OTHER}, 1, roe, 0, 1000, pk, goodsig) == 0,
              "a signed engagement is refused when NO verifier is installed");
        mage_set_verifier(&s, verify_stub);
        CHECK(mage_engage_signed(&s, (uint64_t[]){OTHER}, 1, roe, 0, 1000, pk, badsig) == 0,
              "a FORGED signature is refused");
        uint32_t se = mage_engage_signed(&s, (uint64_t[]){OTHER}, 1, roe, 0, 1000, pk, goodsig);
        CHECK(se != 0, "a validly-signed engagement is accepted");
        CHECK(mage_authorize(&s, MAGE_HAT_RED, MAGE_CAP_EXPLOIT, OTHER, 500) == MAGE_ALLOW,
              "and it authorizes offense against the externally-authorized target");
    }

    /* ---------- green is sandboxed, contained is contained ---------- */
    CHECK(mage_authorize(&m, MAGE_HAT_GREEN, MAGE_CAP_EXPLOIT, OWN, 100) == MAGE_SANDBOX_ONLY,
          "GREEN runs offense in a SANDBOX only, even with an engagement present");
    CHECK(mage_authorize(&m, MAGE_HAT_GREEN, MAGE_CAP_HARDEN, OWN, 100) == MAGE_SANDBOX_ONLY,
          "GREEN sandboxes defensive capabilities too — safe to fail");
    mage_contain(&m, MAGE_HAT_RED, true);
    CHECK(mage_authorize(&m, MAGE_HAT_RED, MAGE_CAP_EXPLOIT, OWN, 100) == MAGE_CONTAINED,
          "a CONTAINED hat is refused offense (rehabilitative quarantine)");
    CHECK(mage_authorize(&m, MAGE_HAT_RED, MAGE_CAP_FORENSIC, OWN, 100) == MAGE_ALLOW,
          "but a contained hat keeps its DEFENSIVE capabilities");
    mage_contain(&m, MAGE_HAT_RED, false);

    /* ---------- the audit chain ---------- */
    CHECK(mage_audit_verify(&m), "the audit chain verifies after all those decisions");
    CHECK(mage_audit_len(&m) > 10, "and it recorded every decision");
    {
        /* tamper with a retained entry: flip the decision byte */
        uint32_t start = m.audit_count <= MAGE_MAX_AUDIT ? 0 : m.audit_head;
        m.audit[start].decision ^= 0xFF;
        CHECK(!mage_audit_verify(&m),
              "flipping one recorded decision BREAKS the audit chain");
        m.audit[start].decision ^= 0xFF;      /* repair for later */
        CHECK(mage_audit_verify(&m), "repairing it restores the chain");
    }

    /* ---------- integrity baseline / tamper detection ---------- */
    {
        const char *v1 = "the trusted contents of a protected asset";
        CHECK(mage_baseline_set(&m, OWN, (const uint8_t*)v1, (uint32_t)strlen(v1)),
              "a defender records an integrity baseline");
        CHECK(mage_integrity_check(&m, OWN, (const uint8_t*)v1, (uint32_t)strlen(v1)),
              "unchanged content passes the integrity check");
        char v2[64]; strcpy(v2, v1); v2[0] ^= 0x20;
        CHECK(!mage_integrity_check(&m, OWN, (const uint8_t*)v2, (uint32_t)strlen(v2)),
              "a one-byte change is DETECTED (this is what a tamper attack fights)");
    }

    /* ---------- anomaly detection ---------- */
    {
        mage_rate_t r; mage_rate_init(&r);
        bool flagged_steady = false;
        for (int i = 0; i < 12; i++) if (mage_rate_observe(&r, 10, 12)) flagged_steady = true;
        CHECK(!flagged_steady, "a steady stream of ~10 is not flagged");
        CHECK(mage_rate_observe(&r, 500, 12),
              "a sudden spike to 500 IS flagged as anomalous");
    }

    /* ================= adversary emulation (the constructive loop) ================= */
    mage_defenses_reset();
    {
        static mage_ctx_t e2; mage_init(&e2);
        mage_asset_add(&e2, OWN, true);
        mage_asset_add(&e2, OTHER, false);
        uint64_t sc[1] = { OWN };
        /* authorize a broad self-engagement to test our own architecture */
        mage_roe_t all_off = MAGE_ROE_BIT(MAGE_CAP_SCAN)|MAGE_ROE_BIT(MAGE_CAP_EXPLOIT)|
                             MAGE_ROE_BIT(MAGE_CAP_C2)|MAGE_ROE_BIT(MAGE_CAP_LATERAL)|
                             MAGE_ROE_BIT(MAGE_CAP_EXFIL);
        mage_engage_self(&e2, sc, 1, all_off, 0, 1000);

        /* register a defense that catches TAMPER but leave INJECT undefended */
        mage_defense_register(&e2, MAGE_TECH_TAMPER, defense_catches, 0);
        mage_defense_register(&e2, MAGE_TECH_PORT_PROBE, defense_blind, 0);

        /* purple-team our own asset: tamper is caught, inject is a GAP */
        CHECK(mage_emulate(&e2, MAGE_HAT_PURPLE, MAGE_TECH_TAMPER, OWN, 100)
              == MAGE_FINDING_CAUGHT,
              "emulating TAMPER against our own asset: the defense CATCHES it");
        CHECK(mage_emulate(&e2, MAGE_HAT_PURPLE, MAGE_TECH_INJECT, OWN, 100)
              == MAGE_FINDING_GAP,
              "emulating INJECT: no defense registered -> a GAP is reported "
              "(this is the finding that makes the system stronger)");
        CHECK(mage_emulate(&e2, MAGE_HAT_PURPLE, MAGE_TECH_PORT_PROBE, OWN, 100)
              == MAGE_FINDING_GAP,
              "a present-but-blind defense still yields a GAP, not a false CAUGHT");

        /* THE CRITICAL ONE: the emulator cannot reach an unauthorized target */
        CHECK(mage_emulate(&e2, MAGE_HAT_BLACK, MAGE_TECH_EXFIL_CHANNEL, OTHER, 100)
              == MAGE_FINDING_NOT_APPLICABLE,
              "the emulator REFUSES a target outside the engagement — it cannot "
              "be used as a bypass around the authorization gate");

        /* and that refusal is recorded as a non-allow decision in the audit */
        uint32_t before = mage_audit_len(&e2);
        mage_emulate(&e2, MAGE_HAT_BLACK, MAGE_TECH_EXFIL_CHANNEL, OTHER, 100);
        CHECK(mage_audit_len(&e2) == before + 1,
              "every emulation attempt — including the refused one — is audited");
        CHECK(mage_audit_verify(&e2), "the emulation audit chain verifies");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
