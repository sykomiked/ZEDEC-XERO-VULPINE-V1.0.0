# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/pillars/fortran-trebago — the Fortran module zxv and Trebago over libzxv.
#
#   make                 build/libztrebago.a, build/ztrebago (the command-line
#                        tool) and, with gfortran, build/libzxvf.a + build/mod/
#   make test            C unit tests, golden programs, a fuzz smoke run, the
#                        no-float and no-REAL checks, and with gfortran the
#                        Fortran conformance tests, the fail-closed test and
#                        the compile-fail tests
#   make test-c          only the C half (no Fortran compiler needed)
#   make test-fortran    only the Fortran half
#   make demo            the Fortran demo (examples/trebago_demo.f90)
#   make fuzz            the deterministic parser/executor fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make check-nofloat   R11: no float type in the C sources
#   make check-noreal    R11: no REAL in the Fortran sources outside the one
#                        marked boundary (rational_from_ieee754)
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined (C, and Fortran with gfortran)
#   FORTRAN=0|1          skip or require the Fortran half (default: 1 if $(FC) exists)
#   FC=gfortran          the Fortran compiler (gfortran 10 or later)
#   OUT=dir              build directory (default build/, git-ignored)
#
# libzxv is built with `make -C ../../libzxv all`.

ifeq ($(origin CC),default)
  CC := gcc
endif
ifeq ($(origin FC),default)
  FC := gfortran
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
UNAME_S := $(shell uname -s)
FORTRAN ?= $(if $(shell command -v $(FC) 2>/dev/null),1,0)

ifeq ($(UNAME_S),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZXV_ABS := $(abspath $(ZXV)/build)

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

# R11 compiler guard where the compiler enforces it (x86-64 and arm64 gcc/clang).
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) \
           -Iinclude -Isrc -I$(ZXV)/include

SRC  := src/tb_util.c src/tb_parse.c src/tb_print.c src/tb_exec.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
LIB  := $(OUT)/libztrebago.a
HDRS := include/ztrebago.h src/tb_internal.h $(ZXV)/include/zxv_api.h

LINK  := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) -Iinclude -Isrc -I$(ZXV)/include -Itest

# ---- Fortran ----
FSTD    := -std=f2018
FWARN   := -Wall -Wextra -Wimplicit-interface -Wimplicit-procedure -Werror
FFLAGS_ := $(FSTD) -O1 -g -fcheck=all -ffree-line-length-132 $(FWARN) $(SAN) -J$(OUT)/mod -I$(OUT)/mod
# Programs that use the modules: gfortran's -Wextra warns when an impure
# function sits in an .and./.or. it may short-circuit. Arithmetic, parsing
# and handle methods are impure on purpose (they can stop the program), so
# the tests and the demo turn that one warning off. See the README.
FPROG   := $(FFLAGS_) -Wno-function-elimination
FSRC    := fortran/zxv.f90 fortran/trebago.f90
FOBJ    := $(OUT)/fobj/zxv.o $(OUT)/fobj/trebago.o
FLIB    := $(OUT)/libzxvf.a
FLINK   := $(FLIB) $(LIB) $(LINK)

CTESTS := test_parse test_exec
FTESTS := test_zxv test_trebago
FUZZ_ITERS ?= 20000
LIBFUZZER_SECONDS ?= 60

.PHONY: all test test-c test-fortran demo fuzz fuzz-libfuzzer check-nofloat check-noreal \
        compile-fail fatal clean fortran-required

ifeq ($(FORTRAN),1)
all: $(LIB) $(OUT)/ztrebago $(FLIB)
test: test-c test-fortran
	@echo "fortran-trebago: all tests passed (C and Fortran)"
else
all: $(LIB) $(OUT)/ztrebago
test: test-c
	@echo "fortran-trebago: C tests passed; Fortran SKIPPED (no $(FC); FORTRAN=1 to require it)"
