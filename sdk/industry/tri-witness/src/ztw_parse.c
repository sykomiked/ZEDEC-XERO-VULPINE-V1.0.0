/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztw_parse.c — Tri-Witness's readers: the chain export's cycle fragments,
 * the "ZSC1" clearing-cycle record (with a log of every field) and the
 * "ZSJ1" journal export. An independent implementation of the formats
 * documented in sdk/industry/sutra-clear/README.md: the witness shares no
 * code with the house it checks. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ztw_internal.h"

/* ===== field-logging reader ===== */

typedef struct {
    const uint8_t *b;
    size_t n, pos;
    int bad;
    ztw_fieldlog *log;
} rd_t;

static void logf_(rd_t *r, const char *name, size_t off, size_t len, uint8_t type, uint8_t derived) {
    ztw_fieldlog *l = r->log;
    ztw_field *f;
    if (!l || r->bad) return;
    if (l->n == l->cap) {
        size_t c = l->cap ? l->cap * 2 : 256;
        ztw_field *nf = (ztw_field *) realloc(l->f, c * sizeof *nf);
        if (!nf) {
            l->oom = 1;
            return;
        }
        l->f = nf;
        l->cap = c;
    }
    f = &l->f[l->n++];
    memset(f, 0, sizeof *f);
    snprintf(f->name, sizeof f->name, "%s", name);
    f->offset = (uint32_t) off;
    f->len = (uint32_t) len;
    f->type = type;
    f->derived = derived;
}

static uint64_t rd_n(rd_t *r, size_t k, const char *name, uint8_t type, uint8_t derived) {
    uint64_t v = 0;
    if (r->bad || r->n - r->pos < k) {
        r->bad = 1;
        return 0;
    }
    for (size_t i = 0; i < k; i++) v |= (uint64_t) r->b[r->pos + i] << (8 * i);
    logf_(r, name, r->pos, k, type, derived);
    r->pos += k;
    return v;
}

static void rd_bytes(rd_t *r, void *out, size_t k, const char *name, uint8_t derived) {
    if (r->bad || r->n - r->pos < k) {
        r->bad = 1;
        return;
    }
    if (out) memcpy(out, r->b + r->pos, k);
    logf_(r, name, r->pos, k, ZTW_FT_BYTES, derived);
    r->pos += k;
}

/* A u8-length string into out (cap bytes with the NUL); no NUL inside. */
static void rd_str(rd_t *r, char *out, size_t cap, const char *name, uint8_t derived) {
    size_t k;
    if (r->bad || r->pos >= r->n) {
        r->bad = 1;
        return;
    }
    k = r->b[r->pos];
    if (k + 1 > cap || r->n - r->pos - 1 < k || memchr(r->b + r->pos + 1, 0, k)) {
        r->bad = 1;
        return;
    }
    memcpy(out, r->b + r->pos + 1, k);
    out[k] = 0;
    logf_(r, name, r->pos + 1, k, ZTW_FT_STR, derived);
    r->pos += k + 1;
}

/* A count whose items take at least `min_item` bytes each: refused before
 * any allocation if the rest of the record could not hold them. */
static uint32_t rd_count(rd_t *r, size_t width, size_t min_item, const char *name) {
    uint64_t v = rd_n(r, width, name, width == 1 ? ZTW_FT_U8 : width == 2 ? ZTW_FT_U16 : ZTW_FT_U32, 1);
    if (!r->bad && min_item && v > (r->n - r->pos) / min_item) r->bad = 1;
    return r->bad ? 0 : (uint32_t) v;
}

