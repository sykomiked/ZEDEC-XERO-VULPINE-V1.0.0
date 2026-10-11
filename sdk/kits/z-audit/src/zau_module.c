/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zau_module.c — a structural walk of a Wasm module for z-audit: types,
 * imports, functions, globals, exports and code bodies, listing every
 * float and SIMD type or instruction on the way (module.float-simd).
 *
 * It reuses Z-Wasm's LEB128 readers and instruction decoder (zw_wasm.h),
 * so it decodes instructions exactly as the validator and the meter do.
 * The validator (zw_validate) stays the authority on whether a module is
 * acceptable; this walk only collects what the other checks need and
 * stops at the first malformed byte. Integer code only (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zau_internal.h"

typedef struct {
    const uint8_t *b;
    size_t len;
    zau_module *m;
    zau_report *r;
} W;

static int bad(W *w, size_t off, const char *what) {
    if (!w->m->err[0]) {
        snprintf(w->m->err, sizeof w->m->err, "%s", what);
        w->m->err_off = off;
    }
    return -1;
}

static int rd_name(W *w, size_t end, size_t *pos, char *out, int *cut) {
    uint32_t n;
    size_t at = *pos;
    if (zw_rd_u32(w->b, end, pos, &n) != 0 || n > end - *pos) return bad(w, at, "a name is truncated");
    if (!zw_utf8_ok(w->b + *pos, n)) return bad(w, *pos, "a name is not valid UTF-8");
    *cut = zau_clean(out, ZAU_NAME_MAX + 1u, w->b + *pos, n) || n > ZAU_NAME_MAX;
    *pos += n;
    return 0;
}

static int rd_count(W *w, size_t end, size_t *pos, uint32_t *out) {
    size_t at = *pos;
    /* every entry takes at least one byte */
    if (zw_rd_u32(w->b, end, pos, out) != 0 || *out > end - *pos || *out > ZAU_MAX_ITEMS)
        return bad(w, at, "a vector count is malformed or too large");
    return 0;
}

static int rd_limits(W *w, size_t end, size_t *pos) {
    uint8_t flag;
    uint32_t x;
    size_t at = *pos;
    if (zw_rd_u8(w->b, end, pos, &flag) != 0 || zw_rd_u32(w->b, end, pos, &x) != 0)
        return bad(w, at, "limits are truncated");
    if ((flag & 1u) && zw_rd_u32(w->b, end, pos, &x) != 0) return bad(w, at, "limits are truncated");
    return 0;
}

static void float_ins(W *w, const zw_ins *ins, uint32_t fidx, size_t at) {
    int64_t fi = fidx == ZW_NO_FUNC ? -1 : (int64_t) fidx;
    if (zw_is_float_op(ins->op)) {
        if (ins->op >= ZW_OP_FC(0))
            zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "FLOAT_OP", NULL, fi, (int64_t) at,
                    "float instruction 0xFC %u (a saturating float-to-int truncation)",
                    (unsigned) (ins->op - ZW_OP_FC(0)));
        else
            zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "FLOAT_OP", NULL, fi, (int64_t) at,
                    "float instruction (opcode 0x%02X)", (unsigned) ins->op);
    }
    if ((ins->op == 0x02 || ins->op == 0x03 || ins->op == 0x04) && ins->bt_kind == 1 &&
        zau_vt_float((uint8_t) ins->idx))
        zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "FLOAT_TYPE", NULL, fi, (int64_t) at,
                "a block with a %s result", zau_vt_name((uint8_t) ins->idx));
    if (ins->op == 0x1C && zau_vt_float((uint8_t) ins->idx))
        zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "FLOAT_TYPE", NULL, fi, (int64_t) at,
                "a typed select of %s", zau_vt_name((uint8_t) ins->idx));
}

/* Scan instructions in [pos, end) for float/SIMD. A constant expression
 * stops at its first `end` when `const_expr`. Returns the position after
 * the scan, or -1 when it could not continue (SIMD or malformed). */
