/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_spaces.c — spaces (see zxv_spaces.h S1-S4). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_spaces.h"
#include "web4_web2.h"

/* ===================== S1 ===================== */

#define V ZXS_VIEW
#define W ZXS_WRITE
static const unsigned char RIGHTS[ZXS_KINDS][ZXS_ROLES] = {
    /*            owner  member guest  public */
    [ZXS_HOME] = {V | W, 0, 0, 0},
    [ZXS_OFFICE] = {V | W, V | W, V, 0},
    [ZXS_TEAM] = {V | W, V | W, V, 0},
    [ZXS_CLOUDTOP] = {V | W, V, V, V},
    [ZXS_PARK] = {V | W, V | W, V | W, V | W},
};
#undef V
#undef W

unsigned zxs_rights(zxs_kind_t kind, zxs_role_t role)
{
    if ((unsigned) kind >= ZXS_KINDS || (unsigned) role >= ZXS_ROLES) return 0;
    return RIGHTS[kind][role];
}

bool zxs_can(const zxs_space_t *s, unsigned want)
{
    return want && (zxs_rights((zxs_kind_t) s->kind, (zxs_role_t) s->role) & want) == want;
}

static const char *const KIND[ZXS_KINDS] = {"home", "office", "team", "cloudtop", "park"};
static const char *const ROLE[ZXS_ROLES] = {"owner", "member", "guest", "public"};

const char *zxs_kind_name(zxs_kind_t k)
{
    return (unsigned) k < ZXS_KINDS ? KIND[k] : "?";
}

const char *zxs_role_name(zxs_role_t r)
{
    return (unsigned) r < ZXS_ROLES ? ROLE[r] : "?";
}

int zxs_kind_parse(const char *s)
{
    for (int i = 0; i < ZXS_KINDS; i++)
        if (!strcmp(s, KIND[i])) return i;
    return -1;
}

int zxs_role_parse(const char *s)
{
    for (int i = 0; i < ZXS_ROLES; i++)
        if (!strcmp(s, ROLE[i])) return i;
    return -1;
}

