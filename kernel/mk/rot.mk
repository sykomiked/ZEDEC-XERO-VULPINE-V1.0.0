# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# rot.mk — roadmap R1.4/R1.5, hardware root of trust track:
#   src/bootattest  boot-state attestation (measured boot log, signed quote,
#                   signed golden manifest with a CID-style id)
#   src/ztp         zero-touch provisioning (ZHP-1 provisioning profile,
#                   QR claim, transfer, reset)
#   src/qr          QR Code encoder (byte mode, level M, versions 1-10)
# See docs/retail/ROOT_OF_TRUST.md.
#
#   cd kernel && make -f Makefile -f mk/rot.mk test-rot
#
# Each test runs under ASan/UBSan; then every library is compiled
# freestanding for aarch64 (no FP registers) and i386 and its undefined
# symbols are checked: only the named crypto/library entry points, no libc,
# no compiler helpers (so no 64-bit division helpers on i386).

ZXV_TMP ?= /tmp
ROT_TMP := $(ZXV_TMP)/zxv_rot
ROT_CC := gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wshadow -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST
ROT_INC := -Iinclude -Isrc/modbind -Isrc/e8 -Isrc/event_space -Isrc/surplus
ROT_MLDSA := src/pqsec/pq_mldsa87.c src/pqsec/mldsa/fips202.c
ROT_BA_DEPS := src/bootattest/ba.c src/robin_debanks/sha256.c src/mlkem/keccak.c $(ROT_MLDSA)
ROT_ZTP_DEPS := src/ztp/ztp.c src/qr/qr.c src/robin_debanks/crypto_verify.c $(ROT_BA_DEPS)
ROT_FREE := clang -ffreestanding -std=c11 -O2 -Wall -Werror -Wextra

# $(call rot_run,name,binary-args...) — run a built test, print one line.
define rot_run
	$(ROT_TMP)/$(1) > $(ROT_TMP)/$(1).log 2>&1 && echo "[PASS] $(1) ($$(tail -1 $(ROT_TMP)/$(1).log | sed -E 's/^OK: ([0-9]+) checks.*/\1 checks/'))" || { cat $(ROT_TMP)/$(1).log; echo "[FAIL] $(1)"; exit 1; }
endef

# $(call rot_free,label,source,extra-flags,allowed-symbol-regex)
define rot_free
	@for t in aarch64-none-elf i386-none-elf; do fl=; [ $$t = aarch64-none-elf ] && fl=-mgeneral-regs-only; \
	  $(ROT_FREE) --target=$$t $$fl $(3) -c $(2) -o $(ROT_TMP)/$(1)_free.o || { echo "[FAIL] $(1) freestanding $$t"; exit 1; }; \
	  if nm -u $(ROT_TMP)/$(1)_free.o | grep -vE ' ($(4))$$' | grep -q .; then nm -u $(ROT_TMP)/$(1)_free.o; echo "[FAIL] $(1) freestanding: libc or helper symbol ($$t)"; exit 1; fi; \
	done; echo "[PASS] $(1) freestanding (aarch64 -mgeneral-regs-only and i386: no libc, no 64-bit division helpers)"
endef

.PHONY: test-rot
test-rot:
	@mkdir -p $(ROT_TMP)
	@$(ROT_CC) $(ROT_INC) src/qr/test_qr.c src/qr/qr.c -o $(ROT_TMP)/test_qr || { echo "[FAIL] test_qr (build)"; exit 1; }
	@$(call rot_run,test_qr)
	@$(ROT_CC) $(ROT_INC) src/bootattest/test_ba.c $(ROT_BA_DEPS) -o $(ROT_TMP)/test_ba 2>$(ROT_TMP)/build_ba.log || { cat $(ROT_TMP)/build_ba.log; echo "[FAIL] test_ba (build)"; exit 1; }
	@$(call rot_run,test_ba)
	@$(ROT_CC) $(ROT_INC) src/ztp/test_ztp.c $(ROT_ZTP_DEPS) -o $(ROT_TMP)/test_ztp 2>$(ROT_TMP)/build_ztp.log || { cat $(ROT_TMP)/build_ztp.log; echo "[FAIL] test_ztp (build)"; exit 1; }
	@$(call rot_run,test_ztp)
	@$(ROT_CC) -DZTP_PHYSICAL_RESET=1 $(ROT_INC) src/ztp/test_ztp.c $(ROT_ZTP_DEPS) -o $(ROT_TMP)/test_ztp_presence 2>$(ROT_TMP)/build_ztp2.log || { cat $(ROT_TMP)/build_ztp2.log; echo "[FAIL] test_ztp_presence (build)"; exit 1; }
	@$(call rot_run,test_ztp_presence)
	$(call rot_free,qr,src/qr/qr.c,,__nothing__)
	$(call rot_free,bootattest,src/bootattest/ba.c,,sha256|sha256_init|sha256_update|sha256_final|sha3_256|pqm_mldsa87_keygen|pqm_mldsa87_sign|pqm_mldsa87_verify)
	$(call rot_free,ztp,src/ztp/ztp.c,,ba_base32|ba_key_hash|ba_soft_keygen|crypto_hmac_sha256|pqm_mldsa87_verify|sha3_256)
	$(call rot_free,ztp_presence,src/ztp/ztp.c,-DZTP_PHYSICAL_RESET=1,ba_base32|ba_key_hash|ba_soft_keygen|crypto_hmac_sha256|pqm_mldsa87_verify|sha3_256)
