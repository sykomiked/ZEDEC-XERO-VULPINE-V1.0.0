/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zaero_rx.c — the SEU receiver: redundant copies of each telemetry packet
 * are compared; disagreement or an on-board CRC failure holds the packet as
 * BOTH and writes the quarantine log; the optional majority policy releases
 * only after a log entry and a signed correction record; an analyst signs
 * the decision on a held packet. Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "zaero.h"

#define DOMAIN "aero-orbital/seu"
#define REC_CORRECTION 1
#define REC_REVIEW     2
#define DIGEST_IN_MAX  (1 + ZAO_MAX_COPIES * (3 + ZAO_TM_MAX_BYTES))

typedef struct {
    uint16_t len;
    uint8_t crc_ok;
    uint8_t bytes[ZAO_TM_MAX_BYTES];
} copy_t;

typedef struct {
    zao_key key;
    int32_t state;
    uint8_t tag;
    uint8_t n;
    int32_t chosen; /* copy holding the decided value, -1 when none */
    uint32_t holds;
    copy_t c[ZAO_MAX_COPIES];
} pkt_t;

struct zao_rx {
    const zxv_cap *cap;
    const zind_signer *op;
    zind_qsink *qs;
    zao_rx_cfg cfg;
    uint8_t analysts[ZAO_MAX_ANALYSTS][32];
    uint32_t n_analysts;
    pkt_t **pk;
    uint32_t n, alloc;
    uint32_t *index; /* open addressing: packet index + 1, 0 = empty */
    uint32_t index_mask;
    zind_chain *chain;
    uint32_t held;
};

const char *zao_status_name(int32_t status) {
    switch (status) {
    case ZAO_E_RANGE: return "ZAO_E_RANGE";
    case ZAO_E_CRC: return "ZAO_E_CRC";
    case ZAO_E_WIRE: return "ZAO_E_WIRE";
    case ZAO_E_SEU: return "ZAO_E_SEU";
    case ZAO_E_PACKET: return "ZAO_E_PACKET";
    case ZAO_E_ANALYST: return "ZAO_E_ANALYST";
    case ZAO_E_CORRECTED: return "ZAO_E_CORRECTED";
    case ZAO_E_PENDING: return "ZAO_E_PENDING";
    default: return zind_status_name(status);
    }
}

static uint32_t key_hash(zao_key k) {
    uint64_t h = ((uint64_t) k.scid << 40) ^ ((uint64_t) k.vcid << 32) ^ k.seq;
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33;
    return (uint32_t) h;
}

static int key_eq(zao_key a, zao_key b) {
    return a.scid == b.scid && a.vcid == b.vcid && a.seq == b.seq;
}

static pkt_t *find(const zao_rx *rx, zao_key k) {
    if (!rx) return NULL;
    for (uint32_t i = key_hash(k) & rx->index_mask;; i = (i + 1) & rx->index_mask) {
        uint32_t v = rx->index[i];
        if (!v) return NULL;
        if (key_eq(rx->pk[v - 1]->key, k)) return rx->pk[v - 1];
    }
}

int32_t zao_rx_create(const zxv_cap *cap, const zind_signer *operator_key, zxv_hexlog *qlog,
                      const zao_rx_cfg *cfg, zao_rx **out) {
    zao_rx *rx;
    uint32_t icap = 2;
    int32_t st;
    if (!cap || !operator_key || !qlog || !cfg || !out) {
        zind_set_error("zao_rx_create: a cap, an operator key, a quarantine log and a config "
                       "are required");
        return ZXV_E_ARG;
    }
    *out = NULL;
    if (cfg->copies < 1 || cfg->copies > ZAO_MAX_COPIES ||
        (cfg->policy != ZAO_POLICY_HOLD && cfg->policy != ZAO_POLICY_MAJORITY) ||
        cfg->capacity < 1 || cfg->capacity > ZAO_MAX_PACKETS) {
        zind_set_error("zao_rx_create: copies 1..5, a known policy and capacity 1..65536");
        return ZXV_E_ARG;
    }
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN | ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zind_set_error("zao_rx_create: the cap needs CRYPTO_SIGN and ATTEST");
        return st;
    }
    while (icap < 2u * cfg->capacity) icap <<= 1;
    rx = (zao_rx *) calloc(1, sizeof *rx);
    if (!rx) return ZXV_E_NOMEM;
    rx->cap = cap;
    rx->op = operator_key;
    rx->cfg = *cfg;
    rx->index = (uint32_t *) calloc(icap, sizeof *rx->index);
    rx->index_mask = icap - 1;
    st = rx->index ? ZXV_OK : ZXV_E_NOMEM;
    if (st == ZXV_OK) st = zind_qsink_create(cap, qlog, NULL, &rx->qs);
    if (st == ZXV_OK) st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &rx->chain);
    if (st != ZXV_OK) {
        zao_rx_destroy(rx);
        return st;
    }
    *out = rx;
    return ZXV_OK;
}

