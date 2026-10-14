/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zsc_house.c — Sutra-Clear's clearing house: participants and currencies,
 * the cycle, conflict holds for an operator, netting, the assurance fee,
 * the all-or-nothing funds check, ledger postings, ISO 20022 output and
 * the signed clearing-cycle record. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zsc_internal.h"

#define DOMAIN      "sutra-clear"
#define OPERATOR_CTX "sutra-clear/operator"
#define DECISION_TAG "sutra-clear/decision/v1"

#define REASON_REJECTED (ZIND_REASON_KIT_BASE + 0) /* an operator rejected this version */
#define REASON_BROKEN   (ZIND_REASON_KIT_BASE + 1) /* a posting failed after the checks */
#define REASON_WITHDRAWN (ZIND_REASON_KIT_BASE + 2) /* taken out of a cycle by the house */

static const char *const k_bucket_name[4] = {"reserve", "dividend", "infra", "regen"};

typedef struct {
    char ccy[4];
    uint8_t minor;
    uint32_t hub;
    uint32_t bucket[4];
} ccy_t;

typedef struct {
    char code[12];
    uint32_t acct[ZSC_MAX_CCY];
    uint64_t carry[ZSC_MAX_CCY];
} part_t;

typedef struct {
    zsc_instr in;
    uint8_t dig[32];
    uint8_t e2e_dig[32];
    uint8_t ccy, alive;
    uint16_t dbtr, cdtr;
} item_t;

typedef struct {
    char e2e[36];
    item_t v[ZSC_MAX_VERSIONS];
    uint32_t n;
} held_t;

typedef struct {
    char e2e[36];
    uint8_t choice;
    uint8_t group[32], chosen[32], op_id[32];
    uint8_t sig[ZIND_SIG_BYTES];
} res_t;

typedef struct {
    uint8_t ccy;
    uint16_t part;
    int64_t net;
} posrow_t;

typedef struct {
    uint8_t ccy, used;
    uint16_t part;
    uint64_t gross, rem_in, fee, rem_out, split[4];
} feerow_t;

typedef struct {
    uint8_t kind, ccy, bucket;
    uint16_t part;
    int64_t amount;
    uint64_t entry;
    uint32_t from, to;
    char ref[36];
} post_t;

typedef struct {
    uint32_t acct;
    uint8_t ccy;
    int64_t open, close;
} balrow_t;

typedef struct {
    uint8_t kind;
    char msg_id[36];
    char *xml;
    size_t len;
    uint8_t dig[32];
} iso_t;

struct zsc_house {
    const zxv_cap *cap;
    char id[9];
    const zind_signer *signer;
    zind_qsink *qs;
    zind_chain *chain;
    zind_seen *settled;
    int broken;
    zxv_ctx *nctx; /* zxv_netting_* context, when the library has it */
    ccy_t ccy[ZSC_MAX_CCY];
    uint32_t n_ccy;
    part_t *part;
    uint32_t n_part;
    uint8_t ops[ZSC_MAX_OPERATORS][32];
    uint32_t n_ops;
    /* the open cycle */
    int open;
    uint32_t cycle_no;
    char cycle_id[17];
    char settle_dt[36];
    item_t *items;
    uint32_t n_items, alloc_items, n_alive;
    uint32_t *map; /* open addressing: e2e digest -> item index + 1 */
    uint32_t map_mask;
    held_t *held;
    uint32_t n_held;
    res_t *res;
    uint32_t n_res;
    item_t *wd; /* withdrawn this cycle */
    uint32_t n_wd;
    /* the last closed cycle */
    posrow_t *pos;
    uint32_t n_pos;
    feerow_t *fee;
    uint32_t n_fee;
    post_t *post;
    uint32_t n_post;
    iso_t *iso;
    uint32_t n_iso;
};

/* ===== small helpers ===== */

static int house_id_ok(const char *s) {
    size_t n;
    if (!s) return 0;
    for (n = 0; s[n]; n++)
        if (n >= 8 || !((s[n] >= 'A' && s[n] <= 'Z') || (s[n] >= '0' && s[n] <= '9') || s[n] == '-'))
            return 0;
    return n >= 1;
}

static int cycle_id_ok(const char *s) {
    size_t n;
    if (!s) return 0;
    for (n = 0; s[n]; n++)
        if (n >= 16 || !((s[n] >= 'A' && s[n] <= 'Z') || (s[n] >= 'a' && s[n] <= 'z') ||
                         (s[n] >= '0' && s[n] <= '9') || s[n] == '-'))
            return 0;
    return n >= 1;
}

static int find_ccy(const zsc_house *h, const char *ccy) {
    if (!ccy) return -1;
    for (uint32_t i = 0; i < h->n_ccy; i++)
        if (strcmp(h->ccy[i].ccy, ccy) == 0) return (int) i;
    return -1;
}

static int find_part(const zsc_house *h, const char *code) {
    if (!code) return -1;
    for (uint32_t i = 0; i < h->n_part; i++)
        if (strcmp(h->part[i].code, code) == 0) return (int) i;
    return -1;
}

static int32_t usable(const zsc_house *h) {
    if (!h) return ZXV_E_ARG;
    if (h->broken) {
        zind_set_error("zsc: the house stopped after a failed posting; it refuses all work");
        return ZSC_E_BROKEN;
    }
    return ZXV_OK;
}

static int32_t e2e_digest(const char *e2e, uint8_t out[32]) {
    return zind_subject("sutra-clear/e2e", (const uint8_t *) e2e, strlen(e2e), out);
}

static void quarantine(zsc_house *h, int32_t lp, uint8_t reason, uint32_t who,
                       const uint8_t subject[32]) {
    uint8_t tag = 0;
    if (!h->qs) return;
    if (lp == ZXV_LP_BOTH) {
        if (zind_tag(ZXV_LP_BOTH, ZXV_HEX_POL_ENTROPIC, ZXV_HEX_PHASE_EMERGING, &tag) != ZXV_OK)
            return;
    } else if (zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &tag) !=
               ZXV_OK) {
        return;
    }
    (void) zind_qsink_event(h->qs, tag, reason, who, subject, NULL);
}

static void free_last(zsc_house *h) {
    for (uint32_t i = 0; i < h->n_iso; i++) free(h->iso[i].xml);
    free(h->pos);
    free(h->fee);
    free(h->post);
    free(h->iso);
    h->pos = NULL;
    h->fee = NULL;
    h->post = NULL;
    h->iso = NULL;
    h->n_pos = h->n_fee = h->n_post = h->n_iso = 0;
}

/* ===== house ===== */

int32_t zsc_house_create(const zxv_cap *cap, const char *house_id, const zind_signer *signer,
                         zxv_hexlog *qlog, zsc_house **out) {
    zsc_house *h;
    int32_t st;
    if (!cap || !signer || !out) return ZXV_E_ARG;
    *out = NULL;
    if (!house_id_ok(house_id)) {
        zind_set_error("zsc_house_create: a house id is 1..8 of [A-Z0-9-]");
        return ZSC_E_FIELD;
    }
    st = zxv_cap_check(cap, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("zsc_house_create: the cap needs LEDGER_POST, LEDGER_READ and "
                       "CRYPTO_SIGN");
        return st;
    }
    h = (zsc_house *) calloc(1, sizeof *h);
    if (!h) return ZXV_E_NOMEM;
    h->cap = cap;
    memcpy(h->id, house_id, strlen(house_id) + 1);
    h->signer = signer;
    h->part = (part_t *) calloc(ZSC_MAX_PARTICIPANTS, sizeof *h->part);
    h->held = (held_t *) calloc(ZSC_MAX_HELD, sizeof *h->held);
    h->map_mask = 2u * ZSC_MAX_CYCLE_INSTR - 1u;
    h->map = (uint32_t *) calloc((size_t) h->map_mask + 1u, sizeof *h->map);
    if (!h->part || !h->held || !h->map) {
        zsc_house_destroy(h);
        return ZXV_E_NOMEM;
    }
    st = zind_chain_create(DOMAIN, ZIND_CHAIN_MAX, &h->chain);
    if (st == ZXV_OK) st = zind_seen_create(ZSC_MAX_SETTLED, &h->settled);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &h->qs);
    if (st != ZXV_OK) {
        zsc_house_destroy(h);
        return st;
    }
    *out = h;
    return ZXV_OK;
}

void zsc_house_destroy(zsc_house *h) {
    if (!h) return;
    free_last(h);
    zind_chain_destroy(h->chain);
    zind_seen_destroy(h->settled);
    zind_qsink_destroy(h->qs);
    free(h->part);
    free(h->held);
    free(h->map);
    free(h->items);
    free(h->res);
    free(h->wd);
    free(h);
}

const zind_chain *zsc_records(const zsc_house *h) { return h ? h->chain : NULL; }

static int32_t open_acct(zsc_house *h, const char *who, const char *ccy, uint32_t *out) {
    char name[40];
    snprintf(name, sizeof name, "%s/%s/%s", h->id, who, ccy);
    return zxv_ledger_open_account_cap(h->cap, name, ccy, ZXV_ACCT_HOLDER, out);
}

