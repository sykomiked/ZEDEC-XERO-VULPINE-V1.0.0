/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lodestone_model.c — Lodestone, the inspector for developers (see
 * lodestone_model.h). Every answer is checked whole before any of it is
 * kept, and every sample bounded before it enters the ring. */
#include <stdio.h>
#include <string.h>

#include "lodestone_model.h"

static const char *const PROBE_NAME[LODESTONE_PROBES] = {"rpc.discover", "model.info", "net.peers",
                                                         "zxvd.version", "features.get"};

/* a method name: 1..LODESTONE_NAME bytes of a-z, 0-9, '_' and '.' */
static bool method_name_ok(const char *s, int32_t n)
{
    if (n < 1 || n > (int32_t) LODESTONE_NAME) return false;
    for (int32_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
            return false;
    }
    return true;
}

/* exactly len lower-case hex digits */
static bool hex_ok(const char *s, uint32_t len)
{
    uint32_t i = 0;
    for (; s[i]; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return i == len;
}

/* a count zxvd sends as a decimal string: 1..18 digits, no leading zero */
static bool digits_val(const char *s, int64_t *v)
{
    int64_t x = 0;
    uint32_t i = 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || i >= 18) return false;
        if (i == 1 && s[0] == '0') return false;
        x = x * 10 + (s[i] - '0');
    }
    if (!i) return false;
    *v = x;
    return true;
}

/* the length of a method's group: its name up to the first '.' (LR1) */
static uint32_t group_len(const char *name)
{
    uint32_t n = 0;
    while (name[n] && name[n] != '.') n++;
    return n;
}

void lodestone_init(lodestone_t *t, zxm_link_t *link, lodestone_clock_fn clock, void *clock_ctx)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->clock = clock;
    t->clock_ctx = clock_ctx;
}

/* ---- LM1: methods by group ------------------------------------------------------------ */

/* the group number of name, adding the group when there is room; 0 when none */
static int64_t file_group(lodestone_t *t, const char *name)
{
    uint32_t n = group_len(name);
    if (n > LODESTONE_GROUP) return 0;
    for (uint32_t g = 0; g < t->ngroups; g++)
        if (strlen(t->group[g].name) == n && !memcmp(t->group[g].name, name, n))
            return (int64_t) g + 1;
    if (t->ngroups >= LODESTONE_GROUPS) return 0;
    lodestone_group_t *g = &t->group[t->ngroups++];
    memcpy(g->name, name, n);
    g->name[n] = 0;
    g->methods = 0;
    return (int64_t) t->ngroups;
}

int lodestone_read_methods(lodestone_t *t)
{
    zxm_link_t *l = t->link;
    char protocol[LODESTONE_TEXT + 1], name[LODESTONE_NAME + 2];
    zxm_begin(l, "rpc.discover");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ ||
        zxm_get_str(l, r, "protocol", protocol, sizeof protocol) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "methods");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    /* the whole list is checked before any of it is kept */
    for (uint32_t i = 0; i < n; i++) {
        int32_t m = zxm_at(l, arr, i);
        if (m < 0 || l->tok[m].type != W4_J_STR) return ZXM_E_PROTOCOL;
        int32_t k = w4_json_str(l->resp, &l->tok[m], name, sizeof name);
        if (!method_name_ok(name, k)) return ZXM_E_PROTOCOL;
    }
    t->ngroups = 0;
    t->methods_kept = 0;
    t->methods_left_out = 0;
    for (uint32_t i = 0; i < n && i < LODESTONE_METHODS; i++) {
        int32_t k = w4_json_str(l->resp, &l->tok[zxm_at(l, arr, i)], name, sizeof name);
        memcpy(t->method[i], name, (uint32_t) k + 1u);
        t->method_group[i] = file_group(t, name);
        if (t->method_group[i] == 0) t->methods_left_out++;
        t->methods_kept++;
    }
    for (uint32_t g = 0; g < t->ngroups; g++)
        t->group[g].methods =
            lodestone_group_count(t->method_group, t->methods_kept, (int64_t) g + 1);
    memcpy(t->protocol, protocol, sizeof protocol);
    t->methods = n;
    t->methods_known = true;
    snprintf(t->notice, sizeof t->notice, "%s serves %u methods in %u groups%s.",
             protocol[0] ? protocol : "zxvd", n, t->ngroups,
             n > t->methods_kept || t->methods_left_out ? " (some not listed here)" : "");
    return ZXM_OK;
}