static void rd_instr(rd_t *r, ztw_instr *in, const char *pfx, uint8_t economic) {
    char nm[48];
#define NM(x) (snprintf(nm, sizeof nm, "%s.%s", pfx, x), nm)
    in->source = (uint8_t) rd_n(r, 1, NM("source"), ZTW_FT_U8, 0);
    rd_str(r, in->msg_id, sizeof in->msg_id, NM("msg_id"), 0);
    rd_str(r, in->cre_dt_tm, sizeof in->cre_dt_tm, NM("cre_dt_tm"), 0);
    rd_str(r, in->e2e, sizeof in->e2e, NM("e2e"), 0);
    rd_str(r, in->ccy, sizeof in->ccy, NM("ccy"), economic);
    in->amount = (int64_t) rd_n(r, 8, NM("amount"), ZTW_FT_U64, economic);
    rd_str(r, in->dbtr_agt, sizeof in->dbtr_agt, NM("dbtr_agt"), economic);
    rd_str(r, in->cdtr_agt, sizeof in->cdtr_agt, NM("cdtr_agt"), economic);
    rd_str(r, in->dbtr_nm, sizeof in->dbtr_nm, NM("dbtr_nm"), 0);
    rd_str(r, in->dbtr_acct, sizeof in->dbtr_acct, NM("dbtr_acct"), 0);
    rd_str(r, in->cdtr_nm, sizeof in->cdtr_nm, NM("cdtr_nm"), 0);
    rd_str(r, in->cdtr_acct, sizeof in->cdtr_acct, NM("cdtr_acct"), 0);
#undef NM
}

#define MIN_INSTR 21u /* source + 11 length bytes + amount (8) + 1 */

void ztw_cycle_free(ztw_cycle *c) {
    if (!c) return;
    free(c->code);
    free(c->instr);
    free(c->wd);
    if (c->held)
        for (uint32_t i = 0; i < c->n_held; i++) free(c->held[i].v);
    free(c->held);
    free(c->res);
    free(c->pos);
    free(c->fee);
    free(c->post);
    free(c->bal);
    free(c->iso);
    memset(c, 0, sizeof *c);
}

void ztw_fieldlog_free(ztw_fieldlog *l) {
    if (!l) return;
    free(l->f);
    memset(l, 0, sizeof *l);
}

