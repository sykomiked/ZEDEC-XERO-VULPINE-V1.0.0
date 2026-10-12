/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zsc_parse.c — Sutra-Clear's instruction fields: checks, canonical bytes,
 * the strict pacs.008 subset parser and its writer. Integer code only
 * (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zsc_internal.h"

const char *zsc_status_name(int32_t status) {
    switch (status) {
    case ZSC_E_SYNTAX: return "ZSC_E_SYNTAX";
    case ZSC_E_FIELD: return "ZSC_E_FIELD";
    case ZSC_E_PARTICIPANT: return "ZSC_E_PARTICIPANT";
    case ZSC_E_CYCLE: return "ZSC_E_CYCLE";
    case ZSC_E_TREATY: return "ZSC_E_TREATY";
    case ZSC_E_OPERATOR: return "ZSC_E_OPERATOR";
    case ZSC_E_BROKEN: return "ZSC_E_BROKEN";
    default: return zind_status_name(status);
    }
}

/* ===== field checks ===== */

static int is_up(char c) { return c >= 'A' && c <= 'Z'; }
static int is_dig(char c) { return c >= '0' && c <= '9'; }

int zsc_code_ok(const char *s) {
    size_t n;
    if (!s) return 0;
    for (n = 0; s[n]; n++)
        if (n >= 11 || !(is_up(s[n]) || is_dig(s[n]))) return 0;
    return n >= 1;
}

int zsc_bic_ok(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n != 8 && n != 11) return 0;
    for (size_t i = 0; i < n; i++) {
        if (i < 6 ? !is_up(s[i]) : !(is_up(s[i]) || is_dig(s[i]))) return 0;
    }
    return 1;
}

int zsc_text_ok(const char *s, size_t max) {
    size_t n;
    if (!s) return 0;
    for (n = 0; s[n]; n++)
        if (n >= max || (unsigned char) s[n] < 0x21 || (unsigned char) s[n] > 0x7e) return 0;
    return n >= 1;
}

/* Strict UTF-8 (no overlongs, no surrogates, <= U+10FFFF), no C0/C1
 * controls and no DEL. */
static int utf8_ok(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned c = s[i];
        uint32_t cp;
        size_t k;
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f) return 0;
            i++;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) {
            k = 1;
            cp = c & 0x1f;
        } else if (c >= 0xe0 && c <= 0xef) {
            k = 2;
            cp = c & 0x0f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            k = 3;
            cp = c & 0x07;
        } else {
            return 0;
        }
        if (n - i <= k) return 0;
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (s[i + j] & 0x3f);
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp <= 0x9f))
            return 0;
        i += k + 1;
    }
    return 1;
}

int zsc_name_ok(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n < 1 || n > 70) return 0;
    if (s[0] == ' ' || s[n - 1] == ' ') return 0;
    return utf8_ok((const unsigned char *) s, n);
}

static int num2(const char *s, int lo, int hi) {
    int v;
    if (!is_dig(s[0]) || !is_dig(s[1])) return 0;
    v = (s[0] - '0') * 10 + (s[1] - '0');
    return v >= lo && v <= hi;
}

int zsc_dt_ok(const char *s) {
    size_t i = 19, f = 0;
    if (!s || strlen(s) < 19 || strlen(s) > 32) return 0;
    for (int k = 0; k < 4; k++)
        if (!is_dig(s[k])) return 0;
    if (s[4] != '-' || !num2(s + 5, 1, 12) || s[7] != '-' || !num2(s + 8, 1, 31) || s[10] != 'T' ||
        !num2(s + 11, 0, 23) || s[13] != ':' || !num2(s + 14, 0, 59) || s[16] != ':' ||
        !num2(s + 17, 0, 59))
        return 0;
    if (s[i] == '.') {
        i++;
        while (is_dig(s[i]) && f < 7) i++, f++;
        if (f < 1 || f > 6) return 0;
    }
    if (s[i] == 0) return 1;
    if (s[i] == 'Z') return s[i + 1] == 0;
    if (s[i] == '+' || s[i] == '-')
        return num2(s + i + 1, 0, 14) && s[i + 3] == ':' && num2(s + i + 4, 0, 59) && s[i + 6] == 0;
    return 0;
}

