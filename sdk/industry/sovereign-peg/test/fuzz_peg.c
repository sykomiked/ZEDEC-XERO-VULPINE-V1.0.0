/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_peg.c — a deterministic operation fuzzer for sovereign-peg.
 * Random issuance (holders, pools, floats), flows with random
 * authorisations and amounts (exact quotes, off-by-one amounts, wrong
 * currencies), officer decisions (genuine and forged), issuer pegs
 * (genuine, forged, stale, uncovering), policy edits (including the locked
 * entries), outflow caps, blocked senders and epoch rollovers. After every
 * operation, against an independent model:
 *   - the flow's tag and outcome are what the policy table says for it;
 *   - money moves only for a DONE or RELEASED flow, by the library's quote;
 *   - every currency stays covered (zsp_check), holders sum to circulation;
 *   - Forbidden tags never reach ALLOW, a Forbidden flow never moves money;
 * and every 100 operations the operator log and the quarantine log verify.
 * The last line prints the log head and a digest of every balance, so two
 * builds (gcc, clang) can be compared byte for byte.
 *
 *   fuzz_peg ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zspeg.h"

#define NS 3
#define NH 4
#define MAXF 65536

typedef struct {
    int32_t state;
    uint8_t from;
} mflow_t;

static mflow_t g_f[MAXF + 1];

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_peg: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    abort();
}

