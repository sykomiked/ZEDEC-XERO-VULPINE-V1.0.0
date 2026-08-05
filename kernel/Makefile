# Makefile — VOVINA SHAKINA M5 Axiomatic Kernel
# Builds a bootable ISO image using GRUB multiboot
# Author: H.M. Michael-Laurence: Curzi (c)

ARCH ?= x86_32

ifeq ($(ARCH),arm64)
$(error "ARM64 builds are canonical through build_system/Makefile.arm64; see Dockerfile.arm64 or build_system/build_arm64.sh")
else
CC := gcc
LD := ld
CFLAGS := -m32 -std=c11 -Wall -Wextra -ffreestanding -nostdlib -O3 \
          -include include/freestanding.h \
          -Dmemset=fs_memset -Dmemcpy=fs_memcpy -Dstrlen=fs_strlen \
          -Dstrcmp=fs_strcmp -Dstrcpy=fs_strcpy \
          -Dmalloc=fs_malloc -Dcalloc=fs_calloc \
          -Iinclude/freestanding_stubs -Iinclude -Isrc/axiom_matrix -Icompat -Iboot \
          -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase \
          -Isrc/choice -Isrc/phase_coord -Isrc/telemetry \
          -Isrc/crit168 -Isrc/gdt -Isrc/idt -Isrc/pic -Isrc/timer \
          -Isrc/keyboard -Isrc/mouse -Isrc/pci -Isrc/acpi \
          -Isrc/vbe -Isrc/ata -Isrc/fat32 \
          -Isrc/mm -Isrc/sched -Isrc/syscall -Isrc/vfs \
          -Isrc/net -Isrc/vino -Isrc/vena -Isrc/apps -Isrc/holographic \
          -Isrc/license -Isrc/surplus -Isrc/edp_risk -Isrc/predictive \
          -Isrc/situation -Isrc/finance -Isrc/identity -Isrc/quantum \
          -Isrc/hardware -Isrc/synthesis \
          -I../init -I../gui
LINKER := linker.ld
LIBGCC := $(shell gcc -m32 -print-libgcc-file-name 2>/dev/null || echo /usr/lib/gcc/x86_64-linux-gnu/11/32/libgcc.a)
endif

KERNEL_NAME := vovina_shakina
KERNEL_BIN := $(KERNEL_NAME).bin
ISO_NAME := $(KERNEL_NAME).iso

LDFLAGS := -T $(LINKER) -o

# Kernel source files (architecture-conditional)

COMMON_SRCS := src/oseq/oseq_core.c \
    src/rmag/rmag_core.c \
    src/lpres/lpres_core.c \
    src/iphase/iphase_core.c \
    src/choice/choice_core.c \
    src/phase_coord/phase_coordinator.c \
    src/telemetry/telemetry_core.c \
    src/axiom_matrix/axiom_matrix_core.c \
    src/crit168/crit_168_word.c \
    compat/compat_layer.c \
    src/license/license.c \
    src/surplus/surplus.c \
    src/edp_risk/edp_risk.c \
    src/predictive/predictive_model.c \
    src/situation/situation_model.c \
    src/finance/triple_ledger.c \
    src/finance/financial.c \
    src/finance/rails.c \
    src/finance/crypto_bridge.c \
    src/identity/identity.c \
    src/quantum/quantum_device.c \
    src/hardware/rtl_device.c \
    src/synthesis/synthesis_engine.c

X86_SRCS := boot/kernel_main.c \
    boot/framebuffer.c \
    src/gdt/gdt.c \
    src/idt/idt.c \
    src/pic/pic.c \
    src/timer/timer.c \
    src/keyboard/keyboard.c \
    src/mouse/mouse.c \
    src/pci/pci.c \
    src/acpi/acpi.c \
    src/vbe/vbe.c \
    src/ata/ata.c \
    src/fat32/fat32.c \
    src/mm/mm.c \
    src/sched/sched.c \
    src/syscall/syscall.c \
    src/vfs/vfs.c \
    src/net/net.c \
    src/net/m5route.c \
    src/net/rtl8139.c \
    src/net/dtmf.c \
    src/net/radio.c \
    src/net/jdr_piratenet.c \
    src/vino/vino.c \
    src/vena/vena.c \
    src/apps/apps.c \
    src/holographic/holo.c \
    ../init/init.c \
    ../gui/gui.c

ifeq ($(ARCH),arm64)
# Minimal ARM64 core build: boot + M5 subsystems + portable helpers.
# x86-only drivers (net, PCI, PIC, VBE, etc.) and user-space apps
# are excluded from the bring-up image; added back as they are ported.
KERNEL_SRCS := boot/kernel_main_arm64.c \
    freestanding.c \
    src/oseq/oseq_core.c \
    src/rmag/rmag_core.c \
    src/lpres/lpres_core.c \
    src/iphase/iphase_core.c \
    src/choice/choice_core.c \
    src/phase_coord/phase_coordinator.c \
    src/telemetry/telemetry_core.c \
    src/axiom_matrix/axiom_matrix_core.c \
    src/crit168/crit_168_word.c \
    src/surplus/surplus.c \
    src/edp_risk/edp_risk.c \
    src/predictive/predictive_model.c \
    src/situation/situation_model.c \
    src/finance/triple_ledger.c \
    src/finance/financial.c \
    src/finance/rails.c \
    src/finance/crypto_bridge.c \
    src/identity/identity.c \
    src/quantum/quantum_device.c \
    src/hardware/rtl_device.c \
    src/license/license.c \
    src/synthesis/synthesis_engine.c \
    src/vino/vino.c \
    compat/compat_layer.c
