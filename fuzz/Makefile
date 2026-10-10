# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# fuzz/ — libFuzzer harnesses for every parser that reads outside bytes, and
# property-based state-machine harnesses for the economic ledgers.
#
#   make -C fuzz replay          build every harness with gcc (no libFuzzer,
#                                ASan+UBSan) and replay the committed corpora
#                                (what `make -C kernel verify-all` runs)
#   make -C fuzz prop            seeded property runs of the econ harnesses
#                                (PROP_ITERS inputs each, deterministic seed)
#   make -C fuzz fuzzers         build the libFuzzer binaries (clang)
#   make -C fuzz run SECS=60     run every fuzzer for SECS seconds each
#   make -C fuzz run-one T=fuzz_ubh_frame SECS=300
#   make -C fuzz smoke           every fuzzer for SMOKE_SECS (CI)
#   make -C fuzz coverage        line/branch/MC/DC coverage of verify-all plus
#                                the corpora (tools/coverage.sh)
#   make -C fuzz clean
#
# New corpus entries go to build/corpus/<target> (not committed); crashes to
# build/artifacts/. Copy a crash into corpus/<target>/ as regress-<name> once
# it is fixed so replay keeps it fixed. See README.md.

SHELL  := /bin/bash
K      ?= ../kernel
S      := $(K)/src
# The kernel Makefile puts these on every compile's search path (after -I).
KABS   := $(abspath $(K))
export CPATH := $(KABS)/include:$(KABS)/src/modbind:$(KABS)/src/e8:$(KABS)/src/event_space:$(KABS)/src/surplus
OUT    ?= build
SECS   ?= 60
SMOKE_SECS ?= 30
JOBS   ?= 1
CC_REPLAY ?= gcc
CC_FUZZ   ?= clang
REPLAY_SAN    ?= -fsanitize=address,undefined -fno-sanitize-recover=all
# -Wno-maybe-uninitialized: gcc flags the exact-size empty copies (malloc(0)) the
# harnesses make on purpose; it is a false positive there. clang has no such
# warning (and would reject the unknown option under -Werror).
REPLAY_WNO ?= $(if $(findstring clang,$(notdir $(CC_REPLAY))),,-Wno-maybe-uninitialized)
REPLAY_CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror $(REPLAY_WNO) -D_DEFAULT_SOURCE \
                 $(REPLAY_SAN)
IGNORELIST    := $(abspath sanitizer-ignorelist.txt)
FUZZ_CFLAGS   ?= -std=c11 -O1 -g -D_DEFAULT_SOURCE -fsanitize=fuzzer,address,undefined \
                 -fno-sanitize-recover=all -fsanitize-ignorelist=$(IGNORELIST)
# gcc has no ignorelist: targets that link the vendored ed25519 (whose ref10
# carries left-shift negative limbs on purpose, see sanitizer-ignorelist.txt)
# drop only that one check in the gcc replay build.
ED25519_GCC := -fno-sanitize=shift-base
FUZZ_FLAGS    ?= -timeout=20 -rss_limit_mb=4096 -print_final_stats=1
PROP_ITERS ?= 20000
PROP_SEED  ?= 20261010
PROP_MAXLEN ?= 4096

IPFSN := $(S)/ipfs_node/ipfsn_multiformats.c $(S)/ipfs_node/ipfsn_unixfs.c \
         $(S)/ipfs_node/ipfsn_store.c $(S)/ipfs_node/ipfsn_net.c $(S)/ipfs_node/ipfsn_ubh.c \
         $(S)/ipfs_node/ipfsn_fidx.c $(S)/robin_debanks/sha256.c $(S)/tls/aead.c \
         $(S)/tls/hkdf.c $(S)/ubh/ubh.c $(S)/event_space/event_envelope.c
MLKEM := $(S)/mlkem/keccak.c $(S)/mlkem/mlkem768.c $(S)/mlkem/mlkem_encode.c \
         $(S)/mlkem/mlkem_sample.c $(S)/mlkem/mlkem_ntt.c $(S)/mlkem/mlkem_kpe.c
MLDSA := $(S)/pqsec/pq_mldsa65.c $(sort $(wildcard $(S)/pqsec/mldsa/[a-z]*.c))