int32_t zsc_currency_add(zsc_house *h, const char *ccy) {
    ccy_t c;
    int minor;
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    minor = zsc_ccy_minor(ccy);
    if (minor < 0) {
        zind_set_error("zsc_currency_add: \"%.8s\" is not a payable ISO 4217 currency with minor "
                       "units",
                       ccy ? ccy : "(null)");
        return ZXV_E_CURRENCY;
    }
    if (find_ccy(h, ccy) >= 0) return ZXV_E_DUPLICATE;
    if (h->n_ccy >= ZSC_MAX_CCY) return ZXV_E_CAPACITY;
    if (h->open) {
        zind_set_error("zsc_currency_add: not while a cycle is open");
        return ZSC_E_CYCLE;
    }
    st = zxv_ccy_enable_cap(h->cap, ccy);
    if (st != ZXV_OK && st != ZXV_E_DUPLICATE) return st;
    memset(&c, 0, sizeof c);
    memcpy(c.ccy, ccy, 4);
    c.minor = (uint8_t) minor;
    st = open_acct(h, "hub", ccy, &c.hub);
    for (int b = 0; b < 4 && st == ZXV_OK; b++) st = open_acct(h, k_bucket_name[b], ccy, &c.bucket[b]);
    for (uint32_t p = 0; p < h->n_part && st == ZXV_OK; p++)
        st = open_acct(h, h->part[p].code, ccy, &h->part[p].acct[h->n_ccy]);
    if (st != ZXV_OK) return st;
    h->ccy[h->n_ccy++] = c;
    return ZXV_OK;
}

int32_t zsc_participant_add(zsc_house *h, const char *code) {
    part_t *p;
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    if (!zsc_code_ok(code)) {
        zind_set_error("zsc_participant_add: a code is 1..11 of [A-Z0-9]");
        return ZSC_E_FIELD;
    }
    if (find_part(h, code) >= 0) return ZXV_E_DUPLICATE;
    if (h->n_part >= ZSC_MAX_PARTICIPANTS) return ZXV_E_CAPACITY;
    if (h->open) {
        zind_set_error("zsc_participant_add: not while a cycle is open");
        return ZSC_E_CYCLE;
    }
    p = &h->part[h->n_part];
    memset(p, 0, sizeof *p);
    memcpy(p->code, code, strlen(code) + 1);
    for (uint32_t c = 0; c < h->n_ccy; c++) {
        st = open_acct(h, code, h->ccy[c].ccy, &p->acct[c]);
        if (st != ZXV_OK) return st;
    }
    h->n_part++;
    return ZXV_OK;
}

int32_t zsc_operator_add(zsc_house *h, const uint8_t pk[ZIND_PK_BYTES]) {
    uint8_t id[32];
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    if (!pk) return ZXV_E_ARG;
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < h->n_ops; i++)
        if (memcmp(h->ops[i], id, 32) == 0) return ZXV_E_DUPLICATE;
    if (h->n_ops >= ZSC_MAX_OPERATORS) return ZXV_E_CAPACITY;
    memcpy(h->ops[h->n_ops++], id, 32);
    return ZXV_OK;
}

int32_t zsc_account(const zsc_house *h, int32_t which, const char *code, const char *ccy,
                    uint32_t *out_account) {
    int c, p;
    if (!h || !out_account) return ZXV_E_ARG;
    c = find_ccy(h, ccy);
    if (c < 0) return ZXV_E_CURRENCY;
    switch (which) {
    case ZSC_ACCT_PARTICIPANT:
        p = find_part(h, code);
        if (p < 0) return ZSC_E_PARTICIPANT;
        *out_account = h->part[p].acct[c];
        return ZXV_OK;
    case ZSC_ACCT_HUB: *out_account = h->ccy[c].hub; return ZXV_OK;
    case ZSC_ACCT_RESERVE:
    case ZSC_ACCT_DIVIDEND:
    case ZSC_ACCT_INFRA:
    case ZSC_ACCT_REGEN: *out_account = h->ccy[c].bucket[which - ZSC_ACCT_RESERVE]; return ZXV_OK;
    default: return ZXV_E_ARG;
    }
}

int32_t zsc_carry(const zsc_house *h, const char *code, const char *ccy, uint64_t *out_rem) {
    int c, p;
    if (!h || !out_rem) return ZXV_E_ARG;
    c = find_ccy(h, ccy);
    p = find_part(h, code);
    if (c < 0) return ZXV_E_CURRENCY;
    if (p < 0) return ZSC_E_PARTICIPANT;
    *out_rem = h->part[p].carry[c];
    return ZXV_OK;
}

/* ===== the open cycle ===== */

int32_t zsc_cycle_open(zsc_house *h, const char *cycle_id, const char *settle_dt) {
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    if (h->open) {
        zind_set_error("zsc_cycle_open: cycle %u is still open", h->cycle_no);
        return ZSC_E_CYCLE;
    }
    if (!cycle_id_ok(cycle_id) || !zsc_dt_ok(settle_dt)) {
        zind_set_error("zsc_cycle_open: cycle id 1..16 of [A-Za-z0-9-], settle_dt ISO 8601");
        return ZSC_E_FIELD;
    }
    if (h->cycle_no == UINT32_MAX) return ZXV_E_CAPACITY;
    h->open = 1;
    h->cycle_no++;
    memcpy(h->cycle_id, cycle_id, strlen(cycle_id) + 1);
    memcpy(h->settle_dt, settle_dt, strlen(settle_dt) + 1);
    h->n_items = h->n_alive = 0;
    memset(h->map, 0, ((size_t) h->map_mask + 1u) * sizeof *h->map);
    h->n_res = 0;
    h->n_wd = 0;
    return ZXV_OK;
}

int32_t zsc_use_netting(zsc_house *h, zxv_ctx *ctx) {
    uint32_t feat = 0;
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    if (!ctx) return ZXV_E_ARG;
    (void) zxv_features(&feat);
    if (!(feat & ZXV_FEAT_NETTING)) {
        zind_set_error("zsc_use_netting: this libzxv has no netting cycles (ZXV_FEAT_NETTING "
                       "clear); the house keeps the treaty engine");
        return ZXV_E_NOTIMPL;
    }
    h->nctx = ctx;
    return ZXV_OK;
}

int32_t zsc_cycle_is_open(const zsc_house *h, int32_t *out_open, uint32_t *out_cycle_no) {
    if (!h) return ZXV_E_ARG;
    if (out_open) *out_open = h->open;
    if (out_cycle_no) *out_cycle_no = h->cycle_no;
    return ZXV_OK;
}

int32_t zsc_pending_count(const zsc_house *h, uint32_t *out_count) {
    if (!h || !out_count) return ZXV_E_ARG;
    *out_count = h->n_alive;
    return ZXV_OK;
}

int32_t zsc_pending_at(const zsc_house *h, uint32_t i, zsc_instr *out) {
    if (!h || !out) return ZXV_E_ARG;
    for (uint32_t k = 0; k < h->n_items; k++)
        if (h->items[k].alive && i-- == 0) {
            *out = h->items[k].in;
            return ZXV_OK;
        }
    return ZXV_E_NOT_FOUND;
}

static uint32_t *map_slot(zsc_house *h, const item_t *probe, int *found) {
    uint32_t i = ((uint32_t) probe->e2e_dig[0] | ((uint32_t) probe->e2e_dig[1] << 8) |
                  ((uint32_t) probe->e2e_dig[2] << 16) | ((uint32_t) probe->e2e_dig[3] << 24)) &
                 h->map_mask;
    for (;;) {
        uint32_t v = h->map[i];
        if (v == 0) {
            *found = 0;
            return &h->map[i];
        }
        if (memcmp(h->items[v - 1].e2e_dig, probe->e2e_dig, 32) == 0) {
            *found = 1;
            return &h->map[i];
        }
        i = (i + 1) & h->map_mask;
    }
}

static held_t *find_held(const zsc_house *h, const char *e2e, uint32_t *out_idx) {
    for (uint32_t i = 0; i < h->n_held; i++)
        if (strcmp(h->held[i].e2e, e2e) == 0) {
            if (out_idx) *out_idx = i;
            return &h->held[i];
        }
    return NULL;
}

static int32_t push_item(zsc_house *h, const item_t *it) {
    if (h->n_items >= ZSC_MAX_CYCLE_INSTR) {
        zind_set_error("zsc: a cycle takes at most %u instructions", ZSC_MAX_CYCLE_INSTR);
        return ZXV_E_CAPACITY;
    }
    if (h->n_items == h->alloc_items) {
        uint32_t a = h->alloc_items ? h->alloc_items * 2 : 64;
        item_t *n = (item_t *) realloc(h->items, (size_t) a * sizeof *n);
        if (!n) return ZXV_E_NOMEM;
        h->items = n;
        h->alloc_items = a;
    }
    h->items[h->n_items++] = *it;
    h->n_alive++;
    return ZXV_OK;
}

