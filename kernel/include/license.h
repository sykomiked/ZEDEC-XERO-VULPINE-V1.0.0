/* license.h — the platform license: a four-instrument SHARE-ALIKE stack
 *
 * ZEDEC XERO VULPINE and everything made natively for it are licensed under a
 * bundle of FOUR instruments that TRAVEL TOGETHER. A derivative must be shared
 * under the whole stack — you cannot strip one off — and attribution is
 * required. That is the strengthened share-alike (copyleft) the platform runs on:
 *
 *   1. OPEN PIRACY LICENSE (OPL-1.1)      the operative software license
 *   2. CREATIVE COMMONS BY-SA 4.0         attribution + share-alike (copyleft)
 *   3. ROYAL WRIT OF THE SICILIAN CROWN   mutual sovereign recognition (§1.2),
 *                                         a reciprocity term of share-alike form
 *   4. STREISAND ENGINE LICENSE (SEL-3.3) a statement of position (declaratory)
 *
 * Precedence when they differ: OPL-1.1 > CC BY-SA 4.0 > Royal Writ > SEL-3.3.
 * SPDX for the bundle:
 *   LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND
 *   LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
 *
 * All four require attribution to:
 *   Author: H.M. Michael-Laurence: Curzi (c)
 *   36N9 Genetics, LLC
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Issued under the auspices of 36N9 Genetics, LLC
 */
#ifndef SEL_LICENSE_H
#define SEL_LICENSE_H

#include <stdbool.h>

/* ===== Instrument 1 (operative): OPL v1.1 ===== */
#define OPL_VERSION "OPL v1.1"
#define OPL_FULL_NAME "Open Piracy License"
#define OPL_SPDX "LicenseRef-OPL-1.1"
#define OPL_MANDATE "Piracy is Sovereign Infrastructure Maintenance"
#define OPL_CONTAINER "UN-LICENSE-ABLE ULA v0.0.0"

/* ===== Instrument 2 (share-alike): CC BY-SA 4.0 ===== */
#define CC_VERSION "CC BY-SA 4.0"
#define CC_FULL_NAME "Creative Commons Attribution-ShareAlike 4.0 International"
#define CC_SPDX "CC-BY-SA-4.0"
#define CC_URL "https://creativecommons.org/licenses/by-sa/4.0/"

/* ===== Instrument 3 (reciprocity): Royal Writ of the Sicilian Crown ===== */
#define RW_VERSION "Royal Writ of the Sicilian Crown"
#define RW_FULL_NAME "Royal Writ of the Sicilian Crown (mutual sovereign recognition)"
#define RW_SPDX "LicenseRef-Royal-Writ-Sicilian-Crown-1.0"

/* ===== Instrument 4 (declaratory): SEL-3.3 ===== */
#define SEL_VERSION "SEL-3.3"
#define SEL_SPDX "LicenseRef-SEL-3.3"
#define SEL_ISSUER "36N9 Genetics, LLC — Michael Laurence Curzi"
#define SEL_TAGLINE "Powered by the Streisand Engine™"
#define SEL_ATTRIBUTION "An Institution You've Never Heard Of"
#define SEL_STAMP "144,000"

/* The whole bundle as one SPDX expression (AND = all four travel together). */
#define LICENSE_SPDX_BUNDLE \
    "LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND " \
    "LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3"

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

/* Attribution required by all four instruments */
#define LICENSE_ATTRIBUTION_AUTHOR  "H.M. Michael-Laurence: Curzi"
#define LICENSE_ATTRIBUTION_ENTITY  "36N9 Genetics, LLC"
#define LICENSE_ATTRIBUTION_EMAIL   "admin@zedec.ai"
#define LICENSE_ATTRIBUTION_ADDRESS "PO BOX 6, CALPINE, CA 96124-0006, USA"

/* Four instruments now, not three. */
#define LICENSE_COUNT 4

/* License IDs. Existing values are unchanged for ABI stability; the Royal Writ
 * is appended. Ordering here is enumeration order, NOT precedence (precedence is
 * OPL > CC BY-SA > Royal Writ > SEL — see license_precedence_rank). */
typedef enum {
    LICENSE_SEL_33      = 0,  /* declaratory      */
    LICENSE_CC_BY_SA_4  = 1,  /* share-alike      */
    LICENSE_OPL_11      = 2,  /* operative        */
    LICENSE_ROYAL_WRIT  = 3,  /* reciprocity      */
} license_id_t;

/* Back-compat alias: the secondary instrument used to be CC BY 4.0 (a bug — the
 * non-copyleft variant). It is now CC BY-SA 4.0, so old callers keep building. */
#define LICENSE_CC_BY_4 LICENSE_CC_BY_SA_4

void license_print(void);
void license_print_all(void);
const char *license_get_version(void);
const char *license_get_issuer(void);
const char *license_get_name(license_id_t id);
const char *license_get_full_text(license_id_t id);
const char *license_get_attribution(void);

/* The bundle's SPDX expression — the one line every native artifact must carry
 * so the four instruments travel together. */
const char *license_spdx_bundle(void);

/* True if this instrument imposes a share-alike / reciprocity duty that a
 * derivative must carry forward (OPL, CC BY-SA, and the Royal Writ §1.2 do; the
 * declaratory SEL does not). */
bool license_is_share_alike(license_id_t id);

/* Every instrument in this stack travels with the others — a derivative may not
 * drop any of them. True for all four (this is what "strengthened share-alike"
 * means here); the SEL rides along as a conveyed statement of position. */
bool license_travels_together(license_id_t id);

/* Lower rank = higher precedence when instruments differ: OPL(0) < CC BY-SA(1)
 * < Royal Writ(2) < SEL(3). */
int license_precedence_rank(license_id_t id);

#endif /* SEL_LICENSE_H */
