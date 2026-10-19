/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_settings.c — the settings store (zui_settings.h S1..S10). */
#include "zui_settings.h"

/* ---- small string helpers (no libc) ------------------------------------ */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

/* a (len n) equals the NUL-terminated b */
static bool neq(const char *a, uint32_t n, const char *b)
{
    uint32_t i = 0;
    for (; i < n; i++)
        if (!b[i] || b[i] != a[i]) return false;
    return b[n] == 0;
}

static void zero(void *p, uint32_t n)
{
    uint8_t *z = p;
    for (uint32_t i = 0; i < n; i++) z[i] = 0;
}

static void scopy(char *d, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    for (; s && s[i] && i + 1u < cap; i++) d[i] = s[i];
    d[i] = 0;
}

static bool key_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

static bool name_char(char c)
{
    return key_char(c) || (c >= 'A' && c <= 'Z');
}

static bool good_key(const char *k)
{
    uint32_t n = slen(k);
    if (n == 0 || n >= ZUI_SET_KEY_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!key_char(k[i])) return false;
    return true;
}

static uint32_t fnv(const char *s, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) {
        h ^= (uint8_t) s[i];
        h *= 16777619u;
    }
    return h;
}

#define NINDEX (ZUI_SET_KEYS * 2u)

/* the key named by s[0..n), or -1 */
static int find_key(const zui_settings_t *st, const char *s, uint32_t n)
{
    if (n == 0 || n >= ZUI_SET_KEY_MAX) return -1;
    uint32_t h = fnv(s, n) % NINDEX;
    for (uint32_t probe = 0; probe < NINDEX; probe++) {
        uint16_t k = st->index[(h + probe) % NINDEX];
        if (!k) return -1;
        if (neq(s, n, st->def[k - 1u].key)) return (int) (k - 1u);
    }
    return -1;
}

void zui_set_init(zui_settings_t *st)
{
    zero(st, (uint32_t) sizeof *st);
}

/* ---- values ------------------------------------------------------------ */

static bool val_eq(uint8_t type, const zui_set_value_t *a, const zui_set_value_t *b)
{
    if (type == ZUI_SET_STR) return seq(a->s, b->s);
    return a->i == b->i;
}

/* S2: is v a value of d, and the normal form of it in *out */
static int check(const zui_set_def_t *d, const zui_set_value_t *v, zui_set_value_t *out)
{
    zui_set_value_t n;
    zero(&n, (uint32_t) sizeof n);
    switch (d->type) {
    case ZUI_SET_INT: {
        if (v->i < d->min || v->i > d->max) return ZUI_SET_E_RANGE;
        int64_t off = (int64_t) v->i - (int64_t) d->min;
        if (off % d->step) return ZUI_SET_E_RANGE;
        n.i = v->i;
        break;
    }
    case ZUI_SET_BOOL:
        if (v->i != 0 && v->i != 1) return ZUI_SET_E_RANGE;
        n.i = v->i;
        break;
    case ZUI_SET_ENUM:
        if (v->i < 0 || v->i >= (int32_t) d->nnames) return ZUI_SET_E_RANGE;
        n.i = v->i;
        break;
    case ZUI_SET_COLOR:
        if (v->i < 0 || v->i >= (int32_t) ZUI_C_COUNT) return ZUI_SET_E_RANGE;
        n.i = v->i;
        break;
    case ZUI_SET_STR: {
        uint32_t i = 0;
        for (; i < ZUI_SET_STR_MAX && v->s[i]; i++) {
            uint8_t c = (uint8_t) v->s[i];
            if (c < 0x20u || c == 0x7Fu) return ZUI_SET_E_RANGE;
            n.s[i] = v->s[i];
        }
        if (i == ZUI_SET_STR_MAX) return ZUI_SET_E_RANGE; /* too long */
        break;
    }
    default:
        return ZUI_SET_E_TYPE;
    }
    if (out) *out = n;
    return ZUI_SET_OK;
}

/* ---- the schema -------------------------------------------------------- */

