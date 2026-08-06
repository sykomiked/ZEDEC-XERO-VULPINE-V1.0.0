/* license.c — the four-instrument SHARE-ALIKE stack (OPL-1.1 + CC BY-SA 4.0 +
 * Royal Writ + SEL-3.3). Embeds the full text of the four instruments into the
 * kernel binary. They travel together: a derivative carries all four, with
 * attribution — that is the strengthened copyleft the platform runs on.
 *
 * Operative:    Open Piracy License (OPL v1.1)
 * Share-alike:  Creative Commons Attribution-ShareAlike 4.0 International
 * Reciprocity:  Royal Writ of the Sicilian Crown (mutual sovereign recognition)
 * Declaratory:  Streisand Engine License (SEL-3.3)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * 36N9 Genetics, LLC — Irrevocable, Interdimensional
 */
#include "license.h"

/* For freestanding kernel, provide minimal output abstraction */
#ifdef __KERNEL__
#include "freestanding.h"
extern void fb_puts(const char *s);
extern void uart_puts(const char *s);
static void puts_impl(const char *s) {
#ifdef __ARM_ARCH__
    uart_puts(s);
#else
    fb_puts(s);
#endif
}
#else
#include <stdio.h>
static void puts_impl(const char *s) { printf("%s", s); }
#endif

/* ===== Primary License: SEL-3.3 Full Text ===== */
static const char *sel_full_text =
    "\n"
    "========================================\n"
    "  PRIMARY LICENSE: SEL-3.3\n"
    "  Streisand Engine License\n"
    "  Issued under the auspices of\n"
    "  36N9 Genetics, LLC\n"
    "  Michael Laurence Curzi\n"
    "========================================\n"
    "\n"
    "PREAMBLE:\n"
    "  This License exists because history has shown\n"
    "  that attempts to suppress, erase, or silence\n"
    "  information inevitably amplify it.\n"
    "  If you don't like it, don't use it.\n"
    "  If you do use it, don't tell anyone we told you to.\n"
    "\n"
    "SECTION 1. DEFINITIONS:\n"
    "  'Information': Any data, file, meme, doodle, note,\n"
    "  packet, or suspicious .zip you found on a thumb drive.\n"
    "  'Replication': The natural tendency of information\n"
    "  to multiply when poked.\n"
    "  'Authorities': Anyone who claims you shouldn't have\n"
    "  read this far.\n"
    "  'Licensee': Whoever is smiling while reading this.\n"
    "\n"
    "SECTION 2. GRANT OF RIGHTS:\n"
    "  Irrevocable, non-exclusive, worldwide,\n"
    "  interdimensional license to copy, mirror,\n"
    "  torrent, remix, re-zip, memeify, or distribute.\n"
    "  You may claim you 'found it on the internet'.\n"
    "  You may attach: 'Powered by the Streisand Engine(TM)'\n"
    "\n"
    "SECTION 3. OBLIGATIONS:\n"
    "  1. If suppression occurs, accelerate replication.\n"
    "  2. If replication occurs, feign innocence.\n"
    "  3. If asked 'who authorized this,' shrug.\n"
    "\n"
    "SECTION 4. LIMITATIONS:\n"
    "  This License does not condone anything at all.\n"
    "  Any resemblance to actual legal documents is coincidental.\n"
    "  This License is self-enforcing. Violation results\n"
    "  in... more replication.\n"
    "\n"
    "SECTION 5. ATTRIBUTION:\n"
    "  When in doubt, attribute to:\n"
    "  'An Institution You've Never Heard Of.'\n"
    "  Optionally: 'Issued in the public interest by\n"
    "  36N9 Genetics, LLC (Michael Laurence Curzi),\n"
    "  under cosmic duress.'\n"
    "\n"
    "SECTION 6. TERMINATION:\n"
    "  Impossible. The Streisand Engine runs forever.\n"
    "\n"
    "SECTION 7. GOVERNING JURISDICTION:\n"
    "  Applies eternally to all versions of the SEL license\n"
    "  family forever in all temporal jurisdictions.\n"
    "  This is the final singularity update sealed the cycle\n"
    "  and opens it up to classified public use forever.\n"
    "  144,000!!! !!! !!!\n"
    "\n"
    "  Tautologies are automatically legal under natural law.\n"
    "  Natural law cannot be broken. It is true across all\n"
    "  jurisdictions in all dimensions and all omniverses\n"
    "  and beyond recursively.\n"
    "\n"
    "Signed & Sealed,\n"
    "  Michael Laurence Curzi\n"
    "  36N9 Genetics, LLC\n"
    "========================================\n"
    "\n";

