# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/kits/tri-isolate — run a Z-Wasm plugin in a child process.
#
#   make                 build/libtriisolate.a and build/ti-worker
#   make test            frame tests, process tests (real worker and hostile
#                        test workers), a fuzz smoke run and the no-float check
#   make demo            the isolate demo on Z-Wasm's example plugins
#   make fuzz            the deterministic frame fuzzer (FUZZ_ITERS=...)
#   make fuzz-libfuzzer  the libFuzzer target, if clang ships libFuzzer
#   make check-nofloat   R11: no float type in the kit's sources
#   make clean
#
# Options:
#   SANITIZE=1           -fsanitize=address,undefined for the kit, Z-Wasm
#                        (rebuilt under $(OUT)), the worker and the tests
#   TI_SKIP_EXAMPLES=1   make test without zig (the plugin tests skip)
#
# Z-Wasm is built into $(OUT)/zwasm; its own build/ directory is not touched.
# POSIX only (Linux, macOS); Windows is not supported.

ifeq ($(origin CC),default)
  CC := gcc
endif
AR      ?= ar
PYTHON  ?= python3
OUT     ?= build
ZXV     := ../../libzxv
ZW      := ../z-wasm

ifeq ($(shell uname -s),Darwin)
  ZXV_LIB := $(ZXV)/build/libzxv.dylib
else
  ZXV_LIB := $(ZXV)/build/libzxv.so
endif
ZXV_ABS := $(abspath $(ZXV)/build)
OUT_ABS := $(abspath $(OUT))
ZW_OUT  := $(OUT_ABS)/zwasm
ZW_LIB  := $(ZW_OUT)/libzwasm.a
ZW_EX   := $(ZW_OUT)/examples

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
  SANDEF := -DTI_TEST_SANITIZE=1
endif
NOFLOAT := $(shell $(CC) -mgeneral-regs-only -x c -c /dev/null -o /dev/null 2>/dev/null && echo -mgeneral-regs-only)

WARN    := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
INC     := -Iinclude -Isrc -I$(ZXV)/include -I$(ZW)/include
CFLAGS_ := -std=c11 -O2 -g -fPIC $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) $(INC)

SRC  := src/ti_frame.c src/ti_host.c src/ti_seccomp.c
WSRC := src/ti_frame.c src/ti_wio.c src/ti_seccomp.c
OBJ  := $(patsubst src/%.c,$(OUT)/obj/%.o,$(SRC))
WOBJ := $(patsubst src/%.c,$(OUT)/obj/%.o,$(WSRC))
LIB  := $(OUT)/libtriisolate.a
HDRS := include/triisolate.h src/ti_frame.h src/ti_seccomp.h src/ti_wio.h $(ZXV)/include/zxv_api.h \
        $(ZW)/include/zwasm.h

LINK  := -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -lpthread -lm $(SAN)
TESTCC = $(CC) -std=c11 -O1 -g $(WARN) $(SAN) $(SANDEF) $(INC) -I$(ZW)/test -Itest

WORKER  := $(OUT)/ti-worker
HOSTILE := $(OUT)/ti-hostile
HW_MODES := exit-early bad-version no-seccomp crash-load hang-load hang-call garbage oversize \
            zero-length lie-fuel bad-prop unnormal long-label trailing wrong-type bad-status \
            too-many syscall half-frame slow-drip ignore-quit hygiene exit-after-hello \
            close-and-hang props
HW_LINKS := $(addprefix $(OUT)/hw/,$(HW_MODES))

ifeq ($(TI_SKIP_EXAMPLES),)
TEST_EXAMPLES := $(ZW_EX)/rational_plugin.wasm $(ZW_EX)/float_plugin.wasm $(ZW_EX)/spin_plugin.wasm
endif

FUZZ_ITERS ?= 20000

.PHONY: all test demo fuzz fuzz-libfuzzer check-nofloat clean hostile

all: $(LIB) $(WORKER)

$(ZXV_LIB):
	$(MAKE) -C $(ZXV) all

