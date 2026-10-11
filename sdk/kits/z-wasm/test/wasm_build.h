/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* wasm_build.h — a tiny WebAssembly binary assembler for the Z-Wasm tests,
 * so the tests need no external toolchain. Header-only, test use only. */
#ifndef ZW_WASM_BUILD_H
#define ZW_WASM_BUILD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t b[1 << 16];
    size_t n;
} wb;

static inline void wb_reset(wb *w) { w->n = 0; }

static inline void wb_u8(wb *w, uint8_t v) {
    if (w->n >= sizeof w->b) {
        fprintf(stderr, "wasm_build: buffer full\n");
        abort();
    }
    w->b[w->n++] = v;
}

static inline void wb_bytes(wb *w, const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) wb_u8(w, ((const uint8_t *) p)[i]);
}

static inline void wb_uleb(wb *w, uint64_t v) {
    do {
        uint8_t c = (uint8_t) (v & 0x7Fu);
        v >>= 7;
        if (v) c |= 0x80u;
        wb_u8(w, c);
    } while (v);
}

static inline void wb_sleb(wb *w, int64_t v) {
    for (;;) {
        uint8_t c = (uint8_t) ((uint64_t) v & 0x7Fu);
        v = v < 0 ? ~(~v >> 7) : v >> 7; /* arithmetic shift without UB */
        if ((v == 0 && !(c & 0x40u)) || (v == -1 && (c & 0x40u))) {
            wb_u8(w, c);
            return;
        }
        wb_u8(w, (uint8_t) (c | 0x80u));
    }
}

static inline void wb_name(wb *w, const char *s) {
    size_t n = strlen(s);
    wb_uleb(w, n);
    wb_bytes(w, s, n);
}

