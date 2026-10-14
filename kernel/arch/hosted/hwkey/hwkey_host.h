/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwkey_host.h — the machine's own security chip as an hwkey provider.
 *
 *   macOS    Secure Enclave through CryptoKit (hwkey_se.swift). Works in an
 *            ad-hoc signed app: the enclave key is kept as its encrypted
 *            dataRepresentation inside the sealed blob, not in the keychain,
 *            so no keychain entitlement is needed.
 *   Linux    TPM 2.0 spoken directly (hwkey_tpm.c) on /dev/tpmrm0, or on a
 *            TPM simulator when ZXV_TPM=mssim:HOST:PORT (swtpm in CI). The
 *            device key is a primary key re-derived from the owner seed and
 *            a label, so nothing about it is stored but the label.
 *   Windows  TPM 2.0 through the Platform Crypto Provider (hwkey_win.c), a
 *            persisted NCrypt ECDH P-256 key.
 *
 * With no chip, the software provider (hwkey_soft.c) is used with a 32-byte
 * secret in a 0600 file, unless HWK_HOST_REQUIRE_HW is set. On Windows the
 * fallback is the Microsoft software key storage provider (DPAPI) instead
 * of a file. Either way the key reports HWK_KIND_SOFTWARE.
 */
#ifndef ZXV_HWKEY_HOST_H
#define ZXV_HWKEY_HOST_H

#include "../../../src/hwkey/hwkey.h"

#define HWK_HOST_REQUIRE_HW 1u /* fail instead of falling back to software */
#define HWK_HOST_SOFT_ONLY  2u /* skip the chip (tests)                    */

typedef struct {
    hwk_provider_t p;
    hwk_soft_t soft;
    void *impl;    /* the chip provider's own state */
    char why[160]; /* why the chip was not used, when it was not */
} hwk_host_t;

/* Open the best provider. soft_path is the fallback secret file (created
 * 0600 when missing; may be NULL on Windows). Returns HWK_OK, or
 * HWK_ERR_UNAVAILABLE with HWK_HOST_REQUIRE_HW and no chip. */
hwk_status_t hwk_host_open(hwk_host_t *h, const char *soft_path, unsigned flags);
void hwk_host_close(hwk_host_t *h);

/* The operating system's random generator. Returns 0 on success. */
int hwk_host_random(void *ctx, uint8_t *out, uint32_t len);

/* Per-platform chip providers (each returns HWK_ERR_UNAVAILABLE where the
 * platform has none). */
hwk_status_t hwk_tpm_open(hwk_provider_t *p, void **impl, char *why, size_t why_cap);
void hwk_tpm_close(void *impl);
hwk_status_t hwk_se_open(hwk_provider_t *p, void **impl, char *why, size_t why_cap);
void hwk_se_close(void *impl);
hwk_status_t hwk_win_open(hwk_provider_t *p, void **impl, bool software_ksp, char *why,
                          size_t why_cap);
void hwk_win_close(void *impl);

#endif /* ZXV_HWKEY_HOST_H */
