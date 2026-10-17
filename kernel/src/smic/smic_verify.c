/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "smic_verify.h"
#include "../pqsec/pq_matrix_algs.h"

static void smc_copy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--) *d++ = *s++;
}

/* the attestation's domain separator */
static const uint8_t SMIC_CTX[] = "ZXV/SMIC/1";

int smic_attest(const smic_hw_t *hw, uint16_t type,
                const uint8_t pk[PQM_MLDSA87_PK_BYTES], const uint8_t sk[PQM_MLDSA87_SK_BYTES],
                uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    int r;
    if (!hw || !pk || !sk || !out || !out_len) return SMIC_ERR_ARG;
    *out_len = 0;
    if (cap < SMIC_ATTEST_BYTES) return SMIC_ERR_SPACE;
    r = smic_manifest_pack(type, hw, out);
    if (r != SMIC_OK) return r;
    smc_copy(out + SMIC_ENV_BYTES, pk, PQM_MLDSA87_PK_BYTES);
    pqm_mldsa87_sign(sk, out, SMIC_ENV_BYTES, SMIC_CTX, (uint32_t) sizeof SMIC_CTX - 1, 0,
                    out + SMIC_ENV_BYTES + PQM_MLDSA87_PK_BYTES);
    *out_len = SMIC_ATTEST_BYTES;
    return SMIC_OK;
}

int smic_attest_verify(const uint8_t *in, uint32_t len, uint16_t *type, smic_hw_t *hw)
{
    smic_hw_t tmp;
    int r;
    if (!in || len != SMIC_ATTEST_BYTES) return SMIC_ERR_ARG;
    r = smic_manifest_unpack(in, type, hw ? hw : &tmp);
    if (r != SMIC_OK) return r;
    if (!pqm_mldsa87_verify(in + SMIC_ENV_BYTES, in, SMIC_ENV_BYTES,
                           SMIC_CTX, (uint32_t) sizeof SMIC_CTX - 1,
                           in + SMIC_ENV_BYTES + PQM_MLDSA87_PK_BYTES))
        return SMIC_ERR_SIG;
    return SMIC_OK;
}
