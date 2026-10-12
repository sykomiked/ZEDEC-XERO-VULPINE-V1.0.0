/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztmodem.c — tri-modem: signed neighbour adverts, four-valued reachability
 * evidence with BOTH holds, owner agreements and resolutions, and the
 * max-min fair relay plan charged to libzxv rmag bandwidth budgets.
 * Integer and exact-rational code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "ztmodem.h"

#define MAX_EV (2u * ZTM_MAX_DESTS)

typedef struct {
    char name[ZTM_DEST_MAX + 1];
    int32_t advert_lp; /* the current advert's claim (BOTH if it claims both ways) */
    int32_t probe_lp;
    uint64_t probe_time;
    int has_probe;
    int held;
    int has_override;
    int32_t override_lp;
    uint64_t override_seq;
} ev_t;

typedef struct {
    char id[ZTM_ID_MAX + 1];
    int32_t alg;
    uint8_t pk[ZIND_PK_BYTES];
    size_t pk_len;
    int has_advert;
    ztm_advert adv;
    uint64_t last_seq;
    zind_qty agreement;
    uint32_t budget;
    ev_t ev[MAX_EV];
    uint32_t n_ev;
} nb_t;

struct ztm_planner {
    const zxv_cap *cap;
    char self[ZTM_ID_MAX + 1];
    uint8_t owner_pk[32];
    zind_qsink *qs;
    zxv_rmag *rmag;
    nb_t nb[ZTM_MAX_NEIGHBOURS];
    uint32_t n_nb;
};

const char *ztm_status_name(int32_t status) {
    switch (status) {
    case ZTM_E_NEIGHBOUR: return "ZTM_E_NEIGHBOUR";
    case ZTM_E_EXPIRED: return "ZTM_E_EXPIRED";
    case ZTM_E_AGREEMENT: return "ZTM_E_AGREEMENT";
    case ZTM_E_MEDIUM: return "ZTM_E_MEDIUM";
    case ZTM_E_NO_RELAY: return "ZTM_E_NO_RELAY";
    case ZTM_E_WAN_UP: return "ZTM_E_WAN_UP";
    case ZTM_E_OWNER: return "ZTM_E_OWNER";
    default: return zind_status_name(status);
    }
}

/* ===== adverts ===== */

static int medium_ok(uint8_t m) {
    return m == ZTM_MEDIUM_ETHERNET || m == ZTM_MEDIUM_WIFI || m == ZTM_MEDIUM_POWERLINE ||
           m == ZTM_MEDIUM_FIBRE || m == ZTM_MEDIUM_AMATEUR;
}

static int32_t advert_check(const ztm_advert *a) {
    if (!zind_ident_ok(a->neighbour, ZTM_ID_MAX) || a->seq == 0 || a->expires <= a->issued ||
        a->expires - a->issued > ZTM_LIFETIME_MAX || a->wan_up > 1 || !medium_ok(a->medium) ||
        !zind_qty_valid(a->offer_kbps) || zind_qty_sign(a->offer_kbps) < 0 ||
        a->n_claims > ZTM_MAX_DESTS) {
        zind_set_error("ztm advert: id 1..16 of [A-Za-z0-9._:/-], seq >= 1, issued < expires "
                       "<= issued + 1 day, a known medium, an offer >= 0, at most 8 claims");
        return ZXV_E_FORMAT;
    }
    for (uint32_t i = 0; i < a->n_claims; i++)
        if (!zind_ident_ok(a->claims[i].name, ZTM_DEST_MAX) ||
            (a->claims[i].lp != ZXV_LP_TRUE && a->claims[i].lp != ZXV_LP_FALSE)) {
            zind_set_error("ztm advert: claim %u needs a destination name and TRUE or FALSE", i);
            return ZXV_E_FORMAT;
        }
    return ZXV_OK;
}

static void advert_body(zind_wr *w, const ztm_advert *a) {
    zind_wr_str(w, a->neighbour);
    zind_wr_u64(w, a->seq);
    zind_wr_u64(w, a->issued);
    zind_wr_u64(w, a->expires);
    zind_wr_u8(w, a->wan_up);
    zind_wr_u8(w, a->medium);
    zind_wr_qty(w, a->offer_kbps);
    zind_wr_u32(w, a->latency_ms);
    zind_wr_u8(w, a->n_claims);
    for (uint32_t i = 0; i < a->n_claims; i++) {
        zind_wr_str(w, a->claims[i].name);
        zind_wr_u8(w, (uint8_t) a->claims[i].lp);
    }
}