int zsc_ccy_minor(const char *ccy) {
    uint8_t minor = 0;
    uint32_t flags = 0;
    if (!ccy || strlen(ccy) != 3 || !is_up(ccy[0]) || !is_up(ccy[1]) || !is_up(ccy[2])) return -1;
    if (zxv_iso4217_by_alpha(ccy, NULL, &minor, &flags) != ZXV_OK) return -1;
    if (!(flags & ZXV_CCYF_PAYABLE) || minor == ZXV_MINOR_NA || minor > 8) return -1;
    return minor;
}

int32_t zsc_instr_check(const zsc_instr *in) {
    if (!in) return ZXV_E_ARG;
    if (!zsc_text_ok(in->msg_id, 35) || !zsc_text_ok(in->e2e, 35)) {
        zind_set_error("zsc: MsgId and EndToEndId are 1..35 bytes of printable ASCII");
        return ZSC_E_FIELD;
    }
    if (!zsc_dt_ok(in->cre_dt_tm)) {
        zind_set_error("zsc: CreDtTm \"%.35s\" is not YYYY-MM-DDThh:mm:ss[.f{1,6}][zone]",
                       in->cre_dt_tm);
        return ZSC_E_FIELD;
    }
    if (zsc_ccy_minor(in->ccy) < 0) {
        zind_set_error("zsc: \"%.3s\" is not a payable ISO 4217 currency with minor units",
                       in->ccy);
        return ZXV_E_CURRENCY;
    }
    if (in->amount <= 0 || in->amount > ZXV_AMOUNT_MAX) {
        zind_set_error("zsc: amount %lld minor units is outside 1..2^53-1", (long long) in->amount);
        return ZXV_E_RANGE;
    }
    if (!zsc_code_ok(in->dbtr_agt) || !zsc_code_ok(in->cdtr_agt)) {
        zind_set_error("zsc: an agent code is 1..11 of [A-Z0-9]");
        return ZSC_E_FIELD;
    }
    if (strcmp(in->dbtr_agt, in->cdtr_agt) == 0) {
        zind_set_error("zsc: debtor agent and creditor agent are both %s", in->dbtr_agt);
        return ZSC_E_PARTICIPANT;
    }
    if (!zsc_name_ok(in->dbtr_nm) || !zsc_name_ok(in->cdtr_nm)) {
        zind_set_error("zsc: a name is 1..70 bytes of UTF-8 without control characters or "
                       "leading/trailing spaces");
        return ZSC_E_FIELD;
    }
    if (!zsc_text_ok(in->dbtr_acct, 34) || !zsc_text_ok(in->cdtr_acct, 34)) {
        zind_set_error("zsc: an account id is 1..34 bytes of printable ASCII");
        return ZSC_E_FIELD;
    }
    return ZXV_OK;
}

void zsc_wr_content(zind_wr *w, const zsc_instr *in) {
    zind_wr_str(w, in->e2e);
    zind_wr_str(w, in->ccy);
    zind_wr_u64(w, (uint64_t) in->amount);
    zind_wr_str(w, in->dbtr_agt);
    zind_wr_str(w, in->cdtr_agt);
    zind_wr_str(w, in->dbtr_nm);
    zind_wr_str(w, in->dbtr_acct);
    zind_wr_str(w, in->cdtr_nm);
    zind_wr_str(w, in->cdtr_acct);
}

void zsc_wr_instr(zind_wr *w, const zsc_instr *in) {
    zind_wr_u8(w, in->source);
    zind_wr_str(w, in->msg_id);
    zind_wr_str(w, in->cre_dt_tm);
    zsc_wr_content(w, in);
}

int32_t zsc_instr_digest(const zsc_instr *in, uint8_t out[32]) {
    uint8_t buf[ZSC_INSTR_WIRE_MAX];
    zind_wr w;
    if (!in || !out) return ZXV_E_ARG;
    zind_wr_init(&w, buf, sizeof buf);
    zsc_wr_content(&w, in);
    if (w.overflow) return ZXV_E_ARG;
    return zind_subject("sutra-clear/instr", buf, w.len, out);
}

