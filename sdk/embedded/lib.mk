# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# sdk/embedded/lib.mk — builds one configuration of the embedded libraries
# into $(OUT). Called by ./Makefile (make -f lib.mk ...), once per host or
# cross configuration:
#
#   $(OUT)/libzxv_embedded.a   libzxv subset + kernel modules, through the port
#   $(OUT)/libzxp.a            port layer: zxp_core + zxp_$(PORT)
#   $(OUT)/libzxe.a            zxe_ledger, zxe_clock
#
# Variables: CC AR OUT PORT (posix | baremetal) TARGET_CFLAGS NO_TLS=1
#            ZXE_PQC ZXE_TENSOR ZXE_PROFILE TESTING=1 SANITIZE=1

ZXE_ROOT := .
include sources.mk

CC   ?= cc
AR   ?= ar
OUT  ?= build/host
PORT ?= posix

ifneq ($(SANITIZE),)
  SAN := -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all \
         -fno-omit-frame-pointer
endif
ifeq ($(NO_TLS),1)
  TLSDEF := -DZXP_NO_TLS
endif
ifeq ($(TESTING),1)
  TESTDEF := -DZXP_TESTING -DZXE_TESTING
endif

OPT ?= -Os
COMMON := -std=c11 $(OPT) -g -ffunction-sections -fdata-sections $(TARGET_CFLAGS) $(SAN) $(TLSDEF)
# libzxv and kernel sources are compiled as they are: their warnings belong
# to their own builds (sdk/libzxv uses -Werror there).
ZXV_CFLAGS := $(COMMON) $(ZXE_DEFS) $(ZXE_INC) -w -Werror=implicit-function-declaration \
              -Werror=incompatible-pointer-types -Werror=int-conversion
# Our own sources: strict.
WARN := -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes
OWN_CFLAGS := $(COMMON) $(WARN) $(TESTDEF) $(NOFLOAT_FLAG)

ZXV_PLAIN_CFLAGS := $(COMMON) -DTEST_HOST $(filter-out -I$(ZXE_ROOT)/port/compat,$(ZXE_INC)) -w

ZXV_SRCS := $(ZXE_LIBZXV_SRC) $(sort $(ZXE_KSRC)) $(ZXE_KSRC_PLAIN)
ZXV_OBJS := $(foreach s,$(ZXV_SRCS),$(OUT)/zxv/$(notdir $(s:.c=.o)))
ifneq ($(words $(ZXV_OBJS)),$(words $(sort $(ZXV_OBJS))))
  $(error two libzxv/kernel sources share a file name)
endif
PORT_OBJS := $(OUT)/port/zxp_core.o $(OUT)/port/zxp_$(PORT).o
ZXE_OBJS := $(OUT)/zxe/zxe_ledger.o $(OUT)/zxe/zxe_clock.o

.PHONY: all
all: $(OUT)/libzxv_embedded.a $(OUT)/libzxp.a $(OUT)/libzxe.a

define zxv_rule
$(OUT)/zxv/$(notdir $(1:.c=.o)): $(1) port/compat/zxp_prelude.h port/compat/pthread.h port/include/zxp.h \
    sources.mk lib.mk
	@mkdir -p $$(dir $$@)
	$$(CC) $$(ZXV_CFLAGS) -c $$< -o $$@
endef
define zxv_plain_rule
$(OUT)/zxv/$(notdir $(1:.c=.o)): $(1)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(ZXV_PLAIN_CFLAGS) -c $$< -o $$@
endef
$(foreach s,$(ZXE_LIBZXV_SRC) $(ZXE_KSRC),$(eval $(call zxv_rule,$(s))))
$(foreach s,$(ZXE_KSRC_PLAIN),$(eval $(call zxv_plain_rule,$(s))))

$(OUT)/port/%.o: port/src/%.c port/include/zxp.h port/src/zxp_os.h $(wildcard port/include/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) -Iport/include -c $< -o $@

$(OUT)/zxe/%.o: zxe/src/%.c zxe/include/zxe.h port/include/zxp.h $(ZXE_LIBZXV)/include/zxv_api.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_CFLAGS) $(ZXE_KIT_INC) -c $< -o $@

$(OUT)/libzxv_embedded.a: $(ZXV_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/libzxp.a: $(PORT_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

$(OUT)/libzxe.a: $(ZXE_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^
