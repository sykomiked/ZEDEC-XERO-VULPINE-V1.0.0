/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_command.c — a deterministic operation fuzzer for mesh-command
 * against an independent model. Five units sign numbered reports about six
 * facts; the mesh delivers them late, out of order, repeatedly and with
 * flipped bits; units sometimes equivocate (two reports, one number).
 * Commanders resolve held facts with genuine, forged, misdirected and
 * stale decisions (signed, then new evidence arrives before it is
 * applied). Tasks and resources are added, the advice is compared with
 * the model's, and commanders approve genuine, forged and blocked
 * assignments.
 *
 * The model keeps each unit's latest claim per fact and epoch, the
 * (unit, number) map, decisions and assignments. After every operation
 * the status and every fact's state (lp, counts, epoch, decision, reports)
 * must equal the model; the journal is replayed offline every 250
 * operations and at the end. One random draw per statement, so gcc and
 * clang print the same bytes.
 *
 *   fuzz_command ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zmcommand.h"

#define NU    5
#define NF    6
#define NC    2
#define NRES0 5
#define MAXR  32
#define MAXP  8192
#define MAXT  64

static const char *k_unit[NU] = {"U-0", "U-1", "U-2", "U-3", "U-4"};
static const char *k_fact[NF] = {"road:R1/open", "road:R2/open", "bridge:B4/ok",
                                 "gas:main/leak", "power:grid/up", "shelter:S2/full"};
static const char *k_cmd[NC] = {"IC-A", "IC-B"};
static const char *k_res0[NRES0] = {"ENG-1", "ENG-2", "AMB-1", "ENG-3", "AMB-2"};
static const char *k_kind0[NRES0] = {"engine", "engine", "ambulance", "engine", "ambulance"};
/* resources: the five above, then more arrive during the incident */
static char k_res[MAXR][16];
static const char *k_kind[MAXR];
static int g_nres;
static const char *k_why[3] = {"aerial photo", "crew on scene confirms", "utility called it"};
static const char *INC = "FUZZ-INCIDENT";

typedef struct {
    int u, f;
    uint64_t no;
    int32_t claim;
} pr_t;
static pr_t g_pool[MAXP];
static int g_np;

/* the model */
typedef struct {
    int has;
    uint64_t no;
    uint32_t epoch;
    int32_t claim;
} mclaim_t;
static struct {
    mclaim_t c[NU];
    uint32_t epoch, reports;
    int32_t decision, lp;
    int exists; /* reported, or named by a task */
} g_f[NF];
static struct {
    uint64_t no;
    int f;
    int32_t claim;
} g_seen[MAXP];
static int g_nseen;
static uint64_t g_next[NU];
static struct {
    char id[16];
    uint8_t pri;
    int kind; /* 0 engine, 1 ambulance */
    int f;    /* -1 none */
    int32_t req;
    int assigned; /* resource + 1 */
} g_t[MAXT];
static int g_nt;
static int g_busy[MAXR];
static uint32_t g_decisions, g_approvals, g_reports;

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_command: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what,
            zt_last_err());
    abort();
}

static uint64_t draw(uint64_t *s, uint64_t n) {
    uint64_t v = zt_rand(s);
    return v % n;
}

static void model_eval(int f) {
    uint32_t pos = 0, neg = 0;
    for (int u = 0; u < NU; u++) {
        if (!g_f[f].c[u].has || g_f[f].c[u].epoch != g_f[f].epoch) continue;
        if (g_f[f].c[u].claim == ZXV_LP_TRUE) pos++;
        else neg++;
    }
    if (g_f[f].decision == ZXV_LP_TRUE) pos++;
    if (g_f[f].decision == ZXV_LP_FALSE) neg++;
    g_f[f].lp = pos && neg ? ZXV_LP_BOTH : pos ? ZXV_LP_TRUE : neg ? ZXV_LP_FALSE : ZXV_LP_NEITHER;
}

