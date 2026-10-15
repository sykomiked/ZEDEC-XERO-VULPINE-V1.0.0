/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_choose.h — the determiner: which kernels run a model on this machine,
 * chosen by measuring, never by what the device is called. For HOSTED
 * builds (the app, zxvd, bench/).
 *
 * zt_choose times the loaded model itself on every engine this machine can
 * run: each CPU kernel set this CPU has (zt_simd.h: AVX-512 VNNI, AVX-512,
 * AVX2, SVE, NEON, RVV; the C reference only when there is no other) with
 * its thread pool, and each GPU backend that opens and takes the weights
 * (zt_accel.h: Metal, CUDA, HIP, Vulkan). Each engine reads the same short
 * prompt and takes one more token; an engine whose logits differ from the
 * first CPU engine's in any bit is refused (rule R1). Of the rest:
 *
 *   - the CPU engine is the one fastest at one-token steps (generation is
 *     made of them); it also runs whatever a GPU declines;
 *   - the GPU (the fastest over a prompt plus as many generated tokens)
 *     takes prompts when it reads them faster than that CPU engine, and
 *     one-token steps when it takes them faster, through
 *     zt_accel_set_tokens; a GPU faster at neither is closed.
 *
 * Every choice is written as one line (zt_engine_record), for logs and for
 * replay:
 *
 *   zt-choice/1 cpu=avx512vnni/8 gpu=vulkan:2-max model=qwen2/1536x28 |
 *     cpu:avx512vnni/8 prompt 412 us/token step 17210 us; ...;
 *     gpu:cuda unavailable (no driver)
 *
 * ZT_CHOICE="cpu=avx512vnni/8 gpu=vulkan:2-max" (the part after
 * "zt-choice/1 " up to " model=") builds that engine again without measuring;
 * gpu=none keeps it on the CPU. ZT_CHOICE_LOG=FILE appends each record to
 * FILE. ZT_GPU (zt_accel.h), ZT_KERN and ZT_THREADS (zt_simd.h) narrow what
 * is measured.
 *
 * zt_choose_host is the machine side of the same probe (cores, memory,
 * kernel sets, GPUs), for planners that size a swarm before loading. */
#ifndef ZT_CHOOSE_H
#define ZT_CHOOSE_H

#include <stdbool.h>
#include <stddef.h>

#include "zt_accel.h"
#include "zt_model.h"

#define ZT_CHOOSE_MAX_GPUS 8u

/* ---- the machine ---- */
typedef struct {
    uint32_t ncpu;           /* logical CPUs online */
    uint64_t ram_bytes;      /* physical memory (0: unknown) */
    uint64_t ram_free_bytes; /* available now (0: unknown) */
    uint32_t n_sets;         /* CPU kernel sets this CPU runs, fastest first, "c" last */
    const char *sets[8];
    uint32_t n_gpus;                    /* GPUs that opened (only when asked to open them) */
    char gpus[ZT_CHOOSE_MAX_GPUS][128]; /* "vulkan: AMD Radeon RX 7900 XTX" */
    char why[384];                      /* why the other backends did not open */
} zt_host_t;

/* Fill h. open_gpus: open each GPU backend to see whether it works (this
 * compiles its kernels: seconds on some drivers); else n_gpus is 0. */
void zt_choose_host(zt_host_t *h, bool open_gpus);
/* h as one line ("cpus=8 ram=128.0GiB free=97.3GiB sets=neon,c gpus=metal: Apple M3 Max"). */
void zt_host_line(const zt_host_t *h, char *out, size_t cap);

/* ---- one model ---- */
typedef struct zt_engine zt_engine_t;

typedef struct {
    const void *weights; /* the bytes the model's tensors point into (the GGUF) */
    uint64_t weights_len;
    zt_model_state_t *s; /* an initialised state the measurement may use */
    void *state_mem;     /* its memory: aligned to zt_accel_state_align() */
    uint64_t state_len;  /* rounded up to it */
} zt_choose_in_t;

/* Measure (or replay ZT_CHOICE) and set m->kern to the chosen kernels, which
 * live in the engine: free the engine only after the model. in->s is left
 * empty (sought to 0). The chosen GPU, if any, keeps in->state_mem mapped.
 * Null only when out of memory (m->kern is then zt_simd_default()). */
zt_engine_t *zt_choose(zt_model_t *m, const zt_choose_in_t *in);
void zt_engine_free(zt_engine_t *e);

/* The choice as a line (see above). */
const char *zt_engine_record(const zt_engine_t *e);
/* What runs the model, short: "vulkan: llvmpipe (prompts and steps)",
 * "metal: Apple M3 Max (prompts); avx512vnni/8 (steps)", "neon/8". */
const char *zt_engine_device(const zt_engine_t *e);
/* The chosen GPU (null: none), to share more states with it. */
zt_accel_t *zt_engine_accel(zt_engine_t *e);

#endif /* ZT_CHOOSE_H */