/* "ztm/advert/v1" || body, the bytes that are signed */
static int32_t signed_msg(const ztm_advert *a, uint8_t *buf, size_t cap, size_t *len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ztm/advert/v1", 13);
    advert_body(&w, a);
    *len = w.len;
    return w.overflow ? ZXV_E_INTERNAL : ZXV_OK;
}

static int32_t seal(const ztm_advert *a, int32_t alg, const uint8_t *sig, size_t sig_len,
                    uint8_t *out, size_t cap, size_t *out_len) {
    uint8_t body[512];
    zind_wr w, bw;
    zind_wr_init(&bw, body, sizeof body);
    advert_body(&bw, a);
    if (bw.overflow) return ZXV_E_INTERNAL;
    *out_len = 4 + 1 + 1 + 2 + bw.len + 2 + sig_len;
    if (!out || cap < *out_len) return ZXV_E_BUFFER;
    zind_wr_init(&w, out, cap);
    zind_wr_bytes(&w, "ZTMA", 4);
    zind_wr_u8(&w, 1);
    zind_wr_u8(&w, (uint8_t) alg);
    zind_wr_u16(&w, (uint16_t) bw.len);
    zind_wr_bytes(&w, body, bw.len);
    zind_wr_u16(&w, (uint16_t) sig_len);
    zind_wr_bytes(&w, sig, sig_len);
    return w.overflow ? ZXV_E_INTERNAL : ZXV_OK;
}

int32_t ztm_advert_seal_ed25519(const zxv_cap *cap, const ztm_advert *a, const uint8_t sk[32],
                                uint8_t *out, size_t cap_bytes, size_t *out_len) {
    uint8_t msg[512], sig[ZXV_ED25519_SIG_BYTES];
    size_t len = 0;
    int32_t st;
    if (!cap || !a || !sk || !out_len) return ZXV_E_ARG;
    st = advert_check(a);
    if (st != ZXV_OK) return ZXV_E_ARG;
    st = signed_msg(a, msg, sizeof msg, &len);
    if (st == ZXV_OK) st = zxv_ed25519_sign(cap, sk, msg, len, sig);
    if (st != ZXV_OK) return st;
    return seal(a, ZTM_ALG_ED25519, sig, sizeof sig, out, cap_bytes, out_len);
}

int32_t ztm_advert_seal_mldsa(const zxv_cap *cap, const ztm_advert *a, const zind_signer *s,
                              uint8_t *out, size_t cap_bytes, size_t *out_len) {
    uint8_t msg[512], *sig;
    size_t len = 0;
    int32_t st;
    if (!cap || !a || !s || !out_len) return ZXV_E_ARG;
    st = advert_check(a);
    if (st != ZXV_OK) return ZXV_E_ARG;
    sig = (uint8_t *) malloc(ZIND_SIG_BYTES);
    if (!sig) return ZXV_E_NOMEM;
    st = signed_msg(a, msg, sizeof msg, &len);
    if (st == ZXV_OK) st = zind_sign(cap, s, msg, len, ZTM_ADVERT_CTX, sig);
    if (st == ZXV_OK) st = seal(a, ZTM_ALG_MLDSA87, sig, ZIND_SIG_BYTES, out, cap_bytes, out_len);
    free(sig);
    return st;
}

/* Parse; *sig points into buf. */
static int32_t parse(const uint8_t *buf, size_t len, ztm_advert *a, int32_t *alg,
                     const uint8_t **sig, size_t *sig_len) {
    zind_rd r, br;
    uint16_t blen;
    const uint8_t *body;
    memset(a, 0, sizeof *a);
    if (!buf || len < 8 || memcmp(buf, "ZTMA", 4) != 0) return ZXV_E_FORMAT;
    zind_rd_init(&r, buf + 4, len - 4);
    if (zind_rd_u8(&r) != 1) return ZXV_E_FORMAT;
    *alg = zind_rd_u8(&r);
    blen = zind_rd_u16(&r);
    if (r.bad || r.len - r.pos < (size_t) blen + 2) return ZXV_E_FORMAT;
    body = r.buf + r.pos;
    r.pos += blen;
    *sig_len = zind_rd_u16(&r);
    if (r.bad || r.len - r.pos != *sig_len) return ZXV_E_FORMAT;
    *sig = r.buf + r.pos;
    if ((*alg == ZTM_ALG_ED25519 && *sig_len != ZXV_ED25519_SIG_BYTES) ||
        (*alg == ZTM_ALG_MLDSA87 && *sig_len != ZIND_SIG_BYTES) ||
        (*alg != ZTM_ALG_ED25519 && *alg != ZTM_ALG_MLDSA87))
        return ZXV_E_FORMAT;
    zind_rd_init(&br, body, blen);
    zind_rd_str(&br, a->neighbour, sizeof a->neighbour);
    a->seq = zind_rd_u64(&br);
    a->issued = zind_rd_u64(&br);
    a->expires = zind_rd_u64(&br);
    a->wan_up = zind_rd_u8(&br);
    a->medium = zind_rd_u8(&br);
    zind_rd_qty(&br, &a->offer_kbps);
    a->latency_ms = zind_rd_u32(&br);
    a->n_claims = zind_rd_u8(&br);
    if (br.bad || a->n_claims > ZTM_MAX_DESTS) return ZXV_E_FORMAT;
    for (uint32_t i = 0; i < a->n_claims; i++) {
        zind_rd_str(&br, a->claims[i].name, sizeof a->claims[i].name);
        a->claims[i].lp = zind_rd_u8(&br);
    }
    if (br.bad || br.pos != br.len) return ZXV_E_FORMAT;
    return advert_check(a);
}

