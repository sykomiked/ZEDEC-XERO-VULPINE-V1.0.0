/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zt_gguf.h — read GGUF model files (the llama.cpp format) into the tensor
 * engine.
 *
 *   T18 GGUF.  A model file is parsed in place from one memory buffer (the
 *       host maps the file; nothing is copied or allocated). zt_gguf_open
 *       walks the whole header first and refuses a file whose counts,
 *       strings, arrays or tensor extents run past the buffer, so every
 *       later lookup is bounds-safe. Metadata values and tensors are found
 *       by name. Floating-point fields are decoded from their bits with
 *       integer arithmetic (no FPU), rounding to nearest: f32 and f16
 *       metadata to Q16, weights to Q16 rows. Q8_0 blocks convert to
 *       zt_q8_t losslessly, because the block size is the same (32) and an
 *       f16 scale is an 11-bit integer times a power of two, which the
 *       scale/shift pair holds exactly.
 *       Supported weight types: F32, F16, BF16, Q8_0, Q4_0, Q5_0, Q5_1,
 *       Q4_K, Q6_K (the types in Q8_0, Q4_0 and Q4_K_M files, including the
 *       Q5_0/Q5_1 tensors of Q4_K_M files of models whose width is not a
 *       multiple of 256). Q5_0/Q5_1 follow ggml's dequantize_row_q5_0/q5_1
 *       bit layout and are decoded exactly, then rounded once to Q16 (ggml
 *       itself computes d*q + m in f32, so it may differ by one f32
 *       rounding; checked by test_zt_q5). Others are reported as
 *       unsupported, never guessed. GGUF versions 2 and 3, little-endian.
 */
#ifndef ZT_GGUF_H
#define ZT_GGUF_H

#include <stdint.h>
#include <stdbool.h>
#include "zt.h"

/* metadata value types */
enum {
    ZT_GGUF_U8 = 0,
    ZT_GGUF_I8 = 1,
    ZT_GGUF_U16 = 2,
    ZT_GGUF_I16 = 3,
    ZT_GGUF_U32 = 4,
    ZT_GGUF_I32 = 5,
    ZT_GGUF_F32 = 6,
    ZT_GGUF_BOOL = 7,
    ZT_GGUF_STRING = 8,
    ZT_GGUF_ARRAY = 9,
    ZT_GGUF_U64 = 10,
    ZT_GGUF_I64 = 11,
    ZT_GGUF_F64 = 12
};

/* tensor element types (ggml numbering) */
enum {
    ZT_GGML_F32 = 0,
    ZT_GGML_F16 = 1,
    ZT_GGML_Q4_0 = 2,
    ZT_GGML_Q5_0 = 6,
    ZT_GGML_Q5_1 = 7,
    ZT_GGML_Q8_0 = 8,
    ZT_GGML_Q4_K = 12,
    ZT_GGML_Q6_K = 14,
    ZT_GGML_BF16 = 30
};

#define ZT_GGUF_MAX_DIMS 4u

typedef struct {
    const uint8_t *p; /* not NUL-terminated */
    uint64_t len;
} zt_gguf_str_t;

typedef struct {
    const uint8_t *buf;
    uint64_t size;
    uint32_t version;
    uint64_t n_tensors, n_kv;
    uint64_t kv_off;    /* first key/value pair */
    uint64_t tinfo_off; /* first tensor info */
    uint64_t data_off;  /* start of tensor data (aligned) */
    uint32_t alignment;
} zt_gguf_t;

/* A metadata value: scalars are widened; strings and arrays point into the
 * buffer. For arrays, elem_type/count describe the elements and data points
 * at the first one. */
typedef struct {
    uint32_t type;
    uint64_t u; /* unsigned scalars and bool; raw bits of f32 and f64 */
    int64_t i;  /* signed scalars */
    zt_fx q16;  /* f32 / f64 scalars, rounded to Q16 (saturating) */
    zt_gguf_str_t str;
    uint32_t elem_type;
    uint64_t count;
    uint64_t data_off; /* array: offset of the first element */
} zt_gguf_val_t;

