/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* acpi_pm.c — FADT registers, sleep states, power button and GPE polling, and
 * the boot report, over the AML interpreter. See acpi_pm.h. */
#include "acpi_pm.h"

/* ---- log lines ---------------------------------------------------------------- */
typedef struct pline {
    char b[160];
    uint32_t n;
} pline_t;

static void pl_c(pline_t *l, char c)
{
    if (l->n + 1 < sizeof l->b) l->b[l->n++] = c;
    l->b[l->n] = 0;
}
static void pl_s(pline_t *l, const char *s)
{
    while (s && *s) pl_c(l, *s++);
}
static void pl_u(pline_t *l, uint64_t v)
{
    char t[24];
    int i = 0;
    do {
        t[i++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v);
    while (i) pl_c(l, t[--i]);
}
static void pl_x(pline_t *l, uint64_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    char t[20];
    int i = 0;
    pl_s(l, "0x");
    do {
        t[i++] = hx[v & 15];
        v >>= 4;
    } while (v);
    while (i) pl_c(l, t[--i]);
}
static void pl_init(pline_t *l, const char *s)
{
    l->n = 0;
    l->b[0] = 0;
    pl_s(l, s);
}
static uint32_t pl_emit(acpi_pm_t *pm, pline_t *l)
{
    if (pm->plat && pm->plat->log) pm->plat->log(l->b);
    return 1;
}

static uint32_t le(const uint8_t *p, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) v |= (uint32_t) p[i] << (8 * i);
    return v;
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t) le(p, 4) | ((uint64_t) le(p + 4, 4) << 32);
}

/* ---- FADT --------------------------------------------------------------------------- */
static void gas_legacy(acpi_gas_t *g, uint32_t addr, uint8_t len)
{
    g->space = 1;
    g->bit_width = (uint8_t) (len * 8);
    g->bit_offset = 0;
    g->access = 0;
    g->addr = addr;
}

static void gas_x(acpi_gas_t *g, const uint8_t *f, uint32_t len, uint32_t off, uint8_t blen)
{
    if (len < off + 12) return;
    uint64_t a = le64(f + off + 4);
    if (!a) return;
    g->space = f[off];
    g->bit_width = f[off + 1] ? f[off + 1] : (uint8_t) (blen * 8);
    g->bit_offset = f[off + 2];
    g->access = f[off + 3];
    g->addr = a;
}

int acpi_parse_fadt(const uint8_t *f, uint32_t len, acpi_fadt_info_t *o)
{
    if (!f || !o || len < 116) return -1;
    for (uint32_t i = 0; i < (uint32_t) sizeof *o; i++) ((uint8_t *) o)[i] = 0;
    if (f[0] != 'F' || f[1] != 'A' || f[2] != 'C' || f[3] != 'P') return -1;
    uint32_t tl = le(f + 4, 4);
    if (tl < len) len = tl;
    if (len < 116) return -1;
    o->rev = f[8];
    o->facs = le(f + 36, 4);
    o->dsdt = le(f + 40, 4);
    o->sci_int = (uint16_t) le(f + 46, 2);
    o->smi_cmd = le(f + 48, 4);
    o->acpi_enable = f[52];
    o->acpi_disable = f[53];
    o->pm1_evt_len = f[88];
    o->pm1_cnt_len = f[89];
    o->gpe0_len = f[92];
    o->gpe1_len = f[93];
    o->gpe1_base = f[94];
    o->flags = le(f + 112, 4);
    gas_legacy(&o->pm1a_evt, le(f + 56, 4), o->pm1_evt_len);
    gas_legacy(&o->pm1b_evt, le(f + 60, 4), o->pm1_evt_len);
    gas_legacy(&o->pm1a_cnt, le(f + 64, 4), o->pm1_cnt_len);
    gas_legacy(&o->pm1b_cnt, le(f + 68, 4), o->pm1_cnt_len);
    gas_legacy(&o->pm_tmr, le(f + 76, 4), f[91]);
    gas_legacy(&o->gpe0, le(f + 80, 4), o->gpe0_len);
    gas_legacy(&o->gpe1, le(f + 84, 4), o->gpe1_len);
    if (len >= 129) {
        o->reset.space = f[116];
        o->reset.bit_width = f[117];
        o->reset.bit_offset = f[118];
        o->reset.access = f[119];
        o->reset.addr = le64(f + 120);
        o->reset_value = f[128];
    }
    if (len >= 140 && le64(f + 132)) o->facs = le64(f + 132);
    if (len >= 148 && le64(f + 140)) o->dsdt = le64(f + 140);
    gas_x(&o->pm1a_evt, f, len, 148, o->pm1_evt_len);
    gas_x(&o->pm1b_evt, f, len, 160, o->pm1_evt_len);
    gas_x(&o->pm1a_cnt, f, len, 172, o->pm1_cnt_len);
    gas_x(&o->pm1b_cnt, f, len, 184, o->pm1_cnt_len);
    gas_x(&o->pm_tmr, f, len, 208, f[91]);
    gas_x(&o->gpe0, f, len, 220, o->gpe0_len);
    gas_x(&o->gpe1, f, len, 232, o->gpe1_len);
    gas_x(&o->sleep_ctl, f, len, 244, 1);
    gas_x(&o->sleep_sts, f, len, 256, 1);
    return 0;
}