int main(int argc, char **argv) {
    static const uint8_t hub_seed[32] = {0x5e, 0xed};
    static const char *codes[NS] = {"AAA", "BBB", "CCC"};
    static const uint8_t minors[NS] = {2, 2, 0};
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x5065675eULL), s = seed;
    zt_env e;
    zxv_cap *rd = NULL;
    zind_signer *desk_key, *officer, *rogue;
    uint8_t iss_pk[NS][ZIND_PK_BYTES], iss_sk[NS][4896], rog_pk[ZIND_PK_BYTES], rog_sk[4896];
    uint8_t sig[ZIND_SIG_BYTES], policy[64];
    uint64_t peg_num[NS], peg_den[NS], peg_ep[NS], ocap[NS] = {0, 0, 0};
    uint32_t hold[NS][NH], blocked[8], n_blocked = 0, nf = 0;
    zxv_hexlog *qlog = NULL;
    zsp_desk *d = NULL;
    unsigned long n_done = 0, n_held = 0, n_ref = 0, n_quar = 0, n_fail = 0, n_rel = 0,
                  n_deny = 0, n_forged = 0, n_unbacked = 0, n_pegs = 0, n_issue = 0;

    if (zt_env_open(&e, NULL) != 0 || zxv_cap_attenuate(e.cap, ZXV_CAP_LEDGER_READ, &rd) != ZXV_OK)
        return 2;
    desk_key = zt_signer(e.cap, 1);
    officer = zt_signer(e.cap, 2);
    rogue = zt_signer(e.cap, 3);
    if (!desk_key || !officer || !rogue) return 2;
    for (int i = 0; i <= NS; i++) {
        uint8_t sd[32];
        zt_seed(sd, (uint8_t) (60 + i));
        if (zxv_mldsa87_keygen_seed(e.cap, sd, i < NS ? iss_pk[i] : rog_pk,
                                    i < NS ? iss_sk[i] : rog_sk) != ZXV_OK)
            return 2;
    }
    if (zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        zsp_desk_create(e.cap, hub_seed, desk_key, qlog, &d) != ZXV_OK ||
        zsp_officer_add(d, e.cap, zind_signer_pk(officer)) != ZXV_OK)
        return 2;
    for (uint8_t c = 0; c < NS; c++) {
        uint8_t m[40];
        peg_num[c] = c == 0 ? 150 : c == 1 ? 300 : 1;
        peg_den[c] = c == 2 ? 3 : 1;
        peg_ep[c] = 1;
        if (zsp_ccy_add(d, e.cap, c, codes[c], minors[c], iss_pk[c], 100000000, 20000) !=
                ZXV_OK ||
            zsp_peg_message(c, peg_num[c], peg_den[c], 1, m) != ZXV_OK ||
            zxv_mldsa87_sign_deterministic(e.cap, iss_sk[c], m, 40, NULL, 0, sig) != ZXV_OK ||
            zsp_peg_set(d, e.cap, c, peg_num[c], peg_den[c], 1, sig) != ZXV_OK ||
            zsp_limit_add(d, e.cap, c, 2000000) != ZXV_OK)
            return 2;
        for (int h = 0; h < NH; h++)
            if (zsp_account_open(d, e.cap, c, (uint32_t) (c * 10 + h), &hold[c][h]) != ZXV_OK)
                return 2;
    }
    for (uint8_t t = 0; t < 64; t++) {
        int32_t dec = 0;
        zsp_policy_get(d, t, &dec);
        policy[t] = (uint8_t) dec;
    }

    for (unsigned long it = 0; it < iters; it++) {
        int op = (int) (zt_rand(&s) % 100);
        uint8_t c = (uint8_t) (zt_rand(&s) % NS);
        uint32_t h = hold[c][zt_rand(&s) % NH];
        int32_t st;
        if (op < 15) {
            /* issue to a holder: OK iff still covered (checked here with zxv_rat_cmp) */
            uint64_t amt = 1 + zt_rand(&s) % 400000, circ = 0, flt = 0;
            int32_t cmp = 0;
            int64_t before = 0, after = 0;
            if (minors[c] == 0) amt = 1 + amt % 2000;
            if (zsp_coverage(d, rd, c, &circ, &flt, NULL) != ZXV_OK) die("coverage", it);
            if (zxv_rat_cmp((int64_t) (circ + amt), (int64_t) peg_num[c], (int64_t) flt,
                            (int64_t) peg_den[c], &cmp) != ZXV_OK)
                die("rat_cmp", it);
            zsp_balance(d, rd, h, &before);
            st = zsp_issue(d, e.cap, h, amt);
            zsp_balance(d, rd, h, &after);
            if (cmp <= 0) {
                if (st != ZXV_OK) die("covered issue refused", it);
                if (after != before + (int64_t) amt) die("issue amount", it);
                n_issue++;
            } else {
                if (st != ZSP_E_UNBACKED) die("uncovered issue accepted", it);
                if (after != before) die("refused issue moved money", it);
                n_unbacked++;
            }
        } else if (op < 20) {
            uint32_t pool = 0, vf = 0;
            zsp_ccy_accounts(d, rd, c, NULL, &pool, &vf);
            if (zsp_issue(d, e.cap, op < 17 ? vf : pool, 1 + zt_rand(&s) % 1000) != ZXV_OK)
                die("float/pool issue", it);
        } else if (op < 60 && nf < MAXF) {
            /* a flow */
            uint8_t c2 = (uint8_t) ((c + 1 + zt_rand(&s) % (NS - 1)) % NS);
            uint32_t r = hold[c2][zt_rand(&s) % NH], id = 0, k;
            uint64_t want = 1 + zt_rand(&s) % 60000, amt = 0, rcv = 0;
            int64_t sb = 0, rb = 0, sa = 0, ra = 0;
            int32_t lp = 0, dec, want_state, real = 0;
            uint8_t base = 0, pol = ZXV_HEX_POL_NEUTRAL, tag = 0, tag2 = 0;
            int blk = 0;
            zsp_flow fl;
            if (zsp_quote(d, rd, c, c2, want, &amt, NULL, &rcv) != ZXV_OK) die("quote", it);
            if (amt == 0 || zt_rand(&s) % 8 == 0) amt = want; /* often inexact */
            memset(&fl, 0, sizeof fl);
            fl.from_slot = c;
            fl.to_slot = c2;
            fl.sender = h;
            fl.receiver = r;
            fl.amount = amt;
            fl.tick = it;
            fl.approvals = (uint32_t) (zt_rand(&s) % 4);
            fl.objections = (uint32_t) (zt_rand(&s) % 3 == 0 ? zt_rand(&s) % 3 : 0);
            /* the model's tag */
            zxv_lp_from_evidence(fl.approvals, fl.objections, &lp);
            zxv_hex_from_lp(lp, &base);
            for (k = 0; k < n_blocked; k++)
                if (blocked[k] == h) blk = 1;
            if (blk || (ocap[c] && amt > ocap[c]))
                pol = ZXV_HEX_POL_FORBIDDEN;
            else if (fl.approvals >= 2 && fl.objections == 0)
                pol = ZXV_HEX_POL_NEGENTROPIC;
            zxv_hex_pack(base, pol, ZXV_HEX_PHASE_POTENTIAL, &tag);
            if (zsp_flow_tag(d, &fl, &tag2) != ZXV_OK || tag2 != tag) die("tag differs", it);
            dec = pol == ZXV_HEX_POL_FORBIDDEN ? ZSP_REFUSE : policy[tag];
            want_state = pol == ZXV_HEX_POL_FORBIDDEN ? ZSP_FLOW_QUARANTINED
                         : dec == ZSP_HOLD            ? ZSP_FLOW_HELD
                         : dec == ZSP_REFUSE          ? ZSP_FLOW_REFUSED
                                                      : -1; /* DONE or FAILED */
            zsp_balance(d, rd, h, &sb);
            zsp_balance(d, rd, r, &rb);
            st = zsp_flow_submit(d, e.cap, &fl, &id);
            zsp_balance(d, rd, h, &sa);
            zsp_balance(d, rd, r, &ra);
            if (id != nf + 1) die("flow id", it);
            nf = id;
            zsp_flow_state(d, id, &real, NULL, NULL);
            g_f[id].state = real;
            g_f[id].from = c;
            if (want_state >= 0 && real != want_state) die("flow state differs from policy", it);
            if (want_state < 0 && real != ZSP_FLOW_DONE && real != ZSP_FLOW_FAILED)
                die("allowed flow neither done nor failed", it);
            if (real == ZSP_FLOW_DONE) {
                if (st != ZXV_OK || amt != fl.amount || sa != sb - (int64_t) amt ||
                    ra != rb + (int64_t) rcv)
                    die("done flow did not move the quoted amounts", it);
                n_done++;
            } else {
                if (sa != sb || ra != rb) die("money moved for a flow not done", it);
                if (real == ZSP_FLOW_HELD) n_held++;
                if (real == ZSP_FLOW_REFUSED) n_ref++;
                if (real == ZSP_FLOW_QUARANTINED) n_quar++;
                if (real == ZSP_FLOW_FAILED) n_fail++;
            }
        } else if (op < 70 && nf) {
            /* an officer decision on some flow */
            uint32_t id = 1 + (uint32_t) (zt_rand(&s) % nf);
            int forged = zt_rand(&s) % 4 == 0;
            int32_t decn = zt_rand(&s) % 2 ? ZSP_RELEASE : ZSP_DENY, real = 0;
            uint8_t m[64];
            size_t len = 0;
            if (g_f[id].state != ZSP_FLOW_HELD) {
                st = zsp_decide(d, e.cap, id, zind_signer_pk(officer), decn, it, sig);
                if (st == ZXV_OK) die("decision on a flow not held", it);
                continue;
            }
            if (zsp_decision_message(d, id, decn, m, sizeof m, &len) != ZXV_OK) die("dmsg", it);
            if (zind_sign(e.cap, forged ? rogue : officer, m, len, ZSP_DECISION_CTX, sig) !=
                ZXV_OK)
                die("sign", it);
            st = zsp_decide(d, e.cap, id, zind_signer_pk(officer), decn, it, sig);
            zsp_flow_state(d, id, &real, NULL, NULL);
            if (forged) {
                if (st != ZXV_E_VERIFY || real != ZSP_FLOW_HELD) die("forged decision", it);
                n_forged++;
            } else if (st == ZXV_OK) {
                if (real != (decn == ZSP_RELEASE ? ZSP_FLOW_RELEASED : ZSP_FLOW_DENIED))
                    die("decision state", it);
                if (decn == ZSP_RELEASE) n_rel++;
                else n_deny++;
            } else if (decn != ZSP_RELEASE || real != ZSP_FLOW_HELD) {
                die("only a release may fail, and then it stays held", it);
            }
            g_f[id].state = real;
        } else if (op < 78) {
            /* a peg: genuine, forged or stale; covered or not */
            uint64_t num = c == 2 ? 1 + zt_rand(&s) % 3 : 50 * (1 + zt_rand(&s) % 8);
            uint64_t den = c == 2 ? 3 : 1, ep = peg_ep[c] + (zt_rand(&s) % 5 == 0 ? 0 : 1);
            uint64_t circ = 0, flt = 0;
            int forged = zt_rand(&s) % 5 == 0;
            int32_t cmp = 0;
            uint8_t m[40];
            zsp_peg_message(c, num, den, ep, m);
            if (zxv_mldsa87_sign_deterministic(e.cap, forged ? rog_sk : iss_sk[c], m, 40, NULL, 0,
                                               sig) != ZXV_OK)
                die("peg sign", it);
            zsp_coverage(d, rd, c, &circ, &flt, NULL);
            zxv_rat_cmp((int64_t) circ, (int64_t) num, (int64_t) flt, (int64_t) den, &cmp);
            st = zsp_peg_set(d, e.cap, c, num, den, ep, sig);
            if (forged) {
                if (st != ZXV_E_VERIFY) die("forged peg", it);
            } else if (cmp > 0 && circ > 0) {
                if (st != ZSP_E_UNBACKED) die("uncovering peg", it);
            } else if (ep <= peg_ep[c]) {
                if (st != ZXV_E_PEG) die("stale peg", it);
            } else {
                if (st != ZXV_OK) die("genuine peg refused", it);
                peg_num[c] = num;
                peg_den[c] = den;
                peg_ep[c] = ep;
                n_pegs++;
            }
        } else if (op < 85) {
            uint8_t t = (uint8_t) (zt_rand(&s) % 64), base = 0, pol = 0;
            int32_t dec = 1 + (int32_t) (zt_rand(&s) % 3);
            int locked;
            zxv_hex_unpack(t, &base, &pol, NULL);
            locked = (pol == ZXV_HEX_POL_FORBIDDEN && dec != ZSP_REFUSE) ||
                     ((base == ZXV_HEX_BASE_BOTH || base == ZXV_HEX_BASE_FALSE) &&
                      dec == ZSP_ALLOW);
            st = zsp_policy_set(d, e.cap, t, dec);
            if (locked ? st != ZSP_E_POLICY : st != ZXV_OK) die("policy lock", it);
            if (!locked) policy[t] = (uint8_t) dec;
        } else if (op < 90) {
            if (zsp_epoch_next(d, e.cap, NULL) != ZXV_OK) die("epoch", it);
        } else if (op < 95) {
            if (zt_rand(&s) % 10 == 0 && n_blocked < 2) {
                uint32_t k;
                int dup = 0;
                for (k = 0; k < n_blocked; k++)
                    if (blocked[k] == h) dup = 1;
                st = zsp_block_sender(d, e.cap, h);
                if (dup ? st != ZXV_E_DUPLICATE : st != ZXV_OK) die("block", it);
                if (!dup) blocked[n_blocked++] = h;
            } else {
                uint64_t cap = zt_rand(&s) % 2 == 0 ? 0 : 20000 + zt_rand(&s) % 200000;
                if (zsp_outflow_cap_set(d, e.cap, c, cap) != ZXV_OK) die("cap", it);
                ocap[c] = cap;
            }
        } else {
            uint8_t c2 = (uint8_t) ((c + 1) % NS);
            uint64_t want = zt_rand(&s) % 100000, amt = 0, vb = 0, rcv = 0;
            if (zsp_quote(d, rd, c, c2, want, &amt, &vb, &rcv) != ZXV_OK) die("quote", it);
            /* exact through the V-Bill: amt * den_c == vb * num_c, vb * num_c2 == rcv * den_c2 */
            if (amt > want || amt * peg_den[c] != vb * peg_num[c] ||
                vb * peg_num[c2] != rcv * peg_den[c2])
                die("quote not exact under the model pegs", it);
        }
        /* invariants */
        if (zsp_check(d, rd, NULL) != ZXV_OK) die("hub check / coverage", it);
        if (it % 16 == 0)
            for (uint8_t cc = 0; cc < NS; cc++) {
                uint64_t circ = 0, sum = 0;
                zsp_coverage(d, rd, cc, &circ, NULL, NULL);
                for (int k = 0; k < NH; k++) {
                    int64_t b = 0;
                    zsp_balance(d, rd, hold[cc][k], &b);
                    sum += (uint64_t) b;
                }
                if (sum != circ) die("holders do not sum to circulation", it);
            }
        if (it % 100 == 99) {
            const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
            if (zind_chain_verify(e.cap, zsp_log(d), trust, 1, NULL) != ZXV_OK) die("log", it);
            if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", it);
        }
    }
    {
        uint8_t head[32], dig[32], acc[NS * NH * 8];
        uint32_t nrec = 0;
        size_t k = 0;
        zind_chain_head(zsp_log(d), head);
        zind_chain_count(zsp_log(d), &nrec);
        for (int c = 0; c < NS; c++)
            for (int hh = 0; hh < NH; hh++) {
                int64_t b = 0;
                zsp_balance(d, rd, hold[c][hh], &b);
                for (int i = 0; i < 8; i++) acc[k++] = (uint8_t) ((uint64_t) b >> (8 * i));
            }
        zxv_sha3_256(acc, sizeof acc, dig);
        printf("fuzz_peg: %lu ops, %u flows (done %lu, held %lu, refused %lu, quarantined %lu, "
               "failed %lu; released %lu, denied %lu, forged refused %lu), issues %lu, unbacked "
               "refused %lu, pegs %lu, seed %llu, 0 invariant failures\n",
               iters, nf, n_done, n_held, n_ref, n_quar, n_fail, n_rel, n_deny, n_forged, n_issue,
               n_unbacked, n_pegs, (unsigned long long) seed);
        printf("fuzz_peg: %u log records, head ", nrec);
        for (int i = 0; i < 8; i++) printf("%02x", head[i]);
        printf(", balances ");
        for (int i = 0; i < 8; i++) printf("%02x", dig[i]);
        printf("\n");
    }
    zsp_desk_destroy(d);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(desk_key);
    zind_signer_destroy(officer);
    zind_signer_destroy(rogue);
    zxv_cap_destroy(rd);
    zt_env_close(&e);
    return 0;
}