static int32_t make_item(const zsc_house *h, const zsc_instr *in, item_t *it) {
    int c, d, k;
    int32_t st = zsc_instr_check(in);
    if (st != ZXV_OK) return st;
    c = find_ccy(h, in->ccy);
    if (c < 0) {
        zind_set_error("zsc_submit: %s is not a currency of this house", in->ccy);
        return ZXV_E_CURRENCY;
    }
    d = find_part(h, in->dbtr_agt);
    k = find_part(h, in->cdtr_agt);
    if (d < 0 || k < 0) {
        zind_set_error("zsc_submit: %s is not a participant of this house",
                       d < 0 ? in->dbtr_agt : in->cdtr_agt);
        return ZSC_E_PARTICIPANT;
    }
    memset(it, 0, sizeof *it);
    it->in = *in;
    it->ccy = (uint8_t) c;
    it->dbtr = (uint16_t) d;
    it->cdtr = (uint16_t) k;
    it->alive = 1;
    st = zsc_instr_digest(in, it->dig);
    if (st == ZXV_OK) st = e2e_digest(in->e2e, it->e2e_dig);
    return st;
}

int32_t zsc_submit(zsc_house *h, const zsc_instr *in) {
    item_t it;
    held_t *g;
    uint32_t *slot;
    int found = 0;
    int32_t present = 0, st = usable(h);
    if (st != ZXV_OK) return st;
    if (!in) return ZXV_E_ARG;
    if (!h->open) {
        zind_set_error("zsc_submit: no cycle is open");
        return ZSC_E_CYCLE;
    }
    st = make_item(h, in, &it);
    if (st != ZXV_OK) return st;
    st = zind_seen_has(h->settled, it.e2e_dig, &present);
    if (st != ZXV_OK) return st;
    if (present) {
        zind_set_error("zsc_submit: end-to-end id %s was already settled or decided", in->e2e);
        quarantine(h, ZXV_LP_FALSE, ZIND_REASON_REPLAY, it.dbtr, it.dig);
        return ZIND_E_REPLAY;
    }
    g = find_held(h, in->e2e, NULL);
    if (g) {
        for (uint32_t k = 0; k < g->n; k++)
            if (memcmp(g->v[k].dig, it.dig, 32) == 0) {
                zind_set_error("zsc_submit: %s is already held in this form", in->e2e);
                return ZIND_E_REPLAY;
            }
        if (g->n >= ZSC_MAX_VERSIONS) {
            zind_set_error("zsc_submit: %s already has %u conflicting versions held", in->e2e,
                           ZSC_MAX_VERSIONS);
            return ZXV_E_CAPACITY;
        }
        g->v[g->n++] = it;
        quarantine(h, ZXV_LP_BOTH, ZIND_REASON_HELD_BOTH, it.dbtr, it.dig);
        zind_set_error("zsc_submit: %s conflicts with %u held version(s); held for an operator",
                       in->e2e, g->n - 1);
        return ZIND_E_HELD;
    }
    slot = map_slot(h, &it, &found);
    if (found) {
        item_t *old = &h->items[*slot - 1];
        if (!old->alive) return ZXV_E_INTERNAL;
        if (memcmp(old->dig, it.dig, 32) == 0) {
            zind_set_error("zsc_submit: %s was already submitted in this cycle", in->e2e);
            return ZIND_E_REPLAY;
        }
        if (h->n_held >= ZSC_MAX_HELD) return ZXV_E_CAPACITY;
        g = &h->held[h->n_held++];
        memset(g, 0, sizeof *g);
        memcpy(g->e2e, in->e2e, strlen(in->e2e) + 1);
        g->v[0] = *old;
        g->v[1] = it;
        g->n = 2;
        old->alive = 0;
        h->n_alive--;
        quarantine(h, ZXV_LP_BOTH, ZIND_REASON_HELD_BOTH, old->dbtr, old->dig);
        quarantine(h, ZXV_LP_BOTH, ZIND_REASON_HELD_BOTH, it.dbtr, it.dig);
        zind_set_error("zsc_submit: %s was submitted with different content; both versions "
                       "held as BOTH for an operator",
                       in->e2e);
        return ZIND_E_HELD;
    }
    st = push_item(h, &it);
    if (st != ZXV_OK) return st;
    *slot = h->n_items;
    return ZXV_OK;
}

int32_t zsc_submit_pacs008(zsc_house *h, const char *xml, size_t len, size_t *out_err_off) {
    zsc_instr in;
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    st = zsc_parse_pacs008(xml, len, &in, out_err_off);
    if (st != ZXV_OK) return st;
    return zsc_submit(h, &in);
}

int32_t zsc_withdraw(zsc_house *h, const char *e2e) {
    item_t probe, *it;
    uint32_t *slot;
    int found = 0;
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    if (!e2e) return ZXV_E_ARG;
    if (!h->open) return ZSC_E_CYCLE;
    memset(&probe, 0, sizeof probe);
    st = e2e_digest(e2e, probe.e2e_dig);
    if (st != ZXV_OK) return st;
    slot = map_slot(h, &probe, &found);
    if (!found || !h->items[*slot - 1].alive) {
        zind_set_error("zsc_withdraw: nothing is pending under %.35s", e2e);
        return ZXV_E_NOT_FOUND;
    }
    it = &h->items[*slot - 1];
    {
        item_t *nw = (item_t *) realloc(h->wd, ((size_t) h->n_wd + 1) * sizeof *nw);
        if (!nw) return ZXV_E_NOMEM;
        h->wd = nw;
    }
    st = zind_seen_add(h->settled, it->e2e_dig);
    if (st != ZXV_OK && st != ZIND_E_REPLAY) return st; /* REPLAY: an operator's choice */
    h->wd[h->n_wd++] = *it;
    it->alive = 0;
    h->n_alive--;
    quarantine(h, ZXV_LP_FALSE, REASON_WITHDRAWN, it->dbtr, it->dig);
    return ZXV_OK;
}

/* ===== treaties ===== */

static int32_t submit_treaty(zsc_house *h, zxv_treaty *t, uint8_t source, zsc_batch *out) {
    char name[32], asset[8], ids[ZXV_TREATY_MAX_PARTIES][12];
    size_t l = 0;
    int32_t op = 0, st;
    uint32_t np = 0, no = 0;
    zsc_batch b = {0, 0, 0};
    if ((st = zxv_treaty_name(t, name, sizeof name, &l)) != ZXV_OK ||
        (st = zxv_treaty_asset(t, asset, sizeof asset, &l)) != ZXV_OK ||
        (st = zxv_treaty_header(t, &op, &np, &no)) != ZXV_OK)
        return st;
    if (op != ZXV_TREATY_OP_NET) {
        zind_set_error("zsc: treaty %s is not a NET treaty", name);
        return ZSC_E_TREATY;
    }
    if (find_ccy(h, asset) < 0) {
        zind_set_error("zsc: treaty %s is in %s, not a currency of this house", name, asset);
        return ZXV_E_CURRENCY;
    }
    if (strlen(name) + 1 + 2 > 35) return ZXV_E_INTERNAL; /* name <= 30 */
    for (uint32_t i = 0; i < np; i++) {
        int64_t dep = 0, claim = 0;
        uint32_t wn = 0, wd = 0;
        if ((st = zxv_treaty_party_id(t, i, ids[i], sizeof ids[i], &l)) != ZXV_OK ||
            (st = zxv_treaty_party(t, i, &dep, &claim, &wn, &wd)) != ZXV_OK)
            return st;
        if (find_part(h, ids[i]) < 0) {
            zind_set_error("zsc: treaty party %s is not a participant of this house", ids[i]);
            return ZSC_E_PARTICIPANT;
        }
        if (dep != 0 || claim != 0) {
            zind_set_error("zsc: treaty party %s has a deposit or claim; the house settles from "
                           "prefunded accounts, so both must be 0",
                           ids[i]);
            return ZSC_E_TREATY;
        }
    }
    /* check every obligation before submitting any */
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t k = 0; k < no; k++) {
            zsc_instr in;
            uint32_t f = 0, to = 0;
            int64_t amt = 0;
            item_t it;
            st = zxv_treaty_owe(t, k, &f, &to, &amt);
            if (st != ZXV_OK) return st;
            memset(&in, 0, sizeof in);
            in.source = source;
            memcpy(in.msg_id, name, strlen(name) + 1);
            memcpy(in.cre_dt_tm, h->settle_dt, strlen(h->settle_dt) + 1);
            {
                char tmp[64];
                int n = snprintf(tmp, sizeof tmp, "%s-%u", name, k + 1);
                if (n < 0 || (size_t) n >= sizeof in.e2e) return ZXV_E_INTERNAL; /* <= 33 */
                memcpy(in.e2e, tmp, (size_t) n + 1);
            }
            memcpy(in.ccy, asset, 4);
            in.amount = amt;
            memcpy(in.dbtr_agt, ids[f], sizeof in.dbtr_agt);
            memcpy(in.cdtr_agt, ids[to], sizeof in.cdtr_agt);
            memcpy(in.dbtr_nm, ids[f], sizeof in.dbtr_agt);
            memcpy(in.dbtr_acct, ids[f], sizeof in.dbtr_agt);
            memcpy(in.cdtr_nm, ids[to], sizeof in.cdtr_agt);
            memcpy(in.cdtr_acct, ids[to], sizeof in.cdtr_agt);
            if (pass == 0) {
                st = make_item(h, &in, &it);
                if (st != ZXV_OK) return st;
                continue;
            }
            st = zsc_submit(h, &in);
            if (st == ZXV_OK) b.accepted++;
            else if (st == ZIND_E_HELD) b.held++;
            else if (st == ZIND_E_REPLAY) b.replayed++;
            else {
                if (out) *out = b;
                return st;
            }
        }
    }
    if (out) *out = b;
    return ZXV_OK;
}

