/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_logix.c — a deterministic operation fuzzer for Logix-Clear. Random
 * bookings, lines, departures, rulings, releases, broker decisions (genuine
 * and forged) and closings run against a small rule table. After every
 * operation:
 *   - the lifecycle phase matches the state (booked Potential, in transit /
 *     held / refused Emerging, released Manifest, closed Dissolving);
 *   - a held shipment is BOTH and only a genuine broker decision moves it;
 *   - zlc_release succeeds only when zlc_evaluate said TRUE, and is HELD
 *     exactly when it said BOTH;
 *   - money is conserved: payer + every duty account == what was funded,
 *     and zxv_ledger_check passes;
 * and every 100 operations (and at the end) the record chain verifies.
 *
 *   fuzz_logix ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zlogix.h"

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_logix: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    abort();
}

static const char *k_juris[3] = {"CN", "US", "MX"};
static const char *k_hs[5] = {"84714100", "85423100", "03061700", "61091000", "93011000"};
static const char *k_origin[3] = {"CN", "VN", "BD"};

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x10915ULL), s = seed;
    zt_env e;
    zind_signer *desk_key, *broker, *rogue;
    zxv_hexlog *qlog = NULL;
    zlc_desk *d = NULL;
    uint32_t issuer, payer, duty[3], n_ship = 0;
    int64_t funded = 0;
    unsigned long n_release = 0, n_held = 0, n_refused = 0, n_broker = 0, n_forged = 0;
    if (zt_env_open(&e, "USD") != 0) return 2;
    desk_key = zt_signer(e.cap, 1);
    broker = zt_signer(e.cap, 2);
    rogue = zt_signer(e.cap, 3);
    if (!desk_key || !broker || !rogue || zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        zlc_desk_create(e.cap, "USD", desk_key, qlog, &d) != ZXV_OK)
        return 2;
    for (int i = 0; i < 3; i++)
        if (zlc_jurisdiction_add(d, k_juris[i], &duty[i]) != ZXV_OK) return 2;
    zlc_broker_add(d, zind_signer_pk(broker));
    {
        /* a rule table with real conflicts in it */
        zlc_rule rules[] = {
            {"CN", "8471", "*", ZLC_RULE_RELEASE, {0, 1}},
            {"CN", "8542", "*", ZLC_RULE_REFUSE, {0, 1}},
            {"CN", "", "*", ZLC_RULE_RELEASE, {0, 1}},
            {"US", "8471", "CN", ZLC_RULE_RELEASE, {1, 20}},
            {"US", "8471", "*", ZLC_RULE_RELEASE, {1, 40}},
            {"US", "8542", "*", ZLC_RULE_RELEASE, {1, 100}},
            {"US", "0306", "*", ZLC_RULE_INSPECT, {0, 1}},
            {"US", "6109", "*", ZLC_RULE_RELEASE, {4, 25}},
            {"US", "610910", "BD", ZLC_RULE_RELEASE, {1, 7}},
            {"US", "9301", "*", ZLC_RULE_REFUSE, {0, 1}},
            {"MX", "", "*", ZLC_RULE_RELEASE, {3, 100}},
        };
        for (size_t i = 0; i < sizeof rules / sizeof rules[0]; i++)
            if (zlc_rule_add(d, &rules[i]) != ZXV_OK) return 2;
    }
    issuer = zt_issuer(e.cap, "USD");
    if (zxv_ledger_open_account_cap(e.cap, "payer", "USD", ZXV_ACCT_HOLDER, &payer) != ZXV_OK)
        return 2;

    for (unsigned long it = 0; it < iters; it++) {
        int op = (int) (zt_rand(&s) % 100);
        uint32_t id = n_ship ? 1 + (uint32_t) (zt_rand(&s) % n_ship) : 0;
        int32_t st, before = 0, after = 0, lp = -1;
        uint8_t tag = 0;
        if (op < 7 || n_ship == 0) {
            char c[12];
            const char *route[3];
            size_t n = 1 + zt_rand(&s) % 3, start = zt_rand(&s) % 3;
            snprintf(c, sizeof c, "FUZU%06u", (unsigned) (zt_rand(&s) % 1000));
            c[10] = (char) ('0' + zlc_container_digit(c));
            c[11] = 0;
            for (size_t i = 0; i < n; i++) route[i] = k_juris[(start + i) % 3];
            st = zlc_book(d, c, route, n, payer, &id);
            if (st == ZXV_OK) n_ship = id;
            else if (st != ZXV_E_DUPLICATE) die("book failed", it);
            continue;
        }
        if (zlc_shipment_state(d, id, &before, NULL) != ZXV_OK) die("state", it);
        if (op < 35) {
            zind_qty v = {(int64_t) (zt_rand(&s) % 1000000) + 1, 100};
            if (zind_qty_make(v.num, v.den, &v) != ZXV_OK) die("qty", it);
            {
                const char *hs = k_hs[zt_rand(&s) % 5];
                st = zlc_line_add(d, id, hs, k_origin[zt_rand(&s) % 3], v);
            }
            if (st != ZXV_OK && st != ZXV_E_STATE && st != ZXV_E_CAPACITY) die("line_add", it);
            if (st == ZXV_E_STATE && before == ZLC_ST_BOOKED) die("line refused while booked", it);
        } else if (op < 45) {
            st = zlc_depart(d, id);
            if (st != ZXV_OK && st != ZXV_E_STATE) die("depart", it);
        } else if (op < 55) {
            const char *jur = k_juris[zt_rand(&s) % 3]; /* one draw per statement */
            st = zlc_ruling_record(d, id, jur, (zt_rand(&s) % 3) ? ZXV_LP_TRUE : ZXV_LP_FALSE);
            if (st != ZXV_OK && st != ZXV_E_STATE && st != ZLC_E_JURIS && st != ZXV_E_CAPACITY)
                die("ruling", it);
        } else if (op < 75) {
            int64_t top = (int64_t) (zt_rand(&s) % 200000);
            char ref[32];
            if (top > 0 && (zt_rand(&s) % 2)) {
                snprintf(ref, sizeof ref, "f%lu", it);
                if (zt_fund(e.cap, "USD", issuer, payer, top, ref) != 0) die("fund", it);
                funded += top;
            }
            if (zlc_evaluate(d, id, &lp, NULL, 0) != ZXV_OK) die("evaluate", it);
            zlc_set_rounding(d, (int32_t) (zt_rand(&s) % 6));
            st = zlc_release(d, id, NULL);
            if (st == ZXV_OK) {
                if (before != ZLC_ST_IN_TRANSIT || lp != ZXV_LP_TRUE)
                    die("released without a TRUE decision in transit", it);
                n_release++;
            } else if (st == ZIND_E_HELD) {
                if (before == ZLC_ST_IN_TRANSIT && lp != ZXV_LP_BOTH) die("held without BOTH", it);
                n_held++;
            } else if (st == ZLC_E_REFUSED) {
                if (before == ZLC_ST_IN_TRANSIT && lp != ZXV_LP_FALSE) die("refused w/o FALSE", it);
                n_refused++;
            } else if (st == ZLC_E_PENDING) {
                if (lp != ZXV_LP_NEITHER) die("pending without NEITHER", it);
            } else if (st != ZXV_E_STATE && st != ZIND_E_REPLAY && st != ZIND_E_INEXACT &&
                       st != ZXV_E_FUNDS && st != ZXV_E_RANGE) {
                die("release returned an unexpected status", it);
            }
            if (lp == ZXV_LP_BOTH && before == ZLC_ST_IN_TRANSIT && st != ZIND_E_HELD)
                die("BOTH not held", it);
        } else if (op < 85) {
            uint8_t msg[128], sig[ZIND_SIG_BYTES];
            size_t len = 0;
            int forged = (int) (zt_rand(&s) % 3 == 0);
            int32_t dec = (zt_rand(&s) % 2) ? ZXV_LP_TRUE : ZXV_LP_FALSE;
            st = zlc_broker_message(d, id, dec, msg, sizeof msg, &len);
            if (st == ZXV_E_STATE) {
                if (before == ZLC_ST_HELD) die("held but no broker message", it);
                continue;
            }
            if (st != ZXV_OK) die("broker message", it);
            if (zind_sign(e.cap, forged ? rogue : broker, msg, len, "logix-clear/broker", sig) !=
                ZXV_OK)
                die("sign", it);
            if (forged && (zt_rand(&s) % 2)) {
                /* a rogue signature presented with the broker's key */
                st = zlc_broker_resolve(d, id, zind_signer_pk(broker), dec, sig, NULL);
                if (st != ZXV_E_VERIFY) die("forged decision not refused", it);
                n_forged++;
            } else {
                st = zlc_broker_resolve(d, id, zind_signer_pk(forged ? rogue : broker), dec, sig,
                                        NULL);
                if (forged && st != ZLC_E_BROKER) die("unregistered broker accepted", it);
                if (!forged && st == ZXV_OK) n_broker++;
                if (!forged && st != ZXV_OK && st != ZIND_E_INEXACT && st != ZXV_E_FUNDS &&
                    st != ZXV_E_RANGE)
                    die("genuine decision failed", it);
            }
        } else {
            st = zlc_close(d, id);
            if (st != ZXV_OK && st != ZXV_E_STATE && st != ZIND_E_HELD) die("close", it);
        }
        /* invariants */
        if (zlc_shipment_state(d, id, &after, &tag) != ZXV_OK) die("state after", it);
        {
            uint8_t base = 0, ph = 0;
            static const uint8_t want_phase[7] = {0, ZXV_HEX_PHASE_POTENTIAL,
                                                  ZXV_HEX_PHASE_EMERGING, ZXV_HEX_PHASE_EMERGING,
                                                  ZXV_HEX_PHASE_EMERGING, ZXV_HEX_PHASE_MANIFEST,
                                                  ZXV_HEX_PHASE_DISSOLVING};
            if (after < 1 || after > 6) die("state out of range", it);
            zxv_hex_unpack(tag, &base, NULL, &ph);
            if (ph != want_phase[after]) die("phase does not match state", it);
            if ((after == ZLC_ST_HELD) != (base == ZXV_HEX_BASE_BOTH)) die("held iff BOTH", it);
            if (before == ZLC_ST_HELD && after == ZLC_ST_RELEASED && op < 75)
                die("held shipment released without a broker", it);
        }
        {
            int64_t total = zt_equity(e.cap, payer);
            for (int i = 0; i < 3; i++) total += zt_equity(e.cap, duty[i]);
            if (total != funded) die("money not conserved", it);
        }
        if (it % 100 == 99) {
            const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
            if (zxv_ledger_check_cap(e.cap) != ZXV_OK) die("ledger check", it);
            if (zind_chain_verify(e.cap, zlc_records(d), trust, 1, NULL) != ZXV_OK)
                die("record chain does not verify", it);
        }
    }
    {
        const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
        if (zind_chain_verify(e.cap, zlc_records(d), trust, 1, NULL) != ZXV_OK)
            die("final chain", iters);
        if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    }
    printf("fuzz_logix: %lu ops, %u shipments (released %lu, held %lu, refused %lu, broker %lu, "
           "forged refused %lu), seed %llu, 0 invariant failures\n",
           iters, n_ship, n_release, n_held, n_refused, n_broker, n_forged,
           (unsigned long long) seed);
    zlc_desk_destroy(d);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(desk_key);
    zind_signer_destroy(broker);
    zind_signer_destroy(rogue);
    zt_env_close(&e);
    return 0;
}
