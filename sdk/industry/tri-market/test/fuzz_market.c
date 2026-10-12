/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_market.c — a deterministic operation fuzzer for tri-market.
 * Random orders (Ed25519 and ML-DSA-87 buyers, three sellers), shipments,
 * genuine confirmations and hostile ones (replays across orders, late
 * dates, wrong keys, flipped bits), disputes, rulings (genuine, forged,
 * unequal), and timeouts early and late, on a moving clock. After every
 * operation, against a model that charges the fee with zxv_fee_assure_carry
 * and splits it with zxv_fee_split on its own:
 *   - escrow on the ledger == the desk's owed sum, and money is conserved;
 *   - every seller and every fee account holds exactly the model's amount,
 *     so an order pays out at most once and only as the library says;
 *   - a disputed order moves only through a genuine ruling;
 *   - disputed iff BOTH;
 * and the record chain verifies every 100 operations. The last line
 * prints the chain head and a digest of every balance (compare gcc and
 * clang byte for byte).
 *
 *   fuzz_market ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "ztmarket.h"

#define MAXO 4096
#define NSELL 3

typedef struct {
    int ml;
    int64_t price;
    int32_t state;
    uint64_t deadline;
    int seller;
} model_t;

static model_t g_m[MAXO + 1];

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_market: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what,
            zt_last_err());
    abort();
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x3a2c37ULL), s = seed, now = 1700000000ULL;
    zt_env e;
    zind_signer *desk_key, *arbiter, *rogue, *bml;
    uint8_t ed_sk[32], ed_pk[32], rogue_sk[32];
    zxv_hexlog *qlog = NULL;
    ztm_desk *d = NULL;
    uint32_t issuer, buyer, sellers[NSELL], fee[4], n = 0;
    int64_t funded = 0, model_seller[NSELL] = {0, 0, 0}, model_fee[4] = {0, 0, 0, 0},
            model_buyer;
    uint64_t carry[NSELL] = {0, 0, 0};
    uint8_t last_sig[64];
    uint32_t last_o = 0;
    uint64_t last_t = 0;
    unsigned long n_rel = 0, n_replay = 0, n_forged = 0, n_disp = 0, n_rule = 0, n_exp = 0,
                  n_late = 0;

    if (zt_env_open(&e, "EUR") != 0) return 2;
    desk_key = zt_signer(e.cap, 1);
    arbiter = zt_signer(e.cap, 2);
    rogue = zt_signer(e.cap, 3);
    bml = zt_signer(e.cap, 4);
    zt_seed(ed_sk, 9);
    zt_seed(rogue_sk, 10);
    if (!desk_key || !arbiter || !rogue || !bml ||
        zxv_ed25519_public(e.cap, ed_sk, ed_pk) != ZXV_OK ||
        zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        ztm_desk_create(e.cap, "EUR", desk_key, qlog, &d) != ZXV_OK ||
        ztm_arbiter_add(d, zind_signer_pk(arbiter)) != ZXV_OK || ztm_fee_accounts(d, fee) != ZXV_OK)
        return 2;
    issuer = zt_issuer(e.cap, "EUR");
    if (zxv_ledger_open_account_cap(e.cap, "buyer", "EUR", ZXV_ACCT_HOLDER, &buyer) != ZXV_OK)
        return 2;
    for (int i = 0; i < NSELL; i++) {
        char nm[32];
        snprintf(nm, sizeof nm, "seller%d", i);
        if (zxv_ledger_open_account_cap(e.cap, nm, "EUR", ZXV_ACCT_HOLDER, &sellers[i]) != ZXV_OK)
            return 2;
    }
    funded = 4000000000000LL;
    if (zt_fund(e.cap, "EUR", issuer, buyer, funded, "fund") != 0) return 2;
    model_buyer = funded;

    for (unsigned long it = 0; it < iters; it++) {
        int op = (int) (zt_rand(&s) % 100);
        uint32_t o = n ? 1 + (uint32_t) (zt_rand(&s) % n) : 0;
        model_t *m = o ? &g_m[o] : NULL;
        int32_t st;
        now += zt_rand(&s) % 600;
        if ((op < 20 || n == 0) && n < MAXO) {
            char id[24];
            uint32_t no = 0;
            model_t nm;
            memset(&nm, 0, sizeof nm);
            nm.ml = zt_rand(&s) % 8 == 0;
            {
                uint64_t span = zt_rand(&s) % 2 ? 2000 : 50000000; /* sequenced draws */
                nm.price = 1 + (int64_t) (zt_rand(&s) % span);
            }
            nm.seller = (int) (zt_rand(&s) % NSELL);
            nm.deadline = now + 1 + zt_rand(&s) % (3 * 86400);
            nm.state = ZTM_ST_ESCROW;
            snprintf(id, sizeof id, "ORD-%u", n + 1);
            st = ztm_order_create(d, id, buyer, sellers[nm.seller], nm.price,
                                  nm.ml ? ZTM_ALG_MLDSA87 : ZTM_ALG_ED25519,
                                  nm.ml ? zind_signer_pk(bml) : ed_pk, nm.ml ? ZIND_PK_BYTES : 32,
                                  nm.deadline, now, &no);
            if (st != ZXV_OK || no != n + 1) die("create", it);
            g_m[++n] = nm;
            model_buyer -= nm.price;
            o = no;
            m = &g_m[o];
            goto check;
        } else if (op < 28) {
            st = ztm_ship(d, o, "TRACK", now);
            if (m->state == ZTM_ST_ESCROW) {
                if (st != ZXV_OK) die("ship refused", it);
                m->state = ZTM_ST_SHIPPED;
            } else if (st == ZXV_OK) {
                die("ship accepted twice", it);
            }
        } else if (op < 55) {
            /* a confirmation: genuine, late, forged, flipped, or another order's */
            int kind = (int) (zt_rand(&s) % 10);
            uint64_t t = kind == 1 ? m->deadline + 1 + zt_rand(&s) % 100 : now;
            uint8_t msg[160], sig[ZIND_SIG_BYTES];
            size_t len = 0, sl = m->ml ? ZIND_SIG_BYTES : 64;
            int open = m->state == ZTM_ST_ESCROW || m->state == ZTM_ST_SHIPPED;
            if (kind == 9 && last_o && !g_m[last_o].ml) {
                st = ztm_confirm(d, o, last_t, last_sig, 64);
                if (o == last_o) {
                    if (st == ZXV_OK) die("replayed confirmation accepted", it);
                } else if (st == ZXV_OK) {
                    die("another order's confirmation accepted", it);
                }
                n_replay++;
                goto check;
            }
            if (ztm_confirm_message(d, o, t, msg, sizeof msg, &len) != ZXV_OK) die("msg", it);
            if (m->ml) {
                if (zind_sign(e.cap, kind == 2 ? rogue : bml, msg, len, ZTM_CONFIRM_CTX, sig) !=
                    ZXV_OK)
                    die("sign", it);
            } else if (zxv_ed25519_sign(e.cap, kind == 2 ? rogue_sk : ed_sk, msg, len, sig) !=
                       ZXV_OK) {
                die("sign", it);
            }
            if (kind == 3) {
                size_t at = (size_t) (zt_rand(&s) % sl);
                sig[at] ^= (uint8_t) (1u << (zt_rand(&s) % 8));
            }
            st = ztm_confirm(d, o, t, sig, sl);
            if (kind == 2 || kind == 3) {
                if (st == ZXV_OK) die("forged confirmation accepted", it);
                n_forged++;
            } else if (!open) {
                if (st == ZXV_OK) die("released twice", it);
                if (m->state == ZTM_ST_DISPUTED ? st != ZIND_E_HELD : st != ZIND_E_REPLAY)
                    die("closed order status", it);
                n_replay++;
            } else if (kind == 1) {
                if (st != ZTM_E_EXPIRED) die("late confirmation", it);
                n_late++;
            } else if (t > m->deadline) {
                if (st != ZTM_E_EXPIRED) die("confirmation after the deadline", it);
                n_late++;
            } else {
                uint64_t f = 0, parts[4];
                if (st != ZXV_OK) die("genuine confirmation refused", it);
                zxv_fee_assure_carry(carry[m->seller], (uint64_t) m->price, &f, &carry[m->seller]);
                zxv_fee_split(f, parts);
                model_seller[m->seller] += m->price - (int64_t) f;
                for (int k = 0; k < 4; k++) model_fee[k] += (int64_t) parts[k];
                m->state = ZTM_ST_RELEASED;
                n_rel++;
                if (!m->ml) {
                    memcpy(last_sig, sig, 64);
                    last_o = o;
                    last_t = t;
                }
            }
        } else if (op < 65) {
            st = ztm_dispute(d, o, 1 + (int32_t) (zt_rand(&s) % 2), (uint32_t) it);
            if (m->state == ZTM_ST_ESCROW || m->state == ZTM_ST_SHIPPED) {
                if (st != ZXV_OK) die("dispute refused", it);
                m->state = ZTM_ST_DISPUTED;
                n_disp++;
            } else if (st == ZXV_OK) {
                die("dispute accepted on a closed order", it);
            }
        } else if (op < 82) {
            /* a ruling */
            int64_t to_s = m->price == 0 ? 0 : (int64_t) (zt_rand(&s) % (uint64_t) (m->price + 1));
            int64_t to_b = m->price - to_s;
            int forged = zt_rand(&s) % 4 == 0, uneven = zt_rand(&s) % 6 == 0;
            uint8_t msg[160], sig[ZIND_SIG_BYTES];
            size_t len = 0;
            if (m->state != ZTM_ST_DISPUTED) {
                memset(sig, 0, sizeof sig);
                st = ztm_rule(d, o, zind_signer_pk(arbiter), to_s, to_b, sig);
                if (st == ZXV_OK) die("ruling on an order not disputed", it);
                goto check;
            }
            if (ztm_ruling_message(d, o, to_s, to_b, msg, sizeof msg, &len) != ZXV_OK)
                die("ruling msg", it);
            if (zind_sign(e.cap, forged ? rogue : arbiter, msg, len, ZTM_RULING_CTX, sig) != ZXV_OK)
                die("sign", it);
            st = ztm_rule(d, o, zind_signer_pk(arbiter), to_s, uneven ? to_b + 1 : to_b, sig);
            if (uneven) {
                if (st != ZTM_E_SPLIT) die("uneven ruling", it);
            } else if (forged) {
                if (st != ZXV_E_VERIFY) die("forged ruling", it);
                n_forged++;
            } else {
                uint64_t f = 0, parts[4];
                if (st != ZXV_OK) die("genuine ruling refused", it);
                if (to_s > 0) {
                    zxv_fee_assure_carry(carry[m->seller], (uint64_t) to_s, &f, &carry[m->seller]);
                    zxv_fee_split(f, parts);
                    model_seller[m->seller] += to_s - (int64_t) f;
                    for (int k = 0; k < 4; k++) model_fee[k] += (int64_t) parts[k];
                }
                model_buyer += to_b;
                m->state = ZTM_ST_RULED;
                n_rule++;
            }
        } else {
            uint64_t t = zt_rand(&s) % 2 ? now : m->deadline + (zt_rand(&s) % 3);
            st = ztm_expire(d, o, t);
            if (m->state == ZTM_ST_DISPUTED) {
                if (st != ZIND_E_HELD) die("expired a disputed order", it);
            } else if (m->state != ZTM_ST_ESCROW && m->state != ZTM_ST_SHIPPED) {
                if (st != ZIND_E_REPLAY) die("expired a closed order", it);
            } else if (t <= m->deadline) {
                if (st != ZTM_E_EARLY) die("expired early", it);
            } else {
                if (st != ZXV_OK) die("expiry refused", it);
                model_buyer += m->price;
                m->state = ZTM_ST_REFUNDED;
                n_exp++;
            }
        }
    check : {
        int32_t real = -1;
        uint8_t tag = 0, base = 0;
        int64_t owed = -1, total = 0;
        if (ztm_order_state(d, o, &real, &tag) != ZXV_OK || real != m->state)
            die("state differs from the model", it);
        zxv_hex_unpack(tag, &base, NULL, NULL);
        if ((real == ZTM_ST_DISPUTED) != (base == ZXV_HEX_BASE_BOTH)) die("disputed iff BOTH", it);
        if (ztm_escrow_owed(d, &owed) != ZXV_OK || owed != zt_equity(e.cap, ztm_escrow_account(d)))
            die("escrow != owed", it);
        if (zt_equity(e.cap, buyer) != model_buyer) die("buyer balance", it);
        for (int k = 0; k < NSELL; k++)
            if (zt_equity(e.cap, sellers[k]) != model_seller[k]) die("seller balance", it);
        for (int k = 0; k < 4; k++)
            if (zt_equity(e.cap, fee[k]) != model_fee[k]) die("fee bucket", it);
        total = zt_equity(e.cap, buyer) + owed;
        for (int k = 0; k < NSELL; k++) total += model_seller[k];
        for (int k = 0; k < 4; k++) total += model_fee[k];
        if (total != funded) die("money not conserved", it);
    }
        if (it % 100 == 99) {
            const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
            if (zind_chain_verify(e.cap, ztm_records(d), trust, 1, NULL) != ZXV_OK)
                die("chain", it);
            if (zxv_ledger_check_cap(e.cap) != ZXV_OK) die("ledger", it);
        }
    }
    if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    {
        uint8_t head[32], dig[32], acc[8 * 8];
        size_t k = 0;
        uint32_t accts[8] = {buyer, sellers[0], sellers[1], sellers[2], fee[0], fee[1], fee[2],
                             fee[3]};
        zind_chain_head(ztm_records(d), head);
        for (int a = 0; a < 8; a++) {
            int64_t b = zt_equity(e.cap, accts[a]);
            for (int i = 0; i < 8; i++) acc[k++] = (uint8_t) ((uint64_t) b >> (8 * i));
        }
        zxv_sha3_256(acc, sizeof acc, dig);
        printf("fuzz_market: %lu ops, %u orders (released %lu, ruled %lu, expired %lu, disputed "
               "%lu; replays refused %lu, forged refused %lu, late refused %lu), seed %llu, 0 "
               "invariant failures\n",
               iters, n, n_rel, n_rule, n_exp, n_disp, n_replay, n_forged, n_late,
               (unsigned long long) seed);
        printf("fuzz_market: fees %lld/%lld/%lld/%lld, head ", (long long) model_fee[0],
               (long long) model_fee[1], (long long) model_fee[2], (long long) model_fee[3]);
        for (int i = 0; i < 8; i++) printf("%02x", head[i]);
        printf(", balances ");
        for (int i = 0; i < 8; i++) printf("%02x", dig[i]);
        printf("\n");
    }
    ztm_desk_destroy(d);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(desk_key);
    zind_signer_destroy(arbiter);
    zind_signer_destroy(rogue);
    zind_signer_destroy(bml);
    zt_env_close(&e);
    return 0;
}
