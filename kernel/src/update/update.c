/* update.c — decentralized opt-in updates. See update.h. */
#include "update.h"
#include "../robin_debanks/sha256.h"

static void scpy(char *d, const char *s, uint32_t cap) {
    uint32_t i = 0; if (s) while (s[i] && i + 1u < cap) { d[i] = s[i]; i++; } d[i] = 0;
}
static bool seq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) { a++; b++; } return *a == 0 && *b == 0;
}
static void bcpy(uint8_t *d, const uint8_t *s, uint32_t n) { for (uint32_t i=0;i<n;i++) d[i]=s[i]; }
static bool beq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

void upd_init(upd_catalog_t *c) {
    if (!c) return;
    for (uint32_t i = 0; i < sizeof(*c); i++) ((uint8_t *)c)[i] = 0;
}
void upd_set_transport(upd_catalog_t *c, const upd_transport_t *t) {
    if (!c) return;
    if (t)
        c->transport = *t;
    else {
        c->transport.fetch = 0;
        c->transport.ctx = 0;
    }
}
void upd_set_verifier(upd_catalog_t *c, upd_verify_fn fn) { if (c) c->verify = fn; }

bool upd_trust_author(upd_catalog_t *c, const uint8_t pubkey[UPD_KEY_LEN]) {
    if (!c || !pubkey) return false;
    if (upd_is_trusted(c, pubkey)) return true;
    if (c->n_trusted >= UPD_MAX_TRUSTED) return false;
    bcpy(c->trusted[c->n_trusted++], pubkey, UPD_KEY_LEN);
    return true;
}
bool upd_is_trusted(const upd_catalog_t *c, const uint8_t pubkey[UPD_KEY_LEN]) {
    if (!c || !pubkey) return false;
    for (uint32_t i = 0; i < c->n_trusted; i++)
        if (beq(c->trusted[i], pubkey, UPD_KEY_LEN)) return true;
    return false;
}

int32_t upd_find(const upd_catalog_t *c, const char *id) {
    if (!c || !id) return -1;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->upd[i].in_use && seq(c->upd[i].id, id)) return (int32_t)i;
    return -1;
}

upd_result_t upd_publish(upd_catalog_t *c, const char *id, const char *name,
                         uint32_t version, const uint8_t cid[UPD_CID_LEN],
                         const uint8_t author[UPD_KEY_LEN], const uint8_t sig[UPD_SIG_LEN],
                         uint32_t size, const char deps[][UPD_ID_LEN], uint32_t n_deps) {
    if (!c || !id || !cid || !author || !sig) return UPD_ERR_NOT_FOUND;
    if (n_deps > UPD_MAX_DEPS) return UPD_ERR_FULL;
    int32_t ex = upd_find(c, id);
    upd_entry_t *e;
    if (ex >= 0) e = &c->upd[ex];               /* re-publish updates in place */
    else {
        if (c->n >= UPD_MAX) return UPD_ERR_FULL;
        e = &c->upd[c->n++];
        for (uint32_t i = 0; i < sizeof(*e); i++) ((uint8_t*)e)[i] = 0;
    }
    e->in_use = true;
    scpy(e->id, id, UPD_ID_LEN);
    scpy(e->name, name, UPD_NAME_LEN);
    e->version = version;
    bcpy(e->cid, cid, UPD_CID_LEN);
    bcpy(e->author, author, UPD_KEY_LEN);
    bcpy(e->sig, sig, UPD_SIG_LEN);
    e->size = size;
    e->n_deps = n_deps;
    for (uint32_t i = 0; i < n_deps; i++) scpy(e->dep[i], deps[i], UPD_ID_LEN);
    e->state = UPD_AVAILABLE;
    return UPD_OK;
}

upd_result_t upd_bundle_define(upd_catalog_t *c, const char *id, const char *name,
                               const char members[][UPD_ID_LEN], uint32_t n) {
    if (!c || !id || n > UPD_MAX_BUNDLE) return UPD_ERR_FULL;
    if (c->n_bundles >= UPD_MAX_BUNDLES) return UPD_ERR_FULL;
    upd_bundle_t *b = &c->bundle[c->n_bundles++];
    b->in_use = true;
    scpy(b->id, id, UPD_ID_LEN);
    scpy(b->name, name, UPD_NAME_LEN);
    b->n_members = n;
    for (uint32_t i = 0; i < n; i++) scpy(b->member[i], members[i], UPD_ID_LEN);
    return UPD_OK;
}

bool upd_select(upd_catalog_t *c, const char *id) {
    int32_t i = upd_find(c, id); if (i < 0) return false;
    c->upd[i].selected = true;
    if (c->upd[i].state == UPD_AVAILABLE) c->upd[i].state = UPD_SELECTED;
    return true;
}
bool upd_deselect(upd_catalog_t *c, const char *id) {
    int32_t i = upd_find(c, id); if (i < 0) return false;
    c->upd[i].selected = false;
    if (c->upd[i].state == UPD_SELECTED) c->upd[i].state = UPD_AVAILABLE;
    return true;
}
bool upd_select_bundle(upd_catalog_t *c, const char *bundle_id) {
    if (!c || !bundle_id) return false;
    for (uint32_t b = 0; b < c->n_bundles; b++)
        if (c->bundle[b].in_use && seq(c->bundle[b].id, bundle_id)) {
            bool all = true;
            for (uint32_t m = 0; m < c->bundle[b].n_members; m++)
                if (!upd_select(c, c->bundle[b].member[m])) all = false;
            return all;   /* false if a member is not in the catalog */
        }
    return false;
}
bool upd_is_selected(const upd_catalog_t *c, const char *id) {
    int32_t i = upd_find(c, id); return i >= 0 && c->upd[i].selected;
}

