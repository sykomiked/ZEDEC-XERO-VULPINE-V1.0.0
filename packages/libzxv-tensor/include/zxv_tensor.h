/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_tensor.h - public header of libzxv-tensor.
 *
 * The ZXV integer tensor engine as one static library: Q16 fixed point and
 * 8-bit blocks (zt.h), the GGUF reader with F32/F16/BF16/Q8_0/Q4_0/Q5_0/
 * Q5_1/Q4_K/Q6_K weights (zt_gguf.h), the GGUF tokenizer (zt_tok.h), RoPE
 * tables (zt_rope.h), E8/Leech lattice quantisers (zt_lattice.h) and the
 * qwen2/qwen3/llama forward pass (zt_model.h). Integer arithmetic only, no
 * floating point, no allocation: every buffer is the caller's.
 *
 * Tested by the ZXV tree's test_zt* suites (golden values from gguf-py and
 * numpy references, sanitizer builds). NOT formally verified, NOT
 * certified, and no real model has been run end to end (see zt_model.h
 * for the measured and estimated accuracy and speed).
 */
#ifndef ZXV_TENSOR_H
#define ZXV_TENSOR_H

/* system headers first: the kernel headers include them too */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "zxv/tensor/zt.h"
#include "zxv/tensor/zt_gguf.h"
#include "zxv/tensor/zt_tok.h"
#include "zxv/tensor/zt_rope.h"
#include "zxv/tensor/zt_lattice.h"
#include "zxv/tensor/zt_model.h"

#ifdef __cplusplus
}
#endif

#endif /* ZXV_TENSOR_H */
