/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_record.c — records, their labels, accessors, digests, attestation,
 * names and the last error. Integer code only (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "za_internal.h"

static _Thread_local char g_err[256];

void za_set_error(const char *msg) {
    size_t n = msg ? strlen(msg) : 0;
    if (n >= sizeof g_err) n = sizeof g_err - 1u;
    if (n) memcpy(g_err, msg, n);
    g_err[n] = '\0';
}

int32_t za_last_error(char *buf, size_t cap, size_t *out_len) {
    size_t n = strlen(g_err);
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1u);
    return ZXV_OK;
}

const char *za_status_name(int32_t status) {
    if (status == ZA_E_REFUSED) return "ZA_E_REFUSED";
    return zc_status_name(status);
}

const char *za_reason_name(int32_t reason) {
    static const char *const n[] = {"NONE",      "MALFORMED", "LIMIT",        "NUMBER",
                                    "DUPLICATE", "UTF8",      "TOPIC",        "FIX_HEADER",
                                    "FIX_LENGTH", "FIX_CHECKSUM", "UNSUPPORTED", "MISSING",
                                    "TYPE",      "CONTRACT"};
    if (reason < 1 || reason > ZA_R_CONTRACT) return "UNKNOWN";
    return n[reason];
}

int za_utf8_ok(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        uint32_t cp;
        size_t k;
        if (c < 0x80u) {
            i++;
            continue;
        } else if ((c & 0xE0u) == 0xC0u) {
            k = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0u) == 0xE0u) {
            k = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8u) == 0xF0u) {
            k = 3;
            cp = c & 0x07u;
        } else {
            return 0;
        }
        if (n - i <= k) return 0;
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xC0u) != 0x80u) return 0;
            cp = (cp << 6) | (s[i + j] & 0x3Fu);
        }
        if ((k == 1 && cp < 0x80u) || (k == 2 && cp < 0x800u) || (k == 3 && cp < 0x10000u) ||
            cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
            return 0;
        i += k + 1u;
    }
    return 1;
}

/* A neutral libzxv 64-state tag (ABI 1.3) for a ZXV_LP_* state and phase. */
static int32_t tag_of(int32_t lp, uint8_t phase) {
    uint8_t base = 0, t = 0;
    if (zxv_hex_from_lp(lp, &base) != ZXV_OK ||
        zxv_hex_pack(base, ZXV_HEX_POL_NEUTRAL, phase, &t) != ZXV_OK)
        return -1;
    return t;
}

za_record *za_rec_new(int32_t kind, const char *source, size_t source_len) {
    za_record *r = (za_record *) calloc(1, sizeof *r);
    if (!r) return NULL;
    r->source = (char *) malloc(source_len + 1u);
    if (!r->source) {
        free(r);
        return NULL;
    }
    if (source_len) memcpy(r->source, source, source_len);
    r->source[source_len] = '\0';
    r->kind = kind;
    r->lp = ZXV_LP_NEITHER;
    r->tag = tag_of(ZXV_LP_NEITHER, ZXV_HEX_PHASE_EMERGING);
    return r;
}

void za_rec_drop_fields(za_record *rec) {
    for (uint32_t i = 0; i < rec->nf; i++) {
        free(rec->f[i].name);
        free(rec->f[i].text);
    }
    rec->nf = 0;
}

void za_record_destroy(za_record *record) {
    if (!record) return;
    za_rec_drop_fields(record);
    free(record->f);
    free(record->source);
    free(record);
}

int za_rec_add(za_record *rec, const char *name, size_t name_len, int32_t kind, int64_t num,
               int64_t den, const uint8_t *text, size_t len) {
    za_fld *f;
    if (name_len > ZA_MAX_NAME) return ZA_R_LIMIT;
    if (rec->nf >= ZA_MAX_FIELDS) return ZA_R_LIMIT;
    for (uint32_t i = 0; i < rec->nf; i++)
        if (strlen(rec->f[i].name) == name_len && memcmp(rec->f[i].name, name, name_len) == 0)
            return ZA_R_DUPLICATE;
    if (rec->nf == rec->fcap) {
        uint32_t nc = rec->fcap ? rec->fcap * 2u : 16u;
        za_fld *nf = (za_fld *) realloc(rec->f, (size_t) nc * sizeof *nf);
        if (!nf) {
            rec->oom = 1;
            return -1;
        }
        rec->f = nf;
        rec->fcap = nc;
    }
    f = &rec->f[rec->nf];
    memset(f, 0, sizeof *f);
    f->name = (char *) malloc(name_len + 1u);
    f->text = (uint8_t *) malloc(len + 1u);
    if (!f->name || !f->text) {
        free(f->name);
        free(f->text);
        rec->oom = 1;
        return -1;
    }
    if (name_len) memcpy(f->name, name, name_len);
    f->name[name_len] = '\0';
    if (len) memcpy(f->text, text, len);
    f->text[len] = 0;
    f->len = len;
    f->kind = kind;
    f->num = num;
    f->den = den;
    rec->nf++;
    return 0;
}