void zao_rx_destroy(zao_rx *rx) {
    if (!rx) return;
    for (uint32_t i = 0; i < rx->n; i++) free(rx->pk[i]);
    free(rx->pk);
    free(rx->index);
    zind_qsink_destroy(rx->qs);
    zind_chain_destroy(rx->chain);
    free(rx);
}

int32_t zao_analyst_add(zao_rx *rx, const uint8_t pk[ZIND_PK_BYTES]) {
    uint8_t id[32];
    int32_t st;
    if (!rx || !pk) return ZXV_E_ARG;
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < rx->n_analysts; i++)
        if (memcmp(rx->analysts[i], id, 32) == 0) return ZXV_E_DUPLICATE;
    if (rx->n_analysts >= ZAO_MAX_ANALYSTS) return ZXV_E_CAPACITY;
    memcpy(rx->analysts[rx->n_analysts++], id, 32);
    return ZXV_OK;
}

/* ===== evidence ===== */

static int copy_eq(const copy_t *a, const copy_t *b) {
    return a->len == b->len && memcmp(a->bytes, b->bytes, a->len) == 0;
}

/* good copies: how many distinct strings, the largest identical group and
 * its first member, and how many failed their CRC */
static void tally(const pkt_t *p, uint8_t *crc_bad, uint8_t *distinct, uint8_t *best_n,
                  int32_t *best_i) {
    *crc_bad = 0;
    *distinct = 0;
    *best_n = 0;
    *best_i = -1;
    for (uint32_t i = 0; i < p->n; i++) {
        uint8_t same = 0;
        int first = 1;
        if (!p->c[i].crc_ok) {
            (*crc_bad)++;
            continue;
        }
        for (uint32_t j = 0; j < p->n; j++) {
            if (!p->c[j].crc_ok || !copy_eq(&p->c[i], &p->c[j])) continue;
            if (j < i) first = 0;
            same++;
        }
        if (first) {
            (*distinct)++;
            if (same > *best_n) {
                *best_n = same;
                *best_i = (int32_t) i;
            }
        }
    }
}

static int32_t copies_digest(const pkt_t *p, uint8_t out[32]) {
    static const uint32_t cap = DIGEST_IN_MAX;
    uint8_t *buf = (uint8_t *) malloc(cap);
    zind_wr w;
    int32_t st;
    if (!buf) return ZXV_E_NOMEM;
    zind_wr_init(&w, buf, cap);
    zind_wr_u8(&w, p->n);
    for (uint32_t i = 0; i < p->n; i++) {
        zind_wr_u8(&w, p->c[i].crc_ok);
        zind_wr_u16(&w, p->c[i].len);
        zind_wr_bytes(&w, p->c[i].bytes, p->c[i].len);
    }
    st = w.overflow ? ZXV_E_INTERNAL : zxv_sha3_256(buf, w.len, out);
    free(buf);
    return st;
}

static int32_t log_event(zao_rx *rx, uint8_t tag, uint8_t reason, uint32_t submitter,
                         const uint8_t *data, size_t len) {
    uint8_t subj[32];
    int32_t st = zind_subject("aero-orbital/packet", data, len, subj);
    if (st == ZXV_OK) st = zind_qsink_event(rx->qs, tag, reason, submitter, subj, NULL);
    return st;
}

static int32_t log_pkt(zao_rx *rx, const pkt_t *p, uint8_t tag, uint8_t reason) {
    uint8_t k[7];
    k[0] = (uint8_t) p->key.scid;
    k[1] = (uint8_t) (p->key.scid >> 8);
    k[2] = p->key.vcid;
    for (int i = 0; i < 4; i++) k[3 + i] = (uint8_t) (p->key.seq >> (8 * i));
    return log_event(rx, tag, reason, (uint32_t) p->key.seq, k, sizeof k);
}