static int32_t treaty_status(int32_t st, uint32_t off, int32_t fault, const char *what) {
    if (st == ZXV_E_TREATY) {
        zind_set_error("zsc: the %s is not a valid treaty (fault %d at byte %u)", what,
                       (int) fault, off);
        return ZSC_E_TREATY;
    }
    return st;
}

int32_t zsc_submit_sutra(zsc_house *h, const char *text, zsc_batch *out) {
    zxv_treaty *t = NULL;
    uint32_t off = 0;
    int32_t fault = 0, st = usable(h);
    if (st != ZXV_OK) return st;
    if (!text) return ZXV_E_ARG;
    if (!h->open) return ZSC_E_CYCLE;
    st = zxv_treaty_create(&t);
    if (st != ZXV_OK) return st;
    st = treaty_status(zxv_treaty_parse_sutra(t, text, &off, &fault), off, fault, "Sutra text");
    if (st == ZXV_OK) st = submit_treaty(h, t, ZSC_SRC_SUTRA, out);
    zxv_treaty_destroy(t);
    return st;
}

int32_t zsc_submit_record(zsc_house *h, const uint8_t *rec, size_t len, zsc_batch *out) {
    zxv_treaty *t = NULL;
    uint32_t off = 0;
    int32_t fault = 0, st = usable(h);
    if (st != ZXV_OK) return st;
    if (!rec) return ZXV_E_ARG;
    if (!h->open) return ZSC_E_CYCLE;
    if (len != ZXV_TREATY_REC_LEN) {
        zind_set_error("zsc_submit_record: a treaty record is exactly %u bytes", ZXV_TREATY_REC_LEN);
        return ZSC_E_TREATY;
    }
    st = zxv_treaty_create(&t);
    if (st != ZXV_OK) return st;
    st = treaty_status(zxv_treaty_parse_record(t, rec, len, &off, &fault), off, fault,
                       "copybook record");
    if (st == ZXV_OK) st = submit_treaty(h, t, ZSC_SRC_RECORD, out);
    zxv_treaty_destroy(t);
    return st;
}

/* ===== held instructions ===== */

int32_t zsc_held_count(const zsc_house *h, uint32_t *out_count) {
    if (!h || !out_count) return ZXV_E_ARG;
    *out_count = h->n_held;
    return ZXV_OK;
}

int32_t zsc_held_at(const zsc_house *h, uint32_t i, char *e2e, size_t cap, uint32_t *out_versions) {
    if (!h || !e2e) return ZXV_E_ARG;
    if (i >= h->n_held) return ZXV_E_NOT_FOUND;
    if (cap <= strlen(h->held[i].e2e)) return ZXV_E_BUFFER;
    memcpy(e2e, h->held[i].e2e, strlen(h->held[i].e2e) + 1);
    if (out_versions) *out_versions = h->held[i].n;
    return ZXV_OK;
}

int32_t zsc_held_version(const zsc_house *h, const char *e2e, uint32_t k, zsc_instr *out,
                         uint8_t out_digest[32]) {
    const held_t *g;
    if (!h || !e2e) return ZXV_E_ARG;
    g = find_held(h, e2e, NULL);
    if (!g || k >= g->n) return ZXV_E_NOT_FOUND;
    if (out) *out = g->v[k].in;
    if (out_digest) memcpy(out_digest, g->v[k].dig, 32);
    return ZXV_OK;
}

static int32_t group_digest(const held_t *g, uint8_t out[32]) {
    uint8_t buf[32 * ZSC_MAX_VERSIONS];
    for (uint32_t k = 0; k < g->n; k++) memcpy(buf + 32 * k, g->v[k].dig, 32);
    return zind_subject("sutra-clear/held", buf, 32 * (size_t) g->n, out);
}

static int32_t decision_bytes(const uint8_t key_id[32], const char *e2e, uint32_t choice,
                              const uint8_t group[32], const uint8_t chosen[32], uint8_t *buf,
                              size_t cap, size_t *out_len) {
    uint8_t tmp[256];
    zind_wr w;
    zind_wr_init(&w, tmp, sizeof tmp);
    zind_wr_bytes(&w, DECISION_TAG, strlen(DECISION_TAG));
    zind_wr_bytes(&w, key_id, 32);
    zind_wr_str(&w, e2e);
    zind_wr_u8(&w, (uint8_t) choice);
    zind_wr_bytes(&w, group, 32);
    zind_wr_bytes(&w, chosen, 32);
    if (w.overflow) return ZXV_E_INTERNAL;
    *out_len = w.len;
    if (!buf || cap < w.len) return ZXV_E_BUFFER;
    memcpy(buf, tmp, w.len);
    return ZXV_OK;
}

static int32_t decision_parts(const zsc_house *h, const char *e2e, uint32_t choice,
                              const held_t **out_g, uint8_t group[32], uint8_t chosen[32]) {
    const held_t *g;
    int32_t st;
    if (!h || !e2e) return ZXV_E_ARG;
    g = find_held(h, e2e, NULL);
    if (!g) {
        zind_set_error("zsc: nothing is held under end-to-end id %.35s", e2e);
        return ZXV_E_NOT_FOUND;
    }
    if (choice > g->n) {
        zind_set_error("zsc: choice %u is not 0..%u", choice, g->n);
        return ZXV_E_ARG;
    }
    st = group_digest(g, group);
    if (st != ZXV_OK) return st;
    memset(chosen, 0, 32);
    if (choice) memcpy(chosen, g->v[choice - 1].dig, 32);
    *out_g = g;
    return ZXV_OK;
}

int32_t zsc_decision_message(const zsc_house *h, const char *e2e, uint32_t choice, uint8_t *buf,
                             size_t cap, size_t *out_len) {
    const held_t *g = NULL;
    uint8_t group[32], chosen[32];
    int32_t st;
    if (!out_len) return ZXV_E_ARG;
    st = decision_parts(h, e2e, choice, &g, group, chosen);
    if (st != ZXV_OK) return st;
    return decision_bytes(zind_signer_key_id(h->signer), e2e, choice, group, chosen, buf, cap,
                          out_len);
}

int32_t zsc_resolve(zsc_house *h, const uint8_t op_pk[ZIND_PK_BYTES], const char *e2e,
                    uint32_t choice, const uint8_t sig[ZIND_SIG_BYTES]) {
    const held_t *g = NULL;
    uint8_t group[32], chosen[32], op_id[32], msg[256], subj[32];
    size_t len = 0;
    uint32_t gi = 0, k;
    int known = 0;
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    if (!op_pk || !sig || !e2e) return ZXV_E_ARG;
    if (!h->open) {
        zind_set_error("zsc_resolve: a decision is applied in an open cycle");
        return ZSC_E_CYCLE;
    }
    st = zind_key_id(op_pk, op_id);
    if (st != ZXV_OK) return st;
    for (k = 0; k < h->n_ops; k++)
        if (memcmp(h->ops[k], op_id, 32) == 0) known = 1;
    if (!known) {
        zind_set_error("zsc_resolve: the key is not a registered operator");
        return ZSC_E_OPERATOR;
    }
    st = decision_parts(h, e2e, choice, &g, group, chosen);
    if (st != ZXV_OK) return st;
    st = decision_bytes(zind_signer_key_id(h->signer), e2e, choice, group, chosen, msg, sizeof msg,
                        &len);
    if (st != ZXV_OK) return st;
    st = zind_verify(h->cap, op_pk, msg, len, OPERATOR_CTX, sig);
    if (st != ZXV_OK) {
        zind_set_error("zsc_resolve: the operator's signature does not verify");
        if (zind_subject("sutra-clear/forged", msg, len, subj) == ZXV_OK)
            quarantine(h, ZXV_LP_FALSE, ZIND_REASON_TAMPER, UINT32_MAX, subj);
        return st == ZXV_E_VERIFY ? ZXV_E_VERIFY : st;
    }
    (void) find_held(h, e2e, &gi);
    {
        uint8_t e2d[32];
        res_t *r;
        st = e2e_digest(e2e, e2d);
        if (st != ZXV_OK) return st;
        if (choice && h->n_items >= ZSC_MAX_CYCLE_INSTR) return ZXV_E_CAPACITY;
        {
            res_t *nr = (res_t *) realloc(h->res, ((size_t) h->n_res + 1) * sizeof *nr);
            if (!nr) return ZXV_E_NOMEM;
            h->res = nr;
        }
        st = zind_seen_add(h->settled, e2d);
        if (st != ZXV_OK) return st == ZIND_E_REPLAY ? ZXV_E_INTERNAL : st;
        if (choice) {
            item_t it = g->v[choice - 1];
            int found = 0;
            uint32_t *slot;
            it.alive = 1;
            slot = map_slot(h, &it, &found);
            if (found && h->items[*slot - 1].alive) return ZXV_E_INTERNAL;
            st = push_item(h, &it);
            if (st != ZXV_OK) return st;
            *slot = h->n_items;
        }
        for (k = 0; k < g->n; k++)
            if (k + 1 != choice) quarantine(h, ZXV_LP_FALSE, REASON_REJECTED, g->v[k].dbtr, g->v[k].dig);
        r = &h->res[h->n_res++];
        memset(r, 0, sizeof *r);
        memcpy(r->e2e, e2e, strlen(e2e) + 1);
        r->choice = (uint8_t) choice;
        memcpy(r->group, group, 32);
        memcpy(r->chosen, chosen, 32);
        memcpy(r->op_id, op_id, 32);
        memcpy(r->sig, sig, ZIND_SIG_BYTES);
    }
    /* drop the group */
    h->held[gi] = h->held[h->n_held - 1];
    h->n_held--;
    return ZXV_OK;
}

