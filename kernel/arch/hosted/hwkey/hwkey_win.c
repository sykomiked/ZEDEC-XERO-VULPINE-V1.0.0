/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwkey_win.c — Windows key storage as an hwkey provider (CNG / NCrypt).
 *
 *   create  A persisted ECDH P-256 key named "ZXV-hwkey-<hex>" (hex of
 *           SHAKE256 of the label) in the Microsoft Platform Crypto
 *           Provider, i.e. inside the TPM; opened again if it exists. The
 *           handle stored is that name.
 *   wrap    A fresh ephemeral P-256 key pair in BCrypt; its public half is
 *           imported into the same provider and agreed with the TPM key
 *           (NCryptSecretAgreement). The ephemeral private key is destroyed.
 *   unwrap  The stored public point is imported and agreed again.
 *   Z       NCryptDeriveKey with BCRYPT_KDF_HASH / SHA-256 over the agreed
 *           secret: 32 bytes, the same on wrap and unwrap.
 *
 * With software_ksp the Microsoft Software Key Storage Provider is used
 * instead (keys protected by DPAPI in the user profile, not a chip): the
 * fallback on a PC with no TPM, reported as HWK_KIND_SOFTWARE. Windows only.
 */
#ifdef _WIN32
#    include "hwkey_host.h"
#    include "../../../src/mlkem/keccak.h"
#    include <windows.h>
#    include <bcrypt.h>
#    include <ncrypt.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>

#    ifndef NTE_BAD_KEYSET
#        define NTE_BAD_KEYSET ((SECURITY_STATUS) 0x80090016L)
#    endif

#    define NAME_PREFIX "ZXV-hwkey-"
#    define NAME_LEN    (10 + 32) /* prefix + 16 bytes of hex */

typedef struct {
    NCRYPT_PROV_HANDLE prov;
    char name[64];
} win_t;

static int name_of(const uint8_t *label, uint32_t label_len, char out[NAME_LEN + 1])
{
    uint8_t in[12 + HWK_LABEL_MAX], h[16];
    static const char hex[] = "0123456789abcdef";
    if (label_len > HWK_LABEL_MAX) return -1;
    memcpy(in, "ZXV-WIN-KEY1", 12);
    if (label_len) memcpy(in + 12, label, label_len);
    shake256(in, 12 + label_len, h, sizeof h);
    memcpy(out, NAME_PREFIX, 10);
    for (int i = 0; i < 16; i++) {
        out[10 + 2 * i] = hex[h[i] >> 4];
        out[11 + 2 * i] = hex[h[i] & 15];
    }
    out[NAME_LEN] = 0;
    return 0;
}

static int open_key(win_t *w, const uint8_t *handle, uint32_t handle_len, int create,
                    NCRYPT_KEY_HANDLE *k)
{
    WCHAR wname[NAME_LEN + 1];
    if (handle_len != NAME_LEN) return -1;
    for (uint32_t i = 0; i < NAME_LEN; i++) {
        if (handle[i] < 0x20 || handle[i] > 0x7e) return -1;
        wname[i] = (WCHAR) handle[i];
    }
    wname[NAME_LEN] = 0;
    SECURITY_STATUS s = NCryptOpenKey(w->prov, k, wname, 0, 0);
    if (s == ERROR_SUCCESS) return 0;
    if (!create || s != NTE_BAD_KEYSET) return -1;
    if (NCryptCreatePersistedKey(w->prov, k, BCRYPT_ECDH_P256_ALGORITHM, wname, 0, 0) !=
        ERROR_SUCCESS)
        return -1;
    if (NCryptFinalizeKey(*k, 0) != ERROR_SUCCESS) {
        NCryptFreeObject(*k);
        return -1;
    }
    return 0;
}

