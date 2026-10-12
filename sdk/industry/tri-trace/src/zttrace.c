/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zttrace.c — tri-trace: per-item custody chains signed by the scanning
 * party, duplicate scans frozen as BOTH for a human reviewer, and offline
 * verification of an exported chain. The live registry and the offline
 * verifier run the same custody machine. Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "zttrace.h"

#define DOMAIN "tri-trace"
#define MAX_REVIEWERS 16u

/* ===== the custody machine ===== */

typedef struct {
    int32_t state; /* 0 = no events yet */
    char holder[ZTT_PARTY_MAX + 1];
    char to[ZTT_PARTY_MAX + 1];
    uint64_t last_time;
    uint8_t tag;
    uint8_t conflict_hash[32];
    uint32_t events, conflicts;
} mach_t;

static int32_t to_phase(uint8_t tag, uint8_t want, uint8_t *out) {
    uint8_t ph = 0, t = tag;
    int32_t st = zxv_hex_unpack(tag, NULL, NULL, &ph);
    /* move forward along the Gray cycle until `want` (at most three steps) */
    for (int i = 0; st == ZXV_OK && ph != want && i < 3; i++) {
        uint8_t next = 0;
        st = zxv_hex_phase_next(ph, &next);
        if (st == ZXV_OK) st = zxv_hex_step(t, next, &t);
        ph = next;
    }
    *out = t;
    return st;
}

/* Classify and apply a scan. *conflict = 1 if it does not fit the history.
 * Refusals (HELD, FROZEN) leave the machine unchanged. */
static int32_t mach_scan(mach_t *m, int32_t kind, const char *party, const char *to, uint64_t time,
                         const uint8_t hash[32], int *conflict) {
    int ok = 0;
    uint8_t t = m->tag;
    int32_t st = ZXV_OK;
    *conflict = 0;
    if (m->state == ZTT_ST_COUNTERFEIT) return ZIND_E_FROZEN;
    if (m->state == ZTT_ST_CONFLICT) return ZIND_E_HELD;
    if (m->state == 0) {
        ok = kind == ZTT_EV_COMMISSION;
    } else if (time >= m->last_time) {
        switch (kind) {
        case ZTT_EV_SHIP:
            ok = m->state == ZTT_ST_HELD && strcmp(party, m->holder) == 0 && strcmp(to, party) != 0;
            break;
        case ZTT_EV_RECEIVE: ok = m->state == ZTT_ST_TRANSIT && strcmp(party, m->to) == 0; break;
        case ZTT_EV_DISPENSE:
            ok = m->state == ZTT_ST_HELD && strcmp(party, m->holder) == 0;
            break;
        default: ok = 0; break; /* COMMISSION of an existing tag: a clone */
        }
    }
    if (!ok) {
        if (m->state == 0) st = zind_tag(ZXV_LP_BOTH, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &t);
        else st = zxv_hex_set(m->tag, ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, &t);
        if (st != ZXV_OK) return st;
        m->tag = t;
        m->state = ZTT_ST_CONFLICT;
        memcpy(m->conflict_hash, hash, 32);
        m->conflicts++;
        m->events++;
        *conflict = 1;
        return ZXV_OK;
    }
    switch (kind) {
    case ZTT_EV_COMMISSION:
        st = zind_tag(ZXV_LP_TRUE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &t);
        if (st == ZXV_OK) {
            m->state = ZTT_ST_HELD;
            memcpy(m->holder, party, strlen(party) + 1);
        }
        break;
    case ZTT_EV_SHIP:
        st = to_phase(m->tag, ZXV_HEX_PHASE_EMERGING, &t);
        if (st == ZXV_OK) {
            m->state = ZTT_ST_TRANSIT;
            memcpy(m->to, to, strlen(to) + 1);
            m->holder[0] = 0;
        }
        break;
    case ZTT_EV_RECEIVE:
        m->state = ZTT_ST_HELD;
        memcpy(m->holder, party, strlen(party) + 1);
        m->to[0] = 0;
        break;
    default: /* DISPENSE */
        st = to_phase(m->tag, ZXV_HEX_PHASE_MANIFEST, &t);
        if (st == ZXV_OK) m->state = ZTT_ST_DISPENSED;
        break;
    }
    if (st != ZXV_OK) return st;
    m->tag = t;
    m->last_time = time;
    m->events++;
    return ZXV_OK;
}

