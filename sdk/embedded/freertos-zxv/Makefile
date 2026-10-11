# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# freertos-zxv — the ZXV owner task on FreeRTOS, built and run on the
# FreeRTOS POSIX simulator (Linux host). FreeRTOS-Kernel is NOT vendored:
# `make fetch` clones the pinned tag into build/ and checks the commit.
#
#   make fetch    clone FreeRTOS-Kernel $(FREERTOS_TAG) (needs network once)
#   make          build build/zxv_freertos_sim
#   make test     run the scenario, the --no-entropy and --violate runs
#   make clean
#
# Use an existing checkout instead: make FREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel

ifeq ($(origin CC),default)
  CC := gcc
endif
AR ?= ar
FREERTOS_TAG    := V11.2.0
FREERTOS_COMMIT := 0adc196d4bd52a2d91102b525b0aafc1e14a2386
FREERTOS_URL    := https://github.com/FreeRTOS/FreeRTOS-Kernel
FREERTOS_KERNEL_PATH ?= build/FreeRTOS-Kernel
FR := $(abspath $(FREERTOS_KERNEL_PATH))
OUT := build
ROOT := ..

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

FR_INC := -I$(abspath .) -I$(FR)/include -I$(FR)/portable/ThirdParty/GCC/Posix \
          -I$(FR)/portable/ThirdParty/GCC/Posix/utils
FR_SRC := $(addprefix $(FR)/,tasks.c queue.c list.c timers.c event_groups.c stream_buffer.c \
          portable/MemMang/heap_3.c portable/ThirdParty/GCC/Posix/port.c \
          portable/ThirdParty/GCC/Posix/utils/wait_for_event.c)
FR_OBJ := $(patsubst $(FR)/%.c,$(OUT)/freertos/%.o,$(FR_SRC))

APP_CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra -Werror $(SAN) $(FR_INC) -I$(ROOT)/port/include \
              -I$(ROOT)/zxe/include -I$(ROOT)/../libzxv/include

.PHONY: all fetch test clean

all: $(OUT)/zxv_freertos_sim

fetch: $(FR)/tasks.c

$(FR)/tasks.c:
	@if [ "$(FREERTOS_KERNEL_PATH)" != "build/FreeRTOS-Kernel" ]; then \
	  echo "FREERTOS_KERNEL_PATH=$(FREERTOS_KERNEL_PATH) has no tasks.c"; exit 1; fi
	@mkdir -p $(OUT)
	git -c advice.detachedHead=false clone --quiet --depth 1 --branch $(FREERTOS_TAG) $(FREERTOS_URL) $(FR)
	@test "$$(git -C $(FR) rev-parse HEAD)" = "$(FREERTOS_COMMIT)" || \
	  { echo "FreeRTOS-Kernel $(FREERTOS_TAG) is not commit $(FREERTOS_COMMIT)"; exit 1; }

# The clone brings every other kernel source with tasks.c. Saying so gives
# make a rule for them in a fresh checkout, where they do not exist yet.
$(filter-out $(FR)/tasks.c,$(FR_SRC)): $(FR)/tasks.c ;

# The FreeRTOS kernel and its POSIX port: third-party, no -Werror.
$(OUT)/freertos/%.o: $(FR)/%.c FreeRTOSConfig.h | fetch
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -g $(SAN) $(FR_INC) -w -c $< -o $@

# libzxv subset + port (zxp_freertos.c) + zxe, single-owner mode (no TLS).
$(OUT)/zxv/libzxv_embedded.a $(OUT)/zxv/libzxp.a $(OUT)/zxv/libzxe.a: FORCE | fetch
	+$(MAKE) --no-print-directory -C $(ROOT) -f lib.mk CC=$(CC) AR=$(AR) \
	  OUT=$(abspath $(OUT))/zxv PORT=freertos NO_TLS=1 SANITIZE=$(SANITIZE) OPT=-O1 \
	  TARGET_CFLAGS="$(FR_INC)"

$(OUT)/zxv_freertos_sim: main_posix.c zxv_task.c zxv_task.h $(FR_OBJ) $(OUT)/zxv/libzxv_embedded.a \
                         $(OUT)/zxv/libzxp.a $(OUT)/zxv/libzxe.a
	$(CC) $(APP_CFLAGS) main_posix.c zxv_task.c $(FR_OBJ) $(OUT)/zxv/libzxe.a \
	  $(OUT)/zxv/libzxv_embedded.a $(OUT)/zxv/libzxp.a -lpthread $(SAN) -o $@

test: $(OUT)/zxv_freertos_sim
	$(OUT)/zxv_freertos_sim
	$(OUT)/zxv_freertos_sim --no-entropy
	@if $(OUT)/zxv_freertos_sim --violate; then echo "--violate was not stopped"; exit 1; \
	 else st=$$?; test $$st -eq 70 || { echo "--violate exited $$st, want 70"; exit 1; }; \
	 echo "--violate: stopped by the single-owner check (exit 70)"; fi

clean:
	rm -rf $(OUT)/freertos $(OUT)/zxv $(OUT)/zxv_freertos_sim

FORCE:
