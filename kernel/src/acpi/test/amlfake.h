/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* amlfake.h — a host aml_plat_t with fake hardware behind every hook:
 *   memory   windows of host memory at fake physical addresses
 *   ports    a 64 KB port space that remembers writes, plus device models:
 *            ACPI EC (0x62/0x66), CMOS (0x70-0x73), an index/data pair
 *            (0x500/0x501), a banked register (select 0x600, data 0x610),
 *            PM1 event/control blocks and a GPE block (write-1-to-clear)
 *   PCI      a few configuration spaces, byte addressed
 *   time     delay_us advances a clock; timer_100ns reads it
 * Every port access is logged so tests can check widths and order. */
#ifndef ZXV_AMLFAKE_H
#define ZXV_AMLFAKE_H

#include "../aml.h"

#define FK_PM1A_EVT 0xB000 /* STS 0xB000, EN 0xB002 */
#define FK_PM1B_EVT 0xB010
#define FK_PM1A_CNT 0xB004
#define FK_PM1B_CNT 0xB014
#define FK_GPE0     0xB020 /* 4 status bytes, 4 enable bytes */
#define FK_SMI_CMD  0x00B2
#define FK_EC_DATA  0x62
#define FK_EC_CMD   0x66

typedef struct fk_io {
    uint32_t port, width, val;
    uint8_t write;
} fk_io_t;

typedef struct fk_win {
    uint64_t phys, len;
    uint8_t *buf;
} fk_win_t;

typedef struct fk_pci {
    uint32_t seg, bus, dev, fn;
    uint8_t cfg[256];
} fk_pci_t;

typedef struct amlfake {
    aml_plat_t plat;
    fk_win_t win[6];
    uint32_t nwin, map_calls, map_fail;
    uint8_t io[0x10000];
    fk_io_t iolog[1024];
    uint32_t niolog, io_dropped;
    fk_pci_t pci[8];
    uint32_t npci;
    uint64_t now_us;
    uint32_t delays;
    uint32_t gl_acq, gl_rel, gl_fail;
    /* EC model */
    uint8_t ec_ram[256];
    uint8_t ec_state, ec_addr, ec_out, ec_obf, ec_query, ec_dead, ec_burst;
    uint32_t ec_reads, ec_writes;
    /* CMOS, index/data, bank */
    uint8_t cmos[256], cmos_idx[2];
    uint8_t idx_file[256], idx_sel;
    uint8_t bank_sel, bank_reg[4][4];
    /* PM */
    uint32_t sleep_writes, last_slp_typ_a, last_slp_typ_b;
    uint8_t wake_after_sleep, sci_en_after_smi;
    /* log */
    char log[512][160];
    uint32_t nlog;
} amlfake_t;

void fk_init(amlfake_t *f);
/* Map host buffer buf (len bytes) at physical address phys. */
void fk_window(amlfake_t *f, uint64_t phys, void *buf, uint64_t len);
fk_pci_t *fk_pci_add(amlfake_t *f, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn);
int fk_log_has(const amlfake_t *f, const char *s);
uint32_t fk_log_count(const amlfake_t *f, const char *s);
void fk_log_clear(amlfake_t *f);
/* How many logged port accesses match (port, width, write). */
uint32_t fk_io_count(const amlfake_t *f, uint32_t port, uint32_t width, int write);
/* Wrap body (AML after the header) into a table with a valid checksum. */
uint32_t fk_table(uint8_t *out, uint32_t max, const char sig[4], uint8_t rev, const uint8_t *body,
                  uint32_t blen);

#endif