# ---- parsers ---------------------------------------------------------------
PARSERS := fuzz_cobol_copybook fuzz_ubh_frame fuzz_gguf_loader fuzz_fat32_mount fuzz_vinea \
           fuzz_pay_xml fuzz_tls_record fuzz_tls_handshake fuzz_freight fuzz_upcheck \
           fuzz_bootlegger fuzz_ipfs_cid fuzz_ipfs_dag

DIR_fuzz_cobol_copybook := parsers
INC_fuzz_cobol_copybook := -I$(S)/legacy
SRC_fuzz_cobol_copybook := $(S)/legacy/cobol.c
DEF_fuzz_cobol_copybook := -DTEST_HOST

DIR_fuzz_ubh_frame := parsers
INC_fuzz_ubh_frame := -I$(S)/event_space -I$(S)/ubh
SRC_fuzz_ubh_frame := $(S)/ubh/ubh.c $(S)/event_space/event_envelope.c

DIR_fuzz_gguf_loader := parsers
INC_fuzz_gguf_loader := -I$(S)/tensor
SRC_fuzz_gguf_loader := $(S)/tensor/zt_gguf.c $(S)/tensor/zt.c
DEF_fuzz_gguf_loader := -DTEST_HOST

DIR_fuzz_fat32_mount := parsers
INC_fuzz_fat32_mount := -I$(S)/fat32
SRC_fuzz_fat32_mount := $(S)/fat32/fat32.c
DEF_fuzz_fat32_mount := -DTEST_HOST

DIR_fuzz_vinea := parsers
INC_fuzz_vinea := -I$(S)/vinea -I$(S)/zcapital -I$(S)/lpres -I$(S)/edp_risk
SRC_fuzz_vinea := $(addprefix $(S)/vinea/,vna_agree.c vna_cid.c vna_cmd.c vna_common.c \
                  vna_econ.c vna_file.c vna_frame.c vna_id.c vna_kad.c vna_lan.c vna_node.c \
                  vna_schema.c vna_session.c vna_wire.c) \
                  $(MLDSA) $(MLKEM) $(S)/tls/x25519.c $(S)/tls/aead.c $(S)/tls/hkdf.c \
                  $(S)/robin_debanks/sha256.c $(S)/swarm/swarm_budget.c \
                  $(S)/swarm/swarm_emotion.c $(S)/swarm/swarm_market.c $(S)/swarm/swarm_hk.c \
                  $(S)/ubh/ubh.c $(S)/event_space/event_envelope.c $(S)/zcapital/zcapital.c
DEF_fuzz_vinea := -DTEST_HOST

DIR_fuzz_pay_xml := parsers
INC_fuzz_pay_xml := -I$(K)/include -I$(S)/modbind -I$(S)/pay -I$(S)/tensor -I$(S)/mlkem \
                    -I$(S)/swarm -I$(S)/robin_debanks -I$(S)/zcapital -I$(S)/surplus
SRC_fuzz_pay_xml := $(sort $(wildcard $(S)/pay/pay_*.c)) $(S)/tensor/zt.c $(S)/mlkem/keccak.c \
                    $(S)/robin_debanks/sha256.c $(S)/swarm/swarm_market.c \
                    $(S)/swarm/swarm_budget.c $(S)/swarm/swarm_emotion.c
DEF_fuzz_pay_xml := -DTEST_HOST

DIR_fuzz_tls_record := parsers
INC_fuzz_tls_record := -I$(S)/tls
SRC_fuzz_tls_record := $(S)/tls/record.c $(S)/tls/aead.c
DEF_fuzz_tls_record := -DTEST_HOST

DIR_fuzz_tls_handshake := parsers
INC_fuzz_tls_handshake := -I$(S)/tls -I$(S)/robin_debanks
SRC_fuzz_tls_handshake := $(S)/tls/handshake.c $(S)/tls/record.c $(S)/tls/aead.c \
                          $(S)/tls/hkdf.c $(S)/tls/x25519.c $(S)/robin_debanks/sha256.c
DEF_fuzz_tls_handshake := -DTEST_HOST

