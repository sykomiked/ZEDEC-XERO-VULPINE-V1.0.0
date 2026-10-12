/* acpi.c — ACPI table parser implementation
 * Scans EBDA and 0xE0000-0xFFFFF for RSDP, follows to RSDT; or, given the
 * RSDP's address, follows the XSDT with a mapping hook (acpi_init_from_rsdp).
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
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
    uint32_t ebda_ptr = *(volatile uint16_t *)(uintptr_t)0x40E;
    if (ebda_ptr) {
        uint32_t ebda = ebda_ptr << 4;
        for (uint32_t addr = ebda; addr < ebda + 1024; addr += 16) {
            acpi_rsdp_t *rsdp = (acpi_rsdp_t *)(uintptr_t)addr;
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
        acpi_rsdp_t *rsdp = (acpi_rsdp_t *)(uintptr_t)addr;
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

    state->rsdp_address = (uint32_t)(uintptr_t)rsdp;
    acpi_rsdt_t *rsdt = (acpi_rsdt_t *)(uintptr_t)rsdp->rsdt_address;
    if (!rsdt) return;

    uint32_t num_entries = (rsdt->header.length - sizeof(acpi_header_t)) / 4;
    if (num_entries > ACPI_MAX_TABLES) num_entries = ACPI_MAX_TABLES;

    for (uint32_t i = 0; i < num_entries; i++) {
        acpi_header_t *tbl = (acpi_header_t *)(uintptr_t)rsdt->entries[i];
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

uint64_t acpi_find_rsdp_legacy(void) {
    return (uint64_t)(uintptr_t)find_rsdp();
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

/* Map a whole table: the header first (for its length), then all of it. */
static const uint8_t *map_table(uint64_t phys, acpi_map_fn map, void *ctx,
                                uint32_t *len) {
    const uint8_t *h = (const uint8_t *)map(ctx, phys, 36);
    if (!h) return 0;
    uint32_t l = rd32(h + 4);
    if (l < 36 || l > (16u << 20)) return 0;
    const uint8_t *t = (const uint8_t *)map(ctx, phys, l);
    if (!t || acpi_checksum(t, l) != 0) return 0;
    *len = l;
    return t;
}

int acpi_init_from_rsdp(acpi_state_t *st, uint64_t rsdp_phys, acpi_map_fn map,
                        void *ctx) {
    st->num_tables = 0;
    st->local_apic_addr = 0;
    st->has_madt = st->has_fadt = st->has_hpet = false;
    st->rsdp_address = 0;
    st->rsdp_phys = 0;
    st->root_phys = 0;
    st->rsdp_rev = 0;
    st->bad_checksums = 0;
    if (!map || !rsdp_phys) return -1;
    const uint8_t *r = (const uint8_t *)map(ctx, rsdp_phys, 36);
    if (!r) return -1;
    static const char sig[8] = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' '};
    for (int i = 0; i < 8; i++)
        if (r[i] != (uint8_t)sig[i]) return -2;
    if (acpi_checksum(r, 20) != 0) return -3;
    st->rsdp_phys = rsdp_phys;
    st->rsdp_rev = r[15];
    st->rsdp_address = (uint32_t)rsdp_phys;
    uint64_t root = rd32(r + 16);
    uint32_t esz = 4;
    if (r[15] >= 2) {
        uint32_t rlen = rd32(r + 20);
        if (rlen >= 36 && rlen <= 64 && acpi_checksum(r, rlen) == 0 && rd64(r + 24)) {
            root = rd64(r + 24);
            esz = 8;
        }
    }
    uint32_t rl;
    const uint8_t *rt = map_table(root, map, ctx, &rl);
    if (!rt) return -4;
    st->root_phys = root;
    uint32_t n = (rl - 36) / esz;
    for (uint32_t i = 0; i < n && st->num_tables < ACPI_MAX_TABLES; i++) {
        uint64_t ph = esz == 8 ? rd64(rt + 36 + 8 * i) : rd32(rt + 36 + 4 * i);
        uint32_t tl;
        const uint8_t *t = ph ? map_table(ph, map, ctx, &tl) : 0;
        if (!t) {
            st->bad_checksums++;
            continue;
        }
        acpi_header_t *h = (acpi_header_t *)(uintptr_t)t;
        st->table_phys[st->num_tables] = ph;
        st->tables[st->num_tables++] = h;
        if (t[0] == 'A' && t[1] == 'P' && t[2] == 'I' && t[3] == 'C' && tl >= 44) {
            st->local_apic_addr = rd32(t + 36);
            st->has_madt = true;
        }
        if (t[0] == 'F' && t[1] == 'A' && t[2] == 'C' && t[3] == 'P') st->has_fadt = true;
        if (t[0] == 'H' && t[1] == 'P' && t[2] == 'E' && t[3] == 'T') st->has_hpet = true;
    }
    return (int)st->num_tables;
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

/* ---- DECLARATION -----------------------------------------------------------

 * PROVIDES acpi_parse_ready, NOT "acpi_ready", and the difference is the whole
 * point of this comment.
 *
 * THE PARSER IS PORTABLE; THE DISCOVERY IS NOT. find_rsdp() above dereferences
 * 0x40E (the BIOS EBDA pointer) and scans 0xE0000..0xFFFFF. Those are x86
 * firmware addresses. On QEMU virt/aarch64 RAM starts at 0x40000000 and none
 * of that range is mapped, so acpi_init() -- the module's own entry point --
 * would take a data abort during boot. It is therefore NOT called here, and
 * this module does not claim to have found any table.
 *
 * What IS portable, and what the bring-up actually exercises, is the table
 * machinery: the 8-bit sum check and the signature search over a state the
 * caller supplies. The bring-up builds one synthetic APIC table in static
 * memory, makes its checksum come out to zero the way a real one does, and
 * asks acpi_find_table to locate it -- so a regression in either is caught
 * without any firmware being present at all.
 */
#include "zxv_decl.h"
static int zxvd_acpi_bringup(void) {
    static uint8_t tbl[64];
    static acpi_state_t st;
    acpi_header_t *h = (acpi_header_t *)tbl;
    uint32_t i;
    uint8_t sum = 0;

    for (i = 0; i < sizeof tbl; i++) tbl[i] = 0;
    h->signature[0] = 'A'; h->signature[1] = 'P';
    h->signature[2] = 'I'; h->signature[3] = 'C';
    h->length = sizeof tbl;
    /* make the table self-consistent: a real ACPI table sums to zero */
    for (i = 0; i < sizeof tbl; i++) sum = (uint8_t)(sum + tbl[i]);
    h->checksum = (uint8_t)(0u - sum);
    if (acpi_checksum(tbl, sizeof tbl) != 0) return -1;

    st.num_tables = 1;
    st.tables[0] = h;
    if (acpi_find_table(&st, "APIC") != h)  return -1;
    if (acpi_find_table(&st, "FACP") != 0)  return -1;
    return 0;
}

ZXV_DECLARE(acpi,
    ZXV_PROVIDES(acpi_parse_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_acpi_bringup));
