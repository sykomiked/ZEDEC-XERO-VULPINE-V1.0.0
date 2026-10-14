/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zmcommand.c — mesh-command: signed field reports merged into an incident
 * picture with zxv_lp_from_evidence, contradictions held as BOTH until a
 * commander's signed decision, advisory assignments approved by a
 * commander, and a signed journal that replays offline. Integer code only
 * (R11). */
#include <stdlib.h>
#include <string.h>

#include "zmcommand.h"

#define REPORT_BODY_MAX 256u
#define DECISION_MAX    320u
#define APPROVE_MAX     160u
#define MAP_SLOTS       (1u << 17) /* 2 x ZMC_MAX_REPORTS */
#define DEC_MAGIC       "zmc/decision/v1"
#define DEC_MAGIC_LEN   15u
#define APP_MAGIC       "zmc/approve/v1"
#define APP_MAGIC_LEN   14u

const char *zmc_status_name(int32_t status) {
    switch (status) {
    case ZMC_E_UNIT: return "ZMC_E_UNIT";
    case ZMC_E_COMMANDER: return "ZMC_E_COMMANDER";
    case ZMC_E_FACT: return "ZMC_E_FACT";
    case ZMC_E_TASK: return "ZMC_E_TASK";
    case ZMC_E_EQUIVOCATION: return "ZMC_E_EQUIVOCATION";
    case ZMC_E_BLOCKED: return "ZMC_E_BLOCKED";
    case ZMC_E_INCIDENT: return "ZMC_E_INCIDENT";
    default: return zind_status_name(status);
    }
}

/* ===== reports ===== */

static int printable(const char *s, size_t lo, size_t hi) {
    size_t n = 0;
    if (!s) return 0;
    for (; s[n]; n++) {
        if (n >= hi) return 0;
        if ((uint8_t) s[n] < 0x20 || (uint8_t) s[n] > 0x7e) return 0;
    }
    return n >= lo;
}

int32_t zmc_report_check(const zmc_report *r) {
    if (!r) return ZXV_E_ARG;
    if (memchr(r->fact, 0, sizeof r->fact) == NULL || !zind_ident_ok(r->fact, ZMC_FACT_MAX)) {
        zind_set_error("zmc_report: a fact name is 1..31 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    if (r->claim != ZXV_LP_TRUE && r->claim != ZXV_LP_FALSE) {
        zind_set_error("zmc_report: a claim is TRUE or FALSE");
        return ZXV_E_ARG;
    }
    if (r->lat_e6 == ZMC_NO_POS || r->lon_e6 == ZMC_NO_POS) {
        if (r->lat_e6 != r->lon_e6) {
            zind_set_error("zmc_report: give both coordinates or neither");
            return ZXV_E_ARG;
        }
    } else if (r->lat_e6 < -90000000 || r->lat_e6 > 90000000 || r->lon_e6 < -180000000 ||
               r->lon_e6 > 180000000) {
        zind_set_error("zmc_report: position out of range");
        return ZXV_E_RANGE;
    }
    if (memchr(r->note, 0, sizeof r->note) == NULL || !printable(r->note, 0, ZMC_NOTE_MAX)) {
        zind_set_error("zmc_report: a note is up to 120 printable ASCII bytes");
        return ZXV_E_ARG;
    }
    return ZXV_OK;
}

static size_t report_body(const char *incident, const char *unit, uint64_t no, const zmc_report *r,
                          uint8_t *buf, size_t cap) {
    zind_wr w;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, "ZMCR", 4);
    zind_wr_u8(&w, 1);
    zind_wr_str(&w, incident);
    zind_wr_str(&w, unit);
    zind_wr_u64(&w, no);
    zind_wr_str(&w, r->fact);
    zind_wr_u8(&w, (uint8_t) r->claim);
    zind_wr_u64(&w, r->observed);
    zind_wr_u32(&w, (uint32_t) r->lat_e6);
    zind_wr_u32(&w, (uint32_t) r->lon_e6);
    zind_wr_str(&w, r->note);
    return w.overflow ? 0 : w.len;
}

