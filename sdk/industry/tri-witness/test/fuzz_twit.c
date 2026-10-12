/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_twit.c — a deterministic fuzzer for Tri-Witness against a live
 * Sutra-Clear house. Random traffic (instructions, conflicts and operator
 * decisions, withdrawals, Sutra treaties) runs through the house; cycles
 * close at random. Every random draw is its own statement, so the stream
 * (and this program's output) is the same with every compiler.
 *
 *   - after every close, the exports replay with no finding, every journal
 *     posting is covered and the signed report verifies;
 *   - a random field of a random cycle record, changed and re-signed with
 *     the house key: a derived field is always a finding;
 *   - a random bit of the chain export flipped: always a finding;
 *   - a random bit of the journal export flipped: always a finding.
 *
 * A fresh house (a new context) starts every 8 closes, so each replay stays
 * short however long the run.
 *
 *   fuzz_twit ITERATIONS       ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zsclear.h"
#include "ztwit.h"

#define NP 5
#define NC 2

static const char *k_codes[NP] = {"NGBKNGLA", "GHBKGHAC", "SNBKSNDA", "KEBKKENA", "ZA01"};
static const char *k_ccy[NC] = {"NGN", "XOF"};

static void die(const char *what, unsigned long it, const ztw_witness *w) {
    uint32_t n = 0;
    ztw_finding f;
    fprintf(stderr, "fuzz_twit: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    if (w && ztw_finding_count(w, &n) == ZXV_OK)
        for (uint32_t i = 0; i < n && i < 10; i++)
            if (ztw_finding_at(w, i, &f) == ZXV_OK)
                fprintf(stderr, "  %s %s: %s\n", ztw_finding_name(f.code), f.field, f.detail);
    abort();
}

typedef struct {
    uint8_t *chain, *jr;
    size_t cl, jl;
} exp_t;

static int do_export(const zxv_cap *cap, const zsc_house *h, exp_t *x) {
    free(x->chain);
    free(x->jr);
    x->chain = x->jr = NULL;
    (void) zind_chain_export(zsc_records(h), NULL, 0, &x->cl);
    (void) zsc_journal_export(cap, NULL, 0, &x->jl);
    x->chain = (uint8_t *) malloc(x->cl);
    x->jr = (uint8_t *) malloc(x->jl);
    if (!x->chain || !x->jr) return -1;
    if (zind_chain_export(zsc_records(h), x->chain, x->cl, &x->cl) != ZXV_OK) return -1;
    return zsc_journal_export(cap, x->jr, x->jl, &x->jl) == ZXV_OK ? 0 : -1;
}

/* Re-sign every cycle record, record k replaced by blob/len. */
static uint8_t *rebuild(const zxv_cap *cap, const zind_signer *s, const exp_t *x, uint32_t k,
                        const uint8_t *blob, size_t len, size_t *out_len) {
    uint32_t n = 0;
    zind_chain *ch = NULL;
    uint8_t *buf = (uint8_t *) malloc(8 + ZSC_FRAG_MAX), *out = NULL, *b = NULL;
    int ok = buf && ztw_cycle_count(x->chain, x->cl, &n) == ZXV_OK &&
             zind_chain_create("sutra-clear", 1u << 16, &ch) == ZXV_OK;
    for (uint32_t c = 0; ok && c < n; c++) {
        size_t bl = 0, nf;
        const uint8_t *src;
        if (c == k) {
            src = blob;
            bl = len;
        } else {
            (void) ztw_cycle_blob(x->chain, x->cl, c, NULL, 0, &bl);
            free(b);
            b = (uint8_t *) malloc(bl);
            ok = b && ztw_cycle_blob(x->chain, x->cl, c, b, bl, &bl) == ZXV_OK;
            src = b;
        }
        nf = (bl + ZSC_FRAG_MAX - 1) / ZSC_FRAG_MAX;
        for (size_t i = 0; ok && i < nf; i++) {
            size_t off = i * ZSC_FRAG_MAX, l = bl - off > ZSC_FRAG_MAX ? ZSC_FRAG_MAX : bl - off;
            zind_wr w;
            zind_wr_init(&w, buf, 8 + ZSC_FRAG_MAX);
            zind_wr_u32(&w, c + 1);
            zind_wr_u16(&w, (uint16_t) i);
            zind_wr_u16(&w, (uint16_t) nf);
            zind_wr_bytes(&w, src + off, l);
            ok = zind_chain_append(cap, ch, s, ZSC_REC_CYCLE, buf, w.len, NULL, NULL) == ZXV_OK;
        }
    }
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
    free(b);
    return out;
}

/* A fresh house in a fresh context (one epoch of the run). */
static int house_open(zt_env *e, zind_signer **hk, zind_signer **op, zsc_house **h) {
    if (zt_env_open(e, NULL) != 0) return -1;
    *hk = zt_signer(e->cap, 51);
    *op = zt_signer(e->cap, 52);
    if (!*hk || !*op || zsc_house_create(e->cap, "FUZZ", *hk, NULL, h) != ZXV_OK) return -1;
    for (int c = 0; c < NC; c++)
        if (zsc_currency_add(*h, k_ccy[c]) != ZXV_OK) return -1;
    for (int p = 0; p < NP; p++)
        if (zsc_participant_add(*h, k_codes[p]) != ZXV_OK) return -1;
    if (zsc_operator_add(*h, zind_signer_pk(*op)) != ZXV_OK) return -1;
    for (int c = 0; c < NC; c++) {
        uint32_t issuer = zt_issuer(e->cap, k_ccy[c]);
        for (int p = 0; p < NP; p++) {
            uint32_t a = 0;
            char ref[48];
            if (zsc_account(*h, ZSC_ACCT_PARTICIPANT, k_codes[p], k_ccy[c], &a) != ZXV_OK) return -1;
            snprintf(ref, sizeof ref, "seed-%d-%d", c, p);
            if (zt_fund(e->cap, k_ccy[c], issuer, a, 1000000000000LL, ref) != 0) return -1;
        }
    }
    return 0;
}

static void house_close(zt_env *e, zind_signer *hk, zind_signer *op, zsc_house *h) {
    zsc_house_destroy(h);
    zind_signer_destroy(hk);
    zind_signer_destroy(op);
    zt_env_close(e);
}

#define EPOCH_CYCLES 8 /* a fresh house after this many closes: replays stay short */

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    unsigned long pool = 40 + iters / 20;
    uint64_t seed = zt_seed_from_env(0x7717e55ULL), s = seed;
    zt_env e;
    zxv_ctx *wctx = NULL;
    zxv_cap *wcap = NULL;
    zind_signer *house_key, *op, *wkey;
    zsc_house *h = NULL;
    ztw_witness *w = NULL;
    exp_t x = {NULL, NULL, 0, 0};
    uint32_t cycles = 0, epochs = 1, total_cycles = 0;
    unsigned long n_sub = 0, n_held = 0, n_decided = 0, n_wd = 0, n_treaty = 0, n_clean = 0,
                  n_derived = 0, n_declared = 0, n_declared_caught = 0, n_cflip = 0, n_jflip = 0,
                  by_code[24] = {0};
    int open = 0;
    if (house_open(&e, &house_key, &op, &h) != 0) return 2;
    /* the witness: its own context, a read-only cap */
    if (zxv_ctx_create(&wctx) != ZXV_OK ||
        zxv_cap_create(wctx, 2, NULL, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN, &wcap) != ZXV_OK)
        return 2;
    wkey = zt_signer(wcap, 53);
    if (!wkey || ztw_create(wcap, wkey, &w) != ZXV_OK ||
        ztw_trust_house(w, zind_signer_pk(house_key)) != ZXV_OK ||
        ztw_trust_operator(w, zind_signer_pk(op)) != ZXV_OK)
        return 2;

    for (unsigned long it = 0; it < iters; it++) {
        uint64_t r = zt_rand(&s) % 100;
        if (!open && cycles == EPOCH_CYCLES) {
            house_close(&e, house_key, op, h);
            if (house_open(&e, &house_key, &op, &h) != 0) die("a new house", it, NULL);
            free(x.chain);
            free(x.jr);
            memset(&x, 0, sizeof x);
            cycles = 0;
            epochs++;
        }
        if (!open) {
            char cid[24];
            snprintf(cid, sizeof cid, "FZ-%u", cycles + 1);
            if (zsc_cycle_open(h, cid, "2026-10-12T10:00:00Z") != ZXV_OK) die("open", it, NULL);
            open = 1;
        }
        if (r < 40) {
            /* an instruction; the small id pool makes conflicts and replays */
            zsc_instr in;
            int32_t st;
            int d, k;
            memset(&in, 0, sizeof in);
            r = zt_rand(&s) % pool;
            snprintf(in.e2e, sizeof in.e2e, "FZ-%llu", (unsigned long long) r);
            snprintf(in.msg_id, sizeof in.msg_id, "M-%llu", (unsigned long long) r);
            snprintf(in.cre_dt_tm, sizeof in.cre_dt_tm, "2026-10-12T10:00:00Z");
            r = zt_rand(&s) % NC;
            snprintf(in.ccy, sizeof in.ccy, "%s", k_ccy[r]);
            r = zt_rand(&s);
            in.amount = 1 + (int64_t) (r % 2000000);
            d = (int) (zt_rand(&s) % NP);
            k = (int) (zt_rand(&s) % (NP - 1));
            if (k >= d) k++;
            snprintf(in.dbtr_agt, sizeof in.dbtr_agt, "%s", k_codes[d]);
            snprintf(in.cdtr_agt, sizeof in.cdtr_agt, "%s", k_codes[k]);
            snprintf(in.dbtr_nm, sizeof in.dbtr_nm, "Payer %d", d);
            snprintf(in.dbtr_acct, sizeof in.dbtr_acct, "A-%d", d);
            snprintf(in.cdtr_nm, sizeof in.cdtr_nm, "Payee %d", k);
            snprintf(in.cdtr_acct, sizeof in.cdtr_acct, "A-%d", k);
            st = zsc_submit(h, &in);
            if (st == ZXV_OK) n_sub++;
            else if (st == ZIND_E_HELD) n_held++;
            else if (st != ZIND_E_REPLAY && st != ZXV_E_CAPACITY) die("submit", it, NULL);
        } else if (r < 50) {
            /* an operator decides a held group */
            uint32_t ng = 0, nv = 0, choice;
            char e2e[40];
            uint8_t msg[256], sig[ZIND_SIG_BYTES];
            size_t len = 0;
            (void) zsc_held_count(h, &ng);
            if (ng == 0) continue;
            r = zt_rand(&s) % ng;
            if (zsc_held_at(h, (uint32_t) r, e2e, sizeof e2e, &nv) != ZXV_OK) die("held_at", it, NULL);
            choice = (uint32_t) (zt_rand(&s) % (nv + 1));
            if (zsc_decision_message(h, e2e, choice, msg, sizeof msg, &len) != ZXV_OK ||
                zind_sign(e.cap, op, msg, len, "sutra-clear/operator", sig) != ZXV_OK ||
                zsc_resolve(h, zind_signer_pk(op), e2e, choice, sig) != ZXV_OK)
                die("decision", it, NULL);
            n_decided++;
        } else if (r < 55) {
            /* the house withdraws a pending instruction */
            uint32_t np = 0;
            zsc_instr in;
            (void) zsc_pending_count(h, &np);
            if (np == 0) continue;
            r = zt_rand(&s) % np;
            if (zsc_pending_at(h, (uint32_t) r, &in) != ZXV_OK || zsc_withdraw(h, in.e2e) != ZXV_OK)
                die("withdraw", it, NULL);
            n_wd++;
        } else if (r < 60) {
            /* a Sutra treaty between two participants */
            char t[256];
            zsc_batch b;
            int d = (int) (zt_rand(&s) % NP), k = 0;
            uint64_t a1, a2;
            int32_t st;
            k = (int) (zt_rand(&s) % (NP - 1));
            if (k >= d) k++;
            a1 = 1 + zt_rand(&s) % 900000;
            a2 = 1 + zt_rand(&s) % 900000;
            snprintf(t, sizeof t,
                     "TREATY T%lu %s NET\nPARTY %s 0 0 1/2\nPARTY %s 0 0 1/2\nOWE %s %s %llu\n"
                     "OWE %s %s %llu\nEND\n",
                     it, k_ccy[it % NC], k_codes[d], k_codes[k], k_codes[d], k_codes[k],
                     (unsigned long long) a1, k_codes[k], k_codes[d], (unsigned long long) a2);
            st = zsc_submit_sutra(h, t, &b);
            if (st != ZXV_OK) die("treaty", it, NULL);
            n_treaty++;
        } else if (r < 66) {
            /* close, export, replay: no finding */
            ztw_summary sum;
            int32_t st = zsc_cycle_close(h, NULL);
            const uint8_t *trust[1] = {zind_signer_pk(wkey)};
            uint32_t nrep = 0;
            if (st != ZXV_OK) die("close", it, NULL);
            open = 0;
            cycles++;
            total_cycles++;
            if (do_export(e.cap, h, &x) != 0) die("export", it, NULL);
            st = ztw_verify(w, x.chain, x.cl, x.jr, x.jl, &sum);
            if (st != ZXV_OK) die("a clean replay has findings", it, w);
            if (sum.cycles != cycles || sum.findings != 0 ||
                sum.covered_postings != sum.journal_postings)
                die("summary", it, w);
            if (zind_chain_count(ztw_report(w), &nrep) != ZXV_OK || nrep != 1 ||
                zind_chain_verify(wcap, ztw_report(w), trust, 1, NULL) != ZXV_OK)
                die("report", it, w);
            n_clean++;
        } else if (r < 80) {
            /* a field of a cycle record, changed and re-signed */
            uint32_t k;
            size_t bl = 0, nf = 0, el = 0;
            uint8_t *b, *ex;
            ztw_field *fl;
            ztw_summary sum;
            int32_t st;
            if (!x.chain || cycles == 0) continue;
            k = (uint32_t) (zt_rand(&s) % cycles);
            (void) ztw_cycle_blob(x.chain, x.cl, k, NULL, 0, &bl);
            b = (uint8_t *) malloc(bl);
            if (!b || ztw_cycle_blob(x.chain, x.cl, k, b, bl, &bl) != ZXV_OK) die("blob", it, NULL);
            (void) ztw_blob_fields(b, bl, NULL, 0, &nf);
            fl = (ztw_field *) calloc(nf + 1, sizeof *fl);
            if (!fl || ztw_blob_fields(b, bl, fl, nf, &nf) != ZXV_OK) die("fields", it, NULL);
            r = zt_rand(&s) % nf;
            {
                const ztw_field *fd = &fl[r];
                uint8_t *p = b + fd->offset;
                uint64_t bit = zt_rand(&s) % 8;
                if (fd->len == 0) {
                    free(fl);
                    free(b);
                    continue;
                }
                if (fd->type == ZTW_FT_STR) {
                    uint8_t *q = p + fd->len - 1;
                    if ((*q >= 'A' && *q < 'Z') || (*q >= 'a' && *q < 'z') || (*q >= '0' && *q < '9'))
                        (*q)++;
                    else if (*q == 'Z' || *q == 'z' || *q == '9')
                        (*q)--;
                    else
                        *q = 'X';
                } else {
                    p[(zt_rand(&s) % fd->len)] ^= (uint8_t) (1u << bit);
                }
                ex = rebuild(e.cap, house_key, &x, k, b, bl, &el);
                if (!ex) die("rebuild", it, NULL);
                st = ztw_verify(w, ex, el, x.jr, x.jl, &sum);
                if (fd->derived) {
                    if (st != ZTW_E_DISCREPANCY) {
                        char m[128];
                        snprintf(m, sizeof m, "changed derived field %s (cycle %u) was not found",
                                 fd->name, k + 1);
                        die(m, it, w);
                    }
                    n_derived++;
                } else {
                    if (st != ZXV_OK && st != ZTW_E_DISCREPANCY) die("declared field", it, w);
                    n_declared++;
                    n_declared_caught += st == ZTW_E_DISCREPANCY;
                }
                {
                    uint32_t n = 0;
                    ztw_finding f;
                    (void) ztw_finding_count(w, &n);
                    for (uint32_t i = 0; i < n; i++)
                        if (ztw_finding_at(w, i, &f) == ZXV_OK && f.code < 24) by_code[f.code]++;
                }
                free(ex);
            }
            free(fl);
            free(b);
        } else if (r < 91) {
            /* a bit of the chain export */
            ztw_summary sum;
            size_t at;
            uint64_t bit;
            if (!x.chain) continue;
            at = (size_t) (zt_rand(&s) % x.cl);
            bit = zt_rand(&s) % 8;
            x.chain[at] ^= (uint8_t) (1u << bit);
            if (ztw_verify(w, x.chain, x.cl, x.jr, x.jl, &sum) != ZTW_E_DISCREPANCY)
                die("a flipped chain bit was not found", it, w);
            x.chain[at] ^= (uint8_t) (1u << bit);
            n_cflip++;
        } else {
            /* a bit of the journal export */
            ztw_summary sum;
            size_t at;
            uint64_t bit;
            if (!x.jr) continue;
            at = (size_t) (zt_rand(&s) % x.jl);
            bit = zt_rand(&s) % 8;
            x.jr[at] ^= (uint8_t) (1u << bit);
            if (ztw_verify(w, x.chain, x.cl, x.jr, x.jl, &sum) != ZTW_E_DISCREPANCY) {
                char m[128];
                snprintf(m, sizeof m, "journal byte %zu of %zu, bit %u flipped, not found", at, x.jl,
                         (unsigned) bit);
                die(m, it, w);
            }
            x.jr[at] ^= (uint8_t) (1u << bit);
            n_jflip++;
        }
    }
    printf("fuzz_twit: %lu ops, %u houses, %u cycles replayed clean %lu times, instructions %lu, "
           "held %lu, "
           "decided %lu, withdrawn %lu, treaties %lu; changed fields: %lu derived (all found), "
           "%lu declared (%lu found by the replay); flipped bits found: chain %lu, journal %lu; "
           "findings:",
           iters, epochs, total_cycles, n_clean, n_sub, n_held, n_decided, n_wd, n_treaty, n_derived,
           n_declared, n_declared_caught, n_cflip, n_jflip);
    for (uint16_t c = 1; c < 24; c++)
        if (by_code[c]) printf(" %s %lu", ztw_finding_name(c), by_code[c]);
    printf(", seed %llu, 0 invariant failures\n", (unsigned long long) seed);
    free(x.chain);
    free(x.jr);
    ztw_destroy(w);
    zind_signer_destroy(wkey);
    zxv_cap_destroy(wcap);
    zxv_ctx_destroy(wctx);
    house_close(&e, house_key, op, h);
    return 0;
}