/* Expected status of ingesting p, and apply it to the model when OK. */
static int32_t model_ingest(const pr_t *p) {
    mclaim_t *c;
    for (int i = 0; i < g_nseen; i++) {
        if ((int) (g_seen[i].f >> 8) != p->u || g_seen[i].no != p->no) continue;
        if ((g_seen[i].f & 0xff) == p->f && g_seen[i].claim == p->claim) return ZIND_E_REPLAY;
        return ZMC_E_EQUIVOCATION;
    }
    g_seen[g_nseen].no = p->no;
    g_seen[g_nseen].f = (p->u << 8) | p->f;
    g_seen[g_nseen].claim = p->claim;
    g_nseen++;
    g_reports++;
    g_f[p->f].reports++;
    g_f[p->f].exists = 1;
    c = &g_f[p->f].c[p->u];
    if (c->has && p->no < c->no) return ZXV_OK;
    c->has = 1;
    c->no = p->no;
    c->epoch = g_f[p->f].epoch;
    c->claim = p->claim;
    model_eval(p->f);
    return ZXV_OK;
}

static size_t sign_report(const zxv_cap *cap, zind_signer *const *key, const pr_t *p,
                          uint8_t *buf, size_t cap_len) {
    zmc_report r;
    size_t len = 0;
    memset(&r, 0, sizeof r);
    snprintf(r.fact, sizeof r.fact, "%s", k_fact[p->f]);
    r.claim = p->claim;
    r.observed = 1700000000ULL + p->no;
    r.lat_e6 = ZMC_NO_POS;
    r.lon_e6 = ZMC_NO_POS;
    snprintf(r.note, sizeof r.note, "unit %d", p->u);
    if (zmc_report_sign(cap, key[p->u], INC, k_unit[p->u], p->no, &r, buf, cap_len, &len) !=
        ZXV_OK)
        return 0;
    return len;
}

static void compare(const zmc_incident *inc, unsigned long it) {
    for (int f = 0; f < NF; f++) {
        zmc_fact_view v;
        int32_t st = zmc_fact_state(inc, k_fact[f], &v);
        uint32_t pos = 0, neg = 0;
        if (st == ZMC_E_FACT) {
            if (g_f[f].exists) die("a known fact is missing", it);
            continue;
        }
        if (st != ZXV_OK) die("fact_state", it);
        for (int u = 0; u < NU; u++) {
            if (!g_f[f].c[u].has || g_f[f].c[u].epoch != g_f[f].epoch) continue;
            if (g_f[f].c[u].claim == ZXV_LP_TRUE) pos++;
            else neg++;
        }
        if (v.lp != g_f[f].lp || v.pos != pos || v.neg != neg || v.epoch != g_f[f].epoch ||
            v.decision != g_f[f].decision || v.reports != g_f[f].reports) {
            fprintf(stderr, "fact %s: lp %d/%d pos %u/%u neg %u/%u epoch %u/%u dec %d/%d rep %u/%u\n",
                    k_fact[f], (int) v.lp, (int) g_f[f].lp, v.pos, pos, v.neg, neg, v.epoch,
                    g_f[f].epoch, (int) v.decision, (int) g_f[f].decision, v.reports,
                    g_f[f].reports);
            die("fact state differs from the model", it);
        }
    }
}

static int gate(int t) {
    int32_t lp;
    if (g_t[t].f < 0) return ZMC_REC_ASSIGN;
    lp = g_f[g_t[t].f].lp;
    if (lp == ZXV_LP_BOTH) return ZMC_REC_HELD;
    if (lp == ZXV_LP_NEITHER) return ZMC_REC_UNKNOWN;
    if (lp != g_t[t].req) return ZMC_REC_BLOCKED;
    return ZMC_REC_ASSIGN;
}