int32_t ztw_parse_cycle(const uint8_t *b, size_t n, ztw_cycle *c, ztw_fieldlog *log) {
    rd_t r;
    char nm[48], magic[4];
    memset(c, 0, sizeof *c);
    memset(&r, 0, sizeof r);
    r.b = b;
    r.n = n;
    r.log = log;
    c->blob = b;
    c->blob_len = n;
    rd_bytes(&r, magic, 4, "magic", 1);
    if (!r.bad && memcmp(magic, "ZSC1", 4) != 0) r.bad = 1;
    rd_str(&r, c->house, sizeof c->house, "house_id", 1);
    rd_bytes(&r, c->key_id, 32, "house_key_id", 1);
    c->no = (uint32_t) rd_n(&r, 4, "cycle_no", ZTW_FT_U32, 1);
    rd_str(&r, c->cycle_id, sizeof c->cycle_id, "cycle_id", 0);
    rd_str(&r, c->settle_dt, sizeof c->settle_dt, "settle_dt", 1);
    c->backend = (uint8_t) rd_n(&r, 1, "backend", ZTW_FT_U8, 0);
    c->n_ccy = rd_count(&r, 1, 2, "n_ccy");
    if (c->n_ccy > ZTW_MAX_CCY) r.bad = 1;
    for (uint32_t i = 0; i < c->n_ccy && !r.bad; i++) {
        snprintf(nm, sizeof nm, "ccy[%u]", i);
        rd_str(&r, c->ccy[i], sizeof c->ccy[i], nm, 1);
        snprintf(nm, sizeof nm, "ccy[%u].minor", i);
        c->minor[i] = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
    }
    c->n_part = rd_count(&r, 2, 1, "n_part");
    if (!r.bad && c->n_part) {
        c->code = (char(*)[12]) calloc(c->n_part, 12);
        if (!c->code) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_part && !r.bad; i++) {
        snprintf(nm, sizeof nm, "part[%u]", i);
        rd_str(&r, c->code[i], 12, nm, 1);
    }
    c->n_instr = rd_count(&r, 4, MIN_INSTR, "n_instr");
    if (!r.bad && c->n_instr) {
        c->instr = (ztw_instr *) calloc(c->n_instr, sizeof *c->instr);
        if (!c->instr) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_instr && !r.bad; i++) {
        snprintf(nm, sizeof nm, "instr[%u]", i);
        rd_instr(&r, &c->instr[i], nm, 1);
    }
    c->n_held = rd_count(&r, 4, 2, "n_held");
    if (!r.bad && c->n_held) {
        c->held = (ztw_held *) calloc(c->n_held, sizeof *c->held);
        if (!c->held) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_held && !r.bad; i++) {
        ztw_held *g = &c->held[i];
        snprintf(nm, sizeof nm, "held[%u].e2e", i);
        rd_str(&r, g->e2e, sizeof g->e2e, nm, 0);
        snprintf(nm, sizeof nm, "held[%u].n", i);
        g->n = rd_count(&r, 1, MIN_INSTR, nm);
        if (!r.bad && g->n) {
            g->v = (ztw_instr *) calloc(g->n, sizeof *g->v);
            if (!g->v) return ZXV_E_NOMEM;
        }
        for (uint32_t k = 0; k < g->n && !r.bad; k++) {
            snprintf(nm, sizeof nm, "held[%u].v[%u]", i, k);
            rd_instr(&r, &g->v[k], nm, 0);
        }
    }
    c->n_res = rd_count(&r, 4, 1 + 1 + 96 + ZIND_SIG_BYTES, "n_res");
    if (!r.bad && c->n_res) {
        c->res = (ztw_res *) calloc(c->n_res, sizeof *c->res);
        if (!c->res) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_res && !r.bad; i++) {
        ztw_res *x = &c->res[i];
        snprintf(nm, sizeof nm, "res[%u].e2e", i);
        rd_str(&r, x->e2e, sizeof x->e2e, nm, 1);
        snprintf(nm, sizeof nm, "res[%u].choice", i);
        x->choice = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
        snprintf(nm, sizeof nm, "res[%u].group", i);
        rd_bytes(&r, x->group, 32, nm, 1);
        snprintf(nm, sizeof nm, "res[%u].chosen", i);
        rd_bytes(&r, x->chosen, 32, nm, 1);
        snprintf(nm, sizeof nm, "res[%u].operator", i);
        rd_bytes(&r, x->op_id, 32, nm, 1);
        snprintf(nm, sizeof nm, "res[%u].sig", i);
        if (!r.bad && r.n - r.pos >= ZIND_SIG_BYTES) x->sig = r.b + r.pos;
        rd_bytes(&r, NULL, ZIND_SIG_BYTES, nm, 1);
    }
    c->n_wd = rd_count(&r, 4, MIN_INSTR, "n_withdrawn");
    if (!r.bad && c->n_wd) {
        c->wd = (ztw_instr *) calloc(c->n_wd, sizeof *c->wd);
        if (!c->wd) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_wd && !r.bad; i++) {
        snprintf(nm, sizeof nm, "withdrawn[%u]", i);
        rd_instr(&r, &c->wd[i], nm, 0);
    }
    c->n_pos = rd_count(&r, 4, 11, "n_pos");
    if (!r.bad && c->n_pos) {
        c->pos = (ztw_pos *) calloc(c->n_pos, sizeof *c->pos);
        if (!c->pos) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_pos && !r.bad; i++) {
        snprintf(nm, sizeof nm, "pos[%u].ccy", i);
        c->pos[i].c = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
        snprintf(nm, sizeof nm, "pos[%u].part", i);
        c->pos[i].p = (uint16_t) rd_n(&r, 2, nm, ZTW_FT_U16, 1);
        snprintf(nm, sizeof nm, "pos[%u].net", i);
        c->pos[i].net = (int64_t) rd_n(&r, 8, nm, ZTW_FT_U64, 1);
    }
    c->n_fee = rd_count(&r, 4, 67, "n_fee");
    if (!r.bad && c->n_fee) {
        c->fee = (ztw_fee *) calloc(c->n_fee, sizeof *c->fee);
        if (!c->fee) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_fee && !r.bad; i++) {
        ztw_fee *x = &c->fee[i];
        static const char *const nmf[4] = {"gross", "rem_in", "fee", "rem_out"};
        uint64_t *v[4] = {&x->gross, &x->rem_in, &x->fee, &x->rem_out};
        snprintf(nm, sizeof nm, "fee[%u].ccy", i);
        x->c = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
        snprintf(nm, sizeof nm, "fee[%u].part", i);
        x->p = (uint16_t) rd_n(&r, 2, nm, ZTW_FT_U16, 1);
        for (int k = 0; k < 4; k++) {
            snprintf(nm, sizeof nm, "fee[%u].%s", i, nmf[k]);
            *v[k] = rd_n(&r, 8, nm, ZTW_FT_U64, 1);
        }
        for (int k = 0; k < 4; k++) {
            snprintf(nm, sizeof nm, "fee[%u].split[%d]", i, k);
            x->split[k] = rd_n(&r, 8, nm, ZTW_FT_U64, 1);
        }
    }
    c->n_post = rd_count(&r, 4, 24, "n_post");
    if (!r.bad && c->n_post) {
        c->post = (ztw_post *) calloc(c->n_post, sizeof *c->post);
        if (!c->post) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_post && !r.bad; i++) {
        ztw_post *x = &c->post[i];
        snprintf(nm, sizeof nm, "post[%u].kind", i);
        x->kind = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
        snprintf(nm, sizeof nm, "post[%u].ccy", i);
        x->c = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
        snprintf(nm, sizeof nm, "post[%u].part", i);
        x->p = (uint16_t) rd_n(&r, 2, nm, ZTW_FT_U16, 1);
        snprintf(nm, sizeof nm, "post[%u].bucket", i);
        x->b = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
        snprintf(nm, sizeof nm, "post[%u].amount", i);
        x->amount = (int64_t) rd_n(&r, 8, nm, ZTW_FT_U64, 1);
        snprintf(nm, sizeof nm, "post[%u].entry", i);
        x->entry = rd_n(&r, 8, nm, ZTW_FT_U64, 1);
        snprintf(nm, sizeof nm, "post[%u].from", i);
        rd_str(&r, x->from, sizeof x->from, nm, 1);
        snprintf(nm, sizeof nm, "post[%u].to", i);
        rd_str(&r, x->to, sizeof x->to, nm, 1);
        snprintf(nm, sizeof nm, "post[%u].ref", i);
        rd_str(&r, x->ref, sizeof x->ref, nm, 1);
    }
    c->n_bal = rd_count(&r, 4, 18, "n_bal");
    if (!r.bad && c->n_bal) {
        c->bal = (ztw_bal *) calloc(c->n_bal, sizeof *c->bal);
        if (!c->bal) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_bal && !r.bad; i++) {
        snprintf(nm, sizeof nm, "bal[%u].account", i);
        rd_str(&r, c->bal[i].name, sizeof c->bal[i].name, nm, 1);
        snprintf(nm, sizeof nm, "bal[%u].ccy", i);
        c->bal[i].c = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
        snprintf(nm, sizeof nm, "bal[%u].open", i);
        c->bal[i].open = (int64_t) rd_n(&r, 8, nm, ZTW_FT_U64, 1);
        snprintf(nm, sizeof nm, "bal[%u].close", i);
        c->bal[i].close = (int64_t) rd_n(&r, 8, nm, ZTW_FT_U64, 1);
    }
    c->first = rd_n(&r, 8, "first_entry", ZTW_FT_U64, 1);
    c->last = rd_n(&r, 8, "last_entry", ZTW_FT_U64, 1);
    c->head = rd_n(&r, 8, "ledger_head", ZTW_FT_U64, 1);
    rd_bytes(&r, c->jdig, 32, "journal_digest", 1);
    c->n_iso = rd_count(&r, 4, 34, "n_iso");
    if (!r.bad && c->n_iso) {
        c->iso = (ztw_iso *) calloc(c->n_iso, sizeof *c->iso);
        if (!c->iso) return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < c->n_iso && !r.bad; i++) {
        snprintf(nm, sizeof nm, "iso[%u].kind", i);
        c->iso[i].kind = (uint8_t) rd_n(&r, 1, nm, ZTW_FT_U8, 1);
        snprintf(nm, sizeof nm, "iso[%u].msg_id", i);
        rd_str(&r, c->iso[i].msg_id, sizeof c->iso[i].msg_id, nm, 1);
        snprintf(nm, sizeof nm, "iso[%u].sha3", i);
        rd_bytes(&r, c->iso[i].dig, 32, nm, 1);
    }
    if (!r.bad && r.pos != r.n) r.bad = 1; /* nothing may follow */
    if (log && log->oom) return ZXV_E_NOMEM;
    if (r.bad) {
        c->bad_at = r.pos;
        zind_set_error("ztw: the cycle record does not parse (at or after byte %zu)", r.pos);
        return ZXV_E_FORMAT;
    }
    return ZXV_OK;
}

