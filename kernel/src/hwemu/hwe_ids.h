/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwe_ids.h — standard engine contract ids (hwe.h), dependency-free so
 * ZHP-1 profiles (hwp.h) can name contracts without the matrix. */
#ifndef HWE_IDS_H
#define HWE_IDS_H

/* Contract ids ('Z' 'E' family 0x5A45xxxx). The values never change. */
#define HWE_FN_SHA256   0x5A450001u
#define HWE_FN_KECCAK   0x5A450002u
#define HWE_FN_MAC      0x5A450003u
#define HWE_FN_GCD      0x5A450004u
#define HWE_FN_LOGIC64  0x5A450005u
#define HWE_FN_TENSOR   0x5A450006u
#define HWE_CONTRACT_V1 1u

#endif /* HWE_IDS_H */