/* ===== close ===== */

static int cmp_item_e2e(const void *a, const void *b) {
    const item_t *x = *(const item_t *const *) a, *y = *(const item_t *const *) b;
    return strcmp(x->in.e2e, y->in.e2e);
}

static int add64(int64_t a, int64_t b, int64_t *o) { return !__builtin_add_overflow(a, b, o); }
static int addu64(uint64_t a, uint64_t b, uint64_t *o) { return !__builtin_add_overflow(a, b, o); }

static int32_t equity(const zsc_house *h, uint32_t acct, int64_t *out) {
    return zxv_ledger_balance_cap(h->cap, acct, NULL, NULL, out);
}

static void acct_name(const zsc_house *h, uint32_t acct, char *buf, size_t cap) {
    size_t l = 0;
    if (zxv_ledger_account_name_cap(h->cap, acct, buf, cap, &l) != ZXV_OK) buf[0] = 0;
}

/* Everything computed for a close before anything is posted. */
typedef struct {
    item_t **sorted;
    uint32_t n;
    int64_t *pos;      /* [ccy * n_part + part] */
    uint8_t *active;   /* same index */
    feerow_t *fr;      /* same index */
    post_t *post;
    uint32_t n_post;
    balrow_t *bal;
    uint32_t n_bal;
    iso_t *iso;
    uint32_t n_iso;
    uint8_t backend;
} plan_t;

static void plan_free(plan_t *p) {
    if (p->iso)
        for (uint32_t i = 0; i < p->n_iso; i++) free(p->iso[i].xml);
    free(p->sorted);
    free(p->pos);
    free(p->active);
    free(p->fr);
    free(p->post);
    free(p->bal);
    free(p->iso);
    memset(p, 0, sizeof *p);
}

static int32_t net_all(zsc_house *h, plan_t *pl, zxv_ctx *nctx) {
    uint32_t np = h->n_part;
    zsc_oblig *obs = (zsc_oblig *) malloc(((size_t) pl->n + 1) * sizeof *obs);
    uint32_t feat = 0;
    int32_t st = ZXV_OK;
    if (!obs) return ZXV_E_NOMEM;
    (void) zxv_features(&feat);
    pl->backend = (nctx && (feat & ZXV_FEAT_NETTING)) ? ZSC_NET_ZXV_NETTING : ZSC_NET_TREATY;
    for (uint32_t c = 0; c < h->n_ccy && st == ZXV_OK; c++) {
        size_t n = 0;
        for (uint32_t i = 0; i < pl->n; i++) {
            const item_t *it = pl->sorted[i];
            if (it->ccy != c) continue;
            obs[n].from = it->dbtr;
            obs[n].to = it->cdtr;
            obs[n].amount = it->in.amount;
            pl->active[c * np + it->dbtr] = 1;
            pl->active[c * np + it->cdtr] = 1;
            n++;
        }
        if (n == 0) continue;
        if (pl->backend == ZSC_NET_ZXV_NETTING) {
            const char(*codes)[12] = (const char(*)[12]) malloc((size_t) np * 12);
            if (!codes) {
                st = ZXV_E_NOMEM;
                break;
            }
            for (uint32_t p = 0; p < np; p++)
                memcpy((char *) codes[p], h->part[p].code, 12);
            st = zsc_net_zxv(nctx, h->cycle_id, h->ccy[c].ccy, np, codes, obs, n, pl->pos + c * np);
            free((void *) codes);
        } else {
            st = zsc_net_treaty(h->ccy[c].ccy, np, obs, n, pl->pos + c * np);
        }
    }
    free(obs);
    return st;
}

static int32_t fees_all(zsc_house *h, plan_t *pl) {
    uint32_t np = h->n_part;
    int32_t st = ZXV_OK;
    for (uint32_t i = 0; i < pl->n && st == ZXV_OK; i++) {
        const item_t *it = pl->sorted[i];
        feerow_t *r = &pl->fr[it->ccy * np + it->dbtr];
        uint64_t fee = 0, rem = 0;
        if (!r->used) {
            r->used = 1;
            r->ccy = it->ccy;
            r->part = it->dbtr;
            r->rem_in = h->part[it->dbtr].carry[it->ccy];
            r->rem_out = r->rem_in;
        }
        st = zxv_fee_assure_carry(r->rem_out, (uint64_t) it->in.amount, &fee, &rem);
        if (st != ZXV_OK) break;
        if (!addu64(r->fee, fee, &r->fee) || !addu64(r->gross, (uint64_t) it->in.amount, &r->gross)) {
            zind_set_error("zsc: a fee total overflows");
            st = ZXV_E_RANGE;
            break;
        }
        r->rem_out = rem;
    }
    for (uint32_t k = 0; k < h->n_ccy * np && st == ZXV_OK; k++)
        if (pl->fr[k].used) st = zxv_fee_split(pl->fr[k].fee, pl->fr[k].split);
    return st;
}

static int32_t plan_post(plan_t *pl, uint8_t kind, uint8_t c, uint16_t p, uint8_t b, int64_t amt,
                         uint32_t from, uint32_t to, const zsc_house *h) {
    post_t *x = &pl->post[pl->n_post++];
    static const char *const tag[4] = {"F0", "F1", "F2", "F3"};
    memset(x, 0, sizeof *x);
    x->kind = kind;
    x->ccy = c;
    x->part = p;
    x->bucket = b;
    x->amount = amt;
    x->from = from;
    x->to = to;
    {
        char tmp[96];
        int n = snprintf(tmp, sizeof tmp, "%s/%u/%s/%u/%s", h->id, h->cycle_no, h->ccy[c].ccy,
                         (unsigned) p,
                         kind == ZSC_POST_PAYIN ? "S" : kind == ZSC_POST_PAYOUT ? "R" : tag[b]);
        if (n < 0 || (size_t) n >= sizeof x->ref) return ZXV_E_INTERNAL; /* <= 30 by design */
        memcpy(x->ref, tmp, (size_t) n + 1);
    }
    return ZXV_OK;
}

