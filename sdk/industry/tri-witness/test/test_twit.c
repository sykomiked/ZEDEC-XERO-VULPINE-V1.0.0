/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_twit.c — Tri-Witness against real Sutra-Clear exports: the full
 * round trip (house -> chain and journal exports -> witness in another
 * context), tampering at every field of every cycle record (re-signed with
 * the house key, and unsigned in the export), every field of the journal,
 * byte flips, replayed, reordered, dropped and broken records, untrusted
 * house and operator keys, a second writer on the ledger, capabilities
 * (a witness never posts) and the signed report. */
#include "zind_test.h"
#include "zsclear.h"
#include "ztwit.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              ztw_status_name(rc_), (int) (want), ztw_status_name(want));                     \
    } while (0)

static const char *k_code[4] = {"NGBKNGLA", "GHBKGHAC", "SNBKSNDA", "M001"};
static const char *k_ccy[3] = {"NGN", "GHS", "XOF"};

typedef struct {
    zt_env e; /* the house's context */
    zind_signer *hk, *op, *rogue;
    zsc_house *h;
    uint32_t issuer[3];
    uint8_t *chain, *jr;
    size_t cl, jl;
    /* the witness's own context */
    zxv_ctx *wctx;
    zxv_cap *wcap;
    zind_signer *wk;
    ztw_witness *w;
} fx_t;

static zsc_instr mk(const char *e2e, int c, int64_t amt, int d, int k) {
    zsc_instr in;
    memset(&in, 0, sizeof in);
    snprintf(in.msg_id, sizeof in.msg_id, "MSG-%s", e2e);
    snprintf(in.cre_dt_tm, sizeof in.cre_dt_tm, "2026-10-12T08:00:00Z");
    snprintf(in.e2e, sizeof in.e2e, "%s", e2e);
    memcpy(in.ccy, k_ccy[c], 4);
    in.amount = amt;
    snprintf(in.dbtr_agt, sizeof in.dbtr_agt, "%s", k_code[d]);
    snprintf(in.cdtr_agt, sizeof in.cdtr_agt, "%s", k_code[k]);
    snprintf(in.dbtr_nm, sizeof in.dbtr_nm, "Payer %d", d);
    snprintf(in.dbtr_acct, sizeof in.dbtr_acct, "ACCT-%d", d);
    snprintf(in.cdtr_nm, sizeof in.cdtr_nm, "Payee %d", k);
    snprintf(in.cdtr_acct, sizeof in.cdtr_acct, "ACCT-%d", k);
    return in;
}

static int decide(fx_t *f, const zind_signer *op, const char *e2e, uint32_t choice) {
    uint8_t msg[256], sig[ZIND_SIG_BYTES];
    size_t len = 0;
    if (zsc_decision_message(f->h, e2e, choice, msg, sizeof msg, &len) != ZXV_OK) return -1;
    if (zind_sign(f->e.cap, op, msg, len, "sutra-clear/operator", sig) != ZXV_OK) return -1;
    return zsc_resolve(f->h, zind_signer_pk(op), e2e, choice, sig) == ZXV_OK ? 0 : -1;
}

static int sub(fx_t *f, const char *e2e, int c, int64_t amt, int d, int k, int32_t want) {
    zsc_instr in = mk(e2e, c, amt, d, k);
    return zsc_submit(f->h, &in) == want ? 0 : -1;
}

static int do_export(fx_t *f) {
    free(f->chain);
    free(f->jr);
    f->chain = f->jr = NULL;
    (void) zind_chain_export(zsc_records(f->h), NULL, 0, &f->cl);
    (void) zsc_journal_export(f->e.cap, NULL, 0, &f->jl);
    f->chain = (uint8_t *) malloc(f->cl + 1);
    f->jr = (uint8_t *) malloc(f->jl + 1);
    if (!f->chain || !f->jr) return -1;
    if (zind_chain_export(zsc_records(f->h), f->chain, f->cl, &f->cl) != ZXV_OK) return -1;
    return zsc_journal_export(f->e.cap, f->jr, f->jl, &f->jl) == ZXV_OK ? 0 : -1;
}

/* The house: three currencies (XOF has no minor units), four participants. */
static int fx_house(fx_t *f, int with_rogue_op) {
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    f->hk = zt_signer(f->e.cap, 41);
    f->op = zt_signer(f->e.cap, 42);
    f->rogue = zt_signer(f->e.cap, 43);
    if (!f->hk || !f->op || !f->rogue) return -1;
    if (zsc_house_create(f->e.cap, "SCLR", f->hk, NULL, &f->h) != ZXV_OK) return -1;
    for (int c = 0; c < 3; c++)
        if (zsc_currency_add(f->h, k_ccy[c]) != ZXV_OK) return -1;
    for (int p = 0; p < 4; p++)
        if (zsc_participant_add(f->h, k_code[p]) != ZXV_OK) return -1;
    if (zsc_operator_add(f->h, zind_signer_pk(f->op)) != ZXV_OK) return -1;
    if (with_rogue_op && zsc_operator_add(f->h, zind_signer_pk(f->rogue)) != ZXV_OK) return -1;
    for (int c = 0; c < 3; c++) {
        f->issuer[c] = zt_issuer(f->e.cap, k_ccy[c]);
        if (f->issuer[c] == UINT32_MAX) return -1;
        for (int p = 0; p < 4; p++) {
            uint32_t a = 0;
            char ref[48];
            if (zsc_account(f->h, ZSC_ACCT_PARTICIPANT, k_code[p], k_ccy[c], &a) != ZXV_OK) return -1;
            snprintf(ref, sizeof ref, "fund-%d-%d", c, p);
            if (zt_fund(f->e.cap, k_ccy[c], f->issuer[c], a, 1000000000, ref) != 0) return -1;
        }
    }
    return 0;
}

/* Two cycles: payments in every currency between every participant, a
 * conflict decided in the cycle, a conflict held over and decided in the
 * next, a withdrawal and a Sutra treaty. */
