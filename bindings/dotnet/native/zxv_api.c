/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_api.c — hosted implementation of the zxv_api.h C ABI.
 *
 * This is the ONLY file in bindings/ that includes kernel headers. Every
 * kernel struct is filled and read here and nowhere else, so a change to a
 * kernel module touches this adapter, never a managed binding.
 *
 * Backed today by (stable kernel modules):
 *   kernel/src/iso20022     pacs.008 / camt.053 serializers, rail caveats
 *   kernel/src/finance      triple_ledger (financial, provenance, externality)
 *   kernel/src/vino_stores  rail numerics 846 / 810 / 888 (lockstep-checked)
 * Optional, switched on by the Makefile when the header is present:
 *   ZXV_HAVE_CBANK_CCY      kernel/src/cbank/cb_ccy.h  ISO 4217 + AU tables
 *   ZXV_HAVE_CN_CHECK       kernel/src/cardnet/cn_check.h  PAN check digits
 * Stubbed (ZXV_E_NOTIMPL), marked "TODO(kernel-...)":
 *   netting cycles, VSS conformance (cbank cb_net.h / cb_vss.h),
 *   pacs.002/004/009, camt.056/029 (cbank cb_mx.h),
 *   card issue/reissue/authorize (cardnet cardnet.h).
 *
 * Hosted build: this file uses libc (malloc, pthreads) and compiles the
 * kernel modules with -DTEST_HOST, where triple_ledger's surplus_real_t is an
 * IEEE double. Exactness is guaranteed by the ABI, not by the double: every
 * amount and balance is bounded by ZXV_AMOUNT_MAX (2^53 - 1), the adapter
 * keeps an exact int64 shadow of every account, and zxv_ledger_check
 * verifies shadow == triple-ledger financial axis.
 */
#define _POSIX_C_SOURCE 200809L
#include "zxv_api.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iso20022.h"
#include "triple_ledger.h"
#include "vino_stores.h"

#ifdef ZXV_HAVE_CBANK_CCY
#    include "cb_ccy.h"
#endif
#ifdef ZXV_HAVE_CN_CHECK
#    include "cn_check.h"
#endif

/* The rail numerics are duplicated in iso20022.h and vino_stores.h on
 * purpose (iso20022 stays dependency-free). Divergence is a build error. */
_Static_assert(ISO_CCY_DEBIT == VINO_ISO_DEBIT, "DEBIT rail numeric diverged");
_Static_assert(ISO_CCY_CREDIT == VINO_ISO_CREDIT, "CREDIT rail numeric diverged");
_Static_assert(ISO_CCY_EQUITY == VINO_ISO_EQUITY, "EQUITY rail numeric diverged");

/* ===================================================================== */
/* Last error (thread-local, R7)                                         */
/* ===================================================================== */

static _Thread_local char g_err[256];

