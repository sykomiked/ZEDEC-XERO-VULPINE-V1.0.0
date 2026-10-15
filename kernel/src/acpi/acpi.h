/* acpi.h — ACPI table parser
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Two ways in:
 *   acpi_init            legacy BIOS scan of the EBDA and 0xE0000..0xFFFFF for
 *                        the RSDP, RSDT only, tables addressed below 4 GB;
 *   acpi_init_from_rsdp  the RSDP's physical address handed over by the boot
 *                        loader (UEFI configuration table), XSDT when the
 *                        RSDP is revision 2+, every table mapped through a
 *                        caller hook so tables above the identity map work.
 * The AML interpreter (aml.h) and the power-management layer (acpi_pm.h)
 * take their DSDT/SSDTs from the second path. */
#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stdbool.h>

#define ACPI_MAX_TABLES 64

typedef struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_header_t;

typedef struct acpi_rsdt {
    acpi_header_t header;
    uint32_t entries[];
} __attribute__((packed)) acpi_rsdt_t;

typedef struct acpi_madt {
    acpi_header_t header;
    uint32_t local_apic_addr;
    uint32_t flags;
    uint8_t  entries[];
} __attribute__((packed)) acpi_madt_t;

typedef struct acpi_state {
    uint32_t rsdp_address;
    acpi_header_t *tables[ACPI_MAX_TABLES];
    uint32_t num_tables;
    uint64_t table_phys[ACPI_MAX_TABLES]; /* acpi_init_from_rsdp only */
    uint64_t rsdp_phys, root_phys;        /* RSDP and the XSDT/RSDT it names */
    uint8_t rsdp_rev;                     /* 0 = ACPI 1.0 (RSDT), 2+ = XSDT */
    uint8_t bad_checksums;                /* tables listed but rejected */
    uint32_t local_apic_addr;
    bool has_madt;
    bool has_fadt;
    bool has_hpet;
} acpi_state_t;

void acpi_init(acpi_state_t *state);
/* Map hook: return a pointer to len bytes at phys, or 0. */
typedef const void *(*acpi_map_fn)(void *ctx, uint64_t phys, uint64_t len);
/* Validate the RSDP at rsdp_phys and collect every table its XSDT (or RSDT)
 * lists whose length and checksum are sound. Returns the table count, or a
 * negative value when the RSDP itself is unusable. */
int acpi_init_from_rsdp(acpi_state_t *state, uint64_t rsdp_phys, acpi_map_fn map, void *ctx);
/* Physical address of the RSDP found by the legacy BIOS scan, 0 if none. */
uint64_t acpi_find_rsdp_legacy(void);
acpi_header_t *acpi_find_table(acpi_state_t *state, const char *signature);
uint8_t acpi_checksum(const void *data, uint32_t length);

#endif
