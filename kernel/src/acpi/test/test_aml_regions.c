/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aml_regions.c — fields over SystemMemory, SystemIO, PCI_Config (with
 * _BBN and a PCI bridge), EmbeddedControl, SystemCMOS, IndexField, BankField,
 * DataTableRegion and Load(), checked at the register level: the bytes in
 * fake memory, the port accesses and their widths, the EC transactions. */
#include "amltest.h"

static uint8_t mem[0x1000];
static uint8_t ssdt_win[0x400];

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static int run(aml_ctx_t *c, const char *m, uint64_t *v)
{
    return ev_int(c, m, v);
}

int main(void)
{
    static amlfake_t f;
    static aml_ctx_t c;
    uint64_t v;

    CHECK_EQ(setup(&f, &c, fx_regions, sizeof fx_regions), 0);
    fk_window(&f, 0xFED40000ull, mem, sizeof mem);
    memcpy(ssdt_win, fx_ssdt_dyn, sizeof fx_ssdt_dyn);
    fk_window(&f, 0xFED41000ull, ssdt_win, sizeof ssdt_win);
    fk_pci_t *lpc = fk_pci_add(&f, 0, 0, 0x1F, 0);
    wr32(lpc->cfg + 0x40, 0x00001801);
    wr32(lpc->cfg + 0x44, 0xAABBCCDD);
    fk_pci_t *br = fk_pci_add(&f, 0, 0, 0x1C, 2);
    br->cfg[0x19] = 3;
    fk_pci_t *dx = fk_pci_add(&f, 0, 3, 0, 1);
    wr32(dx->cfg, 0x12348086);
    fk_pci_t *dy = fk_pci_add(&f, 0, 0x80, 3, 0);
    dy->cfg[0] = 0x77;
    f.ec_ram[0x20] = 0x2D;
    f.cmos[0x32] = 0x20;
    f.cmos[0x90] = 0x99;

    /* _REG(PCI_Config) runs in the start-up sequence; the EC's once probed */
    CHECK_EQ(aml_init_devices(&c), 0);
    CHECK_EQ(run(&c, "R027", &v), 0);
    CHECK_EQ(v, 0x01);
    CHECK_EQ(aml_ec_probe(&c), 1);
    CHECK_EQ(c.ec.data_port, 0x62);
    CHECK_EQ(c.ec.cmd_port, 0x66);
    CHECK_EQ(c.ec.gpe, 0x17);
    CHECK_EQ(run(&c, "R027", &v), 0);
    CHECK_EQ(v, 0x11);

    /* SystemMemory, AnyAcc */
    CHECK_EQ(run(&c, "R001", &v), 0);
    CHECK_EQ(v, 0x0123456789ABCDEFull);
    static const uint8_t m0[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                   0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01};
    CHECK(memcmp(mem, m0, 16) == 0);
    mem[0x10] = 0xF0;
    CHECK_EQ(run(&c, "R002", &v), 0);
    CHECK_EQ(v, 5);
    CHECK_EQ(mem[0x10], 0xFB);
    aml_val_t r;
    CHECK_EQ(aml_eval(&c, 0, "R003", 0, 0, &r), 0);
    static const uint8_t seq[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    CHECK(val_is_buf(&r, seq, 16));
    CHECK(memcmp(mem + 0x11, seq, 16) == 0);
    aml_val_free(&c, &r);
    CHECK_EQ(run(&c, "R004", &v), 0);
    CHECK_EQ(v, 0x01230ABC);
    CHECK_EQ(mem[0x30], 0xBC);
    CHECK_EQ(mem[0x31], 0x3A);
    CHECK_EQ(mem[0x32], 0x12);
    /* update rules */
    mem[0x40] = 0x00;
    CHECK_EQ(run(&c, "R005", &v), 0);
    CHECK_EQ(v, 5);
    CHECK_EQ(mem[0x40], 0xF5);
    mem[0x41] = 0xFF;
    CHECK_EQ(run(&c, "R006", &v), 0);
    CHECK_EQ(v, 5);
    CHECK_EQ(mem[0x41], 0x05);
    wr32(mem + 0x44, 0x11223344);
    CHECK_EQ(run(&c, "R007", &v), 0);
    CHECK_EQ(v, 0xAB);
    CHECK_EQ(rd32(mem + 0x44), 0x1122AB44);
    CHECK_EQ(run(&c, "R008", &v), 0);
    CHECK_EQ(v, 0x1122334455667788ull);
    CHECK_EQ(rd32(mem + 0x48), 0x55667788);
    CHECK_EQ(rd32(mem + 0x4C), 0x11223344);
    CHECK_EQ(run(&c, "R009", &v), 0);
    CHECK_EQ(v, 0xBEEF);
    CHECK_EQ(mem[0x50], 0xEF);
    CHECK_EQ(mem[0x51], 0xBE);
    uint32_t acq = f.gl_acq, rel = f.gl_rel;
    CHECK_EQ(run(&c, "R010", &v), 0);
    CHECK_EQ(v, 7);
    CHECK_EQ(f.gl_acq - acq, 2);
    CHECK_EQ(f.gl_rel - rel, 2);
    CHECK_EQ(c.gl_count, 0);
    f.gl_fail = 1;
    CHECK_EQ(run(&c, "R010", &v), AML_EIO);
    f.gl_fail = 0;
    mem[0xFF] = 0x5C;
    CHECK_EQ(run(&c, "R011", &v), 0);
    CHECK_EQ(v, 0x5C);
    CHECK_EQ(run(&c, "R012", &v), AML_EBOUNDS);
    wr32(mem + 0x80, 0x11111111);
    wr32(mem + 0xA0, 0xCAFEF00D);
    CHECK_EQ(run(&c, "R013", &v), 0); /* the address is evaluated at first use */
    CHECK_EQ(v, 0xCAFEF00D);
    mem[0x62] = 0xFF;
    mem[0x63] = 0xFF;
    CHECK_EQ(run(&c, "R028", &v), 0);
    CHECK_EQ(v, 0xF);
    CHECK_EQ(mem[0x62], 0xF0);
    CHECK_EQ(mem[0x63], 0x00);
    CHECK_EQ(run(&c, "R030", &v), 0);
    CHECK_EQ(v, 0x4241);
    CHECK_EQ(aml_eval(&c, 0, "R031", 0, 0, &r), 0);
    CHECK_EQ(r.type, AML_T_BUF);
    CHECK_EQ(r.len, 16);
    CHECK_EQ(r.u.bytes[0], 0x55);
    CHECK_EQ(r.u.bytes[1], 0);
    CHECK_EQ(r.u.bytes[15], 0);
    aml_val_free(&c, &r);

    /* SystemIO: widths and read-modify-write */
    f.niolog = 0;
    CHECK_EQ(run(&c, "R014", &v), 0);
    CHECK_EQ(v, 0x1234);
    CHECK_EQ(fk_io_count(&f, 0x400, 16, 1), 1);
    CHECK_EQ(fk_io_count(&f, 0x402, 16, 0), 1);
    CHECK_EQ(fk_io_count(&f, 0x402, 16, 1), 1);
    CHECK_EQ(fk_io_count(&f, 0x400, 16, 0), 1);
    CHECK_EQ(f.io[0x402], 0x56);
    CHECK_EQ(f.niolog, 4);
    f.io[0x408] = 0xEF;
    f.io[0x409] = 0xBE;
    f.io[0x40A] = 0xAD;
    f.io[0x40B] = 0xDE;
    f.niolog = 0;
    CHECK_EQ(run(&c, "R015", &v), 0);
    CHECK_EQ(v, 0xDEADBEEF);
    CHECK_EQ(fk_io_count(&f, 0x408, 32, 0), 1);
    CHECK_EQ(f.niolog, 1);
    f.io[0x40C] = 0xF0;
    f.niolog = 0;
    CHECK_EQ(run(&c, "R016", &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(f.io[0x40C], 0xF1);
    CHECK_EQ(fk_io_count(&f, 0x40C, 8, 1), 1);
    f.niolog = 0;
    CHECK_EQ(run(&c, "R029", &v), 0);
    CHECK_EQ(v, 0x1122334455667788ull);
    CHECK_EQ(fk_io_count(&f, 0x408, 32, 1), 1);
    CHECK_EQ(fk_io_count(&f, 0x40C, 32, 1), 1);

    /* PCI_Config: _ADR, _BBN, a bridge's secondary bus */
    CHECK_EQ(run(&c, "R017", &v), 0);
    CHECK_EQ(v, 0x00001801);
    CHECK_EQ(rd32(lpc->cfg + 0x44), 0xAABB1234);
    CHECK_EQ(run(&c, "R018", &v), 0);
    CHECK_EQ(v, 0x12348086);
    CHECK_EQ(run(&c, "R019", &v), 0);
    CHECK_EQ(v, 0x77);

    /* EmbeddedControl through the RD/WR protocol */
    uint32_t er = f.ec_reads, ew = f.ec_writes;
    CHECK_EQ(run(&c, "R020", &v), 0);
    CHECK_EQ(v, 0x2D1234);
    CHECK_EQ(f.ec_ram[0x10], 0x34);
    CHECK_EQ(f.ec_ram[0x11], 0x12);
    CHECK_EQ(f.ec_writes - ew, 2);
    CHECK_EQ(f.ec_reads - er, 3);
    uint8_t b = 0;
    CHECK_EQ(aml_ec_read(&c, 0x20, &b), 0);
    CHECK_EQ(b, 0x2D);
    CHECK_EQ(aml_ec_write(&c, 0x21, 0x99), 0);
    CHECK_EQ(f.ec_ram[0x21], 0x99);
    /* query: SCI_EVT set, 0x84 returns 0x42, _Q42 runs */
    f.ec_query = 0x42;
    CHECK_EQ(aml_ec_query(&c), 0x42);
    CHECK_EQ(run(&c, "QHIT", &v), 0);
    CHECK_EQ(v, 0x42);
    CHECK_EQ(aml_ec_query(&c), -1); /* nothing pending */

    /* CMOS, IndexField, BankField, OEM space, DataTableRegion */
    CHECK_EQ(run(&c, "R021", &v), 0);
    CHECK_EQ(v, 0x9920);
    CHECK_EQ(run(&c, "R022", &v), 0);
    CHECK_EQ(v, 0x5ABEEF);
    CHECK_EQ(f.idx_file[0x10], 0x5A);
    CHECK_EQ(f.idx_file[0x11], 0xEF);
    CHECK_EQ(f.idx_file[0x12], 0xBE);
    CHECK_EQ(run(&c, "R023", &v), 0);
    CHECK_EQ(v, 0x2211);
    CHECK_EQ(f.bank_reg[1][0], 0x11);
    CHECK_EQ(f.bank_reg[2][0], 0x22);
    CHECK_EQ(run(&c, "R024", &v), AML_EUNSUP);
    CHECK_EQ(run(&c, "R025", &v), 0);
    CHECK_EQ(v, 0x54445344);
    CHECK_EQ(run(&c, "R032", &v), 0);
    CHECK_EQ(v, 0x54445353);

    /* Load() of a table held in a memory region */
    CHECK(aml_lookup(&c, 0, "\\SSXV") == 0);
    CHECK_EQ(run(&c, "R026", &v), 0);
    CHECK_EQ(v, 0x5A5A);
    CHECK_EQ(run(&c, "DDBH", &v), 0);
    CHECK_EQ(v, 2);
    CHECK_EQ(ev_int1(&c, "\\SSXM", 1, &v), 0);
    CHECK_EQ(v, 0x5A5B);
    CHECK_EQ(aml_stats(&c).tables, 2);
    /* the same table twice: its names already exist, so Load reports failure */
    CHECK_EQ(run(&c, "R026", &v), 0);
    CHECK_EQ(run(&c, "DDBH", &v), 0);
    CHECK_EQ(v, 0);

    /* a dead EC: every access times out, then the driver stops trying */
    f.ec_dead = 1;
    uint64_t t0 = f.now_us;
    CHECK_EQ(run(&c, "\\EC0.TMP0", &v), AML_EIO);
    CHECK(f.now_us - t0 >= 20000);
    for (int i = 0; i < 10; i++) run(&c, "\\EC0.TMP0", &v);
    CHECK(c.ec.timeouts >= 8);
    t0 = f.now_us;
    CHECK_EQ(run(&c, "\\EC0.TMP0", &v), AML_EIO);
    CHECK_EQ(f.now_us, t0);
    CHECK_EQ(aml_ec_query(&c), -1);

    /* a platform that cannot map memory */
    static aml_ctx_t c2;
    static amlfake_t f2;
    CHECK_EQ(setup(&f2, &c2, fx_regions, sizeof fx_regions), 0);
    f2.map_fail = 1;
    CHECK_EQ(run(&c2, "\\MB00", &v), AML_EIO);
    CHECK_EQ(run(&c2, "\\IOD8", &v), 0); /* ports still work */
    f2.plat.io_read = 0;
    CHECK_EQ(run(&c2, "\\IOD8", &v), AML_EIO);
    CHECK_EQ(run(&c2, "\\EC0.TMP0", &v), AML_EIO); /* no EC configured */
    CHECK_EQ(aml_ec_init(&c2, 0, 0x66, 0), AML_EARG);
    CHECK_EQ(aml_ec_init(&c2, 0x62, 0x10000, 0), AML_EARG);
    return finish_tests("test_aml_regions");
}
