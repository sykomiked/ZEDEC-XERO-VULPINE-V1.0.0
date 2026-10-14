/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_sclear.c — a deterministic operation fuzzer for Sutra-Clear.
 * Random instructions (through the API, through pacs.008 with random byte
 * damage, and through Sutra treaties) reuse a small pool of end-to-end ids
 * so conflicts and replays are common; operators decide held groups with
 * genuine and forged signatures; participants are funded and cycles closed
 * at random. Every random draw is its own statement, so the stream (and
 * this program's output) is the same with every compiler.
 *
 * After every operation:
 *   - a pacs.008 message that parses re-renders to the same instruction;
 *   - a submission returns one of the documented statuses;
 * after every close that succeeds:
 *   - net positions sum to zero per currency and every hub is back at zero;
 *   - money is conserved: participants + fee buckets == what was funded,
 *     per currency, and zxv_ledger_check passes;
 *   - every fee split sums to its fee, and every carry is below 10^7;
 * after a close that is refused (ZXV_E_FUNDS, ZXV_E_RANGE): nothing was
 * posted; pending instructions are then withdrawn until it closes;
 * every 10 closes and at the end: the record chain and quarantine log
 * verify.
 *
 *   fuzz_sclear ITERATIONS       ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zsclear.h"

#define NP 6
#define NC 3

static const char *k_codes[NP] = {"NGBKNGLA", "GHBKGHAC", "SNBKSNDA", "KEBKKENA", "ZA01", "EG02"};
static const char *k_ccy[NC] = {"NGN", "XOF", "KWD"};

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_sclear: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    abort();
}

static uint32_t entries_total(const zxv_cap *cap) {
    uint32_t na = 0, total = 0;
    (void) zxv_ledger_account_count_cap(cap, &na);
    for (uint32_t a = 0; a < na; a++) {
        uint32_t n = 0;
        (void) zxv_ledger_entry_count_cap(cap, a, &n);
        total += n;
    }
    return total;
}

static zsc_instr rand_instr(uint64_t *s, unsigned long pool) {
    zsc_instr in;
    uint64_t r;
    int d, k, c;
    memset(&in, 0, sizeof in);
    r = zt_rand(s) % pool;
    snprintf(in.e2e, sizeof in.e2e, "FZ-%llu", (unsigned long long) r);
    snprintf(in.msg_id, sizeof in.msg_id, "M-%llu", (unsigned long long) r);
    snprintf(in.cre_dt_tm, sizeof in.cre_dt_tm, "2026-10-12T10:00:00Z");
    c = (int) (zt_rand(s) % NC);
    snprintf(in.ccy, sizeof in.ccy, "%s", k_ccy[c]);
    d = (int) (zt_rand(s) % NP);
    k = (int) (zt_rand(s) % NP);
    if (k == d && (zt_rand(s) % 4) != 0) k = (d + 1) % NP; /* sometimes the same agent twice */
    snprintf(in.dbtr_agt, sizeof in.dbtr_agt, "%s", k_codes[d]);
    snprintf(in.cdtr_agt, sizeof in.cdtr_agt, "%s", k_codes[k]);
    r = zt_rand(s) % 100;
    if (r < 85) in.amount = 1 + (int64_t) (zt_rand(s) % 50000);
    else if (r < 95) in.amount = 1 + (int64_t) (zt_rand(s) % 50000000);
    else if (r < 98) in.amount = ZXV_AMOUNT_MAX - (int64_t) (zt_rand(s) % 3);
    else in.amount = (int64_t) (zt_rand(s) % 2) - 1; /* 0 or -1: refused */
    r = zt_rand(s) % 3;
    snprintf(in.dbtr_nm, sizeof in.dbtr_nm, "Sender %d %s", d, r ? "Ltd" : "& <Sons>");
    snprintf(in.dbtr_acct, sizeof in.dbtr_acct, "D%d-%llu", d, (unsigned long long) r);
    snprintf(in.cdtr_nm, sizeof in.cdtr_nm, "Receiver %d", k);
    snprintf(in.cdtr_acct, sizeof in.cdtr_acct, "C%d", k);
    return in;
}

static int same_instr(const zsc_instr *a, const zsc_instr *b) {
    uint8_t x[32], y[32];
    if (zsc_instr_digest(a, x) != ZXV_OK || zsc_instr_digest(b, y) != ZXV_OK) return 0;
    return memcmp(x, y, 32) == 0 && strcmp(a->msg_id, b->msg_id) == 0 &&
           strcmp(a->cre_dt_tm, b->cre_dt_tm) == 0;
}

