/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_triage.c — a deterministic operation fuzzer for vinea-triage
 * against an independent model. Four responders record triage for six
 * patients (sometimes building on a head they saw, on an older record, or
 * on a hash that means nothing), seal bundles, and deliver them over a
 * "link" that reorders, repeats and corrupts: a random unacknowledged or
 * old bundle each time, sometimes with a flipped bit. Reviewers settle
 * held patients with genuine, forged and wrong-category decisions.
 * Acknowledgements prune the outboxes.
 *
 * The model computes every patient's picture from the whole history (an
 * item is current if nothing references it and, for a record, its
 * responder sent nothing newer for that patient) and every responder's
 * chain state; after each operation the station's status, receipt and
 * every patient's state (TRUE/BOTH, categories, heads, records) must equal
 * the model. One random draw per statement, so gcc and clang print the
 * same bytes.
 *
 *   fuzz_triage ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"
#include "zvtriage.h"

#define NRESP 4
#define NPAT  6
#define MAXB  2048
#define MAXI  8192

static const char *k_resp[NRESP] = {"MEDIC-1", "MEDIC-2", "ENGINE-7", "AIR-3"};
static const char *k_pat[NPAT] = {"T-001", "T-002", "T-003", "T-004", "T-005", "T-006"};

typedef struct {
    uint8_t hash[32], based_on[32], cat;
    int pat, resp; /* resp -1: a reviewer's decision */
    int ref;       /* the model: some item references this one */
} item_t;

/* every record made, by index; bundles list record indices */
static item_t g_rec[MAXI];
static int g_nrec;

typedef struct {
    uint64_t seq;
    uint8_t *bytes;
    size_t len;
    int rec[ZVT_BUNDLE_RECORDS], n;
} sbundle_t;

static struct {
    sbundle_t b[MAXB];
    int nb;
    int staged[1100], nstaged;
    uint64_t next;     /* station: applied in order          */
    int pend[64], npend; /* station: seqs waiting behind a gap */
} g_r[NRESP];

/* per patient: items in arrival order (records and decisions), refs */
static struct {
    item_t it[MAXI];
    int n;
    uint8_t refs[MAXI][32];
    int nrefs;
    uint32_t records;
} g_p[NPAT];

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_triage: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what, zt_last_err());
    abort();
}

static uint64_t draw(uint64_t *s, uint64_t n) {
    uint64_t v = zt_rand(s);
    return v % n;
}

static int is_zero(const uint8_t h[32]) {
    for (int i = 0; i < 32; i++)
        if (h[i]) return 0;
    return 1;
}

static int has_ref(int p, const uint8_t h[32]) {
    for (int i = 0; i < g_p[p].nrefs; i++)
        if (!memcmp(g_p[p].refs[i], h, 32)) return 1;
    return 0;
}

static void add_ref(int p, const uint8_t h[32]) {
    if (has_ref(p, h)) return;
    memcpy(g_p[p].refs[g_p[p].nrefs++], h, 32);
    for (int k = 0; k < g_p[p].n; k++)
        if (!memcmp(g_p[p].it[k].hash, h, 32)) g_p[p].it[k].ref = 1;
}

static void add_item(int p, const item_t *x) {
    item_t *y = &g_p[p].it[g_p[p].n++];
    *y = *x;
    y->ref = has_ref(p, x->hash);
}

/* current: nothing references it and, for a record, its responder sent
 * nothing newer for this patient */
static void heads_of(int p, int *out, int *n) {
    int seen[NRESP] = {0};
    *n = 0;
    for (int k = g_p[p].n - 1; k >= 0; k--) {
        const item_t *x = &g_p[p].it[k];
        int newer = x->resp >= 0 && seen[x->resp];
        if (x->resp >= 0) seen[x->resp] = 1;
        if (!x->ref && !newer) out[(*n)++] = k;
    }
}