void za_rec_refuse(za_record *rec, int32_t code, const char *fmt, ...) {
    va_list ap;
    rec->lp = ZXV_LP_FALSE;
    rec->tag = tag_of(ZXV_LP_FALSE, ZXV_HEX_PHASE_EMERGING);
    if (rec->nr >= ZA_MAX_REASONS) return;
    rec->r[rec->nr].code = code;
    va_start(ap, fmt);
    vsnprintf(rec->r[rec->nr].msg, sizeof rec->r[rec->nr].msg, fmt, ap);
    va_end(ap);
    rec->nr++;
}

int32_t za_ingest(int32_t kind, const char *source, const uint8_t *payload, size_t len,
                  za_record **out_record) {
    za_record *rec;
    if (!out_record || (!payload && len) || kind < ZA_SRC_JSON || kind > ZA_SRC_FIX_PIPE ||
        (kind == ZA_SRC_MQTT && !source)) {
        za_set_error("za_ingest: NULL argument or unknown source kind");
        return ZXV_E_ARG;
    }
    if (!payload) payload = (const uint8_t *) "";
    rec = za_rec_new(kind, source ? source : "", source ? strlen(source) : 0u);
    if (!rec) return ZXV_E_NOMEM;
    if (len > ZA_MAX_PAYLOAD) {
        za_rec_refuse(rec, ZA_R_LIMIT, "payload of %zu bytes is over the %u-byte limit", len,
                      ZA_MAX_PAYLOAD);
    } else {
        switch (kind) {
        case ZA_SRC_JSON: za_json_into(rec, payload, len); break;
        case ZA_SRC_MQTT: za_mqtt_into(rec, source, payload, len); break;
        case ZA_SRC_FIX: za_fix_into(rec, payload, len, 0x01); break;
        default: za_fix_into(rec, payload, len, (uint8_t) '|'); break;
        }
    }
    if (rec->oom) {
        za_record_destroy(rec);
        za_set_error("za_ingest: out of memory");
        return ZXV_E_NOMEM;
    }
    if (rec->lp == ZXV_LP_FALSE) za_rec_drop_fields(rec);
    *out_record = rec;
    return ZXV_OK;
}

int32_t za_record_state(const za_record *record, int32_t *out_lp_state, int32_t *out_tag) {
    if (!record) return ZXV_E_ARG;
    if (out_lp_state) *out_lp_state = record->lp;
    if (out_tag) *out_tag = record->tag;
    return ZXV_OK;
}

int32_t za_record_source(const za_record *record, int32_t *out_kind, const char **out_source) {
    if (!record) return ZXV_E_ARG;
    if (out_kind) *out_kind = record->kind;
    if (out_source) *out_source = record->source;
    return ZXV_OK;
}

int32_t za_record_reason_count(const za_record *record, uint32_t *out_count) {
    if (!record || !out_count) return ZXV_E_ARG;
    *out_count = record->nr;
    return ZXV_OK;
}

int32_t za_record_reason(const za_record *record, uint32_t index, int32_t *out_reason,
                         const char **out_message) {
    if (!record) return ZXV_E_ARG;
    if (index >= record->nr) return ZXV_E_NOT_FOUND;
    if (out_reason) *out_reason = record->r[index].code;
    if (out_message) *out_message = record->r[index].msg;
    return ZXV_OK;
}

int32_t za_record_field_count(const za_record *record, uint32_t *out_count) {
    if (!record || !out_count) return ZXV_E_ARG;
    *out_count = record->nf;
    return ZXV_OK;
}

int32_t za_field(const za_record *record, uint32_t index, const char **out_name,
                 int32_t *out_kind) {
    if (!record) return ZXV_E_ARG;
    if (index >= record->nf) return ZXV_E_NOT_FOUND;
    if (out_name) *out_name = record->f[index].name;
    if (out_kind) *out_kind = record->f[index].kind;
    return ZXV_OK;
}

int32_t za_field_find(const za_record *record, const char *name, uint32_t *out_index) {
    if (!record || !name || !out_index) return ZXV_E_ARG;
    for (uint32_t i = 0; i < record->nf; i++) {
        if (strcmp(record->f[i].name, name) == 0) {
            *out_index = i;
            return ZXV_OK;
        }
    }
    return ZXV_E_NOT_FOUND;
}

int32_t za_field_rat(const za_record *record, uint32_t index, int64_t *out_num,
                     int64_t *out_den) {
    if (!record || !out_num || !out_den) return ZXV_E_ARG;
    if (index >= record->nf) return ZXV_E_NOT_FOUND;
    if (record->f[index].kind != ZA_F_NUMBER && record->f[index].kind != ZA_F_BOOL) {
        za_set_error("za_field_rat: the field is not a number");
        return ZXV_E_STATE;
    }
    *out_num = record->f[index].num;
    *out_den = record->f[index].den;
    return ZXV_OK;
}

