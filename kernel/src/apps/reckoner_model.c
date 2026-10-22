/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* reckoner_model.c — Reckoner, statements and tax reports (see
 * reckoner_model.h). Integers only: money is minor units, and nothing that
 * does not fit is ever rounded. */
#include <string.h>

#include "coffer_model.h"
#include "reckoner_model.h"

/* ---- text ---------------------------------------------------------------- */

/* Append s to out at *at, bounded by cap (room for a NUL is kept); false
 * when it did not all fit. */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    while (*s && *at + 1u < cap) out[(*at)++] = *s++;
    out[*at] = 0;
    return !*s;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* A rate in hundredths of a percent as "12.34%". */
static bool put_rate(char *out, uint32_t cap, uint32_t *at, int64_t rate)
{
    char t[4] = {'.', (char) ('0' + (int) (rate % 100 / 10)), (char) ('0' + (int) (rate % 10)), 0};
    return put_num(out, cap, at, rate / 100) && put(out, cap, at, t) && put(out, cap, at, "%");
}

static void say(reckoner_t *r, const char *s)
{
    uint32_t at = 0;
    r->notice[0] = 0;
    put(r->notice, sizeof r->notice, &at, s);
}

/* A whole decimal string with an optional '-', no leading zero, that fits
 * int64 (RK3). */
static bool dec_i64(const char *s, int64_t *v)
{
    bool neg = *s == '-';
    uint64_t x = 0, lim = neg ? (uint64_t) INT64_MAX + 1u : (uint64_t) INT64_MAX;
    const char *p = s + neg;
    if (!*p || (p[0] == '0' && p[1])) return false;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        uint32_t d = (uint32_t) (*p - '0');
        if (x > (lim - d) / 10u) return false;
        x = x * 10u + d;
    }
    if (neg && !x) return false;
    *v = neg ? (x == (uint64_t) INT64_MAX + 1u ? INT64_MIN : -(int64_t) x) : (int64_t) x;
    return true;
}

static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    return dec_i64(s, v) ? ZXM_OK : ZXM_E_PROTOCOL;
}

/* 1..max bytes of [A-Za-z0-9-]: needs no JSON escape. */
static bool id_ok(const char *s, uint32_t max)
{
    uint32_t n = 0;
    if (!s || !s[0]) return false;
    for (; s[n]; n++) {
        char c = s[n];
        if (n >= max) return false;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-'))
            return false;
    }
    return true;
}

/* YYYY-MM-DDThh:mm:ss, each field in its range. */
static bool when_ok(const char *s)
{
    static const char form[] = "dddd-dd-ddTdd:dd:dd";
    if (!s || strlen(s) != sizeof form - 1u) return false;
    for (uint32_t i = 0; form[i]; i++)
        if (form[i] == 'd' ? (s[i] < '0' || s[i] > '9') : s[i] != form[i]) return false;
#define TWO(k) ((s[k] - '0') * 10 + (s[(k) + 1] - '0'))
    return TWO(5) >= 1 && TWO(5) <= 12 && TWO(8) >= 1 && TWO(8) <= 31 && TWO(11) <= 23 &&
           TWO(14) <= 59 && TWO(17) <= 59;
#undef TWO
}

/* A file name in the Hoard: one segment of [A-Za-z0-9._-], not starting
 * with '.'. */
static bool file_ok(const char *s)
{
    uint32_t n = 0;
    if (!s || !s[0] || s[0] == '.') return false;
    for (; s[n]; n++) {
        char c = s[n];
        if (n >= RECKONER_FILE_MAX) return false;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}

/* An IPP printer address, 1..RECKONER_URI_MAX bytes, no control
 * character, space or quote. */
static bool uri_ok(const char *s)
{
    uint32_t n = 0;
    if (!s || (strncmp(s, "ipp://", 6) && strncmp(s, "ipps://", 7))) return false;
    for (; s[n]; n++)
        if (n >= RECKONER_URI_MAX || (unsigned char) s[n] <= 0x20u || s[n] == '"' || s[n] == '\\' ||
            s[n] == 0x7f)
            return false;
    return true;
}

/* ---- the period (RK2) and the gate (RK4) --------------------------------- */

void reckoner_init(reckoner_t *r, zxm_link_t *link)
{
    memset(r, 0, sizeof *r);
    r->link = link;
    r->job = -1;
}

int reckoner_set_period(reckoner_t *r, uint64_t from, uint64_t to)
{
    if (r->closed) {
        say(r, "The period is closed; reopen it to move it.");
        return ZXM_E_STATE;
    }
    if (from > to) return ZXM_E_ARG;
    r->from = from;
    r->to = to;
    r->period_set = true;
    r->st.ready = false; /* a statement of another period */
    r->xml_len = 0;
    return ZXM_OK;
}

int reckoner_close(reckoner_t *r)
{
    if (!r->period_set || r->closed) return ZXM_E_STATE;
    r->closed = true;
    r->st.ready = false; /* read again from the closed period */
    r->xml_len = 0;
    say(r, "Period closed: its statements may be reported once the books hold.");
    return ZXM_OK;
}

int reckoner_reopen(reckoner_t *r)
{
    if (!r->closed) return ZXM_E_STATE;
    r->closed = false;
    r->xml_len = 0; /* RK2: what was rendered from it is dropped */
    say(r, "Period reopened: nothing from it may be reported until it is closed again.");
    return ZXM_OK;
}

int reckoner_check(reckoner_t *r)
{
    bool ok = false;
    r->books_ok = false;
    zxm_begin(r->link, "ledger.check");
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(r->link, r->link->result, "ok", &ok) != ZXM_OK) return ZXM_E_PROTOCOL;
    r->books_ok = ok;
    say(r, ok ? "The books hold: debits equal credits and no holder account is below zero."
              : "The books do not hold: nothing may be reported.");
    return ok ? ZXM_OK : ZXM_E_STATE;
}