static int32_t mach_review(mach_t *m, int32_t outcome, const char *holder) {
    uint8_t t = 0;
    int32_t st;
    if (m->state != ZTT_ST_CONFLICT) return ZXV_E_STATE;
    if (outcome == ZTT_GENUINE) {
        st = zxv_hex_set(m->tag, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &t);
        if (st != ZXV_OK) return st;
        m->state = ZTT_ST_HELD;
        memcpy(m->holder, holder, strlen(holder) + 1);
        m->to[0] = 0;
    } else {
        st = zxv_hex_set(m->tag, ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_FORBIDDEN, &t);
        if (st != ZXV_OK) return st;
        m->state = ZTT_ST_COUNTERFEIT;
    }
    m->tag = t;
    m->events++;
    return ZXV_OK;
}

/* ===== payloads ===== */

static int32_t scan_payload(zind_wr *w, int32_t kind, const char *item, const char *party,
                            const char *to, uint64_t time) {
    zind_wr_u8(w, 1);
    zind_wr_u8(w, (uint8_t) kind);
    zind_wr_str(w, item);
    zind_wr_str(w, party);
    zind_wr_str(w, to ? to : "");
    zind_wr_u64(w, time);
    return w->overflow ? ZXV_E_TOO_LONG : ZXV_OK;
}

static int32_t review_msg(const char *item, const uint8_t conflict_hash[32], int32_t outcome,
                          const char *holder, uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ztt/review/v1", 13);
    zind_wr_str(&w, item);
    zind_wr_bytes(&w, conflict_hash, 32);
    zind_wr_u8(&w, (uint8_t) outcome);
    zind_wr_str(&w, holder);
    *out_len = 13 + 1 + strlen(item) + 32 + 1 + 1 + strlen(holder);
    return w.overflow ? ZXV_E_BUFFER : ZXV_OK;
}

/* ===== registry ===== */

typedef struct {
    char id[ZTT_PARTY_MAX + 1];
    uint8_t key_id[32];
} party_t;

typedef struct {
    char tag[ZTT_TAG_MAX + 1];
    mach_t m;
    zind_chain *chain;
} item_t;

struct ztt_registry {
    const zxv_cap *cap;
    const zind_signer *op;
    zind_qsink *qs;
    party_t parties[ZTT_MAX_PARTIES];
    uint32_t n_parties;
    uint8_t reviewers[MAX_REVIEWERS][32];
    uint32_t n_reviewers;
    item_t *items;
    uint32_t n_items, alloc;
    zind_seen *scans;
};

const char *ztt_status_name(int32_t status) {
    switch (status) {
    case ZTT_E_PARTY: return "ZTT_E_PARTY";
    case ZTT_E_ITEM: return "ZTT_E_ITEM";
    case ZTT_E_REVIEWER: return "ZTT_E_REVIEWER";
    case ZTT_E_CONFLICT: return "ZTT_E_CONFLICT";
    default: return zind_status_name(status);
    }
}

int32_t ztt_registry_create(const zxv_cap *cap, const zind_signer *operator_key, zxv_hexlog *qlog,
                            ztt_registry **out) {
    ztt_registry *r;
    int32_t st;
    if (!cap || !operator_key || !out) return ZXV_E_ARG;
    *out = NULL;
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("ztt_registry_create: the cap needs CRYPTO_SIGN");
        return st;
    }
    r = (ztt_registry *) calloc(1, sizeof *r);
    if (!r) return ZXV_E_NOMEM;
    r->cap = cap;
    r->op = operator_key;
    st = zind_seen_create(1u << 18, &r->scans);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &r->qs);
    if (st != ZXV_OK) {
        ztt_registry_destroy(r);
        return st;
    }
    *out = r;
    return ZXV_OK;
}