static void check_advice(const zmc_incident *inc, unsigned long it) {
    zmc_rec out[MAXT];
    size_t n = 0, k = 0;
    int used[MAXT] = {0}, taken[MAXR];
    if (zmc_recommend(inc, out, MAXT, &n) != ZXV_OK) die("recommend", it);
    for (int r = 0; r < g_nres; r++) taken[r] = g_busy[r];
    for (;;) {
        int best = -1, st, pick = -1;
        for (int t = 0; t < g_nt; t++) {
            if (used[t] || g_t[t].assigned) continue;
            if (best < 0 || g_t[t].pri > g_t[best].pri ||
                (g_t[t].pri == g_t[best].pri && strcmp(g_t[t].id, g_t[best].id) < 0))
                best = t;
        }
        if (best < 0) break;
        used[best] = 1;
        if (k >= n || strcmp(out[k].task, g_t[best].id) != 0) die("advice order", it);
        st = gate(best);
        if (st == ZMC_REC_ASSIGN) {
            for (int r = 0; r < g_nres; r++) {
                if (taken[r] || strcmp(k_kind[r], g_t[best].kind ? "ambulance" : "engine") != 0)
                    continue;
                if (pick < 0 || strcmp(k_res[r], k_res[pick]) < 0) pick = r;
            }
            if (pick < 0) st = ZMC_REC_NO_RESOURCE;
            else taken[pick] = 1;
        }
        if (out[k].status != st) die("advice status", it);
        if (strcmp(out[k].resource, pick >= 0 && st == ZMC_REC_ASSIGN ? k_res[pick] : "") != 0)
            die("advice resource", it);
        k++;
    }
    if (k != n) die("advice count", it);
}