bool reckoner_allowed(const reckoner_t *r)
{
    return reckoner_report_ok(r->closed ? 1 : 0, r->books_ok ? 1 : 0);
}

/* RK4: the books checked again now, then the rule. */
static int gate(reckoner_t *r)
{
    if (!r->closed) {
        say(r, "Close the period first: an open period may still change.");
        return ZXM_E_STATE;
    }
    int rc = reckoner_check(r);
    if (rc != ZXM_OK) return rc;
    return reckoner_allowed(r) ? ZXM_OK : ZXM_E_STATE;
}

/* ---- the statement (RK3) --------------------------------------------------- */

/* ledger.accounts: the name and currency of account id. */
static int find_account(reckoner_t *r, uint32_t id, reckoner_statement_t *st)
{
    zxm_link_t *l = r->link;
    zxm_begin(l, "ledger.accounts");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "accounts");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        uint64_t a = 0;
        if (o < 0 || zxm_get_u64(l, o, "account", &a) != ZXM_OK) return ZXM_E_PROTOCOL;
        if (a != id) continue;
        if (zxm_get_str(l, o, "name", st->name, sizeof st->name) != ZXM_OK ||
            zxm_get_str(l, o, "currency", st->ccy, sizeof st->ccy) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; k < 3u; k++)
            if (st->ccy[k] < 'A' || st->ccy[k] > 'Z') return ZXM_E_PROTOCOL;
        if (st->ccy[3]) return ZXM_E_PROTOCOL;
        return ZXM_OK;
    }
    return ZXM_E_ARG;
}

int reckoner_statement(reckoner_t *r, uint32_t account)
{
    static reckoner_statement_t next;
    static int64_t before[RECKONER_JOURNAL], inside[RECKONER_JOURNAL];
    zxm_link_t *l = r->link;
    if (!r->period_set) return ZXM_E_STATE;
    memset(&next, 0, sizeof next);
    next.account = account;
    int rc = find_account(r, account, &next);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "ledger.entries");
    zxm_p_u64(l, "account", account);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), nb = 0, ni = 0;
    if (n > RECKONER_JOURNAL) return ZXM_E_TOOBIG;
    int64_t last = -1;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        int64_t id = 0, minor = 0;
        if (o < 0 || get_dec(l, o, "entry_id", &id) != ZXM_OK || id <= last ||
            get_dec(l, o, "signed_minor", &minor) != ZXM_OK || !reckoner_fits18(minor) ||
            minor == 0)
            return ZXM_E_PROTOCOL; /* oldest first, each a movement that fits */
        last = id;
        if ((uint64_t) id < r->from) {
            before[nb++] = minor;
        } else if ((uint64_t) id <= r->to) {
            if (ni >= RECKONER_ENTRIES) return ZXM_E_TOOBIG;
            next.e[ni].id = (uint64_t) id;
            next.e[ni].minor = minor;
            inside[ni++] = minor;
            if (minor > 0)
                next.credits = coffer_balance(next.credits, minor);
            else
                next.debits = coffer_balance(next.debits, -minor);
            if (next.credits == COFFER_REFUSED || next.debits == COFFER_REFUSED)
                return ZXM_E_PROTOCOL;
        }
    }
    next.n = ni;
    next.opening = coffer_holdings(before, nb); /* coffer.holdings: the exact sum */
    next.net = coffer_holdings(inside, ni);
    if (next.opening == COFFER_REFUSED || next.net == COFFER_REFUSED ||
        !reckoner_fits18(next.opening))
        return ZXM_E_PROTOCOL;
    next.closing = reckoner_statement_close(next.opening, next.credits, next.debits);
    if (next.closing == RECKONER_REFUSED ||
        next.closing != coffer_balance(next.opening, next.net)) /* coffer.balance */
        return ZXM_E_PROTOCOL;
    next.ready = true;
    r->st = next;
    r->xml_len = 0;
    say(r, "Statement read from the books.");
    return ZXM_OK;
}

