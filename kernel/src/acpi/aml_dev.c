/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aml_dev.c — the namespace as devices: walking it, matching _HID/_CID,
 * _STA, the _REG/_INI start-up sequence, and typed readers for the standard
 * power-management methods (sleep packages, battery, thermal zone, lid,
 * processor performance, backlight). Every reader returns integers only and
 * evaluates a device's OWN child method (no upward name search). */
#include "aml_int.h"

static uint32_t seg4(const char *s)
{
    return aml_rd_le((const uint8_t *) s, 4);
}

/* Evaluate dev's own child `name`; AML_ENOENT when it has none. */
static int child_eval(aml_ctx_t *c, aml_node_t *dev, const char *name, const aml_val_t *args,
                      uint32_t argc, aml_val_t *ret)
{
    aml_node_t *n = aml_ns_child(dev, seg4(name));
    if (!n) {
        aml_bzero(ret, (uint32_t) sizeof *ret);
        return AML_ENOENT;
    }
    return aml_eval_node(c, n, args, argc, ret);
}

static int child_int(aml_ctx_t *c, aml_node_t *dev, const char *name, uint64_t *v)
{
    aml_val_t r;
    int e = child_eval(c, dev, name, 0, 0, &r);
    if (e) return e;
    e = aml_to_int(c, &r, v);
    aml_val_free(c, &r);
    return e;
}

static int elem_u32(aml_ctx_t *c, const aml_val_t *pkg, uint32_t i, uint32_t *out)
{
    const aml_val_t *e = aml_pkg_elem(pkg, i);
    uint64_t v;
    if (!e || e->type != AML_T_INT) return AML_ETYPE;
    if (aml_to_int(c, e, &v)) return AML_ETYPE;
    *out = (uint32_t) v;
    return AML_OK;
}

/* ---- walking ---------------------------------------------------------------------- */
uint32_t aml_walk(aml_ctx_t *c, aml_node_t *start, aml_walk_fn cb, void *user)
{
    uint32_t n = 0, d = 0;
    if (!c || !c->root || !cb) return 0;
    if (!start) start = c->root;
    aml_node_t *cur = start;
    while (cur) {
        int act = cb(c, cur, d, user);
        n++;
        if (act == 2) break;
        if (act == 0 && cur->child && d < 64) {
            cur = cur->child;
            d++;
            continue;
        }
        while (cur != start && !cur->next) {
            cur = cur->parent;
            d--;
        }
        if (cur == start) break;
        cur = cur->next;
    }
    return n;
}

/* ---- ids ---------------------------------------------------------------------------- */
void aml_eisa_to_str(uint32_t eisa, char out[8])
{
    static const char hx[] = "0123456789ABCDEF";
    uint32_t b =
        ((eisa & 0xFF) << 24) | ((eisa & 0xFF00) << 8) | ((eisa >> 8) & 0xFF00) | (eisa >> 24);
    out[0] = (char) ('@' + ((b >> 26) & 0x1F));
    out[1] = (char) ('@' + ((b >> 21) & 0x1F));
    out[2] = (char) ('@' + ((b >> 16) & 0x1F));
    out[3] = hx[(b >> 12) & 15];
    out[4] = hx[(b >> 8) & 15];
    out[5] = hx[(b >> 4) & 15];
    out[6] = hx[b & 15];
    out[7] = 0;
}

static int id_equal(aml_ctx_t *c, const aml_val_t *v, const char *id)
{
    (void) c;
    if (v->type == AML_T_INT) {
        char s[8];
        aml_eisa_to_str((uint32_t) v->u.i, s);
        for (int i = 0; i < 8; i++)
            if (s[i] != id[i]) return 0;
        return 1;
    }
    if (v->type == AML_T_STR) {
        uint32_t i = 0;
        while (i < v->len && id[i] && (char) v->u.bytes[i] == id[i]) i++;
        return i == v->len && !id[i];
    }
    return 0;
}