static void verify_journal(const zxv_cap *cap, const zmc_incident *inc, zind_signer *op,
                           zind_signer *const *key, zind_signer *const *cmd, unsigned long it) {
    const uint8_t *upk[NU], *cpk[NC];
    zmc_trust t;
    zmc_verdict v;
    uint8_t *b;
    size_t len = 0;
    int32_t st;
    for (int u = 0; u < NU; u++) upk[u] = zind_signer_pk(key[u]);
    for (int c = 0; c < NC; c++) cpk[c] = zind_signer_pk(cmd[c]);
    memset(&t, 0, sizeof t);
    t.unit_ids = k_unit;
    t.unit_pks = upk;
    t.n_units = NU;
    t.commander_ids = k_cmd;
    t.commander_pks = cpk;
    t.n_commanders = NC;
    t.operator_pk = zind_signer_pk(op);
    if (zind_chain_export(zmc_journal(inc), NULL, 0, &len) != ZXV_E_BUFFER) die("export size", it);
    b = (uint8_t *) malloc(len);
    if (!b) die("malloc", it);
    if (zind_chain_export(zmc_journal(inc), b, len, &len) != ZXV_OK) die("export", it);
    st = zmc_verify_journal(cap, b, len, &t, &v);
    free(b);
    if (st != ZXV_OK) die("journal replay", it);
    if (v.reports != g_reports || v.decisions != g_decisions || v.approvals != g_approvals)
        die("journal verdict differs from the model", it);
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x3e5c0ddULL), s = seed;
    zt_env e;
    zind_signer *op, *rogue, *key[NU], *cmd[NC];
    zxv_hexlog *qlog = NULL;
    zmc_incident *inc = NULL;
    static uint8_t buf[5000];
    uint8_t sig[ZIND_SIG_BYTES], msg[512];
    size_t len = 0, mlen = 0;
    unsigned long n_new = 0, n_ok = 0, n_stale = 0, n_replay = 0, n_equiv = 0, n_flip = 0,
                  n_holds = 0, n_res = 0, n_forged = 0, n_stalecut = 0, n_notheld = 0, n_tasks = 0,
                  n_adv = 0, n_app = 0, n_blocked = 0, n_appforged = 0, n_verify = 0;

    if (zt_env_open(&e, NULL) != 0) return 2;
    op = zt_signer(e.cap, 1);
    rogue = zt_signer(e.cap, 2);
    if (!op || !rogue || zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        zmc_incident_create(e.cap, INC, op, qlog, &inc) != ZXV_OK)
        return 2;
    for (int u = 0; u < NU; u++) {
        key[u] = zt_signer(e.cap, (uint8_t) (10 + u));
        if (!key[u] || zmc_unit_add(inc, k_unit[u], zind_signer_pk(key[u])) != ZXV_OK) return 2;
        g_next[u] = 1;
    }
    for (int c = 0; c < NC; c++) {
        cmd[c] = zt_signer(e.cap, (uint8_t) (30 + c));
        if (!cmd[c] || zmc_commander_add(inc, k_cmd[c], zind_signer_pk(cmd[c])) != ZXV_OK)
            return 2;
    }
    for (int r = 0; r < NRES0; r++) {
        snprintf(k_res[r], sizeof k_res[r], "%s", k_res0[r]);
        k_kind[r] = k_kind0[r];
        if (zmc_resource_add(inc, k_res[r], k_kind[r]) != ZXV_OK) return 2;
        g_nres++;
    }
    for (int f = 0; f < NF; f++) {
        g_f[f].decision = ZXV_LP_NEITHER;
        g_f[f].lp = ZXV_LP_NEITHER;
    }

    for (unsigned long it = 0; it < iters && g_np < MAXP - 4 && g_nseen < MAXP - 4; it++) {
        uint64_t op_ = draw(&s, 100);
        if (op_ < 30) {
            /* a unit signs a new report; it is sent now or held back */
            pr_t *p = &g_pool[g_np];
            uint64_t now;
            p->u = (int) draw(&s, NU);
            p->f = (int) draw(&s, NF);
            p->claim = draw(&s, 2) ? ZXV_LP_TRUE : ZXV_LP_FALSE;
            p->no = g_next[p->u];
            g_next[p->u] += 1 + draw(&s, 3);
            g_np++;
            n_new++;
            now = draw(&s, 2);
            if (now) {
                zmc_ingest_info info;
                int32_t want, got;
                int32_t before = g_f[p->f].lp;
                len = sign_report(e.cap, key, p, buf, sizeof buf);
                if (!len) die("sign", it);
                want = model_ingest(p);
                got = zmc_ingest(inc, buf, len, &info);
                if (got != want) die("new report status", it);
                if (info.new_hold != (g_f[p->f].lp == ZXV_LP_BOTH && before != ZXV_LP_BOTH))
                    die("new_hold", it);
                if (info.new_hold) n_holds++;
                n_ok++;
            }
        } else if (op_ < 55) {
            /* the mesh delivers a pooled report: late, again, or corrupted */
            int i, flip;
            int32_t want, got;
            zmc_ingest_info info;
            if (g_np == 0) continue;
            i = (int) draw(&s, (uint64_t) g_np);
            flip = draw(&s, 8) == 0;
            len = sign_report(e.cap, key, &g_pool[i], buf, sizeof buf);
            if (!len) die("sign", it);
            if (flip) {
                size_t at = (size_t) draw(&s, len);
                uint8_t bit = (uint8_t) (1u << draw(&s, 8));
                buf[at] ^= bit;
                got = zmc_ingest(inc, buf, len, NULL);
                if (got == ZXV_OK || got == ZIND_E_REPLAY || got == ZMC_E_EQUIVOCATION)
                    die("a corrupted report was taken", it);
                n_flip++;
            } else {
                want = model_ingest(&g_pool[i]);
                got = zmc_ingest(inc, buf, len, &info);
                if (got != want) die("delivery status", it);
                if (got == ZXV_OK && info.stale) n_stale++;
                if (got == ZXV_OK && info.new_hold) n_holds++;
                if (got == ZXV_OK) n_ok++;
                if (got == ZIND_E_REPLAY) n_replay++;
                if (got == ZMC_E_EQUIVOCATION) n_equiv++;
            }
        } else if (op_ < 60) {
            /* a unit equivocates: a pooled number, another claim */
            pr_t *p = &g_pool[g_np];
            int i;
            if (g_np == 0) continue;
            i = (int) draw(&s, (uint64_t) g_np);
            *p = g_pool[i];
            p->claim = p->claim == ZXV_LP_TRUE ? ZXV_LP_FALSE : ZXV_LP_TRUE;
            g_np++;
        } else if (op_ < 72) {
            /* a commander resolves a fact */
            int f = (int) draw(&s, NF);
            int c = (int) draw(&s, NC);
            int32_t dec = draw(&s, 2) ? ZXV_LP_TRUE : ZXV_LP_FALSE;
            const char *why = k_why[draw(&s, 3)];
            uint64_t how = draw(&s, 10);
            int32_t want, got;
            mclaim_t snap[NU];
            uint32_t ep = g_f[f].epoch;
            if (g_f[f].lp != ZXV_LP_BOTH) {
                memset(sig, 0, sizeof sig);
                if (zmc_decision_message(inc, k_fact[f], dec, k_cmd[c], why, msg, sizeof msg,
                                         &mlen) != (g_f[f].exists ? ZXV_E_STATE : ZMC_E_FACT))
                    die("decision message on an unheld fact", it);
                got = zmc_resolve(inc, k_fact[f], dec, k_cmd[c], why, sig);
                if (got != (g_f[f].exists ? ZXV_E_STATE : ZMC_E_FACT)) die("unheld resolve", it);
                n_notheld++;
                continue;
            }
            if (zmc_decision_message(inc, k_fact[f], dec, k_cmd[c], why, msg, sizeof msg, &mlen) !=
                ZXV_OK)
                die("decision message", it);
            if (zind_sign(e.cap, how == 0 ? rogue : how == 1 ? cmd[1 - c] : cmd[c], msg, mlen,
                          ZMC_DECISION_CTX, sig) != ZXV_OK)
                die("sign decision", it);
            want = how <= 1 ? ZXV_E_VERIFY : ZXV_OK;
            if (how <= 1) n_forged++;
            if (how == 2) {
                /* new evidence about this fact arrives between signing and applying */
                int i = g_np;
                g_pool[i].u = (int) draw(&s, NU);
                g_pool[i].f = f;
                g_pool[i].claim = draw(&s, 2) ? ZXV_LP_TRUE : ZXV_LP_FALSE;
                g_pool[i].no = g_next[g_pool[i].u];
                g_next[g_pool[i].u] += 1;
                g_np++;
                n_new++;
                memcpy(snap, g_f[f].c, sizeof snap);
                len = sign_report(e.cap, key, &g_pool[i], buf, sizeof buf);
                if (!len) die("sign", it);
                got = zmc_ingest(inc, buf, len, NULL);
                if (got != model_ingest(&g_pool[i])) die("delivery between", it);
                if (g_f[f].lp != ZXV_LP_BOTH) {
                    want = ZXV_E_STATE;
                } else {
                    for (int u = 0; u < NU; u++) {
                        int a = snap[u].has && snap[u].epoch == ep;
                        int b = g_f[f].c[u].has && g_f[f].c[u].epoch == ep;
                        if (a != b || (a && (snap[u].no != g_f[f].c[u].no ||
                                             snap[u].claim != g_f[f].c[u].claim)))
                            want = ZXV_E_VERIFY;
                    }
                }
                if (want != ZXV_OK) n_stalecut++;
            }
            got = zmc_resolve(inc, k_fact[f], dec, k_cmd[c], why, sig);
            if (got != want) die("resolve status", it);
            if (got == ZXV_OK) {
                g_f[f].epoch++;
                g_f[f].decision = dec;
                g_decisions++;
                model_eval(f);
                n_res++;
            }
        } else if (op_ < 74) {
            /* another resource arrives */
            int r = g_nres;
            int kind;
            if (g_nres >= MAXR) continue;
            kind = (int) draw(&s, 2);
            snprintf(k_res[r], sizeof k_res[r], "%s-%02d", kind ? "AMB" : "ENG", (r * 5) % MAXR);
            k_kind[r] = kind ? "ambulance" : "engine";
            if (zmc_resource_add(inc, k_res[r], k_kind[r]) != ZXV_OK) die("resource_add", it);
            g_nres++;
        } else if (op_ < 78) {
            /* a task */
            int t = g_nt;
            int pri, kind, f, has;
            int32_t req;
            if (g_nt >= MAXT) continue;
            pri = (int) (1 + draw(&s, 9));
            kind = (int) draw(&s, 2);
            has = draw(&s, 3) != 0;
            f = (int) draw(&s, NF);
            req = draw(&s, 2) ? ZXV_LP_TRUE : ZXV_LP_FALSE;
            snprintf(g_t[t].id, sizeof g_t[t].id, "T-%02d", (int) ((t * 7) % MAXT));
            g_t[t].pri = (uint8_t) pri;
            g_t[t].kind = kind;
            g_t[t].f = has ? f : -1;
            g_t[t].req = has ? req : ZXV_LP_NEITHER;
            g_t[t].assigned = 0;
            if (zmc_task_add(inc, g_t[t].id, (uint8_t) pri, kind ? "ambulance" : "engine",
                             has ? k_fact[f] : NULL, req) != ZXV_OK)
                die("task_add", it);
            if (has) g_f[f].exists = 1;
            g_nt++;
            n_tasks++;
        } else if (op_ < 86) {
            check_advice(inc, it);
            n_adv++;
        } else {
            /* a commander approves an assignment */
            int t, r, c, forged, blocked;
            int32_t got, want;
            if (g_nt == 0) continue;
            t = (int) draw(&s, (uint64_t) g_nt);
            r = (int) draw(&s, (uint64_t) g_nres);
            c = (int) draw(&s, NC);
            forged = draw(&s, 6) == 0;
            if (draw(&s, 2)) {
                /* follow the advice: its first recommended assignment */
                zmc_rec out[MAXT];
                size_t n = 0;
                if (zmc_recommend(inc, out, MAXT, &n) != ZXV_OK) die("recommend", it);
                for (size_t k = 0; k < n; k++) {
                    if (out[k].status != ZMC_REC_ASSIGN) continue;
                    for (int q = 0; q < g_nt; q++)
                        if (strcmp(g_t[q].id, out[k].task) == 0) t = q;
                    for (int q = 0; q < g_nres; q++)
                        if (strcmp(k_res[q], out[k].resource) == 0) r = q;
                    break;
                }
            }
            if (zmc_approve_message(inc, g_t[t].id, k_res[r], k_cmd[c], msg, sizeof msg, &mlen) !=
                ZXV_OK)
                die("approve message", it);
            if (zind_sign(e.cap, forged ? rogue : cmd[c], msg, mlen, ZMC_APPROVE_CTX, sig) != ZXV_OK)
                die("sign approval", it);
            blocked = g_t[t].assigned || g_busy[r] ||
                      strcmp(k_kind[r], g_t[t].kind ? "ambulance" : "engine") != 0 ||
                      gate(t) != ZMC_REC_ASSIGN;
            want = blocked ? ZMC_E_BLOCKED : forged ? ZXV_E_VERIFY : ZXV_OK;
            got = zmc_approve(inc, g_t[t].id, k_res[r], k_cmd[c], sig);
            if (got != want) die("approve status", it);
            if (got == ZXV_OK) {
                g_t[t].assigned = r + 1;
                g_busy[r] = 1;
                g_approvals++;
                n_app++;
            }
            if (got == ZMC_E_BLOCKED) n_blocked++;
            if (got == ZXV_E_VERIFY) n_appforged++;
        }
        compare(inc, it);
        if (it % 250 == 249) {
            verify_journal(e.cap, inc, op, key, cmd, it);
            n_verify++;
        }
    }
    verify_journal(e.cap, inc, op, key, cmd, iters);
    if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    {
        unsigned long held = 0;
        for (int f = 0; f < NF; f++)
            if (g_f[f].lp == ZXV_LP_BOTH) held++;
        printf("fuzz_command: %lu ops (reports signed %lu, accepted %lu incl. %lu stale, replays "
               "%lu, equivocations %lu, corrupted %lu, holds %lu, decisions %lu, forged %lu, "
               "stale or overtaken %lu, unheld %lu, tasks %lu, advice checks %lu, approvals %lu, "
               "blocked %lu, forged approvals %lu, journal replays %lu, held at end %lu), seed "
               "%llu, 0 invariant failures\n",
               iters, n_new, n_ok, n_stale, n_replay, n_equiv, n_flip, n_holds, n_res, n_forged,
               n_stalecut, n_notheld, n_tasks, n_adv, n_app, n_blocked, n_appforged, n_verify + 1,
               held, (unsigned long long) seed);
    }
    for (int u = 0; u < NU; u++) zind_signer_destroy(key[u]);
    for (int c = 0; c < NC; c++) zind_signer_destroy(cmd[c]);
    zmc_incident_destroy(inc);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(op);
    zind_signer_destroy(rogue);
    zt_env_close(&e);
    return 0;
}