static int32_t fail(int32_t status, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static int32_t fail(int32_t status, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    return status;
}

static int32_t ok(void)
{
    g_err[0] = '\0';
    return ZXV_OK;
}

/* R4: copy `s` (length n) to a caller buffer. */
static int32_t out_text_n(const char *s, size_t n, char *buf, size_t cap, size_t *out_len)
{
    if (!out_len) return fail(ZXV_E_ARG, "out_len is NULL");
    *out_len = n;
    if (!buf || cap <= n) return fail(ZXV_E_BUFFER, "buffer needs %zu bytes plus NUL", n);
    memcpy(buf, s, n);
    buf[n] = '\0';
    return ok();
}

static int32_t out_text(const char *s, char *buf, size_t cap, size_t *out_len)
{
    return out_text_n(s, strlen(s), buf, cap, out_len);
}

/* R5: strict UTF-8 (no overlongs, no surrogates, <= U+10FFFF). */
static bool utf8_valid(const char *str)
{
    const unsigned char *s = (const unsigned char *) str;
    while (*s) {
        unsigned char c = *s;
        if (c < 0x80) {
            s++;
            continue;
        }
        int n;
        uint32_t cp;
        if (c >= 0xC2 && c <= 0xDF) {
            n = 1;
            cp = c & 0x1Fu;
        } else if (c >= 0xE0 && c <= 0xEF) {
            n = 2;
            cp = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            n = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        for (int i = 1; i <= n; i++) {
            if ((s[i] & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (s[i] & 0x3Fu);
        }
        if ((n == 2 && cp < 0x800u) || (n == 3 && cp < 0x10000u) || cp > 0x10FFFFu ||
            (cp >= 0xD800u && cp <= 0xDFFFu))
            return false;
        s += n + 1;
    }
    return true;
}

/* Validate a string argument: non-NULL, UTF-8, 1..max bytes. */
static int32_t check_str(const char *s, size_t max, const char *what, bool allow_empty)
{
    if (!s) return fail(ZXV_E_ARG, "%s is NULL", what);
    if (!utf8_valid(s)) return fail(ZXV_E_UTF8, "%s is not valid UTF-8", what);
    size_t n = strlen(s);
    if (n > max) return fail(ZXV_E_TOO_LONG, "%s is %zu bytes; maximum is %zu", what, n, max);
    if (n == 0 && !allow_empty) return fail(ZXV_E_ARG, "%s is empty", what);
    return ZXV_OK;
}

static bool is_upper3(const char *s)
{
    return s && strlen(s) == 3 && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z' &&
           s[2] >= 'A' && s[2] <= 'Z';
}

/* ===================================================================== */
/* Library                                                               */
/* ===================================================================== */

int32_t zxv_version(uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    if (major) *major = ZXV_ABI_VERSION_MAJOR;
    if (minor) *minor = ZXV_ABI_VERSION_MINOR;
    if (patch) *patch = ZXV_ABI_VERSION_PATCH;
    return ok();
}

int32_t zxv_features(uint32_t *out_mask)
{
    if (!out_mask) return fail(ZXV_E_ARG, "out_mask is NULL");
    uint32_t m = ZXV_FEAT_LEDGER | ZXV_FEAT_MX_PACS008 | ZXV_FEAT_MX_CAMT053;
#ifdef ZXV_HAVE_CBANK_CCY
    m |= ZXV_FEAT_ISO4217;
#endif
#ifdef ZXV_HAVE_CN_CHECK
    m |= ZXV_FEAT_CARD_CHECK;
#endif
    *out_mask = m;
    return ok();
}

const char *zxv_status_name(int32_t status)
{
    switch (status) {
    case ZXV_OK: return "ZXV_OK";
    case ZXV_E_ARG: return "ZXV_E_ARG";
    case ZXV_E_BUFFER: return "ZXV_E_BUFFER";
    case ZXV_E_NOTIMPL: return "ZXV_E_NOTIMPL";
    case ZXV_E_NOT_FOUND: return "ZXV_E_NOT_FOUND";
    case ZXV_E_CAPACITY: return "ZXV_E_CAPACITY";
    case ZXV_E_FUNDS: return "ZXV_E_FUNDS";
    case ZXV_E_CURRENCY: return "ZXV_E_CURRENCY";
    case ZXV_E_RANGE: return "ZXV_E_RANGE";
    case ZXV_E_STATE: return "ZXV_E_STATE";
    case ZXV_E_INVARIANT: return "ZXV_E_INVARIANT";
    case ZXV_E_UTF8: return "ZXV_E_UTF8";
    case ZXV_E_TOO_LONG: return "ZXV_E_TOO_LONG";
    case ZXV_E_DUPLICATE: return "ZXV_E_DUPLICATE";
    case ZXV_E_NOMEM: return "ZXV_E_NOMEM";
    case ZXV_E_DECLINED: return "ZXV_E_DECLINED";
    case ZXV_E_FIELD: return "ZXV_E_FIELD";
    case ZXV_E_INTERNAL: return "ZXV_E_INTERNAL";
    default: return "ZXV_E_UNKNOWN";
    }
}

int32_t zxv_last_error(char *buf, size_t cap, size_t *out_len)
{
    /* Do not route through out_text: that would overwrite g_err itself. */
    if (!out_len) return ZXV_E_ARG;
    size_t n = strlen(g_err);
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1);
    return ZXV_OK;
}

/* ===================================================================== */
/* ISO 4217 reference data                                               */
/* ===================================================================== */

#ifdef ZXV_HAVE_CBANK_CCY
static uint32_t ccy_flags(const cb_ccy_t *c)
{
    uint32_t f = 0;
    if (c->flags & CB_CCYF_FUND) f |= ZXV_CCYF_FUND;
    if (c->flags & CB_CCYF_NA) f |= ZXV_CCYF_MINOR_NA;
    if (c->flags & CB_CCYF_NOCTRY) f |= ZXV_CCYF_NOCTRY;
    if (cb_ccy_payable(c)) f |= ZXV_CCYF_PAYABLE;
    return f;
}
#endif

/* Internal: ISO lookup. Returns ZXV_OK, ZXV_E_NOT_FOUND or ZXV_E_NOTIMPL. */
static int32_t iso_lookup(const char *alpha, uint16_t *num, uint8_t *minor, uint32_t *flags)
{
#ifdef ZXV_HAVE_CBANK_CCY
    const cb_ccy_t *c = is_upper3(alpha) ? cb_ccy_by_alpha(alpha) : NULL;
    if (!c) return ZXV_E_NOT_FOUND;
    if (num) *num = c->num;
    if (minor) *minor = c->minor == CB_MINOR_NA ? (uint8_t) ZXV_MINOR_NA : c->minor;
    if (flags) *flags = ccy_flags(c);
    return ZXV_OK;
#else
    (void) alpha;
    (void) num;
    (void) minor;
    (void) flags;
    /* TODO(kernel-cbank): without cb_ccy.h there is no ISO 4217 table. */
    return ZXV_E_NOTIMPL;
#endif
}

int32_t zxv_iso4217_by_alpha(const char *alpha, uint16_t *out_numeric, uint8_t *out_minor_units,
                             uint32_t *out_flags)
{
    if (!alpha) return fail(ZXV_E_ARG, "alpha is NULL");
    int32_t rc = iso_lookup(alpha, out_numeric, out_minor_units, out_flags);
    if (rc == ZXV_E_NOTIMPL) return fail(rc, "ISO 4217 table not built in (kernel/src/cbank)");
    if (rc == ZXV_E_NOT_FOUND) return fail(rc, "'%.8s' is not an ISO 4217 currency", alpha);
    return ok();
}

int32_t zxv_iso4217_by_numeric(uint16_t numeric, char *alpha_buf, size_t cap, size_t *out_len,
                               uint8_t *out_minor_units, uint32_t *out_flags)
{
#ifdef ZXV_HAVE_CBANK_CCY
    const cb_ccy_t *c = cb_ccy_by_num(numeric);
    if (!c) {
        const char *cav = iso20022_ccy_caveat(numeric);
        return fail(ZXV_E_NOT_FOUND, "%u is not an active ISO 4217 numeric%s%s", numeric,
                    *cav ? ": " : "", cav);
    }
    int32_t rc = out_text(c->alpha, alpha_buf, cap, out_len);
    if (rc != ZXV_OK) return rc;
    if (out_minor_units)
        *out_minor_units = c->minor == CB_MINOR_NA ? (uint8_t) ZXV_MINOR_NA : c->minor;
    if (out_flags) *out_flags = ccy_flags(c);
    return ok();
#else
    (void) numeric; (void) alpha_buf; (void) cap; (void) out_len;
    (void) out_minor_units; (void) out_flags;
    return fail(ZXV_E_NOTIMPL, "ISO 4217 table not built in (kernel/src/cbank)");
#endif
}

int32_t zxv_iso4217_name(const char *alpha, char *buf, size_t cap, size_t *out_len)
{
#ifdef ZXV_HAVE_CBANK_CCY
    if (!alpha) return fail(ZXV_E_ARG, "alpha is NULL");
    const cb_ccy_t *c = is_upper3(alpha) ? cb_ccy_by_alpha(alpha) : NULL;
    if (!c) return fail(ZXV_E_NOT_FOUND, "'%.8s' is not an ISO 4217 currency", alpha);
    return out_text(c->name, buf, cap, out_len);
#else
    (void) alpha; (void) buf; (void) cap; (void) out_len;
    return fail(ZXV_E_NOTIMPL, "ISO 4217 table not built in (kernel/src/cbank)");
#endif
}

int32_t zxv_iso4217_count(uint32_t *out_count)
{
    if (!out_count) return fail(ZXV_E_ARG, "out_count is NULL");
#ifdef ZXV_HAVE_CBANK_CCY
    *out_count = CB_CCY_COUNT;
    return ok();
#else
    return fail(ZXV_E_NOTIMPL, "ISO 4217 table not built in (kernel/src/cbank)");
#endif
}

int32_t zxv_iso4217_at(uint32_t index, char *alpha_buf, size_t cap, size_t *out_len)
{
#ifdef ZXV_HAVE_CBANK_CCY
    if (index >= CB_CCY_COUNT) return fail(ZXV_E_NOT_FOUND, "index %u out of range", index);
    return out_text(cb_ccy_tbl[index].alpha, alpha_buf, cap, out_len);
#else
    (void) index; (void) alpha_buf; (void) cap; (void) out_len;
    return fail(ZXV_E_NOTIMPL, "ISO 4217 table not built in (kernel/src/cbank)");
#endif
}

int32_t zxv_iso4217_published(char *buf, size_t cap, size_t *out_len)
{
#ifdef ZXV_HAVE_CBANK_CCY
    return out_text(cb_iso4217_published, buf, cap, out_len);
#else
    (void) buf; (void) cap; (void) out_len;
    return fail(ZXV_E_NOTIMPL, "ISO 4217 table not built in (kernel/src/cbank)");
#endif
}

int32_t zxv_ccy_caveat(uint16_t numeric, char *buf, size_t cap, size_t *out_len)
{
    return out_text(iso20022_ccy_caveat(numeric), buf, cap, out_len);
}

int32_t zxv_au_is_member(const char *country_a2, int32_t *out_is_member)
{
    if (!country_a2 || !out_is_member) return fail(ZXV_E_ARG, "NULL argument");
#ifdef ZXV_HAVE_CBANK_CCY
    *out_is_member = cb_is_au_member(country_a2) ? 1 : 0;
    return ok();
#else
    return fail(ZXV_E_NOTIMPL, "AU member table not built in (kernel/src/cbank)");
#endif
}

int32_t zxv_rail_numeric(int32_t rail, uint16_t *out_numeric)
{
    if (!out_numeric) return fail(ZXV_E_ARG, "out_numeric is NULL");
    switch (rail) {
    case ZXV_RAIL_DEBIT: *out_numeric = (uint16_t) iso20022_rail_ccy(ISO_RAIL_DEBIT); break;
    case ZXV_RAIL_CREDIT: *out_numeric = (uint16_t) iso20022_rail_ccy(ISO_RAIL_CREDIT); break;
    case ZXV_RAIL_EQUITY: *out_numeric = (uint16_t) iso20022_rail_ccy(ISO_RAIL_EQUITY); break;
    default: return fail(ZXV_E_ARG, "unknown rail %d", rail);
    }
    return ok();
}

/* ===================================================================== */
/* Context                                                               */
/* ===================================================================== */

#define ZXV_MAX_CCY      64u
#define ZXV_MAX_ACCOUNTS (1u << 20) /* platform accounts per context        */
#define ZXV_TL_ACCOUNTS  512u /* == triple_ledger_t.accounts (one book)    */
#define ZXV_TL_ENTRY_CAP 256u /* == account_t.entries (one segment)        */
#define ZXV_TL_PER_POST  3u   /* financial + provenance + externality axes */
#define ZXV_REF_MAX      35u
#define ZXV_NAME_MAX     63u
#define ZXV_CODE_MAX     8u

typedef struct {
    char code[ZXV_CODE_MAX + 1];
    uint8_t minor;
    uint32_t flags;
} zxv_ccy_cfg;

typedef struct {
    uint64_t entry_id;
    int64_t amount; /* + received, - sent */
    uint32_t counterparty;
    char ref[ZXV_REF_MAX + 1];
} zxv_line;

/* The kernel triple_ledger is fixed-size by design (freestanding): 512
 * accounts per book and 256 entries per account. The adapter keeps the
 * platform unbounded by SEGMENTING: a platform account is a chain of
 * triple-ledger accounts ("segments"), opened in a chain of books. When a
 * segment's journal is full the next posting opens a new segment; when a
 * book is full a new book is allocated. The financial-axis balance of an
 * account is the sum over its segments, and zxv_ledger_check verifies that
 * sum against the exact int64 shadow. */
typedef struct {
    uint32_t book;
    uint32_t tl_id;
} zxv_seg;

typedef struct {
    char name[ZXV_NAME_MAX + 1];
    char ccy[ZXV_CODE_MAX + 1];
    int32_t kind;
    zxv_seg *segs; /* triple-ledger segments, oldest first; last is live */
    uint32_t n_segs, cap_segs;
    int64_t debit;  /* 846: sum received */
    int64_t credit; /* 810: sum sent     */
    zxv_line *lines;
    uint32_t n_lines, cap_lines;
} zxv_acct;

struct zxv_ctx {
    pthread_mutex_t mu;
    triple_ledger_t **books; /* allocated lazily, ~sizeof(triple_ledger_t) each */
    uint32_t n_books, cap_books;
    uint64_t next_entry_id;
    zxv_ccy_cfg ccy[ZXV_MAX_CCY];
    uint32_t n_ccy;
    zxv_acct *acct;
    uint32_t n_acct, cap_acct;
    /* Reference uniqueness: open-addressing set of owned strings. */
    char **refs;
    size_t refs_cap, refs_n;
};

int32_t zxv_ctx_create(zxv_ctx **out_ctx)
{
    if (!out_ctx) return fail(ZXV_E_ARG, "out_ctx is NULL");
    zxv_ctx *c = calloc(1, sizeof *c);
    if (!c) return fail(ZXV_E_NOMEM, "context allocation failed");
    if (pthread_mutex_init(&c->mu, NULL) != 0) {
        free(c);
        return fail(ZXV_E_INTERNAL, "mutex init failed");
    }
    c->next_entry_id = 1;
    *out_ctx = c;
    return ok();
}

void zxv_ctx_destroy(zxv_ctx *ctx)
{
    if (!ctx) return;
    for (uint32_t i = 0; i < ctx->n_acct; i++) {
        free(ctx->acct[i].lines);
        free(ctx->acct[i].segs);
    }
    free(ctx->acct);
    for (size_t i = 0; i < ctx->refs_cap; i++) free(ctx->refs[i]);
    free(ctx->refs);
    for (uint32_t i = 0; i < ctx->n_books; i++) free(ctx->books[i]);
    free(ctx->books);
    pthread_mutex_destroy(&ctx->mu);
    free(ctx);
}

#define LOCK(ctx) pthread_mutex_lock(&(ctx)->mu)
#define UNLOCK(ctx) pthread_mutex_unlock(&(ctx)->mu)

/* FNV-1a */
static uint64_t hash_str(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    while (*s) {
        h ^= (unsigned char) *s++;
        h *= 1099511628211ull;
    }
    return h;
}

static bool ref_exists(const zxv_ctx *c, const char *ref)
{
    if (!c->refs_cap) return false;
    size_t mask = c->refs_cap - 1, i = (size_t) hash_str(ref) & mask;
    while (c->refs[i]) {
        if (strcmp(c->refs[i], ref) == 0) return true;
        i = (i + 1) & mask;
    }
    return false;
}

static int32_t ref_insert_raw(char **tbl, size_t cap, char *owned)
{
    size_t mask = cap - 1, i = (size_t) hash_str(owned) & mask;
    while (tbl[i]) i = (i + 1) & mask;
    tbl[i] = owned;
    return ZXV_OK;
}

/* Reserve room so the following insert cannot fail. */
static int32_t ref_reserve(zxv_ctx *c)
{
    if ((c->refs_n + 1) * 2 <= c->refs_cap) return ZXV_OK;
    size_t ncap = c->refs_cap ? c->refs_cap * 2 : 64;
    char **n = calloc(ncap, sizeof *n);
    if (!n) return ZXV_E_NOMEM;
    for (size_t i = 0; i < c->refs_cap; i++)
        if (c->refs[i]) ref_insert_raw(n, ncap, c->refs[i]);
    free(c->refs);
    c->refs = n;
    c->refs_cap = ncap;
    return ZXV_OK;
}

/* ===================================================================== */
/* Currency configuration                                                */
/* ===================================================================== */

static zxv_ccy_cfg *ccy_find(zxv_ctx *c, const char *code)
{
    for (uint32_t i = 0; i < c->n_ccy; i++)
        if (strcmp(c->ccy[i].code, code) == 0) return &c->ccy[i];
    return NULL;
}

static int32_t ccy_enable_locked(zxv_ctx *ctx, const char *alpha, int minor_or_neg)
{
    if (!is_upper3(alpha))
        return fail(ZXV_E_CURRENCY, "'%.8s' is not a 3-letter upper-case ISO 4217 code", alpha);
    uint16_t num = 0;
    uint8_t minor = 0;
    uint32_t flags = 0;
    int32_t rc = iso_lookup(alpha, &num, &minor, &flags);
    if (rc == ZXV_E_NOT_FOUND)
        return fail(ZXV_E_CURRENCY, "'%s' is not an ISO 4217 currency", alpha);
    if (rc == ZXV_OK) {
        if (!(flags & ZXV_CCYF_PAYABLE))
            return fail(ZXV_E_CURRENCY, "'%s' is in ISO 4217 but not usable for payments", alpha);
        if (minor_or_neg >= 0 && (uint8_t) minor_or_neg != minor)
            return fail(ZXV_E_CURRENCY, "'%s' has %u minor units in ISO 4217, not %d", alpha,
                        minor, minor_or_neg);
    } else { /* no table compiled in */
        if (minor_or_neg < 0)
            return fail(ZXV_E_NOTIMPL,
                        "ISO 4217 table not built in; use zxv_ccy_enable_with_minor");
        if (minor_or_neg > 4) return fail(ZXV_E_ARG, "minor units %d out of range 0..4", minor_or_neg);
        minor = (uint8_t) minor_or_neg;
        flags = ZXV_CCYF_PAYABLE;
    }
    zxv_ccy_cfg *e = ccy_find(ctx, alpha);
    if (e) return ok(); /* idempotent */
    if (ctx->n_ccy >= ZXV_MAX_CCY) return fail(ZXV_E_CAPACITY, "currency table full (%u)", ZXV_MAX_CCY);
    e = &ctx->ccy[ctx->n_ccy++];
    memcpy(e->code, alpha, 4);
    e->minor = minor;
    e->flags = flags;
    return ok();
}

int32_t zxv_ccy_enable(zxv_ctx *ctx, const char *alpha)
{
    if (!ctx || !alpha) return fail(ZXV_E_ARG, "NULL argument");
    LOCK(ctx);
    int32_t rc = ccy_enable_locked(ctx, alpha, -1);
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ccy_enable_with_minor(zxv_ctx *ctx, const char *alpha, uint8_t minor_units)
{
    if (!ctx || !alpha) return fail(ZXV_E_ARG, "NULL argument");
    LOCK(ctx);
    int32_t rc = ccy_enable_locked(ctx, alpha, minor_units);
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ccy_register_private(zxv_ctx *ctx, const char *code, uint8_t minor_units)
{
    if (!ctx || !code) return fail(ZXV_E_ARG, "NULL argument");
    size_t n = strlen(code);
    if (n < 3 || n > ZXV_CODE_MAX)
        return fail(ZXV_E_ARG, "private code must be 3..%u characters", ZXV_CODE_MAX);
    for (size_t i = 0; i < n; i++)
        if (!((code[i] >= 'A' && code[i] <= 'Z') || (code[i] >= '0' && code[i] <= '9')))
            return fail(ZXV_E_ARG, "private code must be upper-case letters and digits");
    if (minor_units > 8) return fail(ZXV_E_ARG, "minor units %u out of range 0..8", minor_units);
    if (iso_lookup(code, NULL, NULL, NULL) == ZXV_OK)
        return fail(ZXV_E_CURRENCY, "'%s' is an ISO 4217 code; it cannot be private", code);
    LOCK(ctx);
    int32_t rc;
    zxv_ccy_cfg *e = ccy_find(ctx, code);
    if (e) {
        rc = (e->flags & ZXV_CCYF_PRIVATE) && e->minor == minor_units
                 ? ok()
                 : fail(ZXV_E_DUPLICATE, "'%s' already configured differently", code);
    } else if (ctx->n_ccy >= ZXV_MAX_CCY) {
        rc = fail(ZXV_E_CAPACITY, "currency table full (%u)", ZXV_MAX_CCY);
    } else {
        e = &ctx->ccy[ctx->n_ccy++];
        memcpy(e->code, code, n + 1);
        e->minor = minor_units;
        e->flags = ZXV_CCYF_PRIVATE;
        rc = ok();
    }
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ccy_get(zxv_ctx *ctx, const char *code, uint8_t *out_minor_units, uint32_t *out_flags)
{
    if (!ctx || !code) return fail(ZXV_E_ARG, "NULL argument");
    LOCK(ctx);
    zxv_ccy_cfg *e = ccy_find(ctx, code);
    int32_t rc;
    if (!e) {
        rc = fail(ZXV_E_NOT_FOUND, "'%.8s' is not enabled in this context", code);
    } else {
        if (out_minor_units) *out_minor_units = e->minor;
        if (out_flags) *out_flags = e->flags;
        rc = ok();
    }
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ccy_enabled_count(zxv_ctx *ctx, uint32_t *out_count)
{
    if (!ctx || !out_count) return fail(ZXV_E_ARG, "NULL argument");
    LOCK(ctx);
    *out_count = ctx->n_ccy;
    UNLOCK(ctx);
    return ok();
}

int32_t zxv_ccy_enabled_at(zxv_ctx *ctx, uint32_t index, char *buf, size_t cap, size_t *out_len)
{
    if (!ctx) return fail(ZXV_E_ARG, "ctx is NULL");
    LOCK(ctx);
    int32_t rc = index < ctx->n_ccy ? out_text(ctx->ccy[index].code, buf, cap, out_len)
                                    : fail(ZXV_E_NOT_FOUND, "index %u out of range", index);
    UNLOCK(ctx);
    return rc;
}

/* ===================================================================== */
/* Ledger                                                                */
/* ===================================================================== */

/* Open a new triple-ledger segment for platform account `id` (a book is
 * allocated when the current one is full). Writes only an empty account. */
static int32_t seg_open(zxv_ctx *ctx, zxv_acct *a, uint32_t id)
{
    if (a->n_segs == a->cap_segs) {
        uint32_t ncap = a->cap_segs ? a->cap_segs * 2 : 2;
        zxv_seg *n = realloc(a->segs, ncap * sizeof *n);
        if (!n) return fail(ZXV_E_NOMEM, "segment table allocation failed");
        a->segs = n;
        a->cap_segs = ncap;
    }
    if (ctx->n_books == 0 || ctx->books[ctx->n_books - 1]->num_accounts >= ZXV_TL_ACCOUNTS) {
        if (ctx->n_books == ctx->cap_books) {
            uint32_t ncap = ctx->cap_books ? ctx->cap_books * 2 : 4;
            triple_ledger_t **n = realloc(ctx->books, ncap * sizeof *n);
            if (!n) return fail(ZXV_E_NOMEM, "book table allocation failed");
            ctx->books = n;
            ctx->cap_books = ncap;
        }
        triple_ledger_t *b = calloc(1, sizeof *b);
        if (!b) return fail(ZXV_E_NOMEM, "triple ledger book allocation failed (%zu bytes)", sizeof *b);
        triple_ledger_init(b);
        ctx->books[ctx->n_books++] = b;
    }
    uint32_t book = ctx->n_books - 1;
    uint32_t tl_id = triple_ledger_create_account(ctx->books[book], id + 1u, CAP_FINANCIAL, a->name);
    if (tl_id == 0xFFFFFFFFu) return fail(ZXV_E_INTERNAL, "triple_ledger_create_account refused a fresh book");
    a->segs[a->n_segs].book = book;
    a->segs[a->n_segs].tl_id = tl_id;
    a->n_segs++;
    return ZXV_OK;
}

static account_t *seg_live(zxv_ctx *ctx, const zxv_acct *a)
{
    const zxv_seg *g = &a->segs[a->n_segs - 1];
    return &ctx->books[g->book]->accounts[g->tl_id];
}

/* Make sure the live segment of `a` can take one more posting. */
static int32_t seg_room(zxv_ctx *ctx, zxv_acct *a, uint32_t id)
{
    if (seg_live(ctx, a)->num_entries + ZXV_TL_PER_POST <= ZXV_TL_ENTRY_CAP) return ZXV_OK;
    return seg_open(ctx, a, id);
}

int32_t zxv_ledger_open_account(zxv_ctx *ctx, const char *name, const char *ccy, int32_t kind,
                                uint32_t *out_account_id)
{
    if (!ctx || !out_account_id) return fail(ZXV_E_ARG, "NULL argument");
    int32_t rc = check_str(name, ZXV_NAME_MAX, "name", false);
    if (rc) return rc;
    if ((rc = check_str(ccy, ZXV_CODE_MAX, "ccy", false))) return rc;
    if (kind != ZXV_ACCT_HOLDER && kind != ZXV_ACCT_ISSUER)
        return fail(ZXV_E_ARG, "unknown account kind %d", kind);
    LOCK(ctx);
    if (!ccy_find(ctx, ccy)) {
        rc = fail(ZXV_E_CURRENCY, "'%s' is not enabled in this context", ccy);
        goto out;
    }
    if (ctx->n_acct >= ZXV_MAX_ACCOUNTS) {
        rc = fail(ZXV_E_CAPACITY, "account table full (%u)", ZXV_MAX_ACCOUNTS);
        goto out;
    }
    if (ctx->n_acct == ctx->cap_acct) {
        uint32_t ncap = ctx->cap_acct ? ctx->cap_acct * 2 : 64;
        zxv_acct *n = realloc(ctx->acct, ncap * sizeof *n);
        if (!n) {
            rc = fail(ZXV_E_NOMEM, "account table allocation failed");
            goto out;
        }
        ctx->acct = n;
        ctx->cap_acct = ncap;
    }
    zxv_acct *a = &ctx->acct[ctx->n_acct];
    memset(a, 0, sizeof *a);
    snprintf(a->name, sizeof a->name, "%s", name);
    snprintf(a->ccy, sizeof a->ccy, "%s", ccy);
    a->kind = kind;
    if ((rc = seg_open(ctx, a, ctx->n_acct)) != ZXV_OK) {
        free(a->segs);
        goto out;
    }
    *out_account_id = ctx->n_acct++;
    rc = ok();
out:
    UNLOCK(ctx);
    return rc;
}

static int32_t line_reserve(zxv_acct *a)
{
    if (a->n_lines < a->cap_lines) return ZXV_OK;
    uint32_t ncap = a->cap_lines ? a->cap_lines * 2 : 16;
    zxv_line *n = realloc(a->lines, ncap * sizeof *n);
    if (!n) return ZXV_E_NOMEM;
    a->lines = n;
    a->cap_lines = ncap;
    return ZXV_OK;
}

/* One side of a transfer on the three axes, exactly as
 * finance/triple_ledger_transfer posts it: financial and provenance carry
 * the amount, externality carries the phase (0 here: no externality). */
static int32_t post_side(zxv_ctx *ctx, const zxv_acct *a, bool receive, surplus_real_t amt,
                         uint32_t counterparty, const char *reference)
{
    const zxv_seg *g = &a->segs[a->n_segs - 1];
    triple_ledger_t *tl = ctx->books[g->book];
    surplus_real_t d = receive ? amt : SR_ZERO, c = receive ? SR_ZERO : amt;
    const surplus_real_t ell = SR_ONE, phi = SR_ZERO;
    if (triple_ledger_post(tl, g->tl_id, LEDGER_FINANCIAL, amt, ell, phi, d, c, counterparty, reference) != 0)
        return ZXV_E_INTERNAL;
    if (triple_ledger_post(tl, g->tl_id, LEDGER_PROVENANCE, amt, ell, phi, d, c, counterparty,
                           receive ? "Provenance: transfer in" : "Provenance: transfer out") != 0)
        return ZXV_E_INTERNAL;
    if (triple_ledger_post(tl, g->tl_id, LEDGER_EXTERNALITY, amt, ell, phi, receive ? phi : SR_ZERO,
                           receive ? SR_ZERO : phi, counterparty,
                           receive ? "Externality: phase received" : "Externality: phase loading") != 0)
        return ZXV_E_INTERNAL;
    return ZXV_OK;
}

int32_t zxv_ledger_post(zxv_ctx *ctx, uint32_t from_account, uint32_t to_account,
                        int64_t amount_minor, const char *ccy, const char *reference,
                        uint64_t *out_entry_id)
{
    if (!ctx) return fail(ZXV_E_ARG, "ctx is NULL");
    int32_t rc;
    if ((rc = check_str(ccy, ZXV_CODE_MAX, "ccy", false))) return rc;
    if ((rc = check_str(reference, ZXV_REF_MAX, "reference", false))) return rc;
    if (amount_minor <= 0 || amount_minor > ZXV_AMOUNT_MAX)
        return fail(ZXV_E_RANGE, "amount %lld must be in 1..%lld minor units",
                    (long long) amount_minor, (long long) ZXV_AMOUNT_MAX);
    if (from_account == to_account) return fail(ZXV_E_ARG, "from and to are the same account");

    LOCK(ctx);
    zxv_acct *f = from_account < ctx->n_acct ? &ctx->acct[from_account] : NULL;
    zxv_acct *t = to_account < ctx->n_acct ? &ctx->acct[to_account] : NULL;
    if (!f || !t) {
        rc = fail(ZXV_E_NOT_FOUND, "unknown account %u", !f ? from_account : to_account);
        goto out;
    }
    if (strcmp(f->ccy, ccy) != 0 || strcmp(t->ccy, ccy) != 0) {
        rc = fail(ZXV_E_CURRENCY, "accounts are in %s and %s; posting is in %s (no implicit FX)",
                  f->ccy, t->ccy, ccy);
        goto out;
    }
    /* Validate EVERYTHING before writing any balance. */
    if (f->kind == ZXV_ACCT_HOLDER && (f->debit - f->credit) < amount_minor) {
        rc = fail(ZXV_E_FUNDS, "account %u has %lld available, needs %lld", from_account,
                  (long long) (f->debit - f->credit), (long long) amount_minor);
        goto out;
    }
    if (f->credit > ZXV_AMOUNT_MAX - amount_minor || t->debit > ZXV_AMOUNT_MAX - amount_minor) {
        rc = fail(ZXV_E_RANGE, "rail total would exceed the exact range (2^53-1)");
        goto out;
    }
    if (ref_exists(ctx, reference)) {
        rc = fail(ZXV_E_DUPLICATE, "reference '%s' already posted", reference);
        goto out;
    }
    /* Reserve every resource. A segment opened here is an empty account and
     * leaves balances untouched if a later reservation fails. */
    char *ref_copy = strdup(reference);
    if (!ref_copy || ref_reserve(ctx) != ZXV_OK || line_reserve(f) != ZXV_OK ||
        line_reserve(t) != ZXV_OK) {
        free(ref_copy);
        rc = fail(ZXV_E_NOMEM, "allocation failed");
        goto out;
    }
    if ((rc = seg_room(ctx, f, from_account)) != ZXV_OK || (rc = seg_room(ctx, t, to_account)) != ZXV_OK) {
        free(ref_copy);
        goto out;
    }

    /* amount <= 2^53-1, so the TEST_HOST double is exact. */
    surplus_real_t amt = SR_FROM_INT(amount_minor);
    if (post_side(ctx, f, false, amt, to_account, reference) != ZXV_OK ||
        post_side(ctx, t, true, amt, from_account, reference) != ZXV_OK) {
        free(ref_copy);
        /* Unreachable after the reservations above; surfaced, never hidden. */
        rc = fail(ZXV_E_INTERNAL, "triple_ledger_post refused a reserved posting");
        goto out;
    }
    uint64_t entry_id = ctx->next_entry_id++;
    ref_insert_raw(ctx->refs, ctx->refs_cap, ref_copy);
    ctx->refs_n++;
    f->credit += amount_minor;
    t->debit += amount_minor;
    zxv_line *lf = &f->lines[f->n_lines++];
    zxv_line *lt = &t->lines[t->n_lines++];
    lf->entry_id = lt->entry_id = entry_id;
    lf->amount = -amount_minor;
    lt->amount = amount_minor;
    lf->counterparty = to_account;
    lt->counterparty = from_account;
    snprintf(lf->ref, sizeof lf->ref, "%s", reference);
    snprintf(lt->ref, sizeof lt->ref, "%s", reference);
    if (out_entry_id) *out_entry_id = entry_id;
    rc = ok();
out:
    UNLOCK(ctx);
    return rc;
}

static zxv_acct *acct_get(zxv_ctx *ctx, uint32_t id)
{
    return id < ctx->n_acct ? &ctx->acct[id] : NULL;
}

int32_t zxv_ledger_balance(zxv_ctx *ctx, uint32_t account, int64_t *out_debit_minor,
                           int64_t *out_credit_minor, int64_t *out_equity_minor)
{
    if (!ctx) return fail(ZXV_E_ARG, "ctx is NULL");
    LOCK(ctx);
    zxv_acct *a = acct_get(ctx, account);
    int32_t rc;
    if (!a) {
        rc = fail(ZXV_E_NOT_FOUND, "unknown account %u", account);
    } else {
        if (out_debit_minor) *out_debit_minor = a->debit;
        if (out_credit_minor) *out_credit_minor = a->credit;
        if (out_equity_minor) *out_equity_minor = a->debit - a->credit;
        rc = ok();
    }
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ledger_account_ccy(zxv_ctx *ctx, uint32_t account, char *buf, size_t cap, size_t *out_len)
{
    if (!ctx) return fail(ZXV_E_ARG, "ctx is NULL");
    LOCK(ctx);
    zxv_acct *a = acct_get(ctx, account);
    int32_t rc = a ? out_text(a->ccy, buf, cap, out_len)
                   : fail(ZXV_E_NOT_FOUND, "unknown account %u", account);
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ledger_account_name(zxv_ctx *ctx, uint32_t account, char *buf, size_t cap, size_t *out_len)
{
    if (!ctx) return fail(ZXV_E_ARG, "ctx is NULL");
    LOCK(ctx);
    zxv_acct *a = acct_get(ctx, account);
    int32_t rc = a ? out_text(a->name, buf, cap, out_len)
                   : fail(ZXV_E_NOT_FOUND, "unknown account %u", account);
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ledger_account_kind(zxv_ctx *ctx, uint32_t account, int32_t *out_kind)
{
    if (!ctx || !out_kind) return fail(ZXV_E_ARG, "NULL argument");
    LOCK(ctx);
    zxv_acct *a = acct_get(ctx, account);
    int32_t rc;
    if (a) {
        *out_kind = a->kind;
        rc = ok();
    } else {
        rc = fail(ZXV_E_NOT_FOUND, "unknown account %u", account);
    }
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ledger_account_count(zxv_ctx *ctx, uint32_t *out_count)
{
    if (!ctx || !out_count) return fail(ZXV_E_ARG, "NULL argument");
    LOCK(ctx);
    *out_count = ctx->n_acct;
    UNLOCK(ctx);
    return ok();
}

int32_t zxv_ledger_entry_count(zxv_ctx *ctx, uint32_t account, uint32_t *out_count)
{
    if (!ctx || !out_count) return fail(ZXV_E_ARG, "NULL argument");
    LOCK(ctx);
    zxv_acct *a = acct_get(ctx, account);
    int32_t rc;
    if (a) {
        *out_count = a->n_lines;
        rc = ok();
    } else {
        rc = fail(ZXV_E_NOT_FOUND, "unknown account %u", account);
    }
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ledger_entry_at(zxv_ctx *ctx, uint32_t account, uint32_t index, uint64_t *out_entry_id,
                            int64_t *out_signed_minor, uint32_t *out_counterparty, char *ref_buf,
                            size_t ref_cap, size_t *out_ref_len)
{
    if (!ctx) return fail(ZXV_E_ARG, "ctx is NULL");
    LOCK(ctx);
    zxv_acct *a = acct_get(ctx, account);
    int32_t rc;
    if (!a) {
        rc = fail(ZXV_E_NOT_FOUND, "unknown account %u", account);
    } else if (index >= a->n_lines) {
        rc = fail(ZXV_E_NOT_FOUND, "entry index %u out of range", index);
    } else {
        const zxv_line *l = &a->lines[index];
        rc = out_text(l->ref, ref_buf, ref_cap, out_ref_len);
        if (rc == ZXV_OK) {
            if (out_entry_id) *out_entry_id = l->entry_id;
            if (out_signed_minor) *out_signed_minor = l->amount;
            if (out_counterparty) *out_counterparty = l->counterparty;
        }
    }
    UNLOCK(ctx);
    return rc;
}

int32_t zxv_ledger_check(zxv_ctx *ctx)
{
    if (!ctx) return fail(ZXV_E_ARG, "ctx is NULL");
    LOCK(ctx);
    int32_t rc = ok();
    for (uint32_t ci = 0; ci < ctx->n_ccy && rc == ZXV_OK; ci++) {
        int64_t sd = 0, sc = 0;
        for (uint32_t i = 0; i < ctx->n_acct; i++) {
            const zxv_acct *a = &ctx->acct[i];
            if (strcmp(a->ccy, ctx->ccy[ci].code) != 0) continue;
            sd += a->debit;
            sc += a->credit;
        }
        if (sd != sc)
            rc = fail(ZXV_E_INVARIANT, "%s: sum DEBIT %lld != sum CREDIT %lld", ctx->ccy[ci].code,
                      (long long) sd, (long long) sc);
    }
    for (uint32_t i = 0; i < ctx->n_acct && rc == ZXV_OK; i++) {
        const zxv_acct *a = &ctx->acct[i];
        int64_t eq = a->debit - a->credit;
        if (a->kind == ZXV_ACCT_HOLDER && eq < 0) {
            rc = fail(ZXV_E_INVARIANT, "holder account %u is negative (%lld)", i, (long long) eq);
            break;
        }
        surplus_real_t fin = SR_ZERO;
        for (uint32_t k = 0; k < a->n_segs; k++)
            fin = SR_ADD(fin, ctx->books[a->segs[k].book]->accounts[a->segs[k].tl_id].balance[LEDGER_FINANCIAL]);
        if (fin != SR_FROM_INT(eq))
            rc = fail(ZXV_E_INVARIANT,
                      "account %u: exact shadow %lld != triple-ledger financial axis", i,
                      (long long) eq);
    }
    UNLOCK(ctx);
    return rc;
}

/* ===================================================================== */
/* ISO 20022 messages                                                    */
/* ===================================================================== */

#define F_BIT(f) (1u << (f))

struct zxv_msg {
    zxv_ctx *ctx;
    int32_t kind;
    uint32_t set; /* F_BIT(zxv_mx_field) | F_BIT(16 + zxv_mx_amount) */
    union {
        pacs008_t p8;
        camt053_t c53;
    } u;
    char ccy[ISO_CCY_MAX];
};

#define A_BIT(a) (1u << (16 + (a)))

int32_t zxv_msg_create(zxv_ctx *ctx, int32_t kind, zxv_msg **out_msg)
{
    if (!ctx || !out_msg) return fail(ZXV_E_ARG, "NULL argument");
    switch (kind) {
    case ZXV_MX_PACS008:
    case ZXV_MX_CAMT053:
        break;
    case ZXV_MX_PACS002:
    case ZXV_MX_PACS004:
    case ZXV_MX_PACS009:
    case ZXV_MX_CAMT056:
    case ZXV_MX_CAMT029:
        /* TODO(kernel-cbank): bind cb_mx_pacs002/pacs004/pacs009/camt056/
         * camt029 from kernel/src/cbank/cb_mx.h once that header lands. */
        return fail(ZXV_E_NOTIMPL, "message kind %d not built in yet (kernel/src/cbank cb_mx.h)", kind);
    default:
        return fail(ZXV_E_ARG, "unknown message kind %d", kind);
    }
    zxv_msg *m = calloc(1, sizeof *m);
    if (!m) return fail(ZXV_E_NOMEM, "message allocation failed");
    m->ctx = ctx;
    m->kind = kind;
    if (kind == ZXV_MX_PACS008) m->u.p8.nb_of_txs = 1;
    *out_msg = m;
    return ok();
}

void zxv_msg_destroy(zxv_msg *msg)
{
    free(msg);
}

/* Is a currency valid on the wire? ISO 4217, payable, not private. */
static int32_t wire_ccy_check(zxv_ctx *ctx, const char *ccy, uint8_t *out_minor)
{
    if (!is_upper3(ccy))
        return fail(ZXV_E_CURRENCY, "'%.8s' is not a 3-letter upper-case ISO 4217 alpha code", ccy);
    uint8_t minor = 0;
    uint32_t flags = 0;
    int32_t rc = iso_lookup(ccy, NULL, &minor, &flags);
    if (rc == ZXV_E_NOT_FOUND)
        return fail(ZXV_E_CURRENCY, "'%s' is not ISO 4217; private units (e.g. VFV) and rail "
                    "numerics 846/810/888 never go in a Ccy attribute", ccy);
    if (rc == ZXV_OK) {
        if (!(flags & ZXV_CCYF_PAYABLE))
            return fail(ZXV_E_CURRENCY, "'%s' is not usable for a payment amount", ccy);
        *out_minor = minor;
        return ZXV_OK;
    }
    /* No ISO table: fall back to the context's configuration. */
    LOCK(ctx);
    zxv_ccy_cfg *e = ccy_find(ctx, ccy);
    bool known = e != NULL, priv = e && (e->flags & ZXV_CCYF_PRIVATE);
    if (e) minor = e->minor;
    UNLOCK(ctx);
    if (priv) return fail(ZXV_E_CURRENCY, "'%s' is a platform-private unit, not ISO 4217", ccy);
    if (!known)
        return fail(ZXV_E_CURRENCY, "'%s' unknown: no ISO 4217 table built in and not enabled", ccy);
    *out_minor = minor;
    return ZXV_OK;
}

int32_t zxv_msg_set_text(zxv_msg *msg, int32_t field, const char *utf8)
{
    if (!msg) return fail(ZXV_E_ARG, "msg is NULL");
    char *dst = NULL;
    size_t max = 0;
    bool p8 = msg->kind == ZXV_MX_PACS008;
    switch (field) {
    case ZXV_FLD_MSG_ID:
        dst = p8 ? msg->u.p8.msg_id : msg->u.c53.msg_id;
        max = ISO_MSGID_MAX;
        break;
    case ZXV_FLD_CRE_DT_TM:
        dst = p8 ? msg->u.p8.cre_dt_tm : msg->u.c53.cre_dt_tm;
        max = ISO_DTTM_MAX;
        break;
    case ZXV_FLD_DEBTOR_NAME:
        if (p8) dst = msg->u.p8.debtor_name, max = ISO_NAME_MAX;
        break;
    case ZXV_FLD_DEBTOR_ACCT:
        if (p8) dst = msg->u.p8.debtor_acct, max = ISO_ACCT_MAX;
        break;
    case ZXV_FLD_CREDITOR_NAME:
        if (p8) dst = msg->u.p8.creditor_name, max = ISO_NAME_MAX;
        break;
    case ZXV_FLD_CREDITOR_ACCT:
        if (p8) dst = msg->u.p8.creditor_acct, max = ISO_ACCT_MAX;
        break;
    case ZXV_FLD_END_TO_END_ID:
        if (p8) dst = msg->u.p8.end_to_end_id, max = ISO_E2E_MAX;
        break;
    case ZXV_FLD_ACCT_ID:
        if (!p8) dst = msg->u.c53.acct_id, max = ISO_ACCT_MAX;
        break;
    case ZXV_FLD_CCY: {
        if (!utf8) return fail(ZXV_E_ARG, "value is NULL");
        uint8_t minor;
        int32_t rc = wire_ccy_check(msg->ctx, utf8, &minor);
        if (rc) return rc;
        memcpy(msg->ccy, utf8, 4);
        memcpy(p8 ? msg->u.p8.ccy : msg->u.c53.ccy, utf8, 4);
        msg->set |= F_BIT(field);
        return ok();
    }
    default:
        return fail(ZXV_E_FIELD, "unknown field %d", field);
    }
    if (!dst) return fail(ZXV_E_FIELD, "field %d is not part of this message kind", field);
    int32_t rc = check_str(utf8, max, "value", false);
    if (rc) return rc;
    memcpy(dst, utf8, strlen(utf8) + 1);
    msg->set |= F_BIT(field);
    return ok();
}

int32_t zxv_msg_set_amount(zxv_msg *msg, int32_t field, int64_t units, uint8_t frac_digits)
{
    if (!msg) return fail(ZXV_E_ARG, "msg is NULL");
    if (frac_digits > 8) return fail(ZXV_E_ARG, "frac_digits %u out of range 0..8", frac_digits);
    if (units > ZXV_AMOUNT_MAX || units < -ZXV_AMOUNT_MAX)
        return fail(ZXV_E_RANGE, "amount beyond exact range");
    if (msg->kind == ZXV_MX_PACS008 && field == ZXV_AMT_SETTLEMENT) {
        if (units <= 0) return fail(ZXV_E_RANGE, "settlement amount must be > 0");
        msg->u.p8.amount_units = units;
        msg->u.p8.amount_frac = frac_digits;
    } else if (msg->kind == ZXV_MX_CAMT053 && field == ZXV_AMT_OPENING) {
        msg->u.c53.opening_units = units;
        msg->u.c53.opening_frac = frac_digits;
    } else if (msg->kind == ZXV_MX_CAMT053 && field == ZXV_AMT_CLOSING) {
        msg->u.c53.closing_units = units;
        msg->u.c53.closing_frac = frac_digits;
    } else {
        return fail(ZXV_E_FIELD, "amount field %d is not part of this message kind", field);
    }
    msg->set |= A_BIT(field);
    return ok();
}

int32_t zxv_msg_set_tx_count(zxv_msg *msg, uint32_t nb_of_txs)
{
    if (!msg) return fail(ZXV_E_ARG, "msg is NULL");
    if (msg->kind != ZXV_MX_PACS008) return fail(ZXV_E_FIELD, "NbOfTxs is a pacs.008 field");
    if (nb_of_txs == 0) return fail(ZXV_E_ARG, "NbOfTxs must be >= 1");
    msg->u.p8.nb_of_txs = nb_of_txs;
    return ok();
}

int32_t zxv_msg_add_entry(zxv_msg *msg, int64_t units, uint8_t frac_digits, int32_t cdt_dbt,
                          int32_t status)
{
    if (!msg) return fail(ZXV_E_ARG, "msg is NULL");
    if (msg->kind != ZXV_MX_CAMT053) return fail(ZXV_E_FIELD, "entries belong to camt.053");
    if (units <= 0 || units > ZXV_AMOUNT_MAX) return fail(ZXV_E_RANGE, "entry amount must be > 0");
    if (frac_digits > 8) return fail(ZXV_E_ARG, "frac_digits out of range");
    if (cdt_dbt != ZXV_CRDT && cdt_dbt != ZXV_DBIT) return fail(ZXV_E_ARG, "bad CdtDbtInd");
    if (status < ZXV_STS_BOOK || status > ZXV_STS_INFO) return fail(ZXV_E_ARG, "bad entry status");
    camt053_t *c = &msg->u.c53;
    if (c->nb_entries >= ISO_CAMT_MAX_ENTRIES)
        return fail(ZXV_E_CAPACITY, "camt.053 holds at most %u entries per statement page",
                    ISO_CAMT_MAX_ENTRIES);
    camt_entry_t *e = &c->entries[c->nb_entries++];
    e->units = units;
    e->frac_digits = frac_digits;
    e->cdt_dbt = cdt_dbt == ZXV_CRDT ? ISO_CRDT : ISO_DBIT;
    e->status = status == ZXV_STS_BOOK ? ISO_STS_BOOK
              : status == ZXV_STS_PDNG ? ISO_STS_PDNG
                                       : ISO_STS_INFO;
    return ok();
}

static int32_t need(const zxv_msg *m, uint32_t bits, const char *names)
{
    if ((m->set & bits) != bits) return fail(ZXV_E_FIELD, "mandatory field(s) not set: %s", names);
    return ZXV_OK;
}

static int32_t msg_validate(zxv_msg *m)
{
    int32_t rc;
    uint8_t minor = 0;
    if (m->kind == ZXV_MX_PACS008) {
        if ((rc = need(m, F_BIT(ZXV_FLD_MSG_ID) | F_BIT(ZXV_FLD_CRE_DT_TM) | F_BIT(ZXV_FLD_DEBTOR_NAME) |
                              F_BIT(ZXV_FLD_DEBTOR_ACCT) | F_BIT(ZXV_FLD_CREDITOR_NAME) |
                              F_BIT(ZXV_FLD_CREDITOR_ACCT) | F_BIT(ZXV_FLD_END_TO_END_ID) |
                              F_BIT(ZXV_FLD_CCY) | A_BIT(ZXV_AMT_SETTLEMENT),
                       "MsgId, CreDtTm, Dbtr, DbtrAcct, Cdtr, CdtrAcct, EndToEndId, Ccy, Amount")))
            return rc;
        if ((rc = wire_ccy_check(m->ctx, m->ccy, &minor))) return rc;
        if (m->u.p8.amount_frac != minor)
            return fail(ZXV_E_CURRENCY, "%s has %u minor units; amount has %u decimal places",
                        m->ccy, minor, m->u.p8.amount_frac);
        return ZXV_OK;
    }
    if ((rc = need(m, F_BIT(ZXV_FLD_MSG_ID) | F_BIT(ZXV_FLD_CRE_DT_TM) | F_BIT(ZXV_FLD_ACCT_ID) |
                          F_BIT(ZXV_FLD_CCY) | A_BIT(ZXV_AMT_OPENING) | A_BIT(ZXV_AMT_CLOSING),
                   "MsgId, CreDtTm, Acct, Ccy, OPBD, CLBD")))
        return rc;
    if ((rc = wire_ccy_check(m->ctx, m->ccy, &minor))) return rc;
    const camt053_t *c = &m->u.c53;
    if (c->opening_frac != minor || c->closing_frac != minor)
        return fail(ZXV_E_CURRENCY, "%s has %u minor units; balances must match", m->ccy, minor);
    int64_t run = c->opening_units;
    for (uint32_t i = 0; i < c->nb_entries; i++) {
        if (c->entries[i].frac_digits != minor)
            return fail(ZXV_E_CURRENCY, "entry %u: %u decimal places, %s has %u", i,
                        c->entries[i].frac_digits, m->ccy, minor);
        if (c->entries[i].status == ISO_STS_BOOK)
            run += c->entries[i].cdt_dbt == ISO_CRDT ? c->entries[i].units : -c->entries[i].units;
    }
    if (run != c->closing_units)
        return fail(ZXV_E_INVARIANT, "OPBD %lld + booked entries = %lld, but CLBD is %lld",
                    (long long) c->opening_units, (long long) run, (long long) c->closing_units);
    return ZXV_OK;
}

int32_t zxv_msg_render(zxv_msg *msg, char *buf, size_t cap, size_t *out_len)
{
    if (!msg || !out_len) return fail(ZXV_E_ARG, "NULL argument");
    int32_t rc = msg_validate(msg);
    if (rc) return rc;
    uint32_t scap = 8192;
    for (;;) {
        char *s = malloc(scap);
        if (!s) return fail(ZXV_E_NOMEM, "render buffer allocation failed");
        int32_t n = msg->kind == ZXV_MX_PACS008 ? iso20022_pacs008_build(&msg->u.p8, s, scap)
                                                : iso20022_camt053_build(&msg->u.c53, s, scap);
        if (n >= 0) {
            rc = out_text_n(s, (size_t) n, buf, cap, out_len);
            free(s);
            return rc;
        }
        free(s);
        if (n != ISO_ERR_TRUNC || scap >= (1u << 24))
            return fail(ZXV_E_INTERNAL, "iso20022 builder returned %d", n);
        scap *= 2;
    }
}

/* ===================================================================== */
/* Netting cycles — TODO(kernel-cbank)                                   */
/* Bind cb_net_init / cb_net_add_participant / cb_net_submit /           */
/* cb_net_close_cycle from kernel/src/cbank/cb_net.h once it is stable.  */
/* ===================================================================== */

struct zxv_netting {
    int unused;
};

#define NETTING_TODO "netting cycles not built in yet (kernel/src/cbank cb_net.h)"

int32_t zxv_netting_open(zxv_ctx *ctx, const char *cycle_id, const char *ccy, zxv_netting **out_cycle)
{
    (void) ctx; (void) cycle_id; (void) ccy;
    if (out_cycle) *out_cycle = NULL;
    return fail(ZXV_E_NOTIMPL, NETTING_TODO);
}

void zxv_netting_destroy(zxv_netting *cycle)
{
    free(cycle);
}

int32_t zxv_netting_add_participant(zxv_netting *cycle, const char *participant)
{
    (void) cycle; (void) participant;
    return fail(ZXV_E_NOTIMPL, NETTING_TODO);
}

int32_t zxv_netting_submit(zxv_netting *cycle, const char *payment_id, const char *debtor,
                           const char *creditor, int64_t amount_minor)
{
    (void) cycle; (void) payment_id; (void) debtor; (void) creditor; (void) amount_minor;
    return fail(ZXV_E_NOTIMPL, NETTING_TODO);
}

int32_t zxv_netting_close(zxv_netting *cycle)
{
    (void) cycle;
    return fail(ZXV_E_NOTIMPL, NETTING_TODO);
}

int32_t zxv_netting_position_count(zxv_netting *cycle, uint32_t *out_count)
{
    (void) cycle; (void) out_count;
    return fail(ZXV_E_NOTIMPL, NETTING_TODO);
}

int32_t zxv_netting_position_at(zxv_netting *cycle, uint32_t index, char *buf, size_t cap,
                                size_t *out_len, int64_t *out_net_minor)
{
    (void) cycle; (void) index; (void) buf; (void) cap; (void) out_len; (void) out_net_minor;
    return fail(ZXV_E_NOTIMPL, NETTING_TODO);
}

/* ===================================================================== */
/* Cards                                                                 */
/* ===================================================================== */

int32_t zxv_card_check_digit(int32_t network, const char *pan, int32_t *out_valid)
{
    if (!pan || !out_valid) return fail(ZXV_E_ARG, "NULL argument");
    size_t n = strlen(pan);
    if (n < 2 || n > 19) return fail(ZXV_E_ARG, "PAN must be 2..19 digits");
    for (size_t i = 0; i < n; i++)
        if (pan[i] < '0' || pan[i] > '9') return fail(ZXV_E_ARG, "PAN must be decimal digits");
#ifdef ZXV_HAVE_CN_CHECK
    bool v;
    switch (network) {
    case ZXV_NET_PHOENIX: v = cn_damm_valid(pan, (uint32_t) n); break;
    case ZXV_NET_DRAGON: v = cn_luhn_valid(pan, (uint32_t) n); break;
    case ZXV_NET_THUNDERBIRD: v = cn_verhoeff_valid(pan, (uint32_t) n); break;
    default: return fail(ZXV_E_ARG, "unknown card network %d", network);
    }
    *out_valid = v ? 1 : 0;
    return ok();
#else
    (void) network;
    return fail(ZXV_E_NOTIMPL, "check digits not built in (kernel/src/cardnet cn_check.h)");
#endif
}

/* TODO(kernel-cardnet): bind card issue / reissue / authorize (ISO 8583
 * response codes, EMV TLV) from kernel/src/cardnet/cardnet.h once stable. */
#define CARDS_TODO "card lifecycle not built in yet (kernel/src/cardnet cardnet.h)"

int32_t zxv_card_issue(zxv_ctx *ctx, int32_t network, uint32_t account, int64_t limit_minor,
                       uint64_t *out_card_id, char *masked_pan, size_t cap, size_t *out_len)
{
    (void) ctx; (void) network; (void) account; (void) limit_minor;
    (void) out_card_id; (void) masked_pan; (void) cap; (void) out_len;
    return fail(ZXV_E_NOTIMPL, CARDS_TODO);
}

int32_t zxv_card_reissue(zxv_ctx *ctx, uint64_t card_id, uint64_t *out_new_card_id)
{
    (void) ctx; (void) card_id; (void) out_new_card_id;
    return fail(ZXV_E_NOTIMPL, CARDS_TODO);
}

int32_t zxv_card_authorize(zxv_ctx *ctx, uint64_t card_id, int64_t amount_minor, const char *ccy,
                           int32_t *out_response, char *approval_buf, size_t cap, size_t *out_len)
{
    (void) ctx; (void) card_id; (void) amount_minor; (void) ccy;
    (void) out_response; (void) approval_buf; (void) cap; (void) out_len;
    return fail(ZXV_E_NOTIMPL, CARDS_TODO);
}

/* ===================================================================== */
/* VSS conformance — TODO(kernel-cbank)                                  */
/* Bind cb_vss_c1_balance .. cb_vss_c4_backing and cb_vss_level from     */
/* kernel/src/cbank/cb_vss.h once stable.                                */
/* ===================================================================== */

int32_t zxv_vss_run(zxv_ctx *ctx, int32_t check, int32_t *out_passed, char *detail, size_t cap,
                    size_t *out_len)
{
    (void) ctx; (void) check; (void) out_passed; (void) detail; (void) cap; (void) out_len;
    return fail(ZXV_E_NOTIMPL, "VSS conformance not built in yet (kernel/src/cbank cb_vss.h)");
}

int32_t zxv_vss_level_get(zxv_ctx *ctx, int32_t *out_level)
{
    (void) ctx; (void) out_level;
    return fail(ZXV_E_NOTIMPL, "VSS conformance not built in yet (kernel/src/cbank cb_vss.h)");
}