endif

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/ztrebago: tools/ztrebago_tool.c $(LIB) $(ZXV_LIB) $(HDRS)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/test_%: test/test_%.c test/tb_test.h $(LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(LINK) -o $@

$(OUT)/fuzz_parse: test/fuzz_parse.c $(LIB) $(ZXV_LIB)
	$(TESTCC) -DTB_FUZZ_MAIN $< $(LIB) $(LINK) -o $@

test-c: check-nofloat $(addprefix $(OUT)/,$(CTESTS)) $(OUT)/fuzz_parse $(OUT)/ztrebago
	@for t in $(CTESTS); do $(OUT)/$$t || exit 1; done
	$(OUT)/ztrebago golden test/golden/*.tb examples/*.tb
	$(OUT)/fuzz_parse 3000 examples/*.tb test/golden/*.tb
	$(OUT)/ztrebago check examples/dot.tb examples/matvec.tb
	$(OUT)/ztrebago same examples/dot.tb test/golden/dot_spelled.tb
	@if $(OUT)/ztrebago check examples/broken.tb; then echo "broken.tb parsed"; exit 1; fi
	@echo "fortran-trebago: C tests passed"

# ---- Fortran half ----
fortran-required:
	@command -v $(FC) >/dev/null 2>&1 || { echo "$(FC) not found: install gfortran or use FORTRAN=0"; exit 1; }

$(OUT)/fobj/zxv.o: fortran/zxv.f90 | fortran-required
	@mkdir -p $(OUT)/fobj $(OUT)/mod
	$(FC) $(FFLAGS_) -c $< -o $@

$(OUT)/fobj/trebago.o: fortran/trebago.f90 $(OUT)/fobj/zxv.o
	$(FC) $(FFLAGS_) -c $< -o $@

$(FLIB): $(FOBJ)
	@rm -f $@
	$(AR) rcs $@ $^

# vectors.json as a Fortran module of named constants (Fortran has no JSON reader)
$(OUT)/gen/conformance_vectors.f90: ../../conformance/vectors.json tools/gen_vectors_f90.py
	@mkdir -p $(OUT)/gen
	$(PYTHON) tools/gen_vectors_f90.py $< $@

$(OUT)/fobj/conformance_vectors.o: $(OUT)/gen/conformance_vectors.f90 $(FLIB)
	$(FC) $(FFLAGS_) -c $< -o $@

$(OUT)/test_zxv: test/test_zxv.f90 $(OUT)/fobj/conformance_vectors.o $(FLIB) $(LIB) $(ZXV_LIB)
	$(FC) $(FPROG) $< $(OUT)/fobj/conformance_vectors.o $(FLINK) -o $@

$(OUT)/test_trebago: test/test_trebago.f90 $(FLIB) $(LIB) $(ZXV_LIB)
	$(FC) $(FPROG) $< $(FLINK) -o $@

$(OUT)/test_fatal: test/test_fatal.f90 $(FLIB) $(LIB) $(ZXV_LIB)
	$(FC) $(FPROG) $< $(FLINK) -o $@

$(OUT)/trebago_demo: examples/trebago_demo.f90 $(FLIB) $(LIB) $(ZXV_LIB)
	$(FC) $(FPROG) $< $(FLINK) -o $@

# A poisoned context must stop the program: ERROR STOP 70, the message on
# stderr, and nothing after the stop runs.
fatal: $(OUT)/test_fatal
	@set +e; $(OUT)/test_fatal > $(OUT)/fatal.out 2> $(OUT)/fatal.err; rc=$$?; set -e; \
	  cat $(OUT)/fatal.out $(OUT)/fatal.err; \
	  test $$rc -eq 70 || { echo "test_fatal: exit $$rc, expected 70"; exit 1; }; \
	  grep -q 'ZXV_E_POISONED' $(OUT)/fatal.err || { echo "test_fatal: no ZXV_E_POISONED on stderr"; exit 1; }; \
	  if grep -q 'AFTER-STOP' $(OUT)/fatal.out; then echo "test_fatal: code ran after the stop"; exit 1; fi; \
	  echo "test_fatal: stopped with 70 on ZXV_E_POISONED"

# Passing a REAL where an exact value is expected must not compile.
compile-fail: $(FLIB)
	FC="$(FC)" FFLAGS="$(FSTD) -I$(OUT)/mod" sh test/compile_fail/run.sh test/compile_fail

test-fortran: check-noreal $(addprefix $(OUT)/,$(FTESTS)) fatal compile-fail
	@for t in $(FTESTS); do $(OUT)/$$t || exit 1; done
	@echo "fortran-trebago: Fortran tests passed"

ifeq ($(FORTRAN),1)
demo: $(OUT)/trebago_demo $(OUT)/ztrebago
	$(OUT)/trebago_demo
else
demo: $(OUT)/ztrebago
	$(OUT)/ztrebago run examples/dot.tb x=1,1/2,1/3 y=1/3,1/2,1
	$(OUT)/ztrebago run examples/matvec.tb 'a(2,2)=1,1/2,1/2,1/3' x=1,1
endif

fuzz: $(OUT)/fuzz_parse
	$(OUT)/fuzz_parse $(FUZZ_ITERS) examples/*.tb test/golden/*.tb

fuzz-libfuzzer:
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && cp examples/*.tb test/golden/*.tb $(OUT)/corpus/ && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude -Isrc \
	    -I$(ZXV)/include test/fuzz_parse.c $(SRC) \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -o $(OUT)/fuzz_libfuzzer && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=$(LIBFUZZER_SECONDS) $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

# R11: no float type anywhere in the C sources (the z-wasm kit's checker;
# comments and strings are excluded).
check-nofloat:
	$(PYTHON) ../../kits/z-wasm/tools/check_nofloat.py $(SRC) src/tb_internal.h include/ztrebago.h \
	  tools/ztrebago_tool.c test/test_parse.c test/test_exec.c test/fuzz_parse.c test/tb_test.h
	@echo "compiler guard for the library sources: $(or $(NOFLOAT),none on this compiler)"

# R11 for Fortran: no REAL, COMPLEX or DOUBLE PRECISION outside the marked
# boundary in fortran/zxv.f90.
check-noreal:
	$(PYTHON) tools/check_noreal.py $(FSRC)

clean:
	rm -rf $(OUT)
