/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_tables.h — ISO 3166-1 alpha-2 country code table (generated into
 * pay_tables.c by gen_pay_tables.py from Debian iso-codes 4.16.0). The
 * platform's ISO 4217 table lives in kernel/src/cbank, not here.
 *
 * HONEST LIMITS. This is a snapshot of a public code list; it goes stale when
 * the ISO 3166 Maintenance Agency adds or withdraws codes. Schema validity is
 * not certification; there is no SWIFT or CIPS connectivity; operating as a
 * bank or money transmitter needs licences; store-credit classification of
 * VFV is a legal question for counsel.
 */
#ifndef ZXV_PAY_TABLES_H
#define ZXV_PAY_TABLES_H

#include <stdint.h>
#include <stdbool.h>
#include "pay_rails.h" /* canonical rail numerics 555/777/888 and NCR/NRE/PNS */

#define PAY_ISO3166_COUNT 249u

extern const char pay_iso3166_a2[PAY_ISO3166_COUNT][3];

/* True iff `cc` is an assigned ISO 3166-1 alpha-2 code (exactly 2 letters).
 * "NC", "NR" and "PN" are valid (New Caledonia, Nauru, Pitcairn); the rail
 * jurisdictions "NCR", "NRE" and "PNS" are not, and pay_iso never maps one
 * to the other. */
bool pay_iso3166_valid(const char *cc);

#endif /* ZXV_PAY_TABLES_H */
