/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_model_host.h — the hosted app's model slot: maps one GGUF file and
 * joins it to the kernel tensor engine (kernel/src/tensor).
 *
 *   M1 MAP.    The file is memory-mapped read-only (mmap, or a Windows file
 *      mapping); nothing is copied. zt_gguf_open checks every extent before
 *      anything else reads it, so a truncated or hostile file is refused.
 *   M2 TOKENS. The model's own byte-level BPE tokenizer (zt_tok) is loaded
 *      from the file. SentencePiece and WordPiece files load as "metadata
 *      only" and say so.
 *   M3 DECODE. Text generation needs the forward pass (kernel/src/tensor/
 *      zt_model*). The build compiles it in when it exists. The call into
 *      it lives in one glue file, zxv_zt_glue.c, compiled (with
 *      -DZXV_HAVE_ZT_GLUE) only when both zt_model.c and that file exist;
 *      its contract is zxv_zt_generate below. Without it the slot still
 *      maps, checks and tokenizes, and /api/ask says plainly that this
 *      build cannot generate.
 *   M4 PICK.   With no explicit --model, the first *.gguf (by name) in the
 *      models directory is used. No model at all is a normal state: the
 *      swarm runs without one.
 */
#ifndef ZXV_MODEL_HOST_H
#define ZXV_MODEL_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zt_gguf.h"
#include "zt_tok.h"

typedef struct {
    bool loaded;       /* mapped and its header parsed */
    bool tok_ok;       /* its tokenizer is supported and loaded */
    bool can_generate; /* the forward pass is compiled in */
    char path[1024];
    char name[96];      /* general.name, or the file name */
    char arch[32];      /* general.architecture */
    char tokenizer[24]; /* tokenizer.ggml.model */
    uint64_t bytes, n_tensors;
    int64_t n_layers, n_ctx, n_embd;
    uint32_t n_vocab;
    char status[320]; /* one honest line for the window */
} zxv_model_info_t;

/* Map and check one GGUF file (replacing any open one). 0 or negative. */
int zxv_model_open(const char *path);
/* The first *.gguf in dir, by byte order of the name. 0, or -1 if none. */
int zxv_model_find_in_dir(const char *dir, char *out, size_t cap);
const zxv_model_info_t *zxv_model_info(void);
/* Answer a prompt with the model: 1 when out holds a model answer, 0 when
 * the caller should answer without one (out then holds a note about the
 * model, possibly empty). */
int zxv_model_answer(const char *prompt, char *out, size_t cap);
void zxv_model_close(void);

/* Forward-pass glue (zxv_zt_glue.c, M3). Generate a reply to the prompt's
 * token ids into out (UTF-8, NUL-terminated). Returns the number of tokens
 * generated, or negative with a reason in err. */
int32_t zxv_zt_generate(const zt_gguf_t *g, const zt_tok_t *tok, const int32_t *ids, uint64_t n_ids,
                        char *out, size_t cap, char *err, size_t err_cap);

#endif /* ZXV_MODEL_HOST_H */