static int32_t funds_and_plan(zsc_house *h, plan_t *pl) {
    uint32_t np = h->n_part, nb = 0;
    int32_t st;
    /* range of the net positions */
    for (uint32_t k = 0; k < h->n_ccy * np; k++)
        if (pl->pos[k] > ZXV_AMOUNT_MAX || pl->pos[k] < -ZXV_AMOUNT_MAX) {
            zind_set_error("zsc_cycle_close: the net position of %s in %s is beyond 2^53-1",
                           h->part[k % np].code, h->ccy[k / np].ccy);
            return ZXV_E_RANGE;
        }
    pl->post = (post_t *) calloc((size_t) h->n_ccy * np * 6 + 1, sizeof *pl->post);
    pl->bal = (balrow_t *) calloc((size_t) h->n_ccy * (np + 5) + 1, sizeof *pl->bal);
    if (!pl->post || !pl->bal) return ZXV_E_NOMEM;
    for (uint32_t c = 0; c < h->n_ccy; c++) {
        int any = 0;
        int64_t hub_eq = 0, hub_in = 0, beq[4] = {0, 0, 0, 0}, bin[4] = {0, 0, 0, 0};
        for (uint32_t p = 0; p < np; p++) any |= pl->active[c * np + p];
        if (!any) continue;
        st = equity(h, h->ccy[c].hub, &hub_eq);
        for (int b = 0; b < 4 && st == ZXV_OK; b++) st = equity(h, h->ccy[c].bucket[b], &beq[b]);
        if (st != ZXV_OK) return st;
        for (uint32_t p = 0; p < np; p++) {
            const feerow_t *r = &pl->fr[c * np + p];
            int64_t net = pl->pos[c * np + p], eq = 0, after = 0, fee = 0;
            if (!pl->active[c * np + p]) continue;
            st = equity(h, h->part[p].acct[c], &eq);
            if (st != ZXV_OK) return st;
            if (r->used) {
                if (r->fee > (uint64_t) ZXV_AMOUNT_MAX) return ZXV_E_RANGE;
                fee = (int64_t) r->fee;
            }
            if (net < 0 && eq < -net) {
                zind_set_error("zsc_cycle_close: %s holds %lld in %s and owes a net %lld; nothing "
                               "was posted",
                               h->part[p].code, (long long) eq, h->ccy[c].ccy, (long long) -net);
                return ZXV_E_FUNDS;
            }
            if (!add64(eq, net, &after) || after > ZXV_AMOUNT_MAX) {
                zind_set_error("zsc_cycle_close: %s's balance in %s would pass 2^53-1",
                               h->part[p].code, h->ccy[c].ccy);
                return ZXV_E_RANGE;
            }
            if (after < fee) {
                zind_set_error("zsc_cycle_close: %s cannot pay its fee of %lld in %s after "
                               "settlement (it will hold %lld); nothing was posted",
                               h->part[p].code, (long long) fee, h->ccy[c].ccy, (long long) after);
                return ZXV_E_FUNDS;
            }
            if (net < 0 && !add64(hub_in, -net, &hub_in)) return ZXV_E_RANGE;
            for (int b = 0; b < 4 && r->used; b++)
                if (!add64(bin[b], (int64_t) r->split[b], &bin[b])) return ZXV_E_RANGE;
            pl->bal[nb].acct = h->part[p].acct[c];
            pl->bal[nb].ccy = (uint8_t) c;
            pl->bal[nb].open = eq;
            pl->bal[nb].close = after - fee;
            nb++;
        }
        {
            int64_t t = 0;
            if (!add64(hub_eq, hub_in, &t) || t > ZXV_AMOUNT_MAX) {
                zind_set_error("zsc_cycle_close: the hub in %s would pass 2^53-1", h->ccy[c].ccy);
                return ZXV_E_RANGE;
            }
            for (int b = 0; b < 4; b++)
                if (!add64(beq[b], bin[b], &t) || t > ZXV_AMOUNT_MAX) return ZXV_E_RANGE;
        }
        pl->bal[nb].acct = h->ccy[c].hub;
        pl->bal[nb].ccy = (uint8_t) c;
        pl->bal[nb].open = pl->bal[nb].close = hub_eq;
        nb++;
        for (int b = 0; b < 4; b++) {
            pl->bal[nb].acct = h->ccy[c].bucket[b];
            pl->bal[nb].ccy = (uint8_t) c;
            pl->bal[nb].open = beq[b];
            pl->bal[nb].close = beq[b] + bin[b];
            nb++;
        }
        /* the postings, in their fixed order */
        for (uint32_t p = 0; p < np; p++)
            if (pl->pos[c * np + p] < 0)
                if (plan_post(pl, ZSC_POST_PAYIN, (uint8_t) c, (uint16_t) p, 0, -pl->pos[c * np + p],
                          h->part[p].acct[c], h->ccy[c].hub, h) != ZXV_OK) return ZXV_E_INTERNAL;
        for (uint32_t p = 0; p < np; p++)
            if (pl->pos[c * np + p] > 0)
                if (plan_post(pl, ZSC_POST_PAYOUT, (uint8_t) c, (uint16_t) p, 0, pl->pos[c * np + p],
                          h->ccy[c].hub, h->part[p].acct[c], h) != ZXV_OK) return ZXV_E_INTERNAL;
        for (uint32_t p = 0; p < np; p++) {
            const feerow_t *r = &pl->fr[c * np + p];
            if (!r->used) continue;
            for (uint8_t b = 0; b < 4; b++)
                if (r->split[b] > 0)
                    if (plan_post(pl, ZSC_POST_FEE, (uint8_t) c, (uint16_t) p, b, (int64_t) r->split[b],
                              h->part[p].acct[c], h->ccy[c].bucket[b], h) != ZXV_OK) return ZXV_E_INTERNAL;
        }
    }
    pl->n_bal = nb;
    return ZXV_OK;
}

static int32_t render_iso(zsc_house *h, plan_t *pl) {
    uint32_t np = h->n_part, n = 0, max = pl->n_post + h->n_ccy * np + 1;
    int32_t st = ZXV_OK;
    pl->iso = (iso_t *) calloc(max, sizeof *pl->iso);
    if (!pl->iso) return ZXV_E_NOMEM;
    for (uint32_t i = 0; i < pl->n_post && st == ZXV_OK; i++) {
        const post_t *x = &pl->post[i];
        zxv_msg *m = NULL;
        char fn[64], tn[64];
        size_t xl = 0;
        if (x->kind == ZSC_POST_FEE) continue;
        acct_name(h, x->from, fn, sizeof fn);
        acct_name(h, x->to, tn, sizeof tn);
        st = zxv_msg_create_cap(h->cap, ZXV_MX_PACS008, &m);
        if (st != ZXV_OK) break;
        if ((st = zxv_msg_set_text(m, ZXV_FLD_MSG_ID, x->ref)) ||
            (st = zxv_msg_set_text(m, ZXV_FLD_CRE_DT_TM, h->settle_dt)) ||
            (st = zxv_msg_set_text(m, ZXV_FLD_DEBTOR_NAME,
                                   x->kind == ZSC_POST_PAYIN ? h->part[x->part].code : h->id)) ||
            (st = zxv_msg_set_text(m, ZXV_FLD_DEBTOR_ACCT, fn)) ||
            (st = zxv_msg_set_text(m, ZXV_FLD_CREDITOR_NAME,
                                   x->kind == ZSC_POST_PAYOUT ? h->part[x->part].code : h->id)) ||
            (st = zxv_msg_set_text(m, ZXV_FLD_CREDITOR_ACCT, tn)) ||
            (st = zxv_msg_set_text(m, ZXV_FLD_END_TO_END_ID, x->ref)) ||
            (st = zxv_msg_set_text(m, ZXV_FLD_CCY, h->ccy[x->ccy].ccy)) ||
            (st = zxv_msg_set_amount(m, ZXV_AMT_SETTLEMENT, x->amount, h->ccy[x->ccy].minor)))
            goto done1;
        st = zxv_msg_render(m, NULL, 0, &xl);
        if (st == ZXV_E_BUFFER) {
            pl->iso[n].xml = (char *) malloc(xl + 1);
            st = pl->iso[n].xml ? zxv_msg_render(m, pl->iso[n].xml, xl + 1, &xl) : ZXV_E_NOMEM;
        } else if (st == ZXV_OK) {
            st = ZXV_E_INTERNAL;
        }
        if (st == ZXV_OK) {
            pl->iso[n].kind = ZSC_ISO_PACS008;
            pl->iso[n].len = xl;
            memcpy(pl->iso[n].msg_id, x->ref, sizeof x->ref);
            st = zxv_sha3_256((const uint8_t *) pl->iso[n].xml, xl, pl->iso[n].dig);
            n++;
        }
    done1:
        zxv_msg_destroy(m);
    }
    /* a camt.053 statement per participant and currency with postings */
    for (uint32_t c = 0; c < h->n_ccy && st == ZXV_OK; c++)
        for (uint32_t p = 0; p < np && st == ZXV_OK; p++) {
            zxv_msg *m = NULL;
            const balrow_t *b = NULL;
            char an[64], mid[40];
            size_t xl = 0;
            uint32_t entries = 0;
            for (uint32_t i = 0; i < pl->n_bal; i++)
                if (pl->bal[i].acct == h->part[p].acct[c]) b = &pl->bal[i];
            for (uint32_t i = 0; i < pl->n_post; i++)
                if (pl->post[i].ccy == c && pl->post[i].part == p) entries++;
            if (!b || entries == 0) continue;
            acct_name(h, h->part[p].acct[c], an, sizeof an);
            {
                char tmp[96];
                int nn = snprintf(tmp, sizeof tmp, "%s/%u/%s/%u/ST", h->id, h->cycle_no,
                                  h->ccy[c].ccy, (unsigned) p);
                if (nn < 0 || (size_t) nn >= 36) {
                    st = ZXV_E_INTERNAL;
                    break;
                }
                memcpy(mid, tmp, (size_t) nn + 1);
            }
            st = zxv_msg_create_cap(h->cap, ZXV_MX_CAMT053, &m);
            if (st != ZXV_OK) break;
            if ((st = zxv_msg_set_text(m, ZXV_FLD_MSG_ID, mid)) ||
                (st = zxv_msg_set_text(m, ZXV_FLD_CRE_DT_TM, h->settle_dt)) ||
                (st = zxv_msg_set_text(m, ZXV_FLD_ACCT_ID, an)) ||
                (st = zxv_msg_set_text(m, ZXV_FLD_CCY, h->ccy[c].ccy)) ||
                (st = zxv_msg_set_amount(m, ZXV_AMT_OPENING, b->open, h->ccy[c].minor)) ||
                (st = zxv_msg_set_amount(m, ZXV_AMT_CLOSING, b->close, h->ccy[c].minor)))
                goto done2;
            for (uint32_t i = 0; i < pl->n_post && st == ZXV_OK; i++) {
                const post_t *x = &pl->post[i];
                if (x->ccy != c || x->part != p) continue;
                st = zxv_msg_add_entry(m, x->amount, h->ccy[c].minor,
                                       x->kind == ZSC_POST_PAYOUT ? ZXV_CRDT : ZXV_DBIT,
                                       ZXV_STS_BOOK);
            }
            if (st != ZXV_OK) goto done2;
            st = zxv_msg_render(m, NULL, 0, &xl);
            if (st == ZXV_E_BUFFER) {
                pl->iso[n].xml = (char *) malloc(xl + 1);
                st = pl->iso[n].xml ? zxv_msg_render(m, pl->iso[n].xml, xl + 1, &xl) : ZXV_E_NOMEM;
            } else if (st == ZXV_OK) {
                st = ZXV_E_INTERNAL;
            }
            if (st == ZXV_OK) {
                pl->iso[n].kind = ZSC_ISO_CAMT053;
                pl->iso[n].len = xl;
                memcpy(pl->iso[n].msg_id, mid, strlen(mid) + 1);
                st = zxv_sha3_256((const uint8_t *) pl->iso[n].xml, xl, pl->iso[n].dig);
                n++;
            }
        done2:
            zxv_msg_destroy(m);
        }
    pl->n_iso = n;
    return st;
}