static inline void wb_header(wb *w) {
    static const uint8_t h[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    wb_reset(w);
    wb_bytes(w, h, 8);
}

/* Append section `id` with content `c`. */
static inline void wb_section(wb *w, uint8_t id, const wb *c) {
    wb_u8(w, id);
    wb_uleb(w, c->n);
    wb_bytes(w, c->b, c->n);
}

/* Append a section from raw bytes. */
static inline void wb_section_raw(wb *w, uint8_t id, const uint8_t *p, size_t n) {
    wb_u8(w, id);
    wb_uleb(w, n);
    wb_bytes(w, p, n);
}

/* ----- A configurable one-module template -----
 * Enough knobs to place any instruction or section the tests need. */
typedef struct {
    /* types: each is params then results, as value-type strings over
     * "iIfFv" (i32 i64 f32 f64 v128), e.g. "ii:i" */
    const char *types[8];
    int ntypes;
    /* function imports: module, name, type index */
    struct { const char *mod, *name; int type; } fimports[4];
    int nfimports;
    /* raw import entries appended after fimports (module/name/kind/desc) */
    const uint8_t *raw_imports;
    size_t raw_imports_len;
    int raw_imports_count;
    /* defined functions: type index, locals ("i" per local), body bytes
     * WITHOUT the final end (it is appended) */
    struct { int type; const char *locals; const uint8_t *code; size_t code_len; } funcs[4];
    int nfuncs;
    int memory;         /* 0 none, 1 = limits below                          */
    uint8_t mem_flag;   /* default 1 (min, max)                              */
    uint32_t mem_min, mem_max;
    int table;          /* 0 none, 1 funcref table                           */
    uint8_t table_flag;
    uint32_t table_min, table_max;
    const char *globals; /* one mutable global per char: i i32, I i64, f f32, */
                         /* F f64 (each initialised to zero)                  */
    const char *exports[4]; /* exported function names (function index = k) */
    int nexports;
    int start;          /* 1 = add a start section naming function 0         */
    int datacount;      /* -1 none, else count (with that many passive data)  */
} wt;

static inline uint8_t wt_vt(char c) {
    switch (c) {
    case 'i': return 0x7F;
    case 'I': return 0x7E;
    case 'f': return 0x7D;
    case 'F': return 0x7C;
    case 'v': return 0x7B;
    default: return 0x7F;
    }
}

static inline void wt_build(wb *m, const wt *t) {
    wb s;
    wb_header(m);
    if (t->ntypes) {
        wb_reset(&s);
        wb_uleb(&s, (uint64_t) t->ntypes);
        for (int i = 0; i < t->ntypes; i++) {
            const char *ty = t->types[i];
            const char *colon = strchr(ty, ':');
            size_t np = colon ? (size_t) (colon - ty) : strlen(ty);
            wb_u8(&s, 0x60);
            wb_uleb(&s, np);
            for (size_t k = 0; k < np; k++) wb_u8(&s, wt_vt(ty[k]));
            if (colon) {
                size_t nr = strlen(colon + 1);
                wb_uleb(&s, nr);
                for (size_t k = 0; k < nr; k++) wb_u8(&s, wt_vt(colon[1 + k]));
            } else {
                wb_uleb(&s, 0);
            }
        }
        wb_section(m, 1, &s);
    }
    if (t->nfimports || t->raw_imports_count) {
        wb_reset(&s);
        wb_uleb(&s, (uint64_t) (t->nfimports + t->raw_imports_count));
        for (int i = 0; i < t->nfimports; i++) {
            wb_name(&s, t->fimports[i].mod);
            wb_name(&s, t->fimports[i].name);
            wb_u8(&s, 0);
            wb_uleb(&s, (uint64_t) t->fimports[i].type);
        }
        wb_bytes(&s, t->raw_imports, t->raw_imports_len);
        wb_section(m, 2, &s);
    }
    if (t->nfuncs) {
        wb_reset(&s);
        wb_uleb(&s, (uint64_t) t->nfuncs);
        for (int i = 0; i < t->nfuncs; i++) wb_uleb(&s, (uint64_t) t->funcs[i].type);
        wb_section(m, 3, &s);
    }
    if (t->table) {
        wb_reset(&s);
        wb_uleb(&s, 1);
        wb_u8(&s, 0x70);
        wb_u8(&s, t->table_flag);
        wb_uleb(&s, t->table_min);
        if (t->table_flag & 1) wb_uleb(&s, t->table_max);
        wb_section(m, 4, &s);
    }
    if (t->memory) {
        wb_reset(&s);
        wb_uleb(&s, 1);
        wb_u8(&s, t->mem_flag);
        wb_uleb(&s, t->mem_min);
        if (t->mem_flag & 1) wb_uleb(&s, t->mem_max);
        wb_section(m, 5, &s);
    }
    if (t->globals && t->globals[0]) {
        size_t ng = strlen(t->globals);
        wb_reset(&s);
        wb_uleb(&s, ng);
        for (size_t i = 0; i < ng; i++) {
            char c = t->globals[i];
            wb_u8(&s, wt_vt(c));
            wb_u8(&s, 1); /* mutable */
            switch (c) {
            case 'i': wb_u8(&s, 0x41); wb_u8(&s, 0); break;
            case 'I': wb_u8(&s, 0x42); wb_u8(&s, 0); break;
            case 'f': wb_u8(&s, 0x43); for (int k = 0; k < 4; k++) wb_u8(&s, 0); break;
            default:  wb_u8(&s, 0x44); for (int k = 0; k < 8; k++) wb_u8(&s, 0); break;
            }
            wb_u8(&s, 0x0B);
        }
        wb_section(m, 6, &s);
    }
    if (t->nexports) {
        wb_reset(&s);
        wb_uleb(&s, (uint64_t) t->nexports);
        for (int i = 0; i < t->nexports; i++) {
            wb_name(&s, t->exports[i]);
            wb_u8(&s, 0);
            wb_uleb(&s, (uint64_t) (t->nfimports + i));
        }
        wb_section(m, 7, &s);
    }
    if (t->start) {
        wb_reset(&s);
        wb_uleb(&s, (uint64_t) t->nfimports);
        wb_section(m, 8, &s);
    }
    if (t->datacount >= 0 && t->datacount) {
        wb_reset(&s);
        wb_uleb(&s, (uint64_t) t->datacount);
        wb_section(m, 12, &s);
    }
    if (t->nfuncs) {
        wb_reset(&s);
        wb_uleb(&s, (uint64_t) t->nfuncs);
        for (int i = 0; i < t->nfuncs; i++) {
            wb body;
            wb_reset(&body);
            const char *lc = t->funcs[i].locals ? t->funcs[i].locals : "";
            size_t nl = strlen(lc);
            wb_uleb(&body, nl);
            for (size_t k = 0; k < nl; k++) {
                wb_uleb(&body, 1);
                wb_u8(&body, wt_vt(lc[k]));
            }
            wb_bytes(&body, t->funcs[i].code, t->funcs[i].code_len);
            wb_u8(&body, 0x0B);
            wb_uleb(&s, body.n);
            wb_bytes(&s, body.b, body.n);
        }
        wb_section(m, 10, &s);
    }
    if (t->datacount > 0) {
        wb_reset(&s);
        wb_uleb(&s, (uint64_t) t->datacount);
        for (int i = 0; i < t->datacount; i++) {
            wb_u8(&s, 1); /* passive */
            wb_uleb(&s, 2);
            wb_u8(&s, 0xAB);
            wb_u8(&s, 0xCD);
        }
        wb_section(m, 11, &s);
    }
}

/* A template with one ()->() function whose body is `code` (+ end). */
static inline void wt_one(wt *t, const uint8_t *code, size_t len) {
    memset(t, 0, sizeof *t);
    t->types[0] = "";
    t->ntypes = 1;
    t->funcs[0].type = 0;
    t->funcs[0].code = code;
    t->funcs[0].code_len = len;
    t->nfuncs = 1;
    t->datacount = -1;
    t->mem_flag = 1;
    t->table_flag = 1;
}

#endif /* ZW_WASM_BUILD_H */
