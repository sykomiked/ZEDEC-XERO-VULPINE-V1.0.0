# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/industry/sutra-clear — multilateral clearing of ISO 20022 and Sutra
# payment instructions (Sutra-Clear / PAPSS-Core). Targets and options:
# ../mk/kit.mk.

KIT      := sutra-clear
LIBNAME  := zsclear
SRC      := src/zsc_parse.c src/zsc_net.c src/zsc_house.c
HDRS     := include/zsclear.h src/zsc_internal.h
TESTS    := test_sclear
FUZZ     := fuzz_sclear
EXAMPLES := clearing_demo
FUZZ_SMOKE ?= 300

include ../mk/kit.mk