static void wr_instr_db(zsc_dbuf *d, const zsc_instr *in) {
    uint8_t buf[ZSC_INSTR_WIRE_MAX];
    zind_wr w;
    zind_wr_init(&w, buf, sizeof buf);
    zsc_wr_instr(&w, in);
    if (w.overflow) d->bad = 1;
    else zsc_db_bytes(d, buf, w.len);
}

/* The canonical clearing-cycle record (README.md, "The clearing-cycle
 * record"). */
static int32_t build_blob(zsc_house *h, const plan_t *pl, uint64_t first, uint64_t last,
                          uint64_t head, const uint8_t jdig[32], zsc_dbuf *d) {
    uint32_t np = h->n_part, n;
    char name[64];
    zsc_db_bytes(d, "ZSC1", 4);
    zsc_db_str(d, h->id);
    zsc_db_bytes(d, zind_signer_key_id(h->signer), 32);
    zsc_db_u32(d, h->cycle_no);
    zsc_db_str(d, h->cycle_id);
    zsc_db_str(d, h->settle_dt);
    zsc_db_u8(d, pl->backend);
    zsc_db_u8(d, (uint8_t) h->n_ccy);
    for (uint32_t c = 0; c < h->n_ccy; c++) {
        zsc_db_str(d, h->ccy[c].ccy);
        zsc_db_u8(d, h->ccy[c].minor);
    }
    zsc_db_u16(d, (uint16_t) np);
    for (uint32_t p = 0; p < np; p++) zsc_db_str(d, h->part[p].code);
    zsc_db_u32(d, pl->n);
    for (uint32_t i = 0; i < pl->n; i++) wr_instr_db(d, &pl->sorted[i]->in);
    zsc_db_u32(d, h->n_held);
    for (uint32_t g = 0; g < h->n_held; g++) {
        zsc_db_str(d, h->held[g].e2e);
        zsc_db_u8(d, (uint8_t) h->held[g].n);
        for (uint32_t k = 0; k < h->held[g].n; k++) wr_instr_db(d, &h->held[g].v[k].in);
    }
    zsc_db_u32(d, h->n_res);
    for (uint32_t r = 0; r < h->n_res; r++) {
        zsc_db_str(d, h->res[r].e2e);
        zsc_db_u8(d, h->res[r].choice);
        zsc_db_bytes(d, h->res[r].group, 32);
        zsc_db_bytes(d, h->res[r].chosen, 32);
        zsc_db_bytes(d, h->res[r].op_id, 32);
        zsc_db_bytes(d, h->res[r].sig, ZIND_SIG_BYTES);
    }
    zsc_db_u32(d, h->n_wd);
    for (uint32_t w = 0; w < h->n_wd; w++) wr_instr_db(d, &h->wd[w].in);
    n = 0;
    for (uint32_t k = 0; k < h->n_ccy * np; k++) n += pl->active[k];
    zsc_db_u32(d, n);
    for (uint32_t k = 0; k < h->n_ccy * np; k++)
        if (pl->active[k]) {
            zsc_db_u8(d, (uint8_t) (k / np));
            zsc_db_u16(d, (uint16_t) (k % np));
            zsc_db_u64(d, (uint64_t) pl->pos[k]);
        }
    n = 0;
    for (uint32_t k = 0; k < h->n_ccy * np; k++) n += pl->fr[k].used;
    zsc_db_u32(d, n);
    for (uint32_t k = 0; k < h->n_ccy * np; k++) {
        const feerow_t *r = &pl->fr[k];
        if (!r->used) continue;
        zsc_db_u8(d, r->ccy);
        zsc_db_u16(d, r->part);
        zsc_db_u64(d, r->gross);
        zsc_db_u64(d, r->rem_in);
        zsc_db_u64(d, r->fee);
        zsc_db_u64(d, r->rem_out);
        for (int b = 0; b < 4; b++) zsc_db_u64(d, r->split[b]);
    }
    zsc_db_u32(d, pl->n_post);
    for (uint32_t i = 0; i < pl->n_post; i++) {
        const post_t *x = &pl->post[i];
        zsc_db_u8(d, x->kind);
        zsc_db_u8(d, x->ccy);
        zsc_db_u16(d, x->part);
        zsc_db_u8(d, x->bucket);
        zsc_db_u64(d, (uint64_t) x->amount);
        zsc_db_u64(d, x->entry);
        acct_name(h, x->from, name, sizeof name);
        zsc_db_str(d, name);
        acct_name(h, x->to, name, sizeof name);
        zsc_db_str(d, name);
        zsc_db_str(d, x->ref);
    }
    zsc_db_u32(d, pl->n_bal);
    for (uint32_t i = 0; i < pl->n_bal; i++) {
        acct_name(h, pl->bal[i].acct, name, sizeof name);
        zsc_db_str(d, name);
        zsc_db_u8(d, pl->bal[i].ccy);
        zsc_db_u64(d, (uint64_t) pl->bal[i].open);
        zsc_db_u64(d, (uint64_t) pl->bal[i].close);
    }
    zsc_db_u64(d, first);
    zsc_db_u64(d, last);
    zsc_db_u64(d, head);
    zsc_db_bytes(d, jdig, 32);
    zsc_db_u32(d, pl->n_iso);
    for (uint32_t i = 0; i < pl->n_iso; i++) {
        zsc_db_u8(d, pl->iso[i].kind);
        zsc_db_str(d, pl->iso[i].msg_id);
        zsc_db_bytes(d, pl->iso[i].dig, 32);
    }
    return d->bad ? ZXV_E_NOMEM : ZXV_OK;
}

static int32_t append_blob(zsc_house *h, const zsc_dbuf *d, uint32_t *first_rec, uint32_t *n_rec) {
    size_t frags = (d->len + ZSC_FRAG_MAX - 1) / ZSC_FRAG_MAX;
    uint8_t *buf;
    int32_t st = ZXV_OK;
    if (frags == 0) frags = 1;
    if (frags > 0xffff) return ZXV_E_TOO_LONG;
    st = zind_chain_count(h->chain, first_rec);
    if (st != ZXV_OK) return st;
    buf = (uint8_t *) malloc(8 + ZSC_FRAG_MAX);
    if (!buf) return ZXV_E_NOMEM;
    for (size_t f = 0; f < frags && st == ZXV_OK; f++) {
        size_t off = f * ZSC_FRAG_MAX, len = d->len - off;
        zind_wr w;
        if (len > ZSC_FRAG_MAX) len = ZSC_FRAG_MAX;
        zind_wr_init(&w, buf, 8 + ZSC_FRAG_MAX);
        zind_wr_u32(&w, h->cycle_no);
        zind_wr_u16(&w, (uint16_t) f);
        zind_wr_u16(&w, (uint16_t) frags);
        zind_wr_bytes(&w, d->buf + off, len);
        st = zind_chain_append(h->cap, h->chain, h->signer, ZSC_REC_CYCLE, buf, w.len, NULL, NULL);
    }
    free(buf);
    *n_rec = (uint32_t) frags;
    return st;
}

static void go_broken(zsc_house *h, const char *what, int32_t st) {
    uint8_t subj[32];
    char msg[96];
    int n = snprintf(msg, sizeof msg, "%s/%u/%s", h->id, h->cycle_no, what);
    h->broken = 1;
    if (n > 0 && zind_subject("sutra-clear/broken", (const uint8_t *) msg, (size_t) n, subj) == ZXV_OK)
        quarantine(h, ZXV_LP_FALSE, REASON_BROKEN, h->cycle_no, subj);
    zind_set_error("zsc_cycle_close: %s failed (%s) after the checks passed; the house stopped",
                   what, zxv_status_name(st));
}