static int submit_ok_status(int32_t st) {
    return st == ZXV_OK || st == ZIND_E_HELD || st == ZIND_E_REPLAY || st == ZXV_E_RANGE ||
           st == ZSC_E_PARTICIPANT || st == ZXV_E_CAPACITY;
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    unsigned long pool = iters / 3 + 16;
    uint64_t seed = zt_seed_from_env(0x5c1ea4ULL), s = seed;
    zt_env e;
    zind_signer *house_key, *op, *rogue;
    zxv_hexlog *qlog = NULL;
    zsc_house *h = NULL;
    uint32_t issuer[NC], acct[NC][NP], bucket[NC][4], hub[NC];
    int64_t funded[NC];
    unsigned long n_ok = 0, n_held = 0, n_replay = 0, n_refused = 0, n_pacs = 0, n_pacs_bad = 0,
                  n_treaty = 0, n_decided = 0, n_forged = 0, n_closed = 0, n_funds = 0,
                  n_withdrawn = 0, n_settled = 0;
    char xml[8192];
    if (zt_env_open(&e, NULL) != 0) return 2;
    house_key = zt_signer(e.cap, 31);
    op = zt_signer(e.cap, 32);
    rogue = zt_signer(e.cap, 33);
    if (!house_key || !op || !rogue || zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        zsc_house_create(e.cap, "FUZZ", house_key, qlog, &h) != ZXV_OK)
        return 2;
    for (int c = 0; c < NC; c++)
        if (zsc_currency_add(h, k_ccy[c]) != ZXV_OK) return 2;
    for (int p = 0; p < NP; p++)
        if (zsc_participant_add(h, k_codes[p]) != ZXV_OK) return 2;
    if (zsc_operator_add(h, zind_signer_pk(op)) != ZXV_OK) return 2;
    for (int c = 0; c < NC; c++) {
        issuer[c] = zt_issuer(e.cap, k_ccy[c]);
        funded[c] = 0;
        if (zsc_account(h, ZSC_ACCT_HUB, NULL, k_ccy[c], &hub[c]) != ZXV_OK) return 2;
        for (int b = 0; b < 4; b++)
            if (zsc_account(h, ZSC_ACCT_RESERVE + b, NULL, k_ccy[c], &bucket[c][b]) != ZXV_OK)
                return 2;
        for (int p = 0; p < NP; p++) {
            char ref[32];
            if (zsc_account(h, ZSC_ACCT_PARTICIPANT, k_codes[p], k_ccy[c], &acct[c][p]) != ZXV_OK)
                return 2;
            snprintf(ref, sizeof ref, "seed-%d-%d", c, p);
            if (zt_fund(e.cap, k_ccy[c], issuer[c], acct[c][p], 100000000, ref) != 0) return 2;
            funded[c] += 100000000;
        }
    }
    if (zsc_cycle_open(h, "FZ-1", "2026-10-12T10:00:00Z") != ZXV_OK) return 2;

    for (unsigned long it = 0; it < iters; it++) {
        int op_ = (int) (zt_rand(&s) % 100);
        int32_t st;
        if (op_ < 12) {
            /* a pending payment again: the same (a replay) or changed (a conflict) */
            uint32_t np = 0, k;
            zsc_instr in;
            int how;
            (void) zsc_pending_count(h, &np);
            if (np == 0) continue;
            k = (uint32_t) (zt_rand(&s) % np);
            if (zsc_pending_at(h, k, &in) != ZXV_OK) die("pending_at", it);
            how = (int) (zt_rand(&s) % 3);
            if (how == 0) {
                snprintf(in.msg_id, sizeof in.msg_id, "RESEND-%lu", it);
                if (zsc_submit(h, &in) != ZIND_E_REPLAY) die("a resend was not a replay", it);
                n_replay++;
                continue;
            }
            if (how == 1) in.amount = in.amount % 1000 + 1 + (in.amount % 1000 == 0);
            else snprintf(in.cdtr_acct, sizeof in.cdtr_acct, "CHANGED-%lu", it);
            st = zsc_submit(h, &in);
            if (st == ZXV_OK) die("a changed payment under a pending id was accepted", it);
            if (st != ZIND_E_HELD && st != ZIND_E_REPLAY) die("conflict: unexpected status", it);
            if (st == ZIND_E_HELD) n_held++;
            else n_replay++; /* the change happened to give the same content */
        } else if (op_ < 40) {
            zsc_instr in = rand_instr(&s, pool);
            st = zsc_submit(h, &in);
            if (!submit_ok_status(st)) die("submit: unexpected status", it);
            if (st == ZXV_OK) n_ok++;
            else if (st == ZIND_E_HELD) n_held++;
            else if (st == ZIND_E_REPLAY) n_replay++;
            else n_refused++;
            if ((in.amount <= 0 || in.amount > ZXV_AMOUNT_MAX) && st != ZXV_E_RANGE)
                die("a bad amount was not refused", it);
            if (strcmp(in.dbtr_agt, in.cdtr_agt) == 0 && st != ZSC_E_PARTICIPANT && st != ZXV_E_RANGE)
                die("the same agent twice was not refused", it);
        } else if (op_ < 52) {
            zsc_instr in = rand_instr(&s, pool), back;
            size_t len = 0, off = 0;
            int damage;
            if (in.amount <= 0 || strcmp(in.dbtr_agt, in.cdtr_agt) == 0) continue;
            if (zsc_render_pacs008(e.cap, &in, xml, sizeof xml, &len) != ZXV_OK) die("render", it);
            damage = (int) (zt_rand(&s) % 3);
            if (damage == 1) {
                size_t at = (size_t) (zt_rand(&s) % len);
                xml[at] = (char) (zt_rand(&s) % 256);
            } else if (damage == 2) {
                len = (size_t) (zt_rand(&s) % len);
            }
            st = zsc_parse_pacs008(xml, len, &back, &off);
            n_pacs++;
            if (st == ZXV_OK) {
                char again[8192];
                size_t l2 = 0;
                zsc_instr twice;
                if (damage == 0) {
                    in.source = ZSC_SRC_PACS008;
                    if (!same_instr(&in, &back)) die("pacs.008 round trip changed the payment", it);
                }
                /* whatever parses, re-renders and parses to itself */
                if (zsc_render_pacs008(e.cap, &back, again, sizeof again, &l2) != ZXV_OK ||
                    zsc_parse_pacs008(again, l2, &twice, NULL) != ZXV_OK || !same_instr(&back, &twice))
                    die("accepted pacs.008 is not a fixed point", it);
                st = zsc_submit(h, &back);
                if (!submit_ok_status(st)) die("submit pacs: unexpected status", it);
                if (st == ZXV_OK) n_ok++;
                else if (st == ZIND_E_HELD) n_held++;
            } else {
                if (damage == 0) die("an undamaged message was refused", it);
                if (off > len) die("error offset past the input", it);
                n_pacs_bad++;
            }
        } else if (op_ < 57) {
            char text[512];
            zsc_batch b;
            int a = (int) (zt_rand(&s) % 4);
            int bb = (a + 1) % 4;
            int64_t x = 1 + (int64_t) (zt_rand(&s) % 9999);
            int64_t y = 1 + (int64_t) (zt_rand(&s) % 9999);
            uint64_t name = zt_rand(&s) % (pool / 4 + 2);
            int c = (int) (zt_rand(&s) % NC);
            snprintf(text, sizeof text,
                     "TREATY B%llu %s NET\nPARTY %s 0 0 1/1\nPARTY %s 0 0 0/1\nOWE %s %s %lld\n"
                     "OWE %s %s %lld\nEND\n",
                     (unsigned long long) name, k_ccy[c], k_codes[a], k_codes[bb], k_codes[a],
                     k_codes[bb], (long long) x, k_codes[bb], k_codes[a], (long long) y);
            memset(&b, 0, sizeof b);
            st = zsc_submit_sutra(h, text, &b);
            if (st != ZXV_OK && st != ZXV_E_CAPACITY) die("treaty refused", it);
            n_treaty++;
            n_ok += b.accepted;
            n_held += b.held;
            n_replay += b.replayed;
        } else if (op_ < 75) {
            uint32_t nh = 0, v = 0, gi;
            uint8_t msg[256], sig[ZIND_SIG_BYTES];
            size_t len = 0;
            char e2e[40];
            uint32_t choice;
            int forged;
            (void) zsc_held_count(h, &nh);
            if (nh == 0) continue;
            gi = (uint32_t) (zt_rand(&s) % nh);
            if (zsc_held_at(h, gi, e2e, sizeof e2e, &v) != ZXV_OK) die("held_at", it);
            choice = (uint32_t) (zt_rand(&s) % (v + 1));
            forged = (int) (zt_rand(&s) % 4 == 0);
            if (zsc_decision_message(h, e2e, choice, msg, sizeof msg, &len) != ZXV_OK) die("msg", it);
            if (zind_sign(e.cap, forged ? rogue : op, msg, len, "sutra-clear/operator", sig) != ZXV_OK)
                die("sign", it);
            st = zsc_resolve(h, zind_signer_pk(op), e2e, choice, sig);
            if (forged) {
                if (st != ZXV_E_VERIFY) die("a forged decision was not refused", it);
                n_forged++;
            } else {
                if (st != ZXV_OK && st != ZXV_E_CAPACITY) die("a genuine decision failed", it);
                if (st == ZXV_OK) {
                    n_decided++;
                    /* the decided id is closed for good */
                    if (zsc_resolve(h, zind_signer_pk(op), e2e, choice, sig) != ZXV_E_NOT_FOUND)
                        die("a replayed decision was not refused", it);
                }
            }
        } else if (op_ < 87) {
            int c = (int) (zt_rand(&s) % NC);
            int p = (int) (zt_rand(&s) % NP);
            int64_t amt = 1 + (int64_t) (zt_rand(&s) % 5000000);
            char ref[32];
            snprintf(ref, sizeof ref, "top-%lu", it);
            if (zt_fund(e.cap, k_ccy[c], issuer[c], acct[c][p], amt, ref) != 0) die("fund", it);
            funded[c] += amt;
        } else if (op_ < 95) {
            uint32_t np = 0, k;
            zsc_instr in;
            (void) zsc_pending_count(h, &np);
            if (np == 0) continue;
            k = (uint32_t) (zt_rand(&s) % np);
            if (zsc_pending_at(h, k, &in) != ZXV_OK) die("pending_at", it);
            if (zsc_withdraw(h, in.e2e) != ZXV_OK) die("withdraw of a pending id failed", it);
            if (zsc_submit(h, &in) != ZIND_E_REPLAY) die("a withdrawn id came back", it);
            n_withdrawn++;
        } else {
            uint32_t before = entries_total(e.cap);
            zsc_cycle_info info;
            char cid[17];
            st = zsc_cycle_close(h, &info);
            while (st == ZXV_E_FUNDS || st == ZXV_E_RANGE) {
                if (entries_total(e.cap) != before) die("a refused close posted something", it);
                n_funds++;
                {
                    uint32_t np = 0;
                    zsc_instr in;
                    (void) zsc_pending_count(h, &np);
                    if (np == 0) die("refused with nothing pending", it);
                    if (zsc_pending_at(h, np - 1, &in) != ZXV_OK || zsc_withdraw(h, in.e2e) != ZXV_OK)
                        die("withdraw", it);
                }
                n_withdrawn++;
                st = zsc_cycle_close(h, &info);
            }
            if (st != ZXV_OK) die("close failed", it);
            n_closed++;
            n_settled += info.instructions;
            /* invariants of a settled cycle */
            for (int c = 0; c < NC; c++) {
                int64_t sum = 0, money = 0, eq = 0;
                for (int p = 0; p < NP; p++) {
                    int64_t net = 0;
                    zsc_fee_info fi;
                    if (zsc_position(h, k_codes[p], k_ccy[c], &net) == ZXV_OK) sum += net;
                    if (zsc_fee(h, k_codes[p], k_ccy[c], &fi) == ZXV_OK) {
                        if (fi.split[0] + fi.split[1] + fi.split[2] + fi.split[3] != fi.fee)
                            die("fee split does not sum", it);
                        if (fi.rem_out >= ZXV_FEE_DEN) die("carry out of range", it);
                    }
                    eq = zt_equity(e.cap, acct[c][p]);
                    if (eq < 0) die("a participant went negative", it);
                    money += eq;
                }
                for (int b = 0; b < 4; b++) money += zt_equity(e.cap, bucket[c][b]);
                if (sum != 0) die("net positions do not sum to zero", it);
                if (zt_equity(e.cap, hub[c]) != 0) die("a hub is not back at zero", it);
                if (money != funded[c]) die("money not conserved", it);
            }
            if (zxv_ledger_check_cap(e.cap) != ZXV_OK) die("ledger check", it);
            if (n_closed % 10 == 0) {
                const uint8_t *trust[1] = {zind_signer_pk(house_key)};
                if (zind_chain_verify(e.cap, zsc_records(h), trust, 1, NULL) != ZXV_OK)
                    die("record chain does not verify", it);
            }
            snprintf(cid, sizeof cid, "FZ-%lu", n_closed + 1);
            if (zsc_cycle_open(h, cid, "2026-10-12T10:00:00Z") != ZXV_OK) die("open", it);
        }
    }
    {
        const uint8_t *trust[1] = {zind_signer_pk(house_key)};
        if (zind_chain_verify(e.cap, zsc_records(h), trust, 1, NULL) != ZXV_OK)
            die("final chain", iters);
        if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    }
    printf("fuzz_sclear: %lu ops, %lu cycles closed (%lu settled), accepted %lu, held %lu, "
           "replayed %lu, refused %lu, pacs.008 %lu (%lu damaged refused), treaties %lu, "
           "decisions %lu (forged refused %lu), withdrawn %lu, refused closes %lu, seed %llu, "
           "0 invariant failures\n",
           iters, n_closed, n_settled, n_ok, n_held, n_replay, n_refused, n_pacs, n_pacs_bad,
           n_treaty, n_decided, n_forged, n_withdrawn, n_funds, (unsigned long long) seed);
    zsc_house_destroy(h);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(house_key);
    zind_signer_destroy(op);
    zind_signer_destroy(rogue);
    zt_env_close(&e);
    return 0;
}