static int fx_scenario(fx_t *f, const zind_signer *decider) {
    zsc_batch b;
    if (zsc_cycle_open(f->h, "C1", "2026-10-12T09:00:00Z") != ZXV_OK) return -1;
    if (sub(f, "E01", 0, 1250000, 0, 1, ZXV_OK) || sub(f, "E02", 0, 830050, 1, 2, ZXV_OK) ||
        sub(f, "E03", 0, 99999, 2, 3, ZXV_OK) || sub(f, "E04", 0, 5000, 3, 0, ZXV_OK) ||
        sub(f, "E05", 1, 40000, 0, 2, ZXV_OK) || sub(f, "E06", 1, 12345, 2, 1, ZXV_OK) ||
        sub(f, "E07", 1, 999, 3, 0, ZXV_OK) || sub(f, "E08", 2, 700, 1, 3, ZXV_OK) ||
        sub(f, "E09", 2, 650, 3, 0, ZXV_OK) || sub(f, "E10", 2, 1000, 2, 1, ZXV_OK) ||
        sub(f, "E02", 0, 803050, 1, 2, ZIND_E_HELD) || sub(f, "E11", 0, 7000, 0, 3, ZXV_OK) ||
        sub(f, "E11", 0, 7100, 0, 3, ZIND_E_HELD) || sub(f, "E12", 1, 222, 3, 0, ZXV_OK))
        return -1;
    if (zsc_withdraw(f->h, "E12") != ZXV_OK) return -1;
    if (zsc_submit_sutra(f->h,
                         "TREATY BATCH-1 GHS NET\nPARTY NGBKNGLA 0 0 1/2\nPARTY GHBKGHAC 0 0 1/2\n"
                         "OWE NGBKNGLA GHBKGHAC 4000\nEND\n",
                         &b) != ZXV_OK)
        return -1;
    if (decide(f, decider, "E02", 2) != 0) return -1;
    if (zsc_cycle_close(f->h, NULL) != ZXV_OK) return -1;
    if (zsc_cycle_open(f->h, "C2", "2026-10-12T15:00:00Z") != ZXV_OK) return -1;
    if (decide(f, decider, "E11", 1) != 0) return -1;
    if (sub(f, "F01", 0, 31415, 1, 0, ZXV_OK) || sub(f, "F02", 0, 2718, 2, 1, ZXV_OK) ||
        sub(f, "F03", 0, 16180, 3, 2, ZXV_OK) || sub(f, "F04", 1, 7777, 0, 3, ZXV_OK) ||
        sub(f, "F05", 1, 8888, 1, 2, ZXV_OK) || sub(f, "F06", 2, 1500, 0, 2, ZXV_OK) ||
        sub(f, "F07", 2, 2500, 3, 1, ZXV_OK) || sub(f, "F08", 0, 12, 0, 1, ZXV_OK) ||
        sub(f, "F09", 0, 44, 0, 1, ZXV_OK))
        return -1;
    if (zsc_cycle_close(f->h, NULL) != ZXV_OK) return -1;
    return do_export(f);
}

static int fx_witness(fx_t *f, int trust_house, int trust_op) {
    if (zxv_ctx_create(&f->wctx) != ZXV_OK) return -1;
    if (zxv_cap_create(f->wctx, 9, NULL, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN, &f->wcap) !=
        ZXV_OK)
        return -1;
    f->wk = zt_signer(f->wcap, 44);
    if (!f->wk || ztw_create(f->wcap, f->wk, &f->w) != ZXV_OK) return -1;
    if (trust_house && ztw_trust_house(f->w, zind_signer_pk(f->hk)) != ZXV_OK) return -1;
    if (trust_op && ztw_trust_operator(f->w, zind_signer_pk(f->op)) != ZXV_OK) return -1;
    return 0;
}

static int fx_open(fx_t *f) {
    memset(f, 0, sizeof *f);
    if (fx_house(f, 0) != 0) return -1;
    if (fx_scenario(f, f->op) != 0) return -1;
    return fx_witness(f, 1, 1);
}

static void fx_close(fx_t *f) {
    ztw_destroy(f->w);
    zind_signer_destroy(f->wk);
    zxv_cap_destroy(f->wcap);
    zxv_ctx_destroy(f->wctx);
    free(f->chain);
    free(f->jr);
    zsc_house_destroy(f->h);
    zind_signer_destroy(f->hk);
    zind_signer_destroy(f->op);
    zind_signer_destroy(f->rogue);
    zt_env_close(&f->e);
    memset(f, 0, sizeof *f);
}

static uint32_t n_code(const ztw_witness *w, uint16_t code) {
    uint32_t n = 0, k = 0;
    ztw_finding f;
    (void) ztw_finding_count(w, &n);
    for (uint32_t i = 0; i < n; i++)
        if (ztw_finding_at(w, i, &f) == ZXV_OK && f.code == code) k++;
    return k;
}

static void dump(const ztw_witness *w) {
    uint32_t n = 0;
    ztw_finding f;
    (void) ztw_finding_count(w, &n);
    for (uint32_t i = 0; i < n && i < 12; i++)
        if (ztw_finding_at(w, i, &f) == ZXV_OK)
            fprintf(stderr, "    %s %s: %s\n", ztw_finding_name(f.code), f.field, f.detail);
}

/* The cycle blobs of an export. */
typedef struct {
    uint8_t *b[8];
    size_t n[8];
    uint32_t count;
} blobs_t;

static int get_blobs(const uint8_t *chain, size_t cl, blobs_t *bl) {
    memset(bl, 0, sizeof *bl);
    if (ztw_cycle_count(chain, cl, &bl->count) != ZXV_OK || bl->count > 8) return -1;
    for (uint32_t k = 0; k < bl->count; k++) {
        size_t len = 0;
        if (ztw_cycle_blob(chain, cl, k, NULL, 0, &len) != ZXV_E_BUFFER) return -1;
        bl->b[k] = (uint8_t *) malloc(len);
        if (!bl->b[k] || ztw_cycle_blob(chain, cl, k, bl->b[k], len, &bl->n[k]) != ZXV_OK) return -1;
    }
    return 0;
}

static void free_blobs(blobs_t *bl) {
    for (uint32_t k = 0; k < bl->count; k++) free(bl->b[k]);
    memset(bl, 0, sizeof *bl);
}

/* A chain of the given cycle blobs (cycle numbers nos[]), fragments of at
 * most `frag` bytes, signed by `s`; extra_kind != 0 appends a record of
 * that kind. Returns a malloc'd export. */
