# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# edge.mk — roadmap R4.4/R4.5, distribution and edge inference:
#   src/edgeclus     the edge cluster: signed node adverts (ML-DSA-87 via
#                    Vinea identities), the Fibonacci token budget across
#                    nodes (swarm_budget) with a no-monopoly cap, the
#                    deterministic scheduler and failover
#   packages/edge    the headless zxvd package for Raspberry Pi 5, Jetson
#                    and x86_64 mini PC (build script, launcher, systemd
#                    unit, profiles)
# See docs/retail/EDGE_NODES.md.
#
#   cd kernel && make -f Makefile -f mk/edge.mk test-edge
#   cd kernel && make -f Makefile -f mk/edge.mk test-edge-smoke   (slow: builds
#       the packages, cross-compiling for aarch64, and runs zxvd under qemu)
#
# test-edge: the cluster test under ASan/UBSan, the same sources warning-free
# under clang, the cluster library compiled freestanding for aarch64 (general
# registers only) and i386 with its undefined symbols checked (only Vinea,
# swarm_budget and its own entry points: no libc, no 64-bit division
# helpers), then the package checks that need no build.

ZXV_TMP ?= /tmp
EDGE_TMP := $(ZXV_TMP)/zxv_edge
EDGE_CC := gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wshadow -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST
EDGE_INC := -Iinclude -Isrc/modbind
EDGE_SRCS := src/edgeclus/ec_advert.c src/edgeclus/ec_cluster.c
EDGE_DEPS := src/vinea/vna_common.c src/vinea/vna_id.c src/pqsec/pq_mldsa87.c src/pqsec/mldsa/fips202.c \
	src/mlkem/keccak.c src/swarm/swarm_budget.c

.PHONY: test-edge test-edge-smoke
test-edge:
	@mkdir -p $(EDGE_TMP)
	@$(EDGE_CC) $(EDGE_INC) src/edgeclus/test_edgeclus.c $(EDGE_SRCS) $(EDGE_DEPS) -o $(EDGE_TMP)/test_edgeclus 2>$(EDGE_TMP)/test_edgeclus.build || { cat $(EDGE_TMP)/test_edgeclus.build; echo "[FAIL] test_edgeclus build"; exit 1; }
	@$(EDGE_TMP)/test_edgeclus > $(EDGE_TMP)/test_edgeclus.log 2>&1 && echo "[PASS] test_edgeclus ($$(tail -1 $(EDGE_TMP)/test_edgeclus.log | sed -E 's/^([0-9]+) checks passed.*/\1 checks/'))" || { grep -v '^\[PASS\]' $(EDGE_TMP)/test_edgeclus.log; echo "[FAIL] test_edgeclus"; exit 1; }
	@if command -v clang >/dev/null; then clang -std=c11 -O2 -Wall -Werror -Wextra -Wshadow -DTEST_HOST $(EDGE_INC) -fsyntax-only src/edgeclus/test_edgeclus.c $(EDGE_SRCS) && echo "[PASS] edgeclus warning-free under clang (1 check)" || { echo "[FAIL] edgeclus under clang"; exit 1; }; else echo "[SKIP] edgeclus under clang (no clang)"; fi
	@for t in aarch64-none-elf i386-none-elf; do x=; [ $$t = aarch64-none-elf ] && x=-mgeneral-regs-only; for f in $(EDGE_SRCS); do clang --target=$$t $$x -ffreestanding -std=c11 -O2 -Wall -Werror -Wextra -Wshadow -c $$f -o $(EDGE_TMP)/edge_free.o || { echo "[FAIL] edgeclus freestanding $$t $$f"; exit 1; }; if nm -u $(EDGE_TMP)/edge_free.o | grep -vE ' (ec_[a-z0-9_]+|vna_[a-z0-9_]+|swarm_(budget_[a-z_]+|level_capacity|muldiv|split_lr))$$' | grep -q .; then nm -u $(EDGE_TMP)/edge_free.o; echo "[FAIL] edgeclus freestanding: libc, allocation or 64-bit division helper in $$f ($$t)"; exit 1; fi; done; done; echo "[PASS] edgeclus freestanding (aarch64 general-regs-only and i386: no libc, no 64-bit division helpers; 4 checks)"
	@sh ../packages/edge/test_edge_pkg.sh $(EDGE_TMP)/pkg > $(EDGE_TMP)/test_edge_pkg.log 2>&1 && echo "[PASS] test_edge_pkg ($$(tail -1 $(EDGE_TMP)/test_edge_pkg.log | sed -E 's/^([0-9]+) checks passed.*/\1 checks/'))" || { cat $(EDGE_TMP)/test_edge_pkg.log; echo "[FAIL] test_edge_pkg"; exit 1; }

# Builds every profile's tarball with --smoke. A profile whose compiler is
# missing is skipped with a note (Debian/Ubuntu: gcc-aarch64-linux-gnu,
# qemu-user); a build or smoke failure fails the target.
test-edge-smoke:
	@mkdir -p $(EDGE_TMP)
	@for p in rpi5 jetson minipc; do a=$$(sed -n 's/^ARCH=//p' ../packages/edge/profiles/$$p.conf); if [ "$$a" != "$$(uname -m)" ] && ! command -v $$a-linux-gnu-gcc >/dev/null; then echo "[SKIP] edge package $$p (no $$a-linux-gnu-gcc)"; continue; fi; sh ../packages/edge/build_edge.sh $$p $(EDGE_TMP)/out --smoke > $(EDGE_TMP)/smoke_$$p.log 2>&1 && echo "[PASS] edge package $$p ($$(grep -c '^\[PASS\]' $(EDGE_TMP)/smoke_$$p.log) checks; $$(grep -c '^\[SKIP\]' $(EDGE_TMP)/smoke_$$p.log) skipped)" || { cat $(EDGE_TMP)/smoke_$$p.log; echo "[FAIL] edge package $$p"; exit 1; }; done
