/* acpi.c — ACPI table parser implementation
 * Scans EBDA and 0xE0000-0xFFFFF for RSDP, follows to RSDT.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "acpi.h"

#define RSDP_SIG "RSD PTR "

typedef struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) acpi_rsdp_t;

uint8_t acpi_checksum(const void *data, uint32_t length) {
    const uint8_t *p = (const uint8_t *)data;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++)
        sum += p[i];
    return sum;
}

static acpi_rsdp_t *find_rsdp(void) {
    uint32_t ebda_ptr = *(uint16_t *)0x40E;
    if (ebda_ptr) {
        uint32_t ebda = ebda_ptr << 4;
        for (uint32_t addr = ebda; addr < ebda + 1024; addr += 16) {
            acpi_rsdp_t *rsdp = (acpi_rsdp_t *)addr;
            char sig[9];
            for (int i = 0; i < 8; i++) sig[i] = rsdp->signature[i];
            sig[8] = 0;
            if (sig[0] == 'R' && sig[1] == 'S' && sig[2] == 'D' &&
                sig[3] == ' ' && sig[4] == 'P' && sig[5] == 'T' &&
                sig[6] == 'R' && sig[7] == ' ') {
                if (acpi_checksum(rsdp, sizeof(acpi_rsdp_t)) == 0)
                    return rsdp;
            }
        }
    }
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        acpi_rsdp_t *rsdp = (acpi_rsdp_t *)addr;
        char sig[9];
        for (int i = 0; i < 8; i++) sig[i] = rsdp->signature[i];
        sig[8] = 0;
        if (sig[0] == 'R' && sig[1] == 'S' && sig[2] == 'D' &&
            sig[3] == ' ' && sig[4] == 'P' && sig[5] == 'T' &&
            sig[6] == 'R' && sig[7] == ' ') {
            if (acpi_checksum(rsdp, sizeof(acpi_rsdp_t)) == 0)
                return rsdp;
        }
    }
    return 0;
}

void acpi_init(acpi_state_t *state) {
    state->num_tables = 0;
    state->local_apic_addr = 0;
    state->has_madt = false;
    state->has_fadt = false;
    state->has_hpet = false;

    acpi_rsdp_t *rsdp = find_rsdp();
    if (!rsdp) {
        state->rsdp_address = 0;
        return;
    }

    state->rsdp_address = (uint32_t)rsdp;
    acpi_rsdt_t *rsdt = (acpi_rsdt_t *)rsdp->rsdt_address;
    if (!rsdt) return;

    uint32_t num_entries = (rsdt->header.length - sizeof(acpi_header_t)) / 4;
    if (num_entries > ACPI_MAX_TABLES) num_entries = ACPI_MAX_TABLES;

    for (uint32_t i = 0; i < num_entries; i++) {
        acpi_header_t *tbl = (acpi_header_t *)rsdt->entries[i];
        if (tbl && acpi_checksum(tbl, tbl->length) == 0) {
            state->tables[state->num_tables++] = tbl;

            /* The Multiple APIC Description Table's on-disk signature is
             * "APIC", not "MADT" — MADT is only its prose name. Matching
             * "MADT" meant has_madt and local_apic_addr were dead outputs on
             * every real machine and every emulator, which would have been a
             * quiet trap for whoever writes the APIC driver. */
            if (tbl->signature[0] == 'A' && tbl->signature[1] == 'P' &&
                tbl->signature[2] == 'I' && tbl->signature[3] == 'C') {
                acpi_madt_t *madt = (acpi_madt_t *)tbl;
                state->local_apic_addr = madt->local_apic_addr;
                state->has_madt = true;
            }
            if (tbl->signature[0] == 'F' && tbl->signature[1] == 'A' &&
                tbl->signature[2] == 'C' && tbl->signature[3] == 'P') {
                state->has_fadt = true;
            }
            if (tbl->signature[0] == 'H' && tbl->signature[1] == 'P' &&
                tbl->signature[2] == 'E' && tbl->signature[3] == 'T') {
                state->has_hpet = true;
            }
        }
    }
}

acpi_header_t *acpi_find_table(acpi_state_t *state, const char *signature) {
    for (uint32_t i = 0; i < state->num_tables; i++) {
        acpi_header_t *tbl = state->tables[i];
        bool match = true;
        for (int j = 0; j < 4; j++) {
            if (tbl->signature[j] != signature[j]) { match = false; break; }
        }
        if (match) return tbl;
    }
    return 0;
}
