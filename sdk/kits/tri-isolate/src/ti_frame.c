/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_frame.c — encoders and strict decoders for the Tri-Isolate frames. */
#include "ti_frame.h"

#include <stdlib.h>
#include <string.h>

#include "zwasm.h"
#include "zxv_api.h"

/* ----- output ----- */
void ti_buf_free(ti_buf *b) {
    if (!b) return;
    free(b->b);
    b->b = NULL;
    b->n = b->cap = 0;
}

static int grow(ti_buf *b, size_t more) {
    if (more > SIZE_MAX - b->n) return 0;
    if (b->n + more <= b->cap) return 1;
    size_t c = b->cap ? b->cap : 256u;
    while (c < b->n + more) {
        if (c > SIZE_MAX / 2u) return 0;
        c *= 2u;
    }
    uint8_t *p = (uint8_t *) realloc(b->b, c);
    if (!p) return 0;
    b->b = p;
    b->cap = c;
    return 1;
}

typedef struct {
    ti_buf *b;
    size_t start; /* where this frame's length field is */
    int ok;
} wr;

static void w_bytes(wr *w, const void *p, size_t n) {
    if (!w->ok) return;
    if (!grow(w->b, n)) {
        w->ok = 0;
        return;
    }
    if (n) memcpy(w->b->b + w->b->n, p, n);
    w->b->n += n;
}

static void w_u8(wr *w, uint32_t v) {
    uint8_t c = (uint8_t) v;
    w_bytes(w, &c, 1);
}

static void w_u16(wr *w, uint32_t v) {
    uint8_t c[2] = {(uint8_t) v, (uint8_t) (v >> 8)};
    w_bytes(w, c, 2);
}

static void w_u32(wr *w, uint32_t v) {
    uint8_t c[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
    w_bytes(w, c, 4);
}

static void w_u64(wr *w, uint64_t v) {
    uint8_t c[8];
    for (int i = 0; i < 8; i++) c[i] = (uint8_t) (v >> (8 * i));
    w_bytes(w, c, 8);
}

static void w_begin(wr *w, ti_buf *b, uint32_t type) {
    w->b = b;
    w->start = b->n;
    w->ok = 1;
    w_u32(w, 0);
    w_u8(w, type);
}

static int32_t w_end(wr *w) {
    if (!w->ok) {
        w->b->n = w->start;
        return ZXV_E_NOMEM;
    }
    size_t len = w->b->n - w->start - 4u;
    if (len > TI_REQ_MAX) {
        w->b->n = w->start;
        return ZXV_E_ARG;
    }
    uint8_t *p = w->b->b + w->start;
    p[0] = (uint8_t) len;
    p[1] = (uint8_t) (len >> 8);
    p[2] = (uint8_t) (len >> 16);
    p[3] = (uint8_t) (len >> 24);
    return ZXV_OK;
}

/* ----- input ----- */
typedef struct {
    const uint8_t *p;
    size_t n, i;
    int ok;
} rd;

static int r_has(rd *r, size_t n) {
    if (!r->ok || r->n - r->i < n) {
        r->ok = 0;
        return 0;
    }
    return 1;
}

static uint32_t r_u8(rd *r) {
    if (!r_has(r, 1)) return 0;
    return r->p[r->i++];
}

static uint32_t r_u16(rd *r) {
    if (!r_has(r, 2)) return 0;
    uint32_t v = (uint32_t) r->p[r->i] | ((uint32_t) r->p[r->i + 1] << 8);
    r->i += 2;
    return v;
}

static uint32_t r_u32(rd *r) {
    if (!r_has(r, 4)) return 0;
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) v |= (uint32_t) r->p[r->i + (size_t) k] << (8 * k);
    r->i += 4;
    return v;
}

static uint64_t r_u64(rd *r) {
    if (!r_has(r, 8)) return 0;
    uint64_t v = 0;
    for (int k = 0; k < 8; k++) v |= (uint64_t) r->p[r->i + (size_t) k] << (8 * k);
    r->i += 8;
    return v;
}

static int64_t r_i64(rd *r) {
    uint64_t u = r_u64(r);
    int64_t v;
    memcpy(&v, &u, sizeof v); /* two's complement, no implementation-defined cast */
    return v;
}