int32_t ztm_advert_parse(const uint8_t *buf, size_t len, ztm_advert *out, int32_t *out_alg) {
    const uint8_t *sig;
    size_t sl;
    int32_t alg = 0, st;
    if (!buf || !out) return ZXV_E_ARG;
    st = parse(buf, len, out, &alg, &sig, &sl);
    if (st == ZXV_OK && out_alg) *out_alg = alg;
    return st;
}

/* ===== planner ===== */

int32_t ztm_planner_create(const zxv_cap *cap, const char *self_id, const uint8_t owner_pk[32],
                           zxv_hexlog *qlog, ztm_planner **out) {
    ztm_planner *p;
    int32_t st;
    if (!cap || !owner_pk || !out || !zind_ident_ok(self_id, ZTM_ID_MAX)) {
        zind_set_error("ztm_planner_create: a cap, a router id (1..16 of [A-Za-z0-9._:/-]) and "
                       "the owner's key are required");
        return ZXV_E_ARG;
    }
    *out = NULL;
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN | ZXV_CAP_BUDGET);
    if (st != ZXV_OK) {
        zind_set_error("ztm_planner_create: the cap needs CRYPTO_SIGN and BUDGET");
        return st;
    }
    p = (ztm_planner *) calloc(1, sizeof *p);
    if (!p) return ZXV_E_NOMEM;
    p->cap = cap;
    memcpy(p->self, self_id, strlen(self_id) + 1);
    memcpy(p->owner_pk, owner_pk, 32);
    st = zxv_rmag_create_cap(cap, &p->rmag);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &p->qs);
    if (st != ZXV_OK) {
        ztm_planner_destroy(p);
        return st;
    }
    *out = p;
    return ZXV_OK;
}

void ztm_planner_destroy(ztm_planner *p) {
    if (!p) return;
    zxv_rmag_destroy(p->rmag);
    zind_qsink_destroy(p->qs);
    free(p);
}

static nb_t *find_nb(const ztm_planner *p, const char *id) {
    if (!p || !id) return NULL;
    for (uint32_t i = 0; i < p->n_nb; i++)
        if (strcmp(p->nb[i].id, id) == 0) return (nb_t *) &p->nb[i];
    return NULL;
}

int32_t ztm_neighbour_add(ztm_planner *p, const char *id, int32_t alg, const uint8_t *pk,
                          size_t pk_len) {
    nb_t *n;
    char name[ZTM_ID_MAX + 8];
    int32_t st;
    if (!p || !pk || !zind_ident_ok(id, ZTM_ID_MAX) ||
        !((alg == ZTM_ALG_ED25519 && pk_len == ZXV_ED25519_PK_BYTES) ||
          (alg == ZTM_ALG_MLDSA87 && pk_len == ZIND_PK_BYTES))) {
        zind_set_error("ztm_neighbour_add: an id and an Ed25519 (32-byte) or ML-DSA-87 key");
        return ZXV_E_ARG;
    }
    if (strcmp(id, p->self) == 0) return ZXV_E_ARG;
    if (find_nb(p, id)) return ZXV_E_DUPLICATE;
    for (uint32_t i = 0; i < p->n_nb; i++)
        if (p->nb[i].pk_len == pk_len && memcmp(p->nb[i].pk, pk, pk_len) == 0)
            return ZXV_E_DUPLICATE;
    if (p->n_nb >= ZTM_MAX_NEIGHBOURS) return ZXV_E_CAPACITY;
    n = &p->nb[p->n_nb];
    memset(n, 0, sizeof *n);
    memcpy(n->id, id, strlen(id) + 1);
    n->alg = alg;
    memcpy(n->pk, pk, pk_len);
    n->pk_len = pk_len;
    n->agreement = ZIND_QTY_ZERO;
    memcpy(name, "ztm/", 4);
    memcpy(name + 4, id, strlen(id) + 1);
    st = zxv_rmag_budget(p->cap, p->rmag, name, ZXV_RES_BANDWIDTH, 0, 1, 1, &n->budget);
    if (st != ZXV_OK) return st;
    p->n_nb++;
    return ZXV_OK;
}

