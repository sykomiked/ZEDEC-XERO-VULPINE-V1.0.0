/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zind_qsink.c — 64-state tag helpers and the quarantine sink that writes
 * Forbidden and Both events into the libzxv quarantine log (zxv_hexlog).
 * Integer code only (R11). */
#include <stdlib.h>

#include "zind_common.h"

int32_t zind_tag(int32_t lp, uint8_t polarity, uint8_t phase, uint8_t *out_tag) {
    uint8_t base = 0;
    int32_t st;
    if (!out_tag) return ZXV_E_ARG;
    st = zxv_hex_from_lp(lp, &base);
    if (st != ZXV_OK) return st;
    return zxv_hex_pack(base, polarity, phase, out_tag);
}

int32_t zind_tag_lp(uint8_t tag, int32_t *out_lp) {
    uint8_t base = 0;
    int32_t st;
    if (!out_lp) return ZXV_E_ARG;
    st = zxv_hex_unpack(tag, &base, NULL, NULL);
    if (st != ZXV_OK) return st;
    return zxv_hex_to_lp(base, out_lp);
}

struct zind_qsink {
    const zxv_cap *cap;
    zxv_hexlog *log;
    zxv_ctx *halt_ctx;
    uint64_t written;
};

int32_t zind_qsink_create(const zxv_cap *cap, zxv_hexlog *log, zxv_ctx *halt_ctx,
                          zind_qsink **out) {
    zind_qsink *s;
    int32_t st;
    if (!cap || !log || !out) return ZXV_E_ARG;
    st = zxv_cap_check(cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zind_set_error("zind_qsink_create: the cap cannot attest (needs ZXV_CAP_ATTEST)");
        return st;
    }
    s = (zind_qsink *) calloc(1, sizeof *s);
    if (!s) return ZXV_E_NOMEM;
    s->cap = cap;
    s->log = log;
    s->halt_ctx = halt_ctx;
    *out = s;
    return ZXV_OK;
}

void zind_qsink_destroy(zind_qsink *s) { free(s); }

int32_t zind_qsink_event(zind_qsink *s, uint8_t tag, uint8_t reason, uint32_t submitter,
                         const uint8_t subject[32], uint64_t *out_seq) {
    uint8_t base = 0, pol = 0, ftag = 0;
    int32_t st;
    if (!s || !subject) return ZXV_E_ARG;
    st = zxv_hex_unpack(tag, &base, &pol, NULL);
    if (st != ZXV_OK) return st;
    if (base != ZXV_HEX_BASE_BOTH && pol != ZXV_HEX_POL_FORBIDDEN) {
        zind_set_error("zind_qsink_event: tag %s is neither Both nor Forbidden; it does not "
                       "belong in the quarantine log",
                       zxv_hex_name(tag));
        return ZXV_E_ARG;
    }
    st = zxv_hex_forbid(tag, &ftag);
    if (st != ZXV_OK) return st;
    st = zxv_hexlog_append(s->cap, s->log, ftag, reason, submitter, subject, out_seq);
    if (st == ZXV_E_CAPACITY && s->halt_ctx) {
        (void) zxv_ctx_halt(s->halt_ctx, "zind: quarantine log full; forbidden work cannot be "
                                         "recorded");
        zind_set_error("zind_qsink_event: the quarantine log is full; the context was halted");
    } else if (st != ZXV_OK) {
        zind_set_error("zind_qsink_event: zxv_hexlog_append failed (%s)", zxv_status_name(st));
    }
    if (st == ZXV_OK) s->written++;
    return st;
}

int32_t zind_qsink_written(const zind_qsink *s, uint64_t *out_count) {
    if (!s || !out_count) return ZXV_E_ARG;
    *out_count = s->written;
    return ZXV_OK;
}
