/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_evlog.c — the evidence log: append-only, hash-chained with SHA3-256
 * (zxv_sha3_256). Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "zc_internal.h"

#define EVLOG_MAX_CAP (UINT32_C(1) << 20)

static const char k_dom[] = "zxv/zc-evlog/v1";

static void le(uint8_t *p, uint64_t v, int n) {
    for (int i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static int32_t rec_hash(const zc_rec *r, uint8_t out[32]) {
    uint8_t b[sizeof k_dom - 1u + 8u + 32u + 20u + 64u + 4u + ZC_EVLOG_TEXT_MAX];
    size_t k = sizeof k_dom - 1u;
    memcpy(b, k_dom, k);
    le(b + k, r->seq, 8);
    k += 8;
    memcpy(b + k, r->prev, 32);
    k += 32;
    le(b + k, r->origin, 4);
    le(b + k + 4, (uint64_t) (uint32_t) r->part, 4);
    le(b + k + 8, r->index, 4);
    le(b + k + 12, (uint64_t) (uint32_t) r->reason, 4);
    le(b + k + 16, (uint64_t) (uint32_t) r->tag, 4);
    k += 20;
    memcpy(b + k, r->contract, 32);
    memcpy(b + k + 32, r->subject, 32);
    k += 64;
    le(b + k, r->text_len, 4);
    k += 4;
    memcpy(b + k, r->text, r->text_len);
    k += r->text_len;
    return zxv_sha3_256(b, k, out);
}

int32_t zc_evlog_create(uint32_t capacity, zc_evlog **out_log) {
    zc_evlog *l;
    if (!out_log || capacity == 0 || capacity > EVLOG_MAX_CAP) {
        zc_set_error("zc_evlog_create: capacity must be 1 .. 2^20");
        return ZXV_E_ARG;
    }
    l = (zc_evlog *) calloc(1, sizeof *l);
    if (!l) return ZXV_E_NOMEM;
    l->cap = capacity;
    *out_log = l;
    return ZXV_OK;
}

void zc_evlog_destroy(zc_evlog *log) {
    if (!log) return;
    free(log->rec);
    free(log);
}

int32_t zc_evlog_set_sink(zc_evlog *log, zc_evlog_sink fn, void *user) {
    if (!log) return ZXV_E_ARG;
    log->sink = fn;
    log->sink_user = fn ? user : NULL;
    return ZXV_OK;
}

int32_t zc_evlog_append(zc_evlog *log, uint32_t origin, int32_t part, uint32_t index,
                        int32_t reason, int32_t tag, const uint8_t contract[32],
                        const uint8_t subject[32], const char *text, uint64_t *out_seq) {
    zc_rec r;
    int32_t st;
    if (!log) return ZXV_E_ARG;
    if (log->n >= log->cap) {
        zc_set_errorf("evidence log full (%u records): refusing; the caller must stop",
                      (unsigned) log->cap);
        return ZXV_E_CAPACITY;
    }
    if (log->n == log->alloc) {
        uint32_t nc = log->alloc ? log->alloc * 2u : 16u;
        if (nc > log->cap) nc = log->cap;
        zc_rec *nr = (zc_rec *) realloc(log->rec, (size_t) nc * sizeof *nr);
        if (!nr) return ZXV_E_NOMEM;
        log->rec = nr;
        log->alloc = nc;
    }
    memset(&r, 0, sizeof r);
    r.seq = (uint64_t) log->n + 1u;
    r.origin = origin;
    r.part = part;
    r.index = index;
    r.reason = reason;
    r.tag = tag;
    if (contract) memcpy(r.contract, contract, 32);
    if (subject) memcpy(r.subject, subject, 32);
    if (text) {
        size_t n = zc_utf8_cut(text, strlen(text), ZC_EVLOG_TEXT_MAX);
        memcpy(r.text, text, n);
        r.text_len = (uint32_t) n;
    }
    memcpy(r.prev, log->head, 32);
    st = rec_hash(&r, r.hash);
    if (st != ZXV_OK) return st;
    if (log->sink && log->sink(log->sink_user, r.seq, r.origin, r.tag, r.reason, r.subject,
                               r.hash, r.text) != 0) {
        zc_set_error("the evidence sink refused the record: the caller must stop");
        return ZC_E_LOG;
    }
    log->rec[log->n++] = r;
    memcpy(log->head, r.hash, 32);
    if (out_seq) *out_seq = r.seq;
    return ZXV_OK;
}

int32_t zc_evlog_count(const zc_evlog *log, uint64_t *out_count) {
    if (!log || !out_count) return ZXV_E_ARG;
    *out_count = log->n;
    return ZXV_OK;
}

int32_t zc_evlog_head(const zc_evlog *log, uint8_t out[32]) {
    if (!log || !out) return ZXV_E_ARG;
    memcpy(out, log->head, 32);
    return ZXV_OK;
}

int32_t zc_evlog_get(const zc_evlog *log, uint64_t seq, uint32_t *out_origin, int32_t *out_part,
                     uint32_t *out_index, int32_t *out_reason, int32_t *out_tag,
                     uint8_t out_contract[32], uint8_t out_subject[32], uint8_t out_hash[32],
                     char *text, size_t text_cap, size_t *out_text_len) {
    const zc_rec *r;
    if (!log) return ZXV_E_ARG;
    if (seq == 0 || seq > log->n) return ZXV_E_NOT_FOUND;
    r = &log->rec[seq - 1u];
    if (out_text_len) *out_text_len = r->text_len;
    if (!text || text_cap <= r->text_len) return ZXV_E_BUFFER;
    memcpy(text, r->text, r->text_len);
    text[r->text_len] = '\0';
    if (out_origin) *out_origin = r->origin;
    if (out_part) *out_part = r->part;
    if (out_index) *out_index = r->index;
    if (out_reason) *out_reason = r->reason;
    if (out_tag) *out_tag = r->tag;
    if (out_contract) memcpy(out_contract, r->contract, 32);
    if (out_subject) memcpy(out_subject, r->subject, 32);
    if (out_hash) memcpy(out_hash, r->hash, 32);
    return ZXV_OK;
}

int32_t zc_evlog_verify(const zc_evlog *log) {
    uint8_t prev[32], h[32];
    if (!log) return ZXV_E_ARG;
    memset(prev, 0, sizeof prev);
    for (uint32_t i = 0; i < log->n; i++) {
        const zc_rec *r = &log->rec[i];
        if (r->seq != (uint64_t) i + 1u || memcmp(r->prev, prev, 32) != 0 ||
            r->text_len > ZC_EVLOG_TEXT_MAX || rec_hash(r, h) != ZXV_OK ||
            memcmp(h, r->hash, 32) != 0) {
            zc_set_errorf("evidence record %u does not verify (altered, reordered or removed)",
                          (unsigned) i + 1u);
            return ZXV_E_VERIFY;
        }
        memcpy(prev, r->hash, 32);
    }
    if (memcmp(prev, log->head, 32) != 0) {
        zc_set_error("the evidence log head does not match its last record (truncated)");
        return ZXV_E_VERIFY;
    }
    return ZXV_OK;
}