int32_t ztm_agreement_set(ztm_planner *p, const char *id, zind_qty cap_kbps) {
    nb_t *n = find_nb(p, id);
    if (!p) return ZXV_E_ARG;
    if (!n) return ZTM_E_NEIGHBOUR;
    if (!zind_qty_valid(cap_kbps) || zind_qty_sign(cap_kbps) < 0) return ZXV_E_ARG;
    n->agreement = cap_kbps;
    return ZXV_OK;
}

/* ===== evidence ===== */

static int32_t join(int32_t a, int32_t b) {
    if (a == ZXV_LP_NEITHER) return b;
    if (b == ZXV_LP_NEITHER) return a;
    if (a == b) return a;
    return ZXV_LP_BOTH;
}

static int32_t claim_of(const ztm_advert *a, const char *name) {
    int32_t lp = ZXV_LP_NEITHER;
    for (uint32_t i = 0; i < a->n_claims; i++)
        if (strcmp(a->claims[i].name, name) == 0) lp = join(lp, a->claims[i].lp);
    return lp;
}

static int32_t ev_lp(const nb_t *n, const ev_t *e, uint64_t now) {
    int32_t pr = ZXV_LP_NEITHER;
    if (e->held) return ZXV_LP_BOTH;
    if (e->has_override && n->has_advert && e->override_seq == n->adv.seq) return e->override_lp;
    if (e->has_probe && now >= e->probe_time && now - e->probe_time <= ZTM_PROBE_TTL_S)
        pr = e->probe_lp;
    return join(e->advert_lp, pr);
}

static ev_t *find_ev(nb_t *n, const char *name) {
    for (uint32_t i = 0; i < n->n_ev; i++)
        if (strcmp(n->ev[i].name, name) == 0) return &n->ev[i];
    return NULL;
}

static ev_t *add_ev(nb_t *n, const char *name) {
    ev_t *e = find_ev(n, name);
    if (e) return e;
    if (n->n_ev >= MAX_EV) return NULL;
    e = &n->ev[n->n_ev++];
    memset(e, 0, sizeof *e);
    memcpy(e->name, name, strlen(name) + 1);
    e->advert_lp = n->has_advert ? claim_of(&n->adv, name) : ZXV_LP_NEITHER;
    return e;
}

static void log_ev(ztm_planner *p, const nb_t *n, const char *what, int32_t lp, uint8_t pol,
                   uint8_t reason) {
    uint8_t buf[80], subj[32], t = 0;
    size_t a = strlen(n->id), b = strlen(what);
    if (!p->qs) return;
    memcpy(buf, n->id, a);
    buf[a] = 0;
    memcpy(buf + a + 1, what, b);
    if (zind_tag(lp, pol, ZXV_HEX_PHASE_EMERGING, &t) != ZXV_OK ||
        zind_subject("tri-modem/evidence", buf, a + 1 + b, subj) != ZXV_OK)
        return;
    (void) zind_qsink_event(p->qs, t, reason, (uint32_t) (n - p->nb), subj, NULL);
}

/* Hold every unheld entry whose joined evidence is BOTH; 1 if any was. */
static int hold_contradictions(ztm_planner *p, nb_t *n, uint64_t now) {
    int any = 0;
    for (uint32_t i = 0; i < n->n_ev; i++) {
        ev_t *e = &n->ev[i];
        if (e->held || ev_lp(n, e, now) != ZXV_LP_BOTH) continue;
        e->held = 1;
        any = 1;
        log_ev(p, n, e->name, ZXV_LP_BOTH, ZXV_HEX_POL_NEUTRAL, ZIND_REASON_HELD_BOTH);
    }
    return any;
}