/* ===== the pacs.008 subset parser ===== */

typedef struct {
    const char *s;
    size_t n, i;
    int32_t st;
    size_t off;
} px;

static int px_fail(px *p, int32_t st, const char *what) {
    if (p->st == ZXV_OK) {
        p->st = st;
        p->off = p->i;
        zind_set_error("zsc_parse_pacs008: byte %zu: %s", p->i, what);
    }
    return 0;
}

static void px_ws(px *p) {
    while (p->i < p->n &&
           (p->s[p->i] == ' ' || p->s[p->i] == '\t' || p->s[p->i] == '\r' || p->s[p->i] == '\n'))
        p->i++;
}

static int px_lit(px *p, const char *t) {
    size_t k = strlen(t);
    if (p->n - p->i < k || memcmp(p->s + p->i, t, k) != 0) return 0;
    p->i += k;
    return 1;
}

static int px_peek(px *p, const char *t) {
    size_t save = p->i;
    int ok;
    px_ws(p);
    ok = (p->n - p->i >= strlen(t)) && memcmp(p->s + p->i, t, strlen(t)) == 0;
    p->i = save;
    return ok;
}

/* "<name>" after optional whitespace. */
static int px_open(px *p, const char *name) {
    char msg[64];
    if (p->st) return 0;
    px_ws(p);
    if (px_lit(p, "<") && px_lit(p, name) && px_lit(p, ">")) return 1;
    snprintf(msg, sizeof msg, "expected <%s>", name);
    return px_fail(p, ZSC_E_SYNTAX, msg);
}

/* "</name>"; ws = 1 allows whitespace before it (a container). */
static int px_close(px *p, const char *name, int ws) {
    char msg[64];
    if (p->st) return 0;
    if (ws) px_ws(p);
    if (px_lit(p, "</") && px_lit(p, name) && px_lit(p, ">")) return 1;
    snprintf(msg, sizeof msg, "expected </%s>", name);
    return px_fail(p, ZSC_E_SYNTAX, msg);
}

/* Character data up to '<', with the five predefined entities only.
 * ascii = 1 refuses bytes >= 0x80. Decoded length 1..max. */
static int px_text(px *p, char *out, size_t max, int ascii) {
    static const struct {
        const char *ent;
        char c;
    } ents[5] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    size_t k = 0;
    if (p->st) return 0;
    while (p->i < p->n && p->s[p->i] != '<') {
        unsigned char c = (unsigned char) p->s[p->i];
        char v = (char) c;
        if (c == '&') {
            int hit = 0;
            for (int e = 0; e < 5 && !hit; e++)
                if (px_lit(p, ents[e].ent)) {
                    v = ents[e].c;
                    hit = 1;
                }
            if (!hit) return px_fail(p, ZSC_E_SYNTAX, "only &amp; &lt; &gt; &quot; &apos; allowed");
        } else {
            if (c < 0x20 || c == 0x7f) return px_fail(p, ZSC_E_SYNTAX, "control byte in text");
            if (ascii && c >= 0x80) return px_fail(p, ZSC_E_FIELD, "non-ASCII byte in this field");
            p->i++;
        }
        if (k >= max) return px_fail(p, ZSC_E_FIELD, "text longer than the field allows");
        out[k++] = v;
    }
    out[k] = 0;
    if (p->n - p->i >= 2 && p->s[p->i] == '<' && p->s[p->i + 1] != '/')
        return px_fail(p, ZSC_E_SYNTAX, "markup (an element, comment or CDATA) inside a text field");
    if (k == 0) return px_fail(p, ZSC_E_FIELD, "empty text");
    return 1;
}

/* <name>text</name> */
static int px_leaf(px *p, const char *name, char *out, size_t max, int ascii) {
    return px_open(p, name) && px_text(p, out, max, ascii) && px_close(p, name, 0);
}

/* <name Ccy="XXX">amount</name>: the currency and the amount in its minor
 * units (digits, optional '.' and 1..minor fraction digits, no sign, no
 * leading zeros, > 0, <= ZXV_AMOUNT_MAX). */