void ztt_registry_destroy(ztt_registry *r) {
    if (!r) return;
    for (uint32_t i = 0; i < r->n_items; i++) zind_chain_destroy(r->items[i].chain);
    free(r->items);
    zind_seen_destroy(r->scans);
    zind_qsink_destroy(r->qs);
    free(r);
}

static int find_party(const ztt_registry *r, const char *id) {
    if (!id) return -1;
    for (uint32_t i = 0; i < r->n_parties; i++)
        if (strcmp(r->parties[i].id, id) == 0) return (int) i;
    return -1;
}

static item_t *find_item(const ztt_registry *r, const char *tag) {
    if (!r || !tag) return NULL;
    for (uint32_t i = 0; i < r->n_items; i++)
        if (strcmp(r->items[i].tag, tag) == 0) return &r->items[i];
    return NULL;
}

int32_t ztt_party_add(ztt_registry *r, const char *party_id, const uint8_t pk[ZIND_PK_BYTES]) {
    uint8_t id[32];
    int32_t st;
    if (!r || !pk) return ZXV_E_ARG;
    if (!zind_ident_ok(party_id, ZTT_PARTY_MAX)) {
        zind_set_error("ztt_party_add: a party id is 1..16 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < r->n_parties; i++)
        if (strcmp(r->parties[i].id, party_id) == 0 || memcmp(r->parties[i].key_id, id, 32) == 0)
            return ZXV_E_DUPLICATE;
    if (r->n_parties >= ZTT_MAX_PARTIES) return ZXV_E_CAPACITY;
    memcpy(r->parties[r->n_parties].id, party_id, strlen(party_id) + 1);
    memcpy(r->parties[r->n_parties].key_id, id, 32);
    r->n_parties++;
    return ZXV_OK;
}

int32_t ztt_reviewer_add(ztt_registry *r, const uint8_t pk[ZIND_PK_BYTES]) {
    uint8_t id[32];
    int32_t st;
    if (!r || !pk) return ZXV_E_ARG;
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < r->n_reviewers; i++)
        if (memcmp(r->reviewers[i], id, 32) == 0) return ZXV_E_DUPLICATE;
    if (r->n_reviewers >= MAX_REVIEWERS) return ZXV_E_CAPACITY;
    memcpy(r->reviewers[r->n_reviewers++], id, 32);
    return ZXV_OK;
}

static void quarantine(ztt_registry *r, const item_t *it, uint8_t reason) {
    uint8_t subj[32];
    if (!r->qs) return;
    if (zind_subject("tri-trace/item", (const uint8_t *) it->tag, strlen(it->tag), subj) != ZXV_OK)
        return;
    (void) zind_qsink_event(r->qs, it->m.tag, reason, (uint32_t) (it - r->items), subj, NULL);
}

int32_t ztt_scan(ztt_registry *r, const char *item, const char *party,
                 const zind_signer *scanner, int32_t kind, const char *to_party, uint64_t time) {
    uint8_t buf[256], digest[32], hash[32], zero[32] = {0};
    zind_wr w;
    item_t *it;
    mach_t m;
    int p, conflict = 0, created = 0;
    int32_t st, present = 0;
    if (!r || !scanner) return ZXV_E_ARG;
    if (!zind_ident_ok(item, ZTT_TAG_MAX) || kind < ZTT_EV_COMMISSION || kind > ZTT_EV_DISPENSE) {
        zind_set_error("ztt_scan: tag 1..64 of [A-Za-z0-9._:/-] and kind 1..4");
        return ZXV_E_ARG;
    }
    p = find_party(r, party);
    if (p < 0 || memcmp(r->parties[p].key_id, zind_signer_key_id(scanner), 32) != 0) {
        zind_set_error("ztt_scan: the scanner is not the registered key of party %s",
                       party ? party : "(null)");
        return ZTT_E_PARTY;
    }
    if (kind == ZTT_EV_SHIP) {
        if (find_party(r, to_party) < 0) {
            zind_set_error("ztt_scan: SHIP needs a registered destination party");
            return ZTT_E_PARTY;
        }
    } else if (to_party && to_party[0]) {
        return ZXV_E_ARG;
    }
    zind_wr_init(&w, buf, sizeof buf);
    st = scan_payload(&w, kind, item, party, kind == ZTT_EV_SHIP ? to_party : "", time);
    if (st != ZXV_OK) return st;
    st = zind_subject("tri-trace/scan", buf, w.len, digest);
    if (st != ZXV_OK) return st;
    st = zind_seen_has(r->scans, digest, &present);
    if (st != ZXV_OK) return st;
    if (present) {
        zind_set_error("ztt_scan: this exact scan was already recorded");
        return ZIND_E_REPLAY;
    }
    it = find_item(r, item);
    if (!it) {
        if (kind != ZTT_EV_COMMISSION) {
            zind_set_error("ztt_scan: tag %s was never commissioned", item);
            return ZTT_E_ITEM;
        }
        if (r->n_items >= ZTT_MAX_ITEMS) return ZXV_E_CAPACITY;
        if (r->n_items == r->alloc) {
            uint32_t a = r->alloc ? r->alloc * 2 : 16;
            item_t *ni = (item_t *) realloc(r->items, (size_t) a * sizeof *ni);
            if (!ni) return ZXV_E_NOMEM;
            r->items = ni;
            r->alloc = a;
        }
        it = &r->items[r->n_items];
        memset(it, 0, sizeof *it);
        memcpy(it->tag, item, strlen(item) + 1);
        st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &it->chain);
        if (st != ZXV_OK) return st;
        r->n_items++;
        created = 1;
    }
    /* dry run: refusals change nothing and record nothing */
    m = it->m;
    st = mach_scan(&m, kind, party, kind == ZTT_EV_SHIP ? to_party : "", time, zero, &conflict);
    if (st != ZXV_OK) {
        if (st == ZIND_E_HELD) zind_set_error("ztt_scan: %s is frozen for review", item);
        if (st == ZIND_E_FROZEN) zind_set_error("ztt_scan: %s was declared counterfeit", item);
        return st;
    }
    st = zind_chain_append(r->cap, it->chain, scanner, (uint16_t) kind, buf, w.len, NULL, hash);
    if (st != ZXV_OK) {
        if (created) {
            zind_chain_destroy(it->chain);
            r->n_items--;
        }
        return st;
    }
    m = it->m;
    st = mach_scan(&m, kind, party, kind == ZTT_EV_SHIP ? to_party : "", time, hash, &conflict);
    if (st != ZXV_OK) return ZXV_E_INTERNAL;
    it->m = m;
    (void) zind_seen_add(r->scans, digest);
    if (conflict) {
        quarantine(r, it, ZIND_REASON_DUPLICATE);
        zind_set_error("ztt_scan: %s scanned by %s does not fit its custody history; frozen for "
                       "review",
                       item, party);
        return ZTT_E_CONFLICT;
    }
    return ZXV_OK;
}