BOOT_SRC := boot/boot_arm64.s
BOOT_OBJ := boot/boot_arm64.o
ISR_OBJ :=
else
KERNEL_SRCS := boot/kernel_main.c $(COMMON_SRCS) $(X86_SRCS)
BOOT_SRC := boot/boot.s
BOOT_OBJ := boot/boot.o
ISR_OBJ := src/idt/isr_stubs.o
endif

KERNEL_OBJS := $(KERNEL_SRCS:.c=.o) $(BOOT_OBJ) $(ISR_OBJ)

# Test files
TEST_SRCS := src/oseq/test_oseq.c src/rmag/test_rmag.c src/lpres/test_lpres.c \
             src/iphase/test_iphase.c src/choice/test_choice.c \
             src/phase_coord/test_phase_coordinator.c src/telemetry/test_telemetry.c \
             src/axiom_matrix/test_axiom_matrix.c src/crit168/test_crit_168_word.c \
             compat/test_compat_layer.c

TEST_LIBS := src/oseq/oseq_core.o src/rmag/rmag_core.o src/lpres/lpres_core.o \
             src/iphase/iphase_core.o src/choice/choice_core.o \
             src/phase_coord/phase_coordinator.o src/telemetry/telemetry_core.o \
             src/axiom_matrix/axiom_matrix_core.o src/crit168/crit_168_word.o \
             compat/compat_layer.o

.PHONY: all iso test clean verify

all: iso

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble boot
$(BOOT_OBJ): $(BOOT_SRC)
	$(CC) $(if $(filter arm64,$(ARCH)),,-m32) -c $< -o $@

# Assemble ISR stubs (x86 only)
$(ISR_OBJ): src/idt/isr_stubs.s
	$(CC) -m32 -c $< -o $@

# Link the kernel binary
$(KERNEL_BIN): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) $@ $(KERNEL_OBJS) $(LIBGCC)

# Build the bootable ISO
iso: $(KERNEL_BIN)
	mkdir -p iso/boot/grub
	cp $(KERNEL_BIN) iso/boot/
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO_NAME) iso
	rm -rf iso

# Build and run tests (host-side, 64-bit, not freestanding)
test:
	@echo "=== ZXV OS Test Suite — Fail-Closed Harness ==="; \
	failures=0; passed=0; total=0; \
	for t in $(TEST_SRCS); do \
		dir=$$(dirname $$t); \
		base=$$(basename $$t .c); \
		total=$$((total + 1)); \
		echo "--- Building $$base ---"; \
		if ! gcc -std=c11 -Wall -Werror -D_GNU_SOURCE -Iinclude -I$$dir -Isrc/axiom_matrix -Icompat -Iboot \
			-Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase \
			-Isrc/choice -Isrc/phase_coord -Isrc/telemetry \
			-Isrc/crit168 -Isrc/license -Isrc/surplus -Isrc/edp_risk \
			-Isrc/predictive -Isrc/situation -Isrc/finance -Isrc/identity \
			-Isrc/quantum -Isrc/hardware \
			$$t $(TEST_LIBS:.o=.c) -o /tmp/$$base -lm 2>&1; then \
			echo "[FAIL] $$base: compilation failed"; \
			failures=$$((failures + 1)); \
			continue; \
		fi; \
		echo "--- Running $$base ---"; \
		if /tmp/$$base 2>&1; then \
			echo "[PASS] $$base"; \
			passed=$$((passed + 1)); \
		else \
			echo "[FAIL] $$base: execution failed (exit $$?)"; \
			failures=$$((failures + 1)); \
		fi; \
		echo; \
	done; \
	echo "========================================"; \
	echo "RESULTS: $$passed/$$total passed, $$failures failed"; \
	if [ $$failures -gt 0 ]; then \
		echo "*** TEST SUITE FAILED ***"; \
		exit 1; \
	else \
		echo "*** ALL TESTS PASSED ***"; \
	fi

