/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* taxhook_sample.h — SAMPLE tax tables for taxhook.h. NOT TAX ADVICE.
 *
 * These tables are SAMPLE DATA for tests, demos and as a starting point for
 * a tax adviser. They were written from publicly reported rates as
 * understood on TAXHOOK_SAMPLE_ASOF and have NOT been checked by a tax
 * adviser. Before any table is used for real invoices, a tax adviser signs
 * off per country: the rates, their dates, the categories each product
 * falls in, the rounding convention and whether reverse charge applies.
 * Each table carries TAXHOOK_F_SAMPLE, so every receipt computed from it
 * starts with a SAMPLE warning, and a label that says SAMPLE and the date.
 *
 * The functions fill the body and the header's country/region/kind and set
 * version 1. The caller sets not_before, not_after and key_id, encodes the
 * table and signs it; nothing here is signed.
 *
 *   GB VAT        standard 20% and reduced 5% from 2011-01-04, zero 0%.
 *                 Half-up, per invoice, reverse charge flag set.
 *   DE USt (VAT)  standard 19% and reduced 7%, with the temporary 16% / 5%
 *                 from 2020-07-01 to 2020-12-31. Half-up, per invoice,
 *                 reverse charge flag set.
 *   US-NY sales   New York State 4% (region "" = the whole table), New York
 *                 City local 4.5% (region "NYC") and the Metropolitan
 *                 Commuter Transportation District 0.375% (region "MCTD"):
 *                 8.875% combined in New York City. Half-up, per invoice.
 * Simplifications: only these categories, no product mapping, no thresholds
 * or special schemes, and the sales-tax table ignores exemptions that differ
 * between state and city (for example some clothing).
 */
#ifndef ZXV_TAXHOOK_SAMPLE_H
#define ZXV_TAXHOOK_SAMPLE_H

#include "taxhook.h"

#define TAXHOOK_SAMPLE_ASOF "2026-10-12"

void taxhook_sample_gb_vat(taxhook_table_t *t);
void taxhook_sample_de_vat(taxhook_table_t *t);
void taxhook_sample_us_ny_sales(taxhook_table_t *t);

#endif /* ZXV_TAXHOOK_SAMPLE_H */
