/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* write_test_model.c FILE — write the smallest model zxvd can really run: the
 * tiny qwen2 test model (random weights, kernel/src/tensor/
 * test_model_fixture.h) with the qwen2 tokenizer fixture, as one GGUF
 * (test_model_file.h). With it in --models-dir, swarm.ask goes through the
 * forward pass on the integer engine under the budget gate and the answer
 * tag, exactly as with a downloaded model; its words are noise, because its
 * weights are random. tests/browser/test_browser.sh uses it for the browser's
 * companion. test_write_gguf.c writes the tokenizer alone, which loads but
 * cannot generate. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zt_gguf.h"
#include "test_model_fixture.h"
#include "test_tok_fixture.h"
#include "test_fixture_rebuild.h"
#include "test_model_file.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: write_test_model FILE\n");
        return 2;
    }
    const mf_variant_t *v = &MF_VARIANTS[0]; /* qwen2: the tokenizer fixture's vocabulary */
    uint8_t *model = rebuild(v);
    uint64_t n = 0;
    uint8_t *file = model ? model_with_tokenizer(model, v->file_len, &n) : NULL;
    free(model);
    if (!file) {
        fprintf(stderr, "write_test_model: could not build the model\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    size_t w = f ? fwrite(file, 1, (size_t) n, f) : 0;
    int ok = f && fclose(f) == 0 && w == n;
    free(file);
    if (!ok) {
        fprintf(stderr, "write_test_model: could not write %s\n", argv[1]);
        return 1;
    }
    (void) TOK_IDS_QWEN2;
    (void) TOK_NIDS_QWEN2;
    (void) TOK_IDS_LLAMA_BPE;
    (void) TOK_NIDS_LLAMA_BPE;
    (void) TOK_TEXTS;
    return 0;
}
