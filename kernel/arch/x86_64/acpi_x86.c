/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* acpi_x86.c — aml_plat_t for x86_64 and the ACPI boot step (acpi_x86.h).
 *
 *   port I/O      in/out b/w/l
 *   PCI_Config    segment 0 through the class drivers' CAM hooks (dword
 *                 access; narrower writes read-modify-write the dword)
 *   memory        the class drivers' mmio_map (identity, uncached 2 MB pages)
 *   timer         TSC, calibrated by hwplat_x86
 *   global lock   the FACS lock dword, acquired with cmpxchg as the ACPI
 *                 specification's sample code does
 * Integer only. */
#include "acpi_x86.h"

#include "acpi/acpi_pm.h"
#include "efifb_con.h"

static inline void outb(uint16_t p, uint8_t v)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(p));
}
static inline void outw(uint16_t p, uint16_t v)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(v), "Nd"(p));
}
static inline void outl(uint16_t p, uint32_t v)
{
    __asm__ __volatile__("outl %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p)
{
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline uint16_t inw(uint16_t p)
{
    uint16_t v;
    __asm__ __volatile__("inw %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline uint32_t inl(uint16_t p)
{
    uint32_t v;
    __asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t) hi << 32) | lo;
}

static const hw_plat_t *g_hp;
static uint64_t g_tsc_per_us;
static volatile uint32_t *g_glock; /* FACS global lock, 0 = none */
static acpi_pm_t g_pm;

/* ---- aml_plat_t hooks --------------------------------------------------------- */
static volatile void *x_map(void *ctx, uint64_t phys, uint64_t size)
{
    (void) ctx;
    return g_hp->mmio_map(g_hp->ctx, phys, size);
}

static uint32_t x_ior(void *ctx, uint32_t port, uint32_t width)
{
    (void) ctx;
    if (port > 0xFFFF) return 0xFFFFFFFFu;
    if (width == 8) return inb((uint16_t) port);
    if (width == 16) return inw((uint16_t) port);
    return inl((uint16_t) port);
}

static void x_iow(void *ctx, uint32_t port, uint32_t width, uint32_t v)
{
    (void) ctx;
    if (port > 0xFFFF) return;
    if (width == 8)
        outb((uint16_t) port, (uint8_t) v);
    else if (width == 16)
        outw((uint16_t) port, (uint16_t) v);
    else
        outl((uint16_t) port, v);
}

static int pci_ok(uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off)
{
    return seg == 0 && bus < 256 && dev < 32 && fn < 8 && off < 256;
}

static uint32_t x_pcir(void *ctx, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn,
                       uint32_t off, uint32_t width)
{
    (void) ctx;
    uint32_t mask = width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1;
    if (!pci_ok(seg, bus, dev, fn, off)) return mask;
    uint32_t d = g_hp->cfg_read(g_hp->ctx, (bus << 8) | (dev << 3) | fn, off & 0xFC);
    return (d >> (8 * (off & 3))) & mask;
}

static void x_pciw(void *ctx, uint32_t seg, uint32_t bus, uint32_t dev, uint32_t fn, uint32_t off,
                   uint32_t width, uint32_t v)
{
    (void) ctx;
    if (!pci_ok(seg, bus, dev, fn, off)) return;
    uint32_t bdf = (bus << 8) | (dev << 3) | fn;
    if (width >= 32 && !(off & 3)) {
        g_hp->cfg_write(g_hp->ctx, bdf, off, v);
        return;
    }
    uint32_t sh = 8 * (off & 3), mask = ((width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1)) << sh;
    uint32_t d = g_hp->cfg_read(g_hp->ctx, bdf, off & 0xFC);
    d = (d & ~mask) | ((v << sh) & mask);
    g_hp->cfg_write(g_hp->ctx, bdf, off & 0xFC, d);
}

static void x_delay(void *ctx, uint32_t us)
{
    (void) ctx;
    g_hp->delay_us(g_hp->ctx, us);
}

static uint64_t x_timer(void *ctx)
{
    (void) ctx;
    return g_tsc_per_us ? rdtsc() / g_tsc_per_us * 10 : 0;
}

/* FACS global lock: bit 0 pending, bit 1 owned. */
static int x_glock(void *ctx, int acquire)
{
    (void) ctx;
    if (!g_glock) return 1;
    if (acquire) {
        for (uint32_t tries = 0; tries < 2000; tries++) {
            uint32_t old = *g_glock, nw = ((old & ~3u) + 2u) + ((old >> 1) & 1u);
            if (!__sync_bool_compare_and_swap(g_glock, old, nw)) continue;
            if (nw < 3) return 1;
            g_hp->delay_us(g_hp->ctx, 500); /* firmware owns it; we are pending */
        }
        return 0;
    }
    uint32_t old, nw;
    do {
        old = *g_glock;
        nw = old & ~3u;
    } while (!__sync_bool_compare_and_swap(g_glock, old, nw));
    if (old & 1u) /* firmware waits: GBL_RLS (PM1 control bit 2) */
        acpi_gas_write(g_pm.plat, &g_pm.fadt.pm1a_cnt, 0, 16,
                       acpi_gas_read(g_pm.plat, &g_pm.fadt.pm1a_cnt, 0, 16) | 4u);
    return 1;
}

static void (*g_log)(const char *);
static void x_log(const char *s)
{
    g_log(s);
}

static aml_plat_t g_ap;

static const void *map_tables(void *ctx, uint64_t phys, uint64_t len)
{
    (void) ctx;
    return (const void *) g_hp->mmio_map(g_hp->ctx, phys, len);
}

/* ---- QEMU fw_cfg (test hooks only) -------------------------------------------- */
#define FWCFG_SEL  0x510
#define FWCFG_DATA 0x511

static int fwcfg_present(void)
{
    outw(FWCFG_SEL, 0);
    return inb(FWCFG_DATA) == 'Q' && inb(FWCFG_DATA) == 'E' && inb(FWCFG_DATA) == 'M' &&
           inb(FWCFG_DATA) == 'U';
}

/* First byte of fw_cfg file `name`, or 0 when absent. */
static char fwcfg_flag(const char *name)
{
    outw(FWCFG_SEL, 0x19);
    uint32_t n = 0;
    for (int i = 0; i < 4; i++) n = (n << 8) | inb(FWCFG_DATA);
    if (n > 256) n = 256;
    for (uint32_t e = 0; e < n; e++) {
        uint8_t ent[64];
        for (int i = 0; i < 64; i++) ent[i] = inb(FWCFG_DATA);
        int same = 1;
        for (int i = 0; i < 56 && same; i++) {
            if ((char) ent[8 + i] != name[i]) same = 0;
            if (!name[i]) break;
        }
        if (!same) continue;
        uint32_t size = ((uint32_t) ent[0] << 24) | ((uint32_t) ent[1] << 16) |
                        ((uint32_t) ent[2] << 8) | ent[3];
        if (!size) return 0;
        outw(FWCFG_SEL, (uint16_t) ((ent[4] << 8) | ent[5]));
        return (char) inb(FWCFG_DATA);
    }
    return 0;
}

static int oem_is_bochs(acpi_state_t *st)
{
    acpi_header_t *f = acpi_find_table(st, "FACP");
    static const char b[6] = {'B', 'O', 'C', 'H', 'S', ' '};
    if (!f) return 0;
    for (int i = 0; i < 6; i++)
        if (f->oem_id[i] != b[i]) return 0;
    return 1;
}

static void put_hex(char *o, uint64_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    char t[16];
    int n = 0, i = 0;
    do {
        t[n++] = hx[v & 15];
        v >>= 4;
    } while (v);
    o[i++] = '0';
    o[i++] = 'x';
    while (n) o[i++] = t[--n];
    o[i] = 0;
}

static void log_kv(const char *k, uint64_t v)
{
    char line[96], hx[24];
    uint32_t n = 0;
    while (*k && n < 70) line[n++] = *k++;
    put_hex(hx, v);
    for (int i = 0; hx[i] && n < 95; i++) line[n++] = hx[i];
    line[n] = 0;
    g_log(line);
}

/* ---- boot -------------------------------------------------------------------- */
#define AML_ARENA (3u << 20)
static uint8_t g_arena[AML_ARENA] __attribute__((aligned(16)));
static aml_ctx_t g_aml;
static acpi_state_t g_st;

static void wbinvd(void)
{
    __asm__ __volatile__("wbinvd" : : : "memory");
}

int acpi_x86_boot(const hw_plat_t *hp, uint64_t tsc_per_us, void (*log)(const char *))
{
    g_hp = hp;
    g_tsc_per_us = tsc_per_us;
    g_log = log;

    /* RSDP: the UEFI configuration table via the handoff record, else the
     * legacy BIOS area scan. */
    uint64_t rsdp = 0;
    const char *from = "UEFI configuration table";
    {
        const volatile zxv_x86_bootinfo_t *bi =
            (const volatile zxv_x86_bootinfo_t *) (uintptr_t) ZXV_X86_BOOTINFO_ADDR;
        if (bi->reserved[0] == ZXV_BOOTINFO_RSDP_MAGIC &&
            bi->reserved[2] == (ZXV_BOOTINFO_RSDP_MAGIC ^ bi->reserved[1]))
            rsdp = bi->reserved[1];
    }
    if (!rsdp) {
        rsdp = acpi_find_rsdp_legacy();
        from = "legacy BIOS scan";
    }
    if (!rsdp) {
        log("[ACPI] no RSDP (no UEFI handoff, nothing in the BIOS area) -- ACPI skipped");
        return 0;
    }
    int nt = acpi_init_from_rsdp(&g_st, rsdp, map_tables, 0);
    if (nt <= 0) {
        log_kv("[ACPI] RSDP unusable, status ", (uint64_t) (int64_t) nt);
        return 0;
    }
    {
        char line[128];
        uint32_t n = 0;
        const char *a = "[ACPI] RSDP from ";
        while (*a) line[n++] = *a++;
        while (*from) line[n++] = *from++;
        a = g_st.rsdp_rev >= 2 ? ", XSDT, tables: " : ", RSDT, tables: ";
        while (*a) line[n++] = *a++;
        for (uint32_t i = 0; i < g_st.num_tables && n + 6 < sizeof line; i++) {
            for (int k = 0; k < 4; k++) {
                char ch = g_st.tables[i]->signature[k];
                line[n++] = (ch >= 0x20 && ch < 0x7F) ? ch : '?';
            }
            line[n++] = ' ';
        }
        line[n] = 0;
        log(line);
    }

    g_ap.ctx = 0;
    g_ap.mmio_map = x_map;
    g_ap.io_read = x_ior;
    g_ap.io_write = x_iow;
    g_ap.pci_read = x_pcir;
    g_ap.pci_write = x_pciw;
    g_ap.delay_us = x_delay;
    g_ap.timer_100ns = x_timer;
    g_ap.global_lock = x_glock;
    g_ap.log = x_log;
    if (aml_init(&g_aml, &g_ap, g_arena, AML_ARENA)) {
        log("[ACPI] AML arena too small -- ACPI skipped");
        return 0;
    }
    /* The FACS global lock, before any AML runs. */
    {
        acpi_header_t *fh = acpi_find_table(&g_st, "FACP");
        acpi_fadt_info_t fi;
        if (fh && !acpi_parse_fadt((const uint8_t *) fh, fh->length, &fi) && fi.facs) {
            const volatile uint8_t *fc =
                (const volatile uint8_t *) hp->mmio_map(hp->ctx, fi.facs, 64);
            if (fc && fc[0] == 'F' && fc[1] == 'A' && fc[2] == 'C' && fc[3] == 'S')
                g_glock = (volatile uint32_t *) (uintptr_t) (fi.facs + 16);
        }
    }
    if (!acpi_pm_init(&g_pm, &g_aml, &g_ap, &g_st)) {
        acpi_pm_report(&g_pm);
        return 0;
    }
    g_pm.flush_caches = wbinvd;
    int er = acpi_pm_enable(&g_pm);
    if (er) log_kv("[ACPI] could not switch to ACPI mode, status ", (uint64_t) (int64_t) er);
    acpi_pm_report(&g_pm);

    /* Test hooks: QEMU only. */
    if (!oem_is_bochs(&g_st) || !fwcfg_present()) return 1;
    if (fwcfg_flag("opt/zxv/acpi_pwrbtn") == '1') {
        log("[ACPI] fw_cfg acpi_pwrbtn: waiting for the power button (60 s)");
        acpi_poll_events(&g_pm); /* drop anything stale */
        uint32_t ms = 0;
        while (!(acpi_poll_events(&g_pm) & ACPI_EV_PWRBTN)) {
            if (ms >= 60000) {
                log("[ACPI] no power button press within 60 s");
                return 1;
            }
            hp->delay_us(hp->ctx, 10000);
            ms += 10;
        }
        log("[ACPI] power button pressed -> S5");
        log("[ACPI] entering S5 (power off)");
        er = acpi_poweroff(&g_pm);
        log_kv("[ACPI] S5 did not take effect, status ", (uint64_t) (int64_t) er);
        return 1;
    }
    if (fwcfg_flag("opt/zxv/acpi_s3") == '1') {
        log("[ACPI] fw_cfg acpi_s3: entering S3 (suspend to RAM)");
        er = acpi_enter_sleep(&g_pm, 3);
        log_kv(er ? "[ACPI] S3 returned without a wake, status " : "[ACPI] S3 resumed, status ",
               (uint64_t) (int64_t) er);
    }
    if (fwcfg_flag("opt/zxv/acpi_poweroff") == '1') {
        log("[ACPI] fw_cfg acpi_poweroff: entering S5 (power off)");
        er = acpi_poweroff(&g_pm);
        log_kv("[ACPI] S5 did not take effect, status ", (uint64_t) (int64_t) er);
    }
    return 1;
}