/* ---- dependency resolution: topological, deps before dependents ---- */
upd_result_t upd_resolve(const upd_catalog_t *c, int32_t *plan, uint32_t cap, uint32_t *n) {
    if (!c || !plan || !n) return UPD_ERR_NOT_FOUND;
    *n = 0;
    uint8_t mark[UPD_MAX];   /* 0=unseen 1=in-progress 2=done */
    for (uint32_t i = 0; i < UPD_MAX; i++) mark[i] = 0;

    /* iterative DFS with an explicit stack, so a cycle is detected and no
     * recursion is needed in the kernel */
    for (uint32_t root = 0; root < c->n; root++) {
        if (!c->upd[root].in_use || !c->upd[root].selected) continue;
        if (c->upd[root].installed) { mark[root] = 2; continue; }

        int32_t stack[UPD_MAX]; uint32_t sp = 0;
        stack[sp++] = (int32_t)root;
        while (sp > 0) {
            int32_t cur = stack[sp - 1];
            if (mark[cur] == 2) { sp--; continue; }
            if (c->upd[cur].installed) { mark[cur] = 2; sp--; continue; }
            mark[cur] = 1;                          /* in progress */
            /* find an unfinished dependency to visit first */
            int32_t next = -1;
            for (uint32_t d = 0; d < c->upd[cur].n_deps; d++) {
                int32_t di = upd_find(c, c->upd[cur].dep[d]);
                if (di < 0) return UPD_ERR_MISSING_DEP;   /* required, not present */
                if (c->upd[di].installed) continue;
                if (mark[di] == 1) return UPD_ERR_CYCLE;  /* back edge = cycle */
                if (mark[di] != 2) { next = di; break; }
            }
            if (next >= 0) { stack[sp++] = next; continue; }
            /* all deps done -> emit cur */
            mark[cur] = 2; sp--;
            if (*n >= cap) return UPD_ERR_FULL;
            /* avoid duplicates (a shared dep reached twice) */
            bool dup = false; for (uint32_t k = 0; k < *n; k++) if (plan[k] == cur) dup = true;
            if (!dup) plan[(*n)++] = cur;
        }
    }
    return UPD_OK;
}

upd_result_t upd_fetch_verify(upd_catalog_t *c, const char *id,
                              uint8_t *buf, uint32_t cap, uint32_t *out_len) {
    int32_t i = upd_find(c, id);
    if (i < 0) return UPD_ERR_NOT_FOUND;
    upd_entry_t *e = &c->upd[i];
    if (!c->transport.fetch) return UPD_ERR_NO_TRANSPORT;

    uint32_t n = 0;
    if (c->transport.fetch(e->cid, buf, cap, &n, c->transport.ctx) != 0) {
        e->state = UPD_REJECTED; return UPD_ERR_FETCH;
    }
    /* self-certifying: the bytes must hash to the address we asked for */
    uint8_t h[UPD_CID_LEN];
    sha256(n ? buf : (const uint8_t*)"", n, h);
    if (!beq(h, e->cid, UPD_CID_LEN)) { e->state = UPD_REJECTED; return UPD_ERR_CID_MISMATCH; }

    /* trusted authorship: the user must trust the publisher... */
    if (!upd_is_trusted(c, e->author)) { e->state = UPD_REJECTED; return UPD_ERR_UNTRUSTED; }
    /* ...and the publisher's signature over the CID must verify */
    if (!c->verify || !c->verify(e->cid, UPD_CID_LEN, e->sig, e->author)) {
        e->state = UPD_REJECTED; return UPD_ERR_BAD_SIG;
    }

    if (out_len) *out_len = n;
    e->state = UPD_VERIFIED;
    return UPD_OK;
}

bool upd_mark_installed(upd_catalog_t *c, const char *id) {
    int32_t i = upd_find(c, id);
    if (i < 0 || c->upd[i].state != UPD_VERIFIED) return false;
    c->upd[i].installed = true;
    c->upd[i].state = UPD_INSTALLED;
    return true;
}

const char *upd_strerror(upd_result_t r) {
    switch (r) {
    case UPD_OK: return "ok";
    case UPD_ERR_FULL: return "catalog full";
    case UPD_ERR_NOT_FOUND: return "not found";
    case UPD_ERR_NO_TRANSPORT: return "no P2P transport bound";
    case UPD_ERR_FETCH: return "fetch failed";
    case UPD_ERR_CID_MISMATCH: return "content does not match its address";
    case UPD_ERR_UNTRUSTED: return "publisher not trusted";
    case UPD_ERR_BAD_SIG: return "signature invalid";
    case UPD_ERR_MISSING_DEP: return "a required update is missing";
    case UPD_ERR_CYCLE: return "dependency cycle";
    }
    return "unknown";
}