/* ---- the camt.053 (RK5) ------------------------------------------------------ */

int reckoner_render(reckoner_t *r, const char *msg_id, const char *when)
{
    static char params[ZXM_REQ_MAX];
    const reckoner_statement_t *st = &r->st;
    uint32_t at = 0, cap = sizeof params;
    if (!id_ok(msg_id, RECKONER_ID_MAX) || !when_ok(when)) return ZXM_E_ARG;
    if (!st->ready) return ZXM_E_STATE;
    int rc = gate(r);
    if (rc != ZXM_OK) return rc;
    bool ok = put(params, cap, &at, "{\"kind\":\"camt053\",\"fields\":{\"msg_id\":\"") &&
              put(params, cap, &at, msg_id) && put(params, cap, &at, "\",\"cre_dt_tm\":\"") &&
              put(params, cap, &at, when) && put(params, cap, &at, "\",\"acct_id\":\"ZXV-") &&
              put_num(params, cap, &at, st->account) && put(params, cap, &at, "\",\"ccy\":\"") &&
              put(params, cap, &at, st->ccy) &&
              put(params, cap, &at, "\"},\"amounts\":{\"opening\":\"") &&
              put_num(params, cap, &at, st->opening) &&
              put(params, cap, &at, "\",\"closing\":\"") &&
              put_num(params, cap, &at, st->closing) && put(params, cap, &at, "\"},\"entries\":[");
    for (uint32_t i = 0; ok && i < st->n; i++) {
        int64_t m = st->e[i].minor;
        ok = put(params, cap, &at, i ? ",{\"amount_minor\":\"" : "{\"amount_minor\":\"") &&
             put_num(params, cap, &at, m > 0 ? m : -m) &&
             put(params, cap, &at,
                 m > 0 ? "\",\"direction\":\"credit\"" : "\",\"direction\":\"debit\"") &&
             put(params, cap, &at, ",\"status\":\"booked\"}");
    }
    ok = ok && put(params, cap, &at, "],\"tx_count\":") && put_num(params, cap, &at, st->n) &&
         put(params, cap, &at, "}");
    if (!ok) return ZXM_E_TOOBIG;
    zxm_begin(r->link, "iso20022.render");
    zxm_p_raw(r->link, params, at);
    rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    char kind[16];
    if (zxm_get_str(r->link, r->link->result, "kind", kind, sizeof kind) != ZXM_OK ||
        strcmp(kind, "camt053"))
        return ZXM_E_PROTOCOL;
    rc = zxm_get_str(r->link, r->link->result, "xml", r->xml, sizeof r->xml);
    if (rc != ZXM_OK) {
        r->xml_len = 0;
        return rc == ZXM_E_TOOBIG ? ZXM_E_TOOBIG : ZXM_E_PROTOCOL;
    }
    r->xml_len = (uint32_t) strlen(r->xml);
    if (!r->xml_len) return ZXM_E_PROTOCOL;
    say(r, "Statement rendered as camt.053 (nothing was sent).");
    return ZXM_OK;
}

/* ---- the jurisdiction ------------------------------------------------------- */

int reckoner_jurisdiction(reckoner_t *r)
{
    char country[8], standard[16], report[16];
    zxm_link_t *l = r->link;
    zxm_begin(l, "jurisdiction.get");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "country", country, sizeof country) != ZXM_OK ||
        zxm_get_str(l, l->result, "standard", standard, sizeof standard) != ZXM_OK ||
        zxm_get_str(l, l->result, "report_standard", report, sizeof report) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    size_t cn = strlen(country);
    if (cn != 0 && cn != 2) return ZXM_E_PROTOCOL;
    for (size_t i = 0; i < cn; i++)
        if (country[i] < 'A' || country[i] > 'Z') return ZXM_E_PROTOCOL;
    memcpy(r->country, country, cn + 1u);
    memcpy(r->standard, standard, sizeof r->standard);
    memcpy(r->report_standard, report, sizeof r->report_standard);
    r->juris_loaded = true;
    return ZXM_OK;
}

/* ---- the tax estimate (RK6) -------------------------------------------------- */

