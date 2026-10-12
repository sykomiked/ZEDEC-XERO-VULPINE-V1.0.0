/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_shelf.c — a deterministic operation fuzzer for v-shelf against an
 * independent model. Random receipts, sales, transfers, shrinkage and
 * counts (matching or not), approver decisions (genuine or forged), reused
 * references, fractional quantities for whole-unit items and quantities
 * with huge denominators. After every operation:
 *   - every balance equals the model exactly and is never negative;
 *   - a held position changes only through a genuine decision;
 *   - a reused reference is refused;
 *   - received - sold - shrunk + adjusted == total for every SKU;
 * and every 200 operations (and at the end) zvs_reconcile replays the
 * signed journal and agrees.
 *
 *   fuzz_shelf ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zvshelf.h"

#define NI 4
#define NL 3

static const char *k_sku[NI] = {"F-EACH-1", "F-EACH-2", "F-KG", "F-L"};
static zind_qty g_model[NI][NL];
static int g_held[NI][NL];
static zind_qty g_var[NI][NL];

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_shelf: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    abort();
}

static zind_qty rnd_qty(uint64_t *s, int each) {
    zind_qty q = ZIND_QTY_ZERO;
    int64_t n = 1 + (int64_t) (zt_rand(s) % 40), d = 1;
    if (!each || zt_rand(s) % 10 == 0) {
        switch (zt_rand(s) % 4) {
        case 0: d = 1 + (int64_t) (zt_rand(s) % 16); break;
        case 1: d = 1000; break;
        case 2: d = 1000000007LL; break; /* a prime: sums overflow quickly */
        default: d = 1; break;
        }
    }
    (void) zind_qty_make(n, d, &q);
    return q;
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x5e1fULL), s = seed;
    zt_env e;
    zind_signer *key, *mgr, *rogue;
    zxv_hexlog *qlog = NULL;
    zvs_store *st = NULL;
    unsigned long n_ok = 0, n_neg = 0, n_range = 0, n_var = 0, n_dec = 0, n_dup = 0, refn = 0;
    if (zt_env_open(&e, NULL) != 0) return 2;
    key = zt_signer(e.cap, 1);
    mgr = zt_signer(e.cap, 2);
    rogue = zt_signer(e.cap, 3);
    if (!key || !mgr || !rogue || zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        zvs_store_create(e.cap, key, qlog, &st) != ZXV_OK ||
        zvs_approver_add(st, zind_signer_pk(mgr)) != ZXV_OK)
        return 2;
    for (int l = 0; l < NL; l++) {
        char c[24];
        snprintf(c, sizeof c, "loc%d", l);
        if (zvs_location_add(st, c, NULL) != ZXV_OK) return 2;
    }
    for (int i = 0; i < NI; i++) {
        if (zvs_item_add(st, k_sku[i], i < 2 ? ZVS_UNIT_EACH : ZVS_UNIT_MEASURE, i < 2 ? "ea" : "kg") !=
            ZXV_OK)
            return 2;
        for (int l = 0; l < NL; l++) g_model[i][l] = ZIND_QTY_ZERO;
    }

    for (unsigned long it = 0; it < iters; it++) {
        int i = (int) (zt_rand(&s) % NI), op = (int) (zt_rand(&s) % 100);
        uint32_t l = (uint32_t) (zt_rand(&s) % NL), l2 = (l + 1 + (uint32_t) (zt_rand(&s) % 2)) % NL;
        int each = i < 2;
        zind_qty q = rnd_qty(&s, each), after = ZIND_QTY_ZERO, after2 = ZIND_QTY_ZERO;
        char ref[32];
        int32_t rc, want;
        int dup = (int) (zt_rand(&s) % 25 == 0 && refn > 0);
        if (dup) snprintf(ref, sizeof ref, "R%lu", (unsigned long) (zt_rand(&s) % refn));
        else snprintf(ref, sizeof ref, "R%lu", refn);
        if (op < 70) {
            int kind = op < 30 ? 0 : op < 50 ? 1 : op < 60 ? 2 : 3; /* rcv, sell, shrink, transfer */
            int32_t a = ZXV_OK, b = ZXV_OK;
            if (kind == 0) a = zind_qty_add(g_model[i][l], q, &after);
            else a = zind_qty_sub(g_model[i][l], q, &after);
            if (kind == 3) b = zind_qty_add(g_model[i][l2], q, &after2);
            if (dup) want = ZIND_E_REPLAY;
            else if (each && q.den != 1) want = ZVS_E_UNIT;
            else if (g_held[i][l] || (kind == 3 && g_held[i][l2])) want = ZIND_E_HELD;
            else if (a != ZXV_OK) want = a;
            else if (kind != 0 && zind_qty_sign(after) < 0) want = ZIND_E_NEGATIVE;
            else if (b != ZXV_OK) want = b;
            else want = ZXV_OK;
            if (kind == 0) rc = zvs_receive(st, ref, k_sku[i], l, q);
            else if (kind == 1) rc = zvs_sell(st, ref, k_sku[i], l, q);
            else if (kind == 2) rc = zvs_shrink(st, ref, k_sku[i], l, q, ZVS_SHRINK_OTHER);
            else rc = zvs_transfer(st, ref, k_sku[i], l, l2, q);
            /* the lifetime flow totals can overflow before a balance does */
            if (rc == ZXV_E_RANGE && want == ZXV_OK) want = ZXV_E_RANGE;
            if (rc != want) {
                fprintf(stderr, "op kind %d got %s want %s\n", kind, zvs_status_name(rc),
                        zvs_status_name(want));
                die("movement status differs from the model", it);
            }
            if (rc == ZXV_OK) {
                g_model[i][l] = after;
                if (kind == 3) g_model[i][l2] = after2;
                n_ok++;
            }
            n_neg += rc == ZIND_E_NEGATIVE;
            n_range += rc == ZXV_E_RANGE;
            n_dup += rc == ZIND_E_REPLAY;
            if (!dup && rc != ZXV_E_ARG) refn += (rc == ZXV_OK);
        } else if (op < 85) {
            /* a count: usually right, sometimes off */
            zind_qty counted = g_model[i][l], var = ZIND_QTY_ZERO, delta;
            if (zt_rand(&s) % 2) {
                delta = rnd_qty(&s, each);
                if (zt_rand(&s) % 2 && zind_qty_sub(counted, delta, &delta) == ZXV_OK &&
                    zind_qty_sign(delta) >= 0)
                    counted = delta;
                else if (zind_qty_add(counted, rnd_qty(&s, each), &delta) == ZXV_OK)
                    counted = delta;
            }
            rc = zvs_count(st, ref, k_sku[i], l, counted, &var);
            if (dup) {
                if (rc != ZIND_E_REPLAY) die("count with a reused reference", it);
            } else if (each && counted.den != 1) {
                if (rc != ZVS_E_UNIT) die("fractional count of whole units", it);
            } else if (g_held[i][l]) {
                if (rc != ZIND_E_HELD) die("count on a held position", it);
            } else if (rc == ZXV_OK) {
                int32_t c = 1;
                zind_qty_cmp(counted, g_model[i][l], &c);
                if (c != 0) die("a different count was accepted", it);
                refn++;
            } else if (rc == ZVS_E_VARIANCE) {
                zind_qty chk;
                if (zind_qty_sub(counted, g_model[i][l], &chk) != ZXV_OK || chk.num != var.num ||
                    chk.den != var.den || chk.num == 0)
                    die("variance is not counted - book", it);
                g_held[i][l] = 1;
                g_var[i][l] = var;
                n_var++;
                refn++;
            } else if (rc != ZXV_E_RANGE) {
                die("count failed", it);
            }
        } else {
            uint8_t msg[128], sig[ZIND_SIG_BYTES];
            size_t len = 0;
            int32_t dec = (zt_rand(&s) % 2) ? ZVS_ACCEPT_COUNT : ZVS_KEEP_BOOK;
            int forged = (int) (zt_rand(&s) % 4 == 0);
            rc = zvs_variance_message(st, k_sku[i], l, dec, msg, sizeof msg, &len);
            if (!g_held[i][l]) {
                if (rc != ZXV_E_STATE) die("message for a position not held", it);
                continue;
            }
            if (rc != ZXV_OK) die("variance message", it);
            if (zind_sign(e.cap, forged ? rogue : mgr, msg, len, ZVS_VARIANCE_CTX, sig) != ZXV_OK)
                die("sign", it);
            rc = zvs_variance_resolve(st, k_sku[i], l, zind_signer_pk(mgr), dec, sig);
            if (forged) {
                if (rc != ZXV_E_VERIFY) die("forged decision accepted", it);
            } else if (rc == ZXV_OK) {
                if (dec == ZVS_ACCEPT_COUNT &&
                    zind_qty_add(g_model[i][l], g_var[i][l], &g_model[i][l]) != ZXV_OK)
                    die("model add", it);
                g_held[i][l] = 0;
                n_dec++;
            } else if (rc != ZXV_E_RANGE) {
                die("genuine decision refused", it);
            }
        }
        /* invariants */
        for (int k = 0; k < NL; k++) {
            zind_qty h;
            int32_t held = -1, c = 1;
            if (zvs_on_hand(st, k_sku[i], (uint32_t) k, &h) != ZXV_OK) die("on_hand", it);
            if (zind_qty_cmp(h, g_model[i][k], &c) != ZXV_OK || c != 0) die("balance != model", it);
            if (zind_qty_sign(h) < 0) die("negative stock", it);
            zvs_position_state(st, k_sku[i], (uint32_t) k, &held, NULL);
            if (held != g_held[i][k]) die("hold differs from the model", it);
        }
        {
            zind_qty r, so, sh, ad, t, x;
            if (zvs_flows(st, k_sku[i], &r, &so, &sh, &ad) != ZXV_OK) die("flows", it);
            if (zvs_total(st, k_sku[i], &t) == ZXV_OK && zind_qty_sub(r, so, &x) == ZXV_OK &&
                zind_qty_sub(x, sh, &x) == ZXV_OK && zind_qty_add(x, ad, &x) == ZXV_OK &&
                (x.num != t.num || x.den != t.den))
                die("flows do not add up", it);
        }
        if (it % 200 == 199 && zvs_reconcile(st, NULL) != ZXV_OK) die("reconcile", it);
    }
    if (zvs_reconcile(st, NULL) != ZXV_OK) die("final reconcile", iters);
    if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    printf("fuzz_shelf: %lu ops (moved %lu, negative refused %lu, overflow refused %lu, "
           "reused refs refused %lu, variances %lu, decisions %lu), seed %llu, 0 invariant "
           "failures\n",
           iters, n_ok, n_neg, n_range, n_dup, n_var, n_dec, (unsigned long long) seed);
    zvs_store_destroy(st);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(key);
    zind_signer_destroy(mgr);
    zind_signer_destroy(rogue);
    zt_env_close(&e);
    return 0;
}