int32_t ztw_blob_fields(const uint8_t *blob, size_t len, ztw_field *out, size_t cap,
                        size_t *out_n) {
    ztw_cycle c;
    ztw_fieldlog log;
    int32_t st;
    if (!blob || !out_n) return ZXV_E_ARG;
    memset(&log, 0, sizeof log);
    st = ztw_parse_cycle(blob, len, &c, &log);
    ztw_cycle_free(&c);
    if (st == ZXV_OK) {
        *out_n = log.n;
        if (!out || cap < log.n) st = ZXV_E_BUFFER;
        else memcpy(out, log.f, log.n * sizeof *out);
    }
    ztw_fieldlog_free(&log);
    return st;
}

/* ===== chain fragments ===== */

int32_t ztw_fragments(const zind_chain *ch, ztw_frag_cb cb, void *arg) {
    uint32_t n = 0, start = 0, cyc = 0, count = 0, next = 0;
    uint8_t *buf = NULL;
    size_t len = 0, alloc = 0;
    int32_t st = zind_chain_count(ch, &n);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < n; i++) {
        zind_record r;
        zind_rd rd;
        uint32_t no;
        uint16_t idx, cnt;
        if (zind_chain_at(ch, i, &r) != ZXV_OK) continue;
        if (r.kind != ZTW_ZSC_REC_CYCLE) {
            cb(arg, ZTW_FRAG_BADKIND, i, 0, NULL, 0, 0);
            next = 0;
            continue;
        }
        zind_rd_init(&rd, r.payload, r.payload_len);
        no = zind_rd_u32(&rd);
        idx = zind_rd_u16(&rd);
        cnt = zind_rd_u16(&rd);
        if (!rd.bad && cnt != 0 && idx == 0 && next != 0) {
            st = cb(arg, ZTW_FRAG_INCOMPLETE, start, cyc, NULL, 0, 0);
            if (st != ZXV_OK) break;
            next = 0;
        }
        if (rd.bad || cnt == 0 || idx >= cnt || (next == 0 && idx != 0) ||
            (next != 0 && (idx != next || cnt != count || no != cyc))) {
            cb(arg, ZTW_FRAG_BROKEN, i, no, NULL, 0, 0);
            next = 0;
            len = 0;
            continue;
        }
        if (idx == 0) {
            start = i;
            cyc = no;
            count = cnt;
            len = 0;
        }
        {
            size_t k = r.payload_len - 8;
            if (len + k > alloc) {
                size_t a = alloc ? alloc : 16384;
                uint8_t *nb;
                while (a < len + k) a *= 2;
                nb = (uint8_t *) realloc(buf, a);
                if (!nb) {
                    free(buf);
                    return ZXV_E_NOMEM;
                }
                buf = nb;
                alloc = a;
            }
            memcpy(buf + len, r.payload + 8, k);
            len += k;
        }
        next = (uint32_t) idx + 1;
        if (next == count) {
            st = cb(arg, ZTW_FRAG_CYCLE, start, cyc, buf, len, i - start + 1);
            next = 0;
            len = 0;
            if (st != ZXV_OK) break;
        }
    }
    if (next != 0) st = st == ZXV_OK ? cb(arg, ZTW_FRAG_INCOMPLETE, start, cyc, NULL, 0, 0) : st;
    free(buf);
    return st;
}

