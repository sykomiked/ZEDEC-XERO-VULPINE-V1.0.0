# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/industry/sovereign-peg — an operator toolkit over the libzxv V-Bill hub:
# signed exact pegs, backed-only issuance, 64-state capital controls and a
# signed operator log. Targets and options: ../mk/kit.mk.

KIT      := sovereign-peg
LIBNAME  := zspeg
SRC      := src/zspeg.c
HDRS     := include/zspeg.h
TESTS    := test_peg
FUZZ     := fuzz_peg
EXAMPLES := peg_demo
FUZZ_SMOKE ?= 1500

include ../mk/kit.mk
