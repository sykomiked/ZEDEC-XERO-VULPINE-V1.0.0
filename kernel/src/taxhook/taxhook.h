/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* taxhook.h — tax-calculation hooks: signed per-jurisdiction rate tables
 * for VAT, GST and sales tax (roadmap R3.5).
 *
 * NOT TAX ADVICE. This code computes tax from a table someone else signed.
 * It is "designed to follow" published rate rules; whether a table is right
 * for a country, and which rounding convention that country accepts, is for
 * a tax adviser to sign off per country. The tables in taxhook_sample.c are
 * SAMPLE DATA, dated, and flagged TAXHOOK_F_SAMPLE so every receipt made
 * from them says so.
 *
 * OFF BY DEFAULT. taxhook_init leaves the hook disabled; taxhook_compute then
 * returns TAXHOOK_E_DISABLED and computes nothing, so the app shows prices
 * without a tax line. taxhook_enable(h, true) turns it on.
 *
 * RULES
 *   X1 TABLE. A signed manifest (cp_common.h M1-M3, kind CP_KIND_TAXTABLE):
 *      header country = ISO 3166-1 alpha-2, header region = the ISO 3166-2
 *      subdivision the table covers ("" for a national table, "NY" for New
 *      York State). Body, little-endian:
 *        tax kind u8 (VAT, GST, SALES) | ccy u16 (ISO 4217 numeric) |
 *        rounding u8 (HALF_UP, HALF_EVEN) | level u8 (PER_LINE, PER_INVOICE) |
 *        flags u8 (REVERSE_CHARGE, SAMPLE) | label [32] (printable ASCII,
 *        zero padded) | n u8 (<= TAXHOOK_MAX_RATES), n x
 *        {category u8, region[6], rate_ppm u32, from u32, to u32}
 *      rate_ppm is parts per million (20% = 200000, 8.875% = 88750).
 *      from/to are dates as YYYYMMDD, inclusive; to = 0 means open-ended.
 *      Decoding is exact (no trailing bytes) and rejects: a bad date, to <
 *      from, a ZERO-category entry with a nonzero rate, a rate above 100%,
 *      the EXEMPT category (exempt needs no rate), and two entries for the
 *      same (category, region) whose date ranges overlap.
 *   X2 INSTALL (fail closed). As capctl C2: exact decode, valid signature by
 *      a key scoped to CP_KIND_TAXTABLE and that country, `now` inside the
 *      validity window, version strictly above the installed table for the
 *      same (country, region, kind). Nothing else replaces a table.
 *   X3 RATE. For a line of category C on tax-point date D, the rate is the
 *      SUM of every entry of category C in force on D whose region is ""
 *      (the whole table) or one of the invoice's regions (for US sales tax:
 *      state + city + district). No entry in force = TAXHOOK_E_NO_RATE:
 *      the hook never guesses a rate. EXEMPT lines carry no tax and need no
 *      entry. A table outside its signature validity window at `now` is
 *      TAXHOOK_E_EXPIRED.
 *   X4 PRICES. Tax-exclusive: amount is net; tax = round(net * r / 10^6).
 *      Tax-inclusive: amount is gross; tax = round(gross * r / (10^6 + r)),
 *      net = gross - tax. Amount = qty * unit_price, in minor units.
 *   X5 ROUNDING. To the nearest minor unit. HALF_UP: an exact half goes up
 *      (amounts are never negative, so this is also "half away from zero").
 *      HALF_EVEN: an exact half goes to the even neighbour. PER_LINE rounds
 *      each line's tax and adds them; PER_INVOICE adds the amounts of all
 *      lines with the same (category, rate) and rounds once per group.
 *   X6 REVERSE CHARGE. When the table has TAXHOOK_F_REVERSE_CHARGE, the
 *      tax kind is VAT or GST, the buyer is a business and the seller's and
 *      buyer's countries differ, no tax is charged: every group shows rate 0
 *      and the receipt says the buyer accounts for the tax. Inclusive
 *      amounts are then treated as net. (A simplification: real place-of-
 *      supply rules depend on what is sold; the adviser decides whether a
 *      table may set this flag.)
 *   X7 RANGE. qty 1..2^20, unit_price <= 2^40, every line and group amount
 *      <= 2^40 minor units, at most TAXHOOK_MAX_LINES lines, combined rate
 *      <= 100%. Outside that: TAXHOOK_E_RANGE. All products fit in 64 bits;
 *      there is no float anywhere.
 *   X8 RECEIPT. taxhook_receipt_t holds per-line amounts, the per-(category,
 *      rate) breakdown and the totals; taxhook_receipt_text renders it as
 *      plain text, starting with a SAMPLE warning when the table is sample.
 *
 * HONEST LIMITS. No credit notes or negative amounts, no fractional
 * quantities, no per-unit or compound taxes (excise, tax on tax), no tax
 * holidays beyond what effective-date ranges express, no address-to-
 * jurisdiction lookup (the caller passes the regions), no filing. Rounding
 * conventions differ by country and sometimes by business choice. Tested on
 * the host against worked examples derived by hand; not checked by a tax
 * adviser. Freestanding C11: no allocation, no float, no libc.
 */
