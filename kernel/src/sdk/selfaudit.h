/* selfaudit.h — User Space Application Self-Audit Framework
 *
 * Every M5 application must implement self_audit() and register it.
 * The kernel calls this at boot and every 1000 ticks.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef M5_SELFAUDIT_H
#define M5_SELFAUDIT_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_api.h"

/* ===== Audit Result ===== */
typedef enum {
    M5_APP_AUDIT_PASS = 0,
    M5_APP_AUDIT_WARN = 1,
    M5_APP_AUDIT_FAIL = 2,
    M5_APP_AUDIT_VETO = 3
} m5_app_audit_result_t;

/* ===== Audit Callback =====
 * Every application must implement this function.
 * Return M5_APP_AUDIT_PASS if all invariants hold.
 */
typedef m5_app_audit_result_t (*m5_app_audit_fn_t)(void);

/* ===== Registration =====
 * Call this from your app's init() to register the audit callback.
 * Returns 0 on success, -1 if audit table is full.
 */
int32_t m5_app_audit_register(m5_app_audit_fn_t fn);

/* ===== Built-in Checks =====
 * These helper functions implement common audit checks.
 */

// Check that all rational values are normalized (gcd=1, den>0)
bool m5_audit_check_rat_normalized(const m5_rat_t *rats, uint32_t count);

// Check that all LPRES values are canonical (not M5_BOTH alias)
bool m5_audit_check_lpres_canonical(const m5_lpres_t *vals, uint32_t count);

// Check that all ordinals are monotonically increasing
bool m5_audit_check_ordinals_monotonic(const m5_ordinal_t *ords, uint32_t count);

// Check that no buffer overflows are possible
bool m5_audit_check_bounds(const void *base, uint32_t size, const void *ptr, uint32_t access_size);

// Check that all pointers are within valid memory regions
bool m5_audit_check_pointers_valid(void);

// Check coverage hyperbola r·ℓ ≥ 1.8 for all active ledger entries
bool m5_audit_check_coverage_hyperbola(void);

// Check that no production capabilities are held without ledger post
bool m5_audit_check_production_capabilities(void);

// Check that all CID references resolve
bool m5_audit_check_cid_resolution(const m5_cid_t *cids, uint32_t count);

// Check that all vouchers are single-active-state
bool m5_audit_check_voucher_single_active(void);

/* ===== Audit Macros ===== */

// Assert with audit logging
#define M5_APP_AUDIT_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            m5_audit_log_fail(msg, __FILE__, __LINE__); \
            return M5_APP_AUDIT_FAIL; \
        } \
    } while(0)

// Check and log warning
#define M5_APP_AUDIT_WARN(cond, msg) \
    do { \
        if (!(cond)) { \
            m5_audit_log_warn(msg, __FILE__, __LINE__); \
        } \
    } while(0)

/* ===== Logging ===== */
void m5_audit_log_fail(const char *msg, const char *file, int line);
void m5_audit_log_warn(const char *msg, const char *file, int line);
void m5_audit_log_info(const char *msg);

/* ===== Example Application Audit =====
 *
 * m5_app_audit_result_t myapp_self_audit(void) {
 *     // 1. Check all rational values normalized
 *     M5_APP_AUDIT_ASSERT(m5_audit_check_rat_normalized(my_rats, MY_RAT_COUNT),
 *                         "rational not normalized");
 *
 *     // 2. Check LPRES canonical
 *     M5_APP_AUDIT_ASSERT(m5_audit_check_lpres_canonical(my_lpres, MY_LPRES_COUNT),
 *                         "LPRES not canonical");
 *
 *     // 3. Check ordinals monotonic
 *     M5_APP_AUDIT_ASSERT(m5_audit_check_ordinals_monotonic(my_ords, MY_ORD_COUNT),
 *                         "ordinals not monotonic");
 *
 *     // 4. Check bounds
 *     M5_APP_AUDIT_ASSERT(m5_audit_check_bounds(my_buffer, sizeof(my_buffer),
 *                         my_ptr, my_access_size),
 *                         "buffer overflow possible");
 *
 *     // 5. Check coverage hyperbola
 *     M5_APP_AUDIT_ASSERT(m5_audit_check_coverage_hyperbola(),
 *                         "coverage hyperbola violated");
 *
 *     // 6. Check production capabilities
 *     M5_APP_AUDIT_ASSERT(m5_audit_check_production_capabilities(),
 *                         "unauthorized production capability");
 *
 *     // 7. Check CID resolution
 *     M5_APP_AUDIT_ASSERT(m5_audit_check_cid_resolution(my_cids, MY_CID_COUNT),
 *                         "CID does not resolve");
 *
 *     // 8. Check voucher single-active-state
 *     M5_APP_AUDIT_ASSERT(m5_audit_check_voucher_single_active(),
 *                         "voucher double-spend possible");
 *
 *     return M5_APP_AUDIT_PASS;
 * }
 *
 * // Register in init()
 * void myapp_init(void) {
 *     m5_app_audit_register(myapp_self_audit);
 *     ...
 * }
 */

#endif /* M5_SELFAUDIT_H */