typedef struct {
    zt_gguf_str_t name;
    uint32_t n_dims;
    uint64_t dims[ZT_GGUF_MAX_DIMS]; /* dims[0] is the row length */
    uint32_t type;
    uint64_t n_elems;
    uint64_t n_bytes;
    const uint8_t *data;
} zt_gguf_tensor_t;

/* Error codes (negative). */
enum {
    ZT_GGUF_OK = 0,
    ZT_GGUF_EMAGIC = -1,
    ZT_GGUF_EVERSION = -2,
    ZT_GGUF_ETRUNC = -3,
    ZT_GGUF_ETYPE = -4,
    ZT_GGUF_ERANGE = -5,
    ZT_GGUF_ENOTFOUND = -6,
    ZT_GGUF_EUNSUPPORTED = -7
};

int32_t zt_gguf_open(zt_gguf_t *g, const uint8_t *buf, uint64_t size);

bool zt_gguf_str_eq(zt_gguf_str_t s, const char *z);

/* Metadata: by index, or by key. */
int32_t zt_gguf_kv(const zt_gguf_t *g, uint64_t index, zt_gguf_str_t *key, zt_gguf_val_t *val);
int32_t zt_gguf_find(const zt_gguf_t *g, const char *key, zt_gguf_val_t *val);
/* Convenience: a scalar integer key, or def when absent or not an integer. */
int64_t zt_gguf_get_int(const zt_gguf_t *g, const char *key, int64_t def);

/* Array elements (the array came from zt_gguf_find/zt_gguf_kv). Strings are
 * variable length, so string element i costs a walk from the start; use
 * zt_gguf_arr_strings to visit them all in one pass. */
int32_t zt_gguf_arr_int(const zt_gguf_t *g, const zt_gguf_val_t *arr, uint64_t i, int64_t *out);
int32_t zt_gguf_arr_q16(const zt_gguf_t *g, const zt_gguf_val_t *arr, uint64_t i, zt_fx *out);
int32_t zt_gguf_arr_str(const zt_gguf_t *g, const zt_gguf_val_t *arr, uint64_t i,
                        zt_gguf_str_t *out);
/* Calls fn(ctx, i, s) for every string; stops early when fn returns false. */
int32_t zt_gguf_arr_strings(const zt_gguf_t *g, const zt_gguf_val_t *arr,
                            bool (*fn)(void *ctx, uint64_t i, zt_gguf_str_t s), void *ctx);

/* Tensors: by index, or by name. */
int32_t zt_gguf_tensor(const zt_gguf_t *g, uint64_t index, zt_gguf_tensor_t *t);
int32_t zt_gguf_find_tensor(const zt_gguf_t *g, const char *name, zt_gguf_tensor_t *t);

/* Size of n elements of a type, or 0 when unsupported or n is not a whole
 * number of blocks. */
uint64_t zt_ggml_bytes(uint32_t type, uint64_t n);
uint32_t zt_ggml_block(uint32_t type); /* elements per block, 0 if unsupported */

/* Dequantise elements [first, first + n) of a tensor to Q16. first and n
 * must be multiples of the type's block. */
int32_t zt_gguf_dequant(const zt_gguf_tensor_t *t, uint64_t first, uint64_t n, zt_fx *out);
/* Q8_0 rows to zt_q8_t blocks, exactly. Other types are dequantised and
 * re-quantised with zt_quantize. first and n multiples of 32. */
int32_t zt_gguf_to_q8(const zt_gguf_tensor_t *t, uint64_t first, uint64_t n, zt_q8_t *out,
                      zt_fx *scratch);

/* Bit-exact float decoding, rounded to nearest Q16, saturating. */
zt_fx zt_f32_to_q16(uint32_t bits);
zt_fx zt_f16_to_q16(uint16_t bits);
zt_fx zt_bf16_to_q16(uint16_t bits);

#endif /* ZT_GGUF_H */