/* ---- registers ------------------------------------------------------------------------ */
uint32_t acpi_gas_read(const aml_plat_t *p, const acpi_gas_t *g, uint32_t off, uint32_t width)
{
    if (!p || !g || !g->addr) return 0;
    if (g->space == 1) return p->io_read ? p->io_read(p->ctx, (uint32_t) g->addr + off, width) : 0;
    if (g->space == 0 && p->mmio_map) {
        volatile uint8_t *m = (volatile uint8_t *) p->mmio_map(p->ctx, g->addr, off + width / 8);
        if (!m) return 0;
        if (width == 8) return m[off];
        if (width == 16) return *(volatile uint16_t *) (m + off);
        return *(volatile uint32_t *) (m + off);
    }
    return 0;
}

void acpi_gas_write(const aml_plat_t *p, const acpi_gas_t *g, uint32_t off, uint32_t width,
                    uint32_t v)
{
    if (!p || !g || !g->addr) return;
    if (g->space == 1) {
        if (p->io_write) p->io_write(p->ctx, (uint32_t) g->addr + off, width, v);
        return;
    }
    if (g->space == 0 && p->mmio_map) {
        volatile uint8_t *m = (volatile uint8_t *) p->mmio_map(p->ctx, g->addr, off + width / 8);
        if (!m) return;
        if (width == 8) m[off] = (uint8_t) v;
        else if (width == 16) *(volatile uint16_t *) (m + off) = (uint16_t) v;
        else *(volatile uint32_t *) (m + off) = v;
    }
}

static uint32_t pm1_half(const acpi_pm_t *pm)
{
    uint32_t h = pm->fadt.pm1_evt_len / 2;
    return h ? h : 2;
}

/* PM1 status (a | b), enable writes go to both blocks. */
static uint32_t pm1_sts(acpi_pm_t *pm)
{
    uint32_t v = acpi_gas_read(pm->plat, &pm->fadt.pm1a_evt, 0, 16);
    if (pm->fadt.pm1b_evt.addr) v |= acpi_gas_read(pm->plat, &pm->fadt.pm1b_evt, 0, 16);
    return v;
}

static void pm1_sts_clear(acpi_pm_t *pm, uint32_t bits)
{
    acpi_gas_write(pm->plat, &pm->fadt.pm1a_evt, 0, 16, bits);
    if (pm->fadt.pm1b_evt.addr) acpi_gas_write(pm->plat, &pm->fadt.pm1b_evt, 0, 16, bits);
}

static void pm1_en_set(acpi_pm_t *pm, uint32_t bits)
{
    uint32_t h = pm1_half(pm);
    uint32_t v = acpi_gas_read(pm->plat, &pm->fadt.pm1a_evt, h, 16);
    acpi_gas_write(pm->plat, &pm->fadt.pm1a_evt, h, 16, v | bits);
    if (pm->fadt.pm1b_evt.addr) {
        v = acpi_gas_read(pm->plat, &pm->fadt.pm1b_evt, h, 16);
        acpi_gas_write(pm->plat, &pm->fadt.pm1b_evt, h, 16, v | bits);
    }
}