static int32_t to_phase(uint8_t tag, uint8_t want, uint8_t *out) {
    uint8_t ph = 0, t = tag;
    int32_t st = zxv_hex_unpack(tag, NULL, NULL, &ph);
    for (int i = 0; st == ZXV_OK && ph != want && i < 3; i++) {
        uint8_t next = 0;
        st = zxv_hex_phase_next(ph, &next);
        if (st == ZXV_OK) st = zxv_hex_step(t, next, &t);
        ph = next;
    }
    *out = t;
    return st;
}

/* Hold as BOTH (keeping the phase) and log it. The packet is held even if
 * the log refuses: holding is always safe; the log's status is returned. */
static int32_t hold(zao_rx *rx, pkt_t *p, uint8_t reason) {
    uint8_t t = 0;
    int32_t st = zxv_hex_set(p->tag, ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, &t);
    if (st != ZXV_OK) return st;
    p->tag = t;
    p->state = ZAO_PK_HELD;
    p->chosen = -1;
    p->holds++;
    rx->held++;
    st = log_pkt(rx, p, t, reason);
    if (st != ZXV_OK) return st;
    zind_set_error("zao: packet %u/%u/%u held as BOTH for an analyst (%s)", p->key.scid,
                   p->key.vcid, p->key.seq,
                   reason == ZAO_REASON_SEU    ? "copies disagree"
                   : reason == ZAO_REASON_CRC  ? "an on-board CRC failed"
                                               : "a differing copy after release");
    return ZAO_E_SEU;
}

static int32_t evaluate(zao_rx *rx, pkt_t *p) {
    uint8_t crc_bad, distinct, best_n, t = 0, both = 0;
    int32_t best_i, st;
    tally(p, &crc_bad, &distinct, &best_n, &best_i);
    if (crc_bad == 0 && distinct == 1) {
        st = zxv_hex_set(p->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &t);
        if (st == ZXV_OK) st = to_phase(t, ZXV_HEX_PHASE_MANIFEST, &t);
        if (st != ZXV_OK) return st;
        p->tag = t;
        p->state = ZAO_PK_RELEASED;
        p->chosen = 0;
        return ZXV_OK;
    }
    if (rx->cfg.policy == ZAO_POLICY_MAJORITY && best_i >= 0 && 2u * best_n > p->n) {
        uint8_t payload[64], dig[32];
        zind_wr w;
        st = zxv_hex_set(p->tag, ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, &both);
        /* the log entry first: no correction without one */
        if (st == ZXV_OK) st = log_pkt(rx, p, both, ZAO_REASON_CORRECTED);
        if (st == ZXV_OK) st = copies_digest(p, dig);
        if (st == ZXV_OK) {
            zind_wr_init(&w, payload, sizeof payload);
            zind_wr_u8(&w, 1);
            zind_wr_u8(&w, REC_CORRECTION);
            zind_wr_u16(&w, p->key.scid);
            zind_wr_u8(&w, p->key.vcid);
            zind_wr_u32(&w, p->key.seq);
            zind_wr_bytes(&w, dig, 32);
            zind_wr_u8(&w, p->n);
            zind_wr_u8(&w, crc_bad);
            zind_wr_u8(&w, distinct);
            zind_wr_u8(&w, (uint8_t) best_i);
            st = w.overflow ? ZXV_E_INTERNAL
                            : zind_chain_append(rx->cap, rx->chain, rx->op, REC_CORRECTION,
                                                payload, w.len, NULL, NULL);
        }
        if (st == ZXV_OK) st = zxv_hex_set(p->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, &t);
        if (st == ZXV_OK) st = to_phase(t, ZXV_HEX_PHASE_MANIFEST, &t);
        if (st != ZXV_OK) {
            int32_t h = hold(rx, p, distinct > 1 ? ZAO_REASON_SEU : ZAO_REASON_CRC);
            (void) h;
            zind_set_error("zao: the majority could not be logged and recorded; the packet is "
                           "held");
            return st;
        }
        p->tag = t;
        p->state = ZAO_PK_CORRECTED;
        p->chosen = best_i;
        zind_set_error("zao: packet %u/%u/%u released by a logged majority (%u of %u copies)",
                       p->key.scid, p->key.vcid, p->key.seq, best_n, p->n);
        return ZAO_E_CORRECTED;
    }
    return hold(rx, p, distinct > 1 ? ZAO_REASON_SEU : ZAO_REASON_CRC);
}

