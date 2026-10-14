# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/industry/v-merchant — the merchant side of point-of-sale and agent
# payments: invoices, offline-verified payment proofs, batch settlement and
# exact end-of-day reports. Targets and options: ../mk/kit.mk.

KIT      := v-merchant
LIBNAME  := zvmerch
SRC      := src/zvmerch.c
HDRS     := include/zvmerch.h
TESTS    := test_merchant
FUZZ     := fuzz_merchant
EXAMPLES := till_demo
FUZZ_SMOKE ?= 1500

include ../mk/kit.mk