int64_t lodestone_group_methods(const lodestone_t *t, const char *group)
{
    if (!t->methods_known || !group) return -1;
    for (uint32_t g = 0; g < t->ngroups; g++)
        if (!strcmp(t->group[g].name, group)) return t->group[g].methods;
    return -1;
}

bool lodestone_has_method(const lodestone_t *t, const char *name)
{
    if (!t->methods_known || !name) return false;
    for (uint32_t i = 0; i < t->methods_kept; i++)
        if (!strcmp(t->method[i], name)) return true;
    return false;
}

/* ---- LM2: the model ------------------------------------------------------------------- */

int lodestone_read_model(lodestone_t *t)
{
    zxm_link_t *l = t->link;
    bool loaded = false, gen = false;
    char name[LODESTONE_TEXT + 1], arch[LODESTONE_TEXT + 1], status[LODESTONE_TEXT + 1];
    int64_t layers = 0, vocab = 0;
    zxm_begin(l, "model.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || zxm_get_bool(l, r, "loaded", &loaded) != ZXM_OK ||
        zxm_get_str(l, r, "name", name, sizeof name) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    /* the others are read when given: a slot with no model has no arch */
    arch[0] = 0;
    status[0] = 0;
    if ((zxm_get(l, r, "can_generate") >= 0 &&
         zxm_get_bool(l, r, "can_generate", &gen) != ZXM_OK) ||
        (zxm_get(l, r, "arch") >= 0 && zxm_get_str(l, r, "arch", arch, sizeof arch) != ZXM_OK) ||
        (zxm_get(l, r, "status") >= 0 &&
         zxm_get_str(l, r, "status", status, sizeof status) != ZXM_OK) ||
        (zxm_get(l, r, "n_layers") >= 0 && zxm_get_i64(l, r, "n_layers", &layers) != ZXM_OK) ||
        (zxm_get(l, r, "n_vocab") >= 0 && zxm_get_i64(l, r, "n_vocab", &vocab) != ZXM_OK) ||
        layers < 0 || vocab < 0)
        return ZXM_E_PROTOCOL;
    t->model_known = true;
    t->loaded = loaded;
    t->can_generate = loaded && gen;
    memcpy(t->model, name, sizeof name);
    memcpy(t->arch, arch, sizeof arch);
    memcpy(t->status, status, sizeof status);
    t->layers = layers;
    t->vocab = vocab;
    if (loaded)
        snprintf(t->notice, sizeof t->notice, "The slot holds %s (%s, %lld layers, %lld words)%s.",
                 name[0] ? name : "a model", arch[0] ? arch : "unknown arch", (long long) layers,
                 (long long) vocab, t->can_generate ? "" : ", not able to generate");
    else
        snprintf(t->notice, sizeof t->notice, "No model is loaded in the slot%s%s.",
                 status[0] ? ": " : "", status);
    return ZXM_OK;
}

/* ---- LM3: peers ----------------------------------------------------------------------- */

/* one contact of net.peers, read and checked (LR4); false when it is none */
static bool read_contact(const zxm_link_t *l, int32_t o, uint32_t i, lodestone_contact_t *c)
{
    char seen[24];
    memset(c, 0, sizeof *c);
    if (o < 0 || l->tok[o].type != W4_J_OBJ ||
        zxm_get_str(l, o, "id", c->id, sizeof c->id) != ZXM_OK || !hex_ok(c->id, LODESTONE_NODE) ||
        zxm_get_str(l, o, "addr", c->addr, sizeof c->addr) != ZXM_OK ||
        zxm_get_i64(l, o, "bucket", &c->bucket) != ZXM_OK ||
        zxm_get_bool(l, o, "ubh", &c->ubh) != ZXM_OK ||
        zxm_get_str(l, o, "last_seen_ms", seen, sizeof seen) != ZXM_OK ||
        !digits_val(seen, &c->seen) || zxm_get_bool(l, o, "in_book", &c->in_book) != ZXM_OK ||
        zxm_get_bool(l, o, "member", &c->member) != ZXM_OK ||
        zxm_get_i64(l, o, "pending", &c->pending) != ZXM_OK)
        return false;
    if (c->bucket < 0 || c->pending < 0) return false;
    return lodestone_contact_record_fits((int64_t) i, c->bucket, c->ubh, c->seen, c->in_book,
                                         c->member, c->pending);
}

int lodestone_read_peers(lodestone_t *t)
{
    zxm_link_t *l = t->link;
    lodestone_contact_t c;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), members = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!read_contact(l, zxm_at(l, arr, i), i, &c)) return ZXM_E_PROTOCOL;
        members += c.member;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < n && i < LODESTONE_CONTACTS; i++)
        if (read_contact(l, zxm_at(l, arr, i), i, &t->contact[i])) kept++;
    t->contacts = n;
    t->contacts_kept = kept;
    t->members = members;
    t->peers_known = true;
    snprintf(t->notice, sizeof t->notice, "%u contact%s in the routing table, %u admitted%s.", n,
             n == 1 ? "" : "s", members, n ? "" : " (none while the network is off)");
    return ZXM_OK;
}

