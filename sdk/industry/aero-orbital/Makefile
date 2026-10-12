# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/industry/aero-orbital — ground-side SmallSat aid: integer WGS-84
# geodesy, telemetry in UBH-168 frames, SEU flagging. Targets and options:
# ../mk/kit.mk. `make vectors` regenerates test/geo_vectors.h from an
# 80-digit decimal reference (tools/gen_geo.py).

KIT      := aero-orbital
LIBNAME  := zaero
SRC      := src/zaero_geo.c src/zaero_tm.c src/zaero_rx.c
HDRS     := include/zaero.h test/geo_vectors.h
TESTS    := test_aero
FUZZ     := fuzz_aero
EXAMPLES := groundstation_demo
FUZZ_SMOKE ?= 1500

include ../mk/kit.mk

.PHONY: vectors
vectors:
	$(PYTHON) tools/gen_geo.py > test/geo_vectors.h
