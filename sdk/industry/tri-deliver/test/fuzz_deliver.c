/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_deliver.c — a deterministic operation fuzzer for tri-deliver.
 * Random parcels (Ed25519 and ML-DSA-87 recipients), dispatches, genuine
 * proofs, and hostile ones: replays of earlier proofs on any parcel, wrong
 * couriers, times outside the window, wrong keys, flipped bits; disputes,
 * arbiter decisions (genuine and forged), cancels and closes. Invariants
 * after every operation, against a model of each parcel:
 *   - escrow on the ledger == the desk's owed sum, money is conserved;
 *   - the payee is paid for a parcel at most once, and only through a
 *     verified proof or a genuine arbiter decision;
 *   - a disputed parcel moves only through a genuine arbiter decision;
 *   - the phase matches the state;
 * and the record chain verifies every 100 operations.
 *
 *   fuzz_deliver ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "ztdeliver.h"

#define MAXP 4096

typedef struct {
    char id[16];
    int ml;
    int64_t amount;
    int32_t state;
    uint64_t t0;
    char courier[8];
} model_t;

static model_t g_m[MAXP + 1];

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_deliver: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what,
            zt_last_err());
    abort();
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0xde11feedULL), s = seed;
    zt_env e;
    zind_signer *desk_key, *arbiter, *rogue, *rml;
    uint8_t ed_sk[32], ed_pk[32], rogue_sk[32];
    zxv_hexlog *qlog = NULL;
    ztd_desk *d = NULL;
    uint32_t issuer, sender, payee, n = 0;
    int64_t funded = 0, paid = 0;
    uint8_t last_sig[ZIND_SIG_BYTES];
    size_t last_len = 0;
    uint64_t last_t = 0;
    char last_courier[8] = "c0";
    unsigned long n_ok = 0, n_replay = 0, n_forged = 0, n_disputed = 0, n_arb = 0;

    if (zt_env_open(&e, "EUR") != 0) return 2;
    desk_key = zt_signer(e.cap, 1);
    arbiter = zt_signer(e.cap, 2);
    rogue = zt_signer(e.cap, 3);
    rml = zt_signer(e.cap, 4);
    zt_seed(ed_sk, 9);
    zt_seed(rogue_sk, 10);
    if (!desk_key || !arbiter || !rogue || !rml ||
        zxv_ed25519_public(e.cap, ed_sk, ed_pk) != ZXV_OK ||
        zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        ztd_desk_create(e.cap, "EUR", desk_key, qlog, &d) != ZXV_OK ||
        ztd_arbiter_add(d, zind_signer_pk(arbiter)) != ZXV_OK)
        return 2;
    issuer = zt_issuer(e.cap, "EUR");
    if (zxv_ledger_open_account_cap(e.cap, "sender", "EUR", ZXV_ACCT_HOLDER, &sender) != ZXV_OK ||
        zxv_ledger_open_account_cap(e.cap, "payee", "EUR", ZXV_ACCT_HOLDER, &payee) != ZXV_OK)
        return 2;

    for (unsigned long it = 0; it < iters; it++) {
        int op = (int) (zt_rand(&s) % 100);
        uint32_t p = n ? 1 + (uint32_t) (zt_rand(&s) % n) : 0;
        model_t *m = p ? &g_m[p] : NULL;
        int32_t st;
        if ((op < 15 || n == 0) && n < MAXP) {
            model_t nm;
            uint32_t id = 0;
            memset(&nm, 0, sizeof nm);
            snprintf(nm.id, sizeof nm.id, "F%u", n + 1);
            nm.ml = (int) (zt_rand(&s) % 4 == 0);
            nm.amount = 1 + (int64_t) (zt_rand(&s) % 100000);
            if (zt_rand(&s) % 2) {
                char ref[32];
                snprintf(ref, sizeof ref, "fund-%lu", it);
                if (zt_fund(e.cap, "EUR", issuer, sender, nm.amount, ref) != 0) die("fund", it);
                funded += nm.amount;
            }
            st = ztd_parcel_create(d, nm.id, sender, payee, nm.amount,
                                   nm.ml ? ZTD_ALG_MLDSA87 : ZTD_ALG_ED25519,
                                   nm.ml ? zind_signer_pk(rml) : ed_pk,
                                   nm.ml ? ZIND_PK_BYTES : 32, 600, &id);
            if (st == ZXV_OK) {
                if (id != n + 1) die("parcel numbering", it);
                nm.state = ZTD_ST_ESCROW;
                g_m[++n] = nm;
            } else if (st != ZXV_E_FUNDS) {
                die("create", it);
            }
            continue;
        }
        if (!m) continue;
        if (op < 30) {
            uint64_t t = 1700000000ULL + it;
            snprintf(m->courier, sizeof m->courier, "c%u", (unsigned) (zt_rand(&s) % 3));
            st = ztd_dispatch(d, p, m->courier, t);
            if (m->state == ZTD_ST_ESCROW) {
                if (st != ZXV_OK) die("dispatch from escrow failed", it);
                m->state = ZTD_ST_OUT;
                m->t0 = t;
            } else if (st == ZXV_OK) {
                die("dispatch from a non-escrow state", it);
            }
        } else if (op < 60) {
            /* a proof: genuine, or one of several hostile variants */
            uint8_t msg[128], sig[ZIND_SIG_BYTES];
            size_t len = 0, sl = m->ml ? ZIND_SIG_BYTES : 64;
            int kind = (int) (zt_rand(&s) % 6);
            uint64_t t = m->t0 + zt_rand(&s) % 700; /* sometimes outside the 600 s window */
            const char *courier = m->courier[0] ? m->courier : "c0";
            char wrong[8];
            if (kind == 1 && last_len) {
                /* replay the last accepted proof on this parcel */
                st = ztd_handoff_submit(d, p, last_courier, last_t, last_sig, last_len);
                if (st == ZXV_OK) die("a replayed proof was accepted", it);
                n_replay++;
                goto check;
            }
            if (kind == 2) {
                snprintf(wrong, sizeof wrong, "x%u", (unsigned) (zt_rand(&s) % 3));
                courier = wrong;
            }
            if (ztd_handoff_message(m->id, courier, t, msg, sizeof msg, &len) != ZXV_OK)
                die("message", it);
            if (m->ml)
                st = zind_sign(e.cap, kind == 3 ? rogue : rml, msg, len, ZTD_HANDOFF_CTX, sig);
            else
                st = zxv_ed25519_sign(e.cap, kind == 3 ? rogue_sk : ed_sk, msg, len, sig);
            if (st != ZXV_OK) die("sign", it);
            if (kind == 4) {
                size_t at = zt_rand(&s) % sl;
                sig[at] ^= (uint8_t) (1u << (zt_rand(&s) % 8));
            }
            st = ztd_handoff_submit(d, p, courier, t, sig, sl);
            if (st == ZXV_OK) {
                if (m->state != ZTD_ST_OUT) die("proof accepted outside OUT", it);
                if (kind == 2 || kind == 3 || kind == 4) die("hostile proof accepted", it);
                if (t < m->t0 || t - m->t0 > 600) die("proof accepted outside the window", it);
                m->state = ZTD_ST_DELIVERED;
                paid += m->amount;
                memcpy(last_sig, sig, sl);
                last_len = sl;
                last_t = t;
                snprintf(last_courier, sizeof last_courier, "%s", courier);
                n_ok++;
            } else if (kind == 3 || kind == 4) {
                n_forged++;
            }
            if (m->state == ZTD_ST_DISPUTED && st != ZIND_E_HELD) die("disputed not held", it);
        } else if (op < 70) {
            st = ztd_dispute(d, p, (uint32_t) it);
            if (m->state == ZTD_ST_ESCROW || m->state == ZTD_ST_OUT) {
                if (st != ZXV_OK) die("dispute refused", it);
                m->state = ZTD_ST_DISPUTED;
                n_disputed++;
            } else if (st == ZXV_OK) {
                die("dispute accepted without escrow", it);
            }
        } else if (op < 85) {
            uint8_t msg[128], sig[ZIND_SIG_BYTES];
            size_t len = 0;
            int32_t outcome = (zt_rand(&s) % 2) ? ZTD_PAY_PAYEE : ZTD_REFUND_SENDER;
            int forged = (int) (zt_rand(&s) % 3 == 0);
            st = ztd_arbiter_message(d, p, outcome, msg, sizeof msg, &len);
            if (st != ZXV_OK) {
                if (m->state == ZTD_ST_DISPUTED) die("no arbiter message for a dispute", it);
                goto check;
            }
            if (zind_sign(e.cap, forged ? rogue : arbiter, msg, len, ZTD_ARBITER_CTX, sig) !=
                ZXV_OK)
                die("sign", it);
            st = ztd_arbiter_resolve(d, p, zind_signer_pk(arbiter), outcome, sig);
            if (forged) {
                if (st != ZXV_E_VERIFY) die("forged decision not refused", it);
            } else {
                if (st != ZXV_OK) die("genuine decision refused", it);
                m->state = outcome == ZTD_PAY_PAYEE ? ZTD_ST_DELIVERED : ZTD_ST_REFUNDED;
                if (outcome == ZTD_PAY_PAYEE) paid += m->amount;
                n_arb++;
            }
        } else if (op < 92) {
            st = ztd_cancel(d, p);
            if (m->state == ZTD_ST_ESCROW) {
                if (st != ZXV_OK) die("cancel refused", it);
                m->state = ZTD_ST_CANCELLED;
            } else if (st == ZXV_OK) {
                die("cancel accepted after dispatch", it);
            }
        } else {
            st = ztd_close(d, p);
            if (m->state == ZTD_ST_DELIVERED) {
                if (st != ZXV_OK) die("close refused", it);
                m->state = ZTD_ST_CLOSED;
            } else if (st == ZXV_OK) {
                die("close accepted early", it);
            }
        }
    check : {
        int32_t real = -1;
        uint8_t tag = 0, ph = 0, base = 0;
        int64_t owed = -1, total;
        static const uint8_t want_phase[8] = {0,
                                              ZXV_HEX_PHASE_POTENTIAL,
                                              ZXV_HEX_PHASE_EMERGING,
                                              ZXV_HEX_PHASE_MANIFEST,
                                              0xff,
                                              0xff,
                                              ZXV_HEX_PHASE_POTENTIAL,
                                              ZXV_HEX_PHASE_DISSOLVING};
        if (ztd_parcel_state(d, p, &real, &tag) != ZXV_OK || real != m->state)
            die("state differs from the model", it);
        zxv_hex_unpack(tag, &base, NULL, &ph);
        if (want_phase[real] != 0xff && ph != want_phase[real]) die("phase", it);
        if ((real == ZTD_ST_DISPUTED) != (base == ZXV_HEX_BASE_BOTH)) die("disputed iff BOTH", it);
        if (ztd_escrow_owed(d, &owed) != ZXV_OK ||
            owed != zt_equity(e.cap, ztd_escrow_account(d)))
            die("escrow != owed", it);
        if (zt_equity(e.cap, payee) != paid) die("payee paid something unexpected", it);
        total = zt_equity(e.cap, sender) + zt_equity(e.cap, payee) +
                zt_equity(e.cap, ztd_escrow_account(d));
        if (total != funded) die("money not conserved", it);
    }
        if (it % 100 == 99) {
            const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
            if (zind_chain_verify(e.cap, ztd_records(d), trust, 1, NULL) != ZXV_OK)
                die("chain", it);
            if (zxv_ledger_check_cap(e.cap) != ZXV_OK) die("ledger", it);
        }
    }
    if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    printf("fuzz_deliver: %lu ops, %u parcels (delivered %lu, replays refused %lu, forged "
           "refused %lu, disputed %lu, decided %lu), seed %llu, 0 invariant failures\n",
           iters, n, n_ok, n_replay, n_forged, n_disputed, n_arb, (unsigned long long) seed);
    ztd_desk_destroy(d);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(desk_key);
    zind_signer_destroy(arbiter);
    zind_signer_destroy(rogue);
    zind_signer_destroy(rml);
    zt_env_close(&e);
    return 0;
}