int32_t zao_rx_submit(zao_rx *rx, const uint8_t *frames, size_t nframes, zao_key *out_key) {
    uint8_t buf[ZAO_WIRE_MAX_BYTES];
    size_t len = 0;
    zao_tm tm;
    zao_key k;
    pkt_t *p;
    copy_t *c;
    int crc_ok;
    int32_t st;
    if (!rx || !frames) return ZXV_E_ARG;
    st = zao_wire_decode(frames, nframes, buf, sizeof buf, &len, NULL);
    if (st != ZXV_OK) return st == ZXV_E_BUFFER ? ZAO_E_WIRE : st;
    st = len <= ZAO_TM_MAX_BYTES ? zao_tm_decode(buf, len, &tm) : ZXV_E_FORMAT;
    if (st != ZXV_OK && st != ZAO_E_CRC) {
        uint8_t t = 0;
        if (zind_tag(ZXV_LP_BOTH, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &t) == ZXV_OK)
            (void) log_event(rx, t, ZAO_REASON_UNPARSED, 0, buf, len);
        zind_set_error("zao_rx_submit: the bytes are not a telemetry packet; logged");
        return ZXV_E_FORMAT;
    }
    crc_ok = st == ZXV_OK;
    k.scid = tm.scid;
    k.vcid = tm.vcid;
    k.seq = tm.seq;
    if (out_key) *out_key = k;
    p = find(rx, k);
    if (!p) {
        uint32_t i;
        if (rx->n >= rx->cfg.capacity) {
            zind_set_error("zao_rx_submit: the packet table is full (%u)", rx->cfg.capacity);
            return ZXV_E_CAPACITY;
        }
        if (rx->n == rx->alloc) {
            uint32_t a = rx->alloc ? rx->alloc * 2 : 16;
            pkt_t **np = (pkt_t **) realloc(rx->pk, (size_t) a * sizeof *np);
            if (!np) return ZXV_E_NOMEM;
            rx->pk = np;
            rx->alloc = a;
        }
        p = (pkt_t *) calloc(1, sizeof *p);
        if (!p) return ZXV_E_NOMEM;
        p->key = k;
        p->state = ZAO_PK_COLLECTING;
        p->chosen = -1;
        st = zind_tag(ZXV_LP_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_EMERGING, &p->tag);
        if (st != ZXV_OK) {
            free(p);
            return st;
        }
        rx->pk[rx->n] = p;
        for (i = key_hash(k) & rx->index_mask; rx->index[i]; i = (i + 1) & rx->index_mask) {
        }
        rx->index[i] = ++rx->n;
    }
    if (p->state == ZAO_PK_REJECTED) {
        zind_set_error("zao_rx_submit: packet %u/%u/%u was rejected by an analyst", k.scid,
                       k.vcid, k.seq);
        return ZIND_E_FROZEN;
    }
    if (p->state == ZAO_PK_RELEASED || p->state == ZAO_PK_CORRECTED ||
        p->state == ZAO_PK_ACCEPTED) {
        const copy_t *v = &p->c[p->chosen];
        if (crc_ok && v->len == len && memcmp(v->bytes, buf, len) == 0) {
            zind_set_error("zao_rx_submit: an identical copy of a decided packet");
            return ZIND_E_REPLAY;
        }
        if (p->n < ZAO_MAX_COPIES) {
            c = &p->c[p->n++];
            c->len = (uint16_t) len;
            c->crc_ok = (uint8_t) crc_ok;
            memcpy(c->bytes, buf, len);
        }
        return hold(rx, p, ZAO_REASON_LATE);
    }
    if (p->n >= ZAO_MAX_COPIES) {
        zind_set_error("zao_rx_submit: packet %u/%u/%u already has %u copies", k.scid, k.vcid,
                       k.seq, ZAO_MAX_COPIES);
        return ZXV_E_CAPACITY;
    }
    c = &p->c[p->n++];
    c->len = (uint16_t) len;
    c->crc_ok = (uint8_t) crc_ok;
    memcpy(c->bytes, buf, len);
    if (p->state == ZAO_PK_HELD) {
        zind_set_error("zao_rx_submit: packet %u/%u/%u is held; the copy was kept as evidence",
                       k.scid, k.vcid, k.seq);
        return ZIND_E_HELD;
    }
    if (p->n >= rx->cfg.copies) return evaluate(rx, p);
    return ZXV_OK;
}