/* Z = SHA-256 KDF over ECDH(key, point); point is 04 || x || y */
static int agree(win_t *w, NCRYPT_KEY_HANDLE k, const uint8_t point[65], uint8_t z[32])
{
    uint8_t blob[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    BCRYPT_ECCKEY_BLOB *hdr = (BCRYPT_ECCKEY_BLOB *) blob;
    hdr->dwMagic = BCRYPT_ECDH_PUBLIC_P256_MAGIC;
    hdr->cbKey = 32;
    memcpy(blob + sizeof *hdr, point + 1, 64);
    NCRYPT_KEY_HANDLE pub = 0;
    NCRYPT_SECRET_HANDLE sec = 0;
    int rc = -1;
    if (NCryptImportKey(w->prov, 0, BCRYPT_ECCPUBLIC_BLOB, NULL, &pub, blob, sizeof blob, 0) !=
        ERROR_SUCCESS)
        return -1;
    if (NCryptSecretAgreement(k, pub, &sec, 0) == ERROR_SUCCESS) {
        BCryptBuffer b = {(ULONG) ((wcslen(BCRYPT_SHA256_ALGORITHM) + 1) * sizeof(WCHAR)),
                          KDF_HASH_ALGORITHM, (PVOID) BCRYPT_SHA256_ALGORITHM};
        BCryptBufferDesc d = {BCRYPTBUFFER_VERSION, 1, &b};
        DWORD got = 0;
        if (NCryptDeriveKey(sec, BCRYPT_KDF_HASH, &d, z, 32, &got, 0) == ERROR_SUCCESS && got == 32)
            rc = 0;
        NCryptFreeObject(sec);
    }
    NCryptFreeObject(pub);
    return rc;
}

static int win_create(void *ctx, const uint8_t *label, uint32_t label_len, uint8_t *handle,
                      uint32_t *handle_len)
{
    win_t *w = ctx;
    char name[NAME_LEN + 1];
    NCRYPT_KEY_HANDLE k;
    if (*handle_len < NAME_LEN || name_of(label, label_len, name)) return -1;
    memcpy(handle, name, NAME_LEN);
    if (open_key(w, handle, NAME_LEN, 1, &k)) return -1;
    NCryptFreeObject(k);
    *handle_len = NAME_LEN;
    return 0;
}

static int win_wrap(void *ctx, const uint8_t *handle, uint32_t handle_len, uint8_t *eph,
                    uint32_t *eph_len, uint8_t secret[HWK_SECRET_BYTES])
{
    win_t *w = ctx;
    NCRYPT_KEY_HANDLE k;
    BCRYPT_ALG_HANDLE alg = 0;
    BCRYPT_KEY_HANDLE e = 0;
    uint8_t blob[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    ULONG cb = 0;
    int rc = -1;
    if (*eph_len < 65 || open_key(w, handle, handle_len, 0, &k)) return -1;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDH_P256_ALGORITHM, NULL, 0) == 0 &&
        BCryptGenerateKeyPair(alg, &e, 256, 0) == 0 && BCryptFinalizeKeyPair(e, 0) == 0 &&
        BCryptExportKey(e, NULL, BCRYPT_ECCPUBLIC_BLOB, blob, sizeof blob, &cb, 0) == 0 &&
        cb == sizeof blob) {
        eph[0] = 0x04;
        memcpy(eph + 1, blob + sizeof(BCRYPT_ECCKEY_BLOB), 64);
        if (agree(w, k, eph, secret) == 0) {
            *eph_len = 65;
            rc = 0;
        }
    }
    if (e) BCryptDestroyKey(e);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    NCryptFreeObject(k);
    return rc;
}

static int win_unwrap(void *ctx, const uint8_t *handle, uint32_t handle_len, const uint8_t *eph,
                      uint32_t eph_len, uint8_t secret[HWK_SECRET_BYTES])
{
    win_t *w = ctx;
    NCRYPT_KEY_HANDLE k;
    if (eph_len != 65 || eph[0] != 0x04 || open_key(w, handle, handle_len, 0, &k)) return -1;
    int rc = agree(w, k, eph, secret);
    NCryptFreeObject(k);
    return rc;
}

hwk_status_t hwk_win_open(hwk_provider_t *p, void **impl, bool software_ksp, char *why,
                          size_t why_cap)
{
    win_t *w = calloc(1, sizeof *w);
    if (!w) return HWK_ERR_HW;
    LPCWSTR ksp = software_ksp ? MS_KEY_STORAGE_PROVIDER : MS_PLATFORM_CRYPTO_PROVIDER;
    SECURITY_STATUS s = NCryptOpenStorageProvider(&w->prov, ksp, 0);
    if (s != ERROR_SUCCESS) {
        snprintf(why, why_cap, "%s unavailable (0x%08lx)",
                 software_ksp ? "software key storage" : "TPM (Platform Crypto Provider)",
                 (unsigned long) s);
        free(w);
        return HWK_ERR_UNAVAILABLE;
    }
    snprintf(w->name, sizeof w->name, "%s", software_ksp ? "windows-software-ksp" : "tpm2:pcp");
    p->kind = software_ksp ? HWK_KIND_SOFTWARE : HWK_KIND_TPM;
    p->name = w->name;
    p->ctx = w;
    p->create = win_create;
    p->wrap = win_wrap;
    p->unwrap = win_unwrap;
    p->random = hwk_host_random;
    *impl = w;
    return HWK_OK;
}

void hwk_win_close(void *impl)
{
    win_t *w = impl;
    if (!w) return;
    NCryptFreeObject(w->prov);
    free(w);
}
#else
typedef int hwkey_win_unused_t; /* ISO C forbids an empty translation unit */
#endif