static int32_t r_i32(rd *r) {
    uint32_t u = r_u32(r);
    int32_t v;
    memcpy(&v, &u, sizeof v);
    return v;
}

static const uint8_t *r_take(rd *r, size_t n) {
    if (!r_has(r, n)) return NULL;
    const uint8_t *p = r->p + r->i;
    r->i += n;
    return p;
}

static int32_t r_done(const rd *r) { return (r->ok && r->i == r->n) ? ZXV_OK : ZXV_E_FORMAT; }

static uint64_t u64_of(int64_t v) {
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

static uint32_t u32_of(int32_t v) {
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

/* ----- field rules ----- */
static int printable(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || (unsigned char) s[i] > 0x7Eu) return 0;
    return 1;
}

static int no_nul(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (s[i] == '\0') return 0;
    return 1;
}

static uint64_t gcd_u64(uint64_t a, uint64_t b) {
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static uint64_t mag(int64_t v) { return v < 0 ? (uint64_t) 0 - u64_of(v) : (uint64_t) v; }

static int normalised(int64_t num, int64_t den) {
    if (den <= 0) return 0;
    if (num == 0) return den == 1;
    return gcd_u64(mag(num), (uint64_t) den) == 1u;
}

int32_t ti_loaded_status_ok(int32_t s) {
    return s == ZXV_OK || s == ZW_E_REJECTED || s == ZW_E_LINK || s == ZXV_E_NOMEM || s == ZXV_E_ARG;
}

int32_t ti_result_status_ok(int32_t s) {
    return s == ZXV_OK || s == ZW_E_TRAP || s == ZW_E_FUEL || s == ZW_E_EXPORT || s == ZXV_E_RANGE ||
           s == ZXV_E_ARG || s == ZXV_E_BUDGET;
}

/* Statuses after which nothing ran: no fuel may be reported. */
static int nothing_ran(int32_t s) { return s == ZW_E_EXPORT || s == ZXV_E_RANGE || s == ZXV_E_ARG || s == ZXV_E_BUDGET; }

uint32_t ti_msg_clean(char dst[TI_MSGTXT_MAX + 1u], const char *src) {
    uint32_t n = 0;
    if (src)
        for (; src[n] && n < TI_MSGTXT_MAX; n++) {
            unsigned char c = (unsigned char) src[n];
            dst[n] = (c >= 0x20u && c <= 0x7Eu) ? (char) c : '?';
        }
    dst[n] = '\0';
    return n;
}

int32_t ti_frame_header(const uint8_t hdr[4], uint32_t max_len, uint32_t *out_len) {
    uint32_t n = (uint32_t) hdr[0] | ((uint32_t) hdr[1] << 8) | ((uint32_t) hdr[2] << 16) |
                 ((uint32_t) hdr[3] << 24);
    if (n == 0 || n > max_len) return ZXV_E_FORMAT;
    *out_len = n;
    return ZXV_OK;
}

/* ----- HELLO ----- */
int32_t ti_enc_hello(ti_buf *out, const ti_hello *m) {
    wr w;
    if (!out || !m) return ZXV_E_ARG;
    w_begin(&w, out, TI_MSG_HELLO);
    w_u16(&w, m->version);
    w_u16(&w, m->flags);
    return w_end(&w);
}

int32_t ti_dec_hello(const uint8_t *body, size_t len, ti_hello *m) {
    rd r = {body, len, 0, body != NULL || len == 0};
    m->version = (uint16_t) r_u16(&r);
    m->flags = (uint16_t) r_u16(&r);
    if (r.ok && (m->flags & ~TI_HELLO_SECCOMP)) return ZXV_E_FORMAT;
    return r_done(&r);
}

/* ----- LOAD ----- */
int32_t ti_enc_load(ti_buf *out, const ti_load *m) {
    wr w;
    if (!out || !m || m->name_len > TI_NAME_MAX || !printable(m->name, m->name_len) || !m->wasm ||
        m->wasm_len == 0 || m->wasm_len > TI_WASM_MAX)
        return ZXV_E_ARG;
    w_begin(&w, out, TI_MSG_LOAD);
    w_u32(&w, m->grants);
    for (uint32_t i = 0; i < TI_LIMITS_COUNT; i++) w_u64(&w, m->limits[i]);
    w_u8(&w, m->name_len);
    w_bytes(&w, m->name, m->name_len);
    w_u32(&w, (uint32_t) m->wasm_len);
    w_bytes(&w, m->wasm, m->wasm_len);
    return w_end(&w);
}

int32_t ti_dec_load(const uint8_t *body, size_t len, ti_load *m) {
    rd r = {body, len, 0, body != NULL || len == 0};
    const uint8_t *p;
    memset(m, 0, sizeof *m);
    m->grants = r_u32(&r);
    for (uint32_t i = 0; i < TI_LIMITS_COUNT; i++) m->limits[i] = r_u64(&r);
    m->name_len = r_u8(&r);
    if (m->name_len > TI_NAME_MAX) return ZXV_E_FORMAT;
    p = r_take(&r, m->name_len);
    if (!r.ok) return ZXV_E_FORMAT;
    memcpy(m->name, p, m->name_len);
    m->name[m->name_len] = '\0';
    if (!printable(m->name, m->name_len)) return ZXV_E_FORMAT;
    m->wasm_len = r_u32(&r);
    if (!r.ok || m->wasm_len == 0 || m->wasm_len > TI_WASM_MAX) return ZXV_E_FORMAT;
    m->wasm = r_take(&r, m->wasm_len);
    return r_done(&r);
}

/* ----- LOADED ----- */
int32_t ti_enc_loaded(ti_buf *out, const ti_loaded *m) {
    wr w;
    if (!out || !m || m->msg_len > TI_MSGTXT_MAX || !printable(m->msg, m->msg_len) ||
        !ti_loaded_status_ok(m->status))
        return ZXV_E_ARG;
    w_begin(&w, out, TI_MSG_LOADED);
    w_u32(&w, u32_of(m->status));
    w_u16(&w, m->msg_len);
    w_bytes(&w, m->msg, m->msg_len);
    return w_end(&w);
}

int32_t ti_dec_loaded(const uint8_t *body, size_t len, ti_loaded *m) {
    rd r = {body, len, 0, body != NULL || len == 0};
    const uint8_t *p;
    m->status = r_i32(&r);
    m->msg_len = r_u16(&r);
    if (!r.ok || !ti_loaded_status_ok(m->status) || m->msg_len > TI_MSGTXT_MAX) return ZXV_E_FORMAT;
    p = r_take(&r, m->msg_len);
    if (!r.ok) return ZXV_E_FORMAT;
    memcpy(m->msg, p, m->msg_len);
    m->msg[m->msg_len] = '\0';
    if (!printable(m->msg, m->msg_len)) return ZXV_E_FORMAT;
    return r_done(&r);
}

/* ----- CALL ----- */
int32_t ti_enc_call(ti_buf *out, const ti_call *m) {
    wr w;
    if (!out || !m || m->fuel == 0 || m->export_len == 0 || m->export_len > TI_EXPORT_MAX ||
        !no_nul(m->export_name, m->export_len) || m->nargs > TI_ARGS_MAX)
        return ZXV_E_ARG;
    w_begin(&w, out, TI_MSG_CALL);
    w_u64(&w, m->fuel);
    w_u8(&w, m->export_len);
    w_bytes(&w, m->export_name, m->export_len);
    w_u8(&w, m->nargs);
    for (uint32_t i = 0; i < m->nargs; i++) w_u64(&w, u64_of(m->args[i]));
    return w_end(&w);
}

int32_t ti_dec_call(const uint8_t *body, size_t len, ti_call *m) {
    rd r = {body, len, 0, body != NULL || len == 0};
    const uint8_t *p;
    memset(m, 0, sizeof *m);
    m->fuel = r_u64(&r);
    m->export_len = r_u8(&r);
    if (!r.ok || m->fuel == 0 || m->export_len == 0 || m->export_len > TI_EXPORT_MAX) return ZXV_E_FORMAT;
    p = r_take(&r, m->export_len);
    if (!r.ok) return ZXV_E_FORMAT;
    memcpy(m->export_name, p, m->export_len);
    m->export_name[m->export_len] = '\0';
    if (!no_nul(m->export_name, m->export_len)) return ZXV_E_FORMAT;
    m->nargs = r_u8(&r);
    if (!r.ok || m->nargs > TI_ARGS_MAX) return ZXV_E_FORMAT;
    for (uint32_t i = 0; i < m->nargs; i++) m->args[i] = r_i64(&r);
    return r_done(&r);
}

/* ----- RESULT ----- */
static int result_ok(const ti_result *m) {
    if (!ti_result_status_ok(m->status) || (m->has_result != 0 && m->has_result != 1)) return 0;
    if (!m->has_result && m->result != 0) return 0;
    if (m->status != ZXV_OK && (m->has_result || m->nprops)) return 0;
    if (nothing_ran(m->status) && m->fuel_used != 0) return 0;
    if (m->nprops > TI_PROPS_MAX || m->msg_len > TI_MSGTXT_MAX || !printable(m->msg, m->msg_len)) return 0;
    for (uint32_t i = 0; i < m->nprops; i++)
        if (!normalised(m->props[i].num, m->props[i].den) || m->props[i].label_len > TI_LABEL_MAX) return 0;
    return 1;
}

int32_t ti_enc_result(ti_buf *out, const ti_result *m) {
    wr w;
    if (!out || !m || !result_ok(m)) return ZXV_E_ARG;
    w_begin(&w, out, TI_MSG_RESULT);
    w_u32(&w, u32_of(m->status));
    w_u8(&w, (uint32_t) m->has_result);
    w_u64(&w, u64_of(m->result));
    w_u64(&w, m->fuel_used);
    w_u8(&w, m->nprops);
    for (uint32_t i = 0; i < m->nprops; i++) {
        const ti_prop *p = &m->props[i];
        w_u32(&w, p->kind);
        w_u64(&w, u64_of(p->num));
        w_u64(&w, u64_of(p->den));
        w_u8(&w, p->label_len);
        w_bytes(&w, p->label, p->label_len);
    }
    w_u16(&w, m->msg_len);
    w_bytes(&w, m->msg, m->msg_len);
    return w_end(&w);
}

int32_t ti_dec_result(const uint8_t *body, size_t len, ti_result *m) {
    rd r = {body, len, 0, body != NULL || len == 0};
    const uint8_t *p;
    m->status = r_i32(&r);
    m->has_result = (int32_t) r_u8(&r);
    m->result = r_i64(&r);
    m->fuel_used = r_u64(&r);
    m->nprops = r_u8(&r);
    if (!r.ok || m->nprops > TI_PROPS_MAX) return ZXV_E_FORMAT;
    for (uint32_t i = 0; i < m->nprops; i++) {
        ti_prop *q = &m->props[i];
        q->kind = r_u32(&r);
        q->num = r_i64(&r);
        q->den = r_i64(&r);
        q->label_len = r_u8(&r);
        if (!r.ok || q->label_len > TI_LABEL_MAX) return ZXV_E_FORMAT;
        p = r_take(&r, q->label_len);
        if (!r.ok) return ZXV_E_FORMAT;
        if (q->label_len) memcpy(q->label, p, q->label_len);
    }
    m->msg_len = r_u16(&r);
    if (!r.ok || m->msg_len > TI_MSGTXT_MAX) return ZXV_E_FORMAT;
    p = r_take(&r, m->msg_len);
    if (!r.ok) return ZXV_E_FORMAT;
    memcpy(m->msg, p, m->msg_len);
    m->msg[m->msg_len] = '\0';
    if (r_done(&r) != ZXV_OK || !result_ok(m)) return ZXV_E_FORMAT;
    return ZXV_OK;
}

/* ----- QUIT ----- */
int32_t ti_enc_quit(ti_buf *out) {
    wr w;
    if (!out) return ZXV_E_ARG;
    w_begin(&w, out, TI_MSG_QUIT);
    return w_end(&w);
}

int32_t ti_dec_quit(const uint8_t *body, size_t len) {
    (void) body;
    return len == 0 ? ZXV_OK : ZXV_E_FORMAT;
}