int aml_dev_match_id(aml_ctx_t *c, aml_node_t *n, const char *id)
{
    aml_val_t v;
    int hit = 0;
    if (!c || !n || !id) return 0;
    if (!child_eval(c, n, "_HID", 0, 0, &v)) {
        hit = id_equal(c, &v, id);
        aml_val_free(c, &v);
        if (hit) return 1;
    }
    if (!child_eval(c, n, "_CID", 0, 0, &v)) {
        if (v.type == AML_T_PKG) {
            for (uint32_t i = 0; i < v.len && !hit; i++) hit = id_equal(c, &v.u.elems[i], id);
        } else {
            hit = id_equal(c, &v, id);
        }
        aml_val_free(c, &v);
    }
    return hit;
}

int aml_dev_hid(aml_ctx_t *c, aml_node_t *n, char out[16])
{
    aml_val_t v;
    int ok = 0;
    if (!c || !n || !out) return 0;
    out[0] = 0;
    if (child_eval(c, n, "_HID", 0, 0, &v)) return 0;
    if (v.type == AML_T_INT) {
        aml_eisa_to_str((uint32_t) v.u.i, out);
        ok = 1;
    } else if (v.type == AML_T_STR && v.len) {
        uint32_t i = 0;
        for (; i < v.len && i < 15; i++) {
            char ch = (char) v.u.bytes[i];
            out[i] = (ch >= 0x20 && ch < 0x7F) ? ch : '?';
        }
        out[i] = 0;
        ok = 1;
    }
    aml_val_free(c, &v);
    return ok;
}

typedef struct find_ctx {
    const char *id;
    aml_node_t **out;
    uint32_t max, n;
} find_ctx_t;

static int find_cb(aml_ctx_t *c, aml_node_t *n, uint32_t depth, void *user)
{
    find_ctx_t *f = (find_ctx_t *) user;
    (void) depth;
    if (n->v.type == AML_T_DEVICE && aml_dev_match_id(c, n, f->id)) {
        if (f->n < f->max) f->out[f->n] = n;
        f->n++;
    }
    return 0;
}

uint32_t aml_find_devices(aml_ctx_t *c, const char *id, aml_node_t **out, uint32_t max)
{
    find_ctx_t f;
    f.id = id;
    f.out = out;
    f.max = max;
    f.n = 0;
    aml_walk(c, 0, find_cb, &f);
    return f.n < max ? f.n : max;
}

/* ---- _STA ------------------------------------------------------------------------- */
uint64_t aml_sta(aml_ctx_t *c, aml_node_t *dev)
{
    uint64_t v;
    int r = child_int(c, dev, "_STA", &v);
    if (r == AML_ENOENT) return 0x0F;
    return r ? 0 : v;
}

hw_verdict_t aml_sta_verdict(uint64_t sta)
{
    int present = (int) (sta & 1), working = (int) ((sta >> 3) & 1);
    if (present && working) return HW_T;
    if (present) return HW_F;
    if (working) return HW_B;
    return HW_N;
}

/* ---- start-up: _REG then _INI -------------------------------------------------------- */
typedef struct reg_ctx {
    aml_node_t *done_dev[64];
    uint8_t done_space[64];
    uint32_t n;
} reg_ctx_t;

static int reg_cb(aml_ctx_t *c, aml_node_t *n, uint32_t depth, void *user)
{
    reg_ctx_t *g = (reg_ctx_t *) user;
    (void) depth;
    if (n->v.type != AML_T_REGION || !n->parent) return 0;
    uint8_t sp = n->x.r.space;
    if (sp != AML_SPACE_PCI && sp != AML_SPACE_CMOS) return 0;
    for (uint32_t i = 0; i < g->n; i++)
        if (g->done_dev[i] == n->parent && g->done_space[i] == sp) return 0;
    if (g->n < 64) {
        g->done_dev[g->n] = n->parent;
        g->done_space[g->n] = sp;
        g->n++;
    }
    aml_val_t a[2], ret;
    a[0] = aml_int(sp);
    a[1] = aml_int(1);
    if (!child_eval(c, n->parent, "_REG", a, 2, &ret)) aml_val_free(c, &ret);
    return 0;
}