int32_t zao_rx_close(zao_rx *rx, zao_key key) {
    pkt_t *p = find(rx, key);
    if (!rx) return ZXV_E_ARG;
    if (!p) return ZAO_E_PACKET;
    if (p->state != ZAO_PK_COLLECTING) return ZXV_E_STATE;
    return evaluate(rx, p);
}

int32_t zao_rx_info(const zao_rx *rx, zao_key key, zao_pkt_info *out) {
    const pkt_t *p = find(rx, key);
    int32_t bi;
    uint8_t bn;
    if (!rx || !out) return ZXV_E_ARG;
    if (!p) return ZAO_E_PACKET;
    memset(out, 0, sizeof *out);
    out->state = p->state;
    out->tag = p->tag;
    out->copies = p->n;
    out->holds = p->holds;
    tally(p, &out->crc_bad, &out->distinct, &bn, &bi);
    for (uint32_t i = 0; i < p->n; i++)
        for (uint32_t j = i + 1; j < p->n; j++) {
            uint32_t d = zao_bit_diff(p->c[i].bytes, p->c[i].len, p->c[j].bytes, p->c[j].len);
            if (d > out->max_bit_diff) out->max_bit_diff = d;
        }
    return ZXV_OK;
}

int32_t zao_rx_value(const zao_rx *rx, zao_key key, zao_tm *out) {
    const pkt_t *p = find(rx, key);
    if (!rx || !out) return ZXV_E_ARG;
    if (!p) return ZAO_E_PACKET;
    switch (p->state) {
    case ZAO_PK_COLLECTING: return ZAO_E_PENDING;
    case ZAO_PK_HELD: return ZIND_E_HELD;
    case ZAO_PK_REJECTED: return ZIND_E_FROZEN;
    default: return zao_tm_decode(p->c[p->chosen].bytes, p->c[p->chosen].len, out);
    }
}

int32_t zao_rx_copy(const zao_rx *rx, zao_key key, uint32_t i, uint8_t *buf, size_t cap,
                    size_t *out_len, int32_t *out_crc_ok) {
    const pkt_t *p = find(rx, key);
    if (!rx || !out_len) return ZXV_E_ARG;
    if (!p) return ZAO_E_PACKET;
    if (i >= p->n) return ZXV_E_NOT_FOUND;
    *out_len = p->c[i].len;
    if (out_crc_ok) *out_crc_ok = p->c[i].crc_ok;
    if (!buf || cap < p->c[i].len) return ZXV_E_BUFFER;
    memcpy(buf, p->c[i].bytes, p->c[i].len);
    return ZXV_OK;
}

int32_t zao_rx_counts(const zao_rx *rx, uint32_t *out_packets, uint32_t *out_held,
                      uint32_t *out_logged) {
    uint64_t w = 0;
    if (!rx) return ZXV_E_ARG;
    if (out_packets) *out_packets = rx->n;
    if (out_held) *out_held = rx->held;
    if (out_logged) {
        (void) zind_qsink_written(rx->qs, &w);
        *out_logged = (uint32_t) w;
    }
    return ZXV_OK;
}

/* ===== analyst review ===== */

static int32_t review_msg(zao_key key, uint32_t holds, const uint8_t dig[32], int32_t decision,
                          uint32_t copy, uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "zao/seu/v1", 10);
    zind_wr_u16(&w, key.scid);
    zind_wr_u8(&w, key.vcid);
    zind_wr_u32(&w, key.seq);
    zind_wr_u32(&w, holds);
    zind_wr_bytes(&w, dig, 32);
    zind_wr_u8(&w, (uint8_t) decision);
    zind_wr_u8(&w, (uint8_t) copy);
    *out_len = 10 + 2 + 1 + 4 + 4 + 32 + 1 + 1;
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

static int32_t check_review(const pkt_t *p, int32_t decision, uint32_t copy) {
    if (decision != ZAO_ACCEPT && decision != ZAO_REJECT) return ZXV_E_ARG;
    if (decision == ZAO_ACCEPT && (copy >= p->n || !p->c[copy].crc_ok)) {
        zind_set_error("zao: ACCEPT names a recorded copy whose on-board CRC matched");
        return ZXV_E_ARG;
    }
    if (decision == ZAO_REJECT && copy != 0) return ZXV_E_ARG;
    if (p->state != ZAO_PK_HELD) {
        zind_set_error("zao: the packet is not held");
        return ZXV_E_STATE;
    }
    return ZXV_OK;
}