static int define(zui_settings_t *st, const char *key, zui_set_def_t *d)
{
    if (!good_key(key) || seq(key, "lock")) return ZUI_SET_E_NAME; /* S6: "lock" leads a line */
    uint32_t n = slen(key);
    if (find_key(st, key, n) >= 0) return ZUI_SET_E_DUP;
    if (st->nkeys >= ZUI_SET_KEYS) return ZUI_SET_E_FULL;
    zui_set_value_t def;
    int r = check(d, &d->def, &def);
    if (r) return r;
    d->def = def;
    scopy(d->key, ZUI_SET_KEY_MAX, key);
    uint32_t k = st->nkeys++;
    st->def[k] = *d;
    uint32_t h = fnv(key, n) % NINDEX;
    while (st->index[h]) h = (h + 1u) % NINDEX;
    st->index[h] = (uint16_t) (k + 1u);
    return (int) k;
}

static void blank(zui_set_def_t *d, uint8_t type)
{
    zero(d, (uint32_t) sizeof *d);
    d->type = type;
}

int zui_set_def_int(zui_settings_t *st, const char *key, int32_t def, int32_t min, int32_t max,
                    int32_t step)
{
    if (min > max || step <= 0) return ZUI_SET_E_RANGE;
    zui_set_def_t d;
    blank(&d, ZUI_SET_INT);
    d.min = min, d.max = max, d.step = step;
    d.def.i = def;
    return define(st, key, &d);
}

int zui_set_def_bool(zui_settings_t *st, const char *key, bool def)
{
    zui_set_def_t d;
    blank(&d, ZUI_SET_BOOL);
    d.def.i = def ? 1 : 0;
    return define(st, key, &d);
}

int zui_set_def_enum(zui_settings_t *st, const char *key, const char *const *names, uint32_t n,
                     uint32_t def)
{
    if (!names || n == 0 || n > ZUI_SET_ENUM_MAX) return ZUI_SET_E_RANGE;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t l = slen(names[i]);
        if (l == 0 || l >= ZUI_SET_STR_MAX) return ZUI_SET_E_NAME;
        for (uint32_t j = 0; j < l; j++)
            if ((uint8_t) names[i][j] < 0x21u || names[i][j] == '"') return ZUI_SET_E_NAME;
        for (uint32_t j = 0; j < i; j++)
            if (seq(names[i], names[j])) return ZUI_SET_E_DUP;
    }
    zui_set_def_t d;
    blank(&d, ZUI_SET_ENUM);
    d.names = names;
    d.nnames = (uint8_t) n;
    d.def.i = def > n ? (int32_t) n : (int32_t) def;
    return define(st, key, &d);
}

int zui_set_def_str(zui_settings_t *st, const char *key, const char *def)
{
    zui_set_def_t d;
    blank(&d, ZUI_SET_STR);
    uint32_t l = slen(def);
    if (l >= ZUI_SET_STR_MAX) return ZUI_SET_E_RANGE;
    scopy(d.def.s, ZUI_SET_STR_MAX, def);
    return define(st, key, &d);
}

int zui_set_def_color(zui_settings_t *st, const char *key, zui_color_role_t def)
{
    zui_set_def_t d;
    blank(&d, ZUI_SET_COLOR);
    d.def.i = (int32_t) def;
    return define(st, key, &d);
}

int zui_set_key(const zui_settings_t *st, const char *key)
{
    int k = key ? find_key(st, key, slen(key)) : -1;
    return k < 0 ? ZUI_SET_E_NOKEY : k;
}

const zui_set_def_t *zui_set_schema(const zui_settings_t *st, uint32_t key)
{
    return key < st->nkeys ? &st->def[key] : 0;
}

/* ---- seats, users, apps ------------------------------------------------ */

static uint8_t name_id(char (*tab)[ZUI_SET_NAME_MAX], uint32_t cap, const char *name)
{
    uint32_t n = slen(name);
    if (n == 0 || n >= ZUI_SET_NAME_MAX) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (!name_char(name[i])) return 0;
    for (uint32_t i = 0; i < cap; i++)
        if (tab[i][0] && seq(tab[i], name)) return (uint8_t) (i + 1u);
    for (uint32_t i = 0; i < cap; i++)
        if (!tab[i][0]) {
            scopy(tab[i], ZUI_SET_NAME_MAX, name);
            return (uint8_t) (i + 1u);
        }
    return 0;
}