int32_t zmc_report_sign(const zxv_cap *cap, const zind_signer *unit_key, const char *incident,
                        const char *unit, uint64_t report_no, const zmc_report *r, uint8_t *buf,
                        size_t cap_len, size_t *out_len) {
    uint8_t body[REPORT_BODY_MAX], subj[32];
    size_t n;
    int32_t st;
    if (!cap || !unit_key || !out_len) return ZXV_E_ARG;
    if (!zind_ident_ok(incident, ZMC_ID_MAX) || !zind_ident_ok(unit, ZMC_ID_MAX)) {
        zind_set_error("zmc_report_sign: incident and unit IDs are 1..24 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    st = zmc_report_check(r);
    if (st != ZXV_OK) return st;
    n = report_body(incident, unit, report_no, r, body, sizeof body);
    if (n == 0) return ZXV_E_INTERNAL;
    *out_len = n + ZIND_SIG_BYTES;
    if (!buf || cap_len < n + ZIND_SIG_BYTES) return ZXV_E_BUFFER;
    st = zind_subject(ZMC_REPORT_CTX, body, n, subj);
    if (st == ZXV_OK) st = zind_sign(cap, unit_key, subj, 32, ZMC_REPORT_CTX, buf + n);
    if (st != ZXV_OK) return st;
    memcpy(buf, body, n);
    return ZXV_OK;
}

typedef struct {
    char incident[ZMC_ID_MAX + 1];
    char unit[ZMC_ID_MAX + 1];
    uint64_t no;
    zmc_report r;
    size_t body_len;
} parsed_report;

static int32_t report_parse(const uint8_t *buf, size_t len, parsed_report *p) {
    zind_rd rd;
    char tmp[256];
    memset(p, 0, sizeof *p);
    if (!buf || len <= ZIND_SIG_BYTES + 5 || len > REPORT_BODY_MAX + ZIND_SIG_BYTES) goto bad;
    p->body_len = len - ZIND_SIG_BYTES;
    zind_rd_init(&rd, buf, p->body_len);
    if (memcmp(buf, "ZMCR", 4) != 0) goto bad;
    rd.pos = 4;
    if (zind_rd_u8(&rd) != 1) goto bad;
    zind_rd_str(&rd, tmp, sizeof tmp);
    if (rd.bad || !zind_ident_ok(tmp, ZMC_ID_MAX)) goto bad;
    memcpy(p->incident, tmp, strlen(tmp) + 1);
    zind_rd_str(&rd, tmp, sizeof tmp);
    if (rd.bad || !zind_ident_ok(tmp, ZMC_ID_MAX)) goto bad;
    memcpy(p->unit, tmp, strlen(tmp) + 1);
    p->no = zind_rd_u64(&rd);
    zind_rd_str(&rd, tmp, sizeof tmp);
    if (rd.bad || !zind_ident_ok(tmp, ZMC_FACT_MAX)) goto bad;
    memcpy(p->r.fact, tmp, strlen(tmp) + 1);
    p->r.claim = zind_rd_u8(&rd);
    p->r.observed = zind_rd_u64(&rd);
    p->r.lat_e6 = (int32_t) zind_rd_u32(&rd);
    p->r.lon_e6 = (int32_t) zind_rd_u32(&rd);
    zind_rd_str(&rd, tmp, sizeof tmp);
    if (rd.bad || strlen(tmp) > ZMC_NOTE_MAX) goto bad;
    memcpy(p->r.note, tmp, strlen(tmp) + 1);
    if (rd.pos != p->body_len || zmc_report_check(&p->r) != ZXV_OK) goto bad;
    return ZXV_OK;
bad:
    zind_set_error("zmc_ingest: malformed report");
    return ZXV_E_FORMAT;
}

/* ===== the incident ===== */

typedef struct {
    char id[ZMC_ID_MAX + 1];
    uint8_t pk[ZIND_PK_BYTES];
    uint8_t key_id[32];
} member_t;

typedef struct {
    uint64_t no;
    uint32_t epoch;
    uint8_t claim; /* 0: none */
} claim_t;

typedef struct {
    char name[ZMC_FACT_MAX + 1];
    int32_t lp, decision;
    uint8_t tag;
    uint32_t pos, neg, epoch, reports;
    claim_t *claims; /* ZMC_MAX_UNITS, by unit index */
} fact_t;

typedef struct {
    char id[ZMC_ID_MAX + 1];
    char kind[ZMC_ID_MAX + 1];
    int busy;
} resource_t;

typedef struct {
    char id[ZMC_ID_MAX + 1];
    char kind[ZMC_ID_MAX + 1];
    uint8_t priority;
    int fact; /* index, or -1 */
    int32_t required;
    int assigned; /* resource index + 1, or 0 */
} task_t;

typedef struct {
    uint32_t unit;
    uint64_t no;
    uint8_t hash[32];
} seen_t;

struct zmc_incident {
    const zxv_cap *cap;
    const zind_signer *key; /* NULL while replaying */
    char id[ZMC_ID_MAX + 1];
    zind_chain *journal;    /* NULL while replaying */
    zind_qsink *qs;
    member_t *units;
    uint32_t n_units;
    member_t cmd[ZMC_MAX_COMMANDERS];
    uint32_t n_cmd;
    fact_t *facts;
    uint32_t n_facts;
    resource_t *res;
    uint32_t n_res;
    task_t *tasks;
    uint32_t n_tasks;
    seen_t *seen;
    uint32_t n_seen;
    uint32_t *map; /* slot -> seen index + 1 */
    uint32_t held, decisions, approvals;
};

static int32_t incident_new(const zxv_cap *cap, const char *incident, const zind_signer *key,
                            zxv_hexlog *qlog, int journal, zmc_incident **out) {
    zmc_incident *inc;
    int32_t st;
    inc = (zmc_incident *) calloc(1, sizeof *inc);
    if (!inc) return ZXV_E_NOMEM;
    inc->cap = cap;
    inc->key = key;
    memcpy(inc->id, incident, strlen(incident) + 1);
    inc->units = (member_t *) calloc(ZMC_MAX_UNITS, sizeof *inc->units);
    inc->facts = (fact_t *) calloc(ZMC_MAX_FACTS, sizeof *inc->facts);
    inc->res = (resource_t *) calloc(ZMC_MAX_RESOURCES, sizeof *inc->res);
    inc->tasks = (task_t *) calloc(ZMC_MAX_TASKS, sizeof *inc->tasks);
    inc->seen = (seen_t *) calloc(ZMC_MAX_REPORTS, sizeof *inc->seen);
    inc->map = (uint32_t *) calloc(MAP_SLOTS, sizeof *inc->map);
    st = (!inc->units || !inc->facts || !inc->res || !inc->tasks || !inc->seen || !inc->map)
             ? ZXV_E_NOMEM
             : ZXV_OK;
    if (st == ZXV_OK && journal) st = zind_chain_create(ZMC_JOURNAL, ZIND_CHAIN_MAX, &inc->journal);
    if (st == ZXV_OK && qlog) st = zind_qsink_create(cap, qlog, NULL, &inc->qs);
    if (st == ZXV_OK && journal) {
        uint8_t p[64];
        zind_wr w;
        zind_wr_init(&w, p, sizeof p);
        zind_wr_str(&w, incident);
        st = zind_chain_append(cap, inc->journal, key, ZMC_J_INCIDENT, p, w.len, NULL, NULL);
    }
    if (st != ZXV_OK) {
        zmc_incident_destroy(inc);
        return st;
    }
    *out = inc;
    return ZXV_OK;
}

int32_t zmc_incident_create(const zxv_cap *cap, const char *incident,
                            const zind_signer *operator_key, zxv_hexlog *qlog,
                            zmc_incident **out) {
    int32_t st;
    if (!out) return ZXV_E_ARG;
    *out = NULL;
    if (!cap || !operator_key) return ZXV_E_ARG;
    if (!zind_ident_ok(incident, ZMC_ID_MAX)) {
        zind_set_error("zmc_incident_create: an incident ID is 1..24 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("zmc_incident_create: the cap needs CRYPTO_SIGN");
        return st;
    }
    return incident_new(cap, incident, operator_key, qlog, 1, out);
}

void zmc_incident_destroy(zmc_incident *inc) {
    if (!inc) return;
    if (inc->facts)
        for (uint32_t i = 0; i < inc->n_facts; i++) free(inc->facts[i].claims);
    free(inc->units);
    free(inc->facts);
    free(inc->res);
    free(inc->tasks);
    free(inc->seen);
    free(inc->map);
    zind_chain_destroy(inc->journal);
    zind_qsink_destroy(inc->qs);
    free(inc);
}

static int find_member(const member_t *m, uint32_t n, const char *id) {
    if (!id) return -1;
    for (uint32_t i = 0; i < n; i++)
        if (strcmp(m[i].id, id) == 0) return (int) i;
    return -1;
}

static int key_used(const zmc_incident *inc, const uint8_t *pk) {
    for (uint32_t i = 0; i < inc->n_units; i++)
        if (memcmp(inc->units[i].pk, pk, ZIND_PK_BYTES) == 0) return 1;
    for (uint32_t i = 0; i < inc->n_cmd; i++)
        if (memcmp(inc->cmd[i].pk, pk, ZIND_PK_BYTES) == 0) return 1;
    return 0;
}

static int32_t member_add(zmc_incident *inc, member_t *m, uint32_t *n, uint32_t max,
                          const char *id, const uint8_t *pk, const char *what) {
    int32_t st;
    if (!inc || !pk) return ZXV_E_ARG;
    if (!zind_ident_ok(id, ZMC_ID_MAX)) {
        zind_set_error("zmc_%s_add: an ID is 1..24 of [A-Za-z0-9._:/-]", what);
        return ZXV_E_ARG;
    }
    if (find_member(m, *n, id) >= 0 || key_used(inc, pk)) {
        zind_set_error("zmc_%s_add: %s or its key is already registered", what, id);
        return ZXV_E_DUPLICATE;
    }
    if (*n >= max) return ZXV_E_CAPACITY;
    st = zind_key_id(pk, m[*n].key_id);
    if (st != ZXV_OK) return st;
    memcpy(m[*n].id, id, strlen(id) + 1);
    memcpy(m[*n].pk, pk, ZIND_PK_BYTES);
    (*n)++;
    return ZXV_OK;
}

int32_t zmc_unit_add(zmc_incident *inc, const char *unit, const uint8_t pk[ZIND_PK_BYTES]) {
    if (!inc) return ZXV_E_ARG;
    return member_add(inc, inc->units, &inc->n_units, ZMC_MAX_UNITS, unit, pk, "unit");
}

int32_t zmc_commander_add(zmc_incident *inc, const char *commander,
                          const uint8_t pk[ZIND_PK_BYTES]) {
    if (!inc) return ZXV_E_ARG;
    return member_add(inc, inc->cmd, &inc->n_cmd, ZMC_MAX_COMMANDERS, commander, pk, "commander");
}

static void quarantine(zmc_incident *inc, uint8_t tag, uint8_t reason, uint32_t who,
                       const char *dom, const uint8_t *data, size_t len) {
    uint8_t subj[32];
    if (!inc->qs) return;
    if (zind_subject(dom, data, len, subj) != ZXV_OK) return;
    (void) zind_qsink_event(inc->qs, tag, reason, who, subj, NULL);
}

static void quarantine_tamper(zmc_incident *inc, uint32_t who, const char *dom,
                              const uint8_t *data, size_t len) {
    uint8_t tag = 0;
    if (!inc->qs) return;
    if (zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING, &tag) != ZXV_OK)
        return;
    quarantine(inc, tag, ZIND_REASON_TAMPER, who, dom, data, len);
}

/* ===== facts ===== */

static int find_fact(const zmc_incident *inc, const char *name) {
    if (!name) return -1;
    for (uint32_t i = 0; i < inc->n_facts; i++)
        if (strcmp(inc->facts[i].name, name) == 0) return (int) i;
    return -1;
}

static int32_t fact_tag(const fact_t *f, int32_t before, uint8_t *out) {
    uint8_t pol, phase;
    if (f->lp == before)
        pol = ZXV_HEX_POL_NEUTRAL;
    else if (f->lp == ZXV_LP_BOTH)
        pol = ZXV_HEX_POL_ENTROPIC;
    else
        pol = ZXV_HEX_POL_NEGENTROPIC;
    if (f->decision != ZXV_LP_NEITHER && f->lp != ZXV_LP_BOTH)
        phase = ZXV_HEX_PHASE_MANIFEST;
    else if (f->pos + f->neg >= 2)
        phase = ZXV_HEX_PHASE_EMERGING;
    else
        phase = ZXV_HEX_PHASE_POTENTIAL;
    return zind_tag(f->lp, pol, phase, out);
}

/* Count this epoch's claims and derive the state. */
static int32_t fact_eval(zmc_incident *inc, fact_t *f) {
    int32_t before = f->lp, st;
    f->pos = f->neg = 0;
    for (uint32_t u = 0; u < inc->n_units; u++) {
        const claim_t *c = &f->claims[u];
        if (c->claim == 0 || c->epoch != f->epoch) continue;
        if (c->claim == ZXV_LP_TRUE)
            f->pos++;
        else
            f->neg++;
    }
    st = zxv_lp_from_evidence(f->pos + (f->decision == ZXV_LP_TRUE ? 1u : 0u),
                              f->neg + (f->decision == ZXV_LP_FALSE ? 1u : 0u), &f->lp);
    if (st == ZXV_OK) st = fact_tag(f, before, &f->tag);
    return st;
}

static int32_t fact_get(zmc_incident *inc, const char *name, int *out) {
    fact_t *f;
    int i = find_fact(inc, name);
    if (i >= 0) {
        *out = i;
        return ZXV_OK;
    }
    if (inc->n_facts >= ZMC_MAX_FACTS) {
        zind_set_error("zmc: the incident already tracks %u facts", ZMC_MAX_FACTS);
        return ZXV_E_CAPACITY;
    }
    f = &inc->facts[inc->n_facts];
    memset(f, 0, sizeof *f);
    f->claims = (claim_t *) calloc(ZMC_MAX_UNITS, sizeof *f->claims);
    if (!f->claims) return ZXV_E_NOMEM;
    memcpy(f->name, name, strlen(name) + 1);
    f->lp = ZXV_LP_NEITHER;
    f->decision = ZXV_LP_NEITHER;
    if (fact_tag(f, ZXV_LP_NEITHER, &f->tag) != ZXV_OK) {
        free(f->claims);
        f->claims = NULL;
        return ZXV_E_INTERNAL;
    }
    *out = (int) inc->n_facts++;
    return ZXV_OK;
}

int32_t zmc_fact_state(const zmc_incident *inc, const char *fact, zmc_fact_view *out) {
    const fact_t *f;
    int i;
    if (!inc || !out) return ZXV_E_ARG;
    i = find_fact(inc, fact);
    if (i < 0) return ZMC_E_FACT;
    f = &inc->facts[i];
    memset(out, 0, sizeof *out);
    out->lp = f->lp;
    out->tag = f->tag;
    out->pos = f->pos;
    out->neg = f->neg;
    out->epoch = f->epoch;
    out->decision = f->decision;
    out->reports = f->reports;
    return ZXV_OK;
}

int32_t zmc_fact_count(const zmc_incident *inc, uint32_t *out_count) {
    if (!inc || !out_count) return ZXV_E_ARG;
    *out_count = inc->n_facts;
    return ZXV_OK;
}

int32_t zmc_fact_name(const zmc_incident *inc, uint32_t i, char *buf, size_t cap) {
    size_t n;
    if (!inc || !buf) return ZXV_E_ARG;
    if (i >= inc->n_facts) return ZXV_E_NOT_FOUND;
    n = strlen(inc->facts[i].name);
    if (cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, inc->facts[i].name, n + 1);
    return ZXV_OK;
}

/* ===== ingest ===== */

static uint32_t slot_of(uint32_t unit, uint64_t no) {
    uint64_t h = (no ^ ((uint64_t) unit << 40)) * UINT64_C(0x9E3779B97F4A7C15);
    return (uint32_t) (h >> 47) & (MAP_SLOTS - 1u);
}

static int find_seen(const zmc_incident *inc, uint32_t unit, uint64_t no, uint32_t *slot) {
    uint32_t s = slot_of(unit, no);
    while (inc->map[s]) {
        const seen_t *e = &inc->seen[inc->map[s] - 1];
        if (e->unit == unit && e->no == no) {
            *slot = s;
            return (int) (inc->map[s] - 1);
        }
        s = (s + 1) & (MAP_SLOTS - 1u);
    }
    *slot = s;
    return -1;
}

static int32_t ingest(zmc_incident *inc, const uint8_t *buf, size_t len, zmc_ingest_info *info) {
    parsed_report p;
    uint8_t subj[32], hash[32], tag = 0;
    uint32_t slot = 0;
    int ui, si, fi;
    fact_t *f;
    claim_t *c;
    int32_t st, before;
    st = report_parse(buf, len, &p);
    if (st != ZXV_OK) return st;
    if (strcmp(p.incident, inc->id) != 0) {
        zind_set_error("zmc_ingest: the report is for incident %s, not %s", p.incident, inc->id);
        return ZMC_E_INCIDENT;
    }
    ui = find_member(inc->units, inc->n_units, p.unit);
    if (ui < 0) {
        zind_set_error("zmc_ingest: unknown unit %s", p.unit);
        return ZMC_E_UNIT;
    }
    st = zind_subject(ZMC_REPORT_CTX, buf, p.body_len, subj);
    if (st != ZXV_OK) return st;
    st = zind_verify(inc->cap, inc->units[ui].pk, subj, 32, ZMC_REPORT_CTX, buf + p.body_len);
    if (st == ZXV_E_VERIFY) {
        quarantine_tamper(inc, (uint32_t) ui, "mesh-command/forged", buf, len);
        zind_set_error("zmc_ingest: report %llu is not signed by %s", (unsigned long long) p.no,
                       p.unit);
        return ZIND_E_CHAIN;
    }
    if (st != ZXV_OK) return st;
    st = zind_subject("mesh-command/rec", buf, p.body_len, hash);
    if (st != ZXV_OK) return st;
    si = find_seen(inc, (uint32_t) ui, p.no, &slot);
    if (si >= 0) {
        if (memcmp(inc->seen[si].hash, hash, 32) == 0) {
            zind_set_error("zmc_ingest: report %llu of %s is already held",
                           (unsigned long long) p.no, p.unit);
            return ZIND_E_REPLAY;
        }
        if (zind_tag(ZXV_LP_BOTH, ZXV_HEX_POL_ENTROPIC, ZXV_HEX_PHASE_EMERGING, &tag) == ZXV_OK)
            quarantine(inc, tag, ZIND_REASON_DUPLICATE, (uint32_t) ui,
                       "mesh-command/equivocation", buf, p.body_len);
        zind_set_error("zmc_ingest: %s signed two different reports numbered %llu", p.unit,
                       (unsigned long long) p.no);
        return ZMC_E_EQUIVOCATION;
    }
    if (inc->n_seen >= ZMC_MAX_REPORTS) {
        zind_set_error("zmc_ingest: the incident holds %u reports", ZMC_MAX_REPORTS);
        return ZXV_E_CAPACITY;
    }
    if (find_fact(inc, p.r.fact) < 0 && inc->n_facts >= ZMC_MAX_FACTS) {
        zind_set_error("zmc_ingest: the incident already tracks %u facts", ZMC_MAX_FACTS);
        return ZXV_E_CAPACITY;
    }
    if (inc->journal) {
        st = zind_chain_append(inc->cap, inc->journal, inc->key, ZMC_J_REPORT, buf, len, NULL,
                               NULL);
        if (st != ZXV_OK) return st;
    }
    st = fact_get(inc, p.r.fact, &fi);
    if (st != ZXV_OK) return st;
    inc->seen[inc->n_seen].unit = (uint32_t) ui;
    inc->seen[inc->n_seen].no = p.no;
    memcpy(inc->seen[inc->n_seen].hash, hash, 32);
    inc->map[slot] = ++inc->n_seen;
    f = &inc->facts[fi];
    f->reports++;
    before = f->lp;
    c = &f->claims[ui];
    if (info) {
        memset(info, 0, sizeof *info);
        memcpy(info->fact, f->name, strlen(f->name) + 1);
        info->lp_before = before;
    }
    if (c->claim != 0 && p.no < c->no) {
        if (info) {
            info->stale = 1;
            info->lp_after = before;
        }
        return ZXV_OK;
    }
    c->no = p.no;
    c->epoch = f->epoch;
    c->claim = (uint8_t) p.r.claim;
    st = fact_eval(inc, f);
    if (st != ZXV_OK) return st;
    if (info) info->lp_after = f->lp;
    if (f->lp == ZXV_LP_BOTH && before != ZXV_LP_BOTH) {
        uint8_t hb[ZMC_FACT_MAX + 8];
        size_t n = strlen(f->name);
        inc->held++;
        if (info) info->new_hold = 1;
        memcpy(hb, f->name, n);
        hb[n] = 0;
        for (int k = 0; k < 4; k++) hb[n + 1 + (size_t) k] = (uint8_t) (f->epoch >> (8 * k));
        quarantine(inc, f->tag, ZIND_REASON_HELD_BOTH, (uint32_t) ui, "mesh-command/hold", hb,
                   n + 5);
    }
    return ZXV_OK;
}

int32_t zmc_ingest(zmc_incident *inc, const uint8_t *buf, size_t len, zmc_ingest_info *info) {
    if (!inc || !buf) return ZXV_E_ARG;
    if (info) memset(info, 0, sizeof *info);
    return ingest(inc, buf, len, info);
}

/* ===== decisions ===== */

static int32_t evidence_digest(const zmc_incident *inc, const fact_t *f, uint8_t out[32]) {
    uint8_t *b;
    zind_wr w;
    int32_t st;
    size_t cap = 4 + (size_t) inc->n_units * (1 + ZMC_ID_MAX + 8 + 1);
    b = (uint8_t *) malloc(cap);
    if (!b) return ZXV_E_NOMEM;
    zind_wr_init(&w, b, cap);
    uint32_t order[ZMC_MAX_UNITS], n = 0;
    /* counted claims, ordered by unit ID (not by registration order, so a
     * verifier may list the units in any order) */
    for (uint32_t u = 0; u < inc->n_units; u++) {
        const claim_t *c = &f->claims[u];
        uint32_t k;
        if (c->claim == 0 || c->epoch != f->epoch) continue;
        k = n++;
        while (k > 0 && strcmp(inc->units[u].id, inc->units[order[k - 1]].id) < 0) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = u;
    }
    zind_wr_u32(&w, n);
    for (uint32_t k = 0; k < n; k++) {
        const claim_t *c = &f->claims[order[k]];
        zind_wr_str(&w, inc->units[order[k]].id);
        zind_wr_u64(&w, c->no);
        zind_wr_u8(&w, c->claim);
    }
    st = w.overflow ? ZXV_E_INTERNAL : zind_subject("mesh-command/evidence", b, w.len, out);
    free(b);
    return st;
}

static int32_t decision_args(const zmc_incident *inc, const char *fact, int32_t decision,
                             const char *commander, const char *why, int *fi, int *ci) {
    *fi = find_fact(inc, fact);
    if (*fi < 0) {
        zind_set_error("zmc_resolve: unknown fact %s", fact ? fact : "-");
        return ZMC_E_FACT;
    }
    if (decision != ZXV_LP_TRUE && decision != ZXV_LP_FALSE) {
        zind_set_error("zmc_resolve: a decision is TRUE or FALSE");
        return ZXV_E_ARG;
    }
    if (!printable(why, 1, ZMC_WHY_MAX)) {
        zind_set_error("zmc_resolve: the reason is 1..160 printable ASCII bytes");
        return ZXV_E_ARG;
    }
    *ci = find_member(inc->cmd, inc->n_cmd, commander);
    if (*ci < 0) {
        zind_set_error("zmc_resolve: %s is not a registered commander",
                       commander ? commander : "-");
        return ZMC_E_COMMANDER;
    }
    if (inc->facts[*fi].lp != ZXV_LP_BOTH) {
        zind_set_error("zmc_resolve: fact %s is not held", fact);
        return ZXV_E_STATE;
    }
    return ZXV_OK;
}

static int32_t decision_msg(const zmc_incident *inc, const fact_t *f, int32_t decision,
                            const char *commander, const char *why, uint8_t *buf, size_t cap,
                            size_t *out_len) {
    uint8_t ev[32];
    zind_wr w;
    int32_t st;
    size_t need = DEC_MAGIC_LEN + 1 + strlen(inc->id) + 1 + strlen(f->name) + 4 + 32 + 1 + 1 +
                  strlen(commander) + 1 + strlen(why);
    *out_len = need;
    if (!buf || cap < need) return ZXV_E_BUFFER;
    st = evidence_digest(inc, f, ev);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, DEC_MAGIC, DEC_MAGIC_LEN);
    zind_wr_str(&w, inc->id);
    zind_wr_str(&w, f->name);
    zind_wr_u32(&w, f->epoch);
    zind_wr_bytes(&w, ev, 32);
    zind_wr_u8(&w, (uint8_t) decision);
    zind_wr_str(&w, commander);
    zind_wr_str(&w, why);
    return w.overflow || w.len != need ? ZXV_E_INTERNAL : ZXV_OK;
}

int32_t zmc_decision_message(const zmc_incident *inc, const char *fact, int32_t decision,
                             const char *commander, const char *why, uint8_t *buf, size_t cap,
                             size_t *out_len) {
    int fi, ci;
    int32_t st;
    if (!inc || !out_len) return ZXV_E_ARG;
    st = decision_args(inc, fact, decision, commander, why, &fi, &ci);
    if (st != ZXV_OK) return st;
    return decision_msg(inc, &inc->facts[fi], decision, commander, why, buf, cap, out_len);
}

/* expect: when replaying, the recorded message, which must equal ours. */
static int32_t resolve(zmc_incident *inc, const char *fact, int32_t decision,
                       const char *commander, const char *why, const uint8_t *sig,
                       const uint8_t *expect, size_t expect_len) {
    uint8_t msg[DECISION_MAX + 32 + ZIND_SIG_BYTES], subj[32];
    size_t len = 0;
    int fi, ci;
    fact_t *f;
    uint8_t held_tag;
    int32_t st;
    st = decision_args(inc, fact, decision, commander, why, &fi, &ci);
    if (st != ZXV_OK) return st;
    if (inc->journal) {
        st = zxv_cap_check(inc->cap, ZXV_CAP_ATTEST);
        if (st != ZXV_OK) {
            zind_set_error("zmc_resolve: the incident cap needs ATTEST to apply a decision");
            return st;
        }
    }
    f = &inc->facts[fi];
    st = decision_msg(inc, f, decision, commander, why, msg, DECISION_MAX, &len);
    if (st != ZXV_OK) return st;
    if (expect && (expect_len != len || memcmp(expect, msg, len) != 0)) {
        zind_set_error("zmc_verify_journal: the decision names other evidence or another epoch");
        return ZXV_E_VERIFY;
    }
    st = zind_verify(inc->cap, inc->cmd[ci].pk, msg, len, ZMC_DECISION_CTX, sig);
    if (st == ZXV_E_VERIFY) {
        quarantine_tamper(inc, (uint32_t) ci, "mesh-command/forged", sig, ZIND_SIG_BYTES);
        zind_set_error("zmc_resolve: the decision is not signed by %s for the evidence held now",
                       commander);
    }
    if (st != ZXV_OK) return st;
    held_tag = f->tag;
    if (inc->qs) {
        st = zxv_sha3_256(msg, len, subj);
        if (st == ZXV_OK)
            st = zind_qsink_event(inc->qs, held_tag, ZMC_REASON_RESOLVED, (uint32_t) ci, subj,
                                  NULL);
        if (st != ZXV_OK) return st;
    }
    if (inc->journal) {
        memcpy(msg + len, inc->cmd[ci].key_id, 32);
        memcpy(msg + len + 32, sig, ZIND_SIG_BYTES);
        st = zind_chain_append(inc->cap, inc->journal, inc->key, ZMC_J_DECISION, msg,
                               len + 32 + ZIND_SIG_BYTES, NULL, NULL);
        if (st != ZXV_OK) return st;
    }
    f->epoch++;
    f->decision = decision;
    inc->decisions++;
    return fact_eval(inc, f);
}

int32_t zmc_resolve(zmc_incident *inc, const char *fact, int32_t decision, const char *commander,
                    const char *why, const uint8_t sig[ZIND_SIG_BYTES]) {
    if (!inc || !sig) return ZXV_E_ARG;
    return resolve(inc, fact, decision, commander, why, sig, NULL, 0);
}

/* ===== advice ===== */

static int find_res(const zmc_incident *inc, const char *id) {
    if (!id) return -1;
    for (uint32_t i = 0; i < inc->n_res; i++)
        if (strcmp(inc->res[i].id, id) == 0) return (int) i;
    return -1;
}

static int find_task(const zmc_incident *inc, const char *id) {
    if (!id) return -1;
    for (uint32_t i = 0; i < inc->n_tasks; i++)
        if (strcmp(inc->tasks[i].id, id) == 0) return (int) i;
    return -1;
}

int32_t zmc_resource_add(zmc_incident *inc, const char *resource, const char *kind) {
    uint8_t p[64];
    zind_wr w;
    int32_t st;
    if (!inc) return ZXV_E_ARG;
    if (!zind_ident_ok(resource, ZMC_ID_MAX) || !zind_ident_ok(kind, ZMC_ID_MAX)) {
        zind_set_error("zmc_resource_add: IDs are 1..24 of [A-Za-z0-9._:/-]");
        return ZXV_E_ARG;
    }
    if (find_res(inc, resource) >= 0) return ZXV_E_DUPLICATE;
    if (inc->n_res >= ZMC_MAX_RESOURCES) return ZXV_E_CAPACITY;
    if (inc->journal) {
        zind_wr_init(&w, p, sizeof p);
        zind_wr_str(&w, resource);
        zind_wr_str(&w, kind);
        st = zind_chain_append(inc->cap, inc->journal, inc->key, ZMC_J_RESOURCE, p, w.len, NULL,
                               NULL);
        if (st != ZXV_OK) return st;
    }
    memcpy(inc->res[inc->n_res].id, resource, strlen(resource) + 1);
    memcpy(inc->res[inc->n_res].kind, kind, strlen(kind) + 1);
    inc->res[inc->n_res].busy = 0;
    inc->n_res++;
    return ZXV_OK;
}

int32_t zmc_task_add(zmc_incident *inc, const char *task, uint8_t priority, const char *need_kind,
                     const char *requires_fact, int32_t required) {
    uint8_t p[128];
    zind_wr w;
    int fi = -1, has_fact;
    int32_t st;
    if (!inc) return ZXV_E_ARG;
    if (!zind_ident_ok(task, ZMC_ID_MAX) || !zind_ident_ok(need_kind, ZMC_ID_MAX) ||
        priority < 1 || priority > 9) {
        zind_set_error("zmc_task_add: IDs are 1..24 of [A-Za-z0-9._:/-]; priority is 1..9");
        return ZXV_E_ARG;
    }
    has_fact = requires_fact && requires_fact[0];
    if (has_fact && (!zind_ident_ok(requires_fact, ZMC_FACT_MAX) ||
                     (required != ZXV_LP_TRUE && required != ZXV_LP_FALSE))) {
        zind_set_error("zmc_task_add: a required fact is a fact name and TRUE or FALSE");
        return ZXV_E_ARG;
    }
    if (find_task(inc, task) >= 0) return ZXV_E_DUPLICATE;
    if (inc->n_tasks >= ZMC_MAX_TASKS) return ZXV_E_CAPACITY;
    if (has_fact && find_fact(inc, requires_fact) < 0 && inc->n_facts >= ZMC_MAX_FACTS)
        return ZXV_E_CAPACITY;
    if (inc->journal) {
        zind_wr_init(&w, p, sizeof p);
        zind_wr_str(&w, task);
        zind_wr_u8(&w, priority);
        zind_wr_str(&w, need_kind);
        zind_wr_str(&w, has_fact ? requires_fact : "");
        zind_wr_u8(&w, has_fact ? (uint8_t) required : 0);
        st = zind_chain_append(inc->cap, inc->journal, inc->key, ZMC_J_TASK, p, w.len, NULL,
                               NULL);
        if (st != ZXV_OK) return st;
    }
    if (has_fact) {
        st = fact_get(inc, requires_fact, &fi);
        if (st != ZXV_OK) return st;
    }
    {
        task_t *t = &inc->tasks[inc->n_tasks++];
        memset(t, 0, sizeof *t);
        memcpy(t->id, task, strlen(task) + 1);
        memcpy(t->kind, need_kind, strlen(need_kind) + 1);
        t->priority = priority;
        t->fact = fi;
        t->required = has_fact ? required : ZXV_LP_NEITHER;
    }
    return ZXV_OK;
}

static int32_t task_gate(const zmc_incident *inc, const task_t *t) {
    int32_t lp;
    if (t->fact < 0) return ZMC_REC_ASSIGN;
    lp = inc->facts[t->fact].lp;
    if (lp == ZXV_LP_BOTH) return ZMC_REC_HELD;
    if (lp == ZXV_LP_NEITHER) return ZMC_REC_UNKNOWN;
    if (lp != t->required) return ZMC_REC_BLOCKED;
    return ZMC_REC_ASSIGN;
}

/* before(a, b): a is recommended before b. */
static int task_before(const task_t *a, const task_t *b) {
    if (a->priority != b->priority) return a->priority > b->priority;
    return strcmp(a->id, b->id) < 0;
}

int32_t zmc_recommend(const zmc_incident *inc, zmc_rec *out, size_t cap, size_t *out_n) {
    uint32_t order[ZMC_MAX_TASKS], n = 0;
    uint8_t taken[ZMC_MAX_RESOURCES];
    if (!inc || !out_n || (!out && cap)) return ZXV_E_ARG;
    for (uint32_t i = 0; i < inc->n_tasks; i++) {
        uint32_t k;
        if (inc->tasks[i].assigned) continue;
        k = n++;
        while (k > 0 && task_before(&inc->tasks[i], &inc->tasks[order[k - 1]])) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = i;
    }
    *out_n = n;
    if (cap < n) return ZXV_E_BUFFER;
    for (uint32_t r = 0; r < inc->n_res; r++) taken[r] = (uint8_t) inc->res[r].busy;
    for (uint32_t k = 0; k < n; k++) {
        const task_t *t = &inc->tasks[order[k]];
        zmc_rec *o = &out[k];
        memset(o, 0, sizeof *o);
        memcpy(o->task, t->id, strlen(t->id) + 1);
        o->status = task_gate(inc, t);
        if (o->status != ZMC_REC_ASSIGN) continue;
        {
            int best = -1;
            for (uint32_t r = 0; r < inc->n_res; r++) {
                if (taken[r] || strcmp(inc->res[r].kind, t->kind) != 0) continue;
                if (best < 0 || strcmp(inc->res[r].id, inc->res[best].id) < 0) best = (int) r;
            }
            if (best < 0) {
                o->status = ZMC_REC_NO_RESOURCE;
                continue;
            }
            taken[best] = 1;
            memcpy(o->resource, inc->res[best].id, strlen(inc->res[best].id) + 1);
        }
    }
    return ZXV_OK;
}

static int32_t approve_args(const zmc_incident *inc, const char *task, const char *resource,
                            const char *commander, int *ti, int *ri, int *ci) {
    *ti = find_task(inc, task);
    *ri = find_res(inc, resource);
    if (*ti < 0 || *ri < 0) {
        zind_set_error("zmc_approve: unknown task or resource");
        return ZMC_E_TASK;
    }
    *ci = find_member(inc->cmd, inc->n_cmd, commander);
    if (*ci < 0) {
        zind_set_error("zmc_approve: %s is not a registered commander",
                       commander ? commander : "-");
        return ZMC_E_COMMANDER;
    }
    return ZXV_OK;
}

static int32_t approve_msg(const zmc_incident *inc, const task_t *t, const resource_t *r,
                           const char *commander, uint8_t *buf, size_t cap, size_t *out_len) {
    zind_wr w;
    size_t need = APP_MAGIC_LEN + 1 + strlen(inc->id) + 1 + strlen(t->id) + 1 + strlen(r->id) +
                  4 + 1 + strlen(commander);
    *out_len = need;
    if (!buf || cap < need) return ZXV_E_BUFFER;
    zind_wr_init(&w, buf, cap);
    zind_wr_bytes(&w, APP_MAGIC, APP_MAGIC_LEN);
    zind_wr_str(&w, inc->id);
    zind_wr_str(&w, t->id);
    zind_wr_str(&w, r->id);
    zind_wr_u32(&w, t->fact >= 0 ? inc->facts[t->fact].epoch : 0);
    zind_wr_str(&w, commander);
    return w.overflow || w.len != need ? ZXV_E_INTERNAL : ZXV_OK;
}

int32_t zmc_approve_message(const zmc_incident *inc, const char *task, const char *resource,
                            const char *commander, uint8_t *buf, size_t cap, size_t *out_len) {
    int ti, ri, ci;
    int32_t st;
    if (!inc || !out_len) return ZXV_E_ARG;
    st = approve_args(inc, task, resource, commander, &ti, &ri, &ci);
    if (st != ZXV_OK) return st;
    return approve_msg(inc, &inc->tasks[ti], &inc->res[ri], commander, buf, cap, out_len);
}

static int32_t approve(zmc_incident *inc, const char *task, const char *resource,
                       const char *commander, const uint8_t *sig, const uint8_t *expect,
                       size_t expect_len) {
    uint8_t msg[APPROVE_MAX + 32 + ZIND_SIG_BYTES];
    size_t len = 0;
    int ti, ri, ci;
    task_t *t;
    resource_t *r;
    int32_t st;
    st = approve_args(inc, task, resource, commander, &ti, &ri, &ci);
    if (st != ZXV_OK) return st;
    t = &inc->tasks[ti];
    r = &inc->res[ri];
    if (t->assigned || r->busy || strcmp(t->kind, r->kind) != 0 ||
        task_gate(inc, t) != ZMC_REC_ASSIGN) {
        zind_set_error("zmc_approve: %s on %s: the conditions do not hold now", r->id, t->id);
        return ZMC_E_BLOCKED;
    }
    st = approve_msg(inc, t, r, commander, msg, APPROVE_MAX, &len);
    if (st != ZXV_OK) return st;
    if (expect && (expect_len != len || memcmp(expect, msg, len) != 0)) {
        zind_set_error("zmc_verify_journal: the approval names another epoch");
        return ZXV_E_VERIFY;
    }
    st = zind_verify(inc->cap, inc->cmd[ci].pk, msg, len, ZMC_APPROVE_CTX, sig);
    if (st == ZXV_E_VERIFY) {
        quarantine_tamper(inc, (uint32_t) ci, "mesh-command/forged", sig, ZIND_SIG_BYTES);
        zind_set_error("zmc_approve: the approval is not signed by %s", commander);
    }
    if (st != ZXV_OK) return st;
    if (inc->journal) {
        memcpy(msg + len, inc->cmd[ci].key_id, 32);
        memcpy(msg + len + 32, sig, ZIND_SIG_BYTES);
        st = zind_chain_append(inc->cap, inc->journal, inc->key, ZMC_J_APPROVAL, msg,
                               len + 32 + ZIND_SIG_BYTES, NULL, NULL);
        if (st != ZXV_OK) return st;
    }
    t->assigned = ri + 1;
    r->busy = 1;
    inc->approvals++;
    return ZXV_OK;
}

int32_t zmc_approve(zmc_incident *inc, const char *task, const char *resource,
                    const char *commander, const uint8_t sig[ZIND_SIG_BYTES]) {
    if (!inc || !sig) return ZXV_E_ARG;
    return approve(inc, task, resource, commander, sig, NULL, 0);
}

int32_t zmc_task_state(const zmc_incident *inc, const char *task, int32_t *out_assigned,
                       char *resource_buf, size_t cap) {
    int ti;
    const task_t *t;
    if (!inc || !out_assigned) return ZXV_E_ARG;
    ti = find_task(inc, task);
    if (ti < 0) return ZMC_E_TASK;
    t = &inc->tasks[ti];
    *out_assigned = t->assigned ? 1 : 0;
    if (resource_buf) {
        const char *id = t->assigned ? inc->res[t->assigned - 1].id : "";
        if (cap <= strlen(id)) return ZXV_E_BUFFER;
        memcpy(resource_buf, id, strlen(id) + 1);
    }
    return ZXV_OK;
}

/* ===== journal ===== */

const zind_chain *zmc_journal(const zmc_incident *inc) { return inc ? inc->journal : NULL; }

/* Signed message + key_id[32] + sig: split, and check the key_id names the
 * trusted commander the message names. */
static int32_t split_signed(const zmc_incident *inc, const zind_record *rec, size_t *msg_len,
                            const char *commander, const uint8_t **sig) {
    int ci;
    if (rec->payload_len < 32 + ZIND_SIG_BYTES + 1) return ZXV_E_FORMAT;
    *msg_len = rec->payload_len - 32 - ZIND_SIG_BYTES;
    *sig = rec->payload + *msg_len + 32;
    ci = find_member(inc->cmd, inc->n_cmd, commander);
    if (ci < 0 || memcmp(rec->payload + *msg_len, inc->cmd[ci].key_id, 32) != 0)
        return ZIND_E_UNTRUSTED;
    return ZXV_OK;
}

static int32_t replay_one(zmc_incident *inc, const zind_record *rec) {
    zind_rd rd;
    char a[256], b[256], c[256], d[256];
    int32_t st;
    size_t ml = 0;
    const uint8_t *sig = NULL;
    switch (rec->kind) {
    case ZMC_J_REPORT:
        st = ingest(inc, rec->payload, rec->payload_len, NULL);
        if (st == ZMC_E_UNIT) return ZIND_E_UNTRUSTED;
        if (st == ZXV_E_FORMAT || st == ZXV_OK) return st;
        return ZIND_E_CHAIN;
    case ZMC_J_RESOURCE:
        zind_rd_init(&rd, rec->payload, rec->payload_len);
        zind_rd_str(&rd, a, sizeof a);
        zind_rd_str(&rd, b, sizeof b);
        if (rd.bad || rd.pos != rd.len) return ZXV_E_FORMAT;
        return zmc_resource_add(inc, a, b) == ZXV_OK ? ZXV_OK : ZIND_E_CHAIN;
    case ZMC_J_TASK: {
        uint8_t pri, req;
        zind_rd_init(&rd, rec->payload, rec->payload_len);
        zind_rd_str(&rd, a, sizeof a);
        pri = zind_rd_u8(&rd);
        zind_rd_str(&rd, b, sizeof b);
        zind_rd_str(&rd, c, sizeof c);
        req = zind_rd_u8(&rd);
        if (rd.bad || rd.pos != rd.len) return ZXV_E_FORMAT;
        if (c[0] == 0 && req != 0) return ZXV_E_FORMAT;
        return zmc_task_add(inc, a, pri, b, c, req) == ZXV_OK ? ZXV_OK : ZIND_E_CHAIN;
    }
    case ZMC_J_DECISION: {
        uint8_t dec;
        if (rec->payload_len < 32 + ZIND_SIG_BYTES + DEC_MAGIC_LEN) return ZXV_E_FORMAT;
        zind_rd_init(&rd, rec->payload, rec->payload_len - 32 - ZIND_SIG_BYTES);
        if (memcmp(rec->payload, DEC_MAGIC, DEC_MAGIC_LEN) != 0) return ZXV_E_FORMAT;
        rd.pos = DEC_MAGIC_LEN;
        zind_rd_str(&rd, a, sizeof a); /* incident */
        zind_rd_str(&rd, b, sizeof b); /* fact     */
        (void) zind_rd_u32(&rd);
        rd.pos += 32;
        if (rd.pos > rd.len) return ZXV_E_FORMAT;
        dec = zind_rd_u8(&rd);
        zind_rd_str(&rd, c, sizeof c); /* commander */
        zind_rd_str(&rd, d, sizeof d); /* why       */
        if (rd.bad || rd.pos != rd.len) return ZXV_E_FORMAT;
        if (strcmp(a, inc->id) != 0) return ZIND_E_CHAIN;
        st = split_signed(inc, rec, &ml, c, &sig);
        if (st != ZXV_OK) return st;
        st = resolve(inc, b, dec, c, d, sig, rec->payload, ml);
        return st == ZXV_OK ? ZXV_OK : ZIND_E_CHAIN;
    }
    case ZMC_J_APPROVAL:
        if (rec->payload_len < 32 + ZIND_SIG_BYTES + APP_MAGIC_LEN) return ZXV_E_FORMAT;
        zind_rd_init(&rd, rec->payload, rec->payload_len - 32 - ZIND_SIG_BYTES);
        if (memcmp(rec->payload, APP_MAGIC, APP_MAGIC_LEN) != 0) return ZXV_E_FORMAT;
        rd.pos = APP_MAGIC_LEN;
        zind_rd_str(&rd, a, sizeof a); /* incident */
        zind_rd_str(&rd, b, sizeof b); /* task     */
        zind_rd_str(&rd, c, sizeof c); /* resource */
        (void) zind_rd_u32(&rd);
        zind_rd_str(&rd, d, sizeof d); /* commander */
        if (rd.bad || rd.pos != rd.len) return ZXV_E_FORMAT;
        if (strcmp(a, inc->id) != 0) return ZIND_E_CHAIN;
        st = split_signed(inc, rec, &ml, d, &sig);
        if (st != ZXV_OK) return st;
        st = approve(inc, b, c, d, sig, rec->payload, ml);
        return st == ZXV_OK ? ZXV_OK : ZIND_E_CHAIN;
    default:
        return ZXV_E_FORMAT;
    }
}

int32_t zmc_verify_journal(const zxv_cap *cap, const uint8_t *buf, size_t len,
                           const zmc_trust *trust, zmc_verdict *out) {
    zind_chain *ch = NULL;
    zmc_incident *inc = NULL;
    zind_record rec;
    zind_rd rd;
    char id[256];
    uint32_t n = 0, bad = UINT32_MAX;
    const uint8_t *op[1];
    int32_t st;
    if (!cap || !buf || !trust || !trust->operator_pk || !out) return ZXV_E_ARG;
    if ((trust->n_units && (!trust->unit_ids || !trust->unit_pks)) ||
        (trust->n_commanders && (!trust->commander_ids || !trust->commander_pks)))
        return ZXV_E_ARG;
    memset(out, 0, sizeof *out);
    out->bad_index = UINT32_MAX;
    st = zxv_cap_check(cap, ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) return st;
    st = zind_chain_import(ZMC_JOURNAL, buf, len, &ch);
    if (st != ZXV_OK) return st;
    op[0] = trust->operator_pk;
    st = zind_chain_verify(cap, ch, op, 1, &bad);
    if (st != ZXV_OK) {
        out->bad_index = bad;
        zind_chain_destroy(ch);
        return st;
    }
    (void) zind_chain_count(ch, &n);
    out->records = n;
    st = n ? zind_chain_at(ch, 0, &rec) : ZXV_E_FORMAT;
    if (st == ZXV_OK) {
        zind_rd_init(&rd, rec.payload, rec.payload_len);
        zind_rd_str(&rd, id, sizeof id);
        if (rec.kind != ZMC_J_INCIDENT || rd.bad || rd.pos != rd.len ||
            !zind_ident_ok(id, ZMC_ID_MAX))
            st = ZXV_E_FORMAT;
    }
    if (st == ZXV_OK) st = incident_new(cap, id, NULL, NULL, 0, &inc);
    for (size_t i = 0; st == ZXV_OK && i < trust->n_units; i++)
        if (zmc_unit_add(inc, trust->unit_ids[i], trust->unit_pks[i]) != ZXV_OK) st = ZXV_E_ARG;
    for (size_t i = 0; st == ZXV_OK && i < trust->n_commanders; i++)
        if (zmc_commander_add(inc, trust->commander_ids[i], trust->commander_pks[i]) != ZXV_OK)
            st = ZXV_E_ARG;
    if (st == ZXV_E_FORMAT) out->bad_index = 0;
    for (uint32_t i = 1; st == ZXV_OK && i < n; i++) {
        st = zind_chain_at(ch, i, &rec);
        if (st == ZXV_OK) st = replay_one(inc, &rec);
        if (st != ZXV_OK) out->bad_index = i;
        else if (rec.kind == ZMC_J_REPORT) out->reports++;
    }
    if (st == ZXV_OK) {
        memcpy(out->incident, inc->id, strlen(inc->id) + 1);
        out->facts = inc->n_facts;
        out->held = inc->held;
        out->decisions = inc->decisions;
        out->approvals = inc->approvals;
    }
    zmc_incident_destroy(inc);
    zind_chain_destroy(ch);
    return st;
}