bool zxs_who_ok(const char *who)
{
    size_t n = strlen(who);
    if (n == 0 || n > ZXS_WHO_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (who[i] < 0x21 || who[i] > 0x7e || who[i] == '"' || who[i] == '\\') return false;
    return true;
}

/* a display name: 1..80 bytes of UTF-8 with no control characters */
static bool name_ok(const char *p)
{
    const unsigned char *s = (const unsigned char *) p;
    size_t n = strlen(p);
    if (n == 0 || n > ZXS_NAME_MAX) return false;
    for (size_t i = 0; i < n;) {
        unsigned c = s[i];
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f) return false;
            i++;
            continue;
        }
        size_t k = c >= 0xc2 && c <= 0xdf   ? 1
                   : c >= 0xe0 && c <= 0xef ? 2
                   : c >= 0xf0 && c <= 0xf4 ? 3
                                            : 0;
        if (!k) return false;
        uint32_t cp = c & (k == 1 ? 0x1f : k == 2 ? 0x0f : 0x07);
        for (size_t j = 1; j <= k; j++) {
            if (i + j >= n || (s[i + j] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (s[i + j] & 0x3f);
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        i += k + 1;
    }
    return true;
}

/* ===================== S3 ===================== */

static bool is_public(const zxs_space_t *s)
{
    return s->kind == ZXS_CLOUDTOP || s->kind == ZXS_PARK;
}

/* Is `who` in s's audience? This node always is (it holds the copy). */
static bool sees(const zxs_space_t *s, const char *who)
{
    if (is_public(s) || !strcmp(who, ZXS_ME) || !strcmp(who, s->owner)) return true;
    if (s->kind == ZXS_HOME) return false;
    for (uint32_t i = 0; i < s->n; i++)
        if (!strcmp(s->m[i].who, who)) return true;
    return false;
}

bool zxs_widens(const zxs_space_t *from, const zxs_space_t *to)
{
    if (to->kind == ZXS_HOME) return false;
    if (to->kind > from->kind) return true; /* kinds run from private to open */
    if (is_public(from)) return false;
    if (is_public(to)) return true;
    if (!sees(from, to->owner)) return true;
    for (uint32_t i = 0; i < to->n; i++)
        if (!sees(from, to->m[i].who)) return true;
    return false;
}

/* ===================== JSON ===================== */

typedef struct {
    char *p;
    uint32_t n, cap;
    bool over;
} out_t;

static void put(out_t *o, const char *s, size_t n)
{
    if (o->over || o->n + n >= o->cap) {
        o->over = true;
        return;
    }
    memcpy(o->p + o->n, s, n);
    o->n += (uint32_t) n;
}

static void puts_(out_t *o, const char *s)
{
    put(o, s, strlen(s));
}

static void str(out_t *o, const char *s)
{
    put(o, "\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') put(o, "\\", 1);
        put(o, s, 1);
    }
    put(o, "\"", 1);
}

int32_t zxs_json(const zxs_space_t *s, char *out, uint32_t cap)
{
    out_t o = {out, 0, cap, false};
    puts_(&o, "{\"id\":");
    str(&o, s->id);
    puts_(&o, ",\"kind\":");
    str(&o, zxs_kind_name((zxs_kind_t) s->kind));
    puts_(&o, ",\"name\":");
    str(&o, s->name);
    puts_(&o, ",\"owner\":");
    str(&o, s->owner);
    puts_(&o, ",\"role\":");
    str(&o, zxs_role_name((zxs_role_t) s->role));
    unsigned r = zxs_rights((zxs_kind_t) s->kind, (zxs_role_t) s->role);
    puts_(&o, r & ZXS_VIEW ? ",\"view\":true" : ",\"view\":false");
    puts_(&o, r & ZXS_WRITE ? ",\"write\":true" : ",\"write\":false");
    puts_(&o, ",\"public\":");
    puts_(&o, is_public(s) ? "true" : "false");
    puts_(&o, ",\"members\":[");
    for (uint32_t i = 0; i < s->n; i++) {
        puts_(&o, i ? ",{\"who\":" : "{\"who\":");
        str(&o, s->m[i].who);
        puts_(&o, ",\"role\":");
        str(&o, zxs_role_name((zxs_role_t) s->m[i].role));
        puts_(&o, "}");
    }
    puts_(&o, "]}");
    if (o.over) return -1;
    out[o.n] = 0;
    return (int32_t) o.n;
}

int zxs_parse(const char *js, uint32_t n, zxs_space_t *s)
{
    static w4_jtok_t t[16 + 8 * ZXS_MEMBERS_MAX];
    const uint32_t max = sizeof t / sizeof *t;
    char kind[16], role[16];
    memset(s, 0, sizeof *s);
    int32_t k = w4_json_parse(js, n, t, max, 4);
    if (k <= 0 || t[0].type != W4_J_OBJ) return ZXF_E_IO;
    if (w4_json_get_str(js, t, k, 0, "id", s->id, sizeof s->id) < 0 ||
        w4_json_get_str(js, t, k, 0, "kind", kind, sizeof kind) < 0 ||
        w4_json_get_str(js, t, k, 0, "name", s->name, sizeof s->name) < 0 ||
        w4_json_get_str(js, t, k, 0, "owner", s->owner, sizeof s->owner) < 0 ||
        w4_json_get_str(js, t, k, 0, "role", role, sizeof role) < 0)
        return ZXF_E_IO;
    int kk = zxs_kind_parse(kind), rr = zxs_role_parse(role);
    if (kk <= ZXS_HOME || rr < 0 || rr == ZXS_PUBLIC || !zxf_space_id_ok(s->id) ||
        !name_ok(s->name) || !zxs_who_ok(s->owner) ||
        (rr == ZXS_OWNER) != !strcmp(s->owner, ZXS_ME))
        return ZXF_E_IO;
    s->kind = (uint8_t) kk;
    s->role = (uint8_t) rr;
    int32_t arr = w4_json_get(js, t, k, 0, "members");
    if (arr < 0 || t[arr].type != W4_J_ARR || t[arr].size > ZXS_MEMBERS_MAX) return ZXF_E_IO;
    for (uint32_t i = 0; i < t[arr].size; i++) {
        int32_t m = w4_json_at(t, k, arr, i);
        zxs_member_t *e = &s->m[s->n];
        if (m < 0 || t[m].type != W4_J_OBJ ||
            w4_json_get_str(js, t, k, m, "who", e->who, sizeof e->who) < 0 ||
            w4_json_get_str(js, t, k, m, "role", role, sizeof role) < 0)
            return ZXF_E_IO;
        rr = zxs_role_parse(role);
        if (!zxs_who_ok(e->who) || (rr != ZXS_MEMBER && rr != ZXS_GUEST)) return ZXF_E_IO;
        e->role = (uint8_t) rr;
        s->n++;
    }
    return ZXF_OK;
}

/* ===================== the store ===================== */

void zxs_home(zxs_space_t *s)
{
    memset(s, 0, sizeof *s);
    snprintf(s->id, sizeof s->id, "home");
    s->kind = ZXS_HOME;
    snprintf(s->name, sizeof s->name, "Home");
    snprintf(s->owner, sizeof s->owner, ZXS_ME);
    s->role = ZXS_OWNER;
}

int zxs_load(const char *id, zxs_space_t *s)
{
    static char buf[16384];
    uint32_t n;
    if (!strcmp(id, "home")) {
        zxs_home(s);
        return ZXF_OK;
    }
    int rc = zxf_meta_read(id, buf, sizeof buf, &n);
    if (rc) return rc;
    if ((rc = zxs_parse(buf, n, s)) != 0) return rc;
    return strcmp(s->id, id) ? ZXF_E_IO : ZXF_OK; /* a manifest under another name */
}

int zxs_save(const zxs_space_t *s, bool exclusive)
{
    static char buf[16384];
    int32_t n = zxs_json(s, buf, sizeof buf);
    if (n < 0) return ZXF_E_TOOBIG;
    return zxf_meta_write(s->id, buf, (uint32_t) n, exclusive);
}

static int check_new(zxs_kind_t kind, const char *name)
{
    if (kind <= ZXS_HOME || kind >= ZXS_KINDS) return ZXF_E_PATH;
    return name_ok(name) ? ZXF_OK : ZXF_E_PATH;
}

int zxs_create(zxs_kind_t kind, const char *name, zxs_space_t *out)
{
    int rc = check_new(kind, name);
    if (rc) return rc;
    char slug[ZXF_SPACE_ID_MAX + 1];
    size_t n = 0;
    bool dash = false;
    for (const char *p = name; *p && n < ZXF_SPACE_ID_MAX - 4; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (dash && n) slug[n++] = '-';
            slug[n++] = c;
            dash = false;
        } else {
            dash = true;
        }
    }
    slug[n] = 0;
    if (!n || !strcmp(slug, "home")) snprintf(slug, sizeof slug, "%s", zxs_kind_name(kind));
    memset(out, 0, sizeof *out);
    out->kind = (uint8_t) kind;
    snprintf(out->name, sizeof out->name, "%s", name);
    snprintf(out->owner, sizeof out->owner, ZXS_ME);
    out->role = ZXS_OWNER;
    for (unsigned i = 1; i <= 999; i++) {
        /* slug is at most ZXF_SPACE_ID_MAX - 4 bytes: room for "-999" */
        size_t k = strlen(slug);
        memcpy(out->id, slug, k);
        if (i > 1) {
            out->id[k++] = '-';
            if (i >= 100) out->id[k++] = (char) ('0' + i / 100);
            if (i >= 10) out->id[k++] = (char) ('0' + i / 10 % 10);
            out->id[k++] = (char) ('0' + i % 10);
        }
        out->id[k] = 0;
        rc = zxs_save(out, true);
        if (rc != ZXF_E_EXISTS) return rc;
    }
    return ZXF_E_EXISTS;
}

int zxs_join(const char *id, zxs_kind_t kind, const char *name, const char *owner, zxs_role_t role,
             zxs_space_t *out)
{
    int rc = check_new(kind, name);
    if (rc) return rc;
    if (!zxf_space_id_ok(id) || !zxs_who_ok(owner) || !strcmp(owner, ZXS_ME) ||
        (role != ZXS_MEMBER && role != ZXS_GUEST))
        return ZXF_E_PATH;
    memset(out, 0, sizeof *out);
    snprintf(out->id, sizeof out->id, "%s", id);
    out->kind = (uint8_t) kind;
    snprintf(out->name, sizeof out->name, "%s", name);
    snprintf(out->owner, sizeof out->owner, "%s", owner);
    out->role = (uint8_t) role;
    return zxs_save(out, true);
}

typedef struct {
    char (*ids)[ZXF_SPACE_ID_MAX + 1];
    uint32_t n, cap;
} ids_t;

static int collect(void *ctx, const char *id)
{
    ids_t *c = ctx;
    if (c->n == c->cap) {
        uint32_t nc = c->cap ? c->cap * 2 : 16;
        void *nv = realloc(c->ids, nc * sizeof *c->ids);
        if (!nv) return ZXF_E_IO;
        c->ids = nv;
        c->cap = nc;
    }
    snprintf(c->ids[c->n++], sizeof *c->ids, "%s", id);
    return 0;
}

static int by_id(const void *a, const void *b)
{
    return strcmp(a, b);
}

int zxs_each(zxs_each_fn fn, void *ctx)
{
    static zxs_space_t s;
    zxs_home(&s);
    int rc = fn(ctx, &s);
    if (rc) return rc;
    ids_t c = {NULL, 0, 0};
    rc = zxf_meta_each(collect, &c);
    if (!rc && c.n) qsort(c.ids, c.n, sizeof *c.ids, by_id);
    for (uint32_t i = 0; !rc && i < c.n; i++)
        if (zxs_load(c.ids[i], &s) == ZXF_OK) rc = fn(ctx, &s); /* a broken manifest is skipped */
    free(c.ids);
    return rc;
}
