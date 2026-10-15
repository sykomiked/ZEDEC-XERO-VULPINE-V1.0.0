/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* appfab.c — the program fabricator's acceptance gate (appfab.h F1-F6,
 * C1-C3). Freestanding: no libc, no allocation. */
#include "appfab.h"

static const char HEX[] = "0123456789abcdef";

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static bool seq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static void why(appfab_t *w, const char *a, const char *b, const char *c)
{
    uint32_t k = 0;
    const char *s[3] = {a, b, c};
    for (uint32_t i = 0; i < 3u; i++)
        for (const char *q = s[i]; q && *q && k + 1u < APPFAB_WHY; q++) w->why[k++] = *q;
    w->why[k] = 0;
}

static lpres_state_t no(appfab_t *w, const char *a, const char *b, const char *c)
{
    why(w, a, b, c);
    return LPRES_STATE_FALSE;
}

/* F1: the part's zxapp content id, from its (checked) SHA-256 digest */
static void pin_of(const ipfsn_cid_t *cid, char out[ZXAPP_CID_MAX])
{
    const char *p = "sha256:";
    uint32_t k = 0;
    while (*p) out[k++] = *p++;
    for (uint32_t i = 0; i < 32u; i++) {
        out[k++] = HEX[cid->digest[i] >> 4];
        out[k++] = HEX[cid->digest[i] & 15u];
    }
    out[k] = 0;
}

static int32_t part_by_pin(const appfab_t *w, const char *pin)
{
    for (uint32_t i = 0; i < w->nparts; i++)
        if (seq(w->pin[i], pin)) return (int32_t) i;
    return -1;
}

/* F4: "cid:<CID>" names a part */
static int32_t part_by_path(const appfab_t *w, const char *path)
{
    if (path[0] != 'c' || path[1] != 'i' || path[2] != 'd' || path[3] != ':') return -1;
    ipfsn_cid_t c;
    if (ipfsn_cid_parse(path + 4, slen(path + 4), &c) != IPFSN_OK) return -1;
    for (uint32_t i = 0; i < w->nparts; i++)
        if (ipfsn_cid_equal(&c, &w->parts[i].cid)) return (int32_t) i;
    return -1;
}

static int32_t read_part(void *ctx, const char *path, char *buf, uint32_t cap)
{
    const appfab_t *w = (const appfab_t *) ctx;
    int32_t i = part_by_path(w, path);
    if (i < 0 || w->parts[i].len > cap) return -1;
    for (uint32_t k = 0; k < w->parts[i].len; k++) buf[k] = (char) w->parts[i].data[k];
    return (int32_t) w->parts[i].len;
}

/* the anchors: 0 and the Fibonacci numbers */
static bool anchor(uint32_t m)
{
    uint64_t a = 0, b = 1;
    while (a < m) {
        uint64_t c = a + b;
        a = b;
        b = c;
    }
    return a == m;
}

/* F7: m == n, or m in lin(n). lin(0) is empty; lin(anchor A) is every
 * anchor below A; lin(F + M) is every anchor <= F, M and lin(M). */
bool appfab_zeck_may_use(uint32_t n, uint32_t m)
{
    if (m == n) return true;
    while (n) {
        uint64_t a = 1, b = 2;
        while (b <= n) {
            uint64_t c = a + b;
            a = b;
            b = c;
        }
        if (m <= a && anchor(m)) return true;
        n -= (uint32_t) a;
        if (m == n) return true;
    }
    return false;
}

static lpres_state_t resolve(appfab_t *w, zxapp_t *m, uint32_t depth, uint32_t layer);

/* F3: part j, its own uses first, then registered */
static lpres_state_t part(appfab_t *w, uint32_t j, uint32_t depth)
{
    if (w->state[j] == 2u) return LPRES_STATE_TRUE;
    if (w->state[j] == 1u) return no(w, "a part uses itself: ", w->pin[j], 0);
    if (depth >= APPFAB_DEPTH) return no(w, "parts nest too deep at ", w->pin[j], 0);
    w->state[j] = 1u;
    zxapp_t *b = &w->part[j];
    if (zxapp_parse(b, (const char *) w->parts[j].data, w->parts[j].len) != ZXAPP_OK)
        return no(w, "a part does not parse: ", b->err, 0);
    lpres_state_t s = resolve(w, b, depth + 1u, w->parts[j].layer);
    if (s != LPRES_STATE_TRUE) return s;
    if (!b->block && zxapp_compose(b, &w->reg) != ZXAPP_OK)
        return no(w, "a part does not compose: ", b->err, 0);
    if (zxapp_registry_add(&w->reg, b) != ZXAPP_OK)
        return no(w, "a part is refused: ", w->reg.err, 0);
    w->state[j] = 2u;
    return LPRES_STATE_TRUE;
}