#ifndef ZXV_TAXHOOK_H
#define ZXV_TAXHOOK_H

#include "../capctl/cp_common.h"

#define TAXHOOK_MAX_RATES  32u
#define TAXHOOK_MAX_LINES  64u
#define TAXHOOK_MAX_TABLES 8u
#define TAXHOOK_MAX_REGION 3u
#define TAXHOOK_LABEL_LEN  32u
#define TAXHOOK_PPM        1000000u
#define TAXHOOK_AMOUNT_MAX ((uint64_t) 1 << 40)
#define TAXHOOK_QTY_MAX    (1u << 20)
#define TAXHOOK_MANIFEST_MAX                                                                       \
    (CP_HDR_LEN + 1u + 2u + 1u + 1u + 1u + TAXHOOK_LABEL_LEN + 1u + TAXHOOK_MAX_RATES * 19u)

typedef enum { TAXHOOK_VAT = 1, TAXHOOK_GST = 2, TAXHOOK_SALES = 3 } taxhook_kind_t;
typedef enum {
    TAXHOOK_STANDARD = 1,
    TAXHOOK_REDUCED = 2,
    TAXHOOK_SUPER_REDUCED = 3,
    TAXHOOK_ZERO = 4,
    TAXHOOK_EXEMPT = 5
} taxhook_category_t;
typedef enum { TAXHOOK_HALF_UP = 1, TAXHOOK_HALF_EVEN = 2 } taxhook_rounding_t;
typedef enum { TAXHOOK_PER_LINE = 1, TAXHOOK_PER_INVOICE = 2 } taxhook_level_t;

#define TAXHOOK_F_REVERSE_CHARGE 0x01u
#define TAXHOOK_F_SAMPLE         0x02u
#define TAXHOOK_F_ALL            0x03u

typedef enum {
    TAXHOOK_OK = 0,
    TAXHOOK_E_ARG = -1,
    TAXHOOK_E_DECODE = -2,
    TAXHOOK_E_SIG = -3,
    TAXHOOK_E_WINDOW = -4, /* install: now outside the validity window */
    TAXHOOK_E_VERSION = -5,
    TAXHOOK_E_FULL = -6,
    TAXHOOK_E_DISABLED = -7,
    TAXHOOK_E_NO_TABLE = -8,
    TAXHOOK_E_EXPIRED = -9, /* compute: table outside its validity window */
    TAXHOOK_E_NO_RATE = -10,
    TAXHOOK_E_RANGE = -11
} taxhook_status_t;

typedef struct {
    uint8_t category;
    char region[CP_REGION_LEN];
    uint32_t rate_ppm;
    uint32_t from, to; /* YYYYMMDD, to = 0 open */
} taxhook_rate_t;