static uint8_t *rebuild(const zxv_cap *cap, const zind_signer *s, uint8_t *const *b,
                        const size_t *n, const uint32_t *nos, uint32_t count, size_t frag,
                        uint16_t extra_kind, size_t *out_len) {
    zind_chain *ch = NULL;
    uint8_t *buf = (uint8_t *) malloc(8 + frag), *out = NULL;
    int ok = buf && zind_chain_create("sutra-clear", 4096, &ch) == ZXV_OK;
    for (uint32_t k = 0; ok && k < count; k++) {
        size_t nf = (n[k] + frag - 1) / frag;
        for (size_t i = 0; ok && i < nf; i++) {
            size_t off = i * frag, len = n[k] - off > frag ? frag : n[k] - off;
            zind_wr w;
            zind_wr_init(&w, buf, 8 + frag);
            zind_wr_u32(&w, nos[k]);
            zind_wr_u16(&w, (uint16_t) i);
            zind_wr_u16(&w, (uint16_t) nf);
            zind_wr_bytes(&w, b[k] + off, len);
            ok = zind_chain_append(cap, ch, s, 1, buf, w.len, NULL, NULL) == ZXV_OK;
        }
    }
    if (ok && extra_kind) ok = zind_chain_append(cap, ch, s, extra_kind, buf, 8, NULL, NULL) == ZXV_OK;
    if (ok) {
        (void) zind_chain_export(ch, NULL, 0, out_len);
        out = (uint8_t *) malloc(*out_len);
        if (out && zind_chain_export(ch, out, *out_len, out_len) != ZXV_OK) {
            free(out);
            out = NULL;
        }
    }
    zind_chain_destroy(ch);
    free(buf);
    return out;
}

/* Where record `rec`'s payload starts in a chain export (0 if not found):
 * "ZIND" u8 version u8 len domain u32 count, then per record u64 seq u16
 * kind u32 len payload prev hash key_id sig. */
static size_t payload_off(const uint8_t *x, size_t n, uint32_t rec) {
    size_t at;
    if (n < 10) return 0;
    at = 6 + x[5] + 4;
    for (uint32_t i = 0; at + 14 <= n; i++) {
        size_t len = (size_t) x[at + 10] | ((size_t) x[at + 11] << 8) | ((size_t) x[at + 12] << 16) |
                     ((size_t) x[at + 13] << 24);
        if (i == rec) return at + 14;
        at += 14 + len + 96 + ZIND_SIG_BYTES;
    }
    return 0;
}

/* ===== tests ===== */

static void test_caps(void) {
    fx_t f;
    zxv_cap *rd = NULL, *child = NULL;
    ztw_witness *w = NULL;
    ztw_summary s;
    uint32_t na = 0;
    CHECK(fx_open(&f) == 0, "fixture");
    CHECK_RC(ztw_create(f.e.cap, f.wk, &w), ZTW_E_WRITABLE);
    CHECK(w == NULL, "no witness on a writable cap");
    CHECK_RC(ztw_create(NULL, f.wk, &w), ZXV_E_ARG);
    CHECK_RC(ztw_create(f.wcap, NULL, &w), ZXV_E_ARG);
    CHECK_RC(ztw_create(f.wcap, f.wk, NULL), ZXV_E_ARG);
    CHECK_RC(zxv_cap_create(f.wctx, 10, NULL, ZXV_CAP_LEDGER_READ, &rd), ZXV_OK);
    CHECK_RC(ztw_create(rd, f.wk, &w), ZXV_E_CAP);
    /* an attenuated child of the house's cap without LEDGER_POST works */
    CHECK_RC(zxv_cap_attenuate(f.e.cap, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN, &child), ZXV_OK);
    CHECK_RC(ztw_create(child, f.wk, &w), ZXV_OK);
    CHECK_RC(ztw_verify(w, f.chain, f.cl, f.jr, f.jl, &s), ZTW_E_NOTRUST);
    CHECK_RC(ztw_trust_house(w, zind_signer_pk(f.hk)), ZXV_OK);
    CHECK_RC(ztw_trust_house(w, zind_signer_pk(f.hk)), ZXV_E_DUPLICATE);
    CHECK_RC(ztw_trust_house(w, NULL), ZXV_E_ARG);
    CHECK_RC(ztw_trust_operator(NULL, zind_signer_pk(f.op)), ZXV_E_ARG);
    for (uint8_t k = 0; k < 7; k++) {
        zind_signer *x = zt_signer(f.wcap, (uint8_t) (100 + k));
        CHECK_RC(ztw_trust_house(w, zind_signer_pk(x)), ZXV_OK);
        zind_signer_destroy(x);
    }
    {
        zind_signer *x = zt_signer(f.wcap, 120);
        CHECK_RC(ztw_trust_house(w, zind_signer_pk(x)), ZXV_E_CAPACITY);
        zind_signer_destroy(x);
    }
    CHECK_RC(ztw_trust_operator(w, zind_signer_pk(f.op)), ZXV_OK);
    CHECK_RC(ztw_verify(w, f.chain, f.cl, f.jr, f.jl, &s), ZXV_OK);
    CHECK_RC(ztw_verify(w, NULL, 5, f.jr, f.jl, &s), ZXV_E_ARG);
    CHECK_RC(ztw_verify(NULL, f.chain, f.cl, f.jr, f.jl, &s), ZXV_E_ARG);
    /* revoked: the witness's own cap stops working */
    CHECK_RC(zxv_cap_revoke(child), ZXV_OK);
    CHECK_RC(ztw_verify(w, f.chain, f.cl, f.jr, f.jl, &s), ZXV_E_CAP);
    ztw_destroy(w);
    zxv_cap_destroy(child);
    zxv_cap_destroy(rd);
    /* the witness's context has no ledger accounts after any of this */
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZXV_OK);
    CHECK_RC(zxv_ledger_account_count_cap(f.wcap, &na), ZXV_OK);
    CHECK(na == 0, "the witness opened %u accounts", na);
    CHECK(strcmp(ztw_status_name(ZTW_E_DISCREPANCY), "ZTW_E_DISCREPANCY") == 0, "name");
    CHECK(strcmp(ztw_status_name(ZTW_E_WRITABLE), "ZTW_E_WRITABLE") == 0, "name");
    CHECK(strcmp(ztw_status_name(ZTW_E_NOTRUST), "ZTW_E_NOTRUST") == 0, "name");
    CHECK(strcmp(ztw_status_name(ZXV_E_CAP), zind_status_name(ZXV_E_CAP)) == 0, "fallback");
    CHECK(strcmp(ztw_finding_name(ZTW_F_JFORMAT), "JFORMAT") == 0, "finding name");
    CHECK(strcmp(ztw_finding_name(99), "?") == 0, "unknown finding");
    CHECK(ztw_report(NULL) == NULL, "report of NULL");
    fx_close(&f);
}