int32_t ztm_advert_accept(ztm_planner *p, const uint8_t *buf, size_t len, uint64_t now) {
    ztm_advert a;
    const uint8_t *sig = NULL;
    uint8_t msg[512];
    size_t sl = 0, ml = 0;
    int32_t alg = 0, st;
    nb_t *n;
    if (!p || !buf) return ZXV_E_ARG;
    st = parse(buf, len, &a, &alg, &sig, &sl);
    if (st != ZXV_OK) return st;
    n = find_nb(p, a.neighbour);
    if (!n) {
        zind_set_error("ztm_advert_accept: %s is not a registered neighbour", a.neighbour);
        return ZTM_E_NEIGHBOUR;
    }
    st = signed_msg(&a, msg, sizeof msg, &ml);
    if (st != ZXV_OK) return st;
    if (alg != n->alg) st = ZXV_E_VERIFY;
    else if (alg == ZTM_ALG_ED25519) st = zxv_ed25519_verify(p->cap, n->pk, msg, ml, sig);
    else st = zind_verify(p->cap, n->pk, msg, ml, ZTM_ADVERT_CTX, sig);
    if (st != ZXV_OK) {
        if (st == ZXV_E_VERIFY) {
            log_ev(p, n, "advert", ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZIND_REASON_TAMPER);
            zind_set_error("ztm_advert_accept: the advert from %s does not verify", n->id);
        }
        return st;
    }
    if (a.seq <= n->last_seq) {
        log_ev(p, n, "advert", ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZIND_REASON_REPLAY);
        zind_set_error("ztm_advert_accept: advert %llu from %s is not newer than %llu",
                       (unsigned long long) a.seq, n->id, (unsigned long long) n->last_seq);
        return ZIND_E_REPLAY;
    }
    if (now > a.expires || a.issued > now + ZTM_SKEW_S) {
        zind_set_error("ztm_advert_accept: the advert from %s is %s", n->id,
                       now > a.expires ? "expired" : "dated in the future");
        return ZTM_E_EXPIRED;
    }
    if (a.medium == ZTM_MEDIUM_AMATEUR) {
        zind_set_error("ztm_advert_accept: %s offers an amateur-radio relay; tri-modem never "
                       "selects one",
                       n->id);
        return ZTM_E_MEDIUM;
    }
    n->adv = a;
    n->has_advert = 1;
    n->last_seq = a.seq;
    for (uint32_t i = 0; i < n->n_ev; i++) {
        n->ev[i].advert_lp = claim_of(&a, n->ev[i].name);
        if (n->ev[i].has_override && n->ev[i].override_seq != a.seq) n->ev[i].has_override = 0;
    }
    for (uint32_t i = 0; i < a.n_claims; i++)
        if (!add_ev(n, a.claims[i].name)) return ZXV_E_CAPACITY;
    if (hold_contradictions(p, n, now)) {
        zind_set_error("ztm_advert_accept: contradictory reachability from %s held as BOTH",
                       n->id);
        return ZIND_E_HELD;
    }
    return ZXV_OK;
}

int32_t ztm_probe_record(ztm_planner *p, const char *neighbour, const char *dest, int32_t lp,
                         uint64_t now) {
    nb_t *n = find_nb(p, neighbour);
    ev_t *e;
    if (!p) return ZXV_E_ARG;
    if (!n) return ZTM_E_NEIGHBOUR;
    if (!zind_ident_ok(dest, ZTM_DEST_MAX) || (lp != ZXV_LP_TRUE && lp != ZXV_LP_FALSE))
        return ZXV_E_ARG;
    e = add_ev(n, dest);
    if (!e) return ZXV_E_CAPACITY;
    e->has_probe = 1;
    e->probe_lp = lp;
    e->probe_time = now;
    if (e->held) return ZIND_E_HELD;
    if (hold_contradictions(p, n, now)) {
        zind_set_error("ztm_probe_record: the probe via %s to %s contradicts its advert; held "
                       "as BOTH",
                       neighbour, dest);
        return ZIND_E_HELD;
    }
    return ZXV_OK;
}

static int32_t min_qty(zind_qty a, zind_qty b, zind_qty *out) {
    int32_t c = 0, st = zind_qty_cmp(a, b, &c);
    if (st == ZXV_OK) *out = c <= 0 ? a : b;
    return st;
}