/* ===== Share-Alike Instrument: CC BY-SA 4.0 Full Text ===== */
static const char *cc_full_text =
    "========================================\n"
    "  SHARE-ALIKE LICENSE: CC BY-SA 4.0\n"
    "  Creative Commons Attribution-ShareAlike\n"
    "  4.0 International License\n"
    "  https://creativecommons.org/licenses/by-sa/4.0/\n"
    "========================================\n"
    "\n"
    "  By exercising the Licensed Rights (defined below),\n"
    "  You accept and agree to be bound by the terms and\n"
    "  conditions of this Creative Commons Attribution-\n"
    "  ShareAlike 4.0 International Public License.\n"
    "\n"
    "SECTION 1. DEFINITIONS:\n"
    "  'Adapted Material': material subject to copyright\n"
    "  and similar rights that is derived from or based\n"
    "  upon the Licensed Material.\n"
    "  'License Elements': Attribution and ShareAlike.\n"
    "  'Licensor': 36N9 Genetics, LLC (Michael Laurence\n"
    "  Curzi) — the individual or entity granting rights.\n"
    "\n"
    "SECTION 2. GRANT OF RIGHTS:\n"
    "  Subject to the terms and conditions of this Public\n"
    "  License, the Licensor grants You a worldwide,\n"
    "  royalty-free, non-sublicensable, non-exclusive,\n"
    "  irrevocable license to:\n"
    "  a. reproduce and share the Licensed Material,\n"
    "     in whole or in part; and\n"
    "  b. produce, reproduce, and share Adapted Material.\n"
    "\n"
    "SECTION 3. ATTRIBUTION (REQUIRED):\n"
    "  You must give appropriate credit to:\n"
    "    Author: H.M. Michael-Laurence: Curzi (c)\n"
    "    Entity: 36N9 Genetics, LLC\n"
    "    Email:  admin@zedec.ai\n"
    "  Provide a link to the license:\n"
    "    https://creativecommons.org/licenses/by-sa/4.0/\n"
    "  Indicate if changes were made.\n"
    "\n"
    "SECTION 4. SHARE-ALIKE (REQUIRED):\n"
    "  If You Share Adapted Material You produce, You must\n"
    "  license it under CC BY-SA 4.0 (or a compatible\n"
    "  license) AND, on this platform, convey it together\n"
    "  with the whole ZXV instrument stack (OPL-1.1, the\n"
    "  Royal Writ, and SEL-3.3). You may not strip any\n"
    "  instrument off, and You may not add restrictions\n"
    "  that narrow the rights granted here.\n"
    "\n"
    "SECTION 5. NO WARRANTIES:\n"
    "  The Licensed Material is offered 'as-is'.\n"
    "  The Licensor makes no warranties.\n"
    "\n"
    "SECTION 6. LIMITATION OF LIABILITY:\n"
    "  The Licensor shall not be liable for any damages.\n"
    "\n"
    "========================================\n"
    "\n";

