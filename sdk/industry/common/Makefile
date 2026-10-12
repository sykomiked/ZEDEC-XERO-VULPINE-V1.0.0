# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/industry/common — the shared layer every industry kit links (libzind):
# status codes, exact quantities, seen-sets, ML-DSA-87 signed SHA3-256
# chains and the quarantine sink. Targets and options: ../mk/kit.mk.

KIT      := common
LIBNAME  := zind
SRC      := src/zind_util.c src/zind_qty.c src/zind_chain.c src/zind_qsink.c
HDRS     := include/zind_common.h
TESTS    := test_common
FUZZ     := fuzz_common
EXAMPLES :=

include ../mk/kit.mk