typedef struct {
    uint32_t want, seen;
    uint8_t *buf;
    size_t cap, len;
    int found;
} pick_t;

static int32_t pick_cb(void *arg, int what, uint32_t rec, uint32_t cyc, const uint8_t *b,
                       size_t n, uint32_t nrec) {
    pick_t *p = (pick_t *) arg;
    (void) rec;
    (void) cyc;
    (void) nrec;
    if (what != ZTW_FRAG_CYCLE) return ZXV_OK;
    if (p->seen++ != p->want) return ZXV_OK;
    p->found = 1;
    p->len = n;
    if (p->buf && p->cap >= n) memcpy(p->buf, b, n);
    return ZXV_OK;
}

static int32_t import_chain(const uint8_t *chain, size_t len, zind_chain **out) {
    if (!chain) return ZXV_E_ARG;
    return zind_chain_import(ZTW_HOUSE_DOMAIN, chain, len, out);
}

int32_t ztw_cycle_count(const uint8_t *chain, size_t len, uint32_t *out_count) {
    zind_chain *ch = NULL;
    pick_t p;
    int32_t st;
    if (!out_count) return ZXV_E_ARG;
    st = import_chain(chain, len, &ch);
    if (st != ZXV_OK) return st;
    memset(&p, 0, sizeof p);
    p.want = UINT32_MAX;
    st = ztw_fragments(ch, pick_cb, &p);
    zind_chain_destroy(ch);
    if (st == ZXV_OK) *out_count = p.seen;
    return st;
}