int32_t za_field_text(const za_record *record, uint32_t index, const uint8_t **out_bytes,
                      size_t *out_len) {
    if (!record || !out_bytes || !out_len) return ZXV_E_ARG;
    if (index >= record->nf) return ZXV_E_NOT_FOUND;
    *out_bytes = record->f[index].text;
    *out_len = record->f[index].len;
    return ZXV_OK;
}

/* ----- digest ----- */

typedef struct {
    uint8_t *b;
    size_t n, cap;
    int oom;
} dbuf;

static void dput(dbuf *d, const void *p, size_t n) {
    if (d->oom) return;
    if (d->n + n > d->cap) {
        size_t nc = d->cap ? d->cap : 1024u;
        while (nc < d->n + n) nc *= 2u;
        uint8_t *nb = (uint8_t *) realloc(d->b, nc);
        if (!nb) {
            d->oom = 1;
            return;
        }
        d->b = nb;
        d->cap = nc;
    }
    if (n) memcpy(d->b + d->n, p, n);
    d->n += n;
}

static void dle(dbuf *d, uint64_t v, int n) {
    uint8_t t[8];
    for (int i = 0; i < n; i++) t[i] = (uint8_t) (v >> (8 * i));
    dput(d, t, (size_t) n);
}

int32_t za_record_digest(const za_record *record, uint8_t out[32]) {
    static const char dom[] = "zxv/za-record/v1";
    dbuf d = {NULL, 0, 0, 0};
    int32_t st;
    if (!record || !out) return ZXV_E_ARG;
    dput(&d, dom, sizeof dom - 1u);
    dle(&d, (uint64_t) (uint32_t) record->kind, 4);
    dle(&d, strlen(record->source), 4);
    dput(&d, record->source, strlen(record->source));
    dle(&d, record->nf, 4);
    for (uint32_t i = 0; i < record->nf; i++) {
        const za_fld *f = &record->f[i];
        dle(&d, strlen(f->name), 4);
        dput(&d, f->name, strlen(f->name));
        dle(&d, (uint64_t) (uint32_t) f->kind, 4);
        dle(&d, (uint64_t) f->num, 8);
        dle(&d, (uint64_t) f->den, 8);
        dle(&d, f->len, 8);
        dput(&d, f->text, f->len);
    }
    if (d.oom) {
        free(d.b);
        return ZXV_E_NOMEM;
    }
    st = zxv_sha3_256(d.b, d.n, out);
    free(d.b);
    return st;
}

int32_t za_record_attest(za_record *record, const zxv_cap *cap, int32_t lp_state) {
    int32_t st;
    if (!record || !cap) return ZXV_E_ARG;
    if (lp_state != ZXV_LP_TRUE && lp_state != ZXV_LP_FALSE && lp_state != ZXV_LP_BOTH) {
        za_set_error("za_record_attest: attest TRUE, FALSE or BOTH");
        return ZXV_E_ARG;
    }
    st = zxv_cap_check(cap, ZXV_CAP_ATTEST);
    if (st != ZXV_OK) {
        za_set_error("za_record_attest: the capability cannot attest (needs ZXV_CAP_ATTEST)");
        return st;
    }
    if (record->attested) {
        za_set_error("za_record_attest: the record was already attested");
        return ZXV_E_STATE;
    }
    if (record->lp == ZXV_LP_FALSE && lp_state != ZXV_LP_FALSE) {
        za_set_error("za_record_attest: a refused record cannot be attested TRUE or BOTH");
        return ZXV_E_STATE;
    }
    {
        /* The libzxv tag rules: replace base and polarity (zxv_hex_set),
         * then move the phase one step, emerging -> manifest (zxv_hex_step). */
        uint8_t base = 0, t = 0, stepped = 0;
        if (record->tag < 0 || record->tag > 63 || zxv_hex_from_lp(lp_state, &base) != ZXV_OK ||
            zxv_hex_set((uint8_t) record->tag, base, ZXV_HEX_POL_NEUTRAL, &t) != ZXV_OK ||
            zxv_hex_step(t, ZXV_HEX_PHASE_MANIFEST, &stepped) != ZXV_OK) {
            za_set_error("za_record_attest: the record's tag cannot move to manifest");
            return ZXV_E_STATE;
        }
        record->tag = stepped;
    }
    record->lp = lp_state;
    record->attested = 1;
    return ZXV_OK;
}

int32_t za_record_action(const za_record *record, int32_t *out_action) {
    if (!record || !out_action || record->tag < 0 || record->tag > 63) return ZXV_E_ARG;
    return zxv_hex_action((uint8_t) record->tag, out_action);
}