/* ---- init -------------------------------------------------------------------------------- */
static int is_sig(const acpi_header_t *h, const char *s)
{
    return h->signature[0] == s[0] && h->signature[1] == s[1] && h->signature[2] == s[2] &&
           h->signature[3] == s[3];
}

static void log2s(acpi_pm_t *pm, const char *a, const char *b, int err)
{
    pline_t l;
    pl_init(&l, a);
    pl_s(&l, b);
    if (err) {
        pl_s(&l, ": ");
        pl_s(&l, aml_strerror(err));
    }
    pl_emit(pm, &l);
}

static int gpe_count_cb(aml_ctx_t *c, struct aml_node *n, uint32_t depth, void *user)
{
    char nm[5];
    (void) c;
    if (depth != 1) return depth ? 1 : 0;
    aml_node_name(n, nm);
    if (nm[0] == '_' && (nm[1] == 'L' || nm[1] == 'E') && aml_node_type(n) == AML_T_METHOD)
        (*(uint32_t *) user)++;
    return 1;
}

int acpi_pm_init(acpi_pm_t *pm, aml_ctx_t *aml, const aml_plat_t *plat, acpi_state_t *st)
{
    for (uint32_t i = 0; i < (uint32_t) sizeof *pm; i++) ((uint8_t *) pm)[i] = 0;
    pm->aml = aml;
    pm->plat = plat;
    if (!aml || !plat || !st) return 0;
    acpi_header_t *fh = acpi_find_table(st, "FACP");
    if (fh && !acpi_parse_fadt((const uint8_t *) fh, fh->length, &pm->fadt)) pm->have_fadt = 1;
    if (!pm->have_fadt) {
        log2s(pm, "[ACPI] ", "no usable FADT", 0);
        return 0;
    }
    if (pm->fadt.dsdt && plat->mmio_map) {
        const uint8_t *h = (const uint8_t *) plat->mmio_map(plat->ctx, pm->fadt.dsdt, 36);
        uint32_t dl = h ? le(h + 4, 4) : 0;
        const uint8_t *d = dl >= 36 && dl <= (16u << 20)
                               ? (const uint8_t *) plat->mmio_map(plat->ctx, pm->fadt.dsdt, dl)
                               : 0;
        int r = d ? aml_load_table(aml, d, dl) : AML_EIO;
        if (d) pm->loaded++;
        if (r) {
            pm->load_errors++;
            log2s(pm, "[ACPI] DSDT load reported errors", "", r);
        }
    }
    for (uint32_t i = 0; i < st->num_tables; i++) {
        acpi_header_t *t = st->tables[i];
        if (!is_sig(t, "SSDT") && !is_sig(t, "PSDT")) continue;
        int r = aml_load_table(aml, t, t->length);
        pm->loaded++;
        if (r) {
            pm->load_errors++;
            log2s(pm, "[ACPI] SSDT load reported errors", "", r);
        }
    }
    if (!pm->loaded) return 0;
    acpi_header_t *ecdt = acpi_find_table(st, "ECDT");
    if (ecdt && ecdt->length >= 66) {
        const uint8_t *e = (const uint8_t *) ecdt;
        char path[64];
        uint32_t k = 0;
        for (uint32_t i = 65; i < ecdt->length && e[i] && k + 1 < sizeof path; i++)
            path[k++] = (char) e[i];
        path[k] = 0;
        aml_ec_init(aml, (uint32_t) le64(e + 52), (uint32_t) le64(e + 40),
                    aml_lookup(aml, 0, path));
    }
    pm->inis = aml_init_devices(aml);
    if (!aml->ec.cmd_port) aml_ec_probe(aml);
    for (uint32_t s = 0; s <= 5; s++)
        pm->sleep_ok[s] = !aml_sleep_state(aml, s, &pm->typa[s], &pm->typb[s]);
    struct aml_node *gpe = aml_lookup(aml, 0, "\\_GPE");
    if (gpe) aml_walk(aml, gpe, gpe_count_cb, &pm->gpe_methods);
    return (int) pm->loaded;
}