static int scan(W *w, size_t pos, size_t end, uint32_t fidx, int const_expr, size_t *out_pos) {
    zw_ins ins;
    size_t err = 0;
    while (pos < end) {
        int32_t rc = zw_decode(w->b, end, pos, &ins, &err);
        if (rc == ZW_R_SIMD) {
            zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "SIMD", NULL,
                    fidx == ZW_NO_FUNC ? -1 : (int64_t) fidx, (int64_t) pos,
                    "a SIMD (0xFD) instruction; the rest of this body is not scanned");
            return -1;
        }
        if (rc != ZW_R_OK) return bad(w, err, "an instruction does not decode");
        float_ins(w, &ins, fidx, pos);
        pos += ins.len;
        if (const_expr && ins.op == 0x0B) {
            *out_pos = pos;
            return 0;
        }
    }
    if (const_expr) return bad(w, pos, "a constant expression has no end");
    *out_pos = pos;
    return 0;
}

static int sec_type(W *w, size_t pos, size_t end) {
    zau_module *m = w->m;
    uint32_t n;
    if (rd_count(w, end, &pos, &n)) return -1;
    m->types = (zau_ftype *) calloc(n ? n : 1u, sizeof *m->types);
    if (!m->types) return bad(w, pos, "out of memory");
    m->ntypes = n;
    for (uint32_t i = 0; i < n; i++) {
        zau_ftype *t = &m->types[i];
        size_t at = pos;
        uint8_t form;
        if (zw_rd_u8(w->b, end, &pos, &form) != 0 || form != 0x60) return bad(w, at, "a type is not a func type");
        for (int side = 0; side < 2; side++) {
            uint32_t k;
            if (rd_count(w, end, &pos, &k)) return -1;
            for (uint32_t j = 0; j < k; j++) {
                uint8_t vt;
                if (zw_rd_u8(w->b, end, &pos, &vt) != 0) return bad(w, pos, "a type is truncated");
                if (zau_vt_float(vt) && !t->has_float) {
                    t->has_float = 1;
                    zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "FLOAT_TYPE", NULL, -1, (int64_t) at,
                            "type %u has a %s %s", (unsigned) i, zau_vt_name(vt),
                            side ? "result" : "parameter");
                }
                if (j < ZAU_MAX_SIG) (side ? t->r : t->p)[j] = vt;
            }
            if (side) t->nr = k;
            else t->np = k;
        }
    }
    return pos == end ? 0 : bad(w, pos, "the type section has trailing bytes");
}

static int sec_import(W *w, size_t pos, size_t end) {
    zau_module *m = w->m;
    uint32_t n;
    if (rd_count(w, end, &pos, &n)) return -1;
    m->imports = (zau_import *) calloc(n ? n : 1u, sizeof *m->imports);
    if (!m->imports) return bad(w, pos, "out of memory");
    for (uint32_t i = 0; i < n; i++) {
        zau_import *im = &m->imports[m->nimports];
        int c1 = 0, c2 = 0;
        im->off = pos;
        if (rd_name(w, end, &pos, im->mod, &c1) || rd_name(w, end, &pos, im->name, &c2)) return -1;
        im->cut = c1 || c2;
        if (zw_rd_u8(w->b, end, &pos, &im->kind) != 0) return bad(w, pos, "an import is truncated");
        switch (im->kind) {
        case 0:
            if (zw_rd_u32(w->b, end, &pos, &im->type) != 0) return bad(w, pos, "an import is truncated");
            m->nfimports++;
            break;
        case 1: {
            uint8_t rt;
            if (zw_rd_u8(w->b, end, &pos, &rt) != 0 || rd_limits(w, end, &pos)) return bad(w, pos, "a table import is truncated");
            break;
        }
        case 2:
            if (rd_limits(w, end, &pos)) return -1;
            break;
        case 3: {
            uint8_t vt, mut;
            if (zw_rd_u8(w->b, end, &pos, &vt) != 0 || zw_rd_u8(w->b, end, &pos, &mut) != 0)
                return bad(w, pos, "a global import is truncated");
            im->type = vt;
            if (zau_vt_float(vt))
                zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "FLOAT_TYPE", NULL, -1, (int64_t) im->off,
                        "the global import %s.%s is %s", im->mod, im->name, zau_vt_name(vt));
            break;
        }
        default: return bad(w, pos, "an import has an unknown kind");
        }
        m->nimports++;
    }
    return pos == end ? 0 : bad(w, pos, "the import section has trailing bytes");
}

