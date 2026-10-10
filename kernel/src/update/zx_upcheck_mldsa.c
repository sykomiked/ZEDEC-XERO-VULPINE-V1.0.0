/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zx_upcheck_mldsa.c — binds zxu_sigverify_fn to the kernel's ML-DSA-65
 * (FIPS 204, pqsec/pq_mldsa65.c over the vendored pq-crystals reference,
 * checked against NIST ACVP in pqsec/test_pq_kat.c).
 *
 * Kept apart from the checker core because pq_security.h pulls in lpres.h,
 * m5_types.h and surplus.h, which need hosted headers: link this file where
 * pqsec builds (hosted builds, the desktop app), and give the bare-metal
 * checker another zxu_sigverify_fn if pqsec does not build there.
 */
#include "zx_upcheck.h"
#include "../pqsec/pq_security.h"

#if ZXU_PK_BYTES != PQ_MLDSA65_PK_BYTES || ZXU_SIG_BYTES != PQ_MLDSA65_SIG_BYTES
#    error "zx_upcheck key/signature sizes do not match ML-DSA-65"
#endif

bool zxu_mldsa65_verify(void *ctx, const uint8_t pk[ZXU_PK_BYTES], const uint8_t *msg, uint32_t len,
                        const uint8_t *dom, uint32_t dom_len, const uint8_t sig[ZXU_SIG_BYTES])
{
    (void) ctx;
    return pq_mldsa65_verify(pk, msg, len, dom, dom_len, sig);
}
