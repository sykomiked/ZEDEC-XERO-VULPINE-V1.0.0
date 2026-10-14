/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_merchant.c — a deterministic operation fuzzer for v-merchant.
 * Two tills of one merchant post to one ledger. Random invoices (some
 * issued on both tills under one id), payments signed by three payers with
 * fresh and reused nonces, hostile proofs (forged, flipped, wrong amount,
 * out of time), the same proof accepted offline on both tills, batch
 * settlements, payers drained and topped up, voids, day closes, and wire
 * decoding of random and mutated bytes. After every operation, against a
 * model that charges each till's fee with zxv_fee_assure_carry and splits
 * it with zxv_fee_split on its own:
 *   - every payer, the merchant and all eight fee accounts hold exactly the
 *     model's balances, so a (payer, nonce) moves money at most once;
 *   - each till's day totals equal the model's;
 *   - a decoded proof re-encodes to the same bytes;
 * and the record chains, the quarantine log and the ledger verify every 100
 * operations. The last line prints both chain heads and a digest of every
 * balance (compare gcc and clang byte for byte).
 *
 *   fuzz_merchant ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zvmerch.h"

#define NT 2
#define NP 3
#define MAXI 8192
#define MAXQ 8192
#define T0 1760000000ULL

typedef struct {
    char id[24];
    int till;
    uint32_t handle;
    int64_t amount;
    uint64_t issued, expires;
    int32_t state;
} minv_t;

typedef struct {
    int till;
    uint32_t inv; /* index into g_inv */
    uint32_t payer;
    uint64_t nonce;
    int64_t amount;
    int queued;
} mq_t;

static minv_t g_inv[MAXI];
static mq_t g_q[MAXQ];
static uint64_t g_settled[MAXQ][2]; /* (payer index, nonce) settled anywhere */
static uint64_t g_seen[NT][MAXQ][2];

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_merchant: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what,
            zt_last_err());
    abort();
}