static int sec_function(W *w, size_t pos, size_t end) {
    zau_module *m = w->m;
    uint32_t n;
    if (rd_count(w, end, &pos, &n)) return -1;
    m->funcs = (uint32_t *) calloc(n ? n : 1u, sizeof *m->funcs);
    if (!m->funcs) return bad(w, pos, "out of memory");
    for (uint32_t i = 0; i < n; i++)
        if (zw_rd_u32(w->b, end, &pos, &m->funcs[i]) != 0) return bad(w, pos, "the function section is truncated");
    m->nfuncs = n;
    return pos == end ? 0 : bad(w, pos, "the function section has trailing bytes");
}

static int sec_global(W *w, size_t pos, size_t end) {
    uint32_t n;
    if (rd_count(w, end, &pos, &n)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t vt, mut;
        size_t at = pos;
        if (zw_rd_u8(w->b, end, &pos, &vt) != 0 || zw_rd_u8(w->b, end, &pos, &mut) != 0)
            return bad(w, pos, "a global is truncated");
        if (zau_vt_float(vt))
            zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "FLOAT_TYPE", NULL, -1, (int64_t) at,
                    "global %u is %s", (unsigned) i, zau_vt_name(vt));
        if (scan(w, pos, end, ZW_NO_FUNC, 1, &pos)) return -1;
    }
    return pos == end ? 0 : bad(w, pos, "the global section has trailing bytes");
}

static int sec_export(W *w, size_t pos, size_t end) {
    zau_module *m = w->m;
    uint32_t n;
    if (rd_count(w, end, &pos, &n)) return -1;
    m->exports = (zau_export *) calloc(n ? n : 1u, sizeof *m->exports);
    if (!m->exports) return bad(w, pos, "out of memory");
    for (uint32_t i = 0; i < n; i++) {
        zau_export *e = &m->exports[m->nexports];
        e->off = pos;
        if (rd_name(w, end, &pos, e->name, &e->cut)) return -1;
        if (zw_rd_u8(w->b, end, &pos, &e->kind) != 0 || zw_rd_u32(w->b, end, &pos, &e->idx) != 0)
            return bad(w, pos, "an export is truncated");
        m->nexports++;
    }
    return pos == end ? 0 : bad(w, pos, "the export section has trailing bytes");
}

