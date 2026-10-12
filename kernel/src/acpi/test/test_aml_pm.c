/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aml_pm.c — table discovery (RSDP/XSDT/RSDT), FADT parsing, namespace
 * start-up, the typed device helpers (battery, lid, thermal, processors,
 * backlight), sleep-state entry, event polling and the boot report, all on
 * fake firmware tables and fake chipset registers. */
#include "../acpi_pm.h"
#include "amltest.h"

#define FW_PHYS 0x7FE00000ull
#define FW_RSDP 0x0000
#define FW_XSDT 0x0040
#define FW_RSDT 0x0100
#define FW_FACP 0x0200
#define FW_SSDT 0x0400
#define FW_ECDT 0x0500
#define FW_BAD  0x0600
#define FW_DSDT 0x1000
#define FW_SIZE 0x4000

#define F_HWRED  0x01
#define F_XDSDT  0x02
#define F_RSDP1  0x04
#define F_ECDT   0x08
#define F_PM1B   0x10
#define F_BADTBL 0x20

static uint8_t fw[FW_SIZE];

static void w32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void w64(uint8_t *p, uint64_t v)
{
    w32(p, (uint32_t) v);
    w32(p + 4, (uint32_t) (v >> 32));
}
static void fix_sum(uint8_t *t, uint32_t len, uint32_t at)
{
    uint8_t s = 0;
    t[at] = 0;
    for (uint32_t i = 0; i < len; i++) s = (uint8_t) (s + t[i]);
    t[at] = (uint8_t) (0u - s);
}
static void hdr(uint8_t *t, const char *sig, uint32_t len, uint8_t rev)
{
    memset(t, 0, 36);
    memcpy(t, sig, 4);
    w32(t + 4, len);
    t[8] = rev;
    memcpy(t + 10, "ZXVFK ", 6);
    memcpy(t + 16, "PMTEST  ", 8);
}
static void gas(uint8_t *g, uint8_t space, uint8_t bits, uint64_t addr)
{
    g[0] = space;
    g[1] = bits;
    g[2] = 0;
    g[3] = 0;
    w64(g + 4, addr);
}

/* Lay out RSDP -> XSDT/RSDT -> FACP (+ SSDT, ECDT) -> DSDT in fw[]. */
static void build_fw(const uint8_t *dsdt, uint32_t dlen, uint32_t flags)
{
    memset(fw, 0, sizeof fw);
    memcpy(fw + FW_DSDT, dsdt, dlen);
    memcpy(fw + FW_SSDT, fx_ssdt_dyn, sizeof fx_ssdt_dyn);

    uint8_t *f = fw + FW_FACP;
    uint32_t flen = (flags & F_XDSDT) || (flags & F_HWRED) ? 276 : 116;
    hdr(f, "FACP", flen, flen == 276 ? 6 : 1);
    w32(f + 40, (flags & F_XDSDT) ? 0 : (uint32_t) (FW_PHYS + FW_DSDT));
    f[46] = 9;
    w32(f + 48, FK_SMI_CMD);
    f[52] = 0xA0;
    f[53] = 0xA1;
    w32(f + 56, FK_PM1A_EVT);
    w32(f + 60, (flags & F_PM1B) ? FK_PM1B_EVT : 0);
    w32(f + 64, FK_PM1A_CNT);
    w32(f + 68, (flags & F_PM1B) ? FK_PM1B_CNT : 0);
    w32(f + 76, 0xB008);
    w32(f + 80, FK_GPE0);
    f[88] = 4;
    f[89] = 2;
    f[91] = 4;
    f[92] = 8;
    w32(f + 112, (flags & F_HWRED) ? ACPI_FADT_HW_REDUCED : 0);
    if (flen == 276) {
        gas(f + 116, 1, 8, 0xCF9);
        f[128] = 6;
        if (flags & F_XDSDT) w64(f + 140, FW_PHYS + FW_DSDT);
        gas(f + 148, 1, 32, FK_PM1A_EVT);
        gas(f + 172, 1, 16, FK_PM1A_CNT);
        gas(f + 220, 1, 64, FK_GPE0);
        if (flags & F_HWRED) {
            gas(f + 244, 1, 8, 0xB030);
            gas(f + 256, 1, 8, 0xB031);
        }
    }
    fix_sum(f, flen, 9);

    uint8_t *e = fw + FW_ECDT;
    static const char ecpath[] = "\\_SB.PCI0.EC0";
    uint32_t elen = 65 + (uint32_t) sizeof ecpath;
    hdr(e, "ECDT", elen, 1);
    gas(e + 36, 1, 8, FK_EC_CMD);
    gas(e + 48, 1, 8, FK_EC_DATA);
    e[64] = 0x17;
    memcpy(e + 65, ecpath, sizeof ecpath);
    fix_sum(e, elen, 9);

    uint8_t *b = fw + FW_BAD;
    hdr(b, "BADT", 40, 1);
    b[9] = 1; /* wrong checksum on purpose */

    uint64_t list[4];
    uint32_t n = 0;
    list[n++] = FW_PHYS + FW_FACP;
    list[n++] = FW_PHYS + FW_SSDT;
    if (flags & F_ECDT) list[n++] = FW_PHYS + FW_ECDT;
    if (flags & F_BADTBL) list[n++] = FW_PHYS + FW_BAD;

    uint8_t *x = fw + FW_XSDT;
    hdr(x, "XSDT", 36 + 8 * n, 1);
    for (uint32_t i = 0; i < n; i++) w64(x + 36 + 8 * i, list[i]);
    fix_sum(x, 36 + 8 * n, 9);
    uint8_t *r = fw + FW_RSDT;
    hdr(r, "RSDT", 36 + 4 * n, 1);
    for (uint32_t i = 0; i < n; i++) w32(r + 36 + 4 * i, (uint32_t) list[i]);
    fix_sum(r, 36 + 4 * n, 9);

    uint8_t *p = fw + FW_RSDP;
    memcpy(p, "RSD PTR ", 8);
    memcpy(p + 9, "ZXVFK ", 6);
    p[15] = (flags & F_RSDP1) ? 0 : 2;
    w32(p + 16, (uint32_t) (FW_PHYS + FW_RSDT));
    w32(p + 20, 36);
    w64(p + 24, FW_PHYS + FW_XSDT);
    fix_sum(p, 20, 8);
    fix_sum(p, 36, 32);
}