/* Why `n` is or is not usable for dest at now; *budget when usable. */
static int32_t eligible(const nb_t *n, const char *dest, uint64_t now, int32_t *lp, int *held,
                        zind_qty *budget) {
    const ev_t *e = NULL;
    int32_t st;
    *lp = ZXV_LP_NEITHER;
    *held = 0;
    *budget = ZIND_QTY_ZERO;
    for (uint32_t i = 0; i < n->n_ev; i++)
        if (strcmp(n->ev[i].name, dest) == 0) e = &n->ev[i];
    if (e) {
        *lp = ev_lp(n, e, now);
        *held = e->held;
    }
    if (!n->has_advert || now > n->adv.expires) return ZTM_E_EXPIRED;
    st = min_qty(n->adv.offer_kbps, n->agreement, budget);
    if (st != ZXV_OK) return st;
    if (zind_qty_sign(n->agreement) <= 0) return ZTM_E_AGREEMENT;
    if (*held) return ZIND_E_HELD;
    if (!n->adv.wan_up || zind_qty_sign(*budget) <= 0) return ZTM_E_NO_RELAY;
    if (*lp != ZXV_LP_TRUE) return ZXV_E_NOT_FOUND;
    return ZXV_OK;
}

int32_t ztm_neighbour_view(const ztm_planner *p, const char *id, const char *dest, uint64_t now,
                           ztm_view *out) {
    const nb_t *n = find_nb(p, id);
    if (!p || !out || !dest) return ZXV_E_ARG;
    if (!n) return ZTM_E_NEIGHBOUR;
    memset(out, 0, sizeof *out);
    out->why = eligible(n, dest, now, &out->lp, &out->held, &out->budget_kbps);
    out->eligible = out->why == ZXV_OK;
    out->advert_seq = n->has_advert ? n->adv.seq : 0;
    return ZXV_OK;
}

/* ===== owner resolution ===== */

