# VOVINA_SHAKINA — Top-Level Makefile
# Builds and tests all OS layers on top of the kernel.
# Author: H.M. Michael-Laurence: Curzi (c)

CC = gcc
CFLAGS_TEST = -g -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -DTEST_HOST \
              -I kernel/include \
              -I kernel/src/modbind -I kernel/src/e8 -I kernel/src/event_space \
              -I kernel/src/surplus -I kernel/src/zab \
              -I kernel/src/oseq -I kernel/src/rmag -I kernel/src/lpres \
              -I kernel/src/iphase -I kernel/src/choice -I kernel/src/phase_coord \
              -I kernel/src/telemetry -I kernel/src/axiom_matrix \
              -I kernel/src/crit168 -I kernel/compat \
              -I kernel/src/gdt -I kernel/src/idt -I kernel/src/pic \
              -I kernel/src/timer -I kernel/src/keyboard -I kernel/src/mouse \
              -I kernel/src/pci -I kernel/src/acpi -I kernel/src/vbe \
              -I kernel/src/ata -I kernel/src/fat32 \
              -I kernel/src/vino -I kernel/src/vena \
              -I os_lattice -I neon -I gridchain -I security \
              -I physics_sim -I hccs -I audiogenomics -I governance \
              -I init -I gui
LDFLAGS_TEST = -lm

KERNEL_SRCS = kernel/src/oseq/oseq_core.c \
              kernel/src/rmag/rmag_core.c \
              kernel/src/lpres/lpres_core.c \
              kernel/src/iphase/iphase_core.c \
              kernel/src/choice/choice_core.c \
              kernel/src/phase_coord/phase_coordinator.c \
              kernel/src/telemetry/telemetry_core.c \
              kernel/src/axiom_matrix/axiom_matrix_core.c \
              kernel/src/crit168/crit_168_word.c \
              kernel/src/e8/e8.c kernel/src/e8/zphi.c \
              kernel/compat/compat_layer.c

OS_LATTICE_SRCS = os_lattice/lattice_core.c \
                  os_lattice/lattice_scheduler.c \
                  os_lattice/lattice_ipc.c

NEON_SRCS = neon/neon_core.c

GRIDCHAIN_SRCS = gridchain/gridchain_core.c

SECURITY_SRCS = security/security_core.c

PHYSICS_SRCS = physics_sim/physics_core.c

HCCS_SRCS = hccs/hccs_core.c

AUDIOGENOMICS_SRCS = audiogenomics/audiogenomics_core.c

GOVERNANCE_SRCS = governance/governance_core.c

ALL_OS_SRCS = $(OS_LATTICE_SRCS) $(NEON_SRCS) $(GRIDCHAIN_SRCS) \
              $(SECURITY_SRCS) $(PHYSICS_SRCS) $(HCCS_SRCS) \
              $(AUDIOGENOMICS_SRCS) $(GOVERNANCE_SRCS)

TESTS = test_lattice test_neon test_gridchain test_security \
        test_physics test_hccs test_audiogenomics test_governance \
        test_integration test_drivers

# test binaries go under build/ (git-ignored), not the repository root
TBIN ?= build/os_tests

.PHONY: all test clean kernel $(TESTS)

all: test

kernel:
	$(MAKE) -C kernel

test: $(TESTS)
	@echo ""
	@echo "=== Running all OS layer tests ==="
	@fail=0; for t in $(TESTS); do \
		echo "--- Running $$t ---"; \
		if $(TBIN)/$$t; then echo "PASS: $$t"; else echo "FAIL: $$t"; fail=$$((fail + 1)); fi; \
	done; \
	echo ""; \
	if [ $$fail -gt 0 ]; then echo "=== $$fail test(s) FAILED ==="; exit 1; fi; \
	echo "=== All tests complete ==="

test_lattice: tests/test_lattice.c $(OS_LATTICE_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(OS_LATTICE_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_neon: tests/test_neon.c $(NEON_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(NEON_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_gridchain: tests/test_gridchain.c $(GRIDCHAIN_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(GRIDCHAIN_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_security: tests/test_security.c $(SECURITY_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(SECURITY_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_physics: tests/test_physics.c $(PHYSICS_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(PHYSICS_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_hccs: tests/test_hccs.c $(HCCS_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(HCCS_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_audiogenomics: tests/test_audiogenomics.c $(AUDIOGENOMICS_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(AUDIOGENOMICS_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_governance: tests/test_governance.c $(GOVERNANCE_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(GOVERNANCE_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_integration: tests/test_integration.c $(ALL_OS_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ $< $(ALL_OS_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

DRIVER_SRCS = kernel/src/keyboard/keyboard.c kernel/src/pic/pic.c \
              kernel/src/pci/pci.c kernel/src/acpi/acpi.c \
              kernel/src/net/net.c kernel/src/net/tcp.c kernel/src/net/dhcp.c \
              kernel/src/net/dns.c kernel/src/net/m5route.c kernel/src/net/dtmf.c \
              kernel/src/net/radio.c kernel/src/vino/vino.c kernel/src/vena/vena.c \
              kernel/src/zab/zab.c kernel/src/zab/zab_exec.c init/init.c

test_drivers: tests/test_drivers.c $(DRIVER_SRCS) $(KERNEL_SRCS)
	@mkdir -p $(TBIN)
	$(CC) $(CFLAGS_TEST) -o $(TBIN)/$@ tests/test_drivers.c $(DRIVER_SRCS) $(KERNEL_SRCS) \
              $(LDFLAGS_TEST)

clean:
	rm -f $(addprefix $(TBIN)/,$(TESTS))
