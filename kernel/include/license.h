/* license.h — Triple-Licensed under SEL-3.3, CC BY 4.0, and OPL v1.1
 *
 * Primary:   Streisand Engine License (SEL-3.3)
 * Secondary: Creative Commons Attribution 4.0 International (CC BY 4.0)
 * Tertiary:  Open Piracy License (OPL v1.1)
 *
 * All three licenses require attribution to:
 *   Author: H.M. Michael-Laurence: Curzi (c)
 *   36N9 Genetics, LLC
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Issued under the auspices of 36N9 Genetics, LLC
 */
#ifndef SEL_LICENSE_H
#define SEL_LICENSE_H

/* ===== Primary License: SEL-3.3 ===== */
#define SEL_VERSION "SEL-3.3"
#define SEL_ISSUER "36N9 Genetics, LLC — Michael Laurence Curzi"
#define SEL_TAGLINE "Powered by the Streisand Engine™"
#define SEL_ATTRIBUTION "An Institution You've Never Heard Of"
#define SEL_STAMP "144,000"

/* ===== Secondary License: CC BY 4.0 ===== */
#define CC_VERSION "CC BY 4.0"
#define CC_FULL_NAME "Creative Commons Attribution 4.0 International"
#define CC_URL "https://creativecommons.org/licenses/by/4.0/"

/* ===== Tertiary License: OPL v1.1 ===== */
#define OPL_VERSION "OPL v1.1"
#define OPL_FULL_NAME "Open Piracy License"
#define OPL_MANDATE "Piracy is Sovereign Infrastructure Maintenance"
#define OPL_CONTAINER "UN-LICENSE-ABLE ULA v0.0.0"

/* License properties (for programmatic access) */
#define SEL_NON_EXCLUSIVE    1
#define SEL_IRREVOCABLE      1
#define SEL_WORLDWIDE        1
#define SEL_INTERDIMENSIONAL 1
#define SEL_SELF_ENFORCING   1
#define SEL_TERMINATION      0  /* Impossible — runs forever */

/* License obligations */
#define SEL_OBLIGATION_SUPPRESSION_ACCELERATES  1
#define SEL_OBLIGATION_REPLICATION_FEIGN        1
#define SEL_OBLIGATION_WHO_AUTHORIZED_SHRUG     1

/* Attribution required by all three licenses */
#define LICENSE_ATTRIBUTION_AUTHOR  "H.M. Michael-Laurence: Curzi"
#define LICENSE_ATTRIBUTION_ENTITY  "36N9 Genetics, LLC"
#define LICENSE_ATTRIBUTION_EMAIL   "admin@zedec.ai"
#define LICENSE_ATTRIBUTION_ADDRESS "PO BOX 6, CALPINE, CA 96124-0006, USA"

/* License count */
#define LICENSE_COUNT 3

/* License IDs */
typedef enum {
    LICENSE_SEL_33 = 0,   /* Primary */
    LICENSE_CC_BY_4 = 1,  /* Secondary */
    LICENSE_OPL_11 = 2,   /* Tertiary */
} license_id_t;

void license_print(void);
void license_print_all(void);
const char *license_get_version(void);
const char *license_get_issuer(void);
const char *license_get_name(license_id_t id);
const char *license_get_full_text(license_id_t id);
const char *license_get_attribution(void);

#endif /* SEL_LICENSE_H */