uint8_t zui_set_seat(zui_settings_t *st, const char *name)
{
    return name_id(st->seat, ZUI_SET_SEATS, name);
}
uint8_t zui_set_user(zui_settings_t *st, const char *name)
{
    return name_id(st->user, ZUI_SET_USERS, name);
}
uint8_t zui_set_app(zui_settings_t *st, const char *name)
{
    return name_id(st->app, ZUI_SET_APPS, name);
}

static bool known(const zui_settings_t *st, zui_set_view_t v)
{
    if (v.seat && (v.seat > ZUI_SET_SEATS || !st->seat[v.seat - 1u][0])) return false;
    if (v.user && (v.user > ZUI_SET_USERS || !st->user[v.user - 1u][0])) return false;
    if (v.app && (v.app > ZUI_SET_APPS || !st->app[v.app - 1u][0])) return false;
    return true;
}

/* S1: the scope a layer keeps, from a view; false when the layer does not
 * apply to it */
static bool scope_of(zui_set_layer_t layer, zui_set_view_t v, zui_set_view_t *sc)
{
    zui_set_view_t s = {0, 0, 0};
    switch (layer) {
    case ZUI_LAYER_SYSTEM:
    case ZUI_LAYER_DEVICE:
        break;
    case ZUI_LAYER_SEAT:
        if (!v.seat) return false;
        s.seat = v.seat;
        break;
    case ZUI_LAYER_USER:
        if (!v.user) return false;
        s.user = v.user;
        break;
    case ZUI_LAYER_APP:
        if (!v.user || !v.app) return false;
        s.user = v.user;
        s.app = v.app;
        break;
    default:
        return false;
    }
    *sc = s;
    return true;
}

/* a scope given by the caller for a layer: exactly the fields it uses */
static bool good_scope(const zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t sc)
{
    zui_set_view_t n;
    if (!known(st, sc) || !scope_of(layer, sc, &n)) return false;
    return n.seat == sc.seat && n.user == sc.user && n.app == sc.app;
}

static bool same(zui_set_view_t a, zui_set_view_t b)
{
    return a.seat == b.seat && a.user == b.user && a.app == b.app;
}

static zui_set_entry_t *entry(zui_settings_t *st, uint32_t layer, zui_set_view_t sc, uint32_t key)
{
    for (uint32_t i = 0; i < ZUI_SET_ENTRIES; i++) {
        zui_set_entry_t *e = &st->e[i];
        if (e->used && e->key == key && e->layer == layer && same(e->scope, sc)) return e;
    }
    return 0;
}

static const zui_set_entry_t *centry(const zui_settings_t *st, uint32_t layer, zui_set_view_t sc,
                                     uint32_t key)
{
    return entry((zui_settings_t *) (uintptr_t) st, layer, sc, key);
}

/* S1, S4: the value in effect for view */
static void resolve(const zui_settings_t *st, zui_set_view_t view, uint32_t key,
                    zui_set_value_t *out, int below)
{
    *out = st->def[key].def;
    for (int l = 0; l < below; l++) {
        zui_set_view_t sc;
        if (!scope_of((zui_set_layer_t) l, view, &sc)) continue;
        const zui_set_entry_t *e = centry(st, (uint32_t) l, sc, key);
        if (!e) continue;
        *out = e->v;
        if (e->locked) return;
    }
}

/* ---- the cache (S3) ---------------------------------------------------- */

static uint32_t cslot(uint32_t key, zui_set_view_t v)
{
    uint32_t h = key * 2654435761u;
    h ^= ((uint32_t) v.seat << 16) ^ ((uint32_t) v.user << 8) ^ v.app;
    h *= 2246822519u;
    return (h >> 16) & (ZUI_SET_CACHE - 1u);
}

static void drop_key(zui_settings_t *st, uint32_t key)
{
    for (uint32_t i = 0; i < ZUI_SET_CACHE; i++)
        if (st->cache[i].key == key) st->cache[i].valid = 0;
}