DIR_fuzz_freight := parsers
INC_fuzz_freight := -I$(S)/freight -I$(S)/robin_debanks
SRC_fuzz_freight := $(S)/freight/freight.c $(S)/freight/freight_ops.c $(S)/robin_debanks/sha256.c
DEF_fuzz_freight := -DTEST_HOST

XSAN_fuzz_upcheck := $(ED25519_GCC)
DIR_fuzz_upcheck := parsers
INC_fuzz_upcheck := -I$(S)/ipfs_node -I$(S)/update -I$(S)/pqsec -I$(S)/mlkem -I$(S)/lpres \
                    -I$(S)/surplus -I$(S)/edp_risk
SRC_fuzz_upcheck := $(S)/update/zx_upcheck.c $(S)/update/zx_ipns.c $(S)/update/zx_upmanifest.c \
                    $(S)/update/zx_upcheck_mldsa.c $(S)/update/update.c \
                    $(S)/ipfs_node/ipfsn_dir.c $(IPFSN) $(S)/robin_debanks/ed25519_verify.c \
                    $(MLDSA)

DIR_fuzz_bootlegger := parsers
INC_fuzz_bootlegger := -I$(K)/include -I$(S)/modbind -I$(S)/mlkem -I$(S)/bootlegger -I$(S)/pqsec \
                       -I$(S)/lpres -I$(S)/surplus -I$(S)/edp_risk -I$(S)/event_space -I$(S)/trispace
SRC_fuzz_bootlegger := $(S)/bootlegger/bootlegger.c $(S)/robin_debanks/sha256.c $(S)/tls/aead.c \
                       $(S)/tls/hkdf.c $(MLKEM) $(MLDSA)
DEF_fuzz_bootlegger := -DTEST_HOST

DIR_fuzz_ipfs_cid := parsers
INC_fuzz_ipfs_cid := -I$(S)/ipfs -I$(S)/ipfs_node -I$(S)/robin_debanks -I$(S)/update \
                     -I$(S)/bridge -I$(S)/surplus
SRC_fuzz_ipfs_cid := $(S)/ipfs/ipfs.c $(S)/bridge/bridge.c $(IPFSN)
DEF_fuzz_ipfs_cid := -DTEST_HOST

DIR_fuzz_ipfs_dag := parsers
INC_fuzz_ipfs_dag := -I$(S)/ipfs_node
SRC_fuzz_ipfs_dag := $(S)/ipfs_node/ipfsn_dir.c $(IPFSN)

# ---- economic invariants ---------------------------------------------------
ECON := fuzz_econ_vino fuzz_econ_pay fuzz_econ_count_house fuzz_econ_triple

DIR_fuzz_econ_vino := econ
INC_fuzz_econ_vino := -I$(K)/include -I$(S)/vino -I$(S)/rmag -I$(S)/lpres
SRC_fuzz_econ_vino := $(S)/vino/vino.c $(S)/rmag/rmag_core.c $(S)/lpres/lpres_core.c
DEF_fuzz_econ_vino := -DTEST_HOST

DIR_fuzz_econ_pay := econ
INC_fuzz_econ_pay := $(INC_fuzz_pay_xml)
SRC_fuzz_econ_pay := $(SRC_fuzz_pay_xml)
DEF_fuzz_econ_pay := -DTEST_HOST

DIR_fuzz_econ_count_house := econ
INC_fuzz_econ_count_house := -I$(K)/include -I$(S)/count_house -I$(S)/robin_debanks \
                             -I$(S)/edp_risk -I$(S)/porter_house -I$(S)/lpres -I$(S)/surplus
SRC_fuzz_econ_count_house := $(S)/count_house/count_house.c $(S)/robin_debanks/sha256.c \
                             $(S)/robin_debanks/ed25519_verify.c $(S)/robin_debanks/crypto_verify.c \
                             $(S)/edp_risk/edp_risk.c $(S)/surplus/surplus.c \
                             $(S)/porter_house/porter_house.c $(S)/lpres/lpres.c
DEF_fuzz_econ_count_house := -DTEST_HOST