static int32_t owner_msg(const ztm_planner *p, const nb_t *n, const ev_t *e, int32_t decision,
                         uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ztm/owner/v1", 12);
    zind_wr_str(&w, p->self);
    zind_wr_str(&w, n->id);
    zind_wr_str(&w, e->name);
    zind_wr_u64(&w, n->has_advert ? n->adv.seq : 0);
    zind_wr_u64(&w, e->has_probe ? e->probe_time : 0);
    zind_wr_u8(&w, (uint8_t) (e->has_probe ? e->probe_lp : ZXV_LP_NEITHER));
    zind_wr_u8(&w, (uint8_t) decision);
    *out_len = 12 + 3 + strlen(p->self) + strlen(n->id) + strlen(e->name) + 8 + 8 + 1 + 1;
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

static int32_t hold_args(const ztm_planner *p, const char *neighbour, const char *dest,
                         int32_t decision, nb_t **nn, ev_t **ee) {
    nb_t *n = find_nb(p, neighbour);
    ev_t *e;
    if (!n) return ZTM_E_NEIGHBOUR;
    if (decision != ZTM_USE && decision != ZTM_AVOID) return ZXV_E_ARG;
    e = dest ? find_ev(n, dest) : NULL;
    if (!e || !e->held) {
        zind_set_error("ztm: nothing is held for %s via %s", dest ? dest : "(null)", neighbour);
        return ZXV_E_STATE;
    }
    *nn = n;
    *ee = e;
    return ZXV_OK;
}

int32_t ztm_owner_message(const ztm_planner *p, const char *neighbour, const char *dest,
                          int32_t decision, uint8_t *buf, size_t cap, size_t *out_len) {
    nb_t *n = NULL;
    ev_t *e = NULL;
    int32_t st;
    if (!p || !out_len) return ZXV_E_ARG;
    st = hold_args(p, neighbour, dest, decision, &n, &e);
    if (st != ZXV_OK) return st;
    return owner_msg(p, n, e, decision, buf, cap, out_len);
}

int32_t ztm_hold_resolve(ztm_planner *p, const char *neighbour, const char *dest,
                         int32_t decision, const uint8_t sig[64]) {
    nb_t *n = NULL;
    ev_t *e = NULL;
    uint8_t msg[160];
    size_t len = 0;
    int32_t st;
    if (!p || !sig) return ZXV_E_ARG;
    st = hold_args(p, neighbour, dest, decision, &n, &e);
    if (st == ZXV_OK) st = owner_msg(p, n, e, decision, msg, sizeof msg, &len);
    if (st != ZXV_OK) return st;
    st = zxv_ed25519_verify(p->cap, p->owner_pk, msg, len, sig);
    if (st != ZXV_OK) {
        if (st == ZXV_E_VERIFY) {
            log_ev(p, n, e->name, ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZIND_REASON_TAMPER);
            zind_set_error("ztm_hold_resolve: the owner's signature does not verify");
            return ZTM_E_OWNER;
        }
        return st;
    }
    e->held = 0;
    e->has_override = 1;
    e->override_lp = decision == ZTM_USE ? ZXV_LP_TRUE : ZXV_LP_FALSE;
    e->override_seq = n->has_advert ? n->adv.seq : 0;
    /* the probe the owner ruled on is spent */
    e->has_probe = 0;
    return ZXV_OK;
}

/* ===== the plan ===== */

/* b1 / (c1 + 1) vs b2 / (c2 + 1): -1, 0, +1 */
static int32_t share_cmp(zind_qty b1, uint32_t c1, zind_qty b2, uint32_t c2, int32_t *out) {
    zind_qty x, y;
    int32_t st = zind_qty_mul(b1, (zind_qty){(int64_t) c2 + 1, 1}, &x);
    if (st == ZXV_OK) st = zind_qty_mul(b2, (zind_qty){(int64_t) c1 + 1, 1}, &y);
    if (st == ZXV_OK) st = zind_qty_cmp(x, y, out);
    return st;
}

/* What a new flow with demand dn would get on a relay of `budget` already
 * carrying `nf` flows (all with demands <= dn, in ascending order) under
 * max-min sharing: the others are filled first, it takes what is left. */
static int32_t would_get(zind_qty budget, const ztm_demand *d, const uint32_t *flows, uint32_t nf,
                         zind_qty dn, zind_qty *out) {
    zind_qty rem = budget, share, a;
    uint32_t left = nf + 1;
    int32_t st = ZXV_OK;
    for (uint32_t i = 0; st == ZXV_OK && i < nf; i++) {
        st = zind_qty_mul(rem, (zind_qty){1, (int64_t) left}, &share);
        if (st == ZXV_OK) st = min_qty(d[flows[i]].demand_kbps, share, &a);
        if (st == ZXV_OK) st = zind_qty_sub(rem, a, &rem);
        left--;
    }
    if (st == ZXV_OK) st = min_qty(dn, rem, out);
    return st;
}

int32_t ztm_plan_make(ztm_planner *p, const char *dest, uint64_t now, int32_t local_wan_up,
                      const ztm_demand *d, uint32_t nd, ztm_plan *out) {
    uint32_t elig[ZTM_MAX_NEIGHBOURS], ne = 0, order[ZTM_MAX_FLOWS], relay[ZTM_MAX_FLOWS],
        count[ZTM_MAX_NEIGHBOURS], lists[ZTM_MAX_NEIGHBOURS][ZTM_MAX_FLOWS];
    zind_qty budget[ZTM_MAX_NEIGHBOURS], alloc[ZTM_MAX_FLOWS];
    int32_t st, c = 0;
    if (!p || !out || !dest || (nd && !d) || nd > ZTM_MAX_FLOWS) return ZXV_E_ARG;
    memset(out, 0, sizeof *out);
    (void) zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &out->tag);
    if (!zind_ident_ok(dest, ZTM_DEST_MAX)) return ZXV_E_ARG;
    for (uint32_t i = 0; i < nd; i++) {
        if (!zind_ident_ok(d[i].flow, ZTM_ID_MAX) || !zind_qty_valid(d[i].demand_kbps) ||
            zind_qty_sign(d[i].demand_kbps) <= 0) {
            zind_set_error("ztm_plan_make: flow %u needs an id and a demand > 0", i);
            return ZXV_E_ARG;
        }
        for (uint32_t j = 0; j < i; j++)
            if (strcmp(d[i].flow, d[j].flow) == 0) return ZXV_E_DUPLICATE;
    }
    if (local_wan_up) {
        zind_set_error("ztm_plan_make: the local WAN is up; no fallback plan");
        return ZTM_E_WAN_UP;
    }
    for (uint32_t j = 0; j < p->n_nb; j++) {
        int32_t lp;
        int held;
        zind_qty b;
        st = eligible(&p->nb[j], dest, now, &lp, &held, &b);
        if (st == ZXV_E_RANGE) return st;
        if (st != ZXV_OK) continue;
        budget[ne] = b;
        count[ne] = 0;
        elig[ne++] = j;
    }
    out->eligible = ne;
    if (ne == 0) {
        zind_set_error("ztm_plan_make: no neighbour is eligible for %s", dest);
        return ZTM_E_NO_RELAY;
    }
    /* flows by (demand, id) ascending */
    for (uint32_t i = 0; i < nd; i++) {
        uint32_t k = i;
        order[i] = i;
        while (k > 0) {
            const ztm_demand *x = &d[order[k - 1]], *y = &d[order[k]];
            st = zind_qty_cmp(x->demand_kbps, y->demand_kbps, &c);
            if (st != ZXV_OK) return st;
            if (c < 0 || (c == 0 && strcmp(x->flow, y->flow) < 0)) break;
            uint32_t t = order[k - 1];
            order[k - 1] = order[k];
            order[k] = t;
            k--;
        }
    }
    /* each flow, smallest first, to the neighbour where it would get the most
     * under max-min sharing with the flows already there; ties go to the
     * larger fair share, then the lower latency, then the lower id */
    for (uint32_t i = 0; i < nd; i++) {
        uint32_t f = order[i], best = 0;
        zind_qty got[ZTM_MAX_NEIGHBOURS];
        for (uint32_t k = 0; k < ne; k++) {
            st = would_get(budget[k], d, lists[k], count[k], d[f].demand_kbps, &got[k]);
            if (st != ZXV_OK) return st;
        }
        for (uint32_t k = 1; k < ne; k++) {
            const nb_t *a = &p->nb[elig[k]], *b = &p->nb[elig[best]];
            st = zind_qty_cmp(got[k], got[best], &c);
            if (st == ZXV_OK && c == 0)
                st = share_cmp(budget[k], count[k], budget[best], count[best], &c);
            if (st != ZXV_OK) return st;
            if (c > 0 || (c == 0 && (a->adv.latency_ms < b->adv.latency_ms ||
                                     (a->adv.latency_ms == b->adv.latency_ms &&
                                      strcmp(a->id, b->id) < 0))))
                best = k;
        }
        relay[f] = best;
        lists[best][count[best]++] = f;
    }
    /* max-min fair split of each budget, smallest demands first */
    for (uint32_t k = 0; k < ne; k++) {
        zind_qty rem = budget[k];
        uint32_t left = count[k];
        for (uint32_t i = 0; i < nd; i++) {
            uint32_t f = order[i];
            zind_qty share;
            if (relay[f] != k) continue;
            st = zind_qty_mul(rem, (zind_qty){1, (int64_t) left}, &share);
            if (st == ZXV_OK) st = min_qty(d[f].demand_kbps, share, &alloc[f]);
            if (st == ZXV_OK) st = zind_qty_sub(rem, alloc[f], &rem);
            if (st != ZXV_OK) return st;
            left--;
        }
    }
    /* charge the rmag budgets: release the previous plan, set the limits */
    for (uint32_t j = 0; j < p->n_nb; j++) {
        int64_t un = 0, ud = 1, ln = 0, ld = 1;
        for (uint32_t k = 0; k < ne; k++)
            if (elig[k] == j) {
                ln = budget[k].num;
                ld = budget[k].den;
            }
        st = zxv_rmag_status(p->rmag, p->nb[j].budget, NULL, NULL, &un, &ud, NULL);
        if (st == ZXV_OK && un > 0) st = zxv_rmag_release(p->cap, p->rmag, p->nb[j].budget, un, ud);
        if (st == ZXV_OK) st = zxv_rmag_set_limit(p->cap, p->rmag, p->nb[j].budget, ln, ld);
        if (st != ZXV_OK) return st;
    }
    for (uint32_t i = 0; i < nd; i++) {
        const nb_t *n = &p->nb[elig[relay[i]]];
        st = zxv_rmag_consume(p->cap, p->rmag, n->budget, alloc[i].num, alloc[i].den);
        if (st != ZXV_OK) {
            zind_set_error("ztm_plan_make: the rmag budget of %s refused a charge", n->id);
            return st == ZXV_E_BUDGET ? ZXV_E_INTERNAL : st;
        }
        memcpy(out->a[i].flow, d[i].flow, strlen(d[i].flow) + 1);
        memcpy(out->a[i].relay, n->id, strlen(n->id) + 1);
        out->a[i].alloc_kbps = alloc[i];
        if (zind_qty_cmp(alloc[i], d[i].demand_kbps, &c) == ZXV_OK && c == 0) out->satisfied++;
    }
    out->n = nd;
    (void) zind_tag(ZXV_LP_TRUE, out->satisfied == nd ? ZXV_HEX_POL_NEUTRAL : ZXV_HEX_POL_ENTROPIC,
                    ZXV_HEX_PHASE_MANIFEST, &out->tag);
    return ZXV_OK;
}

int32_t ztm_budget_remaining(const ztm_planner *p, const char *id, zind_qty *out) {
    const nb_t *n = find_nb(p, id);
    int64_t rn = 0, rd = 1;
    int32_t st;
    if (!p || !out) return ZXV_E_ARG;
    if (!n) return ZTM_E_NEIGHBOUR;
    st = zxv_rmag_status(p->rmag, n->budget, &rn, &rd, NULL, NULL, NULL);
    if (st != ZXV_OK) return st;
    return zind_qty_make(rn, rd, out);
}
