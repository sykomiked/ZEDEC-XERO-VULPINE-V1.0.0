/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* taxhook_sample.c — SAMPLE DATA, NOT TAX ADVICE. See taxhook_sample.h:
 * written from publicly reported rates as understood on 2026-10-12; a tax
 * adviser signs off per country before any real use. */
#include "taxhook_sample.h"

static void label(taxhook_table_t *t, const char *s)
{
    for (unsigned i = 0; i < TAXHOOK_LABEL_LEN - 1 && s[i]; i++) t->label[i] = s[i];
}

static void region(char out[CP_REGION_LEN], const char *s)
{
    for (unsigned i = 0; i < CP_REGION_LEN && s[i]; i++) out[i] = s[i];
}

static void rate(taxhook_table_t *t, uint8_t cat, const char *reg, uint32_t ppm, uint32_t from,
                 uint32_t to)
{
    taxhook_rate_t *r = &t->rate[t->n_rates++];
    r->category = cat;
    region(r->region, reg);
    r->rate_ppm = ppm;
    r->from = from;
    r->to = to;
}

static void start(taxhook_table_t *t, const char *cc, const char *reg, uint8_t kind, uint16_t ccy)
{
    cp_zero(t, sizeof *t);
    t->hdr.kind = CP_KIND_TAXTABLE;
    t->hdr.country[0] = cc[0];
    t->hdr.country[1] = cc[1];
    region(t->hdr.region, reg);
    t->hdr.version = 1;
    t->kind = kind;
    t->ccy = ccy;
    t->rounding = TAXHOOK_HALF_UP;
    t->level = TAXHOOK_PER_INVOICE;
    t->flags = TAXHOOK_F_SAMPLE;
}

void taxhook_sample_gb_vat(taxhook_table_t *t)
{
    start(t, "GB", "", TAXHOOK_VAT, 826);
    t->flags |= TAXHOOK_F_REVERSE_CHARGE;
    label(t, "SAMPLE GB VAT " TAXHOOK_SAMPLE_ASOF);
    rate(t, TAXHOOK_STANDARD, "", 200000, 20110104, 0);
    rate(t, TAXHOOK_REDUCED, "", 50000, 20110104, 0);
    rate(t, TAXHOOK_ZERO, "", 0, 20110104, 0);
}

void taxhook_sample_de_vat(taxhook_table_t *t)
{
    start(t, "DE", "", TAXHOOK_VAT, 978);
    t->flags |= TAXHOOK_F_REVERSE_CHARGE;
    label(t, "SAMPLE DE USt " TAXHOOK_SAMPLE_ASOF);
    rate(t, TAXHOOK_STANDARD, "", 190000, 20070101, 20200630);
    rate(t, TAXHOOK_STANDARD, "", 160000, 20200701, 20201231);
    rate(t, TAXHOOK_STANDARD, "", 190000, 20210101, 0);
    rate(t, TAXHOOK_REDUCED, "", 70000, 20070101, 20200630);
    rate(t, TAXHOOK_REDUCED, "", 50000, 20200701, 20201231);
    rate(t, TAXHOOK_REDUCED, "", 70000, 20210101, 0);
    rate(t, TAXHOOK_ZERO, "", 0, 20070101, 0);
}

void taxhook_sample_us_ny_sales(taxhook_table_t *t)
{
    start(t, "US", "NY", TAXHOOK_SALES, 840);
    label(t, "SAMPLE US-NY sales " TAXHOOK_SAMPLE_ASOF);
    rate(t, TAXHOOK_STANDARD, "", 40000, 20050601, 0);
    rate(t, TAXHOOK_STANDARD, "NYC", 45000, 20090801, 0);
    rate(t, TAXHOOK_STANDARD, "MCTD", 3750, 20050601, 0);
}
