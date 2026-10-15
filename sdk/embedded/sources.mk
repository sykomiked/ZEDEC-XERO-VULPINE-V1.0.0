# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/embedded/sources.mk — the libzxv subset the embedded kits compile,
# straight from sdk/libzxv/src and kernel/src (nothing copied). Included by
# ./Makefile and freertos-zxv/Makefile; zephyr-zxv/CMakeLists.txt keeps the
# same list (tools/check_lists.py checks they agree).
#
# Set ZXE_ROOT to this directory before including.
#
#   ZXE_PQC=1       add ML-KEM-1024, ML-DSA-87, Ed25519 (gateways; size in README)
#   ZXE_TENSOR=1    add the inference engine (host conformance run only)
#   ZXE_PROFILE     exact (default): the kernel triple-ledger mirror is built
#                   as on the host (-DTEST_HOST), bit-identical to libzxv;
#                   its double arithmetic is software floating point on a
#                   core without a double FPU (see README "Floats").
#                   q32: the kernel's Q32.32 integer path. Amounts, rail
#                   totals and the total of all debits are capped at 2^31-1
#                   minor units (PROPOSED_LIBZXV_CHANGES.md, P2, applied).

ZXE_LIBZXV := $(ZXE_ROOT)/../libzxv
ZXE_K      := $(ZXE_ROOT)/../../kernel
ZXE_PROFILE ?= exact

ZXE_LIBZXV_SRC := $(addprefix $(ZXE_LIBZXV)/src/,zxv_api.c zxv_core.c zxv_econ.c zxv_crypto.c \
                  zxv_tensor.c zxv_evm.c zxv_hex.c)

ZXE_KSRC := $(addprefix $(ZXE_K)/src/,iso20022/iso20022.c finance/triple_ledger.c \
            rational/rational.c lpres/lpres.c lpres/lpres_hex.c rmag/rmag.c \
            phase_coord/phase_coord.c harmonic/zt_harmonic_wire.c swarm/swarm_budget.c \
            swarm/swarm_emotion.c mlkem/keccak.c robin_debanks/sha256.c)

ZXE_KINC := $(addprefix -I$(ZXE_K)/src/,iso20022 finance vino_stores vino surplus edp_risk \
            onepolicy rational lpres event_space rmag phase_coord harmonic swarm mlkem e8 \
            modbind trispace) -I$(ZXE_K)/include

ZXE_PORT_INC := -I$(ZXE_ROOT)/port/compat -I$(ZXE_ROOT)/port/include
ZXE_DEFS := -DZXV_NO_TENSOR -DZXV_RANDOM_GETENTROPY -include zxp_prelude.h
# -Dretain=used: kernel/include/zxv_decl.h marks each module's bring-up
# record __attribute__((used, retain)), so a toolchain that honours retain
# (riscv64-unknown-elf here) keeps the bring-up functions and their static
# registries (lpres 16 KB, phase_coord 2 x 21 KB) through --gc-sections.
# libzxv never walks those records; plain `used` lets the linker drop them.
ZXE_DEFS += -Dretain=used

# ZXE_KIT_DEFS go to the kit's own sources (zxe) and tests, which must use
# the same amount limit as the libzxv they are built with.
ifeq ($(ZXE_PROFILE),exact)
  ZXE_DEFS += -DTEST_HOST
  ZXE_KIT_DEFS :=
else ifeq ($(ZXE_PROFILE),q32)
  ZXE_KIT_DEFS := -DZXE_Q32
else
  $(error ZXE_PROFILE must be exact or q32)
endif

ifeq ($(ZXE_PQC),1)
  ZXE_KSRC += $(ZXE_K)/src/pqsec/pq_mlkem1024.c $(ZXE_K)/src/pqsec/pq_mldsa87.c \
              $(ZXE_K)/src/pqsec/mldsa/fips202.c $(wildcard $(ZXE_K)/third_party/ed25519/src/*.c)
  ZXE_KINC += -I$(ZXE_K)/src/pqsec -I$(ZXE_K)/third_party/ed25519/src
else
  ZXE_DEFS += -DZXV_NO_PQC
endif

# Host-only (the conformance run of libzxv's own tests through the port
# layer): the integer inference engine, as in the hosted library. Its
# kernel files run their own thread pool, so they are compiled WITHOUT the
# port prelude (ZXE_KSRC_PLAIN); libzxv's adapter (zxv_tensor.c) still goes
# through the port. Never part of an MCU build.
ZXE_KSRC_PLAIN :=
ifeq ($(ZXE_TENSOR),1)
  ZXE_KSRC_PLAIN += $(addprefix $(ZXE_K)/src/tensor/,zt.c zt_gguf.c zt_tok.c zt_rope.c zt_model.c \
              zt_simd.c zt_simd_x86.c zt_simd_sve.c zt_simd_neon.c zt_simd_rvv.c)
  ZXE_KINC += -I$(ZXE_K)/src/tensor
  ZXE_DEFS := $(filter-out -DZXV_NO_TENSOR,$(ZXE_DEFS))
endif

ZXE_INC := $(ZXE_PORT_INC) -I$(ZXE_LIBZXV)/include -I$(ZXE_LIBZXV)/src $(ZXE_KINC)

ZXE_PORT_CORE := $(ZXE_ROOT)/port/src/zxp_core.c
ZXE_KIT_SRC   := $(ZXE_ROOT)/zxe/src/zxe_ledger.c $(ZXE_ROOT)/zxe/src/zxe_clock.c
ZXE_KIT_INC   := -I$(ZXE_ROOT)/zxe/include -I$(ZXE_ROOT)/port/include -I$(ZXE_LIBZXV)/include
