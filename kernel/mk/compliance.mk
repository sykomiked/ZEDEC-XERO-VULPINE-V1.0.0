# compliance.mk — retail compliance plug-ins (roadmap R3.4-R3.6), all OFF by
# default: capital-control profiles (src/capctl), tax-calculation hooks
# (src/taxhook, SAMPLE tables only, not tax advice) and the KYC wrapper
# (src/kycwrap: ZK slot not available, minimal-disclosure backend is NOT
# zero-knowledge). See docs/retail/COMPLIANCE_PLUGINS.md.
#
# Run from kernel/:  make -f Makefile -f mk/compliance.mk test-compliance

ZXV_TMP ?= /tmp
CMP_DIR := $(ZXV_TMP)/zxv_compliance
CMP_CFLAGS := -std=c11 -O1 -g -Wall -Werror -Wextra -Wshadow -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST
CMP_INC := -Iinclude -Isrc/modbind -Isrc/pqsec -Isrc/tensor -Isrc/cbank
CMP_SIG := src/capctl/cp_common.c src/capctl/cpk.c src/robin_debanks/sha256.c src/pqsec/pq_mldsa87.c src/pqsec/mldsa/fips202.c
CMP_CBANK := src/cbank/cb_ccy.c src/cbank/cb_ccy_tbl.c src/cbank/cb_util.c src/tensor/zt.c -lm
CMP_FREE := src/capctl/capctl.c src/capctl/cp_common.c src/capctl/cpk.c src/taxhook/taxhook.c src/taxhook/taxhook_sample.c src/kycwrap/kycwrap.c
# Symbols the freestanding objects may leave undefined: each other, SHA-256
# and ML-DSA-87. Anything else (libc, malloc, __udivdi3, ...) fails.
CMP_ALLOW := ' (cp_[a-z0-9_]+|capctl_[a-z_]+|taxhook_[a-z_]+|kyc_[a-z_]+|sha256|sha256_(init|update|final)|pqm_mldsa87_(keygen|sign|verify))$$'

.PHONY: test-compliance
test-compliance:
	@mkdir -p $(CMP_DIR)
	@gcc $(CMP_CFLAGS) $(CMP_INC) src/capctl/test_capctl.c src/capctl/capctl.c $(CMP_SIG) $(CMP_CBANK) -o $(CMP_DIR)/test_capctl 2>$(CMP_DIR)/test_capctl.build && $(CMP_DIR)/test_capctl > $(CMP_DIR)/test_capctl.log 2>&1 && echo "[PASS] test_capctl ($$(tail -1 $(CMP_DIR)/test_capctl.log))" || { cat $(CMP_DIR)/test_capctl.build $(CMP_DIR)/test_capctl.log 2>/dev/null; echo "[FAIL] test_capctl"; exit 1; }
	@gcc $(CMP_CFLAGS) $(CMP_INC) src/taxhook/test_taxhook.c src/taxhook/taxhook.c src/taxhook/taxhook_sample.c $(CMP_SIG) $(CMP_CBANK) -o $(CMP_DIR)/test_taxhook 2>$(CMP_DIR)/test_taxhook.build && $(CMP_DIR)/test_taxhook > $(CMP_DIR)/test_taxhook.log 2>&1 && echo "[PASS] test_taxhook ($$(tail -1 $(CMP_DIR)/test_taxhook.log))" || { cat $(CMP_DIR)/test_taxhook.build $(CMP_DIR)/test_taxhook.log 2>/dev/null; echo "[FAIL] test_taxhook"; exit 1; }
	@gcc $(CMP_CFLAGS) $(CMP_INC) src/kycwrap/test_kycwrap.c src/kycwrap/kycwrap.c $(CMP_SIG) -o $(CMP_DIR)/test_kycwrap 2>$(CMP_DIR)/test_kycwrap.build && $(CMP_DIR)/test_kycwrap > $(CMP_DIR)/test_kycwrap.log 2>&1 && echo "[PASS] test_kycwrap ($$(tail -1 $(CMP_DIR)/test_kycwrap.log))" || { cat $(CMP_DIR)/test_kycwrap.build $(CMP_DIR)/test_kycwrap.log 2>/dev/null; echo "[FAIL] test_kycwrap"; exit 1; }
	@for t in aarch64-none-elf i386-none-elf; do for f in $(CMP_FREE); do o=$(CMP_DIR)/$$(basename $$f .c).$$t.o; clang --target=$$t -ffreestanding -std=c11 -O2 -Wall -Werror -Wextra -Wshadow -c $$f -o $$o || { echo "[FAIL] compliance freestanding build $$t $$f"; exit 1; }; if nm -u $$o | grep -vE $(CMP_ALLOW) | grep -q .; then nm -u $$o; echo "[FAIL] compliance freestanding: libc or 64-bit division helper in $$f ($$t)"; exit 1; fi; done; done; echo "[PASS] compliance freestanding (aarch64 and i386: no libc, no malloc, no 64-bit division helpers)"
	@if command -v clang >/dev/null; then for f in src/capctl/test_capctl.c src/taxhook/test_taxhook.c src/kycwrap/test_kycwrap.c $(CMP_FREE); do clang -fsyntax-only -std=c11 -Wall -Werror -Wextra -Wshadow -DTEST_HOST $(CMP_INC) $$f || { echo "[FAIL] compliance clang warnings in $$f"; exit 1; }; done; echo "[PASS] compliance sources warning-free under clang"; else echo "[SKIP] compliance clang warning check (no clang)"; fi
