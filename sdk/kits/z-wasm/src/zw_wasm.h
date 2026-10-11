/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zw_wasm.h — internal: the Wasm binary decoder shared by the validator
 * and the fuel meter, and the policy layout. Not installed, not stable.
 *
 * R11: this file and zw_wasm.c, zw_validate.c, zw_meter.c, zw_policy.c use
 * no floating-point type at all. `make check-nofloat` scans them (comments
 * and strings removed) and fails on any float type; the Makefile also
 * builds them with -mgeneral-regs-only where the compiler accepts it
 * (strict on arm64, partial on x86-64). */
#ifndef ZW_WASM_H
#define ZW_WASM_H

#include <stddef.h>
#include <stdint.h>

#include "zwasm.h"

/* ----- Wasm constants (core spec 2.0) ----- */
#define WT_I32       0x7F
#define WT_I64       0x7E
#define WT_F32       0x7D
#define WT_F64       0x7C
#define WT_V128      0x7B
#define WT_FUNCREF   0x70
#define WT_EXTERNREF 0x6F
#define WT_EMPTY     0x40 /* block type: no result */

#define SEC_CUSTOM    0
#define SEC_TYPE      1
#define SEC_IMPORT    2
#define SEC_FUNCTION  3
#define SEC_TABLE     4
#define SEC_MEMORY    5
#define SEC_GLOBAL    6
#define SEC_EXPORT    7
#define SEC_START     8
#define SEC_ELEMENT   9
#define SEC_CODE      10
#define SEC_DATA      11
#define SEC_DATACOUNT 12

#define ZW_PAGE_LIMIT_MAX 65536u

/* ----- Policy ----- */
#define ZW_ALLOW_MAX      64
#define ZW_NAME_MAX       63
#define ZW_SIG_MAX        19 /* result + "(" + up to 16 params + ")" */
#define ZW_LIM_COUNT      15

typedef struct {
    char module[ZW_NAME_MAX + 1];
    char name[ZW_NAME_MAX + 1];
    char sig[ZW_SIG_MAX + 1]; /* wasm3 form, e.g. "i(IIi)" */
} zw_allow;

struct zw_policy {
    uint64_t lim[ZW_LIM_COUNT + 1]; /* indexed by ZW_LIM_* */
    uint32_t n_allow;
    zw_allow allow[ZW_ALLOW_MAX];
};

void zw_policy_defaults(zw_policy *p);
/* Copy `src` into `dst` (both caller-owned). */
void zw_policy_copy(zw_policy *dst, const zw_policy *src);

/* Thread-local last error (zw_last_error). */
void zw_set_error(const char *msg);
void zw_set_errorf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

/* ----- Readers ----- */
/* All readers take [*pos, end) of `b`, advance *pos on success and return
 * 0, or return -1 (malformed: truncated, too long, or non-canonical
 * padding bits) without moving *pos. */
int zw_rd_u8(const uint8_t *b, size_t end, size_t *pos, uint8_t *out);
int zw_rd_u32(const uint8_t *b, size_t end, size_t *pos, uint32_t *out);
int zw_rd_s32(const uint8_t *b, size_t end, size_t *pos, int32_t *out);
int zw_rd_s33(const uint8_t *b, size_t end, size_t *pos, int64_t *out);
int zw_rd_s64(const uint8_t *b, size_t end, size_t *pos, int64_t *out);
/* 1 if [p, p+n) is valid UTF-8 (no overlongs, no surrogates, <= U+10FFFF). */
int zw_utf8_ok(const uint8_t *p, size_t n);

/* ----- Instruction decoder ----- */
/* Opcode classes. 0xFC-prefixed instructions are 0xFC00 | sub. */
#define ZW_OP_FC(sub) (0xFC00u | (uint32_t) (sub))

typedef struct {
    uint32_t op;      /* opcode (or ZW_OP_FC(sub))                              */
    size_t len;       /* bytes of the whole instruction                         */
    /* immediates, by opcode: */
    uint32_t idx;     /* first index (label, func, local, global, table, type,  */
                      /* data, elem, memarg align)                              */
    uint32_t idx2;    /* second index (call_indirect table, table.copy src,     */
                      /* table.init table, memarg offset)                       */
    int32_t bt_kind;  /* block type: 0 empty, 1 value type (idx), 2 type index  */
    size_t vec_pos;   /* br_table: offset of the first label; select t: types   */
    uint32_t vec_n;   /* br_table: number of labels before the default          */
} zw_ins;

/* Decode one instruction at b[pos] (end = one past the body). Returns
 * ZW_R_OK, or ZW_R_MALFORMED / ZW_R_SIMD / ZW_R_UNSUPPORTED, with
 * *err_off the offending byte. Float instructions decode fine (they are
 * classified by zw_is_float_op); 0xFD (SIMD) never decodes. */
int32_t zw_decode(const uint8_t *b, size_t end, size_t pos, zw_ins *ins, size_t *err_off);

/* 1 if `op` (as in zw_ins.op) is a floating-point instruction. */
int zw_is_float_op(uint32_t op);
/* 1 for the bulk memory / table instructions 0xFC 8..17. */
int zw_is_bulk_op(uint32_t op);

/* ----- Metering ----- */
/* Instrument a module that zw_validate accepted: append an exported
 * mutable i64 global "__zw_fuel", and charge it at the start of every
 * basic block (one unit per instruction in the block); a block whose
 * charge drives the counter below zero executes `unreachable`. Custom
 * sections are dropped. *out is malloc'ed (caller frees). Returns ZW_OK,
 * ZXV_E_NOMEM, or ZW_E_REJECTED if the input is not a module the
 * validator accepts (defence in depth). */
int32_t zw_meter(const uint8_t *wasm, size_t len, uint8_t **out, size_t *out_len);

#define ZW_FUEL_EXPORT "__zw_fuel"

/* Validator entry used internally: like zw_validate, plus `allow_reserved`
 * (1 lets the "__zw_fuel" export through, to re-check metered output). */
int32_t zw_validate_ex(const zw_policy *policy, const uint8_t *wasm, size_t len,
                       int allow_reserved, int32_t *out_reason, uint32_t *out_section,
                       uint32_t *out_func, uint64_t *out_offset, char *msg, size_t msg_cap,
                       size_t *out_msg_len);

#endif /* ZW_WASM_H */