static void arm_events(acpi_pm_t *pm);

int acpi_pm_enable(acpi_pm_t *pm)
{
    if (!pm->have_fadt || (pm->fadt.flags & ACPI_FADT_HW_REDUCED)) return 0;
    if (!(acpi_gas_read(pm->plat, &pm->fadt.pm1a_cnt, 0, 16) & ACPI_PM1_SCI_EN)) {
        if (!pm->fadt.smi_cmd || !pm->fadt.acpi_enable || !pm->plat->io_write) return AML_EUNSUP;
        pm->plat->io_write(pm->plat->ctx, pm->fadt.smi_cmd, 8, pm->fadt.acpi_enable);
        for (uint32_t t = 0; t < 3000; t++) {
            if (acpi_gas_read(pm->plat, &pm->fadt.pm1a_cnt, 0, 16) & ACPI_PM1_SCI_EN) break;
            if (pm->plat->delay_us) pm->plat->delay_us(pm->plat->ctx, 1000);
        }
        if (!(acpi_gas_read(pm->plat, &pm->fadt.pm1a_cnt, 0, 16) & ACPI_PM1_SCI_EN))
            return AML_EIO;
    }
    pm->acpi_mode = 1;
    arm_events(pm);
    return 0;
}

/* ---- sleep ---------------------------------------------------------------------------------- */
int acpi_enter_sleep(acpi_pm_t *pm, uint32_t state)
{
    aml_val_t a, ret;
    if (!pm || !pm->aml || state > 5 || state == 0) return AML_EARG;
    if (!pm->sleep_ok[state]) return AML_ENOENT;
    a = aml_int(state);
    int r = aml_eval(pm->aml, 0, "\\_PTS", &a, 1, &ret);
    if (!r) aml_val_free(pm->aml, &ret);
    else if (r != AML_ENOENT) log2s(pm, "[ACPI] _PTS failed", "", r);
    if (!aml_eval(pm->aml, 0, "\\_SI._SST", &a, 1, &ret)) aml_val_free(pm->aml, &ret);
    uint32_t ta = pm->typa[state], tb = pm->typb[state];
    if (pm->fadt.flags & ACPI_FADT_HW_REDUCED) {
        if (!pm->fadt.sleep_ctl.addr) return AML_EUNSUP;
        acpi_gas_write(pm->plat, &pm->fadt.sleep_sts, 0, 8, 0x80); /* clear WAK_STS */
        if (pm->flush_caches) pm->flush_caches();
        acpi_gas_write(pm->plat, &pm->fadt.sleep_ctl, 0, 8, ((ta & 7) << 2) | 0x20);
    } else {
        if (!pm->fadt.pm1a_cnt.addr) return AML_EUNSUP;
        pm1_sts_clear(pm, ACPI_PM1_WAK);
        uint32_t va = acpi_gas_read(pm->plat, &pm->fadt.pm1a_cnt, 0, 16) & ~0x3C00u;
        uint32_t vb = pm->fadt.pm1b_cnt.addr
                          ? acpi_gas_read(pm->plat, &pm->fadt.pm1b_cnt, 0, 16) & ~0x3C00u
                          : 0;
        va |= (ta & 7) << 10;
        vb |= (tb & 7) << 10;
        acpi_gas_write(pm->plat, &pm->fadt.pm1a_cnt, 0, 16, va);
        if (pm->fadt.pm1b_cnt.addr) acpi_gas_write(pm->plat, &pm->fadt.pm1b_cnt, 0, 16, vb);
        if (pm->flush_caches) pm->flush_caches();
        acpi_gas_write(pm->plat, &pm->fadt.pm1a_cnt, 0, 16, va | ACPI_SLP_EN);
        if (pm->fadt.pm1b_cnt.addr)
            acpi_gas_write(pm->plat, &pm->fadt.pm1b_cnt, 0, 16, vb | ACPI_SLP_EN);
    }
    /* Still running: S5 failed; S1-S4 have resumed when WAK_STS is set. */
    int woke = 0;
    for (uint32_t t = 0; t < 1000 && !woke; t++) {
        if (pm->fadt.flags & ACPI_FADT_HW_REDUCED)
            woke = (acpi_gas_read(pm->plat, &pm->fadt.sleep_sts, 0, 8) & 0x80) != 0;
        else
            woke = (pm1_sts(pm) & ACPI_PM1_WAK) != 0;
        if (!woke && pm->plat->delay_us) pm->plat->delay_us(pm->plat->ctx, 1000);
    }
    if (state == 5 || !woke) {
        log2s(pm, "[ACPI] sleep request did not take effect", "", 0);
        return AML_EIO;
    }
    if (!aml_eval(pm->aml, 0, "\\_WAK", &a, 1, &ret)) aml_val_free(pm->aml, &ret);
    return AML_OK;
}

