/* acpi.h — ACPI table parser
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stdbool.h>

#define ACPI_MAX_TABLES 16

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
    uint32_t local_apic_addr;
    bool has_madt;
    bool has_fadt;
    bool has_hpet;
} acpi_state_t;

void acpi_init(acpi_state_t *state);
acpi_header_t *acpi_find_table(acpi_state_t *state, const char *signature);
uint8_t acpi_checksum(const void *data, uint32_t length);

#endif