int32_t ztw_cycle_blob(const uint8_t *chain, size_t len, uint32_t k, uint8_t *buf, size_t cap,
                       size_t *out_len) {
    zind_chain *ch = NULL;
    pick_t p;
    int32_t st;
    if (!out_len) return ZXV_E_ARG;
    st = import_chain(chain, len, &ch);
    if (st != ZXV_OK) return st;
    memset(&p, 0, sizeof p);
    p.want = k;
    p.buf = buf;
    p.cap = cap;
    st = ztw_fragments(ch, pick_cb, &p);
    zind_chain_destroy(ch);
    if (st != ZXV_OK) return st;
    if (!p.found) return ZXV_E_NOT_FOUND;
    *out_len = p.len;
    return (!buf || cap < p.len) ? ZXV_E_BUFFER : ZXV_OK;
}

/* ===== the journal export ===== */

void ztw_journal_free(ztw_journal *j) {
    if (!j) return;
    free(j->acct);
    free(j->post);
    memset(j, 0, sizeof *j);
}

int32_t ztw_parse_journal(const uint8_t *b, size_t n, ztw_journal *j) {
    zind_rd r;
    uint8_t magic[4];
    uint32_t na;
    uint64_t np;
    memset(j, 0, sizeof *j);
    if (!b || n < 16) return ZXV_E_FORMAT;
    zind_rd_init(&r, b, n);
    for (int i = 0; i < 4; i++) magic[i] = zind_rd_u8(&r);
    if (memcmp(magic, "ZSJ1", 4) != 0) return ZXV_E_FORMAT;
    na = zind_rd_u32(&r);
    if (r.bad || na > (n - r.pos) / 7) return ZXV_E_FORMAT;
    j->acct = (ztw_jacct *) calloc(na ? na : 1, sizeof *j->acct);
    if (!j->acct) return ZXV_E_NOMEM;
    j->n_acct = na;
    for (uint32_t a = 0; a < na && !r.bad; a++) {
        uint32_t id = zind_rd_u32(&r);
        char tmp[256];
        zind_rd_str(&r, tmp, sizeof tmp);
        if (strlen(tmp) >= sizeof j->acct[a].name) r.bad = 1;
        else memcpy(j->acct[a].name, tmp, strlen(tmp) + 1);
        zind_rd_str(&r, tmp, sizeof tmp);
        if (strlen(tmp) >= sizeof j->acct[a].ccy) r.bad = 1;
        else memcpy(j->acct[a].ccy, tmp, strlen(tmp) + 1);
        j->acct[a].kind = zind_rd_u8(&r);
        if (id != a || j->acct[a].kind > 1) r.bad = 1;
    }
    np = zind_rd_u64(&r);
    if (r.bad || np > (n - r.pos) / 25) {
        ztw_journal_free(j);
        return ZXV_E_FORMAT;
    }
    j->post = (ztw_jpost *) calloc(np ? (size_t) np : 1, sizeof *j->post);
    if (!j->post) {
        ztw_journal_free(j);
        return ZXV_E_NOMEM;
    }
    j->n_post = (size_t) np;
    for (size_t i = 0; i < j->n_post && !r.bad; i++) {
        char tmp[256];
        j->post[i].id = zind_rd_u64(&r);
        j->post[i].from = zind_rd_u32(&r);
        j->post[i].to = zind_rd_u32(&r);
        j->post[i].amount = (int64_t) zind_rd_u64(&r);
        zind_rd_str(&r, tmp, sizeof tmp);
        if (strlen(tmp) >= sizeof j->post[i].ref) r.bad = 1;
        else memcpy(j->post[i].ref, tmp, strlen(tmp) + 1);
    }
    if (r.bad || r.pos != n) {
        ztw_journal_free(j);
        return ZXV_E_FORMAT;
    }
    return ZXV_OK;
}