typedef struct ini_ctx {
    uint32_t ran;
} ini_ctx_t;

static int ini_cb(aml_ctx_t *c, aml_node_t *n, uint32_t depth, void *user)
{
    ini_ctx_t *g = (ini_ctx_t *) user;
    (void) depth;
    uint32_t t = n->v.type;
    if (t != AML_T_DEVICE && t != AML_T_PROC && t != AML_T_THERMAL) return 0;
    if (!n->parent) return 0;
    uint64_t sta = aml_sta(c, n);
    if (!(sta & 1)) return (sta & 8) ? 0 : 1; /* absent: children only if functioning */
    if (n->parent == c->root && n->name == seg4("_SB_")) return 0; /* ran first */
    aml_val_t ret;
    if (!child_eval(c, n, "_INI", 0, 0, &ret)) {
        g->ran++;
        aml_val_free(c, &ret);
    }
    return 0;
}

uint32_t aml_init_devices(aml_ctx_t *c)
{
    reg_ctx_t g;
    ini_ctx_t i;
    aml_val_t ret;
    if (!c || !c->root) return 0;
    g.n = 0;
    i.ran = 0;
    aml_walk(c, 0, reg_cb, &g);
    aml_node_t *sb = aml_ns_child(c->root, seg4("_SB_"));
    if (sb && !child_eval(c, sb, "_INI", 0, 0, &ret)) {
        i.ran++;
        aml_val_free(c, &ret);
    }
    aml_walk(c, 0, ini_cb, &i);
    return i.ran;
}

/* ---- sleep states ------------------------------------------------------------------- */
int aml_sleep_state(aml_ctx_t *c, uint32_t state, uint8_t *typa, uint8_t *typb)
{
    char name[5] = {'_', 'S', (char) ('0' + state), '_', 0};
    aml_val_t v;
    uint32_t a, b;
    if (!c || state > 5 || !typa || !typb) return AML_EARG;
    int r = child_eval(c, c->root, name, 0, 0, &v);
    if (r) return r;
    if (v.type != AML_T_PKG || !v.len) {
        r = AML_ETYPE;
    } else if (v.len == 1) {
        r = elem_u32(c, &v, 0, &a);
        b = a >> 8;
    } else {
        r = elem_u32(c, &v, 0, &a);
        if (!r) r = elem_u32(c, &v, 1, &b);
    }
    aml_val_free(c, &v);
    if (r) return r;
    *typa = (uint8_t) (a & 7);
    *typb = (uint8_t) (b & 7);
    return AML_OK;
}