int zui_set_get_k(zui_settings_t *st, zui_set_view_t view, uint32_t key, zui_set_value_t *out)
{
    if (key >= st->nkeys) return ZUI_SET_E_NOKEY;
    zui_set_cached_t *c = &st->cache[cslot(key, view)];
    if (c->valid && c->key == key && same(c->view, view)) {
        st->hits++;
        if (out) *out = c->v;
        return ZUI_SET_OK;
    }
    st->misses++;
    zui_set_value_t v;
    resolve(st, view, key, &v, ZUI_LAYER_COUNT);
    c->valid = 1;
    c->key = (uint16_t) key;
    c->view = view;
    c->v = v;
    if (out) *out = v;
    return ZUI_SET_OK;
}

int zui_set_get(zui_settings_t *st, zui_set_view_t view, const char *key, zui_set_value_t *out)
{
    int k = zui_set_key(st, key);
    if (k < 0) return k;
    return zui_set_get_k(st, view, (uint32_t) k, out);
}

int32_t zui_set_int(zui_settings_t *st, zui_set_view_t view, const char *key)
{
    zui_set_value_t v;
    if (zui_set_get(st, view, key, &v)) return 0;
    return v.i;
}

bool zui_set_bool(zui_settings_t *st, zui_set_view_t view, const char *key)
{
    return zui_set_int(st, view, key) != 0;
}

void zui_set_str(zui_settings_t *st, zui_set_view_t view, const char *key, char *buf, uint32_t cap)
{
    zui_set_value_t v;
    if (!buf || !cap) return;
    if (zui_set_get(st, view, key, &v)) {
        buf[0] = 0;
        return;
    }
    scopy(buf, cap, v.s);
}

int zui_set_peek(const zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                 const char *key, zui_set_value_t *out, bool *locked)
{
    int k = zui_set_key(st, key);
    if (k < 0) return k;
    if (!good_scope(st, layer, scope)) return ZUI_SET_E_SCOPE;
    const zui_set_entry_t *e = centry(st, (uint32_t) layer, scope, (uint32_t) k);
    if (!e) return ZUI_SET_E_NOKEY;
    if (out) *out = e->v;
    if (locked) *locked = e->locked != 0;
    return ZUI_SET_OK;
}

/* ---- changes and their watchers (S5) ----------------------------------- */

static bool prefixed(const char *key, const char *prefix)
{
    for (uint32_t i = 0; prefix[i]; i++)
        if (key[i] != prefix[i]) return false;
    return true;
}

static bool affects(zui_set_layer_t layer, zui_set_view_t sc, zui_set_view_t v)
{
    switch (layer) {
    case ZUI_LAYER_SEAT:
        return v.seat == sc.seat;
    case ZUI_LAYER_USER:
        return v.user == sc.user;
    case ZUI_LAYER_APP:
        return v.user == sc.user && v.app == sc.app;
    default:
        return true;
    }
}

enum { OP_SET, OP_REMOVE };

/* Apply one change to (layer, scope, key) and tell the watchers whose view
 * now sees a different value. The scope is already checked. */
static int commit(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t sc, uint32_t key,
                  int op, const zui_set_value_t *v, bool locked)
{
    zui_set_entry_t *e = entry(st, (uint32_t) layer, sc, key);
    if (op == OP_REMOVE && !e) return ZUI_SET_OK;
    if (op == OP_SET && !e) {
        for (uint32_t i = 0; i < ZUI_SET_ENTRIES && !e; i++)
            if (!st->e[i].used) e = &st->e[i];
        if (!e) return ZUI_SET_E_FULL;
        zero(e, (uint32_t) sizeof *e);
    }
    uint8_t type = st->def[key].type;
    zui_set_value_t before[ZUI_SET_WATCH];
    bool look[ZUI_SET_WATCH];
    for (uint32_t w = 0; w < ZUI_SET_WATCH; w++) {
        const zui_set_watcher_t *x = &st->watch[w];
        look[w] = x->used && affects(layer, sc, x->view) && prefixed(st->def[key].key, x->prefix);
        if (look[w]) resolve(st, x->view, key, &before[w], ZUI_LAYER_COUNT);
    }
    if (op == OP_SET) {
        e->used = 1;
        e->layer = (uint8_t) layer;
        e->scope = sc;
        e->key = (uint16_t) key;
        e->locked = locked ? 1 : 0;
        e->v = *v;
    } else {
        zero(e, (uint32_t) sizeof *e);
    }
    drop_key(st, key);
    st->busy++;
    for (uint32_t w = 0; w < ZUI_SET_WATCH; w++) {
        if (!look[w] || !st->watch[w].used) continue;
        zui_set_value_t now;
        resolve(st, st->watch[w].view, key, &now, ZUI_LAYER_COUNT);
        if (!val_eq(type, &before[w], &now)) st->watch[w].fn(st, key, &now, st->watch[w].ctx);
    }
    st->busy--;
    return ZUI_SET_OK;
}