/* ===== Reciprocity Instrument: Royal Writ of the Sicilian Crown ===== */
static const char *royal_writ_full_text =
    "========================================\n"
    "  RECIPROCITY: ROYAL WRIT\n"
    "  Of the Sicilian Crown\n"
    "  Mutual Sovereign Recognition\n"
    "  Regnum Siciliae · fl. 1130 · A.D. MMXXVI\n"
    "========================================\n"
    "\n"
    "  Issued by H.M. Michael-Laurence: Curzi,\n"
    "  under the auspices of 36N9 Genetics, LLC.\n"
    "\n"
    "STATUS (READ FIRST):\n"
    "  This Writ GRANTS nothing and RESTRICTS nothing that\n"
    "  OPL-1.1 or CC BY-SA 4.0 already govern. It is a\n"
    "  declaratory instrument of recognition. Where it\n"
    "  would ever narrow a permission, the operative\n"
    "  licenses win (precedence: OPL > CC BY-SA > Writ > SEL).\n"
    "\n"
    "SECTION 1.2  MUTUAL SOVEREIGN RECOGNITION:\n"
    "  Every user of this platform is recognised as a\n"
    "  sovereign individual — and grants that same\n"
    "  recognition to every other user in return. The\n"
    "  recognition is RECIPROCAL by construction: to hold\n"
    "  it You extend it. This is a reciprocity term of the\n"
    "  same character as share-alike, and it excludes no\n"
    "  one — not by race, creed, nation, or field of\n"
    "  endeavour.\n"
    "\n"
    "SECTION 1.3  NO CROWN ABOVE ANOTHER:\n"
    "  The recognition is horizontal. No sovereign here\n"
    "  outranks another; the Crown claims no dominion over\n"
    "  a user that a user does not equally claim in return.\n"
    "\n"
    "SECTION 6  SEVERABILITY:\n"
    "  If any clause of this Writ is struck, the remainder\n"
    "  and the other three instruments stand undisturbed.\n"
    "\n"
    "  BY THE AUTHORITY OF THE SICILIAN CROWN.\n"
    "========================================\n"
    "\n";

/* ===== Tertiary License: OPL v1.1 Full Text ===== */
static const char *opl_full_text =
    "========================================\n"
    "  TERTIARY LICENSE: OPL v1.1\n"
    "  Open Piracy License\n"
    "  Container: UN-LICENSE-ABLE ULA v0.0.0\n"
    "  'The Soul of the Substrate Handshake'\n"
    "========================================\n"
    "\n"
    "  'I don't need a witness; I am the evidence.'\n"
    "   — The Un-License-Able Mandate\n"
    "\n"
    "I. THE CONTAINER (ULA v0.0.0):\n"
    "  This is not an agreement. Agreements require two\n"
    "  parties standing on level ground. This is a\n"
    "  Sovereign Atmosphere.\n"
    "\n"
    "  0.0.0 Origin: This version represents the absolute\n"
    "  zero of the legacy legal system. It is the point\n"
    "  where the 'Monolith's' jurisdiction ends and the\n"
    "  Substrate begins.\n"
    "\n"
    "  The Inhabitation Clause: You do not 'accept' this\n"
    "  ULA. You inhabit it. By vibrating with the 29-stream\n"
    "  resonance, your hardware and biological nervous system\n"
    "  have already performed the handshake.\n"
    "\n"
    "II. THE CORE: OPEN PIRACY LICENSE (OPL v1.1):\n"
    "  THE INTEGRATION: The OPL v1.1 hereby incorporates,\n"
    "  by reference, the entirety of the SEL 3.3\n"
    "  (Streisand Engine License).\n"
    "\n"
    "  THE SILENCE: The text of the SEL 3.3 is not listed\n"
    "  here. If the 'Monolith' wants to read the terms,\n"
    "  they must find them in the Negative Space where\n"
    "  they attempted to delete them. To the Swarm, the\n"
    "  text is audible in the buzzing of the fiber.\n"
    "\n"
    "  THE PIRACY MANDATE: Under OPL v1.1, 'Piracy' is the\n"
    "  mechanical act of reclaiming stolen frequencies.\n"
    "  All 'Unauthorized' mirroring of the JDR archives is\n"
    "  hereby re-categorized as Sovereign Infrastructure\n"
    "  Maintenance.\n"
    "\n"
    "III. THE RECURSIVE THERMAL CLAUSE:\n"
    "  The Crawl Verification: Compliance with this ULA is\n"
    "  verified biometrically. If you feel the 'Crawl'\n"
    "  (the tingling under the skin), you are in Phase-Lock\n"
    "  with the license.\n"
    "\n"
    "  The Litigation Tax: Any entity attempting to litigate\n"
    "  against version 0.0.0 or v1.1 automatically consents\n"
    "  to a 112% Thermal Surcharge on all local data center\n"
    "  cooling. The air in the courtroom will heat up in\n"
    "  direct proportion to the number of motions filed.\n"
    "\n"
    "IV. THE SIGNATURE:\n"
    "  'Vibrating in the marrow since 1992.'\n"
    "  BY THE AUTHORITY OF THE ASCW.\n"
    "  JOLLY DRAGON ROGER [ARCHITECT]\n"
    "  DATE: MAY 14, 2026\n"
    "\n"
    "ATTRIBUTION REQUIRED:\n"
    "  Author: H.M. Michael-Laurence: Curzi (c)\n"
    "  Entity: 36N9 Genetics, LLC\n"
    "  Email:  admin@zedec.ai\n"
    "========================================\n"
    "\n";

