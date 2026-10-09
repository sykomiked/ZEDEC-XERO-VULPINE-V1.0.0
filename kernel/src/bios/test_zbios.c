/* test_zbios.c — ten-key staged measured boot: order, tamper-evidence,
 * rollback, fail-closed behaviour.
 *
 *   gcc -std=c11 -Wall -Wextra -Isrc/bios -Isrc/robin_debanks \
 *       src/bios/test_zbios.c src/bios/zbios.c src/robin_debanks/sha256.c \
 *       -o /tmp/test_zbios && /tmp/test_zbios
 */
#include <stdio.h>
#include <string.h>
#include "zbios.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* a distinct fake image digest per stage */
static void mkdigest(uint8_t *d, uint8_t seed)
{
    for (int i = 0; i < ZB_DIGEST_LEN; i++) d[i] = (uint8_t) (seed * 7 + i);
}
static void hex8(const uint8_t *d, char *out)
{
    const char *H = "0123456789abcdef";
    for (int i = 0; i < 4; i++) {
        out[i * 2] = H[d[i] >> 4];
        out[i * 2 + 1] = H[d[i] & 15];
    }
    out[8] = '\0';
}

/* run the full ten-key chain; returns true if it sealed */
static bool boot_all(zbios_t *b, uint8_t *quote, uint8_t tamper_key, uint8_t tamper_seed)
{
    const uint8_t anchor[] = "ZXV-ROOT-OF-TRUST";
    zb_init(b, anchor, sizeof(anchor) - 1);
    for (uint32_t k = 0; k < ZB_KEYS; k++) {
        uint8_t d[ZB_DIGEST_LEN];
        mkdigest(d, (uint8_t) (k == tamper_key ? tamper_seed : k + 1));
        if (zb_stage(b, (zb_key_t) k, zb_key_name((zb_key_t) k), d, 1, true) != ZB_OK) return false;
    }
    return zb_seal(b, quote) == ZB_OK;
}