/* S4: a SYSTEM or DEVICE lock under layer refuses the write */
static bool locked_below(const zui_settings_t *st, zui_set_layer_t layer, uint32_t key)
{
    zui_set_view_t none = {0, 0, 0};
    for (uint32_t l = ZUI_LAYER_SYSTEM; l <= ZUI_LAYER_DEVICE && l < (uint32_t) layer; l++) {
        const zui_set_entry_t *e = centry(st, l, none, key);
        if (e && e->locked) return true;
    }
    return false;
}

static int prepare(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, const char *key)
{
    if (st->busy) return ZUI_SET_E_BUSY;
    int k = zui_set_key(st, key);
    if (k < 0) return k;
    if (!good_scope(st, layer, scope)) return ZUI_SET_E_SCOPE;
    return k;
}

int zui_set_set(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, const char *key,
                const zui_set_value_t *v)
{
    int k = prepare(st, layer, scope, key);
    if (k < 0) return k;
    if (!v) return ZUI_SET_E_TYPE;
    zui_set_value_t n;
    int r = check(&st->def[k], v, &n);
    if (r) return r;
    if (locked_below(st, layer, (uint32_t) k)) return ZUI_SET_E_LOCKED;
    const zui_set_entry_t *e = centry(st, (uint32_t) layer, scope, (uint32_t) k);
    return commit(st, layer, scope, (uint32_t) k, OP_SET, &n, e && e->locked);
}

int zui_set_set_int(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                    const char *key, int32_t v)
{
    int k = zui_set_key(st, key);
    if (k >= 0 && st->def[k].type == ZUI_SET_STR) return ZUI_SET_E_TYPE;
    zui_set_value_t x;
    zero(&x, (uint32_t) sizeof x);
    x.i = v;
    return zui_set_set(st, layer, scope, key, &x);
}

int zui_set_set_str(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                    const char *key, const char *v)
{
    int k = zui_set_key(st, key);
    if (k >= 0 && st->def[k].type != ZUI_SET_STR) return ZUI_SET_E_TYPE;
    if (slen(v) >= ZUI_SET_STR_MAX) return k < 0 ? k : ZUI_SET_E_RANGE;
    zui_set_value_t x;
    zero(&x, (uint32_t) sizeof x);
    scopy(x.s, ZUI_SET_STR_MAX, v);
    return zui_set_set(st, layer, scope, key, &x);
}

int zui_set_set_text(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                     const char *key, const char *text)
{
    int k = zui_set_key(st, key);
    if (k < 0) return k;
    zui_set_value_t x;
    int r = zui_set_parse(st, (uint32_t) k, text, slen(text), &x);
    if (r) return r;
    return zui_set_set(st, layer, scope, key, &x);
}

int zui_set_lock(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, const char *key,
                 bool on)
{
    int k = prepare(st, layer, scope, key);
    if (k < 0) return k;
    if (layer > ZUI_LAYER_SEAT) return ZUI_SET_E_SCOPE;
    if (on && locked_below(st, layer, (uint32_t) k)) return ZUI_SET_E_LOCKED;
    const zui_set_entry_t *e = centry(st, (uint32_t) layer, scope, (uint32_t) k);
    zui_set_value_t v;
    if (e) {
        if ((e->locked != 0) == on) return ZUI_SET_OK;
        v = e->v;
    } else {
        if (!on) return ZUI_SET_OK;
        resolve(st, scope, (uint32_t) k, &v, (int) layer); /* what shows below */
    }
    return commit(st, layer, scope, (uint32_t) k, OP_SET, &v, on);
}

int zui_set_reset(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, const char *key)
{
    int k = prepare(st, layer, scope, key);
    if (k < 0) return k;
    return commit(st, layer, scope, (uint32_t) k, OP_REMOVE, 0, false);
}