int reckoner_tax(reckoner_t *r, int64_t income, const int64_t *lo, const int64_t *hi,
                 const int64_t *rate, uint32_t n)
{
    static reckoner_tax_t next;
    static char text[RECKONER_TEXT_MAX + 1];
    uint32_t at = 0, cap = sizeof text;
    memset(&next, 0, sizeof next);
    next.total = reckoner_tax_estimate(income, lo, hi, rate, n, next.tax);
    if (next.total == RECKONER_REFUSED) {
        say(r, "The bands do not fit: each 0 to 14 digits, in order, rates 0 to 100%.");
        return ZXM_E_ARG;
    }
    next.income = income;
    next.n = n;
    memcpy(next.lo, lo, n * sizeof lo[0]);
    memcpy(next.hi, hi, n * sizeof hi[0]);
    memcpy(next.rate, rate, n * sizeof rate[0]);
    next.ready = true;
    bool ok = put(text, cap, &at,
                  "Tax estimate: advice only, not tax advice. A person files; Reckoner never "
                  "does.\n");
    if (ok && r->juris_loaded && r->country[0])
        ok = put(text, cap, &at, "Country: ") && put(text, cap, &at, r->country) &&
             put(text, cap, &at, "\n");
    if (ok && r->juris_loaded && r->report_standard[0])
        ok = put(text, cap, &at, "Reports follow: ") && put(text, cap, &at, r->report_standard) &&
             put(text, cap, &at, "\n");
    ok = ok && put(text, cap, &at, "Income: ") && put_num(text, cap, &at, income) &&
         put(text, cap, &at, " minor units\n");
    for (uint32_t i = 0; ok && i < n; i++)
        ok = put(text, cap, &at, "Band ") && put_num(text, cap, &at, lo[i]) &&
             put(text, cap, &at, " to ") && put_num(text, cap, &at, hi[i]) &&
             put(text, cap, &at, " at ") && put_rate(text, cap, &at, rate[i]) &&
             put(text, cap, &at, ": ") && put_num(text, cap, &at, next.tax[i]) &&
             put(text, cap, &at, "\n");
    ok = ok && put(text, cap, &at, "Estimated tax: ") && put_num(text, cap, &at, next.total) &&
         put(text, cap, &at, " minor units\n");
    if (!ok) return ZXM_E_TOOBIG;
    r->tax = next;
    memcpy(r->text, text, at + 1u);
    r->text_len = at;
    say(r, "Tax estimated: advice only, not tax advice; a person files.");
    return ZXM_OK;
}

/* ---- saved through the Banker and printed (RK7) ------------------------------ */

/* One request's text: at most this many bytes, cut on a character
 * boundary. Each byte escapes to at most six ("\u00XX"), inside the
 * request line. */
#define PIECE 2400u

static int write_new(zxm_link_t *l, const char *name, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) n = zxm_utf8_cut(text + at, PIECE);
        if (!n) return ZXM_E_TOOBIG;
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", name);
        zxm_p_strn(l, "text", text + at, n);
        zxm_p_bool(l, "exclusive", true); /* RK7: never over an earlier report */
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static int save(reckoner_t *r, const char *name, const char *text, uint32_t len)
{
    if (!file_ok(name)) return ZXM_E_ARG;
    if (!len) return ZXM_E_STATE;
    int rc = gate(r);
    if (rc != ZXM_OK) return rc;
    rc = write_new(r->link, name, text, len);
    if (rc != ZXM_OK) {
        say(r, "Not saved: the Banker refused it (a report never replaces an earlier one).");
        return rc;
    }
    memset(r->saved, 0, sizeof r->saved);
    memcpy(r->saved, name, strlen(name));
    r->job = -1;
    say(r, "Saved through the Banker.");
    return ZXM_OK;
}

int reckoner_save_statement(reckoner_t *r, const char *name)
{
    return save(r, name, r->xml, r->xml_len);
}

int reckoner_save_estimate(reckoner_t *r, const char *name)
{
    return save(r, name, r->text, r->tax.ready ? r->text_len : 0);
}

int reckoner_print(reckoner_t *r, const char *uri)
{
    if (!uri_ok(uri)) return ZXM_E_ARG;
    if (!r->saved[0] || !reckoner_allowed(r)) return ZXM_E_STATE;
    zxm_begin(r->link, "print.submit");
    zxm_p_str(r->link, "uri", uri);
    zxm_p_str(r->link, "path", r->saved);
    zxm_p_str(r->link, "job_name", "Reckoner");
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    uint64_t job = 0;
    if (zxm_get_u64(r->link, r->link->result, "job_id", &job) != ZXM_OK || job > INT64_MAX)
        return ZXM_E_PROTOCOL;
    r->job = (int64_t) job;
    say(r, "Sent to the printer.");
    return ZXM_OK;
}