static const void *map_fw(void *ctx, uint64_t phys, uint64_t len)
{
    return (const void *) ((amlfake_t *) ctx)->plat.mmio_map(ctx, phys, len);
}

static amlfake_t f;
static aml_ctx_t c;
static acpi_state_t st;
static acpi_pm_t pm;

static int boot(uint32_t flags)
{
    build_fw(fx_pm, sizeof fx_pm, flags);
    if (setup(&f, &c, 0, 0)) return -1;
    fk_window(&f, FW_PHYS, fw, sizeof fw);
    if (acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f) < 0) return -2;
    return acpi_pm_init(&pm, &c, &f.plat, &st);
}

static aml_node_t *node(const char *p)
{
    return aml_lookup(&c, 0, p);
}

static void test_tables(void)
{
    build_fw(fx_pm, sizeof fx_pm, F_ECDT | F_BADTBL);
    fk_init(&f);
    fk_window(&f, FW_PHYS, fw, sizeof fw);
    CHECK_EQ(acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f), 3);
    CHECK_EQ(st.rsdp_rev, 2);
    CHECK_EQ(st.root_phys, FW_PHYS + FW_XSDT);
    CHECK_EQ(st.bad_checksums, 1);
    CHECK(st.has_fadt);
    CHECK(acpi_find_table(&st, "ECDT") != 0);
    CHECK(acpi_find_table(&st, "BADT") == 0);
    CHECK_EQ(st.table_phys[1], FW_PHYS + FW_SSDT);

    /* ACPI 1.0 RSDP: the RSDT is used */
    build_fw(fx_pm, sizeof fx_pm, F_RSDP1);
    CHECK_EQ(acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f), 2);
    CHECK_EQ(st.root_phys, FW_PHYS + FW_RSDT);
    /* revision 2 whose extended checksum is wrong: fall back to the RSDT */
    build_fw(fx_pm, sizeof fx_pm, 0);
    fw[FW_RSDP + 32] ^= 1;
    CHECK_EQ(acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f), 2);
    CHECK_EQ(st.root_phys, FW_PHYS + FW_RSDT);

    build_fw(fx_pm, sizeof fx_pm, 0);
    CHECK_EQ(acpi_init_from_rsdp(&st, 0, map_fw, &f), -1);
    CHECK_EQ(acpi_init_from_rsdp(&st, FW_PHYS, 0, &f), -1);
    CHECK_EQ(acpi_init_from_rsdp(&st, 0x1000, map_fw, &f), -1); /* unmapped */
    fw[FW_RSDP] = 'X';
    CHECK_EQ(acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f), -2);
    fw[FW_RSDP] = 'R';
    fw[FW_RSDP + 8] ^= 0x55;
    CHECK_EQ(acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f), -3);
    fw[FW_RSDP + 8] ^= 0x55;
    /* XSDT with a broken checksum -> the root itself is refused */
    fw[FW_XSDT + 9] ^= 1;
    CHECK_EQ(acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f), -4);
    fw[FW_XSDT + 9] ^= 1;
    /* XSDT that claims a huge length */
    w32(fw + FW_XSDT + 4, 0x7FFFFFFF);
    CHECK_EQ(acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f), -4);
}

