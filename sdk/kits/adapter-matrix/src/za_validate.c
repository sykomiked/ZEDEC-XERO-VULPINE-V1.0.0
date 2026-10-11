/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* za_validate.c — check a record against a z-contract function and label
 * it. Integer code only (R11). */
#include <stdio.h>
#include <string.h>

#include "za_internal.h"

static int32_t record_evidence(za_record *rec, const zc_contract *c, zc_evlog *log,
                               uint32_t index, int32_t zc_reason, const char *msg) {
    uint8_t cd[32], sd[32];
    if (!log) return ZXV_OK;
    if (zc_contract_digest(c, cd) != ZXV_OK) memset(cd, 0, sizeof cd);
    if (za_record_digest(rec, sd) != ZXV_OK) memset(sd, 0, sizeof sd);
    return zc_evlog_append(log, ZC_ORIGIN_ADAPTER, ZC_PART_PARAMS, index, zc_reason, rec->tag, cd,
                           sd, msg, NULL);
}

/* Refuse with a ZA reason, record evidence, return ZA_E_REFUSED (or the
 * log's status: the record is refused either way). */
static int32_t refuse(za_record *rec, const zc_contract *c, zc_evlog *log, int32_t za_reason,
                      uint32_t index, int32_t zc_reason, const char *msg) {
    int32_t st;
    za_rec_refuse(rec, za_reason, "%s", msg);
    st = record_evidence(rec, c, log, index, zc_reason, msg);
    za_set_error(msg);
    return st == ZXV_OK ? ZA_E_REFUSED : st;
}

int32_t za_validate(za_record *record, const zc_contract *contract, const char *fn,
                    zc_evlog *log) {
    uint32_t np = 0, idx = 0;
    int32_t st, reason = 0;
    zc_vals *v = NULL;
    char msg[400];
    if (!record || !contract || !fn) {
        za_set_error("za_validate: NULL argument");
        return ZXV_E_ARG;
    }
    if (record->lp != ZXV_LP_NEITHER || record->attested) {
        za_set_error("za_validate: only an accepted, unattested record can be validated");
        return ZXV_E_STATE;
    }
    st = zc_fn_info(contract, fn, &np, NULL, NULL, NULL);
    if (st != ZXV_OK) {
        za_set_error("za_validate: the contract declares no such function");
        return st;
    }
    st = zc_vals_create(&v);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < np; i++) {
        int32_t ty = 0;
        const char *pname = NULL;
        uint32_t fi = 0;
        (void) zc_slot_type(contract, fn, ZC_PART_PARAMS, i, &ty, &pname);
        if (za_field_find(record, pname, &fi) != ZXV_OK) {
            snprintf(msg, sizeof msg, "%s: parameter %u '%s' has no field in the record", fn,
                     (unsigned) i, pname);
            zc_vals_destroy(v);
            return refuse(record, contract, log, ZA_R_MISSING, i, ZC_V_ARITY, msg);
        }
        const za_fld *f = &record->f[fi];
        int ok = 1;
        switch (ty) {
        case ZC_T_INT:
        case ZC_T_RAT: {
            int64_t n = 0, d = 1;
            if (f->kind == ZA_F_NUMBER) {
                n = f->num;
                d = f->den;
            } else if (f->kind != ZA_F_TEXT ||
                       za_decimal_parse((const char *) f->text, f->len, &n, &d) != ZXV_OK) {
                ok = 0;
            }
            if (ok) (void) zc_vals_push_rat(v, n, d);
            break;
        }
        case ZC_T_TRUTH: {
            static const char *const tn[4] = {"neither", "true", "false", "both"};
            int32_t t = -1;
            if (f->kind == ZA_F_BOOL) t = f->num ? ZXV_LP_TRUE : ZXV_LP_FALSE;
            else if (f->kind == ZA_F_TEXT)
                for (int k = 0; k < 4; k++)
                    if (f->len == strlen(tn[k]) && memcmp(f->text, tn[k], f->len) == 0) t = k;
            if (t < 0) ok = 0;
            else (void) zc_vals_push_truth(v, t);
            break;
        }
        case ZC_T_TAG: {
            int32_t t = -1;
            if (f->kind != ZA_F_TEXT || memchr(f->text, 0, f->len) ||
                zc_tag_parse((const char *) f->text, &t) != ZXV_OK)
                ok = 0;
            else (void) zc_vals_push_tag(v, t);
            break;
        }
        default: /* bytes */
            if (f->kind != ZA_F_TEXT) ok = 0;
            else if (zc_vals_push_bytes(v, f->text, f->len) != ZXV_OK) ok = 0;
            break;
        }
        if (!ok) {
            static const char *const kn[] = {"?", "a number", "text", "a boolean", "null"};
            static const char *const tyn[] = {"?", "int", "rat", "truth", "tag", "bytes"};
            snprintf(msg, sizeof msg, "%s: parameter %u '%s': the field is %s, which cannot be %s",
                     fn, (unsigned) i, pname, kn[f->kind >= 1 && f->kind <= 4 ? f->kind : 0],
                     tyn[ty >= 1 && ty <= 5 ? ty : 0]);
            zc_vals_destroy(v);
            return refuse(record, contract, log, ZA_R_TYPE, i, ZC_V_KIND, msg);
        }
    }
    st = zc_check(contract, fn, ZC_PART_PARAMS, v, &reason, &idx, msg, sizeof msg, NULL);
    zc_vals_destroy(v);
    if (st == ZC_E_VIOLATION) return refuse(record, contract, log, ZA_R_CONTRACT, idx, reason, msg);
    return st;
}