static int px_amount(px *p, const char *name, char ccy[4], int64_t *amt) {
    char t[48];
    size_t at, i = 0;
    int minor, frac = 0;
    uint64_t v = 0;
    if (p->st) return 0;
    px_ws(p);
    if (!(px_lit(p, "<") && px_lit(p, name) && px_lit(p, " Ccy=\"")))
        return px_fail(p, ZSC_E_SYNTAX, "expected an amount element with a Ccy attribute");
    if (p->n - p->i < 5 || !is_up(p->s[p->i]) || !is_up(p->s[p->i + 1]) ||
        !is_up(p->s[p->i + 2]) || p->s[p->i + 3] != '"' || p->s[p->i + 4] != '>')
        return px_fail(p, ZSC_E_SYNTAX, "Ccy is three capital letters in double quotes");
    memcpy(ccy, p->s + p->i, 3);
    ccy[3] = 0;
    minor = zsc_ccy_minor(ccy);
    if (minor < 0) return px_fail(p, ZXV_E_CURRENCY, "not a payable ISO 4217 currency");
    p->i += 5;
    at = p->i;
    if (!px_text(p, t, 40, 1) || !px_close(p, name, 0)) return 0;
    if (!is_dig(t[0]) || (t[0] == '0' && is_dig(t[1]))) {
        p->i = at;
        return px_fail(p, ZSC_E_FIELD, "amount: digits, no sign, no leading zeros");
    }
    for (; is_dig(t[i]); i++) {
        if (v > (uint64_t) ZXV_AMOUNT_MAX) break;
        v = v * 10u + (uint64_t) (t[i] - '0');
    }
    if (t[i] == '.') {
        i++;
        for (; is_dig(t[i]); i++, frac++) {
            if (v > (uint64_t) ZXV_AMOUNT_MAX) break;
            v = v * 10u + (uint64_t) (t[i] - '0');
        }
        if (frac == 0) {
            p->i = at;
            return px_fail(p, ZSC_E_FIELD, "amount: a '.' needs digits after it");
        }
    }
    if (t[i] != 0) {
        p->i = at;
        return px_fail(p, v > (uint64_t) ZXV_AMOUNT_MAX ? ZXV_E_RANGE : ZSC_E_FIELD,
                       "amount: malformed or beyond 2^53-1 minor units");
    }
    if (frac > minor) {
        p->i = at;
        return px_fail(p, ZSC_E_FIELD, "amount has more decimals than the currency's minor units");
    }
    for (; frac < minor; frac++) {
        if (v > (uint64_t) ZXV_AMOUNT_MAX) break;
        v *= 10u;
    }
    if (v == 0 || v > (uint64_t) ZXV_AMOUNT_MAX) {
        p->i = at;
        return px_fail(p, ZXV_E_RANGE, "amount must be above 0 and at most 2^53-1 minor units");
    }
    *amt = (int64_t) v;
    return 1;
}

/* DbtrAgt / CdtrAgt: FinInstnId with BICFI or ClrSysMmbId/MmbId. */
static int px_agent(px *p, const char *name, char code[12]) {
    char t[40];
    size_t at;
    if (!px_open(p, name) || !px_open(p, "FinInstnId")) return 0;
    if (px_peek(p, "<BICFI>")) {
        if (!px_open(p, "BICFI")) return 0;
        at = p->i;
        if (!px_text(p, t, 11, 1)) return 0;
        if (!zsc_bic_ok(t)) {
            p->i = at;
            return px_fail(p, ZSC_E_FIELD, "BICFI is 8 or 11 characters (AAAABBCC[DDD])");
        }
        if (!px_close(p, "BICFI", 0)) return 0;
    } else {
        if (!px_open(p, "ClrSysMmbId") || !px_open(p, "MmbId")) return 0;
        at = p->i;
        if (!px_text(p, t, 11, 1)) return 0;
        if (!zsc_code_ok(t)) {
            p->i = at;
            return px_fail(p, ZSC_E_FIELD, "MmbId is 1..11 of [A-Z0-9]");
        }
        if (!px_close(p, "MmbId", 0) || !px_close(p, "ClrSysMmbId", 1)) return 0;
    }
    memcpy(code, t, strlen(t) + 1);
    return px_close(p, "FinInstnId", 1) && px_close(p, name, 1);
}

