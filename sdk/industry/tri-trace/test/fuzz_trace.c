/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_trace.c — a deterministic operation fuzzer for tri-trace against an
 * independent custody model. Random commissions, shipments, receipts and
 * dispensings by random parties at random (sometimes backwards) times,
 * scans by a key that is not the named party, exact replays of earlier
 * scans, and reviews (genuine, forged, by an unregistered key, for the
 * wrong outcome). After every operation the returned status, the item
 * state and its holder equal the model. Every 100 operations a random
 * item's chain is exported and verified offline against the model, and
 * every 400 operations a random byte of an export is flipped and the
 * verifier must refuse it. At the end every chain is verified.
 *
 *   fuzz_trace ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zttrace.h"

#define NT 6
#define NP 4

static const char *k_base[NT] = {"01.0950001.1001", "01.0950001.1002", "SN:A-3",
                                 "SN:A-4", "lux/bag/77", "lux/bag/78"};
/* slot t holds tag k_tag[t]; a dispensed or counterfeit tag is retired and
 * the slot moves on to a fresh serial */
static char k_tag[NT][40];
static unsigned g_gen[NT];
static const char *k_party[NP] = {"MAKER", "WHOLESALE", "PHARMACY", "CLINIC"};

typedef struct {
    int state; /* 0 = never commissioned, else ZTT_ST_* */
    char holder[ZTT_PARTY_MAX + 1], to[ZTT_PARTY_MAX + 1];
    uint64_t last_time;
    uint32_t events, conflicts;
} model_t;

static model_t g_m[NT];

/* the model's set of recorded scans (64-bit fingerprints; open addressing) */
static uint64_t *g_seen;
static size_t g_seen_cap;

static uint64_t fp(int t, int32_t kind, int p, const char *to, uint64_t time) {
    uint64_t h = 1469598103934665603ULL;
    char b[160];
    int n = snprintf(b, sizeof b, "%s|%d|%s|%s|%llu", k_tag[t], (int) kind, k_party[p], to,
                     (unsigned long long) time);
    for (int i = 0; i < n; i++) h = (h ^ (uint8_t) b[i]) * 1099511628211ULL;
    return h | 1;
}

static int seen_has(uint64_t h) {
    for (size_t i = h & (g_seen_cap - 1);; i = (i + 1) & (g_seen_cap - 1)) {
        if (g_seen[i] == h) return 1;
        if (!g_seen[i]) return 0;
    }
}

static void seen_add(uint64_t h) {
    size_t i = h & (g_seen_cap - 1);
    while (g_seen[i] && g_seen[i] != h) i = (i + 1) & (g_seen_cap - 1);
    g_seen[i] = h;
}

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_trace: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    abort();
}

/* What the registry must answer for this scan, applied to the model. */
static int32_t model_scan(int t, int p, int32_t kind, const char *to, uint64_t time) {
    model_t *m = &g_m[t];
    uint64_t h = fp(t, kind, p, to, time);
    int ok = 0;
    if (seen_has(h)) return ZIND_E_REPLAY;
    if (m->state == 0 && kind != ZTT_EV_COMMISSION) return ZTT_E_ITEM;
    if (m->state == ZTT_ST_COUNTERFEIT) return ZIND_E_FROZEN;
    if (m->state == ZTT_ST_CONFLICT) return ZIND_E_HELD;
    if (m->state == 0) ok = 1;
    else if (time >= m->last_time) {
        if (kind == ZTT_EV_SHIP)
            ok = m->state == ZTT_ST_HELD && !strcmp(m->holder, k_party[p]) && strcmp(to, k_party[p]);
        else if (kind == ZTT_EV_RECEIVE)
            ok = m->state == ZTT_ST_TRANSIT && !strcmp(m->to, k_party[p]);
        else if (kind == ZTT_EV_DISPENSE)
            ok = m->state == ZTT_ST_HELD && !strcmp(m->holder, k_party[p]);
    }
    seen_add(h);
    m->events++;
    if (!ok) {
        m->state = ZTT_ST_CONFLICT;
        m->conflicts++;
        return ZTT_E_CONFLICT;
    }
    m->last_time = time;
    switch (kind) {
    case ZTT_EV_COMMISSION:
        m->state = ZTT_ST_HELD;
        snprintf(m->holder, sizeof m->holder, "%s", k_party[p]);
        break;
    case ZTT_EV_SHIP:
        m->state = ZTT_ST_TRANSIT;
        snprintf(m->to, sizeof m->to, "%s", to);
        m->holder[0] = 0;
        break;
    case ZTT_EV_RECEIVE:
        m->state = ZTT_ST_HELD;
        snprintf(m->holder, sizeof m->holder, "%s", k_party[p]);
        m->to[0] = 0;
        break;
    default: m->state = ZTT_ST_DISPENSED; break;
    }
    return ZXV_OK;
}