# Fail-closed verify-all target: host tests + crypto KATs + ARM64 cross-build check
verify-all: test
	@echo "=== Crypto KATs ==="; \
	for t in src/robin_debanks/test_aes_gcm src/robin_debanks/test_sha256; do \
		base=$$(basename $$t); \
		if [ -f $$t.c ]; then \
			gcc -std=c11 -Wall -Werror -Iinclude -Isrc/robin_debanks $$t.c src/robin_debanks/aes256_gcm.c src/robin_debanks/sha256.c -o /tmp/$$base -lm && \
			/tmp/$$base && echo "[PASS] $$base" || { echo "[FAIL] $$base"; exit 1; }; \
		fi; \
	done; \
	echo "=== ML-KEM validators ==="; \
	for t in src/mlkem/keccak_validate src/mlkem/mlkem768_validate; do \
		base=$$(basename $$t); \
		if [ -f $$t.c ]; then \
			gcc -std=c11 -Wall -Werror -Iinclude -Isrc/mlkem $$t.c src/mlkem/keccak.c src/mlkem/mlkem768.c src/mlkem/mlkem_encode.c src/mlkem/mlkem_sample.c src/mlkem/mlkem_ntt.c src/mlkem/mlkem_kpe.c -o /tmp/$$base -lm && \
			/tmp/$$base && echo "[PASS] $$base" || { echo "[FAIL] $$base"; exit 1; }; \
		fi; \
	done; \
	echo "=== Event-Space Infrastructure ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space \
		../tests/host/test_event_space.c \
		src/event_space/event_envelope.c \
		src/event_space/event_sequencer.c \
		src/event_space/self_audit.c \
		src/event_space/self_healing.c \
		-o /tmp/test_event_space -lm && \
		/tmp/test_event_space && echo "[PASS] test_event_space" || { echo "[FAIL] test_event_space"; exit 1; }; \
	echo "=== Orbital Elevator + Constellation + Transport ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/orbital_elevator -Isrc/constellation -Isrc/event_transport \
		../tests/host/test_orbital_constellation.c \
		src/event_space/event_envelope.c \
		src/event_space/event_sequencer.c \
		src/event_space/self_audit.c \
		src/event_space/self_healing.c \
		src/orbital_elevator/orbital_elevator.c \
		src/constellation/constellation_coordinator.c \
		src/event_transport/event_transport.c \
		-o /tmp/test_orbital_constellation -lm && \
		/tmp/test_orbital_constellation && echo "[PASS] test_orbital_constellation" || { echo "[FAIL] test_orbital_constellation"; exit 1; }; \
	echo "=== Hypercube Scene Architecture ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/hypercube \
		../tests/host/test_hypercube.c \
		src/event_space/event_envelope.c \
		src/event_space/event_sequencer.c \
		src/event_space/self_audit.c \
		src/event_space/self_healing.c \
		src/hypercube/hypercube_scene.c \
		-o /tmp/test_hypercube -lm && \
		/tmp/test_hypercube && echo "[PASS] test_hypercube" || { echo "[FAIL] test_hypercube"; exit 1; }; \
	echo "=== Yantra Fabric ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/yantra \
		../tests/host/test_yantra.c \
		src/event_space/event_envelope.c \
		src/event_space/event_sequencer.c \
		src/event_space/self_audit.c \
		src/event_space/self_healing.c \
		src/yantra/yantra_fabric.c \
		-o /tmp/test_yantra -lm && \
		/tmp/test_yantra && echo "[PASS] test_yantra" || { echo "[FAIL] test_yantra"; exit 1; }; \
	echo "=== Dual-Space Programming ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/dual_space \
		../tests/host/test_dual_space.c \
		src/event_space/event_envelope.c \
		src/event_space/event_sequencer.c \
		src/event_space/self_audit.c \
		src/event_space/self_healing.c \
		src/dual_space/dual_space.c \
		-o /tmp/test_dual_space -lm && \
		/tmp/test_dual_space && echo "[PASS] test_dual_space" || { echo "[FAIL] test_dual_space"; exit 1; }; \
	echo "=== RUR Type System ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/rur \
		../tests/host/test_rur.c \
		src/rur/rur.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_rur -lm && \
		/tmp/test_rur && echo "[PASS] test_rur" || { echo "[FAIL] test_rur"; exit 1; }; \
	echo "=== RCE Units & Dimensions ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/rce \
		../tests/host/test_rce_units.c \
		src/rce/rce_units.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_rce_units -lm && \
		/tmp/test_rce_units && echo "[PASS] test_rce_units" || { echo "[FAIL] test_rce_units"; exit 1; }; \
	echo "=== UBH Matrix ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/ubh \
		../tests/host/test_ubh.c \
		src/ubh/ubh.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_ubh -lm && \
		/tmp/test_ubh && echo "[PASS] test_ubh" || { echo "[FAIL] test_ubh"; exit 1; }; \
	echo "=== RMAG (K2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/rmag \
		../tests/host/test_rmag.c \
		src/rmag/rmag.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_rmag -lm && \
		/tmp/test_rmag && echo "[PASS] test_rmag" || { echo "[FAIL] test_rmag"; exit 1; }; \
	echo "=== Phase Coordinator (K6) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/phase_coord \
		../tests/host/test_phase_coord.c \
		src/phase_coord/phase_coord.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_phase_coord -lm && \
		/tmp/test_phase_coord && echo "[PASS] test_phase_coord" || { echo "[FAIL] test_phase_coord"; exit 1; }; \
	echo "=== LPRES (K3) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/lpres \
		../tests/host/test_lpres.c \
		src/lpres/lpres.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_lpres -lm && \
		/tmp/test_lpres && echo "[PASS] test_lpres" || { echo "[FAIL] test_lpres"; exit 1; }; \
	echo "=== MacGyver (MCG0) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/macgyver \
		../tests/host/test_macgyver.c \
		src/macgyver/macgyver.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_macgyver -lm && \
		/tmp/test_macgyver && echo "[PASS] test_macgyver" || { echo "[FAIL] test_macgyver"; exit 1; }; \
	echo "=== OSEQ (K1) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/oseq \
		../tests/host/test_oseq.c \
		src/oseq/oseq.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_oseq -lm && \
		/tmp/test_oseq && echo "[PASS] test_oseq" || { echo "[FAIL] test_oseq"; exit 1; }; \
	echo "=== IPHASE (K4) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/iphase \
		../tests/host/test_iphase.c \
		src/iphase/iphase.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_iphase -lm && \
		/tmp/test_iphase && echo "[PASS] test_iphase" || { echo "[FAIL] test_iphase"; exit 1; }; \
	echo "=== CHOICE (K5) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/choice \
		../tests/host/test_choice.c \
		src/choice/choice.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_choice -lm && \
		/tmp/test_choice && echo "[PASS] test_choice" || { echo "[FAIL] test_choice"; exit 1; }; \
	echo "=== CRIT_168 OS (O1) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/crit168 \
		../tests/host/test_crit168_os.c \
		src/crit168/crit168_os.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_crit168_os -lm && \
		/tmp/test_crit168_os && echo "[PASS] test_crit168_os" || { echo "[FAIL] test_crit168_os"; exit 1; }; \
	echo "=== FS (O2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/event_space -Isrc/fs \
		../tests/host/test_fs.c \
		src/fs/fs.c \
		src/event_space/event_envelope.c \
		-o /tmp/test_fs -lm && \
		/tmp/test_fs && echo "[PASS] test_fs" || { echo "[FAIL] test_fs"; exit 1; }; \
	echo "=== Sutra Lexer (W2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/sutra -Isrc/vino -Isrc/rmag -Isrc/lpres \
		src/sutra/test_sutra_lexer.c src/sutra/sutra_lexer.c src/event_space/event_envelope.c \
		-o /tmp/test_sutra_lexer -lm && \
		/tmp/test_sutra_lexer && echo "[PASS] test_sutra_lexer" || { echo "[FAIL] test_sutra_lexer"; exit 1; }; \
	echo "=== Sutra Parser (W2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/sutra -Isrc/vino -Isrc/rmag -Isrc/lpres \
		src/sutra/test_sutra_parser.c src/sutra/sutra_lexer.c src/sutra/sutra_parser.c src/sutra/sutra_runtime.c \
		src/sutra/sutra_selfaudit.c src/sutra/sutra_chiglet.c src/sutra/sutra_capital.c src/sutra/sutra_rails.c \
		src/vino/vino.c src/rmag/rmag_core.c src/lpres/lpres_core.c src/event_space/event_envelope.c \
		-o /tmp/test_sutra_parser -lm && \
		/tmp/test_sutra_parser && echo "[PASS] test_sutra_parser" || { echo "[FAIL] test_sutra_parser"; exit 1; }; \
	echo "=== Sutra E2E (W2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/sutra -Isrc/vino -Isrc/rmag -Isrc/lpres \
		src/sutra/test_sutra_e2e.c src/sutra/sutra_lexer.c src/sutra/sutra_parser.c src/sutra/sutra_runtime.c \
		src/sutra/sutra_selfaudit.c src/sutra/sutra_chiglet.c src/sutra/sutra_capital.c src/sutra/sutra_rails.c \
		src/vino/vino.c src/rmag/rmag_core.c src/lpres/lpres_core.c src/event_space/event_envelope.c \
		-o /tmp/test_sutra_e2e -lm && \
		/tmp/test_sutra_e2e && echo "[PASS] test_sutra_e2e" || { echo "[FAIL] test_sutra_e2e"; exit 1; }; \
	echo "=== Sutra Capital (W2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/sutra -Isrc/vino -Isrc/rmag -Isrc/lpres \
		src/sutra/test_sutra_capital.c src/sutra/sutra_lexer.c src/sutra/sutra_parser.c src/sutra/sutra_runtime.c \
		src/sutra/sutra_selfaudit.c src/sutra/sutra_chiglet.c src/sutra/sutra_capital.c src/sutra/sutra_rails.c \
		src/vino/vino.c src/rmag/rmag_core.c src/lpres/lpres_core.c src/event_space/event_envelope.c \
		-o /tmp/test_sutra_capital -lm && \
		/tmp/test_sutra_capital && echo "[PASS] test_sutra_capital" || { echo "[FAIL] test_sutra_capital"; exit 1; }; \
	echo "=== Sutra Chiglet (W2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/sutra -Isrc/vino -Isrc/rmag -Isrc/lpres \
		src/sutra/test_sutra_chiglet.c src/sutra/sutra_lexer.c src/sutra/sutra_parser.c src/sutra/sutra_runtime.c \
		src/sutra/sutra_selfaudit.c src/sutra/sutra_chiglet.c src/sutra/sutra_capital.c src/sutra/sutra_rails.c \
		src/vino/vino.c src/rmag/rmag_core.c src/lpres/lpres_core.c src/event_space/event_envelope.c \
		-o /tmp/test_sutra_chiglet -lm && \
		/tmp/test_sutra_chiglet && echo "[PASS] test_sutra_chiglet" || { echo "[FAIL] test_sutra_chiglet"; exit 1; }; \
	echo "=== Sutra Rails (W2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/sutra -Isrc/vino -Isrc/rmag -Isrc/lpres \
		src/sutra/test_sutra_rails.c src/sutra/sutra_lexer.c src/sutra/sutra_parser.c src/sutra/sutra_runtime.c \
		src/sutra/sutra_selfaudit.c src/sutra/sutra_chiglet.c src/sutra/sutra_capital.c src/sutra/sutra_rails.c \
		src/vino/vino.c src/rmag/rmag_core.c src/lpres/lpres_core.c src/event_space/event_envelope.c \
		-o /tmp/test_sutra_rails -lm && \
		/tmp/test_sutra_rails && echo "[PASS] test_sutra_rails" || { echo "[FAIL] test_sutra_rails"; exit 1; }; \
	echo "=== Sutra Self-Audit (W2) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/sutra -Isrc/vino -Isrc/rmag -Isrc/lpres \
		src/sutra/test_sutra_selfaudit.c src/sutra/sutra_lexer.c src/sutra/sutra_parser.c src/sutra/sutra_runtime.c \
		src/sutra/sutra_selfaudit.c src/sutra/sutra_chiglet.c src/sutra/sutra_capital.c src/sutra/sutra_rails.c \
		src/vino/vino.c src/rmag/rmag_core.c src/lpres/lpres_core.c src/event_space/event_envelope.c \
		-o /tmp/test_sutra_selfaudit -lm && \
		/tmp/test_sutra_selfaudit && echo "[PASS] test_sutra_selfaudit" || { echo "[FAIL] test_sutra_selfaudit"; exit 1; }; \
	echo "=== Porter House (W5) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/porter_house -Isrc/surplus -Isrc/edp_risk \
		src/porter_house/test_porter_house.c src/porter_house/porter_house.c src/event_space/event_envelope.c \
		-o /tmp/test_porter_house -lm && \
		/tmp/test_porter_house && echo "[PASS] test_porter_house" || { echo "[FAIL] test_porter_house"; exit 1; }; \
 echo "=== P-TERM (W6) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/pterm \
		../tests/host/test_pterm.c src/pterm/pterm.c src/pterm/pterm_commands.c src/event_space/event_envelope.c \
		-o /tmp/test_pterm -lm && \
		/tmp/test_pterm && echo "[PASS] test_pterm" || { echo "[FAIL] test_pterm"; exit 1; }; \
	echo "=== Desktop (W7) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/event_space -Isrc/desktop -Isrc/surplus -Isrc/edp_risk \
		../tests/host/test_desktop.c src/desktop/desktop.c src/event_space/event_envelope.c \
		-o /tmp/test_desktop -lm && \
		/tmp/test_desktop && echo "[PASS] test_desktop" || { echo "[FAIL] test_desktop"; exit 1; }; \
	echo "=== Cellular Multikernel (CELL-001) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/cellular_multikernel \
		../tests/host/test_cellular_multikernel.c src/cellular_multikernel/cellular_multikernel.c \
		-o /tmp/test_cellular_multikernel -lm && \
		/tmp/test_cellular_multikernel && echo "[PASS] test_cellular_multikernel" || { echo "[FAIL] test_cellular_multikernel"; exit 1; }; \
	echo "=== ISF axioms on the FIXED-POINT path (S2/S4/monotone/concave/Lipschitz) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/surplus \
		src/surplus/test_surplus_axioms.c src/surplus/surplus.c \
		-o /tmp/test_surplus_axioms -lm && \
		/tmp/test_surplus_axioms && echo "[PASS] test_surplus_axioms" || { echo "[FAIL] test_surplus_axioms"; exit 1; }; \
	echo "=== P-TERM phase-tick multiplexer (ISF-weighted dispatch) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/pterm \
		src/pterm/test_pterm_mux.c src/pterm/pterm_mux.c \
		-o /tmp/test_pterm_mux -lm && \
		/tmp/test_pterm_mux && echo "[PASS] test_pterm_mux" || { echo "[FAIL] test_pterm_mux"; exit 1; }; \
	echo "=== AppKit document model (gap buffer + coalesced undo/redo) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/appkit \
		src/appkit/test_doc.c src/appkit/doc.c \
		-o /tmp/test_doc -lm && \
		/tmp/test_doc && echo "[PASS] test_doc" || { echo "[FAIL] test_doc"; exit 1; }; \
	echo "=== Desktop shimmer field (event-driven, deterministic) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/shimmer \
		src/shimmer/test_shimmer.c src/shimmer/shimmer.c \
		-o /tmp/test_shimmer -lm && \
		/tmp/test_shimmer && echo "[PASS] test_shimmer" || { echo "[FAIL] test_shimmer"; exit 1; }; \
	echo "=== Fractal Z-order locality (MEASURED movement reduction) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/fractal \
		src/fractal/test_zorder.c src/fractal/zorder.c \
		-o /tmp/test_zorder -lm && \
		/tmp/test_zorder && echo "[PASS] test_zorder" || { echo "[FAIL] test_zorder"; exit 1; }; \
	echo "=== WyvernEye exact rational arithmetic (vs floating point) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/rational \
		src/rational/test_rational.c src/rational/rational.c \
		-o /tmp/test_rational -lm && \
		/tmp/test_rational && echo "[PASS] test_rational" || { echo "[FAIL] test_rational"; exit 1; }; \
	echo "=== Smaug's Abacus multilateral clearing (conservation + net preservation) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/abacus -Isrc/rational \
		src/abacus/test_abacus.c src/abacus/abacus.c src/rational/rational.c \
		-o /tmp/test_abacus -lm && \
		/tmp/test_abacus && echo "[PASS] test_abacus" || { echo "[FAIL] test_abacus"; exit 1; }; \
	echo "=== Lightning Rod legacy adapters (COBOL/Fortran/EBCDIC) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/lightningrod -Isrc/rational \
		src/lightningrod/test_lightningrod.c src/lightningrod/lightningrod.c \
		src/rational/rational.c -o /tmp/test_lr -lm && \
		/tmp/test_lr && echo "[PASS] test_lightningrod" || { echo "[FAIL] test_lightningrod"; exit 1; }; \
	echo "=== zBIOS ten-key staged measured boot (chain of trust) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/bios -Isrc/robin_debanks \
		src/bios/test_zbios.c src/bios/zbios.c src/robin_debanks/sha256.c \
		-o /tmp/test_zbios -lm && \
		/tmp/test_zbios && echo "[PASS] test_zbios" || { echo "[FAIL] test_zbios"; exit 1; }; \
	echo "=== Chiglet ISF mixture-of-experts (duplication-attack regression) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/chiglet -Isrc/surplus \
		src/chiglet/test_chiglet.c src/chiglet/chiglet.c src/surplus/surplus.c \
		-o /tmp/test_chiglet -lm && \
		/tmp/test_chiglet && echo "[PASS] test_chiglet" || { echo "[FAIL] test_chiglet"; exit 1; }; \
	echo "=== ZXVFS persistent filesystem + crash journal ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DZXVFS_HOST -Iinclude -Isrc/zxvfs \
		src/zxvfs/test_zxvfs.c src/zxvfs/zxvfs.c \
		-o /tmp/test_zxvfs -lm && \
		/tmp/test_zxvfs && echo "[PASS] test_zxvfs" || { echo "[FAIL] test_zxvfs"; exit 1; }; \
	echo "=== ELF64 loader (incl. adversarial/negative tests) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/loader \
		src/loader/test_elf.c src/loader/elf.c \
		-o /tmp/test_elf -lm && \
		/tmp/test_elf && echo "[PASS] test_elf" || { echo "[FAIL] test_elf"; exit 1; }; \
	echo "=== ZSP signed-package verification (Ed25519 + SHA-256) ==="; \
	if [ -f userapp/hello_signed.h ]; then \
		gcc -std=c11 -Wall -Werror -Wextra -Isrc/loader -Isrc/robin_debanks -Iuserapp \
			src/loader/test_zsp.c src/loader/zsp.c \
			src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c \
			-o /tmp/test_zsp -lm && \
			/tmp/test_zsp && echo "[PASS] test_zsp" || { echo "[FAIL] test_zsp"; exit 1; }; \
	else echo "[SKIP] test_zsp (run build_system/build_signed_app.sh to generate hello_signed.h)"; fi; \
	echo "=== A/B signed update + probation + auto-rollback ==="; \
	if [ -f userapp/hello_signed.h ]; then \
		gcc -std=c11 -Wall -Werror -Wextra -DZXVFS_HOST -Isrc/loader -Isrc/zxvfs -Isrc/robin_debanks -Iinclude -Iuserapp \
			src/loader/test_abupdate.c src/loader/abupdate.c src/loader/zsp.c src/zxvfs/zxvfs.c \
			src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c \
			-o /tmp/test_abupdate -lm && \
			/tmp/test_abupdate && echo "[PASS] test_abupdate" || { echo "[FAIL] test_abupdate"; exit 1; }; \
	else echo "[SKIP] test_abupdate (needs hello_signed.h)"; fi; \
	echo "=== Self-audit regressions: rational / abacus / lightningrod ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/rational \
		src/rational/test_rational_regress.c src/rational/rational.c \
		-o /tmp/test_rational_regress -lm && \
		/tmp/test_rational_regress && echo "[PASS] test_rational_regress" || { echo "[FAIL] test_rational_regress"; exit 1; }; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/abacus -Isrc/rational \
		src/abacus/test_abacus_regress.c src/abacus/abacus.c src/rational/rational.c \
		-o /tmp/test_abacus_regress -lm && \
		/tmp/test_abacus_regress && echo "[PASS] test_abacus_regress" || { echo "[FAIL] test_abacus_regress"; exit 1; }; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/lightningrod -Isrc/rational \
		src/lightningrod/test_lightningrod_regress.c src/lightningrod/lightningrod.c src/rational/rational.c \
		-o /tmp/test_lr_regress -lm && \
		/tmp/test_lr_regress && echo "[PASS] test_lightningrod_regress" || { echo "[FAIL] test_lightningrod_regress"; exit 1; }; \
	echo "=== ZCA card activation language (untrusted-input gate) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/cards -Isrc/chiglet -Isrc/surplus \
		src/cards/test_zca.c src/cards/zca.c src/chiglet/chiglet.c src/surplus/surplus.c \
		-o /tmp/test_zca -lm && \
		/tmp/test_zca && echo "[PASS] test_zca" || { echo "[FAIL] test_zca"; exit 1; }; \
	echo "=== Sigil circuit: the spell shape as a dataflow graph ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/cards \
		src/cards/test_sigil.c src/cards/sigil.c \
		-o /tmp/test_sigil -lm && \
		/tmp/test_sigil && echo "[PASS] test_sigil" || { echo "[FAIL] test_sigil"; exit 1; }; \
	echo "=== Card modules: breadth beats depth in a loadout ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/cards -Isrc/chiglet -Isrc/surplus \
		src/cards/test_cards.c src/cards/cards.c src/cards/zca.c \
		src/chiglet/chiglet.c src/surplus/surplus.c \
		-o /tmp/test_cards -lm && \
		/tmp/test_cards && echo "[PASS] test_cards" || { echo "[FAIL] test_cards"; exit 1; }; \
	echo "=== Magitech Refinery: text -> sigil -> shareable AI preset ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/refinery -Isrc/cards -Isrc/chiglet -Isrc/surplus -Isrc/robin_debanks \
		src/refinery/test_refinery.c src/refinery/refinery.c src/refinery/enochian.c \
		src/cards/sigil.c src/cards/zca.c src/chiglet/chiglet.c src/surplus/surplus.c src/robin_debanks/sha256.c \
		-o /tmp/test_refinery -lm && \
		/tmp/test_refinery && echo "[PASS] test_refinery" || { echo "[FAIL] test_refinery"; exit 1; }; \
	echo "=== Concord: ISF-driven non-coercive social matching ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/concord -Isrc/chiglet -Isrc/surplus \
		src/concord/test_concord.c src/concord/concord.c src/chiglet/chiglet.c src/surplus/surplus.c \
		-o /tmp/test_concord -lm && \
		/tmp/test_concord && echo "[PASS] test_concord" || { echo "[FAIL] test_concord"; exit 1; }; \
	echo "=== Den Connect sovereign-node governance ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/denconnect -Isrc/robin_debanks \
		src/denconnect/test_denconnect.c src/denconnect/denconnect.c src/robin_debanks/sha256.c \
		-o /tmp/test_denconnect && \
		/tmp/test_denconnect && echo "[PASS] test_denconnect" || { echo "[FAIL] test_denconnect"; exit 1; }; \
	echo "=== Wyrmgate: composed six-fold Tri-Space judgment (rmag+lpres+chiglet) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/wyrmgate -Isrc/rmag -Isrc/lpres -Isrc/chiglet -Isrc/surplus -Isrc/event_space -Iinclude \
		src/wyrmgate/test_wyrmgate.c src/wyrmgate/wyrmgate.c src/rmag/rmag.c src/lpres/lpres.c src/chiglet/chiglet.c src/surplus/surplus.c \
		-o /tmp/test_wyrmgate -lm && \
		/tmp/test_wyrmgate && echo "[PASS] test_wyrmgate" || { echo "[FAIL] test_wyrmgate"; exit 1; }; \
	echo "=== WyvernEye: intent forged then judged (refinery+wyrmgate composed) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/wyverneye -Isrc/refinery -Isrc/wyrmgate -Isrc/cards -Isrc/rmag -Isrc/lpres -Isrc/chiglet -Isrc/surplus -Isrc/robin_debanks -Isrc/event_space -Iinclude \
		src/wyverneye/test_wyverneye.c src/wyverneye/wyverneye.c src/wyrmgate/wyrmgate.c \
		src/refinery/refinery.c src/refinery/enochian.c src/cards/sigil.c src/cards/zca.c \
		src/rmag/rmag.c src/lpres/lpres.c src/chiglet/chiglet.c src/surplus/surplus.c src/robin_debanks/sha256.c \
		-o /tmp/test_wyverneye -lm && \
		/tmp/test_wyverneye && echo "[PASS] test_wyverneye" || { echo "[FAIL] test_wyverneye"; exit 1; }; \
	echo "=== Chronicle: hash-chained, self-resolving judgment ledger ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/chronicle -Isrc/wyrmgate -Isrc/rmag -Isrc/lpres -Isrc/chiglet -Isrc/surplus -Isrc/robin_debanks -Isrc/event_space -Iinclude \
		src/chronicle/test_chronicle.c src/chronicle/chronicle.c src/wyrmgate/wyrmgate.c \
		src/rmag/rmag.c src/lpres/lpres.c src/chiglet/chiglet.c src/surplus/surplus.c src/robin_debanks/sha256.c \
		-o /tmp/test_chronicle -lm && \
		/tmp/test_chronicle && echo "[PASS] test_chronicle" || { echo "[FAIL] test_chronicle"; exit 1; }; \
	echo "=== Chronicle durability: survives reboot + power loss (chronicle+zxvfs) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -DZXVFS_HOST -Isrc/chronicle -Isrc/wyrmgate -Isrc/rmag -Isrc/lpres -Isrc/chiglet -Isrc/surplus -Isrc/robin_debanks -Isrc/event_space -Isrc/zxvfs -Iinclude \
		src/chronicle/test_chronicle_persist.c src/chronicle/chronicle_persist.c src/chronicle/chronicle.c src/wyrmgate/wyrmgate.c \
		src/rmag/rmag.c src/lpres/lpres.c src/chiglet/chiglet.c src/surplus/surplus.c src/robin_debanks/sha256.c src/zxvfs/zxvfs.c \
		-o /tmp/test_chronicle_persist -lm && \
		/tmp/test_chronicle_persist && echo "[PASS] test_chronicle_persist" || { echo "[FAIL] test_chronicle_persist"; exit 1; }; \
	echo "=== virtio split-virtqueue engine (vring) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/virtio \
		src/virtio/test_vring.c src/virtio/vring.c \
		-o /tmp/test_vring && \
		/tmp/test_vring && echo "[PASS] test_vring" || { echo "[FAIL] test_vring"; exit 1; }; \
	echo "=== Tri-Space artifact triad (ZXV native module format) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/trispace -Isrc/robin_debanks \
		src/trispace/test_trispace.c src/trispace/trispace.c src/robin_debanks/sha256.c \
		-o /tmp/test_trispace -lm && \
		/tmp/test_trispace && echo "[PASS] test_trispace" || { echo "[FAIL] test_trispace"; exit 1; }; \
	echo "=== Count House stash/valuation (Ed25519-verified deposits) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/chiglet -Isrc/trispace -Isrc/porter_house -Isrc/vino -Isrc/vena -Isrc/count_house -Isrc/mesh_token -Isrc/community_chest -Isrc/telemetry -Isrc/finance -Isrc/identity -Isrc/license -Isrc/net -Isrc/quantum -Isrc/hardware -Isrc/mesh_net -Isrc/immigration -Isrc/ai_layer -Isrc/prism_break -Isrc/edp_risk -Isrc/predictive -Isrc/situation -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/axiom_matrix -Isrc/dual_space -Isrc/lattice \
		src/count_house/test_count_house.c src/count_house/count_house.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_count_house -lm && \
		/tmp/test_count_house && echo "[PASS] test_count_house" || { echo "[FAIL] test_count_house"; exit 1; }; \
	echo "=== Mesh token ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/chiglet -Isrc/trispace -Isrc/porter_house -Isrc/vino -Isrc/vena -Isrc/count_house -Isrc/mesh_token -Isrc/community_chest -Isrc/telemetry -Isrc/finance -Isrc/identity -Isrc/license -Isrc/net -Isrc/quantum -Isrc/hardware -Isrc/mesh_net -Isrc/immigration -Isrc/ai_layer -Isrc/prism_break -Isrc/edp_risk -Isrc/predictive -Isrc/situation -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/axiom_matrix -Isrc/dual_space -Isrc/lattice \
		src/mesh_token/test_mesh_token.c src/mesh_token/mesh_token.c src/count_house/count_house.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_mesh_token -lm && \
		/tmp/test_mesh_token && echo "[PASS] test_mesh_token" || { echo "[FAIL] test_mesh_token"; exit 1; }; \
	echo "=== Mesh networking ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/chiglet -Isrc/trispace -Isrc/porter_house -Isrc/vino -Isrc/vena -Isrc/count_house -Isrc/mesh_token -Isrc/community_chest -Isrc/telemetry -Isrc/finance -Isrc/identity -Isrc/license -Isrc/net -Isrc/quantum -Isrc/hardware -Isrc/mesh_net -Isrc/immigration -Isrc/ai_layer -Isrc/prism_break -Isrc/edp_risk -Isrc/predictive -Isrc/situation -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/axiom_matrix -Isrc/dual_space -Isrc/lattice \
		src/mesh_net/test_mesh_net.c src/mesh_net/mesh_net.c src/count_house/count_house.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_mesh_net -lm && \
		/tmp/test_mesh_net && echo "[PASS] test_mesh_net" || { echo "[FAIL] test_mesh_net"; exit 1; }; \
	echo "=== Prism Break ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/chiglet -Isrc/trispace -Isrc/porter_house -Isrc/vino -Isrc/vena -Isrc/count_house -Isrc/mesh_token -Isrc/community_chest -Isrc/telemetry -Isrc/finance -Isrc/identity -Isrc/license -Isrc/net -Isrc/quantum -Isrc/hardware -Isrc/mesh_net -Isrc/immigration -Isrc/ai_layer -Isrc/prism_break -Isrc/edp_risk -Isrc/predictive -Isrc/situation -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/axiom_matrix -Isrc/dual_space -Isrc/lattice \
		src/prism_break/test_prism_break.c src/prism_break/prism_break.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_prism_break -lm && \
		/tmp/test_prism_break && echo "[PASS] test_prism_break" || { echo "[FAIL] test_prism_break"; exit 1; }; \
	echo "=== Event scheduler ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/chiglet -Isrc/trispace -Isrc/porter_house -Isrc/vino -Isrc/vena -Isrc/count_house -Isrc/mesh_token -Isrc/community_chest -Isrc/telemetry -Isrc/finance -Isrc/identity -Isrc/license -Isrc/net -Isrc/quantum -Isrc/hardware -Isrc/mesh_net -Isrc/immigration -Isrc/ai_layer -Isrc/prism_break -Isrc/edp_risk -Isrc/predictive -Isrc/situation -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/axiom_matrix -Isrc/dual_space -Isrc/lattice \
		src/event_sched/test_event_sched.c src/event_sched/event_sched.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_event_sched -lm && \
		/tmp/test_event_sched && echo "[PASS] test_event_sched" || { echo "[FAIL] test_event_sched"; exit 1; }; \
	echo "=== Quantum device model ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/chiglet -Isrc/trispace -Isrc/porter_house -Isrc/vino -Isrc/vena -Isrc/count_house -Isrc/mesh_token -Isrc/community_chest -Isrc/telemetry -Isrc/finance -Isrc/identity -Isrc/license -Isrc/net -Isrc/quantum -Isrc/hardware -Isrc/mesh_net -Isrc/immigration -Isrc/ai_layer -Isrc/prism_break -Isrc/edp_risk -Isrc/predictive -Isrc/situation -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/axiom_matrix -Isrc/dual_space -Isrc/lattice \
		src/quantum/test_quantum_device.c src/quantum/quantum_device.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_quantum_device -lm && \
		/tmp/test_quantum_device && echo "[PASS] test_quantum_device" || { echo "[FAIL] test_quantum_device"; exit 1; }; \
	echo "=== DLP projector (hardware model) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/chiglet -Isrc/trispace -Isrc/porter_house -Isrc/vino -Isrc/vena -Isrc/count_house -Isrc/mesh_token -Isrc/community_chest -Isrc/telemetry -Isrc/finance -Isrc/identity -Isrc/license -Isrc/net -Isrc/quantum -Isrc/hardware -Isrc/mesh_net -Isrc/immigration -Isrc/ai_layer -Isrc/prism_break -Isrc/edp_risk -Isrc/predictive -Isrc/situation -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/axiom_matrix -Isrc/dual_space -Isrc/lattice \
		src/hardware/test_dlp_projector.c src/hardware/dlp_projector.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_dlp_projector -lm && \
		/tmp/test_dlp_projector && echo "[PASS] test_dlp_projector" || { echo "[FAIL] test_dlp_projector"; exit 1; }; \
	echo "=== verify-all: ALL STAGE-1 CHECKS PASSED ==="

# Adversarial parser fuzzers under ASan+UBSan (untrusted-input gate).
# Iteration counts are overridable: make fuzz FUZZ_ELF_ITERS=1000000
FUZZ_ELF_ITERS ?= 200000
FUZZ_ZXVFS_ITERS ?= 60000
SAN_FLAGS := -O1 -g -fsanitize=address,undefined
.PHONY: fuzz
fuzz:
	@echo "=== fuzz_elf (ELF64 loader) under ASan+UBSan ==="; \
	cc $(SAN_FLAGS) -Isrc/loader tests/fuzz/fuzz_elf.c src/loader/elf.c \
		-o /tmp/fuzz_elf && /tmp/fuzz_elf $(FUZZ_ELF_ITERS) \
		|| { echo "[FAIL] fuzz_elf"; exit 1; }; \
	echo "=== fuzz_zxvfs (ZXVFS mount/recover) under ASan+UBSan ==="; \
	cc $(SAN_FLAGS) -DZXVFS_HOST -Iinclude -Isrc/zxvfs \
		tests/fuzz/fuzz_zxvfs.c src/zxvfs/zxvfs.c \
		-o /tmp/fuzz_zxvfs && /tmp/fuzz_zxvfs $(FUZZ_ZXVFS_ITERS) \
		|| { echo "[FAIL] fuzz_zxvfs"; exit 1; }; \
	echo "*** FUZZ SUITE PASSED ***"

# Experimental / Stage-9 tests that are not yet required for G1 build integrity
verify-experimental: verify-all
	@echo "=== Experimental KATs (Stage 9 security) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/robin_debanks \
		../tests/host/test_ed25519_kat.c src/robin_debanks/ed25519_verify.c src/robin_debanks/sha256.c \
		-o /tmp/test_ed25519_kat -lm && \
		/tmp/test_ed25519_kat && echo "[PASS] test_ed25519_kat" || { echo "[FAIL] test_ed25519_kat"; exit 1; };

# Run the axiom verifier
verify:
	@python3 ../edp_axiom_verifier.py $$(find . -name '*.c' -o -name '*.h' | sed 's/^/--file /' | tr '\n' ' ')

clean:
	rm -f $(KERNEL_OBJS) $(KERNEL_BIN) $(ISO_NAME)
	rm -rf iso