# Built WITHOUT TEST_HOST: surplus_real_t is then Q32.32 int64, so balances are
# exact integers and the conservation law can be checked with ==.
DIR_fuzz_econ_triple := econ
INC_fuzz_econ_triple := -I$(S)/finance -I$(S)/vino -I$(S)/surplus -I$(S)/edp_risk
SRC_fuzz_econ_triple := $(S)/finance/triple_ledger.c

ALL := $(PARSERS) $(ECON)

.PHONY: all replay prop fuzzers run run-one smoke clean list seeds coverage

# Regenerate the binary seeds (corpus/*/seed-*) from the modules' encoders.
SEEDGEN_SRC := $(sort $(SRC_fuzz_vinea) $(SRC_fuzz_freight) $(IPFSN) $(SRC_fuzz_tls_handshake))
$(OUT)/seedgen: tools/seedgen.c $(SEEDGEN_SRC)
	@mkdir -p $(@D)
	$(CC_REPLAY) -std=c11 -O1 -g -D_DEFAULT_SOURCE -DTEST_HOST -Icommon $(INC_fuzz_vinea) \
		$(INC_fuzz_freight) -I$(S)/ipfs_node -I$(S)/tls -I$(S)/tensor -I$(S)/ubh tools/seedgen.c \
		$(SEEDGEN_SRC) -o $@ -lm
seeds: $(OUT)/seedgen
	$(OUT)/seedgen corpus
all: replay

list:
	@echo $(ALL)

define TARGET
$(OUT)/replay/$(1): $(DIR_$(1))/$(1).c $(SRC_$(1)) common/fuzz_main.c common/fuzz_in.h sanitizer-ignorelist.txt
	@mkdir -p $$(@D)
	$(CC_REPLAY) $(REPLAY_CFLAGS) $(XSAN_$(1)) $(DEF_$(1)) -Icommon $(INC_$(1)) $(DIR_$(1))/$(1).c \
		common/fuzz_main.c $(SRC_$(1)) -o $$@ -lm
$(OUT)/fuzz/$(1): $(DIR_$(1))/$(1).c $(SRC_$(1)) common/fuzz_in.h sanitizer-ignorelist.txt
	@mkdir -p $$(@D)
	$(CC_FUZZ) $(FUZZ_CFLAGS) $(DEF_$(1)) -Icommon $(INC_$(1)) $(DIR_$(1))/$(1).c \
		$(SRC_$(1)) -o $$@ -lm
endef
$(foreach t,$(ALL),$(eval $(call TARGET,$(t))))

replay: $(addprefix $(OUT)/replay/,$(ALL))
	@set -e; for t in $(ALL); do \
		if [ -d corpus/$$t ]; then $(OUT)/replay/$$t corpus/$$t; \
		else echo "[FAIL] no corpus for $$t"; exit 1; fi; \
	done; echo "*** FUZZ CORPUS REPLAY PASSED ***"

prop: $(addprefix $(OUT)/replay/,$(ECON))
	@set -e; for t in $(ECON); do \
		$(OUT)/replay/$$t --random $(PROP_SEED) $(PROP_ITERS) $(PROP_MAXLEN); \
	done

fuzzers: $(addprefix $(OUT)/fuzz/,$(ALL))

# One fuzzer: fresh work corpus seeded from the committed one, log kept.
run-one: $(OUT)/fuzz/$(T)
	@mkdir -p $(OUT)/corpus/$(T) $(OUT)/artifacts $(OUT)/logs
	$(OUT)/fuzz/$(T) $(FUZZ_FLAGS) -max_total_time=$(SECS) \
		-artifact_prefix=$(OUT)/artifacts/$(T)- $(OUT)/corpus/$(T) corpus/$(T) \
		2>&1 | tee $(OUT)/logs/$(T).log | grep -E "^#[0-9]+ +(INITED|DONE)|stat::|ERROR|SUMMARY" ; \
		test $${PIPESTATUS[0]} -eq 0

run: fuzzers
	@set -e; for t in $(ALL); do $(MAKE) --no-print-directory run-one T=$$t SECS=$(SECS); done

smoke: fuzzers
	@set -e; for t in $(ALL); do $(MAKE) --no-print-directory run-one T=$$t SECS=$(SMOKE_SECS); done

coverage:
	tools/coverage.sh

clean:
	rm -rf $(OUT)