static int sec_code(W *w, size_t pos, size_t end) {
    zau_module *m = w->m;
    uint32_t n;
    if (rd_count(w, end, &pos, &n)) return -1;
    m->bodies = (zau_body *) calloc(n ? n : 1u, sizeof *m->bodies);
    if (!m->bodies) return bad(w, pos, "out of memory");
    for (uint32_t i = 0; i < n; i++) {
        zau_body *bd = &m->bodies[i];
        uint32_t size, ngroups, fidx = m->nfimports + i;
        uint64_t total = 0;
        size_t p;
        if (zw_rd_u32(w->b, end, &pos, &size) != 0 || size > end - pos) return bad(w, pos, "a body is truncated");
        bd->start = pos;
        bd->end = pos + size;
        p = pos;
        bd->locals_ok = 1;
        if (zw_rd_u32(w->b, bd->end, &p, &ngroups) != 0) return bad(w, p, "a body's locals are truncated");
        for (uint32_t g = 0; g < ngroups; g++) {
            uint32_t cnt;
            uint8_t vt;
            size_t at = p;
            if (zw_rd_u32(w->b, bd->end, &p, &cnt) != 0 || zw_rd_u8(w->b, bd->end, &p, &vt) != 0)
                return bad(w, p, "a body's locals are truncated");
            if (zau_vt_float(vt)) {
                bd->locals_ok = 0;
                zau_add(w->r, ZAU_C_FLOAT, ZAU_ERROR, "FLOAT_TYPE", NULL, (int64_t) fidx, (int64_t) at,
                        "%u %s local(s)", (unsigned) cnt, zau_vt_name(vt));
            }
            total += cnt;
            for (uint32_t k = 0; k < cnt && bd->nlocals < 256u; k++) bd->local_type[bd->nlocals++] = vt;
        }
        if (total > 256u) bd->locals_ok = 0;
        bd->code = p;
        m->nbodies = i + 1u;
        if (scan(w, p, bd->end, fidx, 0, &p) != 0 && w->m->err[0]) return -1;
        pos = bd->end;
    }
    return pos == end ? 0 : bad(w, pos, "the code section has trailing bytes");
}

void zau_module_read(zau_module *m, const uint8_t *b, size_t len, zau_report *r) {
    static const uint8_t magic[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    W w;
    size_t pos = 8;
    uint32_t seen = 0;
    memset(m, 0, sizeof *m);
    w.b = b;
    w.len = len;
    w.m = m;
    w.r = r;
    if (len < 8u || memcmp(b, magic, 8) != 0) {
        (void) bad(&w, 0, "not a Wasm 1.0 binary (magic or version)");
        return;
    }
    while (pos < len) {
        uint8_t id;
        uint32_t size;
        size_t at = pos, end;
        int rc = 0;
        if (zw_rd_u8(b, len, &pos, &id) != 0 || zw_rd_u32(b, len, &pos, &size) != 0 || size > len - pos) {
            (void) bad(&w, at, "a section header is truncated");
            return;
        }
        end = pos + size;
        if (id > 12u) {
            (void) bad(&w, at, "an unknown section id");
            return;
        }
        if (id != SEC_CUSTOM) {
            if (seen & (1u << id)) {
                (void) bad(&w, at, "a section appears twice");
                return;
            }
            seen |= 1u << id;
        }
        switch (id) {
        case SEC_TYPE: rc = sec_type(&w, pos, end); break;
        case SEC_IMPORT: rc = sec_import(&w, pos, end); break;
        case SEC_FUNCTION: rc = sec_function(&w, pos, end); break;
        case SEC_GLOBAL: rc = sec_global(&w, pos, end); break;
        case SEC_EXPORT: rc = sec_export(&w, pos, end); break;
        case SEC_CODE: rc = sec_code(&w, pos, end); break;
        default: break; /* custom, table, memory, start, element, data, data count */
        }
        if (rc) return;
        pos = end;
    }
    m->parsed = 1;
}

void zau_module_free(zau_module *m) {
    free(m->types);
    free(m->imports);
    free(m->funcs);
    free(m->exports);
    free(m->bodies);
    memset(m, 0, sizeof *m);
}

const zau_ftype *zau_func_type(const zau_module *m, uint32_t fidx) {
    uint32_t ti;
    if (fidx < m->nfimports) {
        uint32_t k = 0;
        for (uint32_t i = 0; i < m->nimports; i++) {
            if (m->imports[i].kind != 0) continue;
            if (k++ == fidx) {
                ti = m->imports[i].type;
                return ti < m->ntypes ? &m->types[ti] : NULL;
            }
        }
        return NULL;
    }
    fidx -= m->nfimports;
    if (fidx >= m->nfuncs) return NULL;
    ti = m->funcs[fidx];
    return ti < m->ntypes ? &m->types[ti] : NULL;
}
