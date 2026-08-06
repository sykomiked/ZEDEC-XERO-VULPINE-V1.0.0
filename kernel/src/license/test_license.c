/* test_license.c — the platform license is the four-instrument share-alike stack.
 *
 * There was no license test before, which is how the kernel came to boot
 * advertising CC BY 4.0 — the NON-copyleft Creative Commons variant — while the
 * repository's LICENSE, NOTICE, and every SPDX header said CC BY-SA 4.0. This
 * pins the module to the strengthened share-alike bundle so that bug cannot
 * come back: four instruments, share-alike, attribution, travelling together.
 */
#include <stdio.h>
#include <string.h>
#include "license.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

int main(void) {
    printf("=== platform license: four-instrument share-alike stack ===\n");

    /* ---- the stack is four, not three ---- */
    CHECK(LICENSE_COUNT == 4, "four instruments (OPL, CC BY-SA, Royal Writ, SEL)");

    /* ---- the fixed bug: CC is the SHARE-ALIKE variant, never plain BY ---- */
    CHECK(strcmp(CC_VERSION, "CC BY-SA 4.0") == 0, "CC is BY-SA 4.0 (copyleft), not the plain-BY variant");
    CHECK(strstr(CC_URL, "by-sa") != NULL, "CC URL points at /licenses/by-sa/");
    CHECK(strstr(CC_URL, "/by/4.0") == NULL, "CC URL is NOT the non-copyleft /by/4.0/");
    CHECK(strstr(license_get_name(LICENSE_CC_BY_SA_4), "ShareAlike") != NULL,
          "the CC instrument name says ShareAlike");
    CHECK(strstr(license_get_full_text(LICENSE_CC_BY_SA_4), "SHARE-ALIKE (REQUIRED)") != NULL,
          "the CC full text carries a required SHARE-ALIKE section");

    /* ---- the Royal Writ is a first-class instrument now ---- */
    CHECK(strstr(license_get_name(LICENSE_ROYAL_WRIT), "Royal Writ") != NULL,
          "the Royal Writ is in the enum with a name");
    CHECK(strstr(license_get_full_text(LICENSE_ROYAL_WRIT), "MUTUAL SOVEREIGN RECOGNITION") != NULL,
          "the Royal Writ text carries the §1.2 mutual-recognition reciprocity term");

    /* ---- attribution + SPDX name all four and travel together ---- */
    const char *att = license_get_attribution();
    CHECK(strstr(att, "OPL-1.1") && strstr(att, "CC BY-SA 4.0") &&
          strstr(att, "Royal Writ") && strstr(att, "SEL-3.3"),
          "attribution names all four instruments");
    CHECK(strstr(att, "CC BY 4.0 + OPL") == NULL, "the stale 'CC BY 4.0' attribution string is gone");

    const char *spdx = license_spdx_bundle();
    CHECK(strstr(spdx, "LicenseRef-OPL-1.1") && strstr(spdx, "CC-BY-SA-4.0") &&
          strstr(spdx, "LicenseRef-Royal-Writ-Sicilian-Crown-1.0") && strstr(spdx, "LicenseRef-SEL-3.3"),
          "SPDX bundle is the 4-way conjunction");
    CHECK(strstr(spdx, " AND ") != NULL, "SPDX uses AND (all four required, not a choice)");

    /* ---- share-alike / travel-together semantics ---- */
    CHECK(license_is_share_alike(LICENSE_OPL_11), "OPL is share-alike (operative copyleft)");
    CHECK(license_is_share_alike(LICENSE_CC_BY_SA_4), "CC BY-SA is share-alike");
    CHECK(license_is_share_alike(LICENSE_ROYAL_WRIT), "the Royal Writ §1.2 reciprocity is share-alike in form");
    CHECK(!license_is_share_alike(LICENSE_SEL_33), "SEL is declaratory — no carry-forward duty");
    for (int i = 0; i < LICENSE_COUNT; i++)
        CHECK(license_travels_together((license_id_t)i), "every instrument travels with the bundle");

    /* ---- precedence: OPL > CC BY-SA > Royal Writ > SEL ---- */
    CHECK(license_precedence_rank(LICENSE_OPL_11) < license_precedence_rank(LICENSE_CC_BY_SA_4) &&
          license_precedence_rank(LICENSE_CC_BY_SA_4) < license_precedence_rank(LICENSE_ROYAL_WRIT) &&
          license_precedence_rank(LICENSE_ROYAL_WRIT) < license_precedence_rank(LICENSE_SEL_33),
          "precedence order OPL > CC BY-SA > Royal Writ > SEL");

    /* ---- back-compat: old callers of LICENSE_CC_BY_4 still resolve ---- */
    CHECK(LICENSE_CC_BY_4 == LICENSE_CC_BY_SA_4, "the LICENSE_CC_BY_4 alias maps to the BY-SA instrument");

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
