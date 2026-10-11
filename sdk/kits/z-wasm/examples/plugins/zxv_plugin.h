/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_plugin.h — the plugin side of Z-Wasm: the host functions a ZXV host
 * can grant (import module "zxv"), for C compiled to wasm32-freestanding.
 *
 * Every function returns a ZXV status (0 = ZXV_OK, negative = ZXV_E_*), or
 * for the logic calls the four-valued result 0..3 or a negative status.
 * A host function the host did not grant cannot be imported: the module is
 * refused at load (IMPORT_DENIED). Pointers are offsets in the plugin's own
 * linear memory; the host bounds-checks them. */
#ifndef ZXV_PLUGIN_H
#define ZXV_PLUGIN_H

typedef signed char int8_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

#define ZXV_IMPORT(name) __attribute__((import_module("zxv"), import_name(name)))
#define ZXV_EXPORT(name) __attribute__((export_name(name)))

#define ZXV_OK 0
#define ZXV_LP_NEITHER 0
#define ZXV_LP_TRUE    1
#define ZXV_LP_FALSE   2
#define ZXV_LP_BOTH    3

/* An exact rational as libzxv returns it: reduced, den > 0. */
typedef struct {
    int64_t num;
    int64_t den;
} zxv_rat;

/* ZW_GRANT_RATIONAL */
ZXV_IMPORT("rat_add") int32_t zxv_rat_add(int64_t an, int64_t ad, int64_t bn, int64_t bd, zxv_rat *out);
ZXV_IMPORT("rat_sub") int32_t zxv_rat_sub(int64_t an, int64_t ad, int64_t bn, int64_t bd, zxv_rat *out);
ZXV_IMPORT("rat_mul") int32_t zxv_rat_mul(int64_t an, int64_t ad, int64_t bn, int64_t bd, zxv_rat *out);
ZXV_IMPORT("rat_div") int32_t zxv_rat_div(int64_t an, int64_t ad, int64_t bn, int64_t bd, zxv_rat *out);
ZXV_IMPORT("rat_cmp") int32_t zxv_rat_cmp(int64_t an, int64_t ad, int64_t bn, int64_t bd, int32_t *out);
ZXV_IMPORT("rat_normalize") int32_t zxv_rat_normalize(int64_t num, int64_t den, zxv_rat *out);

/* ZW_GRANT_LOGIC */
ZXV_IMPORT("lp_not") int32_t zxv_lp_not(int32_t a);
ZXV_IMPORT("lp_and") int32_t zxv_lp_and(int32_t a, int32_t b);
ZXV_IMPORT("lp_or") int32_t zxv_lp_or(int32_t a, int32_t b);
ZXV_IMPORT("lp_from_evidence") int32_t zxv_lp_from_evidence(uint32_t positive, uint32_t negative);

/* ZW_GRANT_PROPOSE: put num/den into the host's S0 queue with a plugin
 * chosen kind and a label of at most 64 bytes. The host decides. */
ZXV_IMPORT("propose") int32_t zxv_propose(uint32_t kind, int64_t num, int64_t den,
                                          const char *label, uint32_t label_len);

static inline uint32_t zxv_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

#endif /* ZXV_PLUGIN_H */
