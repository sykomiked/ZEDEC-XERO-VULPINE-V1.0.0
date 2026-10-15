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
#   make -C bench zt_vlm            image + text driver (SmolVLM-256M; see
#                                   bench/results/2026-10-12-smolvlm/REPORT.md)
#   make -C bench zt_asr [ARM64=1]  speech-to-text driver (Whisper, T25; see
#                                   bench/results/2026-10-12-whisper-base/REPORT.md); ARM64=1
#                                   also cross-builds $(OUT)/zt_asr_arm64 (static, qemu-aarch64)
#   make -C bench zt_dump zt_golden logit dump and golden-ratio experiment tools (see
#                                   bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md)
#   make -C bench zt_tts            text-to-speech driver (OuteTTS + WavTokenizer, T26;
#                                   bench/results/2026-10-12-outetts/REPORT.md)
#   make -C bench zt_tts_arm64      the same, cross-built for aarch64 (run under qemu-aarch64)
CC ?= gcc
OUT ?= /tmp/zxv_bench
K := ../kernel
CFLAGS := -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST
N ?= 32
PROMPT ?= The capital of France is
PY ?= python3
THREADS ?=
BATCH ?=

TENSOR_SRCS := $(K)/src/tensor/zt_model.c $(K)/src/tensor/zt_tok.c $(K)/src/tensor/zt_rope.c \
	$(K)/src/tensor/zt_gguf.c $(K)/src/tensor/zt.c $(K)/src/tensor/zt_simd.c \
	$(K)/src/tensor/zt_simd_x86.c $(K)/src/tensor/zt_simd_sve.c $(K)/src/tensor/zt_simd_neon.c \
	$(K)/src/tensor/zt_simd_rvv.c $(K)/src/tensor/dist/zt_dist.c \
	$(K)/src/tensor/zt_gpu.c
# macOS: the Metal GPU backend (zt_metal.h), used with zt_bench --gpu
ifeq ($(shell uname -s),Darwin)
TENSOR_SRCS += $(K)/src/tensor/zt_metal.m $(K)/src/tensor/zt_accel.c
GPU_FLAGS := -DZT_HAVE_METAL -fno-objc-arc
GPU_LIBS := -framework Metal -framework Foundation
else
# Linux: CUDA (zt_cuda.h, NVIDIA), HIP (zt_hip.h, AMD through ROCm), Vulkan
# (zt_vk.h, any Vulkan GPU) and WebGPU (zt_wgpu.h, a native WebGPU library).
# All open their driver at run time, so none needs a GPU SDK to build.
# Vulkan's kernels are built to SPIR-V by glslangValidator when it is
# installed; without it zt_vk.c builds as stubs and --gpu says so. WebGPU's
# kernels are WGSL (always generated, sh and sed); zt_wgpu.c needs
# webgpu.h: WGPU_INC=<dir holding webgpu/webgpu.h> (wgpu-native's or Dawn's
# include directory), else it builds as stubs. At run time ZT_WGPU_LIB names
# the library built from that header, and ZT_GPU=webgpu picks it.
GPU_SRCS := $(K)/src/tensor/zt_cuda.c $(K)/src/tensor/zt_hip.c $(K)/src/tensor/zt_vk.c \
	$(K)/src/tensor/zt_wgpu.c $(K)/src/tensor/zt_accel.c
GPU_FLAGS := -DZT_HAVE_CUDA -DZT_HAVE_HIP -DZT_HAVE_VULKAN
GPU_LIBS := -ldl
ifneq ($(shell command -v glslangValidator 2>/dev/null),)
GPU_SPV := $(OUT)/zt_vk_spv.h
GPU_FLAGS += -DZT_VK_SPV_H='"$(GPU_SPV)"'
endif
GPU_WGSL := $(OUT)/zt_wgpu_wgsl.h
GPU_FLAGS += -DZT_WGPU_WGSL_H='"$(GPU_WGSL)"' $(if $(WGPU_INC),-I$(WGPU_INC))
endif
NN_SRCS := $(K)/src/tensor/zt_nn.c $(K)/src/tensor/zt_vision.c
LEGACY_SRCS := $(K)/src/legacy/cobol.c $(K)/src/legacy/fortran.c \
	$(K)/src/lightningrod/lightningrod.c $(K)/src/rational/rational.c

.PHONY: all legacy legacy-bench zt_bench zt_asr zt_dump zt_golden zt_vlm zt_tts zt_tts_arm64 smoke tensor qwen
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

$(OUT)/zt_vk_spv.h: $(K)/src/tensor/zt_vk.comp $(K)/src/tensor/gen_zt_vk_spv.sh | $(OUT)
	sh $(K)/src/tensor/gen_zt_vk_spv.sh $@