int acpi_poweroff(acpi_pm_t *pm)
{
    return acpi_enter_sleep(pm, 5);
}

/* ---- events -------------------------------------------------------------------------------- */
static uint32_t gpe_dispatch(acpi_pm_t *pm, const acpi_gas_t *blk, uint32_t len, uint32_t base)
{
    static const char hx[] = "0123456789ABCDEF";
    uint32_t half = len / 2, n = 0;
    for (uint32_t i = 0; i < half && i < 32; i++) {
        uint32_t sts = acpi_gas_read(pm->plat, blk, i, 8);
        uint32_t en = acpi_gas_read(pm->plat, blk, half + i, 8);
        uint32_t fire = sts & en;
        for (uint32_t b = 0; b < 8; b++) {
            if (!(fire & (1u << b))) continue;
            uint32_t g = base + i * 8 + b;
            char lv[] = {'\\', '_', 'G', 'P', 'E', '.', '_', 'L', hx[(g >> 4) & 15], hx[g & 15], 0};
            aml_val_t ret;
            if (pm->aml->ec.cmd_port && g == pm->aml->ec.gpe) {
                acpi_gas_write(pm->plat, blk, i, 8, 1u << b);
                aml_ec_query(pm->aml);
                n++;
                continue;
            }
            if (aml_lookup(pm->aml, 0, lv)) { /* level: run, then clear */
                if (!aml_eval(pm->aml, 0, lv, 0, 0, &ret)) aml_val_free(pm->aml, &ret);
                acpi_gas_write(pm->plat, blk, i, 8, 1u << b);
            } else { /* edge (or no method): clear, then run */
                acpi_gas_write(pm->plat, blk, i, 8, 1u << b);
                lv[7] = 'E';
                if (!aml_eval(pm->aml, 0, lv, 0, 0, &ret)) aml_val_free(pm->aml, &ret);
            }
            n++;
        }
    }
    return n;
}

uint32_t acpi_poll_events(acpi_pm_t *pm)
{
    uint32_t ev = 0;
    if (!pm || !pm->have_fadt || !pm->aml) return 0;
    if (!(pm->fadt.flags & ACPI_FADT_HW_REDUCED)) {
        uint32_t s = pm1_sts(pm);
        if (s & ACPI_PM1_PWRBTN) {
            pm1_sts_clear(pm, ACPI_PM1_PWRBTN);
            ev |= ACPI_EV_PWRBTN;
        }
    }
    if (pm->fadt.gpe0.addr && pm->fadt.gpe0_len &&
        gpe_dispatch(pm, &pm->fadt.gpe0, pm->fadt.gpe0_len, 0))
        ev |= ACPI_EV_GPE;
    if (pm->fadt.gpe1.addr && pm->fadt.gpe1_len &&
        gpe_dispatch(pm, &pm->fadt.gpe1, pm->fadt.gpe1_len, pm->fadt.gpe1_base))
        ev |= ACPI_EV_GPE;
    aml_notify_t nt;
    while (aml_notify_pop(pm->aml, &nt)) {
        ev |= ACPI_EV_NOTIFY;
        if (nt.value == 0x80 && aml_dev_match_id(pm->aml, nt.node, "PNP0C0C"))
            ev |= ACPI_EV_PWRBTN;
        if (nt.value == 0x80 && aml_dev_match_id(pm->aml, nt.node, "PNP0C0D")) ev |= ACPI_EV_LID;
    }
    return ev;
}

/* Arm the fixed power button and every GPE that has a method. */
static void arm_events(acpi_pm_t *pm)
{
    if (!(pm->fadt.flags & ACPI_FADT_HW_REDUCED) && !(pm->fadt.flags & ACPI_FADT_PWR_BUTTON)) {
        pm1_sts_clear(pm, ACPI_PM1_PWRBTN);
        pm1_en_set(pm, ACPI_PM1_PWRBTN);
    }
}

/* ---- report --------------------------------------------------------------------------------- */
typedef struct scan {
    uint32_t thermal, procs, proc_objs, pss_first, cpc_first, pss_any, cpc_any;
    uint32_t bcl_devs, bcl_levels;
    struct aml_node *tz[4];
} scan_t;

static int scan_cb(aml_ctx_t *c, struct aml_node *n, uint32_t depth, void *user)
{
    scan_t *s = (scan_t *) user;
    (void) depth;
    uint32_t t = aml_node_type(n);
    if (t == AML_T_THERMAL) {
        if (s->thermal < 4) s->tz[s->thermal] = n;
        s->thermal++;
    }
    int cpu = t == AML_T_PROC || (t == AML_T_DEVICE && aml_dev_match_id(c, n, "ACPI0007"));
    if (cpu) {
        int pss = aml_cpu_pss(c, n, 0, 0), cpc = aml_cpu_cpc(c, n, 0);
        if (!s->procs) {
            s->pss_first = pss > 0 ? (uint32_t) pss : 0;
            s->cpc_first = cpc > 0 ? (uint32_t) cpc : 0;
        }
        if (pss > 0) s->pss_any++;
        if (cpc > 0) s->cpc_any++;
        s->procs++;
        if (t == AML_T_PROC) s->proc_objs++;
    }
    if (t == AML_T_DEVICE && aml_lookup(c, n, "_BCL")) {
        int l = aml_bcl(c, n, 0, 0, 0, 0);
        if (l > 0) {
            if (!s->bcl_devs) s->bcl_levels = (uint32_t) l;
            s->bcl_devs++;
        }
    }
    return 0;
}