int32_t ztt_item_state(const ztt_registry *r, const char *item, int32_t *out_state,
                       uint8_t *out_tag, char *holder_buf, size_t holder_cap) {
    const item_t *it = find_item(r, item);
    if (!r) return ZXV_E_ARG;
    if (!it) return ZTT_E_ITEM;
    if (out_state) *out_state = it->m.state;
    if (out_tag) *out_tag = it->m.tag;
    if (holder_buf) {
        size_t n = strlen(it->m.holder);
        if (holder_cap < n + 1) return ZXV_E_BUFFER;
        memcpy(holder_buf, it->m.holder, n + 1);
    }
    return ZXV_OK;
}

static int32_t check_review_args(const ztt_registry *r, const item_t *it, int32_t outcome,
                                 const char *holder) {
    if (outcome != ZTT_GENUINE && outcome != ZTT_COUNTERFEIT) return ZXV_E_ARG;
    if (!holder) return ZXV_E_ARG;
    if (outcome == ZTT_GENUINE && find_party(r, holder) < 0) return ZTT_E_PARTY;
    if (outcome == ZTT_COUNTERFEIT && holder[0]) return ZXV_E_ARG;
    if (it->m.state != ZTT_ST_CONFLICT) return ZXV_E_STATE;
    return ZXV_OK;
}

