/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zc_hexsink.c — the ready-made evidence sink that quarantines every
 * contract violation in a libzxv ABI 1.3 quarantine log (zxv_hexlog) as a
 * Forbidden record. Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "zc_internal.h"

struct zc_hexsink {
    const zxv_cap *cap;
    zxv_hexlog *hexlog;
    zxv_ctx *halt_ctx;
    uint32_t flags;
    uint64_t written, skipped;
    int32_t last_status;
};

int32_t zc_hexsink_create(const zxv_cap *cap, zxv_hexlog *hexlog, uint32_t flags,
                          zc_hexsink **out_sink) {
    zc_hexsink *s;
    int32_t st;
    if (!cap || !hexlog || !out_sink || (flags & ~ZC_HEXSINK_FORBIDDEN_ONLY)) {
        zc_set_error("zc_hexsink_create: NULL argument or unknown flag");
        return ZXV_E_ARG;
    }
    st = zxv_cap_check(cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        zc_set_error("zc_hexsink_create: the capability cannot attest (needs ZXV_CAP_ATTEST)");
        return st;
    }
    s = (zc_hexsink *) calloc(1, sizeof *s);
    if (!s) return ZXV_E_NOMEM;
    s->cap = cap;
    s->hexlog = hexlog;
    s->flags = flags;
    s->last_status = ZXV_OK;
    *out_sink = s;
    return ZXV_OK;
}

void zc_hexsink_destroy(zc_hexsink *sink) { free(sink); }

int32_t zc_hexsink_halt_on_full(zc_hexsink *sink, zxv_ctx *ctx) {
    if (!sink) return ZXV_E_ARG;
    sink->halt_ctx = ctx;
    return ZXV_OK;
}

int32_t zc_evlog_attach_hexsink(zc_evlog *log, zc_hexsink *sink) {
    if (!log || !sink) return ZXV_E_ARG;
    return zc_evlog_set_sink(log, zc_hexsink_record, sink);
}

int32_t zc_hexsink_record(void *user, uint64_t seq, uint32_t origin, int32_t tag, int32_t reason,
                          const uint8_t subject[32], const uint8_t hash[32], const char *text) {
    zc_hexsink *s = (zc_hexsink *) user;
    uint8_t pol = 0, ftag = 0, why;
    int32_t st;
    (void) seq;
    (void) subject;
    (void) text;
    if (!s || !hash) return ZXV_E_ARG;
    if (tag < 0 || tag > 63 || zxv_hex_unpack((uint8_t) tag, NULL, &pol, NULL) != ZXV_OK) {
        s->last_status = ZXV_E_ARG;
        zc_set_error("zc_hexsink: the evidence record's tag is not a 64-state tag (0..63)");
        return ZXV_E_ARG;
    }
    if ((s->flags & ZC_HEXSINK_FORBIDDEN_ONLY) && pol != ZXV_HEX_POL_FORBIDDEN) {
        s->skipped++;
        return 0;
    }
    st = zxv_hex_forbid((uint8_t) tag, &ftag);
    if (st == ZXV_OK) {
        why = (reason >= 0 && reason < ZC_HEXSINK_REASON_OTHER) ? (uint8_t) reason
                                                                : (uint8_t) ZC_HEXSINK_REASON_OTHER;
        st = zxv_hexlog_append(s->cap, s->hexlog, ftag, why, origin, hash, NULL);
    }
    s->last_status = st;
    if (st != ZXV_OK) {
        if (st == ZXV_E_CAPACITY && s->halt_ctx)
            (void) zxv_ctx_halt(s->halt_ctx, "z-contract: the quarantine log is full");
        zc_set_errorf("zc_hexsink: the quarantine log refused the record (%s)",
                      zxv_status_name(st));
        return st;
    }
    s->written++;
    return 0;
}

int32_t zc_hexsink_stats(const zc_hexsink *sink, uint64_t *out_written, uint64_t *out_skipped,
                         int32_t *out_last_status) {
    if (!sink) return ZXV_E_ARG;
    if (out_written) *out_written = sink->written;
    if (out_skipped) *out_skipped = sink->skipped;
    if (out_last_status) *out_last_status = sink->last_status;
    return ZXV_OK;
}
