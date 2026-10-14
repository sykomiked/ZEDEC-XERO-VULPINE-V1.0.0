# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# tools/zxsh — the paraconsistent shell.
#
#   make          build/zxsh
#   make test     core unit tests (ASan/UBSan) + host end-to-end tests
#   make clean
ifeq ($(origin CC),default)
  CC := gcc
endif
K      := ../../kernel
OUT    ?= build
INC    := -I$(K)/include -I$(K)/src/lpres -I$(K)/src/event_space -I$(K)/src/modbind -I$(K)/src/surplus
CFLAGS ?= -O2
WARN   := -std=c11 -Wall -Wextra -Werror
LOGIC  := $(K)/src/lpres/lpres_hex.c $(K)/src/lpres/lpres.c $(K)/src/robin_debanks/sha256.c
CORE   := zxsh_core.c $(LOGIC)

all: $(OUT)/zxsh

$(OUT)/zxsh: zxsh_host.c $(CORE) zxsh_core.h
	@mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) $(INC) zxsh_host.c $(CORE) -o $@

$(OUT)/test_zxsh: test_zxsh.c $(CORE) zxsh_core.h
	@mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(INC) test_zxsh.c $(CORE) -o $@

$(OUT)/zxsh_asan: zxsh_host.c $(CORE) zxsh_core.h
	@mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $(INC) zxsh_host.c $(CORE) -o $@

test: $(OUT)/test_zxsh $(OUT)/zxsh_asan
	$(OUT)/test_zxsh
	sh ./test_zxsh.sh $(OUT)/zxsh_asan

clean:
	rm -rf $(OUT)

.PHONY: all test clean
