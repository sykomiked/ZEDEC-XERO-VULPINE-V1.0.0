# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# packages/Makefile - three standalone static libraries built from the
# existing kernel sources (nothing is moved or copied into the tree):
#
#   libzxv-pqc     ML-KEM-768/1024, ML-DSA-65/87, SLH-DSA-SHAKE-128s/256s, SHA-3
#   libzxv-tensor  the integer tensor engine (kernel/src/tensor zt*.c)
#   libzxv-legacy  COBOL / Fortran / EBCDIC data codecs, Lightning Rod and
#                  the exact rationals they convert into
#   libzxv-econ    the engine facade: compress (multilateral netting), route
#                  (asymmetric corridors), decompress (ledger postings with
#                  the 0.08889% assurance fee) over pay_ledger
#
#   make -C packages          build all three into packages/build/<lib>/
#   make -C packages check    build, then link and run each smoke example
#                             against only <lib>.a and its staged headers
#   make -C packages clean
#
# Each build/<lib>/ holds lib<name>.a and include/ (the public headers:
# the umbrella zxv_<name>.h plus the kernel headers it needs, staged under
# include/zxv/<module>/ so their relative includes still resolve).

PKG := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
KSRC := $(PKG)/../kernel/src
KINC := $(PKG)/../kernel/include
BUILD ?= $(PKG)/build
ifeq ($(origin CC),default)
CC := gcc
endif
AR ?= ar

# Library code: C11, warnings are errors, no libc beyond what nm -u shows
# (memcpy/memset/memcmp-class symbols; checked in `check`).
LIBCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-strict-aliasing \
	-U_FORTIFY_SOURCE -fno-stack-protector
# Smoke programs: hosted, and must not see the kernel tree. The kernel
# Makefile exports CPATH (kernel/include, ...), so it is dropped here.
SMOKE_CC := env -u CPATH -u C_INCLUDE_PATH $(CC) -std=c11 -O2 -Wall -Wextra -Werror