static void test_round_trip(void) {
    fx_t f;
    ztw_summary s;
    uint32_t n = 0, nrec = 0, cycles = 0;
    zind_record r;
    const uint8_t *tr[1];
    CHECK(fx_open(&f) == 0, "fixture");
    memset(&s, 0, sizeof s);
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZXV_OK);
    dump(f.w);
    CHECK(s.cycles == 2, "cycles %u", s.cycles);
    CHECK_RC(zind_chain_count(zsc_records(f.h), &nrec), ZXV_OK);
    CHECK(s.records == nrec && nrec >= 3, "records %u of %u", s.records, nrec);
    CHECK(s.instructions == 11 + 10, "instructions %u", s.instructions);
    CHECK(s.findings == 0, "findings %u", s.findings);
    CHECK(s.postings > 20, "postings %u", s.postings);
    CHECK(s.journal_postings == s.covered_postings && s.journal_postings > s.postings,
          "journal %llu covered %llu", (unsigned long long) s.journal_postings,
          (unsigned long long) s.covered_postings);
    {
        uint8_t d[32];
        CHECK_RC(zxv_sha3_256(f.chain, f.cl, d), ZXV_OK);
        CHECK(memcmp(d, s.chain_digest, 32) == 0, "chain digest");
        CHECK_RC(zxv_sha3_256(f.jr, f.jl, d), ZXV_OK);
        CHECK(memcmp(d, s.journal_digest, 32) == 0, "journal digest");
    }
    CHECK_RC(ztw_finding_count(f.w, &n), ZXV_OK);
    CHECK(n == 0, "no findings listed");
    CHECK_RC(zind_chain_count(ztw_report(f.w), &n), ZXV_OK);
    CHECK(n == 1, "the report is one summary record (%u)", n);
    tr[0] = zind_signer_pk(f.wk);
    CHECK_RC(zind_chain_verify(f.wcap, ztw_report(f.w), tr, 1, NULL), ZXV_OK);
    CHECK(strcmp(zind_chain_domain(ztw_report(f.w)), "tri-witness/report") == 0, "domain");
    CHECK_RC(zind_chain_at(ztw_report(f.w), 0, &r), ZXV_OK);
    CHECK(r.kind == ZTW_REC_SUMMARY && r.payload_len > 5 && memcmp(r.payload, "ZTWS1", 5) == 0,
          "summary record");
    {
        uint8_t tag = 0, want = 0;
        int32_t lp = 0;
        CHECK_RC(zind_tag(ZXV_LP_TRUE, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_MANIFEST, &want), ZXV_OK);
        tag = r.payload[r.payload_len - 1];
        CHECK(tag == want, "verdict tag TRUE");
        CHECK_RC(zind_tag_lp(tag, &lp), ZXV_OK);
        CHECK(lp == ZXV_LP_TRUE, "verdict TRUE");
    }
    /* the auditors' tools */
    CHECK_RC(ztw_cycle_count(f.chain, f.cl, &cycles), ZXV_OK);
    CHECK(cycles == 2, "cycle count %u", cycles);
    {
        size_t len = 0, nf = 0;
        uint8_t *b;
        ztw_field *fl;
        CHECK_RC(ztw_cycle_blob(f.chain, f.cl, 0, NULL, 0, &len), ZXV_E_BUFFER);
        CHECK(len > ZSC_FRAG_MAX, "cycle 1 spans fragments (%zu bytes)", len);
        CHECK_RC(ztw_cycle_blob(f.chain, f.cl, 2, NULL, 0, &len), ZXV_E_NOT_FOUND);
        CHECK_RC(ztw_cycle_blob(f.chain, f.cl, 0, NULL, 0, NULL), ZXV_E_ARG);
        CHECK_RC(ztw_cycle_count(f.chain, 10, &cycles), ZXV_E_FORMAT);
        CHECK_RC(ztw_cycle_blob(f.chain, f.cl, 1, NULL, 0, &len), ZXV_E_BUFFER);
        b = (uint8_t *) malloc(len);
        CHECK_RC(ztw_cycle_blob(f.chain, f.cl, 1, b, len, &len), ZXV_OK);
        CHECK(len >= 4 && memcmp(b, "ZSC1", 4) == 0, "magic");
        CHECK_RC(ztw_blob_fields(b, len, NULL, 0, &nf), ZXV_E_BUFFER);
        CHECK(nf > 100, "fields %zu", nf);
        fl = (ztw_field *) calloc(nf, sizeof *fl);
        CHECK_RC(ztw_blob_fields(b, len, fl, nf, &nf), ZXV_OK);
        CHECK(strcmp(fl[0].name, "magic") == 0 && fl[0].offset == 0 && fl[0].len == 4, "first field");
        CHECK(strcmp(fl[nf - 1].name + strlen(fl[nf - 1].name) - 5, ".sha3") == 0, "last field %s",
              fl[nf - 1].name);
        {
            uint32_t end = 0;
            int ordered = 1;
            for (size_t i = 0; i < nf; i++) {
                if (fl[i].offset < end) ordered = 0;
                end = fl[i].offset + fl[i].len;
            }
            CHECK(ordered && end == len, "fields tile the record (%u of %zu)", end, len);
        }
        CHECK_RC(ztw_blob_fields(b, len - 1, fl, nf, &nf), ZXV_E_FORMAT);
        CHECK_RC(ztw_blob_fields(NULL, len, fl, nf, &nf), ZXV_E_ARG);
        free(fl);
        free(b);
    }
    /* again: the report is replaced, not extended */
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZXV_OK);
    CHECK_RC(zind_chain_count(ztw_report(f.w), &n), ZXV_OK);
    CHECK(n == 1, "report replaced");
    fx_close(&f);
}