$(OUT)/zt_wgpu_wgsl.h: $(K)/src/tensor/zt_wgpu.wgsl $(K)/src/tensor/gen_zt_wgpu_wgsl.sh | $(OUT)
	sh $(K)/src/tensor/gen_zt_wgpu_wgsl.sh $@

$(OUT)/zt_bench: tensor/zt_bench.c $(TENSOR_SRCS) $(GPU_SRCS) $(GPU_SPV) $(GPU_WGSL) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) $(GPU_FLAGS) -I$(K)/include -I$(K)/src/tensor -I$(K)/src/tensor/dist tensor/zt_bench.c $(TENSOR_SRCS) $(GPU_SRCS) \
		-lm -lpthread $(GPU_LIBS) -o $@

zt_bench: $(OUT)/zt_bench

$(OUT)/zt_vlm: tensor/zt_vlm.c $(TENSOR_SRCS) $(NN_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) -I$(K)/include -I$(K)/src/tensor tensor/zt_vlm.c $(TENSOR_SRCS) $(NN_SRCS) -lm -lpthread -o $@
zt_vlm: $(OUT)/zt_vlm

$(OUT)/zt_dump: tensor/zt_dump.c $(TENSOR_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) $(GPU_FLAGS) -I$(K)/include -I$(K)/src/tensor -I$(K)/src/tensor/dist tensor/zt_dump.c $(TENSOR_SRCS) \
		-lm -lpthread $(GPU_LIBS) -o $@

GOLDEN_SRCS := $(K)/src/tensor/zt_lattice.c $(K)/src/tensor/zt_coil.c $(K)/src/tensor/zt_holo.c
$(OUT)/zt_golden: tensor/zt_golden.c $(TENSOR_SRCS) $(GOLDEN_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) $(GPU_FLAGS) -I$(K)/include -I$(K)/src/tensor -I$(K)/src/tensor/dist -I$(K)/src/e8 tensor/zt_golden.c \
		$(TENSOR_SRCS) $(GOLDEN_SRCS) -lm -lpthread $(GPU_LIBS) -o $@

ASR_SRCS := $(TENSOR_SRCS) $(NN_SRCS) $(K)/src/tensor/zt_whisper.c
$(OUT)/zt_asr: tensor/zt_asr.c $(ASR_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) -I$(K)/include -I$(K)/src/tensor tensor/zt_asr.c $(ASR_SRCS) -lm -lpthread -o $@
$(OUT)/zt_asr_arm64: tensor/zt_asr.c $(ASR_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	aarch64-linux-gnu-gcc $(CFLAGS) -static -I$(K)/include -I$(K)/src/tensor tensor/zt_asr.c $(ASR_SRCS) -lm -lpthread -o $@

zt_asr: $(OUT)/zt_asr $(if $(filter 1,$(ARM64)),$(OUT)/zt_asr_arm64)

zt_dump: $(OUT)/zt_dump

TTS_SRCS := $(TENSOR_SRCS) $(K)/src/tensor/zt_nn.c $(K)/src/tensor/zt_tts.c
$(OUT)/zt_tts: tensor/zt_tts.c $(TTS_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	$(CC) $(CFLAGS) -I$(K)/include -I$(K)/src/tensor tensor/zt_tts.c $(TTS_SRCS) -lm -lpthread -o $@
$(OUT)/zt_tts_arm64: tensor/zt_tts.c $(TTS_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	aarch64-linux-gnu-gcc $(CFLAGS) -static -I$(K)/include -I$(K)/src/tensor tensor/zt_tts.c $(TTS_SRCS) \
		-lm -lpthread -o $@
zt_tts: $(OUT)/zt_tts
zt_tts_arm64: $(OUT)/zt_tts_arm64
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
		--prompt "$(PROMPT)" $(if $(THREADS),--threads $(THREADS)) $(if $(BATCH),--batch $(BATCH))

# Real-model test (bench/tensor/real_model_test.py). ARM64=1 also cross-builds
# zt_bench for aarch64 and runs it under qemu-aarch64 to check the bits match.
$(OUT)/zt_bench_arm64: tensor/zt_bench.c $(TENSOR_SRCS) $(wildcard $(K)/src/tensor/*.h) | $(OUT)
	aarch64-linux-gnu-gcc $(CFLAGS) -static -I$(K)/include -I$(K)/src/tensor -I$(K)/src/tensor/dist tensor/zt_bench.c $(TENSOR_SRCS) -lm -lpthread -o $@

QWEN_BENCH := --bench $(OUT)/zt_bench
ifeq ($(ARM64),1)
qwen: $(OUT)/zt_bench_arm64
QWEN_BENCH += --bench "qemu-aarch64 $(OUT)/zt_bench_arm64"
endif
qwen: $(OUT)/zt_bench
	$(PY) tensor/real_model_test.py $(QWEN_BENCH) -n 64 --json $(OUT)/qwen_results.json
