/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* swarm_bootmodel.h — the boot model answers: what [AI_OK] means.
 *
 * ai_boot_selfcheck (swarm_boot.h) proves the arithmetic with a known-answer
 * test and prints [AI_SELFCHECK_OK]; it runs no model. This runs one. The
 * EFI stub hands the kernel a verified GGUF model in memory and a scratch
 * region (kernel/arch/zxv_aimodel.h); swarm_bootmodel_run then:
 *
 *   1. checks both regions: inside [map_lo, map_hi), outside the kernel's
 *      own [kernel_lo, kernel_hi), not overlapping each other;
 *   2. hashes the model with SHA-256 and joins that witness with the
 *      loader's digest (lpres_kjoin): TRUE runs, BOTH (they disagree) and
 *      anything else refuse;
 *   3. opens the GGUF (zt_gguf), loads the weights (zt_model, Q4_K/Q6_K/Q8_0
 *      in place, other types converted into the arena) and the tokenizer
 *      (zt_tok), after checking each one's measured size against what is
 *      left of the scratch;
 *   4. generates a greedy answer to SWARM_BOOTMODEL_PROMPT, at most
 *      SWARM_BOOTMODEL_MAX_TOKENS tokens, stopping at an end token.
 *
 * It prints, through out (each line ends in a newline):
 *   [AI_MODEL] <name> <bytes> bytes, kernel sha256 <16 hex> agrees with the loader
 *   [AI_OK] <name> answered "<text>" (<p> prompt + <g> tokens, digest 0x<8 hex>)
 * or one refusal:
 *   [AI_FAULT] <name> <why>
 * The digest is FNV-1a over the generated token ids (zt_boot_fnv), so a host
 * run of the same file (test_swarm_bootmodel) predicts the bare-metal line
 * bit for bit. The text is the answer's bytes with newlines and other
 * control bytes shown as spaces, cut at SWARM_BOOTMODEL_TEXT_MAX.
 *
 * No model is ever invented: with no handoff the kernel prints
 * [AI_NO_MODEL] (swarm_bootmodel_none) and never [AI_OK].
 *
 * Freestanding, integer only, no allocation: every byte the engine uses
 * comes from the caller's scratch. */
#ifndef SWARM_BOOTMODEL_H
#define SWARM_BOOTMODEL_H

#include <stdint.h>

#define SWARM_BOOTMODEL_PROMPT                                                                     \
    "<|im_start|>system\nYou are the ZXV swarm, running on bare metal.<|im_end|>\n"                \
    "<|im_start|>user\nSay hello in one short sentence.<|im_end|>\n<|im_start|>assistant\n"
#define SWARM_BOOTMODEL_MAX_TOKENS 16u
#define SWARM_BOOTMODEL_N_CTX      256u
#define SWARM_BOOTMODEL_N_BATCH    16u
#define SWARM_BOOTMODEL_TEXT_MAX   160u

/* Steps, reported in swarm_bootmodel_out_t.step (0 = answered). */
enum {
    SWARM_BM_OK = 0,
    SWARM_BM_EREGION = 1,  /* a region outside the map, inside the kernel, or overlapping */
    SWARM_BM_EDIGEST = 2,  /* the kernel's SHA-256 differs from the loader's */
    SWARM_BM_EGGUF = 3,    /* not a GGUF file this engine reads */
    SWARM_BM_EMODEL = 4,   /* not a model this engine runs */
    SWARM_BM_ETOK = 5,     /* no tokenizer this engine reads, or its vocab differs */
    SWARM_BM_ESCRATCH = 6, /* the scratch is smaller than the measured need */
    SWARM_BM_EGEN = 7,     /* generation failed */
};

typedef struct {
    const uint8_t *model;
    uint64_t model_bytes;
    const uint8_t *loader_sha256; /* 32 bytes */
    const char *name;
    uint32_t name_len;
    uint8_t *scratch;
    uint64_t scratch_bytes;
    /* What the kernel may touch, and what it must not. */
    uint64_t map_lo, map_hi;       /* [lo, hi) mapped RAM */
    uint64_t kernel_lo, kernel_hi; /* [lo, hi) the kernel image, BSS and stack */
} swarm_bootmodel_in_t;

typedef struct {
    int32_t step;    /* SWARM_BM_* */
    int32_t engine;  /* the engine's own error code at that step, else 0 */
    uint8_t verdict; /* lpres: TRUE answered; BOTH witnesses disagree; FALSE refused */
    uint8_t kernel_sha256[32];
    uint32_t n_prompt, n_gen;
    uint32_t digest;     /* FNV-1a (zt_boot_fnv) of the generated ids */
    uint64_t need_bytes; /* scratch the engine measured it needs */
    uint32_t text_len;
    char text[SWARM_BOOTMODEL_TEXT_MAX];
} swarm_bootmodel_out_t;

/* Returns res->step. out may be NULL (nothing printed). */
int32_t swarm_bootmodel_run(const swarm_bootmodel_in_t *in, swarm_bootmodel_out_t *res,
                            void (*out)(const char *));

/* Prints "[AI_NO_MODEL] <why>; the AI is not running" for a boot with no
 * model handed over (why: static text). */
void swarm_bootmodel_none(const char *why, void (*out)(const char *));

#endif /* SWARM_BOOTMODEL_H */
