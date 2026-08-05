# VOVINA_SHAKINA — Top-Level Makefile
# Builds and tests all OS layers on top of the kernel.
# Author: H.M. Michael-Laurence: Curzi (c)

CC = gcc
CFLAGS_TEST = -g -Wall -Wextra -O2 -std=c11 -DTEST_HOST \
              -I kernel/include \
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

.PHONY: all test clean kernel

all: test

kernel:
	$(MAKE) -C kernel

test: $(TESTS)
	@echo ""
	@echo "=== Running all OS layer tests ==="
	@for t in $(TESTS); do \
		echo "--- Running $$t ---"; \
		./$$t && echo "PASS: $$t" || echo "FAIL: $$t"; \
	done
	@echo ""
	@echo "=== All tests complete ==="

test_lattice: tests/test_lattice.c $(OS_LATTICE_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(OS_LATTICE_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_neon: tests/test_neon.c $(NEON_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(NEON_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_gridchain: tests/test_gridchain.c $(GRIDCHAIN_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(GRIDCHAIN_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_security: tests/test_security.c $(SECURITY_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(SECURITY_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_physics: tests/test_physics.c $(PHYSICS_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(PHYSICS_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_hccs: tests/test_hccs.c $(HCCS_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(HCCS_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_audiogenomics: tests/test_audiogenomics.c $(AUDIOGENOMICS_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(AUDIOGENOMICS_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_governance: tests/test_governance.c $(GOVERNANCE_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(GOVERNANCE_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_integration: tests/test_integration.c $(ALL_OS_SRCS) $(KERNEL_SRCS)
	$(CC) $(CFLAGS_TEST) -o $@ $< $(ALL_OS_SRCS) $(KERNEL_SRCS) $(LDFLAGS_TEST)

test_drivers: tests/test_drivers.c kernel/src/keyboard/keyboard.c \
              kernel/src/pic/pic.c kernel/src/pci/pci.c \
              kernel/src/acpi/acpi.c init/init.c
	$(CC) $(CFLAGS_TEST) -o $@ tests/test_drivers.c \
              kernel/src/keyboard/keyboard.c kernel/src/pic/pic.c \
              kernel/src/pci/pci.c kernel/src/acpi/acpi.c init/init.c \
              $(LDFLAGS_TEST)

clean:
	rm -f $(TESTS)