static lpres_state_t resolve(appfab_t *w, zxapp_t *m, uint32_t depth, uint32_t layer)
{
    for (uint32_t u = 0; u < m->nuse; u++) {
        const zxapp_use_t *U = &m->use[u];
        if (!U->cid[0]) return no(w, "use ", U->id, " is not pinned to a content id");
        int32_t j = part_by_pin(w, U->cid);
        if (j < 0) return no(w, "use ", U->id, " is pinned to a part the recipe does not carry");
        if (w->layer != APPFAB_NO_LAYER) {
            const appfab_part_t *P = &w->parts[j];
            if (!P->layered) return no(w, "use ", U->id, " has no matrix layer");
            if (!appfab_zeck_may_use(layer, P->layer))
                return no(w, "use ", U->id, " is not in a Zeckendorf ancestor layer");
        }
        lpres_state_t s = part(w, (uint32_t) j, depth);
        if (s != LPRES_STATE_TRUE) return s;
    }
    return LPRES_STATE_TRUE;
}

static lpres_state_t fit(appfab_t *w, const uint8_t *manifest, uint32_t len, zxapp_host_t *h)
{
    /* h == NULL: the closure only (F1-F4, F6, F7), nothing loaded */
    for (uint32_t i = 0; i < w->nparts; i++) {
        const appfab_part_t *P = &w->parts[i];
        if ((!P->data && P->len) || P->cid.mh_code != IPFSN_MH_SHA2_256 ||
            P->cid.digest_len != 32u || ipfsn_cid_verify(&P->cid, P->data, P->len) != IPFSN_OK)
            return no(w, "a part does not match its CID", 0, 0);
        pin_of(&P->cid, w->pin[i]);
    }
    zxapp_t *a = &w->app;
    if (zxapp_parse(a, (const char *) manifest, len) != ZXAPP_OK)
        return no(w, "the manifest does not parse: ", a->err, 0);
    if (a->block) return no(w, "the manifest is a block, not an app", 0, 0);
    if (!a->nuse && !a->n && !a->nprov) {
        why(w, "an empty scaffold: nothing to fabricate", 0, 0);
        return LPRES_STATE_NEITHER;
    }
    lpres_state_t s = resolve(w, a, 0, w->layer);
    if (s != LPRES_STATE_TRUE) return s;
    if (zxapp_compose(a, &w->reg) != ZXAPP_OK)
        return no(w, "the app does not compose: ", a->err, 0);
    for (uint32_t i = 0; i < a->nprov; i++) {
        bool found = false;
        for (uint32_t k = 0; k < a->n && !found; k++)
            found = seq(a->logic[k].entry, a->prov[i].entry);
        if (!found) return no(w, "provides ", a->prov[i].entry, " with no logic");
    }
    for (uint32_t i = 0; i < a->n; i++)
        if (!a->logic[i].src && part_by_path(w, a->logic[i].path) < 0)
            return no(w, "logic ", a->logic[i].entry, " is not read from a part (cid:<CID>)");
    if (!h) return LPRES_STATE_TRUE;
    int32_t (*rf)(void *, const char *, char *, uint32_t) = h->read_file;
    void *rc_ctx = h->read_ctx;
    h->read_file = read_part;
    h->read_ctx = w;
    int rc = zxapp_load(a, h);
    h->read_file = rf;
    h->read_ctx = rc_ctx;
    if (rc < 0) {
        zxapp_unload(a, h);
        return no(w, "an entry does not load: ", a->err, 0);
    }
    for (uint32_t i = 0; i < a->n; i++)
        if (!a->logic[i].loaded) {
            zxapp_unload(a, h);
            return no(w, "entry ", a->logic[i].entry, " has no runtime on this host");
        }
    return LPRES_STATE_TRUE;
}