int32_t ztt_review_message(const ztt_registry *r, const char *item, int32_t outcome,
                           const char *holder, uint8_t *buf, size_t cap, size_t *out_len) {
    const item_t *it = find_item(r, item);
    int32_t st;
    if (!r || !out_len) return ZXV_E_ARG;
    if (!it) return ZTT_E_ITEM;
    st = check_review_args(r, it, outcome, holder);
    if (st != ZXV_OK) return st;
    return review_msg(it->tag, it->m.conflict_hash, outcome, holder, buf, cap, out_len);
}

int32_t ztt_review_resolve(ztt_registry *r, const char *item, const uint8_t reviewer_pk[ZIND_PK_BYTES],
                           int32_t outcome, const char *holder, const uint8_t sig[ZIND_SIG_BYTES]) {
    item_t *it = find_item(r, item);
    uint8_t id[32], msg[192], *rec;
    size_t len = 0;
    uint32_t i;
    zind_wr w;
    mach_t m;
    int32_t st;
    if (!r || !reviewer_pk || !sig) return ZXV_E_ARG;
    if (!it) return ZTT_E_ITEM;
    st = check_review_args(r, it, outcome, holder);
    if (st != ZXV_OK) {
        if (st == ZXV_E_STATE)
            zind_set_error("ztt_review_resolve: %s is not frozen for review", item);
        return st;
    }
    st = zxv_cap_check(r->cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zind_set_error("ztt_review_resolve: the registry cap needs ATTEST to apply a decision");
        return st;
    }
    st = zind_key_id(reviewer_pk, id);
    if (st != ZXV_OK) return st;
    for (i = 0; i < r->n_reviewers; i++)
        if (memcmp(r->reviewers[i], id, 32) == 0) break;
    if (i == r->n_reviewers) {
        zind_set_error("ztt_review_resolve: the key is not a registered reviewer");
        return ZTT_E_REVIEWER;
    }
    st = review_msg(it->tag, it->m.conflict_hash, outcome, holder, msg, sizeof msg, &len);
    if (st == ZXV_OK) st = zind_verify(r->cap, reviewer_pk, msg, len, ZTT_REVIEW_CTX, sig);
    if (st != ZXV_OK) {
        zind_set_error("ztt_review_resolve: the reviewer's signature does not verify");
        if (st == ZXV_E_VERIFY && r->qs) {
            uint8_t ftag = 0, subj[32];
            if (zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &ftag) ==
                    ZXV_OK &&
                zind_subject("tri-trace/forged", sig, ZIND_SIG_BYTES, subj) == ZXV_OK)
                (void) zind_qsink_event(r->qs, ftag, ZIND_REASON_TAMPER,
                                        (uint32_t) (it - r->items), subj, NULL);
        }
        return st;
    }
    m = it->m;
    st = mach_review(&m, outcome, holder);
    if (st != ZXV_OK) return st;
    rec = (uint8_t *) malloc(512 + ZIND_SIG_BYTES);
    if (!rec) return ZXV_E_NOMEM;
    zind_wr_init(&w, rec, 512 + ZIND_SIG_BYTES);
    zind_wr_u8(&w, 1);
    zind_wr_u8(&w, ZTT_EV_REVIEW);
    zind_wr_str(&w, it->tag);
    zind_wr_u8(&w, (uint8_t) outcome);
    zind_wr_str(&w, holder);
    zind_wr_bytes(&w, it->m.conflict_hash, 32);
    zind_wr_bytes(&w, id, 32);
    zind_wr_bytes(&w, sig, ZIND_SIG_BYTES);
    st = w.overflow ? ZXV_E_INTERNAL
                    : zind_chain_append(r->cap, it->chain, r->op, ZTT_EV_REVIEW, rec, w.len, NULL,
                                        NULL);
    free(rec);
    if (st != ZXV_OK) return st;
    it->m = m;
    if (outcome == ZTT_COUNTERFEIT) quarantine(r, it, ZIND_REASON_FORBIDDEN);
    return ZXV_OK;
}

int32_t ztt_item_export(const ztt_registry *r, const char *item, uint8_t *buf, size_t cap,
                        size_t *out_len) {
    const item_t *it = find_item(r, item);
    if (!r || !out_len) return ZXV_E_ARG;
    if (!it) return ZTT_E_ITEM;
    return zind_chain_export(it->chain, buf, cap, out_len);
}

