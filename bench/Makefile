# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Benchmarks. See bench/README.md. Only the qwen target downloads (a model,
# into ~/.cache/zxv/models, never into the repository).
#   make -C bench legacy            golden-trace check of the legacy transcoders
#   make -C bench legacy-bench      the same plus a throughput figure
#   make -C bench zt_bench          build the tensor-engine driver
#   make -C bench smoke             build both and run the offline smoke checks
#   make -C bench tensor MODEL=path/to/model.gguf [N=32] [PROMPT="..."]
#   make -C bench qwen [ARM64=1]    Qwen2.5-1.5B-Instruct Q4_K_M: speed, cross-build bit check, quality
#   make -C bench zt_dump zt_golden logit dump and golden-ratio experiment tools (see
#                                   bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md)
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

.PHONY: all legacy legacy-bench zt_bench zt_dump zt_golden smoke tensor qwen
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

$(OUT)/zt_dump: tensor/zt_dump.c $(TENSOR_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) -I$(K)/include -I$(K)/src/tensor tensor/zt_dump.c $(TENSOR_SRCS) -lm -lpthread -o $@

GOLDEN_SRCS := $(K)/src/tensor/zt_lattice.c $(K)/src/tensor/zt_coil.c $(K)/src/tensor/zt_holo.c
$(OUT)/zt_golden: tensor/zt_golden.c $(TENSOR_SRCS) $(GOLDEN_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) -I$(K)/include -I$(K)/src/tensor -I$(K)/src/e8 tensor/zt_golden.c $(TENSOR_SRCS) \
		$(GOLDEN_SRCS) -lm -lpthread -o $@

zt_dump: $(OUT)/zt_dump
zt_golden: $(OUT)/zt_golden

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

# Real-model test (bench/tensor/real_model_test.py). ARM64=1 also cross-builds
# zt_bench for aarch64 and runs it under qemu-aarch64 to check the bits match.
$(OUT)/zt_bench_arm64: tensor/zt_bench.c $(TENSOR_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	aarch64-linux-gnu-gcc $(CFLAGS) -static -I$(K)/include -I$(K)/src/tensor tensor/zt_bench.c $(TENSOR_SRCS) -lm -lpthread -o $@

QWEN_BENCH := --bench $(OUT)/zt_bench
ifeq ($(ARM64),1)
qwen: $(OUT)/zt_bench_arm64
QWEN_BENCH += --bench "qemu-aarch64 $(OUT)/zt_bench_arm64"
endif
qwen: $(OUT)/zt_bench
	$(PY) tensor/real_model_test.py $(QWEN_BENCH) -n 64 --json $(OUT)/qwen_results.json
