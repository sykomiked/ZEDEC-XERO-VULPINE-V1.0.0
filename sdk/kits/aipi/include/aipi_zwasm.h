/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aipi_zwasm.h — run AIPI functions in the Z-Wasm sandbox, inside their
 * Z-Contract boundary. It fills an aipi_exec_ops_t (kernel/src/aipi/aipi.h).
 *
 * THE CALL ABI (kit API 0.1)
 *   input        n signed 64-bit integers, little-endian, n = in_len / 8,
 *                0 <= n <= 8 (in_len a multiple of 8)
 *   entry        entry(args...) -> i64: the result, 8 bytes little-endian
 *   checker      checker(args..., result) -> i32/i64: 1 = right, else wrong
 *                (n <= 7)
 *   CERT         certifier(args..., result) -> i64 is the certificate (8
 *                bytes); checker(args..., result, cert) checks it (n <= 6)
 *   contract     when the descriptor names one, the Z-Contract text is
 *                parsed and the contract's `fn <entry>` is enforced on every
 *                run (arguments before it starts, the result after), and a
 *                claimed result outside the contract's result type is
 *                refused before the checker runs.
 *   fuel         each call gets the descriptor's fuel_max (capped by the
 *                budget); running out is AIPI_ERR_EXEC, never a hang.
 * Plugins get no host-function grants: an AIPI function is pure integer
 * code. Every module goes through the Z-Wasm validator (no floats, no SIMD,
 * bounded memory) on every load.
 */
#ifndef AIPI_ZWASM_H
#define AIPI_ZWASM_H

#include <stdint.h>
#include "aipi.h"
#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIPI_ZW_MAX_ARGS 8u

typedef struct aipi_zw aipi_zw;

/* `budget_cap` (ZXV_CAP_BUDGET), `rmag` and `budget` as for zw_host_create;
 * they must outlive the executor. AIPI_OK or AIPI_ERR_ARG / AIPI_ERR_EXEC. */
int aipi_zw_create(const zxv_cap *budget_cap, zxv_rmag *rmag, uint32_t budget, aipi_zw **out);
void aipi_zw_destroy(aipi_zw *z);
/* Fill `ops` with this executor (run, check, certify). */
void aipi_zw_ops(aipi_zw *z, aipi_exec_ops_t *ops);

/* Counters, for audit and tests. */
typedef struct {
    uint64_t runs, checks, certs;
    uint64_t refused_module;    /* validator or link refused the module       */
    uint64_t refused_contract;  /* contract did not parse, or a call broke it */
    uint64_t trapped;           /* trap or out of fuel                        */
    uint64_t fuel_used;
} aipi_zw_stats_t;
void aipi_zw_stats(const aipi_zw *z, aipi_zw_stats_t *out);

/* Pack / unpack the call ABI. */
int aipi_zw_pack(const int64_t *args, uint32_t n, uint8_t *out, uint32_t cap, uint32_t *len);
int aipi_zw_unpack(const uint8_t *in, uint32_t len, int64_t *args, uint32_t *n);

#ifdef __cplusplus
}
#endif

#endif /* AIPI_ZWASM_H */
