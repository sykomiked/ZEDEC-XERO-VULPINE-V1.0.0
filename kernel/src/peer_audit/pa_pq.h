/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* pa_pq.h — the signature scheme peer_audit uses: ML-DSA-87 (FIPS 204,
 * category 5) from the pqsec layered hybrid (src/pqsec/pq_mldsa87.c,
 * declared in the freestanding src/pqsec/pq_matrix_algs.h). It is the same
 * scheme and key size as a Vinea node identity, so one node key can sign
 * its network messages and its audit records.
 */
#ifndef ZXV_PA_PQ_H
#define ZXV_PA_PQ_H

#include <stdint.h>
#include <stdbool.h>
#include "../pqsec/pq_matrix_algs.h"

#endif /* ZXV_PA_PQ_H */