int32_t zao_review_message(const zao_rx *rx, zao_key key, int32_t decision, uint32_t copy,
                           uint8_t *buf, size_t cap, size_t *out_len) {
    const pkt_t *p = find(rx, key);
    uint8_t dig[32];
    int32_t st;
    if (!rx || !out_len) return ZXV_E_ARG;
    if (!p) return ZAO_E_PACKET;
    st = check_review(p, decision, copy);
    if (st == ZXV_OK) st = copies_digest(p, dig);
    if (st != ZXV_OK) return st;
    return review_msg(key, p->holds, dig, decision, copy, buf, cap, out_len);
}

int32_t zao_review_resolve(zao_rx *rx, zao_key key, const uint8_t analyst_pk[ZIND_PK_BYTES],
                           int32_t decision, uint32_t copy, const uint8_t sig[ZIND_SIG_BYTES]) {
    pkt_t *p = find(rx, key);
    uint8_t id[32], dig[32], msg[64], *rec, t = 0;
    size_t len = 0;
    uint32_t i;
    zind_wr w;
    int32_t st;
    if (!rx || !analyst_pk || !sig) return ZXV_E_ARG;
    if (!p) return ZAO_E_PACKET;
    st = check_review(p, decision, copy);
    if (st != ZXV_OK) return st;
    st = zind_key_id(analyst_pk, id);
    if (st != ZXV_OK) return st;
    for (i = 0; i < rx->n_analysts; i++)
        if (memcmp(rx->analysts[i], id, 32) == 0) break;
    if (i == rx->n_analysts) {
        zind_set_error("zao_review_resolve: the key is not a registered analyst");
        return ZAO_E_ANALYST;
    }
    st = copies_digest(p, dig);
    if (st == ZXV_OK) st = review_msg(key, p->holds, dig, decision, copy, msg, sizeof msg, &len);
    if (st == ZXV_OK) st = zind_verify(rx->cap, analyst_pk, msg, len, ZAO_REVIEW_CTX, sig);
    if (st != ZXV_OK) {
        if (st == ZXV_E_VERIFY &&
            zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &t) == ZXV_OK)
            (void) log_event(rx, t, ZIND_REASON_TAMPER, (uint32_t) key.seq, sig, ZIND_SIG_BYTES);
        zind_set_error("zao_review_resolve: the analyst's signature does not verify");
        return st;
    }
    if (decision == ZAO_ACCEPT) {
        st = zxv_hex_set(p->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &t);
        if (st == ZXV_OK) st = to_phase(t, ZXV_HEX_PHASE_MANIFEST, &t);
    } else {
        st = zxv_hex_set(p->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_FORBIDDEN, &t);
    }
    if (st != ZXV_OK) return st;
    rec = (uint8_t *) malloc(128 + ZIND_SIG_BYTES);
    if (!rec) return ZXV_E_NOMEM;
    zind_wr_init(&w, rec, 128 + ZIND_SIG_BYTES);
    zind_wr_u8(&w, 1);
    zind_wr_u8(&w, REC_REVIEW);
    zind_wr_u16(&w, key.scid);
    zind_wr_u8(&w, key.vcid);
    zind_wr_u32(&w, key.seq);
    zind_wr_u32(&w, p->holds);
    zind_wr_bytes(&w, dig, 32);
    zind_wr_u8(&w, (uint8_t) decision);
    zind_wr_u8(&w, (uint8_t) copy);
    zind_wr_bytes(&w, id, 32);
    zind_wr_bytes(&w, sig, ZIND_SIG_BYTES);
    st = w.overflow ? ZXV_E_INTERNAL
                    : zind_chain_append(rx->cap, rx->chain, rx->op, REC_REVIEW, rec, w.len, NULL,
                                        NULL);
    free(rec);
    if (st != ZXV_OK) return st;
    p->tag = t;
    if (decision == ZAO_ACCEPT) {
        p->state = ZAO_PK_ACCEPTED;
        p->chosen = (int32_t) copy;
    } else {
        p->state = ZAO_PK_REJECTED;
        (void) log_pkt(rx, p, t, ZIND_REASON_FORBIDDEN);
    }
    return ZXV_OK;
}

int32_t zao_decisions_export(const zao_rx *rx, uint8_t *buf, size_t cap, size_t *out_len) {
    if (!rx || !out_len) return ZXV_E_ARG;
    return zind_chain_export(rx->chain, buf, cap, out_len);
}