# ---- libzxv-pqc --------------------------------------------------------
# The ML-DSA-65 / SLH-DSA-128s glue includes pq_security.h, which pulls in
# kernel headers (lpres.h, m5_types.h, surplus.h); they are needed only to
# compile the library, not by its users (zxv_pqc.h re-declares the same
# prototypes, and `check` compiles both headers together to catch drift).
# The *_1024 / *_87 / *_256s wrappers #include their vendored sources.
PQC_SRCS := $(KSRC)/mlkem/keccak.c $(KSRC)/mlkem/mlkem768.c $(KSRC)/mlkem/mlkem_encode.c \
	$(KSRC)/mlkem/mlkem_sample.c $(KSRC)/mlkem/mlkem_ntt.c $(KSRC)/mlkem/mlkem_kpe.c \
	$(KSRC)/pqsec/pq_mldsa65.c $(KSRC)/pqsec/pq_slhdsa.c \
	$(sort $(wildcard $(KSRC)/pqsec/mldsa/*.c)) $(sort $(wildcard $(KSRC)/pqsec/slhdsa/*.c)) \
	$(KSRC)/pqsec/pq_mlkem1024.c $(KSRC)/pqsec/pq_mldsa87.c $(KSRC)/pqsec/pq_slh256s.c
PQC_IFLAGS := -I$(KINC) -I$(KSRC)/modbind -I$(KSRC)/e8 -I$(KSRC)/event_space -I$(KSRC)/surplus \
	-I$(KSRC)/pqsec -I$(KSRC)/mlkem -I$(KSRC)/lpres -I$(KSRC)/edp_risk -I$(KSRC)/trispace
PQC_HDRS := mlkem/keccak.h mlkem/mlkem768.h mlkem/mlkem_kpe.h mlkem/mlkem_ntt.h \
	mlkem/mlkem_encode.h pqsec/pq_matrix_algs.h

# ---- libzxv-tensor -----------------------------------------------------
TENSOR_SRCS := $(addprefix $(KSRC)/tensor/,zt.c zt_coil.c zt_isf.c zt_holo.c zt_lattice.c \
	zt_gguf.c zt_tok.c zt_rope.c zt_model.c)
TENSOR_IFLAGS := -I$(KSRC)/tensor -I$(KSRC)/e8
TENSOR_HDRS := $(addprefix tensor/,zt.h zt_gguf.h zt_tok.h zt_rope.h zt_lattice.h zt_model.h)

# ---- libzxv-legacy -----------------------------------------------------
# Only the data transcoders of kernel/src/legacy (cobol, fortran, ebcdic);
# its telephony codecs (sip, ss7, tcap, ...) are not part of this package.
LEGACY_SRCS := $(KSRC)/legacy/cobol.c $(KSRC)/legacy/fortran.c $(KSRC)/legacy/ebcdic.c \
	$(KSRC)/lightningrod/lightningrod.c $(KSRC)/rational/rational.c
LEGACY_IFLAGS := -I$(KSRC)/legacy -I$(KSRC)/lightningrod -I$(KSRC)/rational
LEGACY_HDRS := legacy/cobol.h legacy/fortran.h legacy/ebcdic.h lightningrod/lightningrod.h \
	rational/rational.h

# ---- libzxv-econ -------------------------------------------------------
# kernel/src/engine/zxv_engine.h and what it stands on. pay_assure borrows
# swarm_capped_split (the anti-monopoly split), pay_util zt_udiv64 (64-bit
# division without libgcc on 32-bit targets).
ECON_SRCS := $(KSRC)/engine/zxv_engine.c $(KSRC)/abacus/abacus.c $(KSRC)/rational/rational.c \
	$(KSRC)/pay/pay_ledger.c $(KSRC)/pay/pay_util.c $(KSRC)/pay/pay_assure.c \
	$(KSRC)/swarm/swarm_market.c $(KSRC)/swarm/swarm_budget.c $(KSRC)/swarm/swarm_emotion.c \
	$(KSRC)/tensor/zt.c $(KSRC)/mlkem/keccak.c
ECON_IFLAGS := -I$(KINC) -I$(KSRC)/modbind -I$(KSRC)/e8 -I$(KSRC)/event_space -I$(KSRC)/surplus \
	-I$(KSRC)/engine -I$(KSRC)/pay -I$(KSRC)/abacus -I$(KSRC)/rational -I$(KSRC)/swarm \
	-I$(KSRC)/tensor -I$(KSRC)/mlkem -I$(KSRC)/zcapital
ECON_HDRS := engine/zxv_engine.h pay/pay_ledger.h pay/pay_util.h pay/pay_rails.h \
	pay/pay_assure.h zcapital/zcap_forms.h abacus/abacus.h rational/rational.h

LIBS := pqc tensor legacy econ

.PHONY: all check clean $(LIBS) $(addprefix check-,$(LIBS))
all: $(LIBS)

# build_lib(name, SRCS, IFLAGS, HDRS)
define build_lib
$(1): $(BUILD)/libzxv-$(1)/libzxv-$(1).a $(BUILD)/libzxv-$(1)/include/zxv_$(1).h

$(BUILD)/libzxv-$(1)/libzxv-$(1).a: $(2) $(PKG)/Makefile
	@rm -rf $(BUILD)/libzxv-$(1)/obj && mkdir -p $(BUILD)/libzxv-$(1)/obj
	@set -e; for s in $(2); do \
		o=$(BUILD)/libzxv-$(1)/obj/$$$$(echo $$$${s#$(KSRC)/} | tr / _ | sed 's/\.c$$$$/.o/'); \
		$(CC) $(LIBCFLAGS) $(3) -c $$$$s -o $$$$o; \
	done
	@rm -f $$@ && $(AR) rcs $$@ $(BUILD)/libzxv-$(1)/obj/*.o
	@echo "  AR  $$@"

$(BUILD)/libzxv-$(1)/include/zxv_$(1).h: $(PKG)/libzxv-$(1)/include/zxv_$(1).h $(addprefix $(KSRC)/,$(4))
	@rm -rf $(BUILD)/libzxv-$(1)/include && mkdir -p $(BUILD)/libzxv-$(1)/include
	@set -e; for h in $(4); do mkdir -p $(BUILD)/libzxv-$(1)/include/zxv/$$$$(dirname $$$$h); \
		cp $(KSRC)/$$$$h $(BUILD)/libzxv-$(1)/include/zxv/$$$$h; done
	@cp $(PKG)/libzxv-$(1)/include/zxv_$(1).h $$@
endef

$(eval $(call build_lib,pqc,$(PQC_SRCS),$(PQC_IFLAGS),$(PQC_HDRS)))
$(eval $(call build_lib,tensor,$(TENSOR_SRCS),$(TENSOR_IFLAGS),$(TENSOR_HDRS)))
$(eval $(call build_lib,legacy,$(LEGACY_SRCS),$(LEGACY_IFLAGS),$(LEGACY_HDRS)))
$(eval $(call build_lib,econ,$(ECON_SRCS),$(ECON_IFLAGS),$(ECON_HDRS)))

# Undefined symbols a library may leave to its host: the compiler's memory
# builtins. Anything else (malloc, printf, a kernel symbol) fails the check.
# libzxv-legacy also needs libgcc's 128-bit division: kernel/src/rational
# computes with __int128 (so that library is for 64-bit targets).
MEM_UNDEF := memcpy|memset|memmove|memcmp
ALLOWED_pqc := $(MEM_UNDEF)
ALLOWED_tensor := $(MEM_UNDEF)
ALLOWED_legacy := $(MEM_UNDEF)|__divti3|__modti3|__divmodti4|__udivti3|__umodti3
ALLOWED_econ := $(ALLOWED_legacy)

# check_lib(name, extra smoke flags)
define check_lib
check-$(1): $(1)
	@u=$$$$(nm -u $(BUILD)/libzxv-$(1)/libzxv-$(1).a | awk 'NF==2 {print $$$$2}' | sort -u); \
	d=$$$$(nm --defined-only $(BUILD)/libzxv-$(1)/libzxv-$(1).a | awk 'NF==3 {print $$$$3}' | sort -u); \
	ext=$$$$(comm -23 <(echo "$$$$u") <(echo "$$$$d") | grep -vxE '$(ALLOWED_$(1))' | grep . || true); \
	if [ -n "$$$$ext" ]; then echo "$$$$ext"; echo "[FAIL] libzxv-$(1): unexpected external symbols"; exit 1; fi; \
	echo "[PASS] libzxv-$(1): external symbols only $$$$(comm -23 <(echo "$$$$u") <(echo "$$$$d") | tr '\n' ' ')"
	@$(SMOKE_CC) $(2) -I$(BUILD)/libzxv-$(1)/include $(PKG)/libzxv-$(1)/examples/smoke.c \
		$(BUILD)/libzxv-$(1)/libzxv-$(1).a -o $(BUILD)/libzxv-$(1)/smoke && \
		$(BUILD)/libzxv-$(1)/smoke > $(BUILD)/libzxv-$(1)/smoke.log 2>&1 && \
		echo "[PASS] libzxv-$(1) smoke ($$$$(tail -1 $(BUILD)/libzxv-$(1)/smoke.log))" || \
		{ cat $(BUILD)/libzxv-$(1)/smoke.log; echo "[FAIL] libzxv-$(1) smoke"; exit 1; }
endef

SHELL := /bin/bash
$(eval $(call check_lib,pqc,))
$(eval $(call check_lib,tensor,))
$(eval $(call check_lib,legacy,))
$(eval $(call check_lib,econ,))

# zxv_pqc.h re-declares prototypes from pq_security.h: compile both in one
# translation unit so a signature change in the kernel header fails here.
check-pqc-decls: pqc
	@printf '#include "pq_security.h"\n#include "zxv_pqc.h"\nint main(void){return 0;}\n' | \
		$(CC) -std=c11 -Wall -Wextra -Werror $(PQC_IFLAGS) -I$(BUILD)/libzxv-pqc/include \
		-x c - -fsyntax-only && echo "[PASS] libzxv-pqc: zxv_pqc.h agrees with pq_security.h" || \
		{ echo "[FAIL] libzxv-pqc: zxv_pqc.h disagrees with pq_security.h"; exit 1; }

check: check-pqc-decls $(addprefix check-,$(LIBS))
	@echo "[PASS] packages: libzxv-pqc, libzxv-tensor, libzxv-legacy, libzxv-econ build and run standalone"

clean:
	rm -rf $(BUILD)