static void test_fadt(void)
{
    acpi_fadt_info_t fi;
    build_fw(fx_pm, sizeof fx_pm, 0);
    CHECK_EQ(acpi_parse_fadt(fw + FW_FACP, 116, &fi), 0);
    CHECK_EQ(fi.dsdt, FW_PHYS + FW_DSDT);
    CHECK_EQ(fi.smi_cmd, FK_SMI_CMD);
    CHECK_EQ(fi.acpi_enable, 0xA0);
    CHECK_EQ(fi.sci_int, 9);
    CHECK_EQ(fi.pm1a_evt.addr, FK_PM1A_EVT);
    CHECK_EQ(fi.pm1a_evt.space, 1);
    CHECK_EQ(fi.pm1a_evt.bit_width, 32);
    CHECK_EQ(fi.pm1a_cnt.addr, FK_PM1A_CNT);
    CHECK_EQ(fi.pm1a_cnt.bit_width, 16);
    CHECK_EQ(fi.pm1b_cnt.addr, 0);
    CHECK_EQ(fi.gpe0.addr, FK_GPE0);
    CHECK_EQ(fi.gpe0_len, 8);
    CHECK_EQ(fi.rev, 1);

    build_fw(fx_pm, sizeof fx_pm, F_XDSDT | F_HWRED);
    CHECK_EQ(acpi_parse_fadt(fw + FW_FACP, 276, &fi), 0);
    CHECK_EQ(fi.dsdt, FW_PHYS + FW_DSDT);
    CHECK_EQ(fi.rev, 6);
    CHECK(fi.flags & ACPI_FADT_HW_REDUCED);
    CHECK_EQ(fi.reset.addr, 0xCF9);
    CHECK_EQ(fi.reset_value, 6);
    CHECK_EQ(fi.sleep_ctl.addr, 0xB030);
    CHECK_EQ(fi.sleep_sts.addr, 0xB031);
    CHECK_EQ(fi.gpe0.bit_width, 64);
    /* the length argument caps what is read, even if the header claims more */
    CHECK_EQ(acpi_parse_fadt(fw + FW_FACP, 130, &fi), 0);
    CHECK_EQ(fi.sleep_ctl.addr, 0);
    CHECK_EQ(fi.dsdt, 0);

    CHECK(acpi_parse_fadt(fw + FW_FACP, 100, &fi) != 0);
    CHECK(acpi_parse_fadt(0, 276, &fi) != 0);
    CHECK(acpi_parse_fadt(fw + FW_SSDT, 276, &fi) != 0);
    w32(fw + FW_FACP + 4, 50); /* header length too short */
    CHECK(acpi_parse_fadt(fw + FW_FACP, 276, &fi) != 0);
}