int32_t zao_decisions_verify(const zxv_cap *cap, const uint8_t *buf, size_t len,
                             const uint8_t *const *analyst_pks, size_t n_analysts,
                             const uint8_t *operator_pk, uint32_t *out_reviews,
                             uint32_t *out_corrections, uint32_t *out_bad) {
    zind_chain *c = NULL;
    const uint8_t *trust[1];
    uint32_t n = 0, bad = UINT32_MAX, reviews = 0, corr = 0;
    int32_t st;
    if (!cap || !buf || !operator_pk || (n_analysts && !analyst_pks) || n_analysts > 4096)
        return ZXV_E_ARG;
    if (out_bad) *out_bad = UINT32_MAX;
    st = zind_chain_import(DOMAIN, buf, len, &c);
    if (st != ZXV_OK) return st;
    trust[0] = operator_pk;
    st = zind_chain_verify(cap, c, trust, 1, &bad);
    if (st == ZXV_OK) (void) zind_chain_count(c, &n);
    for (uint32_t i = 0; st == ZXV_OK && i < n; i++) {
        zind_record rec;
        zind_rd rd;
        zao_key key;
        uint8_t ver, kind;
        bad = i;
        zind_chain_at(c, i, &rec);
        zind_rd_init(&rd, rec.payload, rec.payload_len);
        ver = zind_rd_u8(&rd);
        kind = zind_rd_u8(&rd);
        key.scid = zind_rd_u16(&rd);
        key.vcid = zind_rd_u8(&rd);
        key.seq = zind_rd_u32(&rd);
        if (rd.bad || ver != 1 || kind != rec.kind) {
            st = ZXV_E_FORMAT;
            break;
        }
        if (kind == REC_CORRECTION) {
            uint8_t nn, cb, ds, ch;
            rd.pos += 32;
            nn = zind_rd_u8(&rd);
            cb = zind_rd_u8(&rd);
            ds = zind_rd_u8(&rd);
            ch = zind_rd_u8(&rd);
            if (rd.bad || rd.pos != rd.len || nn == 0 || nn > ZAO_MAX_COPIES || cb >= nn ||
                ds == 0 || ch >= nn) {
                st = ZXV_E_FORMAT;
                break;
            }
            corr++;
        } else if (kind == REC_REVIEW) {
            uint8_t dig[32], kid[32], msg[64], dec, cp, pkid[32];
            uint32_t holds;
            size_t mlen = 0, k;
            holds = zind_rd_u32(&rd);
            for (int b = 0; b < 32; b++) dig[b] = zind_rd_u8(&rd);
            dec = zind_rd_u8(&rd);
            cp = zind_rd_u8(&rd);
            for (int b = 0; b < 32; b++) kid[b] = zind_rd_u8(&rd);
            if (rd.bad || rd.len - rd.pos != ZIND_SIG_BYTES ||
                (dec != ZAO_ACCEPT && dec != ZAO_REJECT) || cp >= ZAO_MAX_COPIES) {
                st = ZXV_E_FORMAT;
                break;
            }
            for (k = 0; k < n_analysts; k++) {
                if (!analyst_pks[k]) {
                    st = ZXV_E_ARG;
                    break;
                }
                if (zind_key_id(analyst_pks[k], pkid) == ZXV_OK && memcmp(pkid, kid, 32) == 0)
                    break;
            }
            if (st != ZXV_OK) break;
            if (k == n_analysts) {
                zind_set_error("zao_decisions_verify: record %u names an unknown analyst", i);
                st = ZIND_E_UNTRUSTED;
                break;
            }
            st = review_msg(key, holds, dig, dec, cp, msg, sizeof msg, &mlen);
            if (st == ZXV_OK)
                st = zind_verify(cap, analyst_pks[k], msg, mlen, ZAO_REVIEW_CTX, rd.buf + rd.pos);
            if (st != ZXV_OK) {
                zind_set_error("zao_decisions_verify: record %u has a bad analyst signature", i);
                st = st == ZXV_E_VERIFY ? ZIND_E_UNTRUSTED : st;
                break;
            }
            reviews++;
        } else {
            st = ZXV_E_FORMAT;
        }
    }
    zind_chain_destroy(c);
    if (st == ZXV_OK) {
        if (out_reviews) *out_reviews = reviews;
        if (out_corrections) *out_corrections = corr;
    } else if (out_bad) {
        *out_bad = bad;
    }
    return st;
}
