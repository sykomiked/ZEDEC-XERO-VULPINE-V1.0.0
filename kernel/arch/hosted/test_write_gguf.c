/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_write_gguf.c — write the tokenizer test model (a real 32 KB GGUF
 * with a qwen2 byte-level BPE vocabulary, from kernel/src/tensor's test
 * fixture) to a file, so the hosted app's model slot can be tested. */
#include <stdio.h>

#include "test_tok_fixture.h"

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 1;
    size_t n = fwrite(TOK_GGUF, 1, TOK_GGUF_LEN, f);
    (void) TOK_IDS_QWEN2;
    (void) TOK_NIDS_QWEN2;
    (void) TOK_IDS_LLAMA_BPE;
    (void) TOK_NIDS_LLAMA_BPE;
    (void) TOK_TEXTS;
    return (fclose(f) == 0 && n == TOK_GGUF_LEN) ? 0 : 1;
}
