/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_aimodel.h — the boot model handoff: one verified model, in memory, for
 * the kernel to run.
 *
 * efi_models.h checks every model a golden disc carries against
 * \ZXV\MODELS.SHA. The smallest one listed is the BOOT MODEL: the stub reads
 * it into pages of its own (EfiLoaderData, below ZXAI_MAX_ADDR), hashes it
 * there, and when the hash and size match it also allocates a scratch region
 * for the engine's arena, tokenizer tables, KV cache and generation work, then
 * fills this record. The record sits on a fixed page (ZXAI_REC_ADDR_*) the
 * stub reserves with AllocatePages(AllocateAddress); if the firmware already
 * uses that page there is no handoff and the kernel says so.
 *
 * The kernel (swarm_bootmodel.h) accepts the record only when the magic,
 * version, size and FNV-1a sum hold and both regions lie inside the memory
 * it maps and outside its own image. It then hashes the model a second time
 * itself: the loader's digest and the kernel's are two witnesses of one
 * subject, joined with lpres_kjoin. Only TRUE runs the model.
 *
 * Header-only and freestanding: the stubs (built -fpie, -mgeneral-regs-only,
 * no libc) and the kernels include it; so does the host test. No struct or
 * array initialisers the compiler could lower to memset/memcpy calls. */
#ifndef ZXV_AIMODEL_H
#define ZXV_AIMODEL_H

#include <stdint.h>

#define ZXAI_MAGIC   0x4941585Au /* "ZXAI" little-endian */
#define ZXAI_VERSION 1u

/* The record page, reserved by the stub. x86_64: the page after the
 * framebuffer/handoff page (0x80000). arm64: in the 64 KB below the kernel's
 * load address 0x40080000, after the bootinfo page (0x40070000); efi_tramp.S
 * cleans that range to the point of coherency before the MMU goes off. */
#define ZXAI_REC_ADDR_X86_64 0x81000ull
#define ZXAI_REC_ADDR_ARM64  0x40071000ull

/* Highest address (exclusive) the model and scratch may occupy: what both
 * kernels map. x86_64 maps the low 4 GB (the low 1 GB at boot, the rest on
 * demand, see kernel_main_x86_64.c); arm64 maps RAM from 0x40000000 up to
 * 0x140000000 (arm64_mmu.c, four 1 GB blocks after the first). */
#define ZXAI_MAX_ADDR_X86_64 0x100000000ull
#define ZXAI_MAX_ADDR_ARM64  0x140000000ull

/* Scratch the stub allocates beside the model: the engine's arena, the
 * tokenizer's index tables, the KV cache and the generation work for the boot
 * prompt. The kernel measures its need and refuses (never overruns) a model
 * that needs more than it got. Measured on the golden disc's boot model,
 * Qwen2.5-0.5B-Instruct Q4_K_M (491,400,032 bytes): 315 MiB, most of it the
 * tensors that are not Q4_K/Q6_K/Q8_0, converted into the arena. As many bytes as the
 * model itself, at least 16 MiB, covers that. */
static inline uint64_t zxai_scratch_bytes(uint64_t model_bytes)
{
    uint64_t s = model_bytes;
    if (s < (16ull << 20)) s = 16ull << 20;
    return (s + 0xFFFu) & ~0xFFFull;
}

#define ZXAI_NAME_MAX 120u

typedef struct {
    uint32_t magic;   /* ZXAI_MAGIC */
    uint32_t version; /* ZXAI_VERSION */
    uint32_t bytes;   /* sizeof(zxai_rec_t) */
    uint32_t sum;     /* FNV-1a 32 over the record with sum = 0 */
    uint64_t model_phys, model_bytes;
    uint64_t scratch_phys, scratch_bytes;
    uint8_t sha256[32]; /* the loader's digest of the bytes at model_phys */
    uint32_t name_len;
    char name[ZXAI_NAME_MAX]; /* the file name in \ZXV\MODELS\, not terminated */
    uint32_t _r;
} zxai_rec_t;

typedef char zxai_fits_in_a_page[(sizeof(zxai_rec_t) <= 4096u) ? 1 : -1];

static inline uint32_t zxai_digest(const zxai_rec_t *r)
{
    const uint8_t *b = (const uint8_t *) r;
    const uint32_t sum_at = (uint32_t) __builtin_offsetof(zxai_rec_t, sum);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < (uint32_t) sizeof(zxai_rec_t); i++) {
        uint8_t v = (i >= sum_at && i < sum_at + 4u) ? 0u : b[i];
        h ^= v;
        h *= 16777619u;
    }
    return h;
}

static inline void zxai_seal(zxai_rec_t *r)
{
    r->magic = ZXAI_MAGIC;
    r->version = ZXAI_VERSION;
    r->bytes = (uint32_t) sizeof(zxai_rec_t);
    r->sum = zxai_digest(r);
}

/* 1 when the record is whole, 0 otherwise (absent or damaged). */
static inline int zxai_valid(const zxai_rec_t *r)
{
    return r->magic == ZXAI_MAGIC && r->version == ZXAI_VERSION &&
           r->bytes == (uint32_t) sizeof(zxai_rec_t) && r->sum == zxai_digest(r) &&
           r->name_len <= ZXAI_NAME_MAX;
}

#endif /* ZXV_AIMODEL_H */
