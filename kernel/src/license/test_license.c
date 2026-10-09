/* test_license.c — the kernel advertises the same license as the repository.
 *
 * There was no license test before, which is how the kernel once booted
 * advertising a different license than the repository's LICENSE file. The
 * repository is Apache-2.0 (LICENSE at the root), so this pins the module to
 * Apache-2.0: name, SPDX id, URL, the AS IS warranty disclaimer, and the
 * attribution to Michael Laurence Curzi and 36N9 Genetics, LLC.
 */
#include <stdio.h>
#include <string.h>
#include "license.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

int main(void) {
    printf("=== platform license: Apache-2.0, matching the repository LICENSE ===\n");

    CHECK(strcmp(LICENSE_SPDX, "Apache-2.0") == 0, "SPDX id is Apache-2.0");
    CHECK(strcmp(license_spdx_bundle(), "Apache-2.0") == 0, "the SPDX expression is Apache-2.0 alone");
    CHECK(strcmp(license_get_version(), "Apache-2.0") == 0, "version string is Apache-2.0");
    CHECK(strstr(LICENSE_URL, "apache.org/licenses/LICENSE-2.0") != NULL, "URL points at the Apache 2.0 text");
    CHECK(strcmp(license_get_name(LICENSE_APACHE_20), "Apache License 2.0") == 0, "the instrument is named");
    CHECK(strcmp(license_get_name((license_id_t)99), "Unknown License") == 0,
          "an unknown id is reported as unknown, not as Apache");
    CHECK(strcmp(license_get_full_text((license_id_t)99), "") == 0, "an unknown id has no text");

    /* ---- the text carries the no-warranty disclaimer ---- */
    const char *txt = license_get_full_text(LICENSE_APACHE_20);
    CHECK(strstr(txt, "AS IS") != NULL, "full text carries the AS IS basis");
    CHECK(strstr(txt, "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND") != NULL,
          "full text disclaims all warranties");
    CHECK(strstr(txt, "36N9 Genetics, LLC") != NULL, "full text carries the copyright holder");

    /* ---- attribution ---- */
    const char *att = license_get_attribution();
    CHECK(strstr(att, "Michael Laurence Curzi") != NULL, "attribution names the author");
    CHECK(strstr(att, "36N9 Genetics, LLC") != NULL, "attribution names the entity");
    CHECK(strstr(att, "Apache-2.0") != NULL, "attribution names the license");
    CHECK(strcmp(license_get_issuer(), "36N9 Genetics, LLC") == 0, "issuer is 36N9 Genetics, LLC");

    /* ---- permissive semantics ---- */
    CHECK(!license_is_share_alike(LICENSE_APACHE_20), "Apache-2.0 is permissive, not share-alike");
    CHECK(!license_travels_together(LICENSE_APACHE_20), "derivatives may be relicensed");
    CHECK(license_precedence_rank(LICENSE_APACHE_20) == 0, "single license, rank 0");

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
