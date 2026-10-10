/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_iso_parse.c — a strict, bounded XML reader for inbound ISO 20022.
 *
 * Accepted: an optional leading <?xml ...?> declaration, elements with
 * unprefixed names [A-Za-z][A-Za-z0-9]{0,34}, at most one attribute set per
 * element drawn from a whitelist (xmlns on the root, Ccy elsewhere), text
 * of printable ASCII plus TAB/CR/LF, and the five predefined entities.
 * Refused: DOCTYPE, comments, CDATA, other processing instructions, numeric
 * character references, namespace prefixes, non-ASCII bytes, mixed content,
 * more than PAY_X_DEPTH levels, more than PAY_X_ELEMS elements, text over
 * PAY_X_TEXT bytes, documents over PAY_ISO_IN_MAX bytes, anything after the
 * root element other than whitespace. Every read is bounds-checked. */
#include "pay_iso.h"

#define PAY_X_DEPTH 24u
#define PAY_X_NAME  35u
#define PAY_X_TEXT  512u
#define PAY_X_ELEMS 8192u

typedef struct {
    const char *s;
    uint32_t n, i;
    char stack[PAY_X_DEPTH][PAY_X_NAME + 1];
    uint32_t depth;
    uint32_t elems;
    bool had_child[PAY_X_DEPTH];
    char text[PAY_X_TEXT + 1];
    uint32_t tlen;
    char ccy[4];
    bool has_ccy;
    const char *root, *ns;
    void *ctx;
    int32_t (*on_start)(void *ctx, const char *path);
    int32_t (*on_end)(void *ctx, const char *path, const char *text, uint32_t tlen,
                      const char *ccy);
    char path[PAY_X_DEPTH * (PAY_X_NAME + 1) + 1];
} pay_xr;

static bool at(const pay_xr *r, const char *lit)
{
    uint32_t k = 0;
    while (lit[k]) {
        if (r->i + k >= r->n || r->s[r->i + k] != lit[k]) return false;
        k++;
    }
    return true;
}

static bool ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void skip_ws(pay_xr *r)
{
    while (r->i < r->n && ws(r->s[r->i])) r->i++;
}

static bool name_start(char c)
{
    return pay_is_upper(c) || pay_is_lower(c);
}

static bool name_char(char c)
{
    return name_start(c) || pay_is_digit(c);
}

static int32_t read_name(pay_xr *r, char *out)
{
    uint32_t k = 0;
    if (r->i >= r->n || !name_start(r->s[r->i])) return PAY_ISO_ERR_XML;
    while (r->i < r->n && name_char(r->s[r->i])) {
        if (k >= PAY_X_NAME) return PAY_ISO_ERR_XML;
        out[k++] = r->s[r->i++];
    }
    out[k] = '\0';
    if (r->i < r->n && r->s[r->i] == ':') return PAY_ISO_ERR_XML; /* no prefixes */
    return 0;
}