/* ---- battery ------------------------------------------------------------------------ */
int aml_battery_read(aml_ctx_t *c, aml_node_t *bat, aml_battery_t *o)
{
    aml_val_t v;
    int r;
    if (!c || !bat || !o) return AML_EARG;
    aml_bzero(o, (uint32_t) sizeof *o);
    o->rate = o->remaining = o->voltage = 0xFFFFFFFFu;
    o->design_capacity = o->full_capacity = o->design_voltage = 0xFFFFFFFFu;
    o->cycle_count = o->percent = 0xFFFFFFFFu;
    uint64_t sta = aml_sta(c, bat);
    o->present = (uint32_t) ((sta >> 4) & 1);
    if (!o->present) return AML_OK;
    r = child_eval(c, bat, "_BIX", 0, 0, &v);
    if (!r && v.type == AML_T_PKG && v.len >= 20) {
        o->has_bix = 1;
        elem_u32(c, &v, 1, &o->power_unit);
        elem_u32(c, &v, 2, &o->design_capacity);
        elem_u32(c, &v, 3, &o->full_capacity);
        elem_u32(c, &v, 5, &o->design_voltage);
        elem_u32(c, &v, 8, &o->cycle_count);
        aml_val_free(c, &v);
    } else {
        aml_val_free(c, &v);
        r = child_eval(c, bat, "_BIF", 0, 0, &v);
        if (r) return r;
        if (v.type != AML_T_PKG || v.len < 13) {
            aml_val_free(c, &v);
            return AML_ETYPE;
        }
        elem_u32(c, &v, 0, &o->power_unit);
        elem_u32(c, &v, 1, &o->design_capacity);
        elem_u32(c, &v, 2, &o->full_capacity);
        elem_u32(c, &v, 4, &o->design_voltage);
        aml_val_free(c, &v);
    }
    r = child_eval(c, bat, "_BST", 0, 0, &v);
    if (r) return r;
    if (v.type != AML_T_PKG || v.len < 4) {
        aml_val_free(c, &v);
        return AML_ETYPE;
    }
    elem_u32(c, &v, 0, &o->state);
    elem_u32(c, &v, 1, &o->rate);
    elem_u32(c, &v, 2, &o->remaining);
    elem_u32(c, &v, 3, &o->voltage);
    aml_val_free(c, &v);
    uint32_t full = o->full_capacity;
    if (full && full != 0xFFFFFFFFu && o->remaining != 0xFFFFFFFFu) {
        uint64_t pct = (uint64_t) o->remaining * 100u / full;
        o->percent = pct > 100 ? 100u : (uint32_t) pct;
    }
    return AML_OK;
}

/* ---- thermal ---------------------------------------------------------------------------- */
int aml_thermal_read(aml_ctx_t *c, aml_node_t *tz, aml_thermal_t *o)
{
    uint64_t v;
    if (!c || !tz || !o) return AML_EARG;
    aml_bzero(o, (uint32_t) sizeof *o);
    int r = child_int(c, tz, "_TMP", &v);
    if (r) return r;
    o->tmp = (uint32_t) v;
    if (!child_int(c, tz, "_PSV", &v)) o->psv = (uint32_t) v;
    if (!child_int(c, tz, "_CRT", &v)) o->crt = (uint32_t) v;
    if (!child_int(c, tz, "_HOT", &v)) o->hot = (uint32_t) v;
    return AML_OK;
}

int32_t aml_dk_to_dc(uint32_t dk)
{
    return (int32_t) dk - 2732;
}

int aml_lid_read(aml_ctx_t *c, aml_node_t *lid, uint32_t *open)
{
    uint64_t v;
    if (!c || !lid || !open) return AML_EARG;
    int r = child_int(c, lid, "_LID", &v);
    if (!r) *open = v ? 1u : 0u;
    return r;
}

/* ---- processor performance ------------------------------------------------------------- */
int aml_cpu_pss(aml_ctx_t *c, aml_node_t *cpu, aml_pstate_t *out, uint32_t max)
{
    aml_val_t v;
    if (!c || !cpu) return AML_EARG;
    int r = child_eval(c, cpu, "_PSS", 0, 0, &v);
    if (r) return r;
    if (v.type != AML_T_PKG) {
        aml_val_free(c, &v);
        return AML_ETYPE;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < v.len && !r; i++) {
        const aml_val_t *e = &v.u.elems[i];
        aml_pstate_t p;
        if (e->type != AML_T_PKG || e->len < 6) {
            r = AML_ETYPE;
            break;
        }
        r = elem_u32(c, e, 0, &p.mhz);
        if (!r) r = elem_u32(c, e, 1, &p.mw);
        if (!r) r = elem_u32(c, e, 2, &p.latency);
        if (!r) r = elem_u32(c, e, 3, &p.bm_latency);
        if (!r) r = elem_u32(c, e, 4, &p.control);
        if (!r) r = elem_u32(c, e, 5, &p.status);
        if (!r && out && n < max) out[n] = p;
        if (!r) n++;
    }
    aml_val_free(c, &v);
    return r ? r : (int) n;
}