static void model_view(int p, int32_t *lp, uint8_t *mask, uint32_t *heads) {
    static int h[MAXI];
    int n = 0;
    heads_of(p, h, &n);
    *mask = 0;
    for (int i = 0; i < n; i++) *mask = (uint8_t) (*mask | (1u << g_p[p].it[h[i]].cat));
    *heads = (uint32_t) n;
    *lp = *mask == 0 ? ZXV_LP_NEITHER : (*mask & (*mask - 1)) ? ZXV_LP_BOTH : ZXV_LP_TRUE;
}

/* apply one bundle to the model; returns new conflicts */
static int model_apply(int r, const sbundle_t *b) {
    int conflicts = 0;
    for (int i = 0; i < b->n; i++) {
        const item_t *x = &g_rec[b->rec[i]];
        int32_t before, after;
        uint8_t m;
        uint32_t h;
        model_view(x->pat, &before, &m, &h);
        if (!is_zero(x->based_on)) add_ref(x->pat, x->based_on);
        add_item(x->pat, x);
        g_p[x->pat].records++;
        model_view(x->pat, &after, &m, &h);
        if (after == ZXV_LP_BOTH && before != ZXV_LP_BOTH) conflicts++;
    }
    g_r[r].next++;
    return conflicts;
}

static void check_patients(const zvt_station *st, unsigned long it) {
    for (int p = 0; p < NPAT; p++) {
        zvt_patient_view v;
        int32_t lp;
        uint8_t mask;
        uint32_t heads;
        int32_t rc = zvt_patient_state(st, k_pat[p], &v);
        model_view(p, &lp, &mask, &heads);
        if (g_p[p].records == 0) {
            if (rc != ZVT_E_PATIENT) die("a patient exists without records", it);
            continue;
        }
        if (rc != ZXV_OK) die("patient state", it);
        if (v.lp != lp || v.categories != mask || v.heads != heads || v.records != g_p[p].records)
            die("patient picture differs from the model", it);
        if (lp == ZXV_LP_TRUE && !(mask & (1u << v.category))) die("category", it);
        if (lp == ZXV_LP_BOTH && v.category != 0) die("a held patient shows a category", it);
    }
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x7e1a6eULL), s = seed, clock = 1700000000ULL;
    zt_env e;
    zind_signer *stkey, *rev, *rogue, *key[NRESP];
    zxv_hexlog *qlog = NULL;
    zvt_station *st = NULL;
    zvt_outbox *ob[NRESP];
    unsigned long n_rec = 0, n_seal = 0, n_ok = 0, n_gap = 0, n_replay = 0, n_cap = 0,
                  n_flip = 0, n_held = 0, n_res = 0, n_forged = 0, n_ack = 0, n_drained = 0;

    if (zt_env_open(&e, NULL) != 0) return 2;
    stkey = zt_signer(e.cap, 1);
    rev = zt_signer(e.cap, 2);
    rogue = zt_signer(e.cap, 3);
    if (!stkey || !rev || !rogue || zxv_hexlog_create(e.cap, 65536, &qlog) != ZXV_OK ||
        zvt_station_create(e.cap, stkey, qlog, &st) != ZXV_OK ||
        zvt_reviewer_add(st, "DR-LEE", zind_signer_pk(rev)) != ZXV_OK)
        return 2;
    for (int r = 0; r < NRESP; r++) {
        key[r] = zt_signer(e.cap, (uint8_t) (20 + r));
        if (!key[r] || zvt_responder_add(st, k_resp[r], zind_signer_pk(key[r])) != ZXV_OK ||
            zvt_outbox_create(e.cap, k_resp[r], key[r], &ob[r]) != ZXV_OK)
            return 2;
    }

    for (unsigned long it = 0; it < iters && g_nrec < MAXI - 40; it++) {
        uint64_t op = draw(&s, 100);
        int r = (int) draw(&s, NRESP);
        clock += 1 + draw(&s, 30);
        if (op < 35) {
            /* a responder records a patient */
            zvt_record rec;
            item_t *x = &g_rec[g_nrec];
            int p = (int) draw(&s, NPAT);
            uint64_t how = draw(&s, 10);
            if (g_r[r].nstaged >= 1000) continue;
            memset(&rec, 0, sizeof rec);
            snprintf(rec.patient, sizeof rec.patient, "%s", k_pat[p]);
            rec.system = draw(&s, 4) == 0 ? ZVT_SYS_SALT : ZVT_SYS_START;
            rec.category = (uint8_t) (1 + draw(&s, 5));
            if (rec.category == ZVT_CAT_EXPECTANT && rec.system != ZVT_SYS_SALT)
                rec.category = ZVT_CAT_DELAYED;
            rec.observed = clock;
            rec.n_vitals = 2;
            rec.vitals[0] = (zvt_vital){ZVT_V_RESP, ZVT_U_PER_MIN, (int32_t) draw(&s, 60)};
            rec.vitals[1] = (zvt_vital){ZVT_V_SPO2, ZVT_U_PERCENT, (int32_t) (60 + draw(&s, 41))};
            snprintf(rec.note, sizeof rec.note, "op %lu", it);
            if (how >= 4 && how < 7) {
                /* build on a head the station shows */
                zvt_patient_view v;
                if (zvt_patient_state(st, k_pat[p], &v) == ZXV_OK && v.heads) {
                    uint32_t i = (uint32_t) draw(&s, v.heads);
                    zvt_patient_head(st, k_pat[p], i, NULL, NULL, 0, rec.based_on, NULL, NULL);
                }
            } else if (how >= 7 && how < 9 && g_nrec) {
                /* build on some earlier record, maybe one not delivered yet */
                int k = (int) draw(&s, (uint64_t) g_nrec);
                if (g_rec[k].pat == p) memcpy(rec.based_on, g_rec[k].hash, 32);
            } else if (how == 9) {
                for (int i = 0; i < 32; i++) rec.based_on[i] = (uint8_t) draw(&s, 256);
            }
            if (zvt_outbox_record(ob[r], &rec, x->hash) != ZXV_OK) die("record", it);
            memcpy(x->based_on, rec.based_on, 32);
            x->cat = rec.category;
            x->pat = p;
            x->resp = r;
            g_r[r].staged[g_r[r].nstaged++] = g_nrec++;
            n_rec++;
        } else if (op < 50) {
            /* seal */
            sbundle_t *b;
            uint64_t seq = 0;
            int32_t rc;
            uint32_t pend = 0;
            if (g_r[r].nb >= MAXB) continue;
            zvt_outbox_pending(ob[r], &pend);
            rc = zvt_outbox_seal(ob[r], clock, &seq);
            if (g_r[r].nstaged == 0) {
                if (rc != ZXV_E_STATE) die("seal of nothing", it);
                continue;
            }
            if (pend >= ZVT_OUTBOX_MAX) {
                if (rc != ZXV_E_CAPACITY) die("outbox bound", it);
                continue;
            }
            if (rc != ZXV_OK || seq != (uint64_t) g_r[r].nb) die("seal", it);
            b = &g_r[r].b[g_r[r].nb++];
            b->seq = seq;
            b->n = g_r[r].nstaged < (int) ZVT_BUNDLE_RECORDS ? g_r[r].nstaged
                                                              : (int) ZVT_BUNDLE_RECORDS;
            memcpy(b->rec, g_r[r].staged, (size_t) b->n * sizeof(int));
            memmove(g_r[r].staged, g_r[r].staged + b->n,
                    (size_t) (g_r[r].nstaged - b->n) * sizeof(int));
            g_r[r].nstaged -= b->n;
            {
                uint32_t n = 0;
                zvt_outbox_pending(ob[r], &n);
                zvt_outbox_bundle(ob[r], n - 1, NULL, 0, &b->len);
                b->bytes = (uint8_t *) malloc(b->len);
                if (zvt_outbox_bundle(ob[r], n - 1, b->bytes, b->len, &b->len) != ZXV_OK)
                    die("bundle bytes", it);
            }
            n_seal++;
        } else if (op < 80) {
            /* the link delivers a random bundle of r (often the next one) */
            sbundle_t *b;
            zvt_receipt rc;
            int32_t st_, want, conflicts = 0, k;
            uint32_t applied = 0, recs = 0;
            size_t pos = 0;
            int flip;
            if (g_r[r].nb == 0) continue;
            {
                uint64_t pick = draw(&s, 4);
                uint64_t ahead = (uint64_t) g_r[r].nb - g_r[r].next;
                if (pick < 2 && ahead) k = (int) g_r[r].next;
                else if (pick == 2 && ahead) k = (int) (g_r[r].next + draw(&s, ahead));
                else k = (int) draw(&s, (uint64_t) g_r[r].nb);
            }
            b = &g_r[r].b[k];
            flip = draw(&s, 12) == 0;
            if (flip) {
                pos = (size_t) draw(&s, b->len);
                uint8_t bit = (uint8_t) (1u << draw(&s, 8));
                uint8_t *c = (uint8_t *) malloc(b->len);
                memcpy(c, b->bytes, b->len);
                c[pos] ^= bit;
                st_ = zvt_station_receive(st, c, b->len, &rc);
                free(c);
                /* a flip in the signature of a bundle already held leaves its hash
                 * unchanged: it is dropped as a replay, which changes nothing */
                if (st_ == ZXV_OK || st_ == ZVT_E_GAP || st_ == ZVT_E_FORK ||
                    (st_ == ZIND_E_REPLAY && (pos < b->len - ZIND_SIG_BYTES ||
                                              (uint64_t) k >= g_r[r].next)))
                    die("a corrupted bundle was taken", it);
                n_flip++;
                check_patients(st, it);
                continue;
            }
            /* the model's answer */
            {
                int pending = 0;
                for (int i = 0; i < g_r[r].npend; i++)
                    if (g_r[r].pend[i] == k) pending = 1;
                if ((uint64_t) k < g_r[r].next || pending) want = ZIND_E_REPLAY;
                else if ((uint64_t) k > g_r[r].next)
                    want = g_r[r].npend >= (int) ZVT_PENDING_MAX ? ZXV_E_CAPACITY : ZVT_E_GAP;
                else want = ZXV_OK;
            }
            st_ = zvt_station_receive(st, b->bytes, b->len, &rc);
            if (st_ != want) die("receive status differs from the model", it);
            if (want == ZVT_E_GAP) {
                g_r[r].pend[g_r[r].npend++] = k;
                n_gap++;
            } else if (want == ZIND_E_REPLAY) {
                n_replay++;
            } else if (want == ZXV_E_CAPACITY) {
                n_cap++;
            } else {
                conflicts += model_apply(r, b);
                applied++;
                recs += (uint32_t) b->n;
                for (;;) {
                    int found = -1;
                    for (int i = 0; i < g_r[r].npend; i++)
                        if ((uint64_t) g_r[r].pend[i] == g_r[r].next) found = i;
                    if (found < 0) break;
                    conflicts += model_apply(r, &g_r[r].b[g_r[r].pend[found]]);
                    recs += (uint32_t) g_r[r].b[g_r[r].pend[found]].n;
                    g_r[r].pend[found] = g_r[r].pend[--g_r[r].npend];
                    applied++;
                    n_drained++;
                }
                if (rc.bundles != applied || rc.records != recs || rc.duplicates != 0 ||
                    rc.new_conflicts != (uint32_t) conflicts)
                    die("receipt differs from the model", it);
                n_ok++;
            }
            check_patients(st, it);
        } else if (op < 88) {
            /* acknowledgement */
            uint8_t ack[6000];
            size_t al = 0;
            uint32_t before = 0, after = 0, expect = 0;
            if (zvt_station_ack(st, k_resp[r], ack, sizeof ack, &al) != ZXV_OK) die("ack", it);
            zvt_outbox_pending(ob[r], &before);
            if (draw(&s, 5) == 0) {
                ack[draw(&s, al)] ^= 0x40;
                if (zvt_outbox_ack(ob[r], zind_signer_pk(stkey), ack, al) == ZXV_OK)
                    die("a corrupted acknowledgement was taken", it);
                continue;
            }
            if (zvt_outbox_ack(ob[r], zind_signer_pk(stkey), ack, al) != ZXV_OK) die("ack", it);
            zvt_outbox_pending(ob[r], &after);
            for (int i = 0; i < g_r[r].nb; i++)
                if ((uint64_t) i >= g_r[r].next) expect++;
            if (after != expect || after > before) die("acknowledgement pruned wrongly", it);
            n_ack++;
        } else {
            /* a reviewer settles a held patient */
            int p = -1;
            int32_t lp;
            uint8_t mask, m[4096], sig[ZIND_SIG_BYTES];
            uint32_t heads;
            size_t len = 0;
            uint8_t cat = (uint8_t) (1 + draw(&s, 5));
            uint64_t how = draw(&s, 4);
            for (int q = 0; q < NPAT; q++) {
                model_view(q, &lp, &mask, &heads);
                if (lp == ZXV_LP_BOTH) p = q;
            }
            if (p < 0) continue;
            if (zvt_resolve_message(st, k_pat[p], cat, "DR-LEE", m, sizeof m, &len) != ZXV_OK)
                die("resolve message", it);
            if (zind_sign(e.cap, how == 0 ? rogue : rev, m, len, ZVT_RESOLVE_CTX, sig) != ZXV_OK)
                die("sign", it);
            if (how <= 1) {
                uint8_t other = (uint8_t) (cat % 5 + 1);
                int32_t rc = zvt_resolve(st, k_pat[p], how == 0 ? cat : other, "DR-LEE", sig);
                if (rc != ZXV_E_VERIFY) die("a forged decision was taken", it);
                n_forged++;
            } else {
                item_t d;
                int dec = 0;
                if (zvt_resolve(st, k_pat[p], cat, "DR-LEE", sig) != ZXV_OK) die("resolve", it);
                memset(&d, 0, sizeof d);
                {
                    static int h[MAXI];
                    int nh = 0;
                    heads_of(p, h, &nh);
                    for (int i = 0; i < nh; i++) add_ref(p, g_p[p].it[h[i]].hash);
                }
                if (zvt_patient_head(st, k_pat[p], 0, NULL, NULL, 0, d.hash, NULL, &dec) != ZXV_OK ||
                    !dec)
                    die("decision head", it);
                d.cat = cat;
                d.pat = p;
                d.resp = -1;
                add_item(p, &d);
                n_res++;
            }
            check_patients(st, it);
        }
        if (it % 500 == 499) {
            const uint8_t *trust[1] = {zind_signer_pk(stkey)};
            if (zind_chain_verify(e.cap, zvt_station_journal(st), trust, 1, NULL) != ZXV_OK)
                die("journal", it);
        }
    }
    {
        const uint8_t *trust[1] = {zind_signer_pk(stkey)};
        uint64_t bundles = 0;
        if (zind_chain_verify(e.cap, zvt_station_journal(st), trust, 1, NULL) != ZXV_OK)
            die("final journal", iters);
        if (zxv_hexlog_verify(qlog) != ZXV_OK) die("quarantine log", iters);
        zvt_station_stats(st, &bundles, NULL, NULL, NULL);
        for (int p = 0; p < NPAT; p++) {
            int32_t lp;
            uint8_t mask;
            uint32_t heads;
            model_view(p, &lp, &mask, &heads);
            if (lp == ZXV_LP_BOTH) n_held++;
        }
        printf("fuzz_triage: %lu ops (records %lu, bundles sealed %lu, applied %llu incl. %lu "
               "drained, gaps %lu, replays %lu, pending-full %lu, corrupted %lu, decisions %lu, "
               "forged %lu, acks %lu, held at end %lu), seed %llu, 0 invariant failures\n",
               iters, n_rec, n_seal, (unsigned long long) bundles, n_drained, n_gap, n_replay,
               n_cap, n_flip, n_res, n_forged, n_ack, n_held, (unsigned long long) seed);
    }
    for (int r = 0; r < NRESP; r++) {
        for (int i = 0; i < g_r[r].nb; i++) free(g_r[r].b[i].bytes);
        zvt_outbox_destroy(ob[r]);
        zind_signer_destroy(key[r]);
    }
    zvt_station_destroy(st);
    zxv_hexlog_destroy(qlog);
    zind_signer_destroy(stkey);
    zind_signer_destroy(rev);
    zind_signer_destroy(rogue);
    zt_env_close(&e);
    (void) n_ok;
    return 0;
}
