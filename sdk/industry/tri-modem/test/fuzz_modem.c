/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_modem.c — a deterministic operation fuzzer for tri-modem against an
 * independent model of the evidence rules. Random adverts from five
 * neighbours (Ed25519 and ML-DSA-87): fresh, replayed, older, expired,
 * dated ahead, forged, amateur-radio, and with claims that are absent,
 * true, false or contradictory; random probes; owner resolutions (genuine
 * and forged); agreements changed and withdrawn; and plans with random
 * demands. After every operation the status and every neighbour's view of
 * both destinations (joined value, hold, eligibility and its reason,
 * budget) equal the model; every plan is checked for budgets, exact rmag
 * charges and max-min fairness on each relay.
 *
 * One random draw per statement, so the stream (and the output) is
 * byte-identical under any C compiler.
 *
 *   fuzz_modem ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "ztmodem.h"
#include "zind_test.h"

#define NB 5
#define ND 2

static const char *k_nb[NB] = {"alpha", "bravo", "charlie", "delta", "echo"};
static const char *k_dest[ND] = {"internet", "dns"};

typedef struct {
    int32_t advert_lp, probe_lp;
    uint64_t probe_time;
    int has_probe, held, has_override;
    int32_t override_lp;
    uint64_t override_seq;
} mev_t;

typedef struct {
    int has_adv;
    uint64_t seq, expires, last_seq;
    int wan;
    zind_qty offer, agreement;
    mev_t ev[ND];
} mnb_t;

static mnb_t g_m[NB];
static unsigned long g_n[16];

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_modem: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    abort();
}

static int32_t join(int32_t a, int32_t b) {
    if (a == ZXV_LP_NEITHER) return b;
    if (b == ZXV_LP_NEITHER) return a;
    return a == b ? a : ZXV_LP_BOTH;
}

static int32_t m_lp(const mnb_t *n, const mev_t *e, uint64_t now) {
    int32_t pr = ZXV_LP_NEITHER;
    if (e->held) return ZXV_LP_BOTH;
    if (e->has_override && n->has_adv && e->override_seq == n->seq) return e->override_lp;
    if (e->has_probe && now >= e->probe_time && now - e->probe_time <= ZTM_PROBE_TTL_S)
        pr = e->probe_lp;
    return join(e->advert_lp, pr);
}

static int m_hold(mnb_t *n, uint64_t now) {
    int any = 0;
    for (int d = 0; d < ND; d++)
        if (!n->ev[d].held && m_lp(n, &n->ev[d], now) == ZXV_LP_BOTH) {
            n->ev[d].held = 1;
            any = 1;
        }
    return any;
}

static zind_qty qmin(zind_qty a, zind_qty b) {
    int32_t c = 0;
    zind_qty_cmp(a, b, &c);
    return c <= 0 ? a : b;
}

static int32_t m_why(const mnb_t *n, int d, uint64_t now, zind_qty *budget) {
    *budget = ZIND_QTY_ZERO;
    if (!n->has_adv || now > n->expires) return ZTM_E_EXPIRED;
    *budget = qmin(n->offer, n->agreement);
    if (zind_qty_sign(n->agreement) <= 0) return ZTM_E_AGREEMENT;
    if (n->ev[d].held) return ZIND_E_HELD;
    if (!n->wan || zind_qty_sign(*budget) <= 0) return ZTM_E_NO_RELAY;
    if (m_lp(n, &n->ev[d], now) != ZXV_LP_TRUE) return ZXV_E_NOT_FOUND;
    return ZXV_OK;
}

static void check_views(const ztm_planner *p, uint64_t now, unsigned long it) {
    for (int j = 0; j < NB; j++)
        for (int d = 0; d < ND; d++) {
            ztm_view v;
            zind_qty b;
            int32_t why = m_why(&g_m[j], d, now, &b);
            if (ztm_neighbour_view(p, k_nb[j], k_dest[d], now, &v) != ZXV_OK) die("view", it);
            if (v.why != why || v.eligible != (why == ZXV_OK) || v.held != g_m[j].ev[d].held ||
                v.lp != m_lp(&g_m[j], &g_m[j].ev[d], now)) {
                fprintf(stderr, "%s/%s: why %s (model %s) lp %d (model %d) held %d (model %d)\n",
                        k_nb[j], k_dest[d], ztm_status_name(v.why), ztm_status_name(why), v.lp,
                        m_lp(&g_m[j], &g_m[j].ev[d], now), v.held, g_m[j].ev[d].held);
                die("a view differs from the model", it);
            }
            if (g_m[j].has_adv && (v.budget_kbps.num != b.num || v.budget_kbps.den != b.den))
                die("a budget differs from the model", it);
        }
}