int main(void)
{
    printf("=== ZXV ten-key staged measured boot ===\n");
    zbios_t b;
    uint8_t quote[ZB_DIGEST_LEN], quote2[ZB_DIGEST_LEN];
    char h[9];

    /* ---- the happy path ---- */
    CHECK(boot_all(&b, quote, 255, 0), "all ten keys execute and seal");
    CHECK(b.stages_run == ZB_KEYS && b.sealed, "ten stages recorded, chain sealed");
    hex8(quote, h);
    printf("       attestation quote: %s...\n", h);

    CHECK(zb_triad_of(ZB_K0_ANCHOR) == ZB_TRIAD_SILICON &&
              zb_triad_of(ZB_K2_MEASURE) == ZB_TRIAD_SILICON,
          "keys 0-2 form the SILICON triad");
    CHECK(zb_triad_of(ZB_K3_MEDIUM) == ZB_TRIAD_STORAGE &&
              zb_triad_of(ZB_K5_PAYLOAD) == ZB_TRIAD_STORAGE,
          "keys 3-5 form the STORAGE triad");
    CHECK(zb_triad_of(ZB_K6_FABRIC) == ZB_TRIAD_SOVEREIGNTY &&
              zb_triad_of(ZB_K8_HANDOFF) == ZB_TRIAD_SOVEREIGNTY,
          "keys 6-8 form the SOVEREIGNTY triad");
    CHECK(zb_triad_of(ZB_K9_SEAL) == ZB_TRIAD_SEAL, "key 9 is the seal");

    /* ---- DETERMINISM: same images => same quote (attestable) ---- */
    {
        zbios_t c;
        CHECK(boot_all(&c, quote2, 255, 0), "second identical boot succeeds");
        CHECK(memcmp(quote, quote2, ZB_DIGEST_LEN) == 0,
              "identical boot yields an IDENTICAL attestation quote");
    }

    /* ---- TAMPER EVIDENCE: change ANY stage => different quote ---- */
    {
        int all_differ = 1;
        for (uint32_t k = 0; k < ZB_KEYS; k++) {
            zbios_t t;
            uint8_t q[ZB_DIGEST_LEN];
            if (!boot_all(&t, q, (uint8_t) k, 200)) {
                all_differ = 0;
                break;
            }
            if (memcmp(q, quote, ZB_DIGEST_LEN) == 0) {
                all_differ = 0;
                break;
            }
        }
        CHECK(all_differ, "tampering with ANY ONE of the ten stages changes the quote");
    }

    /* ---- ORDER is enforced ---- */
    {
        const uint8_t anchor[] = "ZXV-ROOT-OF-TRUST";
        uint8_t d[ZB_DIGEST_LEN];
        mkdigest(d, 1);

        zbios_t s;
        zb_init(&s, anchor, sizeof(anchor) - 1);
        CHECK(zb_stage(&s, ZB_K1_SILICON, "skip", d, 1, true) == ZB_ERR_ORDER,
              "skipping key 0 is rejected");
        CHECK(s.halted, "an out-of-order stage HALTS the chain");
        CHECK(zb_stage(&s, ZB_K0_ANCHOR, "retry", d, 1, true) == ZB_ERR_HALTED,
              "a halted chain refuses all further stages (fail-closed)");

        zbios_t r;
        zb_init(&r, anchor, sizeof(anchor) - 1);
        zb_stage(&r, ZB_K0_ANCHOR, "k0", d, 1, true);
        CHECK(zb_stage(&r, ZB_K0_ANCHOR, "k0-again", d, 1, true) == ZB_ERR_ORDER,
              "replaying a stage is rejected");
    }

    /* ---- a failed signature halts the chain ---- */
    {
        const uint8_t anchor[] = "ZXV-ROOT-OF-TRUST";
        uint8_t d[ZB_DIGEST_LEN];
        mkdigest(d, 3);
        zbios_t s;
        zb_init(&s, anchor, sizeof(anchor) - 1);
        zb_stage(&s, ZB_K0_ANCHOR, "k0", d, 1, true);
        CHECK(zb_stage(&s, ZB_K1_SILICON, "bad", d, 1, false) == ZB_ERR_SIGNATURE,
              "unverified stage image is rejected");
        CHECK(s.halted && s.fault == ZB_ERR_SIGNATURE, "chain halted with the right fault");
    }

    /* ---- ROLLBACK protection ---- */
    {
        const uint8_t anchor[] = "ZXV-ROOT-OF-TRUST";
        uint8_t d[ZB_DIGEST_LEN];
        mkdigest(d, 5);
        zbios_t s;
        zb_init(&s, anchor, sizeof(anchor) - 1);
        zb_set_min_version(&s, ZB_K1_SILICON, 7);
        zb_stage(&s, ZB_K0_ANCHOR, "k0", d, 1, true);
        CHECK(zb_stage(&s, ZB_K1_SILICON, "old", d, 6, true) == ZB_ERR_ROLLBACK,
              "a signed-but-OLD stage is rejected (rollback floor)");

        zbios_t s2;
        zb_init(&s2, anchor, sizeof(anchor) - 1);
        zb_set_min_version(&s2, ZB_K1_SILICON, 7);
        zb_stage(&s2, ZB_K0_ANCHOR, "k0", d, 1, true);
        CHECK(zb_stage(&s2, ZB_K1_SILICON, "current", d, 7, true) == ZB_OK,
              "a stage at exactly the floor is accepted");
    }

    /* ---- an incomplete chain cannot be sealed ---- */
    {
        const uint8_t anchor[] = "ZXV-ROOT-OF-TRUST";
        uint8_t d[ZB_DIGEST_LEN];
        mkdigest(d, 9);
        zbios_t s;
        zb_init(&s, anchor, sizeof(anchor) - 1);
        for (uint32_t k = 0; k < 5; k++) zb_stage(&s, (zb_key_t) k, "partial", d, 1, true);
        uint8_t q[ZB_DIGEST_LEN];
        CHECK(zb_seal(&s, q) == ZB_ERR_ORDER, "sealing before all ten keys have run is refused");
    }

    /* ---- a different anchor yields a different chain entirely ---- */
    {
        zbios_t s;
        uint8_t q[ZB_DIGEST_LEN];
        const uint8_t other[] = "SOMEONE-ELSES-ROOT";
        zb_init(&s, other, sizeof(other) - 1);
        int ok = 1;
        for (uint32_t k = 0; k < ZB_KEYS; k++) {
            uint8_t d[ZB_DIGEST_LEN];
            mkdigest(d, (uint8_t) (k + 1));
            if (zb_stage(&s, (zb_key_t) k, "x", d, 1, true) != ZB_OK) ok = 0;
        }
        CHECK(ok && zb_seal(&s, q) == ZB_OK, "boot under a different anchor completes");
        CHECK(memcmp(q, quote, ZB_DIGEST_LEN) != 0,
              "a different root of trust produces a different attestation");
    }

    /* ---- diagnostics are complete ---- */
    {
        int named = 1;
        for (uint32_t k = 0; k < ZB_KEYS; k++) {
            const char *n = zb_key_name((zb_key_t) k);
            if (!n || n[0] == '\0') named = 0;
        }
        CHECK(named, "all ten keys have descriptive names");
        CHECK(zb_strerror(ZB_ERR_ROLLBACK)[0] != '\0', "faults have readable messages");
    }

    /* audit #14: a missing anchor must HALT, not seal an all-zero chain */
    {
        zbios_t nb;
        zb_init(&nb, NULL, 0);
        CHECK(nb.halted && nb.fault == ZB_ERR_ARG,
              "NULL anchor halts the chain at init (no anchorless attestation)");
        uint8_t d[ZB_DIGEST_LEN] = {1};
        CHECK(zb_stage(&nb, (zb_key_t) 0, "boot", d, 1, true) == ZB_ERR_HALTED,
              "staging on an anchorless chain reports HALTED");
        zb_init(&nb, (const uint8_t *) "anchor", 6);
        CHECK(!nb.halted, "a real anchor initialises normally");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