uint32_t acpi_pm_report(acpi_pm_t *pm)
{
    pline_t l;
    uint32_t lines = 0;
    aml_ctx_t *c = pm->aml;
    if (!pm->loaded || !c) {
        pl_init(&l, "[ACPI] no AML namespace (no FADT/DSDT)");
        return pl_emit(pm, &l);
    }
    aml_stats_t st = aml_stats(c);
    pl_init(&l, "[ACPI OK] namespace: ");
    pl_u(&l, st.objects);
    pl_s(&l, " objects, ");
    pl_u(&l, st.methods);
    pl_s(&l, " methods, DSDT rev ");
    pl_u(&l, c->dsdt_rev);
    pl_s(&l, " (");
    pl_u(&l, pm->loaded);
    pl_s(&l, " tables, ");
    pl_u(&l, st.errors);
    pl_s(&l, " load errors, ");
    pl_u(&l, pm->inis);
    pl_s(&l, " _INI, heap ");
    pl_u(&l, st.heap_used / 1024);
    pl_s(&l, " KB)");
    lines += pl_emit(pm, &l);

    for (uint32_t s = 3; s <= 5; s += 2) {
        if (s == 5) pl_init(&l, "[ACPI] \\_S5_ ");
        else pl_init(&l, "[ACPI] S3 ");
        if (pm->sleep_ok[s]) {
            if (s == 3) pl_s(&l, "supported ");
            pl_s(&l, "SLP_TYPa=");
            pl_x(&l, pm->typa[s]);
            pl_s(&l, " SLP_TYPb=");
            pl_x(&l, pm->typb[s]);
        } else {
            pl_s(&l, "absent");
        }
        lines += pl_emit(pm, &l);
    }

    struct aml_node *dev[8];
    uint32_t npb = aml_find_devices(c, "PNP0C0C", dev, 8);
    pl_init(&l, "[ACPI] power button: ");
    if (pm->fadt.flags & ACPI_FADT_HW_REDUCED) pl_s(&l, "hardware-reduced platform, ");
    else if (!(pm->fadt.flags & ACPI_FADT_PWR_BUTTON)) pl_s(&l, "fixed feature (PM1 PWRBTN), ");
    pl_s(&l, "control-method devices PNP0C0C: ");
    pl_u(&l, npb);
    pl_s(&l, ", GPE methods: ");
    pl_u(&l, pm->gpe_methods);
    lines += pl_emit(pm, &l);

    uint32_t nb = aml_find_devices(c, "PNP0C0A", dev, 8);
    pl_init(&l, "[ACPI] battery PNP0C0A: ");
    pl_u(&l, nb);
    for (uint32_t i = 0; i < nb && i < 2; i++) {
        aml_battery_t b;
        if (aml_battery_read(c, dev[i], &b)) {
            pl_s(&l, ", one unreadable");
            continue;
        }
        pl_s(&l, b.present ? ", present " : ", absent");
        if (b.present && b.percent <= 100) {
            pl_u(&l, b.percent);
            pl_s(&l, "%");
        }
    }
    lines += pl_emit(pm, &l);

    uint32_t nl = aml_find_devices(c, "PNP0C0D", dev, 8);
    pl_init(&l, "[ACPI] lid PNP0C0D: ");
    pl_u(&l, nl);
    if (nl) {
        uint32_t open;
        if (!aml_lid_read(c, dev[0], &open)) pl_s(&l, open ? " (open)" : " (closed)");
    }
    lines += pl_emit(pm, &l);

    scan_t sc;
    for (uint32_t i = 0; i < (uint32_t) sizeof sc; i++) ((uint8_t *) &sc)[i] = 0;
    aml_walk(c, 0, scan_cb, &sc);
    pl_init(&l, "[ACPI] thermal zones: ");
    pl_u(&l, sc.thermal);
    for (uint32_t i = 0; i < sc.thermal && i < 2; i++) {
        aml_thermal_t t;
        if (aml_thermal_read(c, sc.tz[i], &t)) continue;
        int32_t dc = aml_dk_to_dc(t.tmp);
        pl_s(&l, ", _TMP ");
        if (dc < 0) {
            pl_c(&l, '-');
            dc = -dc;
        }
        pl_u(&l, (uint32_t) dc / 10);
        pl_c(&l, '.');
        pl_u(&l, (uint32_t) dc % 10);
        pl_s(&l, " C");
        if (t.crt) {
            pl_s(&l, " _CRT ");
            pl_u(&l, t.crt);
            pl_s(&l, " dK");
        }
    }
    lines += pl_emit(pm, &l);

    pl_init(&l, "[ACPI] processors: ");
    pl_u(&l, sc.procs);
    pl_s(&l, " (Processor() objects ");
    pl_u(&l, sc.proc_objs);
    pl_s(&l, "), _PSS states ");
    pl_u(&l, sc.pss_first);
    pl_s(&l, " on #0 (");
    pl_u(&l, sc.pss_any);
    pl_s(&l, " with _PSS), _CPC entries ");
    pl_u(&l, sc.cpc_first);
    pl_s(&l, " (");
    pl_u(&l, sc.cpc_any);
    pl_s(&l, " with _CPC)");
    lines += pl_emit(pm, &l);

    pl_init(&l, "[ACPI] backlight: ");
    pl_u(&l, sc.bcl_devs);
    pl_s(&l, " output(s) with _BCL, ");
    pl_u(&l, sc.bcl_levels);
    pl_s(&l, " levels");
    lines += pl_emit(pm, &l);

    pl_init(&l, "[ACPI] embedded controller: ");
    if (c->ec.cmd_port) {
        pl_s(&l, "data ");
        pl_x(&l, c->ec.data_port);
        pl_s(&l, " cmd ");
        pl_x(&l, c->ec.cmd_port);
    } else {
        pl_s(&l, "none");
    }
    pl_s(&l, ", ACPI mode ");
    pl_s(&l, pm->acpi_mode ? "on" : "off");
    lines += pl_emit(pm, &l);
    return lines;
}