bool lodestone_contact_ok(const lodestone_t *t, uint32_t i)
{
    if (i >= t->contacts_kept) return false;
    const lodestone_contact_t *c = &t->contact[i];
    return lodestone_contact_record_fits((int64_t) i, c->bucket, c->ubh, c->seen, c->in_book,
                                         c->member, c->pending);
}

/* ---- LM4: capabilities ---------------------------------------------------------------- */

int lodestone_read_version(lodestone_t *t)
{
    zxm_link_t *l = t->link;
    char app[LODESTONE_TEXT + 1], wire[LODESTONE_TEXT + 1], lib[LODESTONE_TEXT + 1];
    int64_t mask = 0;
    zxm_begin(l, "zxvd.version");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ ||
        zxm_get_str(l, r, "app", app, sizeof app) != ZXM_OK ||
        zxm_get_str(l, r, "protocol", wire, sizeof wire) != ZXM_OK ||
        zxm_get_str(l, r, "libzxv", lib, sizeof lib) != ZXM_OK ||
        zxm_get_i64(l, r, "libzxv_features", &mask) != ZXM_OK || mask < 0)
        return ZXM_E_PROTOCOL;
    memcpy(t->app, app, sizeof app);
    memcpy(t->wire, wire, sizeof wire);
    memcpy(t->libzxv, lib, sizeof lib);
    t->lib_features = mask;
    t->version_known = true;
    snprintf(t->notice, sizeof t->notice, "zxvd %s speaks %s on libzxv %s (features 0x%llx).", app,
             wire, lib, (unsigned long long) mask);
    return ZXM_OK;
}

/* one feature switch, read and checked; false when it is none */
static bool read_feature(const zxm_link_t *l, int32_t o, lodestone_feature_t *f)
{
    memset(f, 0, sizeof *f);
    if (o < 0 || l->tok[o].type != W4_J_OBJ ||
        zxm_get_str(l, o, "name", f->name, sizeof f->name) != ZXM_OK || !f->name[0] ||
        zxm_get_bool(l, o, "enabled", &f->enabled) != ZXM_OK ||
        zxm_get_bool(l, o, "regulated", &f->regulated) != ZXM_OK)
        return false;
    int32_t gs = zxm_get(l, o, "groups");
    if (gs < 0 || l->tok[gs].type != W4_J_ARR) return false;
    uint32_t n = zxm_len(l, gs);
    for (uint32_t i = 0; i < n; i++) {
        int32_t g = zxm_at(l, gs, i);
        int64_t m = 0;
        if (g < 0 || l->tok[g].type != W4_J_OBJ || zxm_get_i64(l, g, "methods", &m) != ZXM_OK ||
            m < 1 || m > (int64_t) UINT32_MAX)
            return false;
        f->methods += m; /* at most 2^32 groups of 2^32 methods: inside int64 */
    }
    return true;
}

