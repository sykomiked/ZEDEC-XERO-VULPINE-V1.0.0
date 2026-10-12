# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/industry — builds and tests every industry kit in turn. Each kit has
# its own Makefile on mk/kit.mk; this one only loops over them.
#
#   make -C sdk/industry test                 unit tests, fuzz smoke runs,
#                                             examples, R11 no-float check
#   make -C sdk/industry fuzz FUZZ_ITERS=n    every fuzz target, n ops each
#   make -C sdk/industry test SANITIZE=1 OUT=build/san CC=clang
#
# libzxv must be built first: make -C sdk/libzxv all. A new kit is added to
# KITS and needs nothing else here or in CI.

KITS := common logix-clear tri-deliver v-shelf tri-trace z-genome vinea-triage mesh-command aero-orbital tri-modem v-band sutra-clear tri-witness

# forwarded to every kit when given
FWD := $(if $(filter command line environment,$(origin CC)),CC=$(CC)) \
       $(if $(SANITIZE),SANITIZE=$(SANITIZE)) $(if $(OUT),OUT=$(OUT)) \
       $(if $(FUZZ_ITERS),FUZZ_ITERS=$(FUZZ_ITERS)) $(if $(PYTHON),PYTHON=$(PYTHON))

.PHONY: all test fuzz demo check-nofloat clean list

all test fuzz demo check-nofloat clean:
	@set -e; for k in $(KITS); do \
	  echo "== $$k: $@"; \
	  $(MAKE) --no-print-directory -C $$k $(FWD) $@; \
	done

list:
	@echo $(KITS)