/* <Name><Id><Othr><Id>acct</Id></Othr></Id></Name> */
static int px_acct(px *p, const char *name, char *out) {
    return px_open(p, name) && px_open(p, "Id") && px_open(p, "Othr") &&
           px_leaf(p, "Id", out, 34, 1) && px_close(p, "Othr", 1) && px_close(p, "Id", 1) &&
           px_close(p, name, 1);
}

static int px_checked(px *p, int ok, size_t at, int32_t st, const char *what) {
    if (!ok && p->st == ZXV_OK) {
        p->i = at;
        px_ws(p);
        return px_fail(p, st, what);
    }
    return p->st == ZXV_OK;
}

int32_t zsc_parse_pacs008(const char *xml, size_t len, zsc_instr *out, size_t *out_err_off) {
    px p;
    zsc_instr in;
    char t[40], ccy2[4];
    int64_t amt2 = 0;
    int have_instd = 0;
    size_t at;
    if (!xml || !out) return ZXV_E_ARG;
    memset(&p, 0, sizeof p);
    memset(&in, 0, sizeof in);
    p.s = xml;
    p.n = len;
    if (len > ZSC_PACS_MAX) {
        zind_set_error("zsc_parse_pacs008: %zu bytes is over the %u-byte limit", len, ZSC_PACS_MAX);
        if (out_err_off) *out_err_off = ZSC_PACS_MAX;
        return ZXV_E_TOO_LONG;
    }
    if (memchr(xml, 0, len)) {
        p.i = (size_t) ((const char *) memchr(xml, 0, len) - xml);
        px_fail(&p, ZSC_E_SYNTAX, "NUL byte");
        goto done;
    }
    in.source = ZSC_SRC_PACS008;
    px_lit(&p, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    px_ws(&p);
    if (!px_lit(&p, "<Document xmlns=\"urn:iso:std:iso:20022:tech:xsd:pacs.008.001.09\">")) {
        px_fail(&p, ZSC_E_SYNTAX, "expected the pacs.008.001.09 <Document>");
        goto done;
    }
    if (!px_open(&p, "FIToFICstmrCdtTrf") || !px_open(&p, "GrpHdr")) goto done;
    if (!px_leaf(&p, "MsgId", in.msg_id, 35, 1)) goto done;
    at = p.i;
    if (!px_leaf(&p, "CreDtTm", in.cre_dt_tm, 32, 1) ||
        !px_checked(&p, zsc_dt_ok(in.cre_dt_tm), at, ZSC_E_FIELD, "CreDtTm is not ISO 8601"))
        goto done;
    at = p.i;
    if (!px_leaf(&p, "NbOfTxs", t, 8, 1) ||
        !px_checked(&p, strcmp(t, "1") == 0, at, ZSC_E_FIELD, "NbOfTxs must be 1"))
        goto done;
    if (!px_open(&p, "SttlmInf")) goto done;
    at = p.i;
    if (!px_leaf(&p, "SttlmMtd", t, 4, 1) ||
        !px_checked(&p, strcmp(t, "CLRG") == 0, at, ZSC_E_FIELD, "SttlmMtd must be CLRG"))
        goto done;
    if (!px_close(&p, "SttlmInf", 1) || !px_close(&p, "GrpHdr", 1)) goto done;
    if (!px_open(&p, "CdtTrfTxInf") || !px_open(&p, "PmtId")) goto done;
    if (!px_leaf(&p, "EndToEndId", in.e2e, 35, 1) || !px_close(&p, "PmtId", 1)) goto done;
    if (!px_amount(&p, "IntrBkSttlmAmt", in.ccy, &in.amount)) goto done;
    if (px_peek(&p, "<InstdAmt ")) {
        at = p.i;
        if (!px_amount(&p, "InstdAmt", ccy2, &amt2)) goto done;
        if (!px_checked(&p, strcmp(ccy2, in.ccy) == 0 && amt2 == in.amount, at, ZSC_E_FIELD,
                        "InstdAmt differs from IntrBkSttlmAmt (no currency conversion)"))
            goto done;
        have_instd = 1;
    }
    at = p.i;
    if (!px_leaf(&p, "ChrgBr", t, 4, 1) ||
        !px_checked(&p, strcmp(t, "SLEV") == 0, at, ZSC_E_FIELD, "ChrgBr must be SLEV"))
        goto done;
    if (!px_open(&p, "Dbtr")) goto done;
    at = p.i;
    if (!px_leaf(&p, "Nm", in.dbtr_nm, 70, 0) ||
        !px_checked(&p, zsc_name_ok(in.dbtr_nm), at, ZSC_E_FIELD, "Dbtr/Nm"))
        goto done;
    if (!px_close(&p, "Dbtr", 1) || !px_acct(&p, "DbtrAcct", in.dbtr_acct)) goto done;
    if (!px_agent(&p, "DbtrAgt", in.dbtr_agt) || !px_agent(&p, "CdtrAgt", in.cdtr_agt)) goto done;
    if (!px_open(&p, "Cdtr")) goto done;
    at = p.i;
    if (!px_leaf(&p, "Nm", in.cdtr_nm, 70, 0) ||
        !px_checked(&p, zsc_name_ok(in.cdtr_nm), at, ZSC_E_FIELD, "Cdtr/Nm"))
        goto done;
    if (!px_close(&p, "Cdtr", 1) || !px_acct(&p, "CdtrAcct", in.cdtr_acct)) goto done;
    if (!have_instd && px_peek(&p, "<InstdAmt ")) {
        at = p.i;
        if (!px_amount(&p, "InstdAmt", ccy2, &amt2)) goto done;
        if (!px_checked(&p, strcmp(ccy2, in.ccy) == 0 && amt2 == in.amount, at, ZSC_E_FIELD,
                        "InstdAmt differs from IntrBkSttlmAmt (no currency conversion)"))
            goto done;
    }
    if (!px_close(&p, "CdtTrfTxInf", 1) || !px_close(&p, "FIToFICstmrCdtTrf", 1) ||
        !px_close(&p, "Document", 1))
        goto done;
    px_ws(&p);
    if (p.i != p.n) {
        px_fail(&p, ZSC_E_SYNTAX, "bytes after </Document>");
        goto done;
    }
    {
        int32_t st = zsc_instr_check(&in);
        if (st != ZXV_OK) {
            p.st = st;
            p.off = 0;
        }
    }
done:
    if (p.st != ZXV_OK) {
        if (out_err_off) *out_err_off = p.off;
        return p.st;
    }
    *out = in;
    if (out_err_off) *out_err_off = 0;
    return ZXV_OK;
}

/* ===== the writer ===== */

static int32_t agent_xml(const char *tag, const char *code, char *out, size_t cap) {
    int n;
    if (zsc_bic_ok(code))
        n = snprintf(out, cap, "      <%s><FinInstnId><BICFI>%s</BICFI></FinInstnId></%s>\n", tag,
                     code, tag);
    else
        n = snprintf(out, cap,
                     "      <%s><FinInstnId><ClrSysMmbId><MmbId>%s</MmbId></ClrSysMmbId>"
                     "</FinInstnId></%s>\n",
                     tag, code, tag);
    return (n > 0 && (size_t) n < cap) ? ZXV_OK : ZXV_E_INTERNAL;
}

int32_t zsc_render_pacs008(const zxv_cap *cap, const zsc_instr *in, char *buf, size_t cap_len,
                           size_t *out_len) {
    zxv_msg *m = NULL;
    char *x = NULL, da[160], ca[160];
    const char *cut;
    size_t xl = 0, total;
    int32_t st;
    int minor;
    if (!cap || !in || !out_len) return ZXV_E_ARG;
    st = zsc_instr_check(in);
    if (st != ZXV_OK) return st;
    minor = zsc_ccy_minor(in->ccy);
    st = zxv_msg_create_cap(cap, ZXV_MX_PACS008, &m);
    if (st != ZXV_OK) return st;
    if ((st = zxv_msg_set_text(m, ZXV_FLD_MSG_ID, in->msg_id)) ||
        (st = zxv_msg_set_text(m, ZXV_FLD_CRE_DT_TM, in->cre_dt_tm)) ||
        (st = zxv_msg_set_text(m, ZXV_FLD_DEBTOR_NAME, in->dbtr_nm)) ||
        (st = zxv_msg_set_text(m, ZXV_FLD_DEBTOR_ACCT, in->dbtr_acct)) ||
        (st = zxv_msg_set_text(m, ZXV_FLD_CREDITOR_NAME, in->cdtr_nm)) ||
        (st = zxv_msg_set_text(m, ZXV_FLD_CREDITOR_ACCT, in->cdtr_acct)) ||
        (st = zxv_msg_set_text(m, ZXV_FLD_END_TO_END_ID, in->e2e)) ||
        (st = zxv_msg_set_text(m, ZXV_FLD_CCY, in->ccy)) ||
        (st = zxv_msg_set_amount(m, ZXV_AMT_SETTLEMENT, in->amount, (uint8_t) minor)))
        goto out;
    st = zxv_msg_render(m, NULL, 0, &xl);
    if (st != ZXV_E_BUFFER) {
        if (st == ZXV_OK) st = ZXV_E_INTERNAL;
        goto out;
    }
    x = (char *) malloc(xl + 1);
    if (!x) {
        st = ZXV_E_NOMEM;
        goto out;
    }
    st = zxv_msg_render(m, x, xl + 1, &xl);
    if (st != ZXV_OK) goto out;
    /* the agents go after DbtrAcct, before Cdtr (the subset's order) */
    cut = strstr(x, "      <Cdtr><Nm>");
    if (!cut || agent_xml("DbtrAgt", in->dbtr_agt, da, sizeof da) != ZXV_OK ||
        agent_xml("CdtrAgt", in->cdtr_agt, ca, sizeof ca) != ZXV_OK) {
        zind_set_error("zsc_render_pacs008: libzxv's pacs.008 has an unexpected shape");
        st = ZXV_E_INTERNAL;
        goto out;
    }
    total = xl + strlen(da) + strlen(ca);
    *out_len = total;
    if (!buf || cap_len < total + 1) {
        st = ZXV_E_BUFFER;
        goto out;
    }
    {
        size_t head = (size_t) (cut - x), o = 0;
        memcpy(buf, x, head);
        o = head;
        memcpy(buf + o, da, strlen(da));
        o += strlen(da);
        memcpy(buf + o, ca, strlen(ca));
        o += strlen(ca);
        memcpy(buf + o, cut, xl - head);
        o += xl - head;
        buf[o] = 0;
    }
    st = ZXV_OK;
out:
    free(x);
    zxv_msg_destroy(m);
    return st;
}

/* ===== growable writer ===== */

static void db_put(zsc_dbuf *d, const void *p, size_t n) {
    if (d->bad) return;
    if (n > SIZE_MAX / 2 - d->len) {
        d->bad = 1;
        return;
    }
    if (d->len + n > d->cap) {
        size_t c = d->cap ? d->cap : 256;
        uint8_t *nb;
        while (c < d->len + n) c *= 2;
        nb = (uint8_t *) realloc(d->buf, c);
        if (!nb) {
            d->bad = 1;
            return;
        }
        d->buf = nb;
        d->cap = c;
    }
    if (n) memcpy(d->buf + d->len, p, n);
    d->len += n;
}

void zsc_db_u8(zsc_dbuf *d, uint8_t v) { db_put(d, &v, 1); }
void zsc_db_u16(zsc_dbuf *d, uint16_t v) {
    uint8_t b[2] = {(uint8_t) v, (uint8_t) (v >> 8)};
    db_put(d, b, 2);
}
void zsc_db_u32(zsc_dbuf *d, uint32_t v) {
    uint8_t b[4];
    for (int i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
    db_put(d, b, 4);
}
void zsc_db_u64(zsc_dbuf *d, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
    db_put(d, b, 8);
}
void zsc_db_bytes(zsc_dbuf *d, const void *p, size_t n) { db_put(d, p, n); }
void zsc_db_str(zsc_dbuf *d, const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n > 255) {
        d->bad = 1;
        return;
    }
    zsc_db_u8(d, (uint8_t) n);
    db_put(d, s, n);
}
void zsc_db_free(zsc_dbuf *d) {
    free(d->buf);
    memset(d, 0, sizeof *d);
}