int zui_set_reset_layer(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope)
{
    if (st->busy) return ZUI_SET_E_BUSY;
    if (!good_scope(st, layer, scope)) return ZUI_SET_E_SCOPE;
    for (uint32_t k = 0; k < st->nkeys; k++) commit(st, layer, scope, k, OP_REMOVE, 0, false);
    return ZUI_SET_OK;
}

int zui_set_watch(zui_settings_t *st, zui_set_view_t view, const char *prefix, zui_set_watch_fn fn,
                  void *ctx)
{
    if (!fn) return ZUI_SET_E_TYPE;
    if (!known(st, view)) return ZUI_SET_E_SCOPE;
    uint32_t n = slen(prefix);
    if (n >= ZUI_SET_KEY_MAX) return ZUI_SET_E_NAME;
    for (uint32_t i = 0; i < n; i++)
        if (!key_char(prefix[i])) return ZUI_SET_E_NAME;
    for (uint32_t w = 0; w < ZUI_SET_WATCH; w++) {
        zui_set_watcher_t *x = &st->watch[w];
        if (x->used) continue;
        x->used = 1;
        x->view = view;
        scopy(x->prefix, ZUI_SET_KEY_MAX, prefix);
        x->fn = fn;
        x->ctx = ctx;
        return (int) w + 1;
    }
    return ZUI_SET_E_FULL;
}

void zui_set_unwatch(zui_settings_t *st, int id)
{
    if (id >= 1 && id <= (int) ZUI_SET_WATCH) zero(&st->watch[id - 1], sizeof st->watch[0]);
}

/* ---- text (S6, S7) ----------------------------------------------------- */

typedef struct {
    char *b;
    uint32_t cap, n;
    bool over;
} out_t;

static void put(out_t *o, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (o->n + 1u >= o->cap) {
            o->over = true;
            return;
        }
        o->b[o->n++] = s[i];
    }
}

static void puts_(out_t *o, const char *s)
{
    put(o, s, slen(s));
}