/* No journal, a garbage journal, a garbage chain, an empty chain. */
static void test_inputs(void) {
    fx_t f;
    ztw_summary s;
    ztw_finding x;
    CHECK(fx_open(&f) == 0, "fixture");
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, NULL, 0, &s), ZTW_E_DISCREPANCY);
    CHECK(s.findings == 1 && n_code(f.w, ZTW_F_JFORMAT) == 1, "one JFORMAT (%u)", s.findings);
    CHECK_RC(ztw_finding_at(f.w, 0, &x), ZXV_OK);
    CHECK(x.record == UINT32_MAX && x.tag != 0, "a journal finding");
    CHECK_RC(ztw_finding_at(f.w, 1, &x), ZXV_E_NOT_FOUND);
    CHECK_RC(ztw_finding_at(f.w, 0, NULL), ZXV_E_ARG);
    {
        int32_t lp = 0;
        uint8_t base = 0, pol = 0, ph = 0;
        CHECK_RC(zind_tag_lp(x.tag, &lp), ZXV_OK);
        CHECK(lp == ZXV_LP_FALSE, "a finding is FALSE");
        CHECK_RC(zxv_hex_unpack(x.tag, &base, &pol, &ph), ZXV_OK);
        CHECK(pol == ZXV_HEX_POL_FORBIDDEN, "and Forbidden");
    }
    CHECK(s.cycles == 2 && s.instructions == 21, "the chain is still replayed");
    f.jr[0] ^= 1;
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
    CHECK(s.findings == 1 && n_code(f.w, ZTW_F_JFORMAT) == 1, "bad magic");
    f.jr[0] ^= 1;
    for (size_t cut = 0; cut < f.jl; cut += 1 + cut / 3) {
        CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, cut, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_JFORMAT) == 1, "a journal cut at %zu", cut);
    }
    CHECK_RC(ztw_verify(f.w, f.chain, 7, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
    CHECK(n_code(f.w, ZTW_F_IMPORT) == 1 && s.cycles == 0, "a cut chain");
    for (size_t cut = 0; cut < f.cl; cut += 997) {
        CHECK_RC(ztw_verify(f.w, f.chain, cut, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_IMPORT) == 1, "a chain cut at %zu", cut);
    }
    {
        zind_chain *e = NULL;
        uint8_t buf[256];
        size_t n = 0;
        CHECK_RC(zind_chain_create("sutra-clear", 4, &e), ZXV_OK);
        CHECK_RC(zind_chain_export(e, buf, sizeof buf, &n), ZXV_OK);
        CHECK_RC(ztw_verify(f.w, buf, n, f.jr, f.jl, &s), ZXV_OK);
        CHECK(s.cycles == 0 && s.covered_postings == 0, "an empty chain covers nothing");
        zind_chain_destroy(e);
        CHECK_RC(zind_chain_create("other-domain", 4, &e), ZXV_OK);
        CHECK_RC(zind_chain_export(e, buf, sizeof buf, &n), ZXV_OK);
        CHECK_RC(ztw_verify(f.w, buf, n, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_IMPORT) == 1, "another domain");
        zind_chain_destroy(e);
    }
    fx_close(&f);
}

/* Every field of every cycle record, changed and re-signed with the house
 * key: a derived field is always a finding. Then the same change in the
 * export without re-signing: always a LINK finding. */
static void test_every_field(void) {
    fx_t f;
    blobs_t bl;
    uint32_t nos[2] = {1, 2};
    size_t derived = 0, caught = 0, declared = 0, declared_caught = 0, unsigned_caught = 0,
           unsigned_tried = 0, skipped = 0;
    CHECK(fx_open(&f) == 0, "fixture");
    CHECK(get_blobs(f.chain, f.cl, &bl) == 0 && bl.count == 2, "blobs");
    for (uint32_t k = 0; k < bl.count; k++) {
        size_t nf = 0;
        ztw_field *fl;
        (void) ztw_blob_fields(bl.b[k], bl.n[k], NULL, 0, &nf);
        fl = (ztw_field *) calloc(nf + 1, sizeof *fl);
        CHECK_RC(ztw_blob_fields(bl.b[k], bl.n[k], fl, nf, &nf), ZXV_OK);
        for (size_t i = 0; i < nf; i++) {
            uint8_t *b = bl.b[k], save[64], *ex;
            size_t el = 0, len = fl[i].len > 64 ? 64 : fl[i].len;
            uint8_t *p = b + fl[i].offset;
            ztw_summary s;
            int32_t st;
            if (fl[i].len == 0) {
                skipped++;
                continue;
            }
            memcpy(save, p, len);
            if (fl[i].type == ZTW_FT_STR) {
                uint8_t *q = p + fl[i].len - 1;
                if ((*q >= 'A' && *q < 'Z') || (*q >= 'a' && *q < 'z') || (*q >= '0' && *q < '9'))
                    (*q)++;
                else if (*q == 'Z' || *q == 'z' || *q == '9')
                    (*q)--;
                else
                    *q = 'X';
            } else {
                p[0] ^= 1;
            }
            ex = rebuild(f.e.cap, f.hk, bl.b, bl.n, nos, 2, ZSC_FRAG_MAX, 0, &el);
            CHECK(ex != NULL, "rebuild");
            st = ex ? ztw_verify(f.w, ex, el, f.jr, f.jl, &s) : ZXV_E_INTERNAL;
            if (fl[i].derived) {
                derived++;
                CHECK(st == ZTW_E_DISCREPANCY && s.findings > 0,
                      "cycle %u field %s changed and re-signed: %s", k + 1, fl[i].name,
                      ztw_status_name(st));
                caught += st == ZTW_E_DISCREPANCY;
                CHECK(n_code(f.w, ZTW_F_SIGNATURE) == 0 && n_code(f.w, ZTW_F_LINK) == 0,
                      "re-signed: the records themselves verify (%s)", fl[i].name);
            } else {
                declared++;
                CHECK(st == ZXV_OK || st == ZTW_E_DISCREPANCY, "declared field %s: %s",
                      fl[i].name, ztw_status_name(st));
                declared_caught += st == ZTW_E_DISCREPANCY;
            }
            free(ex);
            /* The same change in the house's own export, not re-signed: every
             * declared field (the signature is all that protects it) and every
             * eighth derived one. */
            if (!fl[i].derived || i % 8 == 0) {
                uint8_t *c2 = (uint8_t *) malloc(f.cl);
                uint32_t first = k == 0 ? 0 : (uint32_t) ((bl.n[0] + ZSC_FRAG_MAX - 1) / ZSC_FRAG_MAX);
                int placed = 1;
                memcpy(c2, f.chain, f.cl);
                for (size_t j = 0; j < fl[i].len; j++) {
                    size_t off = fl[i].offset + j;
                    size_t at = payload_off(f.chain, f.cl, first + (uint32_t) (off / ZSC_FRAG_MAX));
                    if (!at) placed = 0;
                    else c2[at + 8 + off % ZSC_FRAG_MAX] = p[j];
                }
                CHECK(placed, "field %s located in the export", fl[i].name);
                st = ztw_verify(f.w, c2, f.cl, f.jr, f.jl, &s);
                CHECK(st == ZTW_E_DISCREPANCY && n_code(f.w, ZTW_F_LINK) >= 1,
                      "cycle %u field %s changed in the export: %s", k + 1, fl[i].name,
                      ztw_status_name(st));
                unsigned_tried++;
                unsigned_caught += n_code(f.w, ZTW_F_LINK) >= 1;
                free(c2);
            }
            memcpy(p, save, len);
        }
        free(fl);
    }
    CHECK(derived > 300 && caught == derived, "derived fields caught %zu of %zu", caught, derived);
    CHECK(declared > 50, "declared fields %zu", declared);
    CHECK(unsigned_caught == unsigned_tried && unsigned_tried > declared, "unsigned changes caught %zu of %zu",
          unsigned_caught, unsigned_tried);
    CHECK(skipped == 0, "no empty field");
    printf("every field: %zu derived (all caught), %zu declared (%zu caught by the replay "
           "alone); %zu of %zu changes caught unsigned\n",
           derived, declared, declared_caught, unsigned_caught, unsigned_tried);
    /* the untouched blobs still rebuild into a clean chain */
    {
        size_t el = 0;
        ztw_summary s;
        uint8_t *ex = rebuild(f.e.cap, f.hk, bl.b, bl.n, nos, 2, ZSC_FRAG_MAX, 0, &el);
        CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZXV_OK);
        free(ex);
    }
    free_blobs(&bl);
    fx_close(&f);
}