int lodestone_read_features(lodestone_t *t)
{
    zxm_link_t *l = t->link;
    lodestone_feature_t f;
    zxm_begin(l, "features.get");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "features");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), on = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!read_feature(l, zxm_at(l, arr, i), &f)) return ZXM_E_PROTOCOL;
        on += f.enabled;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < n && i < LODESTONE_FEATURES; i++)
        if (read_feature(l, zxm_at(l, arr, i), &t->feature[i])) kept++;
    t->features = n;
    t->features_kept = kept;
    t->features_on = on;
    t->features_known = true;
    snprintf(t->notice, sizeof t->notice, "%u feature switch%s, %u on; Lodestone switches none.", n,
             n == 1 ? "" : "es", on);
    return ZXM_OK;
}

/* ---- LM5: probes ---------------------------------------------------------------------- */

static int read_one(lodestone_t *t, int64_t probe)
{
    switch (probe) {
    case LODESTONE_PROBE_METHODS:
        return lodestone_read_methods(t);
    case LODESTONE_PROBE_MODEL:
        return lodestone_read_model(t);
    case LODESTONE_PROBE_PEERS:
        return lodestone_read_peers(t);
    case LODESTONE_PROBE_VERSION:
        return lodestone_read_version(t);
    default:
        return lodestone_read_features(t);
    }
}

int lodestone_probe(lodestone_t *t, int64_t probe)
{
    if (probe < 0 || probe >= LODESTONE_PROBES) return ZXM_E_ARG;
    if (!t->clock) return ZXM_E_STATE;
    int64_t start = t->clock(t->clock_ctx);
    int rc = read_one(t, probe);
    int64_t end = t->clock(t->clock_ctx);
    if (rc != ZXM_OK && rc != ZXM_E_REMOTE) return rc; /* no answer: no sample */
    int64_t ms = end >= start ? end - start : 0;       /* a clock that went back: 0 ms */
    const char *what = PROBE_NAME[probe];
    if (!retort_latency_ok(ms)) {
        t->too_slow++;
        snprintf(t->notice, sizeof t->notice, "%s took longer than %lld ms: not kept.", what,
                 (long long) LODESTONE_LATENCY_MAX);
        return rc;
    }
    if (t->head - t->tail >= LODESTONE_SAMPLES) { /* full: the oldest goes */
        t->tail++;
        t->dropped++;
    }
    lodestone_sample_t *s = &t->sample[t->head % LODESTONE_SAMPLES];
    s->probe = probe;
    s->ms = ms;
    t->head++;
    if (rc == ZXM_OK)
        snprintf(t->notice, sizeof t->notice, "%s answered in %lld ms.", what, (long long) ms);
    else
        snprintf(t->notice, sizeof t->notice, "%s refused in %lld ms: %s", what, (long long) ms,
                 t->link->err);
    return rc;
}

/* ---- LM6: latency --------------------------------------------------------------------- */

int lodestone_latency(const lodestone_t *t, int64_t probe, lodestone_latency_t *out)
{
    int64_t ms[LODESTONE_SAMPLES];
    uint32_t n = 0;
    if (!out || probe < -1 || probe >= LODESTONE_PROBES) return ZXM_E_ARG;
    for (uint64_t i = t->tail; i < t->head; i++) {
        const lodestone_sample_t *s = &t->sample[i % LODESTONE_SAMPLES];
        if (probe == -1 || s->probe == probe) ms[n++] = s->ms;
    }
    if (n == 0) return ZXM_E_STATE;
    lodestone_latency_t r;
    r.samples = (int64_t) n;
    if (!lodestone_latency_mean(ms, n, &r.mean_num, &r.mean_den) ||
        !lodestone_latency_span(ms, n, &r.fastest, &r.slowest))
        return ZXM_E_STATE; /* a sample is checked before it is kept, so never */
    *out = r;
    return ZXM_OK;
}
