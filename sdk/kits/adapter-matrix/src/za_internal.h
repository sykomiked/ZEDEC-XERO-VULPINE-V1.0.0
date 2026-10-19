/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_internal.h — adapter-matrix internals. Integer code only (R11). */
#ifndef ZA_INTERNAL_H
#define ZA_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "zadapter.h"

typedef struct {
    char *name;
    int32_t kind;
    int64_t num, den;
    uint8_t *text; /* NUL-terminated copy; for NUMBER the original text */
    size_t len;
} za_fld;

typedef struct {
    int32_t code;
    char msg[160];
} za_reason_t;

struct za_record {
    int32_t kind;
    char *source;
    int32_t lp, tag;
    int attested;
    za_fld *f;
    uint32_t nf, fcap;
    za_reason_t r[ZA_MAX_REASONS];
    uint32_t nr;
    int oom;
};

void za_set_error(const char *msg);

/* za_record.c */
za_record *za_rec_new(int32_t kind, const char *source, size_t source_len);
/* Add a field. Returns 0, or a ZA_R_* reason (LIMIT, DUPLICATE) the caller
 * turns into a refusal; sets rec->oom and returns -1 when out of memory. */
int za_rec_add(za_record *rec, const char *name, size_t name_len, int32_t kind, int64_t num,
               int64_t den, const uint8_t *text, size_t len);
/* Refuse: state FALSE, tag false/neutral/emerging, one more reason. */
void za_rec_refuse(za_record *rec, int32_t code, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
void za_rec_drop_fields(za_record *rec);
int za_utf8_ok(const uint8_t *s, size_t n);

/* za_json.c: parse a JSON document into rec's fields (prefix "" at the
 * root). Refuses rec on any problem (and drops its fields). */
void za_json_into(za_record *rec, const uint8_t *s, size_t n);

/* za_fix.c, za_mqtt.c */
void za_fix_into(za_record *rec, const uint8_t *s, size_t n, uint8_t delim);
void za_mqtt_into(za_record *rec, const char *topic, const uint8_t *s, size_t n);

#endif /* ZA_INTERNAL_H */