lpres_state_t appfab_fit(appfab_t *w, const uint8_t *manifest, uint32_t len,
                         const appfab_part_t *parts, uint32_t nparts, zxapp_host_t *h)
{
    return appfab_fit_layered(w, manifest, len, APPFAB_NO_LAYER, parts, nparts, h);
}

static lpres_state_t gate(appfab_t *w, const uint8_t *manifest, uint32_t len, uint32_t layer,
                          const appfab_part_t *parts, uint32_t nparts, zxapp_host_t *h, bool load)
{
    if (!w) return LPRES_STATE_FALSE;
    w->layer = layer;
    w->fit = LPRES_STATE_FALSE;
    w->why[0] = 0;
    for (uint32_t i = 0; i < APPFAB_PARTS; i++) w->state[i] = 0;
    zxapp_registry_init(&w->reg);
    w->parts = parts;
    w->nparts = nparts;
    if (!manifest || (load && !h) || (!parts && nparts)) return no(w, "missing argument", 0, 0);
    if (nparts > APPFAB_PARTS) return no(w, "more parts than an app can use", 0, 0);
    lpres_state_t s = fit(w, manifest, len, load ? h : 0);
    /* a closure is not a fit: nothing is loaded, so no function follows */
    if (load) w->fit = s;
    return s;
}

lpres_state_t appfab_fit_layered(appfab_t *w, const uint8_t *manifest, uint32_t len, uint32_t layer,
                                 const appfab_part_t *parts, uint32_t nparts, zxapp_host_t *h)
{
    return gate(w, manifest, len, layer, parts, nparts, h, true);
}

lpres_state_t appfab_closure(appfab_t *w, const uint8_t *manifest, uint32_t len, uint32_t layer,
                             const appfab_part_t *parts, uint32_t nparts)
{
    return gate(w, manifest, len, layer, parts, nparts, 0, false);
}

lpres_state_t appfab_function(appfab_t *w, zxapp_host_t *h, const appfab_check_t *checks,
                              uint32_t n)
{
    if (!w || !h || (!checks && n)) return LPRES_STATE_NEITHER;
    if (w->fit != LPRES_STATE_TRUE) {
        why(w, "not fitted: no function without fit", 0, 0);
        return LPRES_STATE_NEITHER;
    }
    if (!n) {
        why(w, "no checks: unwitnessed", 0, 0);
        return LPRES_STATE_NEITHER;
    }
    lpres_state_t all = LPRES_STATE_TRUE;
    static zxapp_num_t out[ZXAPP_WITNESS_MAX];
    for (uint32_t i = 0; i < n; i++) {
        const appfab_check_t *C = &checks[i];
        uint32_t got = 0;
        int32_t lp = ZXAPP_NEITHER;
        lpres_state_t s;
        int rc = C->entry ? zxapp_call_witnessed(&w->app, h, C->entry, C->args, C->nargs, out,
                                                 ZXAPP_WITNESS_MAX, &got, &lp)
                          : ZXAPP_E_ARG;
        /* zxapp_call_witnessed answers OK only when its witnesses agree */
        bool both = rc == ZXAPP_E_CONFLICT;
        if (C->want_both)
            s = both ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        else if (both)
            s = LPRES_STATE_BOTH;
        else if (rc != ZXAPP_OK || zxapp_witness(out, got, C->want, C->nwant) != ZXAPP_TRUE)
            s = LPRES_STATE_FALSE;
        else
            s = lp == ZXAPP_TRUE ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
        if (s != LPRES_STATE_TRUE && all == LPRES_STATE_TRUE)
            why(w,
                s == LPRES_STATE_BOTH      ? "the witnesses disagree on "
                : s == LPRES_STATE_NEITHER ? "only one language witnesses "
                                           : "check fails: ",
                C->entry ? C->entry : "(no entry)", 0);
        all = lpres_conjoin(all, s);
    }
    return all;
}

lpres_state_t appfab_verdict(lpres_state_t fit, lpres_state_t function)
{
    return lpres_conjoin(fit, function);
}

void appfab_unload(appfab_t *w, zxapp_host_t *h)
{
    if (w && h) zxapp_unload(&w->app, h);
    if (w) w->fit = LPRES_STATE_FALSE;
}