typedef struct {
    cp_hdr_t hdr; /* kind CP_KIND_TAXTABLE */
    uint8_t kind;
    uint16_t ccy;
    uint8_t rounding, level, flags;
    char label[TAXHOOK_LABEL_LEN];
    taxhook_rate_t rate[TAXHOOK_MAX_RATES];
    uint8_t n_rates;
} taxhook_table_t;

typedef struct {
    uint8_t category;
    uint32_t qty;
    uint64_t unit_price; /* minor units; net or gross per the invoice */
} taxhook_line_t;

typedef struct {
    uint32_t date; /* tax point, YYYYMMDD */
    bool inclusive;
    char regions[TAXHOOK_MAX_REGION][CP_REGION_LEN]; /* unused ones all zero */
    char seller[2], buyer[2];
    bool buyer_business;
    const taxhook_line_t *lines;
    uint32_t n_lines;
} taxhook_invoice_t;

typedef struct {
    uint8_t category;
    uint32_t rate_ppm;
    uint64_t amount; /* qty * unit_price as entered */
    uint64_t tax;    /* PER_LINE only; 0 under PER_INVOICE (see groups) */
} taxhook_rline_t;

typedef struct {
    uint8_t category;
    uint32_t rate_ppm;
    uint64_t net, tax, gross;
} taxhook_group_t;

typedef struct {
    char country[2];
    char region[CP_REGION_LEN];
    uint8_t kind, rounding, level, flags;
    uint32_t version;
    bool inclusive, reverse_charge;
    taxhook_rline_t line[TAXHOOK_MAX_LINES];
    uint32_t n_lines;
    taxhook_group_t group[TAXHOOK_MAX_LINES];
    uint32_t n_groups;
    uint64_t net, tax, gross;
} taxhook_receipt_t;

typedef struct {
    bool enabled;
    cp_verify_fn verify;
    void *vctx;
    taxhook_table_t table[TAXHOOK_MAX_TABLES];
    uint32_t n_tables;
} taxhook_t;

void taxhook_init(taxhook_t *h, cp_verify_fn verify, void *vctx);
void taxhook_enable(taxhook_t *h, bool on);

size_t taxhook_encode(const taxhook_table_t *t, uint8_t *out, size_t cap);
taxhook_status_t taxhook_decode(const uint8_t *m, size_t len, taxhook_table_t *out);
taxhook_status_t taxhook_install(taxhook_t *h, const uint8_t *m, size_t len, const uint8_t *sig,
                                 size_t sig_len, uint64_t now);
const taxhook_table_t *taxhook_table(const taxhook_t *h, const char country[2],
                                     const char region[CP_REGION_LEN], uint8_t kind);

/* X3-X7: compute with the installed table for (country, region, kind). */
taxhook_status_t taxhook_compute(const taxhook_t *h, const char country[2],
                                 const char region[CP_REGION_LEN], uint8_t kind,
                                 const taxhook_invoice_t *inv, uint64_t now,
                                 taxhook_receipt_t *out);
/* The same arithmetic on a table that is not installed (no signature or
 * window check, no enable flag): for previews and tests only. */
taxhook_status_t taxhook_compute_table(const taxhook_table_t *t, const taxhook_invoice_t *inv,
                                       taxhook_receipt_t *out);

/* X5: round(n / d) under a rounding mode (d > 0). */
uint64_t taxhook_round_div(uint64_t n, uint64_t d, uint8_t rounding);

/* X8: plain-text receipt; minor = digits after the decimal point (0..4).
 * Returns the length written (NUL-terminated), or 0 if it does not fit. */
size_t taxhook_receipt_text(const taxhook_receipt_t *r, unsigned minor, char *out, size_t cap);

const char *taxhook_status_name(taxhook_status_t s);

#endif /* ZXV_TAXHOOK_H */