int32_t zsc_cycle_close(zsc_house *h, zsc_cycle_info *out) {
    plan_t pl;
    uint32_t np, settled_count = 0, k;
    uint64_t first = 0, last = 0, head = 0;
    uint8_t jdig[32];
    zsc_journal j;
    zsc_dbuf d;
    zsc_cycle_info info;
    int32_t st = usable(h);
    if (st != ZXV_OK) return st;
    if (!h->open) {
        zind_set_error("zsc_cycle_close: no cycle is open");
        return ZSC_E_CYCLE;
    }
    np = h->n_part;
    memset(&pl, 0, sizeof pl);
    memset(&d, 0, sizeof d);
    memset(&j, 0, sizeof j);
    memset(&info, 0, sizeof info);
    st = zind_seen_count(h->settled, &settled_count);
    if (st != ZXV_OK) return st;
    if ((uint64_t) settled_count + h->n_alive > ZSC_MAX_SETTLED) {
        zind_set_error("zsc_cycle_close: the settled-id set is full");
        return ZXV_E_CAPACITY;
    }
    pl.sorted = (item_t **) malloc(((size_t) h->n_alive + 1) * sizeof *pl.sorted);
    pl.pos = (int64_t *) calloc((size_t) h->n_ccy * np + 1, sizeof *pl.pos);
    pl.active = (uint8_t *) calloc((size_t) h->n_ccy * np + 1, 1);
    pl.fr = (feerow_t *) calloc((size_t) h->n_ccy * np + 1, sizeof *pl.fr);
    if (!pl.sorted || !pl.pos || !pl.active || !pl.fr) {
        plan_free(&pl);
        return ZXV_E_NOMEM;
    }
    for (uint32_t i = 0; i < h->n_items; i++)
        if (h->items[i].alive) pl.sorted[pl.n++] = &h->items[i];
    if (pl.n > 1) qsort(pl.sorted, pl.n, sizeof *pl.sorted, cmp_item_e2e);
    st = net_all(h, &pl, h->nctx);
    if (st == ZXV_OK) st = fees_all(h, &pl);
    if (st == ZXV_OK) st = funds_and_plan(h, &pl);
    if (st == ZXV_OK) st = render_iso(h, &pl);
    if (st != ZXV_OK) {
        plan_free(&pl);
        return st;
    }
    /* post: nothing above wrote anything */
    for (k = 0; k < pl.n_post; k++) {
        post_t *x = &pl.post[k];
        st = zxv_ledger_post_cap(h->cap, x->from, x->to, x->amount, h->ccy[x->ccy].ccy, x->ref,
                                 &x->entry);
        if (st != ZXV_OK) {
            go_broken(h, "a ledger posting", st);
            plan_free(&pl);
            return ZSC_E_BROKEN;
        }
        if (k == 0) first = x->entry;
        last = x->entry;
    }
    for (k = 0; k < pl.n_bal; k++) {
        int64_t eq = 0;
        st = equity(h, pl.bal[k].acct, &eq);
        if (st != ZXV_OK || eq != pl.bal[k].close) {
            go_broken(h, "the closing balance check", st ? st : ZXV_E_INVARIANT);
            plan_free(&pl);
            return ZSC_E_BROKEN;
        }
    }
    st = zsc_journal_read(h->cap, &j);
    if (st == ZXV_OK) {
        head = j.n_post ? j.post[j.n_post - 1].id : 0;
        st = zsc_journal_digest(&j, head, jdig);
    }
    zsc_journal_free(&j);
    if (st == ZXV_OK) st = build_blob(h, &pl, first, last, head, jdig, &d);
    if (st == ZXV_OK) st = append_blob(h, &d, &info.first_record, &info.records);
    zsc_db_free(&d);
    if (st != ZXV_OK) {
        go_broken(h, "the cycle record", st);
        plan_free(&pl);
        return ZSC_E_BROKEN;
    }
    /* commit the house state */
    for (uint32_t i = 0; i < pl.n; i++) {
        int32_t s2 = zind_seen_add(h->settled, pl.sorted[i]->e2e_dig);
        (void) s2; /* ZIND_E_REPLAY for an operator's choice, already recorded */
    }
    for (uint32_t x = 0; x < h->n_ccy * np; x++)
        if (pl.fr[x].used) h->part[x % np].carry[x / np] = pl.fr[x].rem_out;
    free_last(h);
    {
        uint32_t na = 0, nf = 0;
        for (uint32_t x = 0; x < h->n_ccy * np; x++) {
            na += pl.active[x];
            nf += pl.fr[x].used;
        }
        h->pos = (posrow_t *) calloc(na + 1u, sizeof *h->pos);
        h->fee = (feerow_t *) calloc(nf + 1u, sizeof *h->fee);
        if (h->pos && h->fee) {
            for (uint32_t x = 0; x < h->n_ccy * np; x++) {
                if (pl.active[x]) {
                    h->pos[h->n_pos].ccy = (uint8_t) (x / np);
                    h->pos[h->n_pos].part = (uint16_t) (x % np);
                    h->pos[h->n_pos].net = pl.pos[x];
                    h->n_pos++;
                }
                if (pl.fr[x].used) h->fee[h->n_fee++] = pl.fr[x];
            }
        }
    }
    h->post = pl.post;
    h->n_post = pl.n_post;
    pl.post = NULL;
    h->iso = pl.iso;
    h->n_iso = pl.n_iso;
    pl.iso = NULL;
    pl.n_iso = 0;
    info.cycle_no = h->cycle_no;
    info.instructions = pl.n;
    info.held = h->n_held;
    info.postings = h->n_post;
    info.first_entry = first;
    info.last_entry = last;
    info.backend = pl.backend;
    plan_free(&pl);
    h->open = 0;
    h->n_items = h->n_alive = 0;
    h->n_res = 0;
    h->n_wd = 0;
    if (out) *out = info;
    return ZXV_OK;
}

/* ===== results ===== */

int32_t zsc_position(const zsc_house *h, const char *code, const char *ccy, int64_t *out_net) {
    int c, p;
    if (!h || !out_net) return ZXV_E_ARG;
    c = find_ccy(h, ccy);
    p = find_part(h, code);
    if (c < 0) return ZXV_E_CURRENCY;
    if (p < 0) return ZSC_E_PARTICIPANT;
    for (uint32_t i = 0; i < h->n_pos; i++)
        if (h->pos[i].ccy == c && h->pos[i].part == p) {
            *out_net = h->pos[i].net;
            return ZXV_OK;
        }
    *out_net = 0;
    return ZXV_E_NOT_FOUND;
}

int32_t zsc_fee(const zsc_house *h, const char *code, const char *ccy, zsc_fee_info *out) {
    int c, p;
    if (!h || !out) return ZXV_E_ARG;
    c = find_ccy(h, ccy);
    p = find_part(h, code);
    if (c < 0) return ZXV_E_CURRENCY;
    if (p < 0) return ZSC_E_PARTICIPANT;
    for (uint32_t i = 0; i < h->n_fee; i++)
        if (h->fee[i].ccy == c && h->fee[i].part == p) {
            out->gross = h->fee[i].gross;
            out->rem_in = h->fee[i].rem_in;
            out->fee = h->fee[i].fee;
            out->rem_out = h->fee[i].rem_out;
            memcpy(out->split, h->fee[i].split, sizeof out->split);
            return ZXV_OK;
        }
    return ZXV_E_NOT_FOUND;
}

int32_t zsc_posting_count(const zsc_house *h, uint32_t *out_count) {
    if (!h || !out_count) return ZXV_E_ARG;
    *out_count = h->n_post;
    return ZXV_OK;
}

int32_t zsc_posting_at(const zsc_house *h, uint32_t i, zsc_posting *out) {
    const post_t *x;
    if (!h || !out) return ZXV_E_ARG;
    if (i >= h->n_post) return ZXV_E_NOT_FOUND;
    x = &h->post[i];
    memset(out, 0, sizeof *out);
    out->kind = x->kind;
    out->bucket = x->bucket;
    memcpy(out->ccy, h->ccy[x->ccy].ccy, 4);
    memcpy(out->code, h->part[x->part].code, 12);
    out->amount = x->amount;
    out->entry_id = x->entry;
    memcpy(out->ref, x->ref, sizeof out->ref);
    return ZXV_OK;
}

int32_t zsc_iso_count(const zsc_house *h, uint32_t *out_count) {
    if (!h || !out_count) return ZXV_E_ARG;
    *out_count = h->n_iso;
    return ZXV_OK;
}

int32_t zsc_iso_at(const zsc_house *h, uint32_t i, int32_t *out_kind, char *buf, size_t cap,
                   size_t *out_len) {
    if (!h || !out_len) return ZXV_E_ARG;
    if (i >= h->n_iso) return ZXV_E_NOT_FOUND;
    if (out_kind) *out_kind = h->iso[i].kind;
    *out_len = h->iso[i].len;
    if (!buf || cap <= h->iso[i].len) return ZXV_E_BUFFER;
    memcpy(buf, h->iso[i].xml, h->iso[i].len + 1);
    return ZXV_OK;
}