/* ===== offline verification ===== */

int32_t ztt_verify_export(const zxv_cap *cap, const char *item, const uint8_t *buf, size_t len,
                          const ztt_trust *t, ztt_verdict *out) {
    zind_chain *c = NULL;
    const uint8_t **keys = NULL;
    uint8_t (*pid)[32] = NULL, (*rid)[32] = NULL, opid[32];
    uint32_t n = 0, bad = UINT32_MAX;
    mach_t m;
    int32_t st;
    if (!cap || !item || !buf || !t || !out || !t->operator_pk ||
        (t->n_parties && (!t->party_ids || !t->party_pks)) ||
        (t->n_reviewers && !t->reviewer_pks) || t->n_parties > 4096 || t->n_reviewers > 4096)
        return ZXV_E_ARG;
    memset(out, 0, sizeof *out);
    out->bad_index = UINT32_MAX;
    st = zind_chain_import(DOMAIN, buf, len, &c);
    if (st != ZXV_OK) return st;
    keys = (const uint8_t **) malloc((t->n_parties + 1) * sizeof *keys);
    pid = (uint8_t (*)[32]) malloc((t->n_parties + 1) * 32);
    rid = (uint8_t (*)[32]) malloc((t->n_reviewers + 1) * 32);
    if (!keys || !pid || !rid) {
        st = ZXV_E_NOMEM;
        goto done;
    }
    for (size_t i = 0; i < t->n_parties; i++) {
        keys[i] = t->party_pks[i];
        if (!keys[i] || !t->party_ids[i]) {
            st = ZXV_E_ARG;
            goto done;
        }
        st = zind_key_id(keys[i], pid[i]);
        if (st != ZXV_OK) goto done;
    }
    keys[t->n_parties] = t->operator_pk;
    for (size_t i = 0; i < t->n_reviewers; i++) {
        if (!t->reviewer_pks[i]) {
            st = ZXV_E_ARG;
            goto done;
        }
        st = zind_key_id(t->reviewer_pks[i], rid[i]);
        if (st != ZXV_OK) goto done;
    }
    st = zind_key_id(t->operator_pk, opid);
    if (st != ZXV_OK) goto done;
    st = zind_chain_verify(cap, c, keys, t->n_parties + 1, &bad);
    if (st != ZXV_OK) goto done;

    memset(&m, 0, sizeof m);
    zind_chain_count(c, &n);
    for (uint32_t i = 0; i < n; i++) {
        zind_record rec;
        zind_rd rd;
        char tag[256], party[256], to[256];
        uint8_t ver, kind;
        bad = i;
        zind_chain_at(c, i, &rec);
        zind_rd_init(&rd, rec.payload, rec.payload_len);
        ver = zind_rd_u8(&rd);
        kind = zind_rd_u8(&rd);
        zind_rd_str(&rd, tag, sizeof tag);
        if (rd.bad || ver != 1 || kind != rec.kind) {
            st = ZXV_E_FORMAT;
            goto done;
        }
        if (strcmp(tag, item) != 0) {
            zind_set_error("ztt_verify_export: record %u is for tag %s", i, tag);
            st = ZTT_E_ITEM;
            goto done;
        }
        if (kind >= ZTT_EV_COMMISSION && kind <= ZTT_EV_DISPENSE) {
            uint64_t time;
            size_t k;
            int conflict = 0;
            zind_rd_str(&rd, party, sizeof party);
            zind_rd_str(&rd, to, sizeof to);
            time = zind_rd_u64(&rd);
            if (rd.bad || rd.pos != rd.len || strlen(party) > ZTT_PARTY_MAX ||
                strlen(to) > ZTT_PARTY_MAX) {
                st = ZXV_E_FORMAT;
                goto done;
            }
            /* signed by the party it names */
            for (k = 0; k < t->n_parties; k++)
                if (strcmp(t->party_ids[k], party) == 0) break;
            if (k == t->n_parties || memcmp(pid[k], rec.key_id, 32) != 0) {
                zind_set_error("ztt_verify_export: record %u is not signed by %s", i, party);
                st = ZIND_E_UNTRUSTED;
                goto done;
            }
            if (kind == ZTT_EV_SHIP) {
                for (k = 0; k < t->n_parties; k++)
                    if (strcmp(t->party_ids[k], to) == 0) break;
                if (k == t->n_parties) {
                    zind_set_error("ztt_verify_export: record %u ships to an unknown party", i);
                    st = ZIND_E_UNTRUSTED;
                    goto done;
                }
            } else if (to[0]) {
                st = ZXV_E_FORMAT;
                goto done;
            }
            st = mach_scan(&m, kind, party, to, time, rec.hash, &conflict);
            if (st != ZXV_OK) {
                /* the live registry never records a scan on a frozen item */
                zind_set_error("ztt_verify_export: record %u follows a freeze", i);
                st = ZXV_E_FORMAT;
                goto done;
            }
        } else if (kind == ZTT_EV_REVIEW) {
            uint8_t outcome, ch[32], rk[32], msg[192];
            const uint8_t *sig;
            size_t k, mlen = 0;
            outcome = zind_rd_u8(&rd);
            zind_rd_str(&rd, party, sizeof party);
            for (int b = 0; b < 32; b++) ch[b] = zind_rd_u8(&rd);
            for (int b = 0; b < 32; b++) rk[b] = zind_rd_u8(&rd);
            if (rd.bad || rd.len - rd.pos != ZIND_SIG_BYTES || strlen(party) > ZTT_PARTY_MAX) {
                st = ZXV_E_FORMAT;
                goto done;
            }
            sig = rd.buf + rd.pos;
            if (memcmp(rec.key_id, opid, 32) != 0) {
                st = ZIND_E_UNTRUSTED;
                goto done;
            }
            for (k = 0; k < t->n_reviewers; k++)
                if (memcmp(rid[k], rk, 32) == 0) break;
            if (k == t->n_reviewers || m.state != ZTT_ST_CONFLICT ||
                memcmp(ch, m.conflict_hash, 32) != 0 ||
                (outcome != ZTT_GENUINE && outcome != ZTT_COUNTERFEIT)) {
                zind_set_error("ztt_verify_export: review %u is not by a trusted reviewer of the "
                               "recorded conflict",
                               i);
                st = ZIND_E_UNTRUSTED;
                goto done;
            }
            if (outcome == ZTT_GENUINE) {
                size_t q;
                for (q = 0; q < t->n_parties; q++)
                    if (strcmp(t->party_ids[q], party) == 0) break;
                if (q == t->n_parties) {
                    st = ZIND_E_UNTRUSTED;
                    goto done;
                }
            }
            st = review_msg(item, ch, outcome, party, msg, sizeof msg, &mlen);
            if (st == ZXV_OK) st = zind_verify(cap, t->reviewer_pks[k], msg, mlen, ZTT_REVIEW_CTX, sig);
            if (st != ZXV_OK) {
                zind_set_error("ztt_verify_export: review %u has a bad reviewer signature", i);
                st = st == ZXV_E_VERIFY ? ZIND_E_UNTRUSTED : st;
                goto done;
            }
            st = mach_review(&m, outcome, party);
            if (st != ZXV_OK) {
                st = ZXV_E_FORMAT;
                goto done;
            }
        } else {
            st = ZXV_E_FORMAT;
            goto done;
        }
    }
    bad = UINT32_MAX;
    out->state = m.state;
    out->lp = m.state == ZTT_ST_CONFLICT      ? ZXV_LP_BOTH
              : m.state == ZTT_ST_COUNTERFEIT ? ZXV_LP_FALSE
              : m.state == 0                  ? ZXV_LP_NEITHER
                                              : ZXV_LP_TRUE;
    memcpy(out->holder, m.holder, sizeof out->holder);
    out->events = n;
    out->conflicts = m.conflicts;
    st = ZXV_OK;
done:
    out->bad_index = st == ZXV_OK ? UINT32_MAX : bad;
    free(keys);
    free(pid);
    free(rid);
    zind_chain_destroy(c);
    return st;
}
