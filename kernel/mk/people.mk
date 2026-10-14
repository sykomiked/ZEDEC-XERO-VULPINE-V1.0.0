# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# mk/people.mk — zxvd's people features (arch/hosted/zxvd_people.c: social
# feed and groups, call signalling, translation, the device mesh, simple
# mode) behind zxvd_rpc.c, with ASan and UBSan.
#
#   cd kernel && make -f Makefile -f mk/people.mk test-people
#
# libzxv (sdk/libzxv) is built first, as test_zxvd_rpc.sh does. keccak.c is
# compiled at -O2: gcc 11 reports a false maybe-uninitialized in
# keccak_sponge at -O1 (the block is zeroed up to the rate before use).
# zxvd_rpc.c also links what it serves beyond the people methods
# (app_sources.sh rpcdeps: files, spaces, the jurisdiction host) and falls
# back to the module methods, from the archive zxvd_mod_archive.sh builds.
ZXV_TMP ?= /tmp
PEOPLE_T := $(ZXV_TMP)/zxv-people
PEOPLE_CC ?= gcc
PEOPLE_SAN := -fsanitize=address,undefined -fno-sanitize-recover=all
PEOPLE_FLAGS := -std=c11 -O1 -g -Wall -Werror -Wextra -Wshadow $(PEOPLE_SAN)

.PHONY: test-people
test-people:
	@mkdir -p $(PEOPLE_T)
	@$(MAKE) -s -C ../sdk/libzxv CC=$(PEOPLE_CC) > $(PEOPLE_T)/libzxv.log 2>&1 || { tail -20 $(PEOPLE_T)/libzxv.log; echo "[FAIL] test_zxvd_people (libzxv build)"; exit 1; }
	@INC="$$(for d in $$(sh arch/hosted/app_sources.sh incs) $$(sh arch/hosted/app_sources.sh people_incs) $$(sh arch/hosted/app_sources.sh rpcincs); do printf -- '-I%s ' $$d; done) -I../sdk/libzxv/include"; \
	MOD="$$(ZXV_TMP=$(PEOPLE_T) CC=$(PEOPLE_CC) sh arch/hosted/zxvd_mod_archive.sh 2>$(PEOPLE_T)/cc.log)" && \
	$(PEOPLE_CC) -std=c11 -O2 -g -Wall -Werror -Wextra $(PEOPLE_SAN) $$INC -c src/mlkem/keccak.c -o $(PEOPLE_T)/keccak.o >> $(PEOPLE_T)/cc.log 2>&1 && \
	$(PEOPLE_CC) $(PEOPLE_FLAGS) $$INC arch/hosted/test_zxvd_people.c arch/hosted/zxvd_rpc.c \
	    $$(sh arch/hosted/app_sources.sh people) $$(sh arch/hosted/app_sources.sh people_deps | grep -v '^src/mlkem/keccak.c$$') \
	    $$(sh arch/hosted/app_sources.sh rpcdeps) $(PEOPLE_T)/keccak.o "$$MOD" -L../sdk/libzxv/build -lzxv -Wl,-rpath,$(CURDIR)/../sdk/libzxv/build -lm \
	    -o $(PEOPLE_T)/test_zxvd_people >> $(PEOPLE_T)/cc.log 2>&1 || { grep -v "Assembler messages\|incorrect section attributes" $(PEOPLE_T)/cc.log | tail -30; echo "[FAIL] test_zxvd_people (build)"; exit 1; }
	@cd $(PEOPLE_T) && ZXV_TMP=$(PEOPLE_T) ./test_zxvd_people > $(PEOPLE_T)/test_zxvd_people.log 2>&1 && \
	    echo "[PASS] test_zxvd_people ($$(grep -c '^\[PASS\]' $(PEOPLE_T)/test_zxvd_people.log) checks)" || \
	    { grep -v '^\[PASS\]' $(PEOPLE_T)/test_zxvd_people.log | tail -40; echo "[FAIL] test_zxvd_people"; exit 1; }