static void check_state(const ztt_registry *r, int t, unsigned long it) {
    int32_t st = 0;
    char h[ZTT_PARTY_MAX + 1];
    int32_t rc = ztt_item_state(r, k_tag[t], &st, NULL, h, sizeof h);
    if (g_m[t].state == 0) {
        if (rc != ZTT_E_ITEM) die("an uncommissioned tag exists", it);
        return;
    }
    if (rc != ZXV_OK || st != g_m[t].state) die("item state differs from the model", it);
    if (strcmp(h, g_m[t].holder)) die("holder differs from the model", it);
}

static uint8_t *export_item(const ztt_registry *r, int t, size_t *len) {
    uint8_t *buf;
    if (ztt_item_export(r, k_tag[t], NULL, 0, len) != ZXV_E_BUFFER) return NULL;
    buf = (uint8_t *) malloc(*len);
    if (buf && ztt_item_export(r, k_tag[t], buf, *len, len) != ZXV_OK) {
        free(buf);
        return NULL;
    }
    return buf;
}

static void check_offline(const zxv_cap *cap, const ztt_registry *r, const ztt_trust *tr, int t,
                          unsigned long it) {
    size_t len = 0;
    ztt_verdict v;
    uint8_t *buf;
    int32_t want_lp;
    if (g_m[t].state == 0) return;
    buf = export_item(r, t, &len);
    if (!buf) die("export", it);
    if (ztt_verify_export(cap, k_tag[t], buf, len, tr, &v) != ZXV_OK) die("offline verify", it);
    want_lp = g_m[t].state == ZTT_ST_CONFLICT      ? ZXV_LP_BOTH
              : g_m[t].state == ZTT_ST_COUNTERFEIT ? ZXV_LP_FALSE
                                                    : ZXV_LP_TRUE;
    if (v.lp != want_lp || v.state != g_m[t].state || strcmp(v.holder, g_m[t].holder) ||
        v.events != g_m[t].events || v.conflicts != g_m[t].conflicts ||
        v.bad_index != UINT32_MAX)
        die("offline verdict differs from the model", it);
    /* the same bytes presented as another tag */
    if (ztt_verify_export(cap, k_tag[(t + 1) % NT], buf, len, tr, &v) == ZXV_OK)
        die("a chain verified for another tag", it);
    free(buf);
}