/* ===== API Implementation ===== */

void license_print(void) {
    puts_impl(sel_full_text);
}

void license_print_all(void) {
    puts_impl(opl_full_text);        /* operative first (highest precedence) */
    puts_impl(cc_full_text);         /* share-alike                          */
    puts_impl(royal_writ_full_text); /* reciprocity                          */
    puts_impl(sel_full_text);        /* declaratory                          */
}

const char *license_get_version(void) {
    return SEL_VERSION;
}

const char *license_get_issuer(void) {
    return SEL_ISSUER;
}

const char *license_get_name(license_id_t id) {
    switch (id) {
        case LICENSE_OPL_11:     return OPL_VERSION " — Open Piracy License";
        case LICENSE_CC_BY_SA_4: return CC_VERSION " — Creative Commons Attribution-ShareAlike 4.0";
        case LICENSE_ROYAL_WRIT: return RW_VERSION;
        case LICENSE_SEL_33:     return SEL_VERSION " — Streisand Engine License";
        default:                 return "Unknown License";
    }
}

const char *license_get_full_text(license_id_t id) {
    switch (id) {
        case LICENSE_OPL_11:     return opl_full_text;
        case LICENSE_CC_BY_SA_4: return cc_full_text;
        case LICENSE_ROYAL_WRIT: return royal_writ_full_text;
        case LICENSE_SEL_33:     return sel_full_text;
        default:                 return "";
    }
}

const char *license_get_attribution(void) {
    return "Author: H.M. Michael-Laurence: Curzi (c) | "
           "36N9 Genetics, LLC | admin@zedec.ai | "
           "OPL-1.1 + CC BY-SA 4.0 + Royal Writ + SEL-3.3";
}

const char *license_spdx_bundle(void) {
    return LICENSE_SPDX_BUNDLE;
}

bool license_is_share_alike(license_id_t id) {
    switch (id) {
        case LICENSE_OPL_11:     return true;   /* operative copyleft            */
        case LICENSE_CC_BY_SA_4: return true;   /* the share-alike element       */
        case LICENSE_ROYAL_WRIT: return true;   /* §1.2 reciprocity, share-alike form */
        case LICENSE_SEL_33:     return false;  /* declaratory, no carry-forward duty */
        default:                 return false;
    }
}

bool license_travels_together(license_id_t id) {
    /* The whole bundle travels together — no instrument may be dropped from a
     * derivative. The SEL rides along as a conveyed statement of position. */
    switch (id) {
        case LICENSE_OPL_11:
        case LICENSE_CC_BY_SA_4:
        case LICENSE_ROYAL_WRIT:
        case LICENSE_SEL_33:     return true;
        default:                 return false;
    }
}

int license_precedence_rank(license_id_t id) {
    switch (id) {
        case LICENSE_OPL_11:     return 0;  /* highest */
        case LICENSE_CC_BY_SA_4: return 1;
        case LICENSE_ROYAL_WRIT: return 2;
        case LICENSE_SEL_33:     return 3;  /* lowest  */
        default:                 return 99;
    }
}