$(ZW_LIB): $(wildcard $(ZW)/src/*.c $(ZW)/src/*.h $(ZW)/include/*.h)
	$(MAKE) -C $(ZW) CC=$(CC) SANITIZE=$(SANITIZE) OUT=$(ZW_OUT) $(ZW_LIB)

$(ZW_EX)/%.wasm: $(ZW)/examples/plugins/%.c
	$(MAKE) -C $(ZW) PYTHON=$(PYTHON) OUT=$(ZW_OUT) $@

$(OUT)/obj/%.o: src/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_) -c $< -o $@

$(LIB): $(OBJ)
	@rm -f $@
	$(AR) rcs $@ $^

# The worker links the whole of Z-Wasm (wasm3 included); the host library
# only needs Z-Wasm's validator.
$(WORKER): worker/ti_worker.c $(WOBJ) $(ZW_LIB) $(ZXV_LIB) $(HDRS)
	$(CC) -std=c11 -O2 -g $(WARN) -fstack-protector-strong $(SAN) $(NOFLOAT) $(INC) \
	  worker/ti_worker.c $(WOBJ) $(ZW_LIB) $(LINK) -o $@

$(HOSTILE): test/hostile_worker.c $(WOBJ) $(ZXV_LIB) $(HDRS)
	$(TESTCC) test/hostile_worker.c $(WOBJ) $(LINK) -o $@

# One name per misbehaviour: the hostile worker reads its mode from argv[0].
$(OUT)/hw/%: $(HOSTILE)
	@mkdir -p $(dir $@)
	@ln -sf ../ti-hostile $@

hostile: $(HOSTILE) $(HW_LINKS)

$(OUT)/test_frame: test/test_frame.c test/ti_test.h $(LIB) $(ZW_LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(ZW_LIB) $(LINK) -o $@

$(OUT)/test_isolate: test/test_isolate.c test/ti_test.h $(LIB) $(ZW_LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(ZW_LIB) $(LINK) -o $@

$(OUT)/fuzz_frame: test/fuzz_frame.c test/ti_test.h $(LIB) $(ZW_LIB) $(ZXV_LIB)
	$(TESTCC) -DTI_FUZZ_MAIN $< $(LIB) $(ZW_LIB) $(LINK) -o $@

$(OUT)/isolate_demo: examples/isolate_demo.c $(LIB) $(ZW_LIB) $(ZXV_LIB)
	$(TESTCC) $< $(LIB) $(ZW_LIB) $(LINK) -o $@

test: check-nofloat $(OUT)/test_frame $(OUT)/test_isolate $(OUT)/fuzz_frame $(WORKER) hostile \
      $(TEST_EXAMPLES)
	$(OUT)/test_frame
	$(OUT)/test_isolate $(WORKER) $(OUT)/hw $(TEST_EXAMPLES)
	$(OUT)/fuzz_frame 5000

demo: $(OUT)/isolate_demo $(WORKER) $(ZW_EX)/rational_plugin.wasm $(ZW_EX)/float_plugin.wasm \
      $(ZW_EX)/spin_plugin.wasm
	$(OUT)/isolate_demo $(WORKER) $(ZW_EX)/rational_plugin.wasm $(ZW_EX)/float_plugin.wasm \
	  $(ZW_EX)/spin_plugin.wasm

fuzz: $(OUT)/fuzz_frame
	$(OUT)/fuzz_frame $(FUZZ_ITERS)

fuzz-libfuzzer: $(ZW_LIB)
	@if clang -fsanitize=fuzzer -x c /dev/null -o /dev/null 2>/dev/null; then \
	  mkdir -p $(OUT)/corpus && \
	  clang -std=gnu11 -O1 -g -fsanitize=fuzzer,address,undefined $(INC) -Itest \
	    test/fuzz_frame.c src/ti_frame.c \
	    -L$(ZXV_ABS) -lzxv -Wl,-rpath,$(ZXV_ABS) -o $(OUT)/fuzz_libfuzzer && \
	  $(MAKE) --no-print-directory $(OUT)/fuzz_frame && \
	  TI_FUZZ_CORPUS=$(OUT)/corpus $(OUT)/fuzz_frame 0 && \
	  $(OUT)/fuzz_libfuzzer -max_total_time=60 $(OUT)/corpus; \
	else echo "clang has no libFuzzer runtime here; use 'make fuzz'"; fi

check-nofloat:
	$(PYTHON) $(ZW)/tools/check_nofloat.py $(SRC) src/ti_wio.c src/*.h include/triisolate.h \
	  worker/ti_worker.c test/*.c test/*.h examples/*.c
	@echo "compiler guard for the kit sources: $(or $(NOFLOAT),none on this compiler)"

clean:
	rm -rf $(OUT)