static void check_mutation(const zxv_cap *cap, const ztt_registry *r, const ztt_trust *tr, int t,
                           uint64_t *s, unsigned long it) {
    size_t len = 0, pos;
    ztt_verdict v;
    uint8_t *buf;
    if (g_m[t].state == 0) return;
    buf = export_item(r, t, &len);
    if (!buf) die("export", it);
    uint8_t bit;
    pos = (size_t) (zt_rand(s) % len);
    bit = (uint8_t) (1u << (zt_rand(s) % 8));
    buf[pos] ^= bit;
    if (ztt_verify_export(cap, k_tag[t], buf, len, tr, &v) == ZXV_OK)
        die("a chain with a flipped byte verified", it);
    /* restored, then cut short by one byte */
    buf[pos] ^= bit;
    if (ztt_verify_export(cap, k_tag[t], buf, len, tr, &v) != ZXV_OK)
        die("the restored chain did not verify", it);
    if (ztt_verify_export(cap, k_tag[t], buf, len - 1, tr, &v) == ZXV_OK)
        die("a truncated chain verified", it);
    free(buf);
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x7ace5ULL), s = seed, clock = 1700000000ULL;
    zt_env e;
    zind_signer *op, *rev, *rogue, *party[NP];
    const uint8_t *pks[NP], *rev_pks[1];
    zxv_hexlog *qlog = NULL;
    ztt_registry *r = NULL;
    ztt_trust tr;
    unsigned long n_ok = 0, n_conf = 0, n_replay = 0, n_held = 0, n_frozen = 0, n_party = 0,
                  n_item = 0, n_rev = 0, n_forged = 0, n_verify = 0, n_retired = 0;
    struct {
        int t, p;
        int32_t kind;
        char to[ZTT_PARTY_MAX + 1];
        uint64_t time;
        int valid;
    } last = {0, 0, 0, "", 0, 0};

    g_seen_cap = 64;
    while (g_seen_cap < 4 * (size_t) iters + 64) g_seen_cap <<= 1;
    g_seen = (uint64_t *) calloc(g_seen_cap, sizeof *g_seen);
    if (!g_seen || zt_env_open(&e, NULL) != 0) return 2;
    op = zt_signer(e.cap, 1);
    rev = zt_signer(e.cap, 2);
    rogue = zt_signer(e.cap, 3);
    if (!op || !rev || !rogue || zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        ztt_registry_create(e.cap, op, qlog, &r) != ZXV_OK ||
        ztt_reviewer_add(r, zind_signer_pk(rev)) != ZXV_OK)
        return 2;
    for (int p = 0; p < NP; p++) {
        party[p] = zt_signer(e.cap, (uint8_t) (20 + p));
        if (!party[p]) return 2;
        pks[p] = zind_signer_pk(party[p]);
        if (ztt_party_add(r, k_party[p], pks[p]) != ZXV_OK) return 2;
    }
    rev_pks[0] = zind_signer_pk(rev);
    tr.party_ids = k_party;
    tr.party_pks = pks;
    tr.n_parties = NP;
    tr.reviewer_pks = rev_pks;
    tr.n_reviewers = 1;
    tr.operator_pk = zind_signer_pk(op);

    for (int t = 0; t < NT; t++) snprintf(k_tag[t], sizeof k_tag[t], "%s.g0", k_base[t]);
    for (unsigned long it = 0; it < iters; it++) {
        int t = (int) (zt_rand(&s) % NT), p = (int) (zt_rand(&s) % NP), op_ = (int) (zt_rand(&s) % 100);
        clock += 1 + zt_rand(&s) % 5;
        if ((g_m[t].state == ZTT_ST_DISPENSED || g_m[t].state == ZTT_ST_COUNTERFEIT) &&
            zt_rand(&s) % 4 == 0) {
            /* retire: the final chain verifies, then the slot takes a new serial */
            check_offline(e.cap, r, &tr, t, it);
            n_verify++;
            n_retired++;
            memset(&g_m[t], 0, sizeof g_m[t]);
            if (last.t == t) last.valid = 0;
            snprintf(k_tag[t], sizeof k_tag[t], "%s.g%u", k_base[t], ++g_gen[t]);
        }
        /* a frozen item mostly gets a review rather than more refused scans */
        if (g_m[t].state == ZTT_ST_CONFLICT && op_ >= 20 && zt_rand(&s) % 3 != 0)
            op_ = 11 + (int) (zt_rand(&s) % 9);
        if (op_ < 8 && last.valid) {
            /* an exact replay of the last recorded scan */
            int32_t rc = ztt_scan(r, k_tag[last.t], k_party[last.p], party[last.p], last.kind,
                                  last.kind == ZTT_EV_SHIP ? last.to : NULL, last.time);
            if (rc != ZIND_E_REPLAY) die("a replayed scan was not refused", it);
            n_replay++;
            t = last.t;
        } else if (op_ < 11) {
            /* a key that is not the named party */
            int32_t rc = ztt_scan(r, k_tag[t], k_party[p], zt_rand(&s) & 1 ? rogue : party[(p + 1) % NP],
                                  ZTT_EV_DISPENSE, NULL, clock);
            if (rc != ZTT_E_PARTY) die("an impersonating scan was not refused", it);
            n_party++;
        } else if (op_ < 20) {
            /* a review of the item (often not frozen: must be refused) */
            int outcome = zt_rand(&s) % 4 == 0 ? ZTT_COUNTERFEIT : ZTT_GENUINE, how = (int) (zt_rand(&s) % 6);
            const char *holder = outcome == ZTT_GENUINE ? k_party[p] : "";
            if (g_m[t].state != ZTT_ST_CONFLICT && zt_rand(&s) % 4 != 0)
                for (int q = 0; q < NT; q++)
                    if (g_m[q].state == ZTT_ST_CONFLICT) t = q;
            uint8_t msg[192], sig[ZIND_SIG_BYTES];
            size_t len = 0;
            int32_t rc = ztt_review_message(r, k_tag[t], outcome, holder, msg, sizeof msg, &len), want;
            if (g_m[t].state == 0) want = ZTT_E_ITEM;
            else if (g_m[t].state != ZTT_ST_CONFLICT) want = ZXV_E_STATE;
            else want = ZXV_OK;
            if (rc != want) die("review message status", it);
            if (rc == ZXV_OK) {
                const zind_signer *k = how == 0 ? rogue : rev;
                const uint8_t *claimed = how == 1 ? zind_signer_pk(rogue) : zind_signer_pk(rev);
                int32_t sign_outcome = how == 2 ? (outcome == ZTT_GENUINE ? ZTT_COUNTERFEIT : ZTT_GENUINE)
                                                : outcome;
                const char *sign_holder = sign_outcome == ZTT_GENUINE ? k_party[p] : "";
                if (ztt_review_message(r, k_tag[t], sign_outcome, sign_holder, msg, sizeof msg, &len) !=
                        ZXV_OK ||
                    zind_sign(e.cap, k, msg, len, ZTT_REVIEW_CTX, sig) != ZXV_OK)
                    die("signing a review", it);
                rc = ztt_review_resolve(r, k_tag[t], claimed, outcome, holder, sig);
                if (how == 0 || how == 2) {
                    if (rc != ZXV_E_VERIFY) die("a forged review was not refused", it);
                    n_forged++;
                } else if (how == 1) {
                    if (rc != ZTT_E_REVIEWER) die("an unregistered reviewer was accepted", it);
                    n_forged++;
                } else {
                    if (rc != ZXV_OK) die("a genuine review was refused", it);
                    g_m[t].events++;
                    if (outcome == ZTT_GENUINE) {
                        g_m[t].state = ZTT_ST_HELD;
                        snprintf(g_m[t].holder, sizeof g_m[t].holder, "%s", holder);
                        g_m[t].to[0] = 0;
                    } else {
                        g_m[t].state = ZTT_ST_COUNTERFEIT;
                    }
                    n_rev++;
                }
            } else {
                memset(sig, 0, sizeof sig);
                if (ztt_review_resolve(r, k_tag[t], zind_signer_pk(rev), outcome, holder, sig) != want)
                    die("a review of an unfrozen item was not refused", it);
            }
        } else {
            /* a scan: mostly what the history expects, sometimes not */
            int32_t kind, want, rc;
            const char *to = "";
            uint64_t time = clock;
            model_t *m = &g_m[t];
            int fit = zt_rand(&s) % 8 != 0;
            if (m->state == 0) kind = fit ? ZTT_EV_COMMISSION : (int32_t) (1 + zt_rand(&s) % 4);
            else if (!fit) kind = (int32_t) (1 + zt_rand(&s) % 4);
            else if (m->state == ZTT_ST_TRANSIT) {
                kind = ZTT_EV_RECEIVE;
                for (int q = 0; q < NP; q++)
                    if (!strcmp(k_party[q], m->to)) p = q;
            } else {
                kind = zt_rand(&s) % 5 == 0 ? ZTT_EV_DISPENSE : ZTT_EV_SHIP;
                for (int q = 0; q < NP; q++)
                    if (!strcmp(k_party[q], m->holder)) p = q;
            }
            if (kind == ZTT_EV_SHIP) to = k_party[fit ? (p + 1 + (int) (zt_rand(&s) % (NP - 1))) % NP
                                                      : (int) (zt_rand(&s) % NP)];
            if (zt_rand(&s) % 20 == 0 && m->last_time > 0) time = m->last_time - 1 - zt_rand(&s) % 3;
            want = model_scan(t, p, kind, to, time);
            rc = ztt_scan(r, k_tag[t], k_party[p], party[p], kind, kind == ZTT_EV_SHIP ? to : NULL, time);
            if (rc != want) {
                fprintf(stderr, "scan %s %s kind %d -> %s, model %s\n", k_tag[t], k_party[p], (int) kind,
                        ztt_status_name(rc), ztt_status_name(want));
                die("scan status differs from the model", it);
            }
            switch (rc) {
            case ZXV_OK: n_ok++; break;
            case ZTT_E_CONFLICT: n_conf++; break;
            case ZIND_E_REPLAY: n_replay++; break;
            case ZIND_E_HELD: n_held++; break;
            case ZIND_E_FROZEN: n_frozen++; break;
            default: n_item++; break;
            }
            if (rc == ZXV_OK || rc == ZTT_E_CONFLICT) {
                last.t = t;
                last.p = p;
                last.kind = kind;
                snprintf(last.to, sizeof last.to, "%s", to);
                last.time = time;
                last.valid = 1;
            }
        }
        check_state(r, t, it);
        if (it % 100 == 99) {
            check_offline(e.cap, r, &tr, (int) (zt_rand(&s) % NT), it);
            n_verify++;
        }
        if (it % 400 == 399) check_mutation(e.cap, r, &tr, (int) (zt_rand(&s) % NT), &s, it);
    }
    for (int t = 0; t < NT; t++) {
        check_state(r, t, iters);
        check_offline(e.cap, r, &tr, t, iters);
        n_verify++;
    }
    if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
    printf("fuzz_trace: %lu ops (scans %lu, conflicts %lu, replays %lu, held %lu, counterfeit "
           "refusals %lu, unknown tags %lu, impersonations %lu, reviews %lu, forged reviews %lu, "
           "offline verifications %lu, retired tags %lu), seed %llu, 0 invariant failures\n",
           iters, n_ok, n_conf, n_replay, n_held, n_frozen, n_item, n_party, n_rev, n_forged, n_verify,
           n_retired,
           (unsigned long long) seed);
    ztt_registry_destroy(r);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(op);
    zind_signer_destroy(rev);
    zind_signer_destroy(rogue);
    for (int p = 0; p < NP; p++) zind_signer_destroy(party[p]);
    zt_env_close(&e);
    free(g_seen);
    return 0;
}
