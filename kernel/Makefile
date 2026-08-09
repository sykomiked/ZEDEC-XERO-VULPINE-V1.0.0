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
FUZZ_FS_ITERS ?= 3000

verify-all: test
	@echo "=== The One Policy: Symbiotic Maxim (void-by-definition) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/onepolicy -Isrc/surplus src/onepolicy/test_onepolicy.c src/onepolicy/onepolicy.c -o /tmp/test_onepolicy -lm && /tmp/test_onepolicy && echo "[PASS] test_onepolicy" || { echo "[FAIL] test_onepolicy"; exit 1; }
	@echo "=== zcapital: nine-form capital substrate + inalienability wall ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/zcapital -Isrc/surplus src/zcapital/test_zcapital.c src/zcapital/zcapital.c -o /tmp/test_zcapital -lm && /tmp/test_zcapital && echo "[PASS] test_zcapital" || { echo "[FAIL] test_zcapital"; exit 1; }
	@echo "=== ipfs: content-addressed self-certifying get (the smuggler's hold) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/ipfs -Isrc/robin_debanks -Isrc/update -Isrc/bridge -Isrc/surplus src/ipfs/test_ipfs.c src/ipfs/ipfs.c src/robin_debanks/sha256.c src/bridge/bridge.c -o /tmp/test_ipfs -lm && /tmp/test_ipfs && echo "[PASS] test_ipfs" || { echo "[FAIL] test_ipfs"; exit 1; }
	@echo "=== crown: the Sicilian Crown pillar (ISC, 7-step registration, CHN) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/crown -Isrc/zcapital -Isrc/identity -Isrc/robin_debanks -Isrc/onepolicy -Isrc/surplus -Isrc/edp_risk src/crown/test_crown.c src/crown/crown.c src/onepolicy/onepolicy.c src/robin_debanks/ed25519_verify.c src/robin_debanks/sha256.c -o /tmp/test_crown -lm && /tmp/test_crown && echo "[PASS] test_crown" || { echo "[FAIL] test_crown"; exit 1; }
	@echo "=== ministry: the Illumaheart Foundation pillar (treasury, tribute/gratuity) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/ministry -Isrc/zcapital -Isrc/finance -Isrc/vino -Isrc/surplus -Isrc/onepolicy -Isrc/edp_risk src/ministry/test_ministry.c src/ministry/ministry.c src/finance/triple_ledger.c src/zcapital/zcapital.c src/onepolicy/onepolicy.c -o /tmp/test_ministry -lm && /tmp/test_ministry && echo "[PASS] test_ministry" || { echo "[FAIL] test_ministry"; exit 1; }
	@echo "=== anti-capture: Crown issues no money, Ministry issues no credentials ==="; \
	( grep -E '^[[:space:]]*#include' src/crown/crown.c | grep -iE 'ledger|vino|wallet|ministry|finance|voucher|treasury' && { echo "[FAIL] crown.c includes a money header"; exit 1; } ; \
	  grep -E '^[[:space:]]*#include' src/ministry/ministry.c | grep -iE '"crown' && { echo "[FAIL] ministry.c includes crown"; exit 1; } ; \
	  echo "[PASS] anti-capture separation holds (corruption would be a build error)" )
	@echo "=== vino_stores: dual-rail Vino settlement engine (>=1.8x, no debt) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/vino_stores -Isrc/finance -Isrc/vino -Isrc/surplus -Isrc/onepolicy -Isrc/edp_risk -Isrc/robin_debanks src/vino_stores/test_vino_stores.c src/vino_stores/vino_stores.c src/finance/triple_ledger.c src/onepolicy/onepolicy.c src/edp_risk/edp_risk.c src/robin_debanks/sha256.c -o /tmp/test_vino_stores -lm && /tmp/test_vino_stores && echo "[PASS] test_vino_stores" || { echo "[FAIL] test_vino_stores"; exit 1; }
	@echo "=== interspace: the space between spaces (Lex Rhodia commons) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/interspace -Isrc/ipfs -Isrc/robin_debanks -Isrc/update -Isrc/bridge -Isrc/onepolicy -Isrc/constellation -Isrc/surplus -Isrc/trispace src/interspace/test_interspace.c src/interspace/interspace.c src/interspace/lex_rhodia.c src/interspace/flagstate.c src/interspace/federation.c src/interspace/minister.c src/ipfs/ipfs.c src/robin_debanks/sha256.c src/onepolicy/onepolicy.c src/constellation/constellation_coordinator.c -o /tmp/test_interspace -lm && /tmp/test_interspace && echo "[PASS] test_interspace" || { echo "[FAIL] test_interspace"; exit 1; }
	@echo "=== broker: Mister Shanghai's info-product marketplace (capability-gated) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/broker -Isrc/ipfs -Isrc/robin_debanks -Isrc/update -Isrc/bridge -Isrc/tls -Isrc/surplus src/broker/test_broker.c src/broker/broker.c src/ipfs/ipfs.c src/robin_debanks/sha256.c src/bridge/bridge.c src/tls/hkdf.c src/robin_debanks/ed25519_verify.c -o /tmp/test_broker -lm && /tmp/test_broker && echo "[PASS] test_broker" || { echo "[FAIL] test_broker"; exit 1; }
	@echo "=== battering_ram: cross-capital exchange (no margin, no minting) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/battering_ram -Isrc/zcapital -Isrc/vino_stores -Isrc/finance -Isrc/vino -Isrc/surplus -Isrc/onepolicy -Isrc/edp_risk -Isrc/predictive -Isrc/robin_debanks src/battering_ram/test_battering_ram.c src/battering_ram/battering_ram.c src/zcapital/zcapital.c src/onepolicy/onepolicy.c src/surplus/surplus.c src/edp_risk/edp_risk.c src/predictive/predictive_model.c src/finance/financial.c src/finance/triple_ledger.c src/vino_stores/vino_stores.c src/robin_debanks/ed25519_verify.c -o /tmp/test_battering_ram -lm && /tmp/test_battering_ram && echo "[PASS] test_battering_ram" || { echo "[FAIL] test_battering_ram"; exit 1; }
	@echo "=== finance_markets: equities/futures/commodities tracker ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/finance_markets -Isrc/finance -Isrc/vino -Isrc/surplus -Isrc/edp_risk -Isrc/predictive src/finance_markets/test_finance_markets.c src/finance_markets/finance_markets.c src/finance/financial.c src/finance/triple_ledger.c src/surplus/surplus.c src/edp_risk/edp_risk.c src/predictive/predictive_model.c -o /tmp/test_finance_markets -lm && /tmp/test_finance_markets && echo "[PASS] test_finance_markets" || { echo "[FAIL] test_finance_markets"; exit 1; }
	@echo "=== iso20022: PACS.008 / CAMT.053 banking interop ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/iso20022 -Isrc/surplus src/iso20022/test_iso20022.c src/iso20022/iso20022.c -o /tmp/test_iso20022 -lm && /tmp/test_iso20022 && echo "[PASS] test_iso20022" || { echo "[FAIL] test_iso20022"; exit 1; }
	@echo "=== alloc: sustainable allocation + kill-switch-free token (host) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/alloc -Isrc/zcapital -Isrc/onepolicy -Isrc/surplus -Isrc/ipfs -Isrc/robin_debanks src/alloc/test_alloc.c src/alloc/alloc.c src/zcapital/zcapital.c src/onepolicy/onepolicy.c src/robin_debanks/sha256.c -o /tmp/test_alloc -lm && /tmp/test_alloc && echo "[PASS] test_alloc" || { echo "[FAIL] test_alloc"; exit 1; }
	@echo "=== alloc: proportional split on the Q32.32 TARGET type (no TEST_HOST) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/alloc -Isrc/zcapital -Isrc/onepolicy -Isrc/surplus -Isrc/ipfs -Isrc/robin_debanks src/alloc/test_alloc_q32.c src/alloc/alloc.c src/zcapital/zcapital.c src/onepolicy/onepolicy.c src/surplus/surplus.c src/robin_debanks/sha256.c -o /tmp/test_alloc_q32 -lm && /tmp/test_alloc_q32 && echo "[PASS] test_alloc_q32" || { echo "[FAIL] test_alloc_q32"; exit 1; }
	@echo "=== pirate_fleet: JDR crew workspace + DAO + compute-for-Vino ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/pirate_fleet -Isrc/concord -Isrc/chiglet -Isrc/surplus -Isrc/vino_stores -Isrc/finance -Isrc/vino -Isrc/onepolicy -Isrc/edp_risk -Isrc/robin_debanks -Isrc/cards src/pirate_fleet/test_pirate_fleet.c src/pirate_fleet/pirate_fleet.c src/concord/concord.c src/chiglet/chiglet.c src/surplus/surplus.c src/vino_stores/vino_stores.c src/finance/triple_ledger.c src/onepolicy/onepolicy.c src/edp_risk/edp_risk.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c -o /tmp/test_pirate_fleet -lm && /tmp/test_pirate_fleet && echo "[PASS] test_pirate_fleet" || { echo "[FAIL] test_pirate_fleet"; exit 1; }
	@echo "=== social: Don Tovani / Zevion free forum (no bans, no ads) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/social_spaces -Isrc/concord -Isrc/surplus -Isrc/chiglet src/social_spaces/test_social.c src/social_spaces/social.c src/concord/concord.c src/surplus/surplus.c src/chiglet/chiglet.c -o /tmp/test_social -lm && /tmp/test_social && echo "[PASS] test_social" || { echo "[FAIL] test_social"; exit 1; }
	@echo "=== reputation: the Pig Badge (over 9000) + earnable badges ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/reputation -Isrc/identity -Isrc/icon -Isrc/theme -Isrc/concord -Isrc/surplus -Isrc/edp_risk src/reputation/test_reputation.c src/reputation/reputation.c -o /tmp/test_reputation -lm && /tmp/test_reputation && echo "[PASS] test_reputation" || { echo "[FAIL] test_reputation"; exit 1; }
	@echo "=== test_art_studio ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/art_studio -Isrc/theme -Isrc/icon -Isrc/cards -Isrc/font -Isrc/ipfs -Isrc/robin_debanks -Isrc/update -Isrc/bridge -Isrc/surplus src/art_studio/test_art_studio.c src/art_studio/art_studio.c src/theme/theme.c src/cards/sigil.c src/ipfs/ipfs.c src/robin_debanks/sha256.c src/bridge/bridge.c -o /tmp/test_art_studio -lm && /tmp/test_art_studio && echo "[PASS] test_art_studio" || { echo "[FAIL] test_art_studio"; exit 1; }
	@echo "=== test_games ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/games -Isrc/chiglet -Isrc/ipfs -Isrc/robin_debanks -Isrc/update -Isrc/bridge -Isrc/surplus -Isrc/cards -Isrc/edp_risk src/games/test_games.c src/games/games.c src/chiglet/chiglet.c src/surplus/surplus.c src/ipfs/ipfs.c src/bridge/bridge.c src/robin_debanks/sha256.c -o /tmp/test_games -lm && /tmp/test_games && echo "[PASS] test_games" || { echo "[FAIL] test_games"; exit 1; }
	@echo "=== test_voice ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/voice -Isrc/audio -Isrc/chiglet -Isrc/surplus -Isrc/edp_risk -Isrc/cards src/voice/test_voice.c src/voice/voice.c src/chiglet/chiglet.c src/surplus/surplus.c -o /tmp/test_voice -lm && /tmp/test_voice && echo "[PASS] test_voice" || { echo "[FAIL] test_voice"; exit 1; }
	@echo "=== test_subterm ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/subterm src/subterm/test_subterm.c src/subterm/subterm.c -o /tmp/test_subterm -lm && /tmp/test_subterm && echo "[PASS] test_subterm" || { echo "[FAIL] test_subterm"; exit 1; }
	@echo "=== test_orbital_compat ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/orbital_compat -Isrc/lightningrod -Isrc/rational -Isrc/surplus -Isrc/wyverneye src/orbital_compat/test_orbital_compat.c src/orbital_compat/orbital_compat.c src/lightningrod/lightningrod.c src/rational/rational.c -o /tmp/test_orbital_compat -lm && /tmp/test_orbital_compat && echo "[PASS] test_orbital_compat" || { echo "[FAIL] test_orbital_compat"; exit 1; }
	@echo "=== test_fusion ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/fusion -Isrc/trispace -Isrc/robin_debanks src/fusion/test_fusion.c src/fusion/fusion.c src/trispace/trispace.c src/robin_debanks/sha256.c -o /tmp/test_fusion -lm && /tmp/test_fusion && echo "[PASS] test_fusion" || { echo "[FAIL] test_fusion"; exit 1; }
	@echo "=== test_sovereign_node ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/sovereign_node -Isrc/interspace -Isrc/ipfs -Isrc/robin_debanks -Isrc/onepolicy -Isrc/constellation -Isrc/surplus -Isrc/trispace -Isrc/update -Isrc/bridge src/sovereign_node/test_sovereign_node.c src/sovereign_node/sovereign_node.c src/interspace/federation.c src/ipfs/ipfs.c src/robin_debanks/sha256.c -o /tmp/test_sovereign_node -lm && /tmp/test_sovereign_node && echo "[PASS] test_sovereign_node" || { echo "[FAIL] test_sovereign_node"; exit 1; }
	@echo "=== logistics: syndicates + escrow + secondary contracts ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/logistics -Isrc/onepolicy -Isrc/zcapital -Isrc/reputation -Isrc/vino_stores -Isrc/finance -Isrc/vino -Isrc/ipfs -Isrc/robin_debanks -Isrc/identity -Isrc/icon -Isrc/theme -Isrc/concord -Isrc/surplus -Isrc/edp_risk src/logistics/test_logistics.c src/logistics/logistics.c src/onepolicy/onepolicy.c src/zcapital/zcapital.c src/reputation/reputation.c src/robin_debanks/ed25519_verify.c -o /tmp/test_logistics -lm && /tmp/test_logistics && echo "[PASS] test_logistics" || { echo "[FAIL] test_logistics"; exit 1; }
	@echo "=== pirate_apps: thin honest UIs over the whole deck ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/pirate_apps -Isrc/ipfs -Isrc/robin_debanks -Isrc/bridge -Isrc/update -Isrc/broker -Isrc/tls -Isrc/vino_stores -Isrc/finance -Isrc/vino -Isrc/onepolicy -Isrc/edp_risk -Isrc/finance_markets -Isrc/predictive -Isrc/surplus -Isrc/social_spaces -Isrc/concord -Isrc/chiglet -Isrc/appkit -Isrc/icon -Isrc/theme -Isrc/cards src/pirate_apps/test_pirate_apps.c src/pirate_apps/pirate_apps.c src/ipfs/ipfs.c src/robin_debanks/sha256.c src/bridge/bridge.c src/broker/broker.c src/tls/hkdf.c src/robin_debanks/ed25519_verify.c src/vino_stores/vino_stores.c src/finance/triple_ledger.c src/onepolicy/onepolicy.c src/edp_risk/edp_risk.c src/finance_markets/finance_markets.c src/finance/financial.c src/predictive/predictive_model.c src/surplus/surplus.c src/social_spaces/social.c src/concord/concord.c src/chiglet/chiglet.c -o /tmp/test_pirate_apps -lm && /tmp/test_pirate_apps && echo "[PASS] test_pirate_apps" || { echo "[FAIL] test_pirate_apps"; exit 1; }
	@echo "=== platform license: four-instrument share-alike stack ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Iinclude -Isrc/license src/license/test_license.c src/license/license.c -o /tmp/test_license && /tmp/test_license && echo "[PASS] test_license" || { echo "[FAIL] test_license"; exit 1; }
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
	echo "=== ZSP v2 enforced verification (version/arch/key-id + anti-rollback) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/loader -Isrc/robin_debanks \
		src/loader/test_zsp2.c src/loader/zsp.c \
		src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c \
		-o /tmp/test_zsp2 -lm && \
		/tmp/test_zsp2 && echo "[PASS] test_zsp2" || { echo "[FAIL] test_zsp2"; exit 1; }; \
	echo "=== Installer security (P0-2: path confinement + manifest auth) ==="; \
	if [ -f ../install/install_zxv.py ]; then \
		python3 ../install/install_zxv.py --selftest \
			&& echo "[PASS] install_security" || { echo "[FAIL] install_security"; exit 1; }; \
	else echo "[SKIP] install_security (installer not present)"; fi; \
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
	echo "=== Immigration daemon visas (Ed25519-verified) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/count_house -Isrc/porter_house -Isrc/edp_risk -Isrc/telemetry -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/vino -Isrc/identity -Isrc/finance -Isrc/license -Isrc/net -Isrc/predictive -Isrc/situation -Isrc/axiom_matrix -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/dual_space -Isrc/lattice -Isrc/immigration -Isrc/ai_layer -Isrc/mesh_net \
		src/immigration/test_immigration.c src/immigration/immigration.c src/count_house/count_house.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_immigration -lm && \
		/tmp/test_immigration && echo "[PASS] test_immigration" || { echo "[FAIL] test_immigration"; exit 1; }; \
	echo "=== AI layer signed model registry (Ed25519-verified) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/count_house -Isrc/porter_house -Isrc/edp_risk -Isrc/telemetry -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/vino -Isrc/identity -Isrc/finance -Isrc/license -Isrc/net -Isrc/predictive -Isrc/situation -Isrc/axiom_matrix -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/dual_space -Isrc/lattice -Isrc/immigration -Isrc/ai_layer -Isrc/mesh_net \
		src/ai_layer/test_ai_layer.c src/ai_layer/ai_layer.c src/count_house/count_house.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c \
		-o /tmp/test_ai_layer -lm && \
		/tmp/test_ai_layer && echo "[PASS] test_ai_layer" || { echo "[FAIL] test_ai_layer"; exit 1; }; \
	echo "=== Holodeck swarm: watching together makes it faster ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/holodeck -Isrc/chiglet -Isrc/surplus -Isrc/robin_debanks \
		src/holodeck/test_swarm.c src/holodeck/swarm.c src/chiglet/chiglet.c src/surplus/surplus.c src/robin_debanks/sha256.c \
		-o /tmp/test_swarm -lm && \
		/tmp/test_swarm && echo "[PASS] test_swarm" || { echo "[FAIL] test_swarm"; exit 1; }; \
	echo "=== Community Chest (Ed25519-verified app registry) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc -Isrc/event_space -Isrc/robin_debanks -Isrc/surplus -Isrc/rational -Isrc/crit168 -Isrc/count_house -Isrc/porter_house -Isrc/edp_risk -Isrc/telemetry -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/vino -Isrc/identity -Isrc/finance -Isrc/license -Isrc/net -Isrc/predictive -Isrc/situation -Isrc/axiom_matrix -Isrc/iphase -Isrc/choice -Isrc/phase_coord -Isrc/dual_space -Isrc/lattice -Isrc/community_chest -Isrc/sched -Isrc/mm \
		src/community_chest/test_community_chest.c src/community_chest/community_chest.c src/vino/vino.c src/count_house/count_house.c src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/robin_debanks/crypto_verify.c src/edp_risk/edp_risk.c src/surplus/surplus.c src/porter_house/porter_house.c src/lpres/lpres.c src/lpres/lpres_core.c src/rmag/rmag_core.c src/rmag/rmag.c src/oseq/oseq_core.c \
		-o /tmp/test_community_chest -lm && \
		/tmp/test_community_chest && echo "[PASS] test_community_chest" || { echo "[FAIL] test_community_chest"; exit 1; }; \
	echo "=== Holodeck viewing governed by its den (swarm+denconnect) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/holodeck -Isrc/denconnect -Isrc/chiglet -Isrc/surplus -Isrc/robin_debanks \
		src/holodeck/test_viewing.c src/holodeck/viewing.c src/holodeck/swarm.c \
		src/denconnect/denconnect.c src/chiglet/chiglet.c src/surplus/surplus.c src/robin_debanks/sha256.c \
		-o /tmp/test_viewing -lm && \
		/tmp/test_viewing && echo "[PASS] test_viewing" || { echo "[FAIL] test_viewing"; exit 1; }; \
	echo "=== DHCP client: RFC 2131 wire format (was an empty stub) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/net \
		src/net/test_dhcp.c src/net/dhcp.c \
		-o /tmp/test_dhcp -lm && \
		/tmp/test_dhcp && echo "[PASS] test_dhcp" || { echo "[FAIL] test_dhcp"; exit 1; }; \
	echo "=== DNS resolver: RFC 1035 + pointer-loop/OOB attacks (was an empty stub) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/net \
		src/net/test_dns.c src/net/dns.c \
		-o /tmp/test_dns -lm && \
		/tmp/test_dns && echo "[PASS] test_dns" || { echo "[FAIL] test_dns"; exit 1; }; \
	echo "=== driver subsystems: wifi / bluetooth / video / epu (hardened) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/wifi -Isrc/edp_risk -Isrc/surplus \
		src/wifi/test_wifi.c src/wifi/wifi.c -o /tmp/test_wifi -lm && \
		/tmp/test_wifi && echo "[PASS] test_wifi" || { echo "[FAIL] test_wifi"; exit 1; }; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/bluetooth -Isrc/edp_risk -Isrc/surplus \
		src/bluetooth/test_bluetooth.c src/bluetooth/bluetooth.c src/surplus/surplus.c -o /tmp/test_bluetooth -lm && \
		/tmp/test_bluetooth && echo "[PASS] test_bluetooth" || { echo "[FAIL] test_bluetooth"; exit 1; }; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/video -Isrc/edp_risk -Isrc/surplus \
		src/video/test_video.c src/video/video.c -o /tmp/test_video -lm && \
		/tmp/test_video && echo "[PASS] test_video" || { echo "[FAIL] test_video"; exit 1; }; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/epu -Isrc/edp_risk -Isrc/surplus \
		src/epu/test_epu_device.c src/epu/epu_device.c src/surplus/surplus.c -o /tmp/test_epu -lm && \
		/tmp/test_epu && echo "[PASS] test_epu" || { echo "[FAIL] test_epu"; exit 1; }; \
	echo "=== theme + icons: tokenized palette, per-system vector icons ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/icon -Isrc/theme \
		src/icon/test_icon.c src/icon/icon.c src/theme/theme.c \
		-o /tmp/test_icon -lm && \
		/tmp/test_icon && echo "[PASS] test_icon" || { echo "[FAIL] test_icon"; exit 1; }; \
	echo "=== multi-language text: UTF-8 + script itemization + direction ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/font \
		src/font/test_script.c src/font/script.c \
		-o /tmp/test_script -lm && \
		/tmp/test_script && echo "[PASS] test_script" || { echo "[FAIL] test_script"; exit 1; }; \
	echo "=== Web2/Web3/Web4 bridge: one resolver across three realms ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/bridge \
		src/bridge/test_bridge.c src/bridge/bridge.c \
		-o /tmp/test_bridge && \
		/tmp/test_bridge && echo "[PASS] test_bridge" || { echo "[FAIL] test_bridge"; exit 1; }; \
	echo "=== TrueType rasteriser + full font stack (registry -> glyph pixels) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/font \
		src/font/test_truetype.c src/font/truetype.c src/font/font.c src/font/script.c \
		-o /tmp/test_truetype -lm && \
		/tmp/test_truetype && echo "[PASS] test_truetype" || { echo "[FAIL] test_truetype"; exit 1; }; \
	echo "=== font registry: script->face resolution + glyph boundary ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/font \
		src/font/test_font.c src/font/font.c src/font/script.c src/font/truetype.c \
		-o /tmp/test_font -lm && \
		/tmp/test_font && echo "[PASS] test_font" || { echo "[FAIL] test_font"; exit 1; }; \
	echo "=== zxpkg: native Tri-Space package (.zxvc/.cedez/.cedec) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/zxpkg -Isrc/trispace -Isrc/robin_debanks -Isrc/loader \
		src/zxpkg/test_zxpkg.c src/zxpkg/zxpkg.c src/trispace/trispace.c src/robin_debanks/sha256.c \
		src/robin_debanks/ed25519_verify.c src/loader/zsp.c \
		-o /tmp/test_zxpkg -lm && \
		/tmp/test_zxpkg && echo "[PASS] test_zxpkg" || { echo "[FAIL] test_zxpkg"; exit 1; }; \
	echo "=== decentralized opt-in content-addressed update system ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/update -Isrc/robin_debanks \
		src/update/test_update.c src/update/update.c src/robin_debanks/sha256.c \
		-o /tmp/test_update -lm && \
		/tmp/test_update && echo "[PASS] test_update" || { echo "[FAIL] test_update"; exit 1; }; \
	echo "=== deployment profiles: laptop -> supercomputer, one OS ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/deploy \
		src/deploy/test_deploy.c src/deploy/deploy.c \
		-o /tmp/test_deploy -lm && \
		/tmp/test_deploy && echo "[PASS] test_deploy" || { echo "[FAIL] test_deploy"; exit 1; }; \
	echo "=== signed native release: Tri-Space triad + Ed25519 provenance (ZSP) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/zxpkg -Isrc/trispace -Isrc/robin_debanks -Isrc/loader \
		src/zxpkg/test_zxrelease.c src/zxpkg/zxpkg.c src/trispace/trispace.c \
		src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c src/loader/zsp.c \
		-o /tmp/test_zxrelease -lm && \
		/tmp/test_zxrelease && echo "[PASS] test_zxrelease" || { echo "[FAIL] test_zxrelease"; exit 1; }; \
	echo "=== The Reality Engine: sigil circuits computing against live variables ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/reality -Isrc/cards \
		src/reality/test_reality.c src/reality/reality.c src/cards/sigil.c \
		-o /tmp/test_reality -lm && \
		/tmp/test_reality && echo "[PASS] test_reality" || { echo "[FAIL] test_reality"; exit 1; }; \
	echo "=== The Mage's Hats: security authorization boundary (hat suites) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/mage -Isrc/robin_debanks \
		src/mage/test_mage.c src/mage/mage.c src/robin_debanks/sha256.c \
		-o /tmp/test_mage -lm && \
		/tmp/test_mage && echo "[PASS] test_mage" || { echo "[FAIL] test_mage"; exit 1; }; \
	echo "=== fs_snprintf: kernel-wide bounded formatter (4 overflow fixes) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Wno-macro-redefined -DTEST_HOST -Iinclude \
		src/legal_engine/test_fs_snprintf.c -o /tmp/test_fs_snprintf -lm && \
		/tmp/test_fs_snprintf && echo "[PASS] test_fs_snprintf" || { echo "[FAIL] test_fs_snprintf"; exit 1; }; \
	echo "=== legal_auto_respond: no unearned claims, no overflow ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Wno-macro-redefined -DTEST_HOST -Iinclude -Isrc/legal_engine \
		src/legal_engine/test_legal_response.c src/legal_engine/legal_engine_ext.c \
		src/legal_engine/legal_engine.c -o /tmp/test_legal_response -lm && \
		/tmp/test_legal_response && echo "[PASS] test_legal_response" || { echo "[FAIL] test_legal_response"; exit 1; }; \
	echo "=== HMAC full-message coverage (regression: 256-byte auth bypass) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/robin_debanks \
		src/robin_debanks/test_hmac_fullcover.c src/robin_debanks/crypto_verify.c \
		src/robin_debanks/sha256.c \
		-o /tmp/test_hmac_fullcover -lm && \
		/tmp/test_hmac_fullcover && echo "[PASS] test_hmac_fullcover" || { echo "[FAIL] test_hmac_fullcover"; exit 1; }; \
	echo "=== TLS 1.3 key schedule: RFC 4231 / 5869 / 8448 published vectors ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/tls -Isrc/robin_debanks \
		src/tls/test_hkdf.c src/tls/hkdf.c src/robin_debanks/sha256.c \
		-o /tmp/test_hkdf -lm && \
		/tmp/test_hkdf && echo "[PASS] test_hkdf" || { echo "[FAIL] test_hkdf"; exit 1; }; \
	echo "=== TLS 1.3 record layer: AAD-is-the-header, inner type, seq/nonce ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/tls \
		src/tls/test_record.c src/tls/record.c src/tls/aead.c \
		-o /tmp/test_record -lm && \
		/tmp/test_record && echo "[PASS] test_record" || { echo "[FAIL] test_record"; exit 1; }; \
	echo "=== ChaCha20-Poly1305 AEAD: RFC 8439 vectors + tamper rejection ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/tls \
		src/tls/test_aead.c src/tls/aead.c \
		-o /tmp/test_aead -lm && \
		/tmp/test_aead && echo "[PASS] test_aead" || { echo "[FAIL] test_aead"; exit 1; }; \
	echo "=== X25519: RFC 7748 vectors incl. the 1000-round iterated test ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/tls \
		src/tls/test_x25519.c src/tls/x25519.c \
		-o /tmp/test_x25519 -lm && \
		/tmp/test_x25519 && echo "[PASS] test_x25519" || { echo "[FAIL] test_x25519"; exit 1; }; \
	echo "=== TCP endpoint: handshake, retransmit, seq wrap, hostile segments ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Isrc/net \
		src/net/test_tcp.c src/net/tcp.c \
		-o /tmp/test_tcp -lm && \
		/tmp/test_tcp && echo "[PASS] test_tcp" || { echo "[FAIL] test_tcp"; exit 1; }; \
	echo "=== TCP/IP stack: RFC-1071 checksums, ARP, ICMP, DHCP, DNS, TCP sockets ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/net -Isrc/crit168 -Isrc/event_space -Isrc/surplus -Isrc/iphase \
		src/net/test_netstack.c src/net/net.c src/net/dhcp.c src/net/dns.c src/net/tcp.c src/net/m5route.c src/iphase/iphase_core.c src/iphase/iphase.c \
		-o /tmp/test_netstack -lm && \
		/tmp/test_netstack && echo "[PASS] test_netstack" || { echo "[FAIL] test_netstack"; exit 1; }; \
	echo "=== Browser: RFC 3986 URLs, HTTP/1.1 framing, hostile HTML, layout ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/browser -Isrc/surplus -Isrc/edp_risk \
		src/browser/test_browser.c src/browser/browser.c src/surplus/surplus.c src/edp_risk/edp_risk.c \
		-o /tmp/test_browser -lm && \
		/tmp/test_browser > /dev/null && echo "[PASS] test_browser" || { echo "[FAIL] test_browser"; exit 1; }; \
	echo "=== Event clock: logical ordinals, measured drift, saturating ns math ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/clock -Isrc/edp_risk -Isrc/surplus \
		src/clock/test_event_clock.c src/clock/event_clock.c \
		-o /tmp/test_event_clock -lm && \
		/tmp/test_event_clock && echo "[PASS] test_event_clock" || { echo "[FAIL] test_event_clock"; exit 1; }; \
	echo "=== Audio mixer: exact sample values, clipping, wild ring cursors ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/audio -Isrc/edp_risk -Isrc/surplus -Isrc/crit168 \
		src/audio/test_audio.c src/audio/audio.c src/edp_risk/edp_risk.c src/surplus/surplus.c \
		-o /tmp/test_audio -lm && \
		/tmp/test_audio > /dev/null && echo "[PASS] test_audio" || { echo "[FAIL] test_audio"; exit 1; }; \
	echo "=== Audio mixer under ASan+UBSan (malformed/truncated input) ==="; \
	cc -std=c11 -Wall -Werror -Wextra -O1 -g -fsanitize=address,undefined -DTEST_HOST \
		-Iinclude -Isrc/audio -Isrc/edp_risk -Isrc/surplus -Isrc/crit168 \
		src/audio/test_audio.c src/audio/audio.c src/edp_risk/edp_risk.c src/surplus/surplus.c \
		-o /tmp/test_audio_san -lm && \
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /tmp/test_audio_san > /dev/null && \
		echo "[PASS] test_audio_san" || { echo "[FAIL] test_audio_san"; exit 1; }; \
	echo "=== ZAB capability verifier: capabilities DERIVED from bytecode, not declared ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/zab src/zab/test_zab.c src/zab/zab.c \
		-o /tmp/test_zab && /tmp/test_zab > /tmp/test_zab.log 2>&1 && \
		echo "[PASS] test_zab ($$(grep -c '\[PASS\]' /tmp/test_zab.log) checks)" || \
		{ cat /tmp/test_zab.log; echo "[FAIL] test_zab"; exit 1; }; \
	echo "=== ZAB VM: an effect happens only if granted AND the host provides it ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/zab src/zab/test_zab_exec.c \
		src/zab/zab_exec.c src/zab/zab.c -o /tmp/test_zab_exec && \
		/tmp/test_zab_exec > /tmp/test_zab_exec.log 2>&1 && \
		echo "[PASS] test_zab_exec ($$(grep -c '\[PASS\]' /tmp/test_zab_exec.log) checks)" || \
		{ cat /tmp/test_zab_exec.log; echo "[FAIL] test_zab_exec"; exit 1; }; \
	echo "=== ZXI inverse witness: a proven inverse must APPLY, not merely claim ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/invproof src/invproof/test_invproof.c \
		src/invproof/invproof.c src/robin_debanks/sha256.c -o /tmp/test_invproof && \
		/tmp/test_invproof > /tmp/test_invproof.log 2>&1 && \
		echo "[PASS] test_invproof ($$(grep -c '\[PASS\]' /tmp/test_invproof.log) checks)" || \
		{ cat /tmp/test_invproof.log; echo "[FAIL] test_invproof"; exit 1; }; \
	echo "=== Tri-Space native storage: triad atomicity under exhaustive crash injection ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Iinclude -Isrc/zxvfs -Isrc/fractal \
		src/zxvfs/test_zxvfs_tri.c src/zxvfs/zxvfs_tri.c src/zxvfs/zxvfs.c \
		src/trispace/trispace.c src/robin_debanks/sha256.c src/zab/zab.c src/zab/zab_exec.c src/invproof/invproof.c src/fractal/zorder.c -o /tmp/test_zxvfs_tri && \
		/tmp/test_zxvfs_tri > /tmp/test_zxvfs_tri.log 2>&1 && \
		echo "[PASS] test_zxvfs_tri ($$(grep -c '\[PASS\]' /tmp/test_zxvfs_tri.log) checks)" || \
		{ cat /tmp/test_zxvfs_tri.log; echo "[FAIL] test_zxvfs_tri"; exit 1; }; \
	echo "=== Display: negotiate a mode (incl 4K/8K), compose NATIVELY, scale the UI ==="; \
	for cfg in "" "-DZXV_DISPLAY_MAX_W=3840 -DZXV_DISPLAY_MAX_H=2160" "-DZXV_DISPLAY_MAX_W=7680 -DZXV_DISPLAY_MAX_H=4320"; do \
	  gcc -std=c11 -Wall -Werror -Wextra $$cfg -Isrc/display src/display/test_display.c \
	    src/display/display.c -o /tmp/test_disp && /tmp/test_disp > /tmp/test_disp.log 2>&1 || \
	    { cat /tmp/test_disp.log; echo "[FAIL] test_display"; exit 1; }; \
	done; \
	echo "[PASS] test_display ($$(grep -c '\[PASS\]' /tmp/test_disp.log) checks x3 budgets: QHD/4K/8K)"; \
	echo "=== Bomb squad: a burning fault is caught and CORRECTED before it detonates ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/bombsquad src/bombsquad/test_bombsquad.c \
		src/bombsquad/bombsquad.c -o /tmp/test_bs && \
		/tmp/test_bs > /tmp/test_bs.log 2>&1 && \
		echo "[PASS] test_bombsquad ($$(grep -c '\[PASS\]' /tmp/test_bs.log) checks)" || \
		{ cat /tmp/test_bs.log; echo "[FAIL] test_bombsquad"; exit 1; }; \
	echo "=== Tri-Space media codec: lossy S+ plus S- restores the original EXACTLY ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/codec src/codec/test_zmedia.c \
		src/codec/zmedia.c src/invproof/invproof.c src/robin_debanks/sha256.c \
		-o /tmp/test_zm && /tmp/test_zm > /tmp/test_zm.log 2>&1 && \
		echo "[PASS] test_zmedia ($$(grep -c '\[PASS\]' /tmp/test_zm.log) checks)" || \
		{ cat /tmp/test_zm.log; echo "[FAIL] test_zmedia"; exit 1; }; \
	echo "=== MegaROM: observed dynamics -> Sutra schemas (no title content carried) ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Isrc/megarom src/megarom/test_mrschema.c \
		src/megarom/mrschema.c -o /tmp/test_mrs && \
		/tmp/test_mrs > /tmp/test_mrs.log 2>&1 && \
		echo "[PASS] test_mrschema ($$(grep -c '\[PASS\]' /tmp/test_mrs.log) checks)" || \
		{ cat /tmp/test_mrs.log; echo "[FAIL] test_mrschema"; exit 1; }; \
	echo "=== ZXVFS crash fuzzer: fsck oracle over randomized workloads ==="; \
	gcc -std=c11 -Wall -Werror -Wextra -Iinclude -Isrc/zxvfs -Isrc/fractal src/zxvfs/test_zxvfs_fuzz.c \
		src/zxvfs/zxvfs_tri.c src/zxvfs/zxvfs.c src/trispace/trispace.c \
		src/robin_debanks/sha256.c src/zab/zab.c src/zab/zab_exec.c \
		src/invproof/invproof.c src/fractal/zorder.c -o /tmp/test_zxvfs_fuzz && \
		/tmp/test_zxvfs_fuzz $(FUZZ_FS_ITERS) > /tmp/test_zxvfs_fuzz.log 2>&1 && \
		echo "[PASS] test_zxvfs_fuzz ($$(grep -c '\[PASS\]' /tmp/test_zxvfs_fuzz.log) checks, $(FUZZ_FS_ITERS) iters)" || \
		{ cat /tmp/test_zxvfs_fuzz.log; echo "[FAIL] test_zxvfs_fuzz"; exit 1; }; \
	echo "=== Release signing: offline-root chain refuses tamper/wrong-key/unsigned ==="; \
	bash ../build_system/test_release_signing.sh > /tmp/test_relsign.log 2>&1 && \
		echo "[PASS] test_release_signing ($$(grep -c '\[PASS\]' /tmp/test_relsign.log) checks)" || \
		{ cat /tmp/test_relsign.log; echo "[FAIL] test_release_signing"; exit 1; }; \
	echo "=== Installer security: path confinement + manifest auth + key pinning ==="; \
	python3 ../install/install_zxv.py --selftest > /tmp/test_installer.log 2>&1 && \
		echo "[PASS] installer_selftest ($$(grep -c '\[PASS\]' /tmp/test_installer.log) checks)" || \
		{ cat /tmp/test_installer.log; echo "[FAIL] installer_selftest"; exit 1; }; \
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
	echo "=== event_clock under ASan+UBSan (200k randomised API sequences) ==="; \
	cc $(SAN_FLAGS) -fno-sanitize-recover=all -std=c11 -DTEST_HOST \
		-Iinclude -Isrc/clock -Isrc/edp_risk -Isrc/surplus \
		src/clock/test_event_clock.c src/clock/event_clock.c \
		-o /tmp/san_event_clock -lm && /tmp/san_event_clock > /dev/null \
		|| { echo "[FAIL] san_event_clock"; exit 1; }; \
	echo "=== browser under ASan+UBSan (URL/HTTP/HTML parsers, exact-size heap) ==="; \
	cc $(SAN_FLAGS) -fno-sanitize-recover=all -std=c11 -DTEST_HOST \
		-Iinclude -Isrc/browser -Isrc/surplus -Isrc/edp_risk \
		src/browser/test_browser.c src/browser/browser.c src/surplus/surplus.c \
		src/edp_risk/edp_risk.c \
		-o /tmp/san_browser -lm && /tmp/san_browser > /dev/null \
		|| { echo "[FAIL] san_browser"; exit 1; }; \
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