/* Every field of the journal export. */
static void test_journal_fields(void) {
    fx_t f;
    zind_rd r;
    size_t n_fields = 0, caught = 0;
    uint32_t na;
    uint64_t np;
    CHECK(fx_open(&f) == 0, "fixture");
    zind_rd_init(&r, f.jr, f.jl);
    r.pos = 4;
    na = zind_rd_u32(&r);
    /* walk the export, mutating each field in place */
#define TRY_FIELD(off, flip)                                                                   \
    do {                                                                                       \
        ztw_summary s_;                                                                        \
        int32_t st_;                                                                           \
        f.jr[(off)] ^= (flip);                                                                 \
        st_ = ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s_);                                 \
        n_fields++;                                                                            \
        caught += st_ == ZTW_E_DISCREPANCY;                                                    \
        CHECK(st_ == ZTW_E_DISCREPANCY, "journal byte %zu ^ %u: %s", (size_t) (off),          \
              (unsigned) (flip), ztw_status_name(st_));                                       \
        f.jr[(off)] ^= (flip);                                                                 \
    } while (0)
    for (uint32_t a = 0; a < na; a++) {
        size_t at = r.pos, l;
        TRY_FIELD(at, 1); /* id */
        r.pos += 4;
        l = f.jr[r.pos];
        TRY_FIELD(r.pos + l, 1); /* the name's last byte */
        r.pos += 1 + l;
        l = f.jr[r.pos];
        TRY_FIELD(r.pos + l, 1); /* the currency */
        r.pos += 1 + l;
        TRY_FIELD(r.pos, 1); /* kind: holder <-> issuer */
        r.pos += 1;
    }
    np = zind_rd_u64(&r);
    CHECK(np > 40, "journal postings %llu", (unsigned long long) np);
    for (uint64_t i = 0; i < np; i++) {
        size_t l;
        TRY_FIELD(r.pos, 1); /* id */
        r.pos += 8;
        TRY_FIELD(r.pos, 1); /* from */
        r.pos += 4;
        TRY_FIELD(r.pos, 1); /* to */
        r.pos += 4;
        TRY_FIELD(r.pos, 1); /* amount */
        r.pos += 8;
        l = f.jr[r.pos];
        TRY_FIELD(r.pos + l, 2); /* the reference's last byte */
        r.pos += 1 + l;
    }
#undef TRY_FIELD
    CHECK(r.pos == f.jl, "walked the whole journal");
    CHECK(caught == n_fields && n_fields > 200, "journal fields caught %zu of %zu", caught,
          n_fields);
    printf("journal: %zu fields changed, %zu caught\n", n_fields, caught);
    fx_close(&f);
}