static void test_init(void)
{
    uint64_t v;
    CHECK_EQ(boot(F_XDSDT), 2);
    CHECK_EQ(pm.loaded, 2);
    CHECK_EQ(pm.load_errors, 0);
    CHECK(pm.have_fadt);
    CHECK_EQ(c.dsdt_rev, 2);
    CHECK_EQ(aml_stats(&c).tables, 2);
    CHECK_EQ(ev_int(&c, "\\SSXV", &v), 0);
    CHECK_EQ(v, 0x5A5A);

    /* _INI: \_SB first, PCI0, the functioning-but-absent FUN0's child; never
     * an absent device, its children, or a device whose _STA fails */
    CHECK_EQ(ev_int(&c, "\\SBIN", &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(ev_int(&c, "\\INIC", &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(ev_int(&c, "\\FUNI", &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(ev_int(&c, "\\BADI", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(pm.inis, 3);

    /* sleep packages, including the one-integer form */
    CHECK(pm.sleep_ok[0] && pm.sleep_ok[1] && pm.sleep_ok[3] && pm.sleep_ok[5]);
    CHECK(!pm.sleep_ok[2] && !pm.sleep_ok[4]);
    CHECK_EQ(pm.typa[1], 1);
    CHECK_EQ(pm.typb[1], 4);
    CHECK_EQ(pm.typa[3], 5);
    CHECK_EQ(pm.typa[5], 7);
    CHECK_EQ(pm.typb[5], 6);
    uint8_t a, b;
    CHECK_EQ(aml_sleep_state(&c, 4, &a, &b), AML_ENOENT);
    CHECK_EQ(aml_sleep_state(&c, 6, &a, &b), AML_EARG);
    CHECK_EQ(aml_sleep_state(&c, 3, 0, &b), AML_EARG);

    CHECK_EQ(pm.gpe_methods, 3);
    /* EC found through PNP0C09 _CRS (no ECDT in this layout) */
    CHECK_EQ(c.ec.data_port, 0x62);
    CHECK_EQ(c.ec.cmd_port, 0x66);
    CHECK_EQ(c.ec.gpe, 0x17);

    /* same thing through the legacy 32-bit DSDT pointer, the ECDT and RSDT */
    CHECK_EQ(boot(F_ECDT | F_RSDP1), 2);
    CHECK_EQ(c.ec.cmd_port, 0x66);
    CHECK(c.ec.dev == node("\\_SB.PCI0.EC0"));
}

static void test_devices(void)
{
    aml_node_t *dev[8];
    CHECK_EQ(boot(F_XDSDT), 2);
    CHECK_EQ(aml_find_devices(&c, "PNP0C0A", dev, 8), 3);
    CHECK_EQ(aml_find_devices(&c, "PNP0C0A", dev, 2), 2);
    CHECK_EQ(aml_find_devices(&c, "ACPI0007", dev, 8), 2);
    CHECK_EQ(aml_find_devices(&c, "PNP0C0C", dev, 8), 1);
    CHECK_EQ(aml_find_devices(&c, "PNP0C0D", dev, 8), 1);
    CHECK_EQ(aml_find_devices(&c, "PNP0A08", dev, 8), 1);
    CHECK_EQ(aml_find_devices(&c, "PNP0A03", dev, 8), 0);
    CHECK_EQ(aml_find_devices(&c, "PNP0C09", dev, 8), 1);

    /* _STA verdicts */
    CHECK_EQ(aml_sta(&c, node("\\_SB.PCI0.ABS0")), 0);
    CHECK_EQ(aml_sta(&c, node("\\_SB.PCI0.FUN0")), 8);
    CHECK_EQ(aml_sta(&c, node("\\_SB.PCI0.ERR0")), 0);
    CHECK_EQ(aml_sta(&c, node("\\_SB.PCI0")), 0x0F);
    CHECK_EQ(aml_sta_verdict(aml_sta(&c, node("\\_SB.BAT0"))), HW_T);
    CHECK_EQ(aml_sta_verdict(aml_sta(&c, node("\\_SB.PCI0.FUN0"))), HW_B);
    CHECK_EQ(aml_sta_verdict(aml_sta(&c, node("\\_SB.PCI0.ABS0"))), HW_N);
    CHECK_EQ(aml_sta_verdict(1), HW_F);

    /* batteries: _BIX, _BIF, absent */
    aml_battery_t bt;
    CHECK_EQ(aml_battery_read(&c, node("\\_SB.BAT0"), &bt), 0);
    CHECK_EQ(bt.present, 1);
    CHECK_EQ(bt.has_bix, 1);
    CHECK_EQ(bt.power_unit, 0);
    CHECK_EQ(bt.design_capacity, 50000);
    CHECK_EQ(bt.full_capacity, 40000);
    CHECK_EQ(bt.design_voltage, 11400);
    CHECK_EQ(bt.cycle_count, 123);
    CHECK_EQ(bt.state, 1);
    CHECK_EQ(bt.rate, 1500);
    CHECK_EQ(bt.remaining, 3000);
    CHECK_EQ(bt.voltage, 11800);
    CHECK_EQ(bt.percent, 7);
    CHECK_EQ(aml_battery_read(&c, node("\\_SB.BAT1"), &bt), 0);
    CHECK_EQ(bt.has_bix, 0);
    CHECK_EQ(bt.power_unit, 1);
    CHECK_EQ(bt.design_capacity, 5200);
    CHECK_EQ(bt.full_capacity, 5000);
    CHECK_EQ(bt.design_voltage, 10800);
    CHECK_EQ(bt.cycle_count, 0xFFFFFFFFu);
    CHECK_EQ(bt.rate, 0xFFFFFFFFu);
    CHECK_EQ(bt.percent, 100); /* remaining above full is capped */
    CHECK_EQ(aml_battery_read(&c, node("\\_SB.BAT2"), &bt), 0);
    CHECK_EQ(bt.present, 0);
    CHECK_EQ(bt.percent, 0xFFFFFFFFu);
    CHECK_EQ(aml_battery_read(&c, 0, &bt), AML_EARG);
    CHECK_EQ(aml_battery_read(&c, node("\\_SB.LID0"), &bt), 0); /* no _STA: 0x0F, no battery */
    CHECK_EQ(bt.present, 0);

    /* lid and thermal */
    uint32_t open = 9;
    CHECK_EQ(aml_lid_read(&c, node("\\_SB.LID0"), &open), 0);
    CHECK_EQ(open, 1);
    CHECK_EQ(aml_lid_read(&c, node("\\_SB.BAT0"), &open), AML_ENOENT);
    aml_thermal_t th;
    CHECK_EQ(aml_thermal_read(&c, node("\\_TZ.TZ00"), &th), 0);
    CHECK_EQ(th.tmp, 3132);
    CHECK_EQ(th.psv, 3532);
    CHECK_EQ(th.crt, 3732);
    CHECK_EQ(th.hot, 3632);
    CHECK_EQ(aml_dk_to_dc(th.tmp), 400);
    CHECK_EQ(aml_thermal_read(&c, node("\\_TZ.TZ01"), &th), 0);
    CHECK(aml_dk_to_dc(th.tmp) == -100);
    CHECK_EQ(th.crt, 0);

    /* processors */
    aml_pstate_t ps[4];
    CHECK_EQ(aml_cpu_pss(&c, node("\\_PR.CPU0"), ps, 4), 3);
    CHECK_EQ(ps[0].mhz, 2400);
    CHECK_EQ(ps[0].mw, 15000);
    CHECK_EQ(ps[2].mhz, 800);
    CHECK_EQ(ps[2].control, 0x800);
    CHECK_EQ(aml_cpu_pss(&c, node("\\_PR.CPU0"), ps, 1), 3); /* counts all, fills one */
    CHECK_EQ(aml_cpu_pss(&c, node("\\_PR.CPU1"), 0, 0), 1);
    CHECK_EQ(aml_cpu_pss(&c, node("\\_SB.CPU2"), ps, 4), AML_ENOENT);
    uint32_t ppc = 9, rev = 0;
    CHECK_EQ(aml_cpu_ppc(&c, node("\\_PR.CPU0"), &ppc), 0);
    CHECK_EQ(ppc, 1);
    uint64_t ctl = 0, sts = 0;
    CHECK_EQ(aml_cpu_pct(&c, node("\\_PR.CPU0"), &ctl, &sts), 1);
    CHECK_EQ(ctl, 0x199);
    CHECK_EQ(sts, 0x198);
    CHECK_EQ(aml_cpu_cpc(&c, node("\\_SB.CPU2"), &rev), 23);
    CHECK_EQ(rev, 3);
    CHECK_EQ(aml_cpu_cpc(&c, node("\\_SB.CPU3"), &rev), AML_ETYPE);
    CHECK_EQ(aml_node_type(node("\\_PR.CPU0")), AML_T_PROC);
    CHECK_EQ(node("\\_PR.CPU0")->x.cpu.pblk, 0x810);
    CHECK_EQ(node("\\_PR.CPU0")->x.cpu.plen, 6);
    CHECK_EQ(node("\\_PR.CPU1")->x.cpu.id, 1);

    /* backlight */
    uint32_t lv[8], ac = 0, dc = 0, cur = 0;
    aml_node_t *lcd = node("\\_SB.PCI0.GFX0.LCD0");
    CHECK_EQ(aml_bcl(&c, lcd, lv, 8, &ac, &dc), 6);
    CHECK_EQ(ac, 80);
    CHECK_EQ(dc, 40);
    CHECK_EQ(lv[0], 10);
    CHECK_EQ(lv[5], 100);
    CHECK_EQ(aml_bqc(&c, lcd, &cur), 0);
    CHECK_EQ(cur, 50);
    CHECK_EQ(aml_bcm(&c, lcd, 60), 0);
    CHECK_EQ(aml_bqc(&c, lcd, &cur), 0);
    CHECK_EQ(cur, 60);
    CHECK_EQ(aml_bcl(&c, node("\\_SB.LID0"), lv, 8, 0, 0), AML_ENOENT);
}

static void test_sleep(void)
{
    uint64_t v;
    CHECK_EQ(boot(F_XDSDT | F_PM1B), 2);
    CHECK_EQ(acpi_enter_sleep(&pm, 0), AML_EARG);
    CHECK_EQ(acpi_enter_sleep(&pm, 6), AML_EARG);
    CHECK_EQ(acpi_enter_sleep(&pm, 4), AML_ENOENT);

    /* S3 that resumes: _PTS, _SST, SLP_TYP then SLP_EN to both blocks, _WAK */
    f.wake_after_sleep = 1;
    CHECK_EQ(acpi_enter_sleep(&pm, 3), 0);
    CHECK_EQ(ev_int(&c, "\\PTSV", &v), 0);
    CHECK_EQ(v, 3);
    CHECK_EQ(ev_int(&c, "\\SSTV", &v), 0);
    CHECK_EQ(v, 3);
    CHECK_EQ(ev_int(&c, "\\WAKV", &v), 0);
    CHECK_EQ(v, 3);
    CHECK_EQ(f.last_slp_typ_a, 5);
    CHECK_EQ(f.last_slp_typ_b, 5);
    CHECK_EQ(f.sleep_writes, 2);
    CHECK_EQ(fk_io_count(&f, FK_PM1A_CNT, 16, 1), 2); /* SLP_TYP, then SLP_EN */
    CHECK_EQ(fk_io_count(&f, FK_PM1B_CNT, 16, 1), 2);

    /* S3 that never wakes: reported, _WAK not run */
    f.wake_after_sleep = 0;
    f.io[FK_PM1A_EVT + 1] = 0;
    CHECK_EQ(aml_eval(&c, 0, "\\WAKV", 0, 0, 0), 0);
    uint64_t t0 = f.now_us;
    CHECK_EQ(acpi_enter_sleep(&pm, 3), AML_EIO);
    CHECK(f.now_us - t0 >= 1000000);
    CHECK(fk_log_has(&f, "sleep request did not take effect"));

    /* S5: SLP_TYPa=7 SLP_TYPb=6; if we are still running it failed */
    f.sleep_writes = 0;
    CHECK_EQ(acpi_poweroff(&pm), AML_EIO);
    CHECK_EQ(f.sleep_writes, 2);
    CHECK_EQ(f.last_slp_typ_a, 7);
    CHECK_EQ(f.last_slp_typ_b, 6);
    CHECK_EQ(ev_int(&c, "\\PTSV", &v), 0);
    CHECK_EQ(v, 5);
    /* other PM1_CNT bits (SCI_EN) survive the write */
    f.io[FK_PM1A_CNT] = 1;
    CHECK_EQ(acpi_poweroff(&pm), AML_EIO);
    CHECK_EQ(f.io[FK_PM1A_CNT] & 1, 1);
    CHECK_EQ((f.io[FK_PM1A_CNT + 1] >> 2) & 7, 7);

    /* hardware-reduced: the sleep control register */
    CHECK_EQ(boot(F_XDSDT | F_HWRED), 2);
    CHECK_EQ(acpi_enter_sleep(&pm, 5), AML_EIO);
    CHECK_EQ(f.io[0xB030], (7 << 2) | 0x20);
    CHECK_EQ(f.sleep_writes, 0);
    f.io[0xB031] = 0x80;                         /* wake status already set: S3 "resumes" */
    CHECK_EQ(acpi_enter_sleep(&pm, 3), AML_EIO); /* cleared before sleeping */
    CHECK_EQ(acpi_pm_enable(&pm), 0);
    CHECK_EQ(pm.acpi_mode, 0);
}

static void test_events(void)
{
    uint64_t v;
    CHECK_EQ(boot(F_XDSDT), 2);

    /* ACPI mode: SMI_CMD <- ACPI_ENABLE, wait for SCI_EN */
    f.sci_en_after_smi = 1;
    CHECK_EQ(acpi_pm_enable(&pm), 0);
    CHECK_EQ(f.io[FK_SMI_CMD], 0xA0);
    CHECK_EQ(pm.acpi_mode, 1);
    CHECK_EQ(f.io[FK_PM1A_EVT + 3] & 1, 1); /* PWRBTN_EN */
    f.io[FK_SMI_CMD] = 0;
    CHECK_EQ(acpi_pm_enable(&pm), 0); /* already on: no SMI */
    CHECK_EQ(f.io[FK_SMI_CMD], 0);

    /* fixed power button */
    f.io[FK_PM1A_EVT + 1] |= 1;
    CHECK_EQ(acpi_poll_events(&pm), ACPI_EV_PWRBTN);
    CHECK_EQ(f.io[FK_PM1A_EVT + 1] & 1, 0);
    CHECK_EQ(acpi_poll_events(&pm), 0);

    /* level GPE 5, edge GPE 6, a disabled GPE 7 */
    f.io[FK_GPE0] = 0xE0;
    f.io[FK_GPE0 + 4] = 0x60;
    CHECK_EQ(acpi_poll_events(&pm), ACPI_EV_GPE);
    CHECK_EQ(ev_int(&c, "\\L05C", &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(ev_int(&c, "\\E06C", &v), 0);
    CHECK_EQ(v, 1);
    CHECK_EQ(f.io[FK_GPE0], 0x80);
    /* GPE 0x1F: _L1F notifies the control-method power button */
    f.io[FK_GPE0 + 3] = 0x80;
    f.io[FK_GPE0 + 7] = 0x80;
    CHECK_EQ(acpi_poll_events(&pm), ACPI_EV_GPE | ACPI_EV_NOTIFY | ACPI_EV_PWRBTN);
    CHECK_EQ(f.io[FK_GPE0 + 3], 0);
    /* EC GPE 0x17: query 0x11 runs _Q11, which notifies the lid */
    f.ec_query = 0x11;
    f.ec_ram[0x40] = 0;
    f.io[FK_GPE0 + 2] = 0x80;
    f.io[FK_GPE0 + 6] = 0x80;
    CHECK_EQ(acpi_poll_events(&pm), ACPI_EV_GPE | ACPI_EV_NOTIFY | ACPI_EV_LID);
    CHECK_EQ(ev_int(&c, "\\Q11C", &v), 0);
    CHECK_EQ(v, 1);
    uint32_t open = 9;
    CHECK_EQ(aml_lid_read(&c, node("\\_SB.LID0"), &open), 0);
    CHECK_EQ(open, 0);

    /* ACPI mode that never comes on */
    CHECK_EQ(boot(F_XDSDT), 2);
    CHECK_EQ(acpi_pm_enable(&pm), AML_EIO);
    CHECK(f.delays >= 3000);
    CHECK_EQ(pm.acpi_mode, 0);
    pm.fadt.smi_cmd = 0;
    CHECK_EQ(acpi_pm_enable(&pm), AML_EUNSUP);
    CHECK_EQ(acpi_poll_events(0), 0);
}

static void test_report(void)
{
    CHECK_EQ(boot(F_XDSDT), 2);
    f.sci_en_after_smi = 1;
    acpi_pm_enable(&pm);
    fk_log_clear(&f);
    CHECK_EQ(acpi_pm_report(&pm), 10);
    CHECK(fk_log_has(&f, "[ACPI OK] namespace: "));
    CHECK(fk_log_has(&f, " methods, DSDT rev 2 (2 tables, 0 load errors, 3 _INI"));
    CHECK(fk_log_has(&f, "[ACPI] S3 supported SLP_TYPa=0x5 SLP_TYPb=0x5"));
    CHECK(fk_log_has(&f, "[ACPI] \\_S5_ SLP_TYPa=0x7 SLP_TYPb=0x6"));
    CHECK(fk_log_has(&f, "[ACPI] power button: fixed feature (PM1 PWRBTN), control-method "
                         "devices PNP0C0C: 1, GPE methods: 3"));
    CHECK(fk_log_has(&f, "[ACPI] battery PNP0C0A: 3, present 7%, present 100%"));
    CHECK(fk_log_has(&f, "[ACPI] lid PNP0C0D: 1 (open)"));
    CHECK(fk_log_has(&f, "[ACPI] thermal zones: 2, _TMP 40.0 C _CRT 3732 dK, _TMP -10.0 C"));
    CHECK(fk_log_has(&f, "[ACPI] processors: 4 (Processor() objects 2), _PSS states 3 on #0 "
                         "(2 with _PSS), _CPC entries 0 (1 with _CPC)"));
    CHECK(fk_log_has(&f, "[ACPI] backlight: 1 output(s) with _BCL, 6 levels"));
    CHECK(fk_log_has(&f, "[ACPI] embedded controller: data 0x62 cmd 0x66, ACPI mode on"));

    /* no S3 */
    pm.sleep_ok[3] = 0;
    fk_log_clear(&f);
    acpi_pm_report(&pm);
    CHECK(fk_log_has(&f, "[ACPI] S3 absent"));

    /* no FADT at all */
    acpi_state_t empty;
    memset(&empty, 0, sizeof empty);
    fk_log_clear(&f);
    CHECK_EQ(acpi_pm_init(&pm, &c, &f.plat, &empty), 0);
    CHECK(fk_log_has(&f, "no usable FADT"));
    CHECK_EQ(acpi_pm_report(&pm), 1);
    CHECK(fk_log_has(&f, "[ACPI] no AML namespace"));
    CHECK_EQ(acpi_enter_sleep(&pm, 5), AML_ENOENT);
    CHECK_EQ(acpi_poll_events(&pm), 0);
    CHECK_EQ(acpi_pm_init(&pm, 0, &f.plat, &st), 0);

    /* a DSDT with garbage in the middle still loads what it can */
    static uint8_t bad[sizeof fx_pm];
    memcpy(bad, fx_pm, sizeof bad);
    bad[36] = 0x5B; /* an extended opcode that does not exist */
    bad[37] = 0x99;
    build_fw(bad, sizeof bad, F_XDSDT);
    setup(&f, &c, 0, 0);
    fk_window(&f, FW_PHYS, fw, sizeof fw);
    CHECK(acpi_init_from_rsdp(&st, FW_PHYS, map_fw, &f) > 0);
    CHECK_EQ(acpi_pm_init(&pm, &c, &f.plat, &st), 2);
    CHECK_EQ(pm.load_errors, 1);
    CHECK(fk_log_has(&f, "DSDT load reported errors"));
    CHECK(aml_stats(&c).objects > 20);
    CHECK(pm.sleep_ok[5]);
    CHECK_EQ(acpi_pm_report(&pm), 10);
    CHECK(fk_log_has(&f, "[ACPI OK] namespace: "));
}

static void test_rev1(void)
{
    uint64_t v;
    CHECK_EQ(setup(&f, &c, fx_rev1, sizeof fx_rev1), 0);
    CHECK_EQ(c.dsdt_rev, 1);
    CHECK_EQ(ev_int(&c, "W001", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(ev_int(&c, "W002", &v), 0);
    CHECK_EQ(v, 0xFFFFFFFFu);
    CHECK_EQ(ev_int(&c, "W003", &v), 0);
    CHECK_EQ(v, 0xFFFFFFFFu);
    CHECK_EQ(ev_int(&c, "W004", &v), 0);
    CHECK_EQ(v, 0);
    CHECK_EQ(ev_int(&c, "W005", &v), 0);
    CHECK_EQ(v, 0xFFFFFFFFu);
    CHECK_EQ(ev_int(&c, "W006", &v), 0);
    CHECK_EQ(v, 0x04030201u);
    CHECK_EQ(ev_int(&c, "W007", &v), 0);
    CHECK_EQ(v, 8);
    CHECK_EQ(ev_int(&c, "W008", &v), 0);
    CHECK_EQ(v, 32);
    CHECK_EQ(ev_int(&c, "W009", &v), 0);
    CHECK_EQ(v, 0x23456789u);
    CHECK_EQ(ev_int(&c, "W010", &v), 0);
    CHECK_EQ(v, 0xFFFFFFFFu);
}

int main(void)
{
    test_tables();
    test_fadt();
    test_init();
    test_devices();
    test_sleep();
    test_events();
    test_report();
    test_rev1();
    return finish_tests("test_aml_pm");
}