int aml_cpu_cpc(aml_ctx_t *c, aml_node_t *cpu, uint32_t *revision)
{
    aml_val_t v;
    if (!c || !cpu) return AML_EARG;
    int r = child_eval(c, cpu, "_CPC", 0, 0, &v);
    if (r) return r;
    uint32_t n = 0, rev = 0;
    if (v.type != AML_T_PKG || v.len < 2) {
        r = AML_ETYPE;
    } else {
        r = elem_u32(c, &v, 0, &n);
        if (!r) r = elem_u32(c, &v, 1, &rev);
        if (!r && n != v.len) r = AML_ETYPE; /* NumEntries must count the package */
    }
    aml_val_free(c, &v);
    if (r) return r;
    if (revision) *revision = rev;
    return (int) n;
}

int aml_cpu_ppc(aml_ctx_t *c, aml_node_t *cpu, uint32_t *ppc)
{
    uint64_t v;
    if (!c || !cpu || !ppc) return AML_EARG;
    int r = child_int(c, cpu, "_PPC", &v);
    if (!r) *ppc = (uint32_t) v;
    return r;
}

/* Generic Register descriptor (0x82): the address at byte 7. */
static int gas_addr(const aml_val_t *b, uint64_t *addr)
{
    if (!b || b->type != AML_T_BUF || b->len < 15 || b->u.bytes[0] != 0x82) return 0;
    uint64_t a = 0;
    for (uint32_t i = 0; i < 8; i++) a |= (uint64_t) b->u.bytes[7 + i] << (8 * i);
    *addr = a;
    return 1;
}

int aml_cpu_pct(aml_ctx_t *c, aml_node_t *cpu, uint64_t *ctl, uint64_t *sts)
{
    aml_val_t v;
    uint64_t a = 0, b = 0;
    if (!c || !cpu) return AML_EARG;
    int r = child_eval(c, cpu, "_PCT", 0, 0, &v);
    if (r) return r;
    int ok = v.type == AML_T_PKG && v.len >= 2 && gas_addr(aml_pkg_elem(&v, 0), &a) &&
             gas_addr(aml_pkg_elem(&v, 1), &b);
    aml_val_free(c, &v);
    if (!ok) return AML_ETYPE;
    if (ctl) *ctl = a;
    if (sts) *sts = b;
    return 1;
}

/* ---- backlight ------------------------------------------------------------------------ */
int aml_bcl(aml_ctx_t *c, aml_node_t *dev, uint32_t *levels, uint32_t max, uint32_t *ac,
            uint32_t *dc)
{
    aml_val_t v;
    if (!c || !dev) return AML_EARG;
    int r = child_eval(c, dev, "_BCL", 0, 0, &v);
    if (r) return r;
    if (v.type != AML_T_PKG || v.len < 3) {
        aml_val_free(c, &v);
        return AML_ETYPE;
    }
    uint32_t n = 0, x;
    if (ac && !elem_u32(c, &v, 0, &x)) *ac = x;
    if (dc && !elem_u32(c, &v, 1, &x)) *dc = x;
    for (uint32_t i = 2; i < v.len && !r; i++) {
        r = elem_u32(c, &v, i, &x);
        if (!r && levels && n < max) levels[n] = x;
        if (!r) n++;
    }
    aml_val_free(c, &v);
    return r ? r : (int) n;
}

int aml_bcm(aml_ctx_t *c, aml_node_t *dev, uint32_t level)
{
    aml_val_t a = aml_int(level), ret;
    if (!c || !dev) return AML_EARG;
    int r = child_eval(c, dev, "_BCM", &a, 1, &ret);
    aml_val_free(c, &ret);
    return r;
}

int aml_bqc(aml_ctx_t *c, aml_node_t *dev, uint32_t *level)
{
    uint64_t v;
    if (!c || !dev || !level) return AML_EARG;
    int r = child_int(c, dev, "_BQC", &v);
    if (!r) *level = (uint32_t) v;
    return r;
}