static void put_int(out_t *o, int32_t v)
{
    char t[12];
    uint32_t n = 0;
    uint32_t u = v < 0 ? (uint32_t) 0 - (uint32_t) v : (uint32_t) v;
    do {
        t[n++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u);
    if (v < 0) t[n++] = '-';
    for (uint32_t i = 0; i < n / 2u; i++) {
        char c = t[i];
        t[i] = t[n - 1u - i];
        t[n - 1u - i] = c;
    }
    put(o, t, n);
}

static void put_value(out_t *o, const zui_set_def_t *d, const zui_set_value_t *v)
{
    switch (d->type) {
    case ZUI_SET_INT:
        put_int(o, v->i);
        break;
    case ZUI_SET_BOOL:
        puts_(o, v->i ? "true" : "false");
        break;
    case ZUI_SET_ENUM:
        puts_(o, (v->i >= 0 && v->i < (int32_t) d->nnames) ? d->names[v->i] : "?");
        break;
    case ZUI_SET_COLOR:
        puts_(o, zui_color_role_name((zui_color_role_t) v->i));
        break;
    default:
        put(o, "\"", 1);
        for (uint32_t i = 0; i < ZUI_SET_STR_MAX && v->s[i]; i++) {
            if (v->s[i] == '"' || v->s[i] == '\\') put(o, "\\", 1);
            put(o, &v->s[i], 1);
        }
        put(o, "\"", 1);
    }
}

int zui_set_format(const zui_settings_t *st, uint32_t key, const zui_set_value_t *v, char *buf,
                   uint32_t cap)
{
    if (key >= st->nkeys || !v) return ZUI_SET_E_NOKEY;
    if (!buf || !cap) return ZUI_SET_E_SPACE;
    out_t o = {buf, cap, 0, false};
    put_value(&o, &st->def[key], v);
    buf[o.n] = 0;
    return o.over ? ZUI_SET_E_SPACE : (int) o.n;
}

const char *zui_set_layer_name(zui_set_layer_t layer)
{
    static const char *const N[ZUI_LAYER_COUNT] = {"system", "device", "seat", "user", "app"};
    return (uint32_t) layer < ZUI_LAYER_COUNT ? N[layer] : "?";
}

int zui_set_save(const zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, char *buf,
                 uint32_t cap)
{
    if (!good_scope(st, layer, scope)) return ZUI_SET_E_SCOPE;
    if (!buf || !cap) return ZUI_SET_E_SPACE;
    out_t o = {buf, cap, 0, false};
    puts_(&o, "zxcfg 1\n# ");
    puts_(&o, zui_set_layer_name(layer));
    if (scope.seat) puts_(&o, " "), puts_(&o, st->seat[scope.seat - 1u]);
    if (scope.user) puts_(&o, " "), puts_(&o, st->user[scope.user - 1u]);
    if (scope.app) puts_(&o, " "), puts_(&o, st->app[scope.app - 1u]);
    puts_(&o, "\n");
    for (uint32_t k = 0; k < st->nkeys; k++) {
        const zui_set_entry_t *e = centry(st, (uint32_t) layer, scope, k);
        if (!e) continue;
        if (e->locked) puts_(&o, "lock ");
        puts_(&o, st->def[k].key);
        puts_(&o, " = ");
        put_value(&o, &st->def[k], &e->v);
        puts_(&o, "\n");
    }
    buf[o.n] = 0;
    return o.over ? ZUI_SET_E_SPACE : (int) o.n;
}

static bool blank_c(char c)
{
    return c == ' ' || c == '\t';
}

int zui_set_parse(const zui_settings_t *st, uint32_t key, const char *t, uint32_t n,
                  zui_set_value_t *out)
{
    if (key >= st->nkeys) return ZUI_SET_E_NOKEY;
    const zui_set_def_t *d = &st->def[key];
    zui_set_value_t v;
    zero(&v, (uint32_t) sizeof v);
    if (!t) return ZUI_SET_E_TYPE;
    switch (d->type) {
    case ZUI_SET_INT: {
        uint32_t i = 0;
        bool neg = n > 0 && t[0] == '-';
        if (neg) i++;
        if (i == n || n - i > 10u) return ZUI_SET_E_TYPE;
        int64_t x = 0;
        for (; i < n; i++) {
            if (t[i] < '0' || t[i] > '9') return ZUI_SET_E_TYPE;
            x = x * 10 + (t[i] - '0');
        }
        if (neg) x = -x;
        if (x < INT32_MIN || x > INT32_MAX) return ZUI_SET_E_TYPE;
        v.i = (int32_t) x;
        break;
    }
    case ZUI_SET_BOOL:
        if (neq(t, n, "true"))
            v.i = 1;
        else if (neq(t, n, "false"))
            v.i = 0;
        else
            return ZUI_SET_E_TYPE;
        break;
    case ZUI_SET_ENUM: {
        uint32_t i = 0;
        while (i < d->nnames && !neq(t, n, d->names[i])) i++;
        if (i == d->nnames) return ZUI_SET_E_TYPE;
        v.i = (int32_t) i;
        break;
    }
    case ZUI_SET_COLOR: {
        uint32_t i = 0;
        while (i < ZUI_C_COUNT && !neq(t, n, zui_color_role_name((zui_color_role_t) i))) i++;
        if (i == ZUI_C_COUNT) return ZUI_SET_E_TYPE;
        v.i = (int32_t) i;
        break;
    }
    default: {
        if (n < 2u || t[0] != '"' || t[n - 1u] != '"') return ZUI_SET_E_TYPE;
        uint32_t j = 0;
        for (uint32_t i = 1; i + 1u < n; i++) {
            char c = t[i];
            if (c == '"') return ZUI_SET_E_TYPE; /* an unescaped quote */
            if (c == '\\') {
                if (i + 2u >= n) return ZUI_SET_E_TYPE; /* escapes the closing quote */
                c = t[++i];
                if (c != '"' && c != '\\') return ZUI_SET_E_TYPE;
            }
            if (j + 1u >= ZUI_SET_STR_MAX) return ZUI_SET_E_RANGE;
            v.s[j++] = c;
        }
    }
    }
    return check(d, &v, out);
}

/* One line, without its newline: parse "[lock ]key = value". Returns 1 for
 * a value, 0 for a blank or comment line, or a negative error. */
static int line_kv(const zui_settings_t *st, const char *s, uint32_t n, int *key, bool *lock,
                   zui_set_value_t *v)
{
    while (n && (blank_c(s[n - 1u]) || s[n - 1u] == '\r')) n--;
    uint32_t i = 0;
    while (i < n && blank_c(s[i])) i++;
    if (i == n || s[i] == '#') return 0;
    *lock = false;
    if (n - i > 5u && neq(s + i, 4, "lock") && blank_c(s[i + 4u])) {
        *lock = true;
        i += 5u;
        while (i < n && blank_c(s[i])) i++;
    }
    uint32_t k0 = i;
    while (i < n && key_char(s[i])) i++;
    uint32_t k1 = i;
    while (i < n && blank_c(s[i])) i++;
    if (k1 == k0 || i == n || s[i] != '=') return ZUI_SET_E_PARSE;
    i++;
    while (i < n && blank_c(s[i])) i++;
    *key = find_key(st, s + k0, k1 - k0);
    if (*key < 0) return ZUI_SET_E_NOKEY;
    int r = zui_set_parse(st, (uint32_t) *key, s + i, n - i, v);
    return r ? r : 1;
}

int zui_set_load(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, const char *text,
                 uint32_t len, uint32_t *line)
{
    uint32_t dummy;
    if (!line) line = &dummy;
    *line = 0;
    if (st->busy) return ZUI_SET_E_BUSY;
    if (!good_scope(st, layer, scope)) return ZUI_SET_E_SCOPE;
    /* pass 1: check every line; note where each key is */
    uint32_t at[ZUI_SET_KEYS]; /* offset of its line + 1, 0 = not in the text */
    zero(at, (uint32_t) sizeof at);
    uint32_t ln = 0, p = 0, adds = 0;
    while (p < len || ln == 0) {
        uint32_t q = p;
        while (q < len && text[q] != '\n') q++;
        ln++;
        *line = ln;
        if (q - p >= ZUI_SET_LINE_MAX) return ZUI_SET_E_PARSE;
        if (ln == 1) {
            uint32_t n = q - p;
            if (n && text[q - 1u] == '\r') n--;
            if (n < 6u || !neq(text + p, 6, "zxcfg ")) return ZUI_SET_E_VERSION;
            if (!neq(text + p + 6, n - 6u, "1")) return ZUI_SET_E_VERSION;
        } else {
            int key;
            bool lock;
            zui_set_value_t v;
            int r = line_kv(st, text + p, q - p, &key, &lock, &v);
            if (r < 0) return r;
            if (r == 1) {
                if (at[key]) return ZUI_SET_E_DUP;
                if (lock && layer > ZUI_LAYER_SEAT) return ZUI_SET_E_SCOPE;
                at[key] = p + 1u;
                if (!centry(st, (uint32_t) layer, scope, (uint32_t) key)) adds++;
            }
        }
        p = q + 1u;
        if (q >= len) break;
    }
    /* room: values of this layer not in the text free their slots first */
    uint32_t free_ = 0;
    for (uint32_t i = 0; i < ZUI_SET_ENTRIES; i++) {
        const zui_set_entry_t *e = &st->e[i];
        if (!e->used || (e->layer == (uint8_t) layer && same(e->scope, scope) && !at[e->key]))
            free_++;
    }
    *line = 0;
    if (adds > free_) return ZUI_SET_E_FULL;
    /* pass 2: drop what the text does not have, then set what it has */
    for (uint32_t k = 0; k < st->nkeys; k++)
        if (!at[k]) commit(st, layer, scope, k, OP_REMOVE, 0, false);
    for (uint32_t k = 0; k < st->nkeys; k++) {
        if (!at[k]) continue;
        uint32_t s = at[k] - 1u, q = s;
        while (q < len && text[q] != '\n') q++;
        int key;
        bool lock;
        zui_set_value_t v;
        line_kv(st, text + s, q - s, &key, &lock, &v);
        const zui_set_entry_t *e = centry(st, (uint32_t) layer, scope, k);
        if (e && (e->locked != 0) == lock && val_eq(st->def[k].type, &e->v, &v)) continue;
        commit(st, layer, scope, k, OP_SET, &v, lock);
    }
    return ZUI_SET_OK;
}