static int has(uint64_t (*set)[2], uint32_t n, uint64_t p, uint64_t nonce) {
    for (uint32_t i = 0; i < n; i++)
        if (set[i][0] == p && set[i][1] == nonce) return 1;
    return 0;
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x7e4c4a47ULL), s = seed, now = T0;
    zt_env e;
    zind_signer *key[NT];
    uint8_t sk[NP][32], pk[NP][32], rogue[32];
    zxv_hexlog *qlog = NULL;
    zvm_terminal *t[NT];
    uint32_t issuer, merchant, payer[NP], fee[NT][4], n_inv = 0, n_q = 0, n_settled = 0;
    uint32_t n_seen[NT] = {0, 0}, day[NT] = {0, 0};
    uint64_t nonce_ctr[NP] = {0, 0, 0}, carry[NT] = {0, 0};
    int64_t m_payer[NP], m_merchant = 0, m_fee[NT][4];
    zvm_totals m_day[NT];
    unsigned long n_ok = 0, n_bounce = 0, n_dup = 0, n_forged = 0, n_replay = 0, n_mis = 0,
                  n_decode_ok = 0, n_decode_bad = 0, n_eod = 0;

    memset(m_fee, 0, sizeof m_fee);
    memset(m_day, 0, sizeof m_day);
    if (zt_env_open(&e, "EUR") != 0) return 2;
    issuer = zt_issuer(e.cap, "EUR");
    if (zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        zxv_ledger_open_account_cap(e.cap, "merchant", "EUR", ZXV_ACCT_HOLDER, &merchant) != ZXV_OK)
        return 2;
    zt_seed(rogue, 77);
    for (int p = 0; p < NP; p++) {
        char nm[32];
        zt_seed(sk[p], (uint8_t) (30 + p));
        snprintf(nm, sizeof nm, "payer%d", p);
        if (zxv_ed25519_public(e.cap, sk[p], pk[p]) != ZXV_OK ||
            zxv_ledger_open_account_cap(e.cap, nm, "EUR", ZXV_ACCT_HOLDER, &payer[p]) != ZXV_OK ||
            zt_fund(e.cap, "EUR", issuer, payer[p], 500000, nm) != 0)
            return 2;
        m_payer[p] = 500000;
    }
    for (int k = 0; k < NT; k++) {
        key[k] = zt_signer(e.cap, (uint8_t) (1 + k));
        if (!key[k] || zvm_terminal_create(e.cap, "SHOP-9", key[k], qlog, &t[k]) != ZXV_OK ||
            zvm_ccy_add(t[k], "EUR", merchant) != ZXV_OK ||
            zvm_fee_accounts(t[k], "EUR", fee[k]) != ZXV_OK)
            return 2;
        for (int p = 0; p < NP; p++)
            if (zvm_payer_add(t[k], payer[p], ZVM_ALG_ED25519, pk[p], 32, 0, NULL) != ZXV_OK)
                return 2;
    }

    for (unsigned long it = 0; it < iters; it++) {
        int op = (int) (zt_rand(&s) % 100);
        int k = (int) (zt_rand(&s) % NT);
        int32_t st;
        now += zt_rand(&s) % 30;
        if (op < 22 && n_inv + 2 < MAXI) {
            uint64_t span = zt_rand(&s) % 3 ? 5000 : 400000; /* sequenced draws */
            int64_t amt = 1 + (int64_t) (zt_rand(&s) % span);
            int both = zt_rand(&s) % 5 == 0;
            for (int c = 0; c <= both; c++) {
                minv_t *v = &g_inv[n_inv];
                snprintf(v->id, sizeof v->id, "INV-%u", both ? n_inv - c : n_inv);
                v->till = both ? c : k;
                v->amount = amt;
                v->issued = now;
                v->expires = now + 3600;
                v->state = ZVM_INV_OPEN;
                if (zvm_invoice_create(t[v->till], v->id, "EUR", amt, now, now + 3600,
                                       &v->handle) != ZXV_OK)
                    die("invoice", it);
                n_inv++;
            }
        } else if (op < 55 && n_inv && n_q < MAXQ) {
            uint32_t vi = n_inv > 24 && zt_rand(&s) % 4 ? n_inv - 1 - (uint32_t) (zt_rand(&s) % 24)
                                                       : (uint32_t) (zt_rand(&s) % n_inv);
            minv_t *v = &g_inv[vi];
            int p = (int) (zt_rand(&s) % NP), kind = (int) (zt_rand(&s) % 12);
            uint64_t nonce = zt_rand(&s) % 7 == 0 && nonce_ctr[p] ? zt_rand(&s) % nonce_ctr[p]
                                                                  : nonce_ctr[p]++;
            zvm_proof pr;
            uint8_t m[160];
            size_t len = 0;
            uint32_t id = 0;
            int seen = has(g_seen[v->till], n_seen[v->till], (uint64_t) p, nonce);
            memset(&pr, 0, sizeof pr);
            snprintf(pr.merchant, sizeof pr.merchant, "SHOP-9");
            snprintf(pr.invoice, sizeof pr.invoice, "%s", v->id);
            snprintf(pr.ccy, sizeof pr.ccy, "EUR");
            pr.amount = kind == 1 ? v->amount + 1 : v->amount;
            pr.payer = payer[p];
            pr.nonce = nonce;
            pr.time = kind == 2 ? now + 4000 : now;
            pr.alg = ZVM_ALG_ED25519;
            pr.sig_len = 64;
            if (zvm_pay_bytes(&pr, m, sizeof m, &len) != ZXV_OK ||
                zxv_ed25519_sign(e.cap, kind == 3 ? rogue : sk[p], m, len, pr.sig) != ZXV_OK)
                die("sign", it);
            if (kind == 4) {
                size_t at = (size_t) (zt_rand(&s) % 64);
                pr.sig[at] ^= (uint8_t) (1u << (zt_rand(&s) % 8));
            }
            st = zvm_accept(t[v->till], &pr, &id);
            if (v->state == ZVM_INV_ACCEPTED || v->state == ZVM_INV_SETTLED) {
                if (st != ZIND_E_REPLAY) die("paid invoice accepted again", it);
                n_replay++;
            } else if (v->state == ZVM_INV_VOID) {
                if (st != ZXV_E_STATE) die("void invoice", it);
            } else if (kind == 1) {
                if (st != ZVM_E_MISMATCH) die("wrong amount", it);
                n_mis++;
            } else if (pr.time < v->issued || pr.time > v->expires) {
                if (st != ZVM_E_EXPIRED) die("out of time", it);
            } else if (kind == 3 || kind == 4) {
                if (st != ZXV_E_VERIFY) die("forged proof", it);
                n_forged++;
            } else if (seen) {
                if (st != ZIND_E_REPLAY) die("reused nonce", it);
                n_replay++;
            } else {
                if (st != ZXV_OK) die("genuine proof refused", it);
                g_seen[v->till][n_seen[v->till]][0] = (uint64_t) p;
                g_seen[v->till][n_seen[v->till]++][1] = nonce;
                g_q[n_q].till = v->till;
                g_q[n_q].inv = vi;
                g_q[n_q].payer = (uint32_t) p;
                g_q[n_q].nonce = nonce;
                g_q[n_q].amount = v->amount;
                g_q[n_q++].queued = 1;
                v->state = ZVM_INV_ACCEPTED;
                /* the twin invoice on the other till takes the same proof offline */
                if (vi + 1 < n_inv && strcmp(g_inv[vi + 1].id, v->id) == 0 &&
                    g_inv[vi + 1].state == ZVM_INV_OPEN && zt_rand(&s) % 2 && n_q < MAXQ) {
                    minv_t *w = &g_inv[vi + 1];
                    int other = w->till;
                    if (!has(g_seen[other], n_seen[other], (uint64_t) p, nonce)) {
                        if (zvm_accept(t[other], &pr, &id) != ZXV_OK) die("twin accept", it);
                        g_seen[other][n_seen[other]][0] = (uint64_t) p;
                        g_seen[other][n_seen[other]++][1] = nonce;
                        g_q[n_q].till = other;
                        g_q[n_q].inv = vi + 1;
                        g_q[n_q].payer = (uint32_t) p;
                        g_q[n_q].nonce = nonce;
                        g_q[n_q].amount = w->amount;
                        g_q[n_q++].queued = 1;
                        w->state = ZVM_INV_ACCEPTED;
                    }
                }
            }
        } else if (op < 65) {
            zvm_batch b, mb = {0, 0, 0};
            for (uint32_t q = 0; q < n_q; q++) {
                mq_t *x = &g_q[q];
                if (!x->queued || x->till != k) continue;
                x->queued = 0;
                if (has(g_settled, n_settled, x->payer, x->nonce)) {
                    g_inv[x->inv].state = ZVM_INV_VOID;
                    m_day[k].duplicates++;
                    mb.duplicates++;
                } else if (m_payer[x->payer] < x->amount) {
                    g_inv[x->inv].state = ZVM_INV_OPEN;
                    m_day[k].bounced++;
                    mb.bounced++;
                } else {
                    uint64_t f = 0, parts[4];
                    zxv_fee_assure_carry(carry[k], (uint64_t) x->amount, &f, &carry[k]);
                    zxv_fee_split(f, parts);
                    m_payer[x->payer] -= x->amount;
                    m_merchant += x->amount - (int64_t) f;
                    for (int i = 0; i < 4; i++) {
                        m_fee[k][i] += (int64_t) parts[i];
                        m_day[k].fee_part[i] += (int64_t) parts[i];
                    }
                    m_day[k].settled++;
                    m_day[k].gross += x->amount;
                    m_day[k].fee += (int64_t) f;
                    m_day[k].net += x->amount - (int64_t) f;
                    g_settled[n_settled][0] = x->payer;
                    g_settled[n_settled++][1] = x->nonce;
                    g_inv[x->inv].state = ZVM_INV_SETTLED;
                    mb.settled++;
                }
            }
            if (zvm_settle(t[k], &b) != ZXV_OK) die("settle", it);
            if (b.settled != mb.settled || b.bounced != mb.bounced || b.duplicates != mb.duplicates)
            { fprintf(stderr, "real %u/%u/%u model %u/%u/%u\n", b.settled, b.bounced, b.duplicates, mb.settled, mb.bounced, mb.duplicates); die("batch differs from the model", it); }
            n_ok += b.settled;
            n_bounce += b.bounced;
            n_dup += b.duplicates;
        } else if (op < 70) {
            int p = (int) (zt_rand(&s) % NP);
            int64_t amt = 1 + (int64_t) (zt_rand(&s) % 300000);
            if (zt_rand(&s) % 2 && m_payer[p] >= amt) {
                {
                    char ref[32];
                    snprintf(ref, sizeof ref, "drain-%lu", it);
                    if (zxv_ledger_post_cap(e.cap, payer[p], issuer, amt, "EUR", ref, NULL) !=
                        ZXV_OK)
                        die("drain", it);
                }
                m_payer[p] -= amt;
            } else {
                char ref[32];
                snprintf(ref, sizeof ref, "topup-%lu", it);
                if (zt_fund(e.cap, "EUR", issuer, payer[p], amt, ref) != 0) die("topup", it);
                m_payer[p] += amt;
            }
        } else if (op < 80) {
            /* decode random and mutated wire bytes */
            uint8_t buf[ZVM_PROOF_MAX + 4], again[ZVM_PROOF_MAX + 4];
            size_t len = 0, l2 = 0;
            zvm_proof pr, out;
            memset(&pr, 0, sizeof pr);
            snprintf(pr.merchant, sizeof pr.merchant, "SHOP-9");
            snprintf(pr.invoice, sizeof pr.invoice, "INV-%u", (unsigned) (zt_rand(&s) % 1000));
            snprintf(pr.ccy, sizeof pr.ccy, "EUR");
            pr.amount = 1 + (int64_t) (zt_rand(&s) % 100000);
            pr.payer = (uint32_t) zt_rand(&s);
            pr.nonce = zt_rand(&s);
            pr.time = zt_rand(&s);
            pr.alg = ZVM_ALG_ED25519;
            pr.sig_len = 64;
            for (int i = 0; i < 64; i++) pr.sig[i] = (uint8_t) zt_rand(&s);
            if (zvm_proof_encode(&pr, buf, sizeof buf, &len) != ZXV_OK) die("encode", it);
            if (zt_rand(&s) % 3 == 0) {
                len = (size_t) (zt_rand(&s) % sizeof buf);
                for (size_t i = 0; i < len; i++) buf[i] = (uint8_t) zt_rand(&s);
            } else {
                int flips = 1 + (int) (zt_rand(&s) % 4);
                for (int i = 0; i < flips; i++) {
                    size_t at = (size_t) (zt_rand(&s) % len);
                    buf[at] ^= (uint8_t) (1u << (zt_rand(&s) % 8));
                }
                if (zt_rand(&s) % 4 == 0) len = (size_t) (zt_rand(&s) % (len + 2));
            }
            st = zvm_proof_decode(buf, len, &out);
            if (st == ZXV_OK) {
                if (zvm_proof_encode(&out, again, sizeof again, &l2) != ZXV_OK || l2 != len ||
                    memcmp(again, buf, len) != 0)
                    die("decoded proof does not re-encode to its bytes", it);
                n_decode_ok++;
            } else {
                if (st != ZXV_E_FORMAT) die("decode status", it);
                n_decode_bad++;
            }
        } else if (op < 85) {
            int pending = 0;
            zvm_totals real;
            for (uint32_t q = 0; q < n_q; q++)
                if (g_q[q].queued && g_q[q].till == k) pending = 1;
            st = zvm_eod_close(t[k], day[k] + 1, NULL);
            if (pending) {
                if (st != ZVM_E_PENDING) die("closed with proofs queued", it);
            } else {
                if (st != ZXV_OK) die("close", it);
                day[k]++;
                memset(&m_day[k], 0, sizeof m_day[k]);
                n_eod++;
            }
            if (zvm_day_totals(t[k], "EUR", &real) != ZXV_OK) die("totals", it);
        } else if (n_inv) {
            uint32_t vi = (uint32_t) (zt_rand(&s) % n_inv);
            st = zvm_invoice_void(t[g_inv[vi].till], g_inv[vi].handle);
            if (g_inv[vi].state == ZVM_INV_OPEN) {
                if (st != ZXV_OK) die("void refused", it);
                g_inv[vi].state = ZVM_INV_VOID;
            } else if (st != ZXV_E_STATE) {
                die("void of a paid invoice", it);
            }
        }
        /* invariants */
        for (int p = 0; p < NP; p++)
            if (zt_equity(e.cap, payer[p]) != m_payer[p]) die("payer balance", it);
        if (zt_equity(e.cap, merchant) != m_merchant) die("merchant balance", it);
        for (int kk = 0; kk < NT; kk++) {
            zvm_totals real;
            for (int i = 0; i < 4; i++)
                if (zt_equity(e.cap, fee[kk][i]) != m_fee[kk][i]) die("fee account", it);
            if (zvm_day_totals(t[kk], "EUR", &real) != ZXV_OK)
                die("day totals", it);
            if (real.settled != m_day[kk].settled || real.bounced != m_day[kk].bounced ||
                real.duplicates != m_day[kk].duplicates || real.gross != m_day[kk].gross ||
                real.fee != m_day[kk].fee || real.net != m_day[kk].net)
                die("day totals differ from the model", it);
            for (int i = 0; i < 4; i++)
                if (real.fee_part[i] != m_day[kk].fee_part[i]) die("day fee lines", it);
        }
        if (it % 100 == 99) {
            for (int kk = 0; kk < NT; kk++) {
                const uint8_t *trust[1] = {zind_signer_pk(key[kk])};
                if (zind_chain_verify(e.cap, zvm_records(t[kk]), trust, 1, NULL) != ZXV_OK)
                    die("chain", it);
            }
            if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", it);
            if (zxv_ledger_check_cap(e.cap) != ZXV_OK) die("ledger", it);
        }
    }
    {
        uint8_t h0[32], h1[32], dig[32], acc[(NP + 1 + NT * 4) * 8];
        size_t k = 0;
        int64_t vals[NP + 1 + NT * 4];
        int nv = 0;
        for (int p = 0; p < NP; p++) vals[nv++] = m_payer[p];
        vals[nv++] = m_merchant;
        for (int kk = 0; kk < NT; kk++)
            for (int i = 0; i < 4; i++) vals[nv++] = m_fee[kk][i];
        for (int a = 0; a < nv; a++)
            for (int i = 0; i < 8; i++) acc[k++] = (uint8_t) ((uint64_t) vals[a] >> (8 * i));
        zxv_sha3_256(acc, k, dig);
        zind_chain_head(zvm_records(t[0]), h0);
        zind_chain_head(zvm_records(t[1]), h1);
        printf("fuzz_merchant: %lu ops, %u invoices, %u proofs (settled %lu, bounced %lu, "
               "duplicates %lu; forged refused %lu, replays refused %lu, mismatches %lu), %lu "
               "day closes, wire %lu ok / %lu refused, seed %llu, 0 invariant failures\n",
               iters, n_inv, n_q, n_ok, n_bounce, n_dup, n_forged, n_replay, n_mis, n_eod,
               n_decode_ok, n_decode_bad, (unsigned long long) seed);
        printf("fuzz_merchant: heads ");
        for (int i = 0; i < 8; i++) printf("%02x", h0[i]);
        printf("/");
        for (int i = 0; i < 8; i++) printf("%02x", h1[i]);
        printf(", balances ");
        for (int i = 0; i < 8; i++) printf("%02x", dig[i]);
        printf("\n");
    }
    for (int kk = 0; kk < NT; kk++) {
        zvm_terminal_destroy(t[kk]);
        zind_signer_destroy(key[kk]);
    }
    zxv_hexlog_destroy(qlog);
    zt_env_close(&e);
    return 0;
}
