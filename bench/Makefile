# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Benchmarks. See bench/README.md. Nothing here downloads anything.
#   make -C bench legacy            golden-trace check of the legacy transcoders
#   make -C bench legacy-bench      the same plus a throughput figure
#   make -C bench zt_bench          build the tensor-engine driver
#   make -C bench smoke             build both and run the offline smoke checks
#   make -C bench tensor MODEL=path/to/model.gguf [N=32] [PROMPT="..."]
CC ?= gcc
OUT ?= /tmp/zxv_bench
K := ../kernel
CFLAGS := -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST
N ?= 32
PROMPT ?= The capital of France is
PY ?= python3

TENSOR_SRCS := $(K)/src/tensor/zt_model.c $(K)/src/tensor/zt_tok.c $(K)/src/tensor/zt_rope.c \
	$(K)/src/tensor/zt_gguf.c $(K)/src/tensor/zt.c $(K)/src/tensor/zt_simd.c \
	$(K)/src/tensor/zt_simd_x86.c $(K)/src/tensor/zt_simd_neon.c
LEGACY_SRCS := $(K)/src/legacy/cobol.c $(K)/src/legacy/fortran.c \
	$(K)/src/lightningrod/lightningrod.c $(K)/src/rational/rational.c

.PHONY: all legacy legacy-bench zt_bench smoke tensor
all: smoke

$(OUT):
	mkdir -p $(OUT)

$(OUT)/legacy_bench: legacy/legacy_bench.c $(LEGACY_SRCS) | $(OUT)
	$(CC) $(CFLAGS) -I$(K)/include -I$(K)/src/legacy -I$(K)/src/lightningrod -I$(K)/src/rational \
		legacy/legacy_bench.c $(LEGACY_SRCS) -o $@

legacy: $(OUT)/legacy_bench
	$(OUT)/legacy_bench legacy/golden_legacy.tsv

legacy-bench: $(OUT)/legacy_bench
	$(OUT)/legacy_bench legacy/golden_legacy.tsv --iters 200000

$(OUT)/zt_bench: tensor/zt_bench.c $(TENSOR_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) -I$(K)/include -I$(K)/src/tensor tensor/zt_bench.c $(TENSOR_SRCS) -lm -lpthread -o $@

zt_bench: $(OUT)/zt_bench

# Offline: the legacy golden check, and zt_bench on the tokenizer-only test GGUF
# (from kernel/src/tensor's fixture), which must be refused cleanly (exit 4)
# because it carries no weights. No model is downloaded.
smoke: legacy $(OUT)/zt_bench
	$(CC) -std=c11 -Wall -Wextra -Werror -I$(K)/src/tensor $(K)/arch/hosted/test_write_gguf.c \
		-o $(OUT)/write_gguf
	$(OUT)/write_gguf $(OUT)/tok_only.gguf
	$(OUT)/zt_bench $(OUT)/tok_only.gguf -n 4; rc=$$?; \
	if [ $$rc -eq 4 ]; then echo "[PASS] zt_bench refuses a weightless GGUF (exit 4)"; \
	else echo "[FAIL] zt_bench exit $$rc on a weightless GGUF, expected 4"; exit 1; fi
	$(PY) tensor/run_tensor_bench.py --self-test

tensor: $(OUT)/zt_bench
	@test -n "$(MODEL)" || { echo "usage: make -C bench tensor MODEL=path/to/model.gguf"; exit 2; }
	$(PY) tensor/run_tensor_bench.py --zt-bench $(OUT)/zt_bench --model "$(MODEL)" -n $(N) \
		--prompt "$(PROMPT)"