/* Byte flips across the export, and specific record damage. */
static void test_bytes_and_records(void) {
    fx_t f;
    blobs_t bl;
    ztw_summary s;
    size_t flips = 0, caught = 0;
    uint32_t nos[3] = {1, 2, 3};
    CHECK(fx_open(&f) == 0, "fixture");
    for (size_t i = 0; i < f.cl; i += 127) {
        f.chain[i] ^= 0x10;
        flips++;
        caught += ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s) == ZTW_E_DISCREPANCY;
        f.chain[i] ^= 0x10;
    }
    CHECK(caught == flips && flips > 140, "byte flips caught %zu of %zu", caught, flips);
    CHECK(get_blobs(f.chain, f.cl, &bl) == 0, "blobs");
    /* small fragments: the witness does not care about fragment sizes */
    {
        size_t el = 0;
        uint8_t *ex = rebuild(f.e.cap, f.hk, bl.b, bl.n, nos, 2, 500, 0, &el);
        uint32_t cnt = 0;
        CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZXV_OK);
        CHECK(s.cycles == 2 && s.records > 30, "many fragments %u", s.records);
        CHECK_RC(ztw_cycle_count(ex, el, &cnt), ZXV_OK);
        CHECK(cnt == 2, "two cycles");
        free(ex);
        /* a finding names the 500-byte fragment that holds the field */
        {
            size_t nf = 0, at = 0;
            ztw_field *fl;
            uint32_t want = 0, n = 0;
            int seen = 0;
            (void) ztw_blob_fields(bl.b[1], bl.n[1], NULL, 0, &nf);
            fl = (ztw_field *) calloc(nf, sizeof *fl);
            CHECK_RC(ztw_blob_fields(bl.b[1], bl.n[1], fl, nf, &nf), ZXV_OK);
            for (size_t i = 0; i < nf; i++)
                if (strcmp(fl[i].name, "post[3].amount") == 0) at = fl[i].offset;
            CHECK(at > 0, "post[3].amount");
            bl.b[1][at] ^= 2;
            ex = rebuild(f.e.cap, f.hk, bl.b, bl.n, nos, 2, 500, 0, &el);
            bl.b[1][at] ^= 2;
            want = (uint32_t) ((bl.n[0] + 499) / 500 + at / 500);
            CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
            CHECK_RC(ztw_finding_count(f.w, &n), ZXV_OK);
            for (uint32_t i = 0; i < n; i++) {
                ztw_finding x;
                zind_record r;
                zind_chain *ch = NULL;
                (void) ztw_finding_at(f.w, i, &x);
                if (strcmp(x.field, "post[3].amount") != 0) continue;
                seen = 1;
                CHECK(x.record == want, "the field is in record %u (found %u)", want, x.record);
                CHECK(x.raw_len == 8 && memcmp(x.raw, bl.b[1] + at, 8) != 0, "its changed bytes");
                CHECK(x.cycle_no == 2, "cycle 2");
                CHECK_RC(zind_chain_import("sutra-clear", ex, el, &ch), ZXV_OK);
                CHECK_RC(zind_chain_at(ch, want, &r), ZXV_OK);
                CHECK(memcmp(r.hash, x.subject, 32) == 0, "subject: that record's hash");
                zind_chain_destroy(ch);
            }
            CHECK(seen, "a finding on post[3].amount");
            free(ex);
            free(fl);
        }
    }
    /* a record of another kind */
    {
        size_t el = 0;
        uint8_t *ex = rebuild(f.e.cap, f.hk, bl.b, bl.n, nos, 2, ZSC_FRAG_MAX, 9, &el);
        CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_KIND) == 1 && s.findings == 1, "KIND (%u findings)", s.findings);
        free(ex);
    }
    /* cycle 1 replayed as cycle 3 */
    {
        uint8_t *b3[3] = {bl.b[0], bl.b[1], bl.b[0]};
        size_t n3[3] = {bl.n[0], bl.n[1], bl.n[0]};
        size_t el = 0;
        uint8_t *ex = rebuild(f.e.cap, f.hk, b3, n3, nos, 3, ZSC_FRAG_MAX, 0, &el);
        CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_SEQUENCE) >= 1, "a replayed cycle is out of sequence");
        CHECK(n_code(f.w, ZTW_F_REPLAY) >= 10, "and every id in it is a replay (%u)",
              n_code(f.w, ZTW_F_REPLAY));
        CHECK(n_code(f.w, ZTW_F_JOURNAL) >= 1, "and its entries are claimed twice");
        free(ex);
    }
    /* the cycles swapped */
    {
        uint8_t *b2[2] = {bl.b[1], bl.b[0]};
        size_t n2[2] = {bl.n[1], bl.n[0]}, el = 0;
        uint32_t no2[2] = {2, 1};
        uint8_t *ex = rebuild(f.e.cap, f.hk, b2, n2, no2, 2, ZSC_FRAG_MAX, 0, &el);
        CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_SEQUENCE) >= 2, "swapped cycles");
        free(ex);
    }
    /* cycle 1 dropped: the carry and the house postings it covered */
    {
        size_t el = 0;
        uint8_t *ex = rebuild(f.e.cap, f.hk, bl.b + 1, bl.n + 1, nos + 1, 1, ZSC_FRAG_MAX, 0, &el);
        CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_SEQUENCE) >= 1, "cycle 2 follows nothing");
        CHECK(n_code(f.w, ZTW_F_JOURNAL) >= 10, "cycle 1's postings are uncovered (%u)",
              n_code(f.w, ZTW_F_JOURNAL));
        CHECK(n_code(f.w, ZTW_F_CARRY) >= 1, "carries from cycle 1 are missing");
        free(ex);
    }
    /* fragments dropped, swapped, cut */
    {
        size_t el = 0;
        uint8_t *ex = rebuild(f.e.cap, f.hk, bl.b, bl.n, nos, 2, 3000, 0, &el), *ex2;
        zind_chain *ch = NULL, *ch2 = NULL;
        uint32_t nrec = 0;
        CHECK_RC(zind_chain_import("sutra-clear", ex, el, &ch), ZXV_OK);
        CHECK_RC(zind_chain_count(ch, &nrec), ZXV_OK);
        CHECK(nrec >= 6, "fragments %u", nrec);
        for (uint32_t drop = 0; drop < nrec; drop++) {
            size_t l2 = 0;
            CHECK_RC(zind_chain_create("sutra-clear", 64, &ch2), ZXV_OK);
            for (uint32_t i = 0; i < nrec; i++) {
                zind_record r;
                if (i == drop) continue;
                (void) zind_chain_at(ch, i, &r);
                CHECK_RC(zind_chain_append(f.e.cap, ch2, f.hk, r.kind, r.payload, r.payload_len,
                                           NULL, NULL),
                         ZXV_OK);
            }
            (void) zind_chain_export(ch2, NULL, 0, &l2);
            ex2 = (uint8_t *) malloc(l2);
            CHECK_RC(zind_chain_export(ch2, ex2, l2, &l2), ZXV_OK);
            CHECK_RC(ztw_verify(f.w, ex2, l2, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
            CHECK(n_code(f.w, ZTW_F_FRAGMENT) >= 1, "fragment %u dropped", drop);
            CHECK(n_code(f.w, ZTW_F_LINK) == 0 && n_code(f.w, ZTW_F_SIGNATURE) == 0,
                  "the re-signed records verify");
            free(ex2);
            zind_chain_destroy(ch2);
        }
        /* two fragments swapped */
        CHECK_RC(zind_chain_create("sutra-clear", 64, &ch2), ZXV_OK);
        for (uint32_t i = 0; i < nrec; i++) {
            zind_record r;
            (void) zind_chain_at(ch, i == 0 ? 1 : i == 1 ? 0 : i, &r);
            CHECK_RC(zind_chain_append(f.e.cap, ch2, f.hk, r.kind, r.payload, r.payload_len, NULL,
                                       NULL),
                     ZXV_OK);
        }
        {
            size_t l2 = 0;
            (void) zind_chain_export(ch2, NULL, 0, &l2);
            ex2 = (uint8_t *) malloc(l2);
            CHECK_RC(zind_chain_export(ch2, ex2, l2, &l2), ZXV_OK);
            CHECK_RC(ztw_verify(f.w, ex2, l2, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
            CHECK(n_code(f.w, ZTW_F_FRAGMENT) >= 1, "swapped fragments");
            free(ex2);
        }
        zind_chain_destroy(ch2);
        zind_chain_destroy(ch);
        free(ex);
    }
    /* records signed by another key: untrusted, then trusted but not the
     * house's own key id */
    {
        size_t el = 0;
        uint8_t *ex = rebuild(f.e.cap, f.rogue, bl.b, bl.n, nos, 2, ZSC_FRAG_MAX, 0, &el);
        CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_UNTRUSTED) == s.records, "every record untrusted");
        CHECK_RC(ztw_trust_house(f.w, zind_signer_pk(f.rogue)), ZXV_OK);
        CHECK_RC(ztw_verify(f.w, ex, el, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
        CHECK(n_code(f.w, ZTW_F_UNTRUSTED) == 0 && n_code(f.w, ZTW_F_SEQUENCE) == s.records,
              "the record names the house key, another key signed (%u)",
              n_code(f.w, ZTW_F_SEQUENCE));
        free(ex);
    }
    free_blobs(&bl);
    fx_close(&f);
}

/* Operator keys: untrusted at the witness; a second operator the house
 * registered but the witness does not know. */
static void test_operators(void) {
    fx_t f;
    ztw_summary s;
    memset(&f, 0, sizeof f);
    CHECK(fx_house(&f, 0) == 0 && fx_scenario(&f, f.op) == 0 && fx_witness(&f, 1, 0) == 0,
          "fixture");
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
    CHECK(n_code(f.w, ZTW_F_DECISION) == 2 && s.findings == 2, "both decisions unknown (%u)",
          s.findings);
    CHECK_RC(ztw_trust_operator(f.w, zind_signer_pk(f.op)), ZXV_OK);
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZXV_OK);
    fx_close(&f);
    CHECK(fx_house(&f, 1) == 0 && fx_scenario(&f, f.rogue) == 0 && fx_witness(&f, 1, 1) == 0,
          "fixture with a second operator");
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
    CHECK(n_code(f.w, ZTW_F_DECISION) == 2, "decisions by a key the witness does not trust");
    {
        ztw_finding x;
        CHECK_RC(ztw_finding_at(f.w, 0, &x), ZXV_OK);
        CHECK(strstr(x.field, ".operator") != NULL && x.raw_len == 32, "names the operator field");
    }
    fx_close(&f);
}

/* A second writer on the ledger, and the report's records. */
static void test_second_writer_and_report(void) {
    fx_t f;
    ztw_summary s;
    uint32_t a = 0, b = 0, n = 0, nf = 0;
    const uint8_t *tr[1];
    CHECK(fx_open(&f) == 0, "fixture");
    /* someone posts with a house-style reference between closes: not in any
     * cycle record, but inside the range the next cycle's head covers */
    CHECK_RC(zsc_account(f.h, ZSC_ACCT_PARTICIPANT, k_code[0], "NGN", &a), ZXV_OK);
    CHECK_RC(zsc_account(f.h, ZSC_ACCT_PARTICIPANT, k_code[1], "NGN", &b), ZXV_OK);
    CHECK_RC(zxv_ledger_post_cap(f.e.cap, a, b, 100, "NGN", "SCLR/9/NGN/0/S", NULL), ZXV_OK);
    CHECK_RC(zsc_cycle_open(f.h, "C3", "2026-10-13T09:00:00Z"), ZXV_OK);
    CHECK(sub(&f, "G01", 0, 5000, 0, 1, ZXV_OK) == 0, "submit");
    CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
    CHECK(do_export(&f) == 0, "export");
    CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZTW_E_DISCREPANCY);
    CHECK(s.findings == 1 && n_code(f.w, ZTW_F_JOURNAL) == 1, "one foreign house posting (%u)",
          s.findings);
    CHECK(s.cycles == 3, "three cycles");
    /* the report: one Forbidden record per finding, then the summary */
    CHECK_RC(ztw_finding_count(f.w, &nf), ZXV_OK);
    CHECK_RC(zind_chain_count(ztw_report(f.w), &n), ZXV_OK);
    CHECK(n == nf + 1, "report records %u for %u findings", n, nf);
    tr[0] = zind_signer_pk(f.wk);
    CHECK_RC(zind_chain_verify(f.wcap, ztw_report(f.w), tr, 1, NULL), ZXV_OK);
    tr[0] = zind_signer_pk(f.hk);
    CHECK_RC(zind_chain_verify(f.wcap, ztw_report(f.w), tr, 1, NULL), ZIND_E_UNTRUSTED);
    for (uint32_t i = 0; i < nf; i++) {
        zind_record r;
        zind_rd rd;
        ztw_finding x;
        char field[64], detail[256];
        CHECK_RC(zind_chain_at(ztw_report(f.w), i, &r), ZXV_OK);
        CHECK_RC(ztw_finding_at(f.w, i, &x), ZXV_OK);
        CHECK(r.kind == ZTW_REC_FINDING, "finding record");
        zind_rd_init(&rd, r.payload, r.payload_len);
        CHECK(zind_rd_u16(&rd) == x.code, "code");
        CHECK(zind_rd_u32(&rd) == x.cycle_no, "cycle");
        CHECK(zind_rd_u32(&rd) == x.record, "record");
        CHECK(zind_rd_u32(&rd) == x.item, "item");
        zind_rd_str(&rd, field, sizeof field);
        CHECK(strcmp(field, x.field) == 0, "field");
        CHECK((int64_t) zind_rd_u64(&rd) == x.expected, "expected");
        CHECK((int64_t) zind_rd_u64(&rd) == x.found, "found");
        zind_rd_str(&rd, detail, sizeof detail);
        CHECK(strcmp(detail, x.detail) == 0 && strstr(detail, "SCLR/9/NGN/0/S") != NULL,
              "detail names the reference: %s", detail);
        CHECK(zind_rd_u8(&rd) == x.raw_len && x.raw_len == 14, "raw");
        rd.pos += x.raw_len;
        CHECK(zind_rd_u8(&rd) == x.tag, "tag");
        rd.pos += 32;
        CHECK(!rd.bad && rd.pos == r.payload_len, "the whole payload");
        CHECK(memcmp(x.subject, s.journal_digest, 32) == 0, "subject: the journal digest");
    }
    fx_close(&f);
}

/* A longer history with random traffic: the replay agrees every time. */
static void test_random_histories(void) {
    for (uint64_t seed = 1; seed <= 4; seed++) {
        fx_t f;
        ztw_summary s;
        uint64_t rs = seed * 0x9E3779B97F4A7C15ULL;
        uint32_t cycles = 3 + (uint32_t) (seed % 3), total = 0;
        memset(&f, 0, sizeof f);
        CHECK(fx_house(&f, 0) == 0, "house");
        for (uint32_t c = 0; c < cycles; c++) {
            char id[16];
            uint32_t n = 4 + (uint32_t) (zt_rand(&rs) % 20);
            snprintf(id, sizeof id, "R%u", c);
            CHECK_RC(zsc_cycle_open(f.h, id, "2026-10-12T10:00:00Z"), ZXV_OK);
            for (uint32_t i = 0; i < n; i++) {
                char e2e[36];
                uint64_t r = zt_rand(&rs);
                int d = (int) (r % 4), k = (int) ((r >> 8) % 3);
                if (k >= d) k++;
                snprintf(e2e, sizeof e2e, "S%u-%u-%u", (unsigned) seed, c, i);
                if (sub(&f, e2e, (int) ((r >> 16) % 3), 1 + (int64_t) ((r >> 20) % 500000), d, k,
                        ZXV_OK) == 0)
                    total++;
            }
            CHECK_RC(zsc_cycle_close(f.h, NULL), ZXV_OK);
        }
        CHECK(do_export(&f) == 0 && fx_witness(&f, 1, 1) == 0, "export");
        CHECK_RC(ztw_verify(f.w, f.chain, f.cl, f.jr, f.jl, &s), ZXV_OK);
        dump(f.w);
        CHECK(s.cycles == cycles && s.instructions == total, "seed %llu: %u cycles, %u instr",
              (unsigned long long) seed, s.cycles, s.instructions);
        fx_close(&f);
    }
}

int main(void) {
    test_caps();
    test_round_trip();
    test_inputs();
    test_every_field();
    test_journal_fields();
    test_bytes_and_records();
    test_operators();
    test_second_writer_and_report();
    test_random_histories();
    return zt_report("test_twit");
}