/* Decode one character of text or attribute value into *c. */
static int32_t read_char(pay_xr *r, char *c)
{
    char ch = r->s[r->i];
    if ((unsigned char) ch >= 0x80) return PAY_ISO_ERR_XML;
    if (ch < 0x20 && ch != '\t' && ch != '\n' && ch != '\r') return PAY_ISO_ERR_XML;
    if (ch == '<') return PAY_ISO_ERR_XML;
    if (ch != '&') {
        *c = ch;
        r->i++;
        return 0;
    }
    static const struct {
        const char *e;
        char c;
    } ent[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    for (uint32_t k = 0; k < 5; k++)
        if (at(r, ent[k].e)) {
            *c = ent[k].c;
            r->i += (uint32_t) pay_strnlen(ent[k].e, 8);
            return 0;
        }
    return PAY_ISO_ERR_XML;
}

static void build_path(pay_xr *r)
{
    uint32_t o = 0;
    for (uint32_t d = 1; d < r->depth; d++) { /* skip the root */
        if (d > 1) r->path[o++] = '/';
        for (uint32_t k = 0; r->stack[d][k]; k++) r->path[o++] = r->stack[d][k];
    }
    r->path[o] = '\0';
}

static int32_t read_attrs(pay_xr *r, bool is_root, bool *seen_ns)
{
    for (;;) {
        char an[PAY_X_NAME + 1], val[64];
        uint32_t vl = 0;
        bool had_ws = false;
        while (r->i < r->n && ws(r->s[r->i])) {
            r->i++;
            had_ws = true;
        }
        if (r->i >= r->n) return PAY_ISO_ERR_XML;
        if (r->s[r->i] == '>' || r->s[r->i] == '/') return 0;
        if (!had_ws) return PAY_ISO_ERR_XML;
        if (read_name(r, an) != 0) return PAY_ISO_ERR_XML;
        skip_ws(r);
        if (r->i >= r->n || r->s[r->i] != '=') return PAY_ISO_ERR_XML;
        r->i++;
        skip_ws(r);
        if (r->i >= r->n || (r->s[r->i] != '"' && r->s[r->i] != '\'')) return PAY_ISO_ERR_XML;
        char q = r->s[r->i++];
        while (r->i < r->n && r->s[r->i] != q) {
            char c;
            if (vl >= sizeof val - 1 || read_char(r, &c) != 0) return PAY_ISO_ERR_XML;
            val[vl++] = c;
        }
        if (r->i >= r->n) return PAY_ISO_ERR_XML;
        r->i++;
        val[vl] = '\0';
        if (is_root && pay_streq(an, "xmlns")) {
            if (*seen_ns || !pay_streq(val, r->ns)) return PAY_ISO_ERR_XML;
            *seen_ns = true;
        } else if (!is_root && pay_streq(an, "Ccy")) {
            if (r->has_ccy || vl != 3) return PAY_ISO_ERR_XML;
            for (int k = 0; k < 3; k++)
                if (!pay_is_upper(val[k])) return PAY_ISO_ERR_XML;
            pay_memcpy(r->ccy, val, 4);
            r->has_ccy = true;
        } else {
            return PAY_ISO_ERR_XML;
        }
    }
}

static int32_t xr_run(pay_xr *r)
{
    bool done = false;
    if (!r->s || r->n == 0 || r->n > PAY_ISO_IN_MAX) return PAY_ISO_ERR_ARG;
    if (at(r, "<?xml")) {
        r->i += 5;
        if (r->i >= r->n || !ws(r->s[r->i])) return PAY_ISO_ERR_XML;
        while (r->i + 1 < r->n && !(r->s[r->i] == '?' && r->s[r->i + 1] == '>')) {
            char c = r->s[r->i];
            if ((unsigned char) c >= 0x80 || c == '<' || c == '>') return PAY_ISO_ERR_XML;
            r->i++;
        }
        if (r->i + 1 >= r->n) return PAY_ISO_ERR_XML;
        r->i += 2;
    }
    skip_ws(r);
    while (r->i < r->n) {
        char c = r->s[r->i];
        if (c != '<') {
            if (done) {
                if (!ws(c)) return PAY_ISO_ERR_XML;
                r->i++;
                continue;
            }
            if (r->depth == 0) return PAY_ISO_ERR_XML;
            char d;
            if (read_char(r, &d) != 0) return PAY_ISO_ERR_XML;
            if (r->had_child[r->depth - 1]) {
                if (!ws(d)) return PAY_ISO_ERR_XML; /* mixed content */
                continue;
            }
            if (r->tlen >= PAY_X_TEXT) return PAY_ISO_ERR_XML;
            r->text[r->tlen++] = d;
            continue;
        }
        if (r->i + 1 >= r->n) return PAY_ISO_ERR_XML;
        if (r->s[r->i + 1] == '!' || r->s[r->i + 1] == '?') return PAY_ISO_ERR_XML;
        if (r->s[r->i + 1] == '/') { /* end tag */
            char nm[PAY_X_NAME + 1];
            r->i += 2;
            if (r->depth == 0 || read_name(r, nm) != 0) return PAY_ISO_ERR_XML;
            skip_ws(r);
            if (r->i >= r->n || r->s[r->i] != '>') return PAY_ISO_ERR_XML;
            r->i++;
            if (!pay_streq(nm, r->stack[r->depth - 1])) return PAY_ISO_ERR_XML;
            bool leaf = !r->had_child[r->depth - 1];
            build_path(r);
            r->text[r->tlen] = '\0';
            if (r->on_end && r->depth > 1) {
                int32_t e = r->on_end(r->ctx, r->path, leaf ? r->text : "", leaf ? r->tlen : 0,
                                      r->has_ccy ? r->ccy : 0);
                if (e) return e;
            }
            r->tlen = 0;
            r->has_ccy = false;
            r->depth--;
            if (r->depth == 0) done = true;
            continue;
        }
        /* start tag */
        if (done) return PAY_ISO_ERR_XML;
        if (r->depth > 0) {
            for (uint32_t k = 0; k < r->tlen; k++)
                if (!ws(r->text[k])) return PAY_ISO_ERR_XML; /* mixed content */
            r->tlen = 0;
            r->had_child[r->depth - 1] = true;
        }
        if (r->depth >= PAY_X_DEPTH || ++r->elems > PAY_X_ELEMS) return PAY_ISO_ERR_XML;
        r->i++;
        if (read_name(r, r->stack[r->depth]) != 0) return PAY_ISO_ERR_XML;
        bool is_root = r->depth == 0, seen_ns = false;
        if (is_root && !pay_streq(r->stack[0], r->root)) return PAY_ISO_ERR_XML;
        r->has_ccy = false;
        if (read_attrs(r, is_root, &seen_ns) != 0) return PAY_ISO_ERR_XML;
        if (is_root && !seen_ns) return PAY_ISO_ERR_XML;
        r->had_child[r->depth] = false;
        r->depth++;
        r->tlen = 0;
        build_path(r);
        if (r->on_start && r->depth > 1) {
            int32_t e = r->on_start(r->ctx, r->path);
            if (e) return e;
        }
        if (r->s[r->i] == '/') { /* empty element */
            if (r->i + 1 >= r->n || r->s[r->i + 1] != '>') return PAY_ISO_ERR_XML;
            r->i += 2;
            if (r->on_end && r->depth > 1) {
                int32_t e = r->on_end(r->ctx, r->path, "", 0, r->has_ccy ? r->ccy : 0);
                if (e) return e;
            }
            r->has_ccy = false;
            r->depth--;
            if (r->depth == 0) done = true;
        } else {
            r->i++; /* '>' */
        }
    }
    return done ? 0 : PAY_ISO_ERR_XML;
}

static void xr_init(pay_xr *r, const char *s, uint32_t n, const char *root, const char *ns)
{
    pay_memset(r, 0, sizeof *r);
    r->s = s;
    r->n = n;
    r->root = root;
    r->ns = ns;
}

int32_t pay_iso_check_xml(const char *xml, uint32_t len, const char *root, const char *ns)
{
    static pay_xr r; /* large; not reentrant (kernel: single caller per CPU) */
    if (!root || !ns) return PAY_ISO_ERR_ARG;
    xr_init(&r, xml, len, root, ns);
    return xr_run(&r);
}

/* ===== Field helpers ===== */

/* Copy text into a fixed field; refuse duplicates and over-long values. */
static int32_t put(char *dst, size_t cap, const char *t, uint32_t n)
{
    if (dst[0]) return PAY_ISO_ERR_FIELD; /* single-valued field seen twice */
    if (n == 0 || n >= cap) return PAY_ISO_ERR_FIELD;
    for (uint32_t i = 0; i < n; i++) dst[i] = t[i];
    dst[n] = '\0';
    return 0;
}

static int32_t put_amt(pay_in_amt_t *a, bool *seen, const char *t, uint32_t n, const char *ccy)
{
    int32_t f;
    if (*seen || !ccy) return PAY_ISO_ERR_FIELD;
    f = pay_amount_frac_digits(t, n);
    if (f < 0 || f > 5 || n > 19) return PAY_ISO_ERR_FIELD;
    if (!pay_parse_amount(t, n, (uint8_t) f, &a->units)) return PAY_ISO_ERR_FIELD;
    a->frac = (uint8_t) f;
    pay_memcpy(a->ccy, ccy, 4);
    *seen = true;
    return 0;
}

static bool code_in(const char *s, const char *const *set)
{
    for (; *set; set++)
        if (pay_streq(s, *set)) return true;
    return false;
}

/* ===== pacs.008 ===== */

typedef struct {
    pay_pacs008_in_t *o;
    int32_t tx; /* current transaction index, -1 outside */
    bool amt_seen[PAY_ISO_MAX_TX];
    char nb[17];
    bool in_tx;
} p8_ctx;

static int32_t p8_start(void *vc, const char *path)
{
    p8_ctx *c = (p8_ctx *) vc;
    if (pay_streq(path, "FIToFICstmrCdtTrf/CdtTrfTxInf")) {
        if (c->o->n_tx >= PAY_ISO_MAX_TX) return PAY_ISO_ERR_FIELD;
        c->tx = (int32_t) c->o->n_tx++;
    }
    return 0;
}

static int32_t p8_end(void *vc, const char *path, const char *t, uint32_t n, const char *ccy)
{
    p8_ctx *c = (p8_ctx *) vc;
    pay_pacs008_in_t *o = c->o;
    const char *G = "FIToFICstmrCdtTrf/GrpHdr/";
    const char *T = "FIToFICstmrCdtTrf/CdtTrfTxInf/";
    size_t gl = pay_strnlen(G, 64), tl = pay_strnlen(T, 64);
    bool g = pay_strnlen(path, 1024) > gl && pay_memeq(path, G, gl);
    bool tx = pay_strnlen(path, 1024) > tl && pay_memeq(path, T, tl);
    if (ccy &&
        !(tx && (pay_streq(path + tl, "IntrBkSttlmAmt") || pay_streq(path + tl, "InstdAmt"))))
        return 0; /* a Ccy on an element we do not read */
    if (g) {
        const char *f = path + gl;
        if (pay_streq(f, "MsgId")) return put(o->msg_id, sizeof o->msg_id, t, n);
        if (pay_streq(f, "CreDtTm")) return put(o->cre_dt_tm, sizeof o->cre_dt_tm, t, n);
        if (pay_streq(f, "NbOfTxs")) return put(c->nb, sizeof c->nb, t, n);
        if (pay_streq(f, "SttlmInf/SttlmMtd")) return put(o->sttlm_mtd, sizeof o->sttlm_mtd, t, n);
        return 0;
    }
    if (!tx || c->tx < 0) return 0;
    pay_pacs008_in_tx_t *x = &o->tx[c->tx];
    const char *f = path + tl;
    if (pay_streq(f, "PmtId/InstrId")) return put(x->instr_id, sizeof x->instr_id, t, n);
    if (pay_streq(f, "PmtId/EndToEndId")) return put(x->e2e, sizeof x->e2e, t, n);
    if (pay_streq(f, "PmtId/TxId")) return put(x->tx_id, sizeof x->tx_id, t, n);
    if (pay_streq(f, "PmtId/UETR")) return put(x->uetr, sizeof x->uetr, t, n);
    if (pay_streq(f, "IntrBkSttlmAmt")) return put_amt(&x->amt, &c->amt_seen[c->tx], t, n, ccy);
    if (pay_streq(f, "IntrBkSttlmDt"))
        return put(x->intr_bk_sttlm_dt, sizeof x->intr_bk_sttlm_dt, t, n);
    if (pay_streq(f, "ChrgBr")) return put(x->chrg_br, sizeof x->chrg_br, t, n);
    if (pay_streq(f, "Dbtr/Nm")) return put(x->dbtr_name, sizeof x->dbtr_name, t, n);
    if (pay_streq(f, "Cdtr/Nm")) return put(x->cdtr_name, sizeof x->cdtr_name, t, n);
    if (pay_streq(f, "DbtrAcct/Id/IBAN") || pay_streq(f, "DbtrAcct/Id/Othr/Id"))
        return put(x->dbtr_acct, sizeof x->dbtr_acct, t, n);
    if (pay_streq(f, "CdtrAcct/Id/IBAN") || pay_streq(f, "CdtrAcct/Id/Othr/Id"))
        return put(x->cdtr_acct, sizeof x->cdtr_acct, t, n);
    if (pay_streq(f, "DbtrAgt/FinInstnId/BICFI"))
        return put(x->dbtr_agt_bic, sizeof x->dbtr_agt_bic, t, n);
    if (pay_streq(f, "CdtrAgt/FinInstnId/BICFI"))
        return put(x->cdtr_agt_bic, sizeof x->cdtr_agt_bic, t, n);
    if (pay_streq(f, "RmtInf/Ustrd")) {
        if (x->ustrd[0]) return 0; /* keep the first line only */
        return put(x->ustrd, sizeof x->ustrd, t, n);
    }
    return 0;
}

static const char *const k_sttlm_in[] = {"INDA", "INGA", "COVE", "CLRG", 0};
static const char *const k_chrg_in[] = {"DEBT", "CRED", "SHAR", "SLEV", 0};

int32_t pay_iso_parse_pacs008(const char *xml, uint32_t len, pay_pacs008_in_t *out)
{
    static pay_xr r;
    p8_ctx c;
    pay_iso_ctx_t base = {PAY_ISO_BASE, 0};
    int32_t e;
    if (!out) return PAY_ISO_ERR_ARG;
    pay_memset(out, 0, sizeof *out);
    pay_memset(&c, 0, sizeof c);
    c.o = out;
    c.tx = -1;
    xr_init(&r, xml, len, "Document", "urn:iso:std:iso:20022:tech:xsd:pacs.008.001.08");
    r.ctx = &c;
    r.on_start = p8_start;
    r.on_end = p8_end;
    if ((e = xr_run(&r)) != 0) goto fail;
    e = PAY_ISO_ERR_FIELD;
    if (!out->msg_id[0] || !pay_iso_datetime_ok(&base, out->cre_dt_tm)) goto fail;
    if (!code_in(out->sttlm_mtd, k_sttlm_in)) goto fail;
    {
        size_t nl = pay_strnlen(c.nb, sizeof c.nb);
        uint64_t nb = 0;
        if (nl == 0 || nl > 15 || !pay_parse_amount(c.nb, nl, 0, &nb)) goto fail;
        if (nb != out->n_tx || out->n_tx == 0) goto fail;
        out->nb_of_txs = (uint32_t) nb;
    }
    for (uint32_t i = 0; i < out->n_tx; i++) {
        const pay_pacs008_in_tx_t *x = &out->tx[i];
        if (!x->e2e[0] || !c.amt_seen[i] || !code_in(x->chrg_br, k_chrg_in)) goto fail;
        if (!pay_uetr_valid(x->uetr)) goto fail;
        if (x->intr_bk_sttlm_dt[0] && !pay_iso_date_ok(x->intr_bk_sttlm_dt)) goto fail;
        if (!x->dbtr_name[0] || !x->cdtr_name[0]) goto fail;
    }
    return 0;
fail:
    pay_memset(out, 0, sizeof *out);
    return e;
}

/* ===== camt.053 ===== */

typedef struct {
    pay_camt053_in_t *o;
    int32_t bal, ntry;
    uint32_t stmts;
    bool bal_amt[PAY_ISO_MAX_BAL], ntry_amt[PAY_ISO_MAX_NTRY];
    char bal_cdi[PAY_ISO_MAX_BAL][5], ntry_cdi[PAY_ISO_MAX_NTRY][5];
    bool txdtls_seen[PAY_ISO_MAX_NTRY];
    bool in_txdtls;
} c53_ctx;

static int32_t c53_start(void *vc, const char *path)
{
    c53_ctx *c = (c53_ctx *) vc;
    if (pay_streq(path, "BkToCstmrStmt/Stmt")) {
        if (++c->stmts > 1) return PAY_ISO_ERR_FIELD; /* one statement per document */
    } else if (pay_streq(path, "BkToCstmrStmt/Stmt/Bal")) {
        if (c->o->n_bal >= PAY_ISO_MAX_BAL) return PAY_ISO_ERR_FIELD;
        c->bal = (int32_t) c->o->n_bal++;
    } else if (pay_streq(path, "BkToCstmrStmt/Stmt/Ntry")) {
        if (c->o->n_ntry >= PAY_ISO_MAX_NTRY) return PAY_ISO_ERR_FIELD;
        c->ntry = (int32_t) c->o->n_ntry++;
    } else if (pay_streq(path, "BkToCstmrStmt/Stmt/Ntry/NtryDtls/TxDtls")) {
        c->in_txdtls = c->ntry >= 0 && !c->txdtls_seen[c->ntry];
    }
    return 0;
}

static int32_t c53_end(void *vc, const char *path, const char *t, uint32_t n, const char *ccy)
{
    c53_ctx *c = (c53_ctx *) vc;
    pay_camt053_in_t *o = c->o;
    const char *S = "BkToCstmrStmt/Stmt/";
    size_t sl = pay_strnlen(S, 64);
    if (pay_streq(path, "BkToCstmrStmt/GrpHdr/MsgId"))
        return put(o->msg_id, sizeof o->msg_id, t, n);
    if (pay_streq(path, "BkToCstmrStmt/GrpHdr/CreDtTm"))
        return put(o->cre_dt_tm, sizeof o->cre_dt_tm, t, n);
    if (pay_strnlen(path, 1024) <= sl || !pay_memeq(path, S, sl)) return 0;
    const char *f = path + sl;
    if (pay_streq(f, "Id")) return put(o->stmt_id, sizeof o->stmt_id, t, n);
    if (pay_streq(f, "Acct/Id/IBAN") || pay_streq(f, "Acct/Id/Othr/Id"))
        return put(o->acct_id, sizeof o->acct_id, t, n);
    if (pay_streq(f, "Acct/Ccy")) return put(o->acct_ccy, sizeof o->acct_ccy, t, n);
    if (c->bal >= 0 && f[0] == 'B' && f[1] == 'a' && f[2] == 'l' && f[3] == '/') {
        pay_in_bal_t *b = &o->bal[c->bal];
        const char *g = f + 4;
        if (pay_streq(g, "Tp/CdOrPrtry/Cd")) return put(b->code, sizeof b->code, t, n);
        if (pay_streq(g, "Amt")) return put_amt(&b->amt, &c->bal_amt[c->bal], t, n, ccy);
        if (pay_streq(g, "CdtDbtInd")) return put(c->bal_cdi[c->bal], 5, t, n);
        if (pay_streq(g, "Dt/Dt")) return put(b->dt, sizeof b->dt, t, n);
        if (pay_streq(g, "Dt/DtTm")) {
            if (n < 19) return PAY_ISO_ERR_FIELD;
            return put(b->dt, sizeof b->dt, t, 10);
        }
        return 0;
    }
    if (c->ntry >= 0 && f[0] == 'N' && f[1] == 't' && f[2] == 'r' && f[3] == 'y' && f[4] == '/') {
        pay_in_ntry_t *e = &o->ntry[c->ntry];
        const char *g = f + 5;
        if (pay_streq(g, "Amt")) return put_amt(&e->amt, &c->ntry_amt[c->ntry], t, n, ccy);
        if (pay_streq(g, "CdtDbtInd")) return put(c->ntry_cdi[c->ntry], 5, t, n);
        if (pay_streq(g, "Sts/Cd")) return put(e->sts, sizeof e->sts, t, n);
        if (c->in_txdtls && pay_streq(g, "NtryDtls/TxDtls/Refs/EndToEndId"))
            return put(e->e2e, sizeof e->e2e, t, n);
        if (c->in_txdtls && pay_streq(g, "NtryDtls/TxDtls/Refs/UETR"))
            return put(e->uetr, sizeof e->uetr, t, n);
        if (pay_streq(g, "NtryDtls/TxDtls") && c->in_txdtls) {
            c->txdtls_seen[c->ntry] = true;
            c->in_txdtls = false;
        }
        return 0;
    }
    return 0;
}

static int32_t cdi(const char *s, bool *debit)
{
    if (pay_streq(s, "CRDT"))
        *debit = false;
    else if (pay_streq(s, "DBIT"))
        *debit = true;
    else
        return PAY_ISO_ERR_FIELD;
    return 0;
}

int32_t pay_iso_parse_camt053(const char *xml, uint32_t len, pay_camt053_in_t *out)
{
    static pay_xr r;
    static c53_ctx c;
    static const char *const sts[] = {"BOOK", "PDNG", "INFO", 0};
    pay_iso_ctx_t base = {PAY_ISO_BASE, 0};
    int32_t e;
    if (!out) return PAY_ISO_ERR_ARG;
    pay_memset(out, 0, sizeof *out);
    pay_memset(&c, 0, sizeof c);
    c.o = out;
    c.bal = c.ntry = -1;
    xr_init(&r, xml, len, "Document", "urn:iso:std:iso:20022:tech:xsd:camt.053.001.08");
    r.ctx = &c;
    r.on_start = c53_start;
    r.on_end = c53_end;
    if ((e = xr_run(&r)) != 0) goto fail;
    e = PAY_ISO_ERR_FIELD;
    if (!out->msg_id[0] || !out->stmt_id[0] || !out->acct_id[0] || out->n_bal == 0) goto fail;
    if (!pay_iso_datetime_ok(&base, out->cre_dt_tm)) goto fail;
    for (uint32_t i = 0; i < out->n_bal; i++) {
        pay_in_bal_t *b = &out->bal[i];
        if (!b->code[0] || !c.bal_amt[i] || cdi(c.bal_cdi[i], &b->debit) != 0) goto fail;
        if (!pay_iso_date_ok(b->dt)) goto fail;
        if (out->acct_ccy[0] && !pay_streq(out->acct_ccy, b->amt.ccy)) goto fail;
    }
    for (uint32_t i = 0; i < out->n_ntry; i++) {
        pay_in_ntry_t *x = &out->ntry[i];
        if (!c.ntry_amt[i] || cdi(c.ntry_cdi[i], &x->debit) != 0 || !code_in(x->sts, sts))
            goto fail;
        if (x->uetr[0] && !pay_uetr_valid(x->uetr)) goto fail;
        if (out->acct_ccy[0] && !pay_streq(out->acct_ccy, x->amt.ccy)) goto fail;
    }
    return 0;
fail:
    pay_memset(out, 0, sizeof *out);
    return e;
}