static void check_plan(const ztm_planner *p, const ztm_demand *dm, uint32_t n, const ztm_plan *pl,
                       int d, uint64_t now, unsigned long it) {
    for (uint32_t i = 0; i < pl->n; i++) {
        zind_qty sum = ZIND_QTY_ZERO, maxa = ZIND_QTY_ZERO, budget, rem, want;
        int j = -1, limited = 0;
        int32_t c = 0;
        for (int k = 0; k < NB; k++)
            if (strcmp(k_nb[k], pl->a[i].relay) == 0) j = k;
        if (j < 0 || m_why(&g_m[j], d, now, &budget) != ZXV_OK)
            die("a plan used a neighbour the model says is not eligible", it);
        if (strcmp(pl->a[i].flow, dm[i].flow) != 0) die("plan order", it);
        zind_qty_cmp(pl->a[i].alloc_kbps, dm[i].demand_kbps, &c);
        if (c > 0 || zind_qty_sign(pl->a[i].alloc_kbps) <= 0) die("an allocation beyond demand", it);
        for (uint32_t k = 0; k < pl->n; k++) {
            if (strcmp(pl->a[k].relay, pl->a[i].relay) != 0) continue;
            if (zind_qty_add(sum, pl->a[k].alloc_kbps, &sum) != ZXV_OK) die("sum", it);
            zind_qty_cmp(pl->a[k].alloc_kbps, maxa, &c);
            if (c > 0) maxa = pl->a[k].alloc_kbps;
        }
        zind_qty_cmp(sum, budget, &c);
        if (c > 0) die("a relay over its budget", it);
        for (uint32_t k = 0; k < pl->n; k++) {
            if (strcmp(pl->a[k].relay, pl->a[i].relay) != 0) continue;
            zind_qty_cmp(pl->a[k].alloc_kbps, dm[k].demand_kbps, &c);
            if (c < 0) {
                limited = 1;
                zind_qty_cmp(pl->a[k].alloc_kbps, maxa, &c);
                if (c != 0) die("a limited flow got less than another on its relay", it);
            }
        }
        if (limited) {
            zind_qty_cmp(sum, budget, &c);
            if (c != 0) die("a limited relay was not used up", it);
        }
        if (ztm_budget_remaining(p, pl->a[i].relay, &rem) != ZXV_OK ||
            zind_qty_sub(budget, sum, &want) != ZXV_OK || rem.num != want.num ||
            rem.den != want.den)
            die("the rmag charge is not exact", it);
    }
    (void) n;
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x30DE3ULL), s = seed, now = 1760000000ULL;
    zt_env e;
    uint8_t owner_sk[32], owner_pk[32], sk[NB][32], pk[NB][32], rogue_sk[32];
    zind_signer *ml[NB] = {NULL};
    zxv_hexlog *qlog = NULL;
    ztm_planner *p = NULL;
    if (zt_env_open(&e, NULL) != 0) return 2;
    zt_seed(owner_sk, 0xA1);
    zt_seed(rogue_sk, 0xA2);
    if (zxv_ed25519_public(e.cap, owner_sk, owner_pk) != ZXV_OK ||
        zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        ztm_planner_create(e.cap, "home", owner_pk, qlog, &p) != ZXV_OK)
        return 2;
    for (int j = 0; j < NB; j++) {
        int32_t st;
        if (j % 2) {
            ml[j] = zt_signer(e.cap, (uint8_t) (0x40 + j));
            if (!ml[j]) return 2;
            st = ztm_neighbour_add(p, k_nb[j], ZTM_ALG_MLDSA87, zind_signer_pk(ml[j]), ZIND_PK_BYTES);
        } else {
            zt_seed(sk[j], (uint8_t) (0x40 + j));
            if (zxv_ed25519_public(e.cap, sk[j], pk[j]) != ZXV_OK) return 2;
            st = ztm_neighbour_add(p, k_nb[j], ZTM_ALG_ED25519, pk[j], 32);
        }
        if (st != ZXV_OK) return 2;
        g_m[j].agreement = ZIND_QTY_ZERO;
    }

    for (unsigned long it = 0; it < iters; it++) {
        uint64_t r = zt_rand(&s);
        int op = (int) (r % 100), j;
        mnb_t *m;
        r = zt_rand(&s);
        j = (int) (r % NB);
        m = &g_m[j];
        r = zt_rand(&s);
        now += 1 + r % 40;
        if (op < 40) {
            /* an advert */
            ztm_advert a;
            uint8_t buf[ZTM_ADVERT_MAX];
            size_t len = 0;
            int forged, kind;
            int32_t want, rc, claim[ND];
            memset(&a, 0, sizeof a);
            snprintf(a.neighbour, sizeof a.neighbour, "%s", k_nb[j]);
            r = zt_rand(&s);
            kind = (int) (r % 20);
            a.seq = kind < 15 ? m->last_seq + 1 + r % 3 : kind < 18 ? m->last_seq - r % 2 : r % 50;
            if (a.seq == 0) a.seq = 1;
            r = zt_rand(&s);
            a.issued = now + r % 400 - 200;
            r = zt_rand(&s);
            a.expires = a.issued + 1 + r % 900;
            r = zt_rand(&s);
            a.wan_up = (uint8_t) (r % 5 != 0);
            r = zt_rand(&s);
            a.medium = r % 25 == 0 ? ZTM_MEDIUM_AMATEUR : (uint8_t) (1 + r % 4);
            r = zt_rand(&s);
            zind_qty_make((int64_t) (r % 1000000), 1 + (int64_t) (r % 7), &a.offer_kbps);
            r = zt_rand(&s);
            a.latency_ms = (uint32_t) (r % 80);
            for (int d = 0; d < ND; d++) {
                r = zt_rand(&s);
                claim[d] = r % 25 == 0 ? ZXV_LP_BOTH
                           : r % 6 == 0 ? ZXV_LP_NEITHER
                           : r % 6 == 1 ? ZXV_LP_FALSE
                                        : ZXV_LP_TRUE;
                if (claim[d] == ZXV_LP_TRUE || claim[d] == ZXV_LP_BOTH) {
                    snprintf(a.claims[a.n_claims].name, sizeof a.claims[0].name, "%s", k_dest[d]);
                    a.claims[a.n_claims++].lp = ZXV_LP_TRUE;
                }
                if (claim[d] == ZXV_LP_FALSE || claim[d] == ZXV_LP_BOTH) {
                    snprintf(a.claims[a.n_claims].name, sizeof a.claims[0].name, "%s", k_dest[d]);
                    a.claims[a.n_claims++].lp = ZXV_LP_FALSE;
                }
            }
            r = zt_rand(&s);
            forged = r % 12 == 0;
            if (forged && j % 2 == 0)
                rc = ztm_advert_seal_ed25519(e.cap, &a, rogue_sk, buf, sizeof buf, &len);
            else if (forged)
                rc = ztm_advert_seal_mldsa(e.cap, &a, ml[(j + 2) % NB] ? ml[(j + 2) % NB] : ml[1],
                                           buf, sizeof buf, &len);
            else if (j % 2)
                rc = ztm_advert_seal_mldsa(e.cap, &a, ml[j], buf, sizeof buf, &len);
            else
                rc = ztm_advert_seal_ed25519(e.cap, &a, sk[j], buf, sizeof buf, &len);
            if (rc != ZXV_OK) die("seal", it);
            if (forged) want = ZXV_E_VERIFY;
            else if (a.seq <= m->last_seq) want = ZIND_E_REPLAY;
            else if (now > a.expires || a.issued > now + ZTM_SKEW_S) want = ZTM_E_EXPIRED;
            else if (a.medium == ZTM_MEDIUM_AMATEUR) want = ZTM_E_MEDIUM;
            else {
                m->has_adv = 1;
                m->seq = m->last_seq = a.seq;
                m->expires = a.expires;
                m->wan = a.wan_up;
                m->offer = a.offer_kbps;
                for (int d = 0; d < ND; d++) {
                    m->ev[d].advert_lp = claim[d];
                    if (m->ev[d].has_override && m->ev[d].override_seq != a.seq)
                        m->ev[d].has_override = 0;
                }
                want = m_hold(m, now) ? ZIND_E_HELD : ZXV_OK;
            }
            rc = ztm_advert_accept(p, buf, len, now);
            if (rc != want) {
                fprintf(stderr, "advert %s seq %llu -> %s, model %s\n", k_nb[j],
                        (unsigned long long) a.seq, ztm_status_name(rc), ztm_status_name(want));
                die("advert status differs from the model", it);
            }
            g_n[rc == ZXV_OK ? 0 : rc == ZIND_E_HELD ? 1 : rc == ZIND_E_REPLAY ? 2
                : rc == ZTM_E_EXPIRED ? 3 : rc == ZXV_E_VERIFY ? 4 : 5]++;
        } else if (op < 60) {
            /* a probe */
            int d;
            int32_t lp, want, rc;
            mev_t *ev;
            r = zt_rand(&s);
            d = (int) (r % ND);
            lp = r % 3 == 0 ? ZXV_LP_FALSE : ZXV_LP_TRUE;
            ev = &m->ev[d];
            ev->has_probe = 1;
            ev->probe_lp = lp;
            ev->probe_time = now;
            want = ev->held ? ZIND_E_HELD : m_hold(m, now) ? ZIND_E_HELD : ZXV_OK;
            rc = ztm_probe_record(p, k_nb[j], k_dest[d], lp, now);
            if (rc != want) die("probe status differs from the model", it);
            g_n[6]++;
        } else if (op < 70) {
            /* the owner resolves a hold, if there is one */
            int jj = -1, d = -1;
            for (int a = 0; a < NB && jj < 0; a++)
                for (int b = 0; b < ND && jj < 0; b++)
                    if (g_m[(j + a) % NB].ev[b].held) {
                        jj = (j + a) % NB;
                        d = b;
                    }
            if (jj >= 0) {
                uint8_t msg[200], sig[64];
                size_t ml_ = 0;
                int forged;
                int32_t dec, rc;
                r = zt_rand(&s);
                dec = r % 2 ? ZTM_USE : ZTM_AVOID;
                forged = r % 5 == 0;
                if (ztm_owner_message(p, k_nb[jj], k_dest[d], dec, msg, sizeof msg, &ml_) != ZXV_OK ||
                    zxv_ed25519_sign(e.cap, forged ? rogue_sk : owner_sk, msg, ml_, sig) != ZXV_OK)
                    die("owner message", it);
                rc = ztm_hold_resolve(p, k_nb[jj], k_dest[d], dec, sig);
                if (forged) {
                    if (rc != ZTM_E_OWNER) die("a forged resolution was accepted", it);
                    g_n[8]++;
                } else {
                    mev_t *ev = &g_m[jj].ev[d];
                    if (rc != ZXV_OK) die("a genuine resolution was refused", it);
                    ev->held = 0;
                    ev->has_override = 1;
                    ev->override_lp = dec == ZTM_USE ? ZXV_LP_TRUE : ZXV_LP_FALSE;
                    ev->override_seq = g_m[jj].has_adv ? g_m[jj].seq : 0;
                    ev->has_probe = 0;
                    g_n[7]++;
                }
            } else {
                uint8_t sig[64] = {0};
                if (ztm_hold_resolve(p, k_nb[j], "internet", ZTM_USE, sig) != ZXV_E_STATE)
                    die("a resolution with nothing held", it);
            }
        } else if (op < 75) {
            /* an agreement changes, or is withdrawn */
            zind_qty a = ZIND_QTY_ZERO;
            r = zt_rand(&s);
            if (r % 4) zind_qty_make((int64_t) (r % 2000000) + 1, 1 + (int64_t) (r % 5), &a);
            if (ztm_agreement_set(p, k_nb[j], a) != ZXV_OK) die("agreement", it);
            m->agreement = a;
            g_n[9]++;
        } else {
            /* a plan */
            ztm_demand dm[12];
            ztm_plan pl;
            int d, nd, wan, any = 0;
            int32_t rc, want;
            zind_qty b;
            r = zt_rand(&s);
            d = (int) (r % ND);
            nd = (int) ((r >> 8) % 12) + 1;
            wan = (r >> 16) % 10 == 0;
            for (int f = 0; f < nd; f++) {
                r = zt_rand(&s);
                snprintf(dm[f].flow, sizeof dm[f].flow, "flow%d", f);
                zind_qty_make((int64_t) (r % 100000) + 1, 1 + (int64_t) (r % 6), &dm[f].demand_kbps);
            }
            for (int k = 0; k < NB; k++) any += m_why(&g_m[k], d, now, &b) == ZXV_OK;
            want = wan ? ZTM_E_WAN_UP : any ? ZXV_OK : ZTM_E_NO_RELAY;
            rc = ztm_plan_make(p, k_dest[d], now, wan, dm, (uint32_t) nd, &pl);
            if (rc != want) die("plan status differs from the model", it);
            if (rc == ZXV_OK) {
                if (pl.eligible != (uint32_t) any || pl.n != (uint32_t) nd)
                    die("plan eligibility differs from the model", it);
                check_plan(p, dm, (uint32_t) nd, &pl, d, now, it);
                g_n[10]++;
                g_n[12] += pl.satisfied;
                g_n[13] += (unsigned long) nd;
            } else {
                g_n[11]++;
            }
        }
        check_views(p, now, it);
    }
    if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    {
        uint32_t nq = 0;
        zxv_hexlog_count(qlog, &nq);
        printf("fuzz_modem: %lu ops (adverts: taken %lu, held %lu, replays %lu, expired %lu, "
               "forged %lu, amateur %lu; probes %lu; owner resolutions %lu, forged %lu; agreements "
               "%lu; plans %lu with %lu of %lu flows satisfied, %lu refused; log entries %u), seed "
               "%llu, 0 invariant failures\n",
               iters, g_n[0], g_n[1], g_n[2], g_n[3], g_n[4], g_n[5], g_n[6], g_n[7], g_n[8],
               g_n[9], g_n[10], g_n[12], g_n[13], g_n[11], nq, (unsigned long long) seed);
    }
    ztm_planner_destroy(p);
    zxv_hexlog_destroy(qlog);
    for (int j = 0; j < NB; j++) zind_signer_destroy(ml[j]);
    zt_env_close(&e);
    return 0;
}
