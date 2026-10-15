# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# bearer.mk — roadmap R2.2-R2.6, edge and mesh transport:
#   src/bearer          bearer kinds and capabilities, policy gates, link
#                       selection by measured latency/loss/class/cost and
#                       failover; BLE fragmentation and length-prefixed
#                       streams; LoRa time on air, EU868 duty cycle and US915
#                       dwell time; COBS + CRC-32 serial frames; the Hayes AT
#                       modem driver; AX.25 UI frames and signed cleartext
#                       emergency messages for the ham bands; the devmesh
#                       transport adapter
#   arch/hosted/bearer_host.c   POSIX termios glue (raw serial, modem lines)
# See docs/retail/BEARERS.md.
#
#   cd kernel && make -f Makefile -f mk/bearer.mk test-bearer
#
# test-bearer: every test under ASan/UBSan, the sources warning-free under
# clang, and the library compiled freestanding for aarch64 (general
# registers only) and i386 with its undefined symbols checked (only its own
# br_* entry points: no libc, no 64-bit division helpers).

ZXV_TMP ?= /tmp
BEARER_TMP := $(ZXV_TMP)/zxv_bearer
BEARER_CC := gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wshadow -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST
BEARER_INC := -Iinclude -Isrc/pqsec -Isrc/mlkem -Isrc/pay -Isrc/swarm -Isrc/tensor -Isrc/lpres -Isrc/surplus \
	-Isrc/edp_risk -Isrc/event_space -Isrc/modbind -Isrc/trispace -Isrc/e8
BEARER_SRCS := src/bearer/bearer.c src/bearer/br_frag.c src/bearer/br_ble.c src/bearer/br_lora.c \
	src/bearer/br_serial.c src/bearer/br_modem.c src/bearer/br_ax25.c src/bearer/br_ham.c \
	src/bearer/br_devmesh.c
BEARER_PQ := src/pqsec/pq_matrix.c src/pqsec/pq_mlkem1024.c src/pqsec/pq_mldsa87.c src/pqsec/pq_hqc5.c \
	src/pqsec/pq_slh256s.c src/pqsec/pq_mldsa65.c src/pqsec/pq_slhdsa.c \
	$(sort $(wildcard src/pqsec/mldsa/*.c)) $(sort $(wildcard src/pqsec/slhdsa/*.c)) \
	src/mlkem/keccak.c src/mlkem/mlkem768.c src/mlkem/mlkem_encode.c src/mlkem/mlkem_sample.c \
	src/mlkem/mlkem_ntt.c src/mlkem/mlkem_kpe.c src/tls/x25519.c src/tls/aead.c
BEARER_DM := src/devmesh/devmesh.c src/devmesh/dm_sync.c src/devmesh/dm_remote.c src/devmesh/dm_capmkt.c \
	src/capmkt/capmkt.c src/pay/pay_util.c src/pay/pay_assure.c src/swarm/swarm_market.c \
	src/swarm/swarm_budget.c src/swarm/swarm_emotion.c src/tensor/zt.c

# $(call bearer_test,name,sources): build, run, report "[PASS] name (N checks)".
define bearer_test
	@$(BEARER_CC) $(BEARER_INC) $(2) -lm -o $(BEARER_TMP)/$(1) 2>$(BEARER_TMP)/$(1).build || { cat $(BEARER_TMP)/$(1).build; echo "[FAIL] $(1) build"; exit 1; }
	@$(BEARER_TMP)/$(1) > $(BEARER_TMP)/$(1).log 2>&1 && echo "[PASS] $(1) ($$(tail -1 $(BEARER_TMP)/$(1).log))" || { grep -v '^\[PASS\]' $(BEARER_TMP)/$(1).log; echo "[FAIL] $(1)"; exit 1; }
endef

.PHONY: test-bearer
test-bearer:
	@mkdir -p $(BEARER_TMP)
	$(call bearer_test,test_bearer,src/bearer/test_bearer.c src/bearer/bearer.c)
	$(call bearer_test,test_br_ble,src/bearer/test_br_ble.c src/bearer/br_ble.c src/bearer/br_frag.c src/bearer/bearer.c)
	$(call bearer_test,test_br_lora,src/bearer/test_br_lora.c src/bearer/br_lora.c src/bearer/br_frag.c src/bearer/bearer.c)
	$(call bearer_test,test_br_serial,src/bearer/test_br_serial.c src/bearer/br_serial.c src/bearer/bearer.c)
	$(call bearer_test,test_br_modem,src/bearer/test_br_modem.c src/bearer/br_modem.c src/bearer/br_serial.c src/bearer/bearer.c)
	$(call bearer_test,test_br_ax25,src/bearer/test_br_ax25.c src/bearer/br_ax25.c src/bearer/bearer.c)
	$(call bearer_test,test_br_ham,src/bearer/test_br_ham.c src/bearer/br_ham.c src/bearer/br_ax25.c src/bearer/bearer.c $(BEARER_PQ))
	$(call bearer_test,test_br_devmesh,src/bearer/test_br_devmesh.c src/bearer/br_devmesh.c src/bearer/br_frag.c src/bearer/br_serial.c src/bearer/bearer.c $(BEARER_DM) $(BEARER_PQ))
	$(call bearer_test,test_bearer_host,arch/hosted/test_bearer_host.c arch/hosted/bearer_host.c src/bearer/br_modem.c src/bearer/br_serial.c src/bearer/bearer.c)
	@if command -v clang >/dev/null; then for f in src/bearer/test_*.c arch/hosted/test_bearer_host.c arch/hosted/bearer_host.c $(BEARER_SRCS); do clang -std=c11 -O2 -Wall -Werror -Wextra -Wshadow -DTEST_HOST $(BEARER_INC) -fsyntax-only $$f || { echo "[FAIL] bearer under clang: $$f"; exit 1; }; done; echo "[PASS] bearer warning-free under clang (1 check)"; else echo "[SKIP] bearer under clang (no clang)"; fi
	@for t in aarch64-none-elf i386-none-elf; do x=; [ $$t = aarch64-none-elf ] && x=-mgeneral-regs-only; for f in $(BEARER_SRCS); do clang --target=$$t $$x -ffreestanding -std=c11 -O2 -Wall -Werror -Wextra -Wshadow -c $$f -o $(BEARER_TMP)/bearer_free.o || { echo "[FAIL] bearer freestanding $$t $$f"; exit 1; }; if nm -u $(BEARER_TMP)/bearer_free.o | grep -vE ' br_[a-z0-9_]+$$' | grep -q .; then nm -u $(BEARER_TMP)/bearer_free.o; echo "[FAIL] bearer freestanding: libc or 64-bit division helper in $$f ($$t)"; exit 1; fi; done; done; echo "[PASS] bearer freestanding (aarch64 general-regs-only and i386, 9 files each: no libc, no 64-bit division helpers; 18 checks)"
