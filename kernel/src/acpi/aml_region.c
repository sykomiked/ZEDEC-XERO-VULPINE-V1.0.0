/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aml_region.c — operation regions, field units and the embedded controller.
 *
 * A field is a run of bits inside a region. Reading it means: pick the access
 * width (the field's AccessType, or for AnyAcc the narrowest aligned width
 * that holds it), walk the aligned units the field touches, read each through
 * the region's address space, and splice the bits. Writing does the reverse;
 * a unit the field only partly covers is completed by the update rule:
 * Preserve reads it first, WriteAsOnes / WriteAsZeros fill the rest.
 * IndexField writes the unit's byte offset into its index field and moves the
 * data through its data field; BankField selects its bank first.
 *
 * Address spaces: SystemMemory (mapped once per region through the platform),
 * SystemIO, PCI_Config (device from _ADR, bus from _BBN and the bridges on
 * the way), EmbeddedControl (the ACPI EC protocol over two ports), SystemCMOS,
 * and the internal table space of DataRegion. Others answer AML_EUNSUP.
 * Every access is checked against the region's length first. */
#include "aml_int.h"

uint32_t aml_rd_le(const uint8_t *p, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n && i < 4; i++) v |= (uint32_t) p[i] << (8 * i);
    return v;
}

static uint64_t ones_mask(uint32_t n)
{
    return n >= 64 ? ~0ULL : ((1ULL << n) - 1);
}

/* ---- embedded controller -------------------------------------------------------- */
#define EC_OBF     0x01
#define EC_IBF     0x02
#define EC_SCI_EVT 0x20
#define EC_RD      0x80
#define EC_WR      0x81
#define EC_QR      0x84
#define EC_POLL_US 10
#define EC_TIMEOUT 20000 /* us per handshake step */

static int ec_wait(aml_ctx_t *c, uint8_t mask, uint8_t want)
{
    const aml_plat_t *p = c->plat;
    for (uint32_t t = 0; t <= EC_TIMEOUT; t += EC_POLL_US) {
        if (((uint8_t) p->io_read(p->ctx, c->ec.cmd_port, 8) & mask) == want) return AML_OK;
        if (p->delay_us) p->delay_us(p->ctx, EC_POLL_US);
    }
    c->ec.timeouts++;
    return AML_EIO;
}

static int ec_ready(aml_ctx_t *c)
{
    const aml_plat_t *p = c->plat;
    if (!c->ec.cmd_port || !p || !p->io_read || !p->io_write) return AML_EIO;
    if (c->ec.timeouts >= 8) return AML_EIO; /* gave up on a dead controller */
    return AML_OK;
}

int aml_ec_read(aml_ctx_t *c, uint8_t addr, uint8_t *val)
{
    const aml_plat_t *p = c->plat;
    int r = ec_ready(c);
    if (!r) r = ec_wait(c, EC_IBF, 0);
    if (!r) p->io_write(p->ctx, c->ec.cmd_port, 8, EC_RD);
    if (!r) r = ec_wait(c, EC_IBF, 0);
    if (!r) p->io_write(p->ctx, c->ec.data_port, 8, addr);
    if (!r) r = ec_wait(c, EC_OBF, EC_OBF);
    if (!r) *val = (uint8_t) p->io_read(p->ctx, c->ec.data_port, 8);
    return r;
}

int aml_ec_write(aml_ctx_t *c, uint8_t addr, uint8_t val)
{
    const aml_plat_t *p = c->plat;
    int r = ec_ready(c);
    if (!r) r = ec_wait(c, EC_IBF, 0);
    if (!r) p->io_write(p->ctx, c->ec.cmd_port, 8, EC_WR);
    if (!r) r = ec_wait(c, EC_IBF, 0);
    if (!r) p->io_write(p->ctx, c->ec.data_port, 8, addr);
    if (!r) r = ec_wait(c, EC_IBF, 0);
    if (!r) p->io_write(p->ctx, c->ec.data_port, 8, val);
    if (!r) r = ec_wait(c, EC_IBF, 0);
    return r;
}

int aml_ec_query(aml_ctx_t *c)
{
    const aml_plat_t *p = c->plat;
    if (ec_ready(c)) return -1;
    if (!((uint8_t) p->io_read(p->ctx, c->ec.cmd_port, 8) & EC_SCI_EVT)) return -1;
    if (ec_wait(c, EC_IBF, 0)) return -1;
    p->io_write(p->ctx, c->ec.cmd_port, 8, EC_QR);
    if (ec_wait(c, EC_OBF, EC_OBF)) return -1;
    uint8_t q = (uint8_t) p->io_read(p->ctx, c->ec.data_port, 8);
    if (!q) return 0;
    static const char hx[] = "0123456789ABCDEF";
    char name[5] = {'_', 'Q', hx[q >> 4], hx[q & 15], 0};
    if (c->ec.dev) {
        aml_val_t ret;
        if (!aml_eval(c, c->ec.dev, name, 0, 0, &ret)) aml_val_free(c, &ret);
    }
    return q;
}

/* ---- PCI address of a PCI_Config region ------------------------------------------- */
static int is_root_bridge(aml_ctx_t *c, aml_node_t *n)
{
    return aml_dev_match_id(c, n, "PNP0A03") || aml_dev_match_id(c, n, "PNP0A08");
}

static int node_int(aml_ctx_t *c, aml_node_t *scope, const char *name, uint64_t *v)
{
    aml_node_t *n = aml_ns_child(scope, aml_rd_le((const uint8_t *) name, 4));
    aml_val_t r;
    if (!n) return AML_ENOENT;
    int e = aml_eval_node(c, n, 0, 0, &r);
    if (e) return e;
    e = aml_to_int(c, &r, v);
    aml_val_free(c, &r);
    return e;
}

/* bus/dev/fn of device node d; depth bounds the bridge walk. */
static int pci_addr(aml_ctx_t *c, aml_node_t *d, uint32_t depth, uint32_t *seg, uint32_t *bus,
                    uint32_t *dev, uint32_t *fn)
{
    uint64_t adr = 0, v;
    if (!d || depth > 16) return AML_EBOUNDS;
    if (is_root_bridge(c, d)) {
        *seg = node_int(c, d, "_SEG", &v) ? 0 : (uint32_t) v;
        *bus = node_int(c, d, "_BBN", &v) ? 0 : (uint32_t) v & 0xFF;
        *dev = node_int(c, d, "_ADR", &adr) ? 0 : (uint32_t) (adr >> 16) & 0x1F;
        *fn = (uint32_t) adr & 0x7;
        return AML_OK;
    }
    if (node_int(c, d, "_ADR", &adr)) {
        /* no address: inherit the parent's (a region in a non-PCI scope) */
        if (!d->parent || !d->parent->parent) {
            *seg = *bus = *dev = *fn = 0;
            return AML_OK;
        }
        return pci_addr(c, d->parent, depth + 1, seg, bus, dev, fn);
    }
    *dev = (uint32_t) (adr >> 16) & 0x1F;
    *fn = (uint32_t) adr & 0x7;
    aml_node_t *p = d->parent;
    if (!p || !p->parent) {
        *seg = 0;
        *bus = 0;
        return AML_OK;
    }
    uint32_t ps, pb, pd, pf;
    int r = pci_addr(c, p, depth + 1, &ps, &pb, &pd, &pf);
    if (r) return r;
    *seg = ps;
    if (is_root_bridge(c, p)) {
        *bus = pb;
    } else {
        /* p is a PCI-to-PCI bridge: our bus is its secondary bus number */
        const aml_plat_t *pl = c->plat;
        if (!pl || !pl->pci_read) return AML_EIO;
        *bus = pl->pci_read(pl->ctx, ps, pb, pd, pf, 0x19, 8) & 0xFF;
    }
    return AML_OK;
}

int aml_region_prepare(aml_ctx_t *c, aml_node_t *rg)
{
    int r;
    if (!rg || rg->v.type != AML_T_REGION) return AML_ETYPE;
    if (rg->x.r.busy) return AML_ELIMIT;
    if (!rg->x.r.ready) {
        rg->x.r.busy = 1;
        r = aml_region_eval_deferred(c, rg);
        rg->x.r.busy = 0;
        if (r) return r;
    }
    if (rg->x.r.space == AML_SPACE_PCI && !rg->x.r.pci_ok) {
        rg->x.r.busy = 1;
        r = pci_addr(c, rg->parent, 0, &rg->x.r.seg, &rg->x.r.bus, &rg->x.r.dev, &rg->x.r.fn);
        rg->x.r.busy = 0;
        if (r) return r;
        rg->x.r.pci_ok = 1;
    }
    return AML_OK;
}

/* ---- region access -------------------------------------------------------------------- */
int aml_region_access(aml_ctx_t *c, aml_node_t *rg, uint64_t off, uint32_t w, int write,
                      uint64_t *val)
{
    const aml_plat_t *p = c->plat;
    int r = aml_region_prepare(c, rg);
    if (r) return r;
    if (w != 1 && w != 2 && w != 4 && w != 8) return AML_EARG;
    if (off > rg->x.r.len || w > rg->x.r.len - off) return AML_EBOUNDS;
    uint64_t addr = rg->x.r.base + off;
    uint64_t v = write ? *val & ones_mask(w * 8) : 0;
    switch (rg->x.r.space) {
    case AML_SPACE_MEM:
    case AML_SPACE_TABLE: {
        if (!rg->x.r.map) {
            if (rg->x.r.space == AML_SPACE_TABLE || !p || !p->mmio_map) return AML_EIO;
            if (!rg->x.r.len) return AML_EBOUNDS;
            rg->x.r.map = (volatile uint8_t *) p->mmio_map(p->ctx, rg->x.r.base, rg->x.r.len);
            if (!rg->x.r.map) return AML_EIO;
        }
        volatile uint8_t *m = rg->x.r.map + off;
        if ((uintptr_t) m & (w - 1)) {
            /* a misaligned unit (odd region base): byte by byte, never a
             * misaligned wide access */
            for (uint32_t i = 0; i < w; i++) {
                if (write)
                    m[i] = (uint8_t) (v >> (8 * i));
                else
                    v |= (uint64_t) m[i] << (8 * i);
            }
        } else if (write) {
            if (w == 1) *m = (uint8_t) v;
            if (w == 2) *(volatile uint16_t *) m = (uint16_t) v;
            if (w == 4) *(volatile uint32_t *) m = (uint32_t) v;
            if (w == 8) *(volatile uint64_t *) m = v;
        } else {
            if (w == 1) v = *m;
            if (w == 2) v = *(volatile uint16_t *) m;
            if (w == 4) v = *(volatile uint32_t *) m;
            if (w == 8) v = *(volatile uint64_t *) m;
        }
        break;
    }
    case AML_SPACE_IO:
        if (!p || !p->io_read || !p->io_write) return AML_EIO;
        if (addr > 0xFFFF || addr + w > 0x10000) return AML_EBOUNDS;
        for (uint32_t done = 0; done < w; done += 4) {
            uint32_t part = w - done >= 4 ? 4 : w - done;
            uint32_t port = (uint32_t) addr + done;
            if (write)
                p->io_write(p->ctx, port, part * 8, (uint32_t) (v >> (8 * done)));
            else
                v |= (uint64_t) (p->io_read(p->ctx, port, part * 8) & ones_mask(part * 8))
                     << (8 * done);
        }
        break;
    case AML_SPACE_PCI:
        if (!p || !p->pci_read || !p->pci_write) return AML_EIO;
        if (addr + w > 0x1000) return AML_EBOUNDS;
        for (uint32_t done = 0; done < w; done += 4) {
            uint32_t part = w - done >= 4 ? 4 : w - done;
            uint32_t o = (uint32_t) addr + done;
            if (write)
                p->pci_write(p->ctx, rg->x.r.seg, rg->x.r.bus, rg->x.r.dev, rg->x.r.fn, o, part * 8,
                             (uint32_t) (v >> (8 * done)));
            else
                v |= (uint64_t) (p->pci_read(p->ctx, rg->x.r.seg, rg->x.r.bus, rg->x.r.dev,
                                             rg->x.r.fn, o, part * 8) &
                                 ones_mask(part * 8))
                     << (8 * done);
        }
        break;
    case AML_SPACE_EC:
        if (addr + w > 0x100) return AML_EBOUNDS;
        for (uint32_t i = 0; i < w; i++) {
            uint8_t b = (uint8_t) (v >> (8 * i));
            r = write ? aml_ec_write(c, (uint8_t) (addr + i), b)
                      : aml_ec_read(c, (uint8_t) (addr + i), &b);
            if (r) return r;
            if (!write) v |= (uint64_t) b << (8 * i);
        }
        break;
    case AML_SPACE_CMOS:
        if (!p || !p->io_read || !p->io_write) return AML_EIO;
        if (addr + w > 0x100) return AML_EBOUNDS;
        for (uint32_t i = 0; i < w; i++) {
            uint32_t a = (uint32_t) addr + i, ip = a < 0x80 ? 0x70 : 0x72;
            p->io_write(p->ctx, ip, 8, a & 0x7F);
            if (write)
                p->io_write(p->ctx, ip + 1, 8, (uint32_t) (v >> (8 * i)) & 0xFF);
            else
                v |= (uint64_t) (p->io_read(p->ctx, ip + 1, 8) & 0xFF) << (8 * i);
        }
        break;
    default:
        return AML_EUNSUP;
    }
    if (!write) *val = v;
    return AML_OK;
}

/* ---- fields ---------------------------------------------------------------------------- */
static void put_bits(uint8_t *dst, uint32_t at, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint8_t m = (uint8_t) (1u << ((at + i) & 7));
        if ((v >> i) & 1)
            dst[(at + i) >> 3] |= m;
        else
            dst[(at + i) >> 3] &= (uint8_t) ~m;
    }
}

static uint64_t get_bits(const uint8_t *src, uint32_t srclen, uint32_t at, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t b = at + i;
        if ((b >> 3) < srclen && (src[b >> 3] >> (b & 7)) & 1) v |= 1ULL << i;
    }
    return v;
}

static uint32_t acc_width(const aml_node_t *f, uint32_t space)
{
    uint32_t t = f->x.f.flags & 0x0F;
    if (space == AML_SPACE_EC || space == AML_SPACE_CMOS) return 1;
    switch (t) {
    case AML_ACC_BYTE:
        return 1;
    case AML_ACC_WORD:
        return 2;
    case AML_ACC_DWORD:
        return 4;
    case AML_ACC_QWORD:
        return 8;
    case AML_ACC_BUF:
        return 1;
    default:
        break;
    }
    /* AnyAcc: narrowest aligned unit holding the whole field, else dwords */
    uint32_t first = f->x.f.bitoff, last = f->x.f.bitoff + f->x.f.bitlen - 1;
    for (uint32_t w = 1; w <= 4; w *= 2)
        if (first / (8 * w) == last / (8 * w)) return w;
    return 4;
}

/* The region whose space decides the access width (index fields: the data
 * field's region; banks: the bank's region). */
static aml_node_t *field_region(const aml_node_t *f, uint32_t depth)
{
    if (!f || depth > 8) return 0;
    if (f->x.f.kind == AML_FIELD_INDEX) return field_region(f->x.f.data, depth + 1);
    return f->x.f.region;
}

static int unit_io(aml_ctx_t *c, aml_node_t *f, uint64_t off, uint32_t w, int write, uint64_t *v,
                   uint32_t depth);

/* Read or write a sub-field (the index/data/bank fields of another field). */
static int sub_io(aml_ctx_t *c, aml_node_t *f, int write, uint64_t *v, uint32_t depth)
{
    uint32_t len = f->x.f.bitlen;
    aml_node_t *rg = field_region(f, 0);
    if (!rg || depth > 8 || !len || len > 64) return AML_ETYPE;
    uint32_t w = acc_width(f, rg->x.r.space);
    uint32_t first = (f->x.f.bitoff / (8 * w)) * w;
    uint32_t last = ((f->x.f.bitoff + len - 1) / (8 * w)) * w;
    uint64_t out = 0;
    for (uint32_t u = first; u <= last; u += w) {
        uint32_t lo = u * 8 > f->x.f.bitoff ? u * 8 : f->x.f.bitoff;
        uint32_t hi = (u + w) * 8 < f->x.f.bitoff + len ? (u + w) * 8 : f->x.f.bitoff + len;
        uint32_t n = hi - lo, sh = lo - u * 8;
        uint64_t m = ones_mask(n) << sh, uv = 0;
        int r;
        if (write) {
            uint64_t piece = (*v >> (lo - f->x.f.bitoff)) & ones_mask(n);
            if (m != ones_mask(w * 8)) {
                uint32_t rule = (f->x.f.flags >> 5) & 3;
                if (rule == AML_UPD_PRESERVE) {
                    r = unit_io(c, f, u, w, 0, &uv, depth + 1);
                    if (r) return r;
                } else if (rule == AML_UPD_ONES) {
                    uv = ones_mask(w * 8);
                }
            }
            uv = (uv & ~m) | (piece << sh);
            r = unit_io(c, f, u, w, 1, &uv, depth + 1);
        } else {
            r = unit_io(c, f, u, w, 0, &uv, depth + 1);
            out |= ((uv >> sh) & ones_mask(n)) << (lo - f->x.f.bitoff);
        }
        if (r) return r;
    }
    if (!write) *v = out;
    return AML_OK;
}

static int unit_io(aml_ctx_t *c, aml_node_t *f, uint64_t off, uint32_t w, int write, uint64_t *v,
                   uint32_t depth)
{
    int r;
    if (depth > 8) return AML_ELIMIT;
    if (f->x.f.kind == AML_FIELD_INDEX) {
        uint64_t idx = off;
        r = sub_io(c, f->x.f.region, 1, &idx, depth + 1);
        if (r) return r;
        return sub_io(c, f->x.f.data, write, v, depth + 1);
    }
    if (f->x.f.kind == AML_FIELD_BANK) {
        uint64_t bank = f->x.f.bank_val;
        r = sub_io(c, f->x.f.data, 1, &bank, depth + 1);
        if (r) return r;
    }
    return aml_region_access(c, f->x.f.region, off, w, write, v);
}

static int lock(aml_ctx_t *c, aml_node_t *f, int acquire)
{
    if (!(f->x.f.flags & 0x10) || !c->plat || !c->plat->global_lock) return 1;
    if (acquire) {
        if (c->gl_count++) return 1;
        if (c->plat->global_lock(c->plat->ctx, 1)) return 1;
        c->gl_count--;
        return 0;
    }
    if (c->gl_count && --c->gl_count == 0) c->plat->global_lock(c->plat->ctx, 0);
    return 1;
}

/* Bits of field f to/from bytes buf (len bytes, little-endian bit order). */
static int field_xfer(aml_ctx_t *c, aml_node_t *f, uint8_t *buf, uint32_t blen, int write)
{
    aml_node_t *rg = field_region(f, 0);
    if (!rg || rg->v.type != AML_T_REGION) return AML_ETYPE;
    int r = aml_region_prepare(c, rg);
    if (r) return r;
    uint32_t w = acc_width(f, rg->x.r.space);
    uint32_t bo = f->x.f.bitoff, len = f->x.f.bitlen;
    if (!len) return AML_OK;
    uint32_t first = (bo / (8 * w)) * w, last = ((bo + len - 1) / (8 * w)) * w;
    if (!lock(c, f, 1)) return AML_EIO;
    for (uint32_t u = first; u <= last && !r; u += w) {
        uint32_t lo = u * 8 > bo ? u * 8 : bo;
        uint32_t hi = (u + w) * 8 < bo + len ? (u + w) * 8 : bo + len;
        uint32_t n = hi - lo, sh = lo - u * 8;
        uint64_t m = ones_mask(n) << sh, uv = 0;
        if (write) {
            uint64_t piece = get_bits(buf, blen, lo - bo, n);
            if (m != ones_mask(w * 8)) {
                uint32_t rule = (f->x.f.flags >> 5) & 3;
                if (rule == AML_UPD_PRESERVE)
                    r = unit_io(c, f, u, w, 0, &uv, 0);
                else if (rule == AML_UPD_ONES)
                    uv = ones_mask(w * 8);
            }
            if (!r) {
                uv = (uv & ~m) | (piece << sh);
                r = unit_io(c, f, u, w, 1, &uv, 0);
            }
        } else {
            r = unit_io(c, f, u, w, 0, &uv, 0);
            if (!r) put_bits(buf, lo - bo, uv >> sh, n);
        }
    }
    lock(c, f, 0);
    return r;
}

int aml_field_read(aml_ctx_t *c, aml_node_t *f, aml_val_t *out)
{
    uint32_t len = f->x.f.bitlen, nb = (len + 7) / 8;
    int r = aml_mkbuf(c, out, nb);
    if (r) return r;
    r = field_xfer(c, f, out->u.bytes, nb, 0);
    if (r) {
        aml_val_free(c, out);
        return r;
    }
    if (len <= (c->int64 ? 64u : 32u)) {
        uint64_t v = 0;
        for (uint32_t i = 0; i < nb; i++) v |= (uint64_t) out->u.bytes[i] << (8 * i);
        aml_val_free(c, out);
        *out = aml_int(v);
    }
    return AML_OK;
}

int aml_field_write(aml_ctx_t *c, aml_node_t *f, const aml_val_t *v)
{
    uint32_t nb = (f->x.f.bitlen + 7) / 8;
    aml_val_t b;
    int r = aml_mkbuf(c, &b, nb);
    if (r) return r;
    if (v->type == AML_T_INT || v->type == AML_T_REF) {
        uint64_t x;
        r = aml_to_int(c, v, &x);
        for (uint32_t i = 0; !r && i < nb && i < 8; i++) b.u.bytes[i] = (uint8_t) (x >> (8 * i));
    } else if (v->type == AML_T_BUF || v->type == AML_T_STR) {
        for (uint32_t i = 0; i < nb && i < v->len; i++) b.u.bytes[i] = v->u.bytes[i];
    } else {
        r = AML_ETYPE;
    }
    if (!r) r = field_xfer(c, f, b.u.bytes, nb, 1);
    aml_val_free(c, &b);
    return r;
}

/* ---- EC discovery ----------------------------------------------------------------- */
int aml_ec_init(aml_ctx_t *c, uint32_t data_port, uint32_t cmd_port, aml_node_t *dev)
{
    if (!c || !data_port || !cmd_port || data_port > 0xFFFF || cmd_port > 0xFFFF) return AML_EARG;
    c->ec.data_port = data_port;
    c->ec.cmd_port = cmd_port;
    c->ec.timeouts = 0;
    c->ec.dev = dev;
    if (dev) {
        uint64_t g;
        if (!aml_eval_int(c, dev, "_GPE", &g)) c->ec.gpe = (uint32_t) g;
        aml_node_t *reg = aml_ns_child(dev, aml_rd_le((const uint8_t *) "_REG", 4));
        if (reg) {
            aml_val_t a[2], ret;
            a[0] = aml_int(AML_SPACE_EC);
            a[1] = aml_int(1);
            if (!aml_eval_node(c, reg, a, 2, &ret)) aml_val_free(c, &ret);
        }
    }
    return AML_OK;
}

int aml_ec_probe(aml_ctx_t *c)
{
    aml_node_t *ec;
    aml_val_t crs;
    if (!aml_find_devices(c, "PNP0C09", &ec, 1)) return 0;
    if (aml_eval(c, ec, "_CRS", 0, 0, &crs)) return 0;
    uint32_t ports[2] = {0, 0}, np = 0, i = 0;
    if (crs.type == AML_T_BUF) {
        while (i < crs.len && np < 2) {
            uint8_t tag = crs.u.bytes[i];
            uint32_t l;
            if (tag & 0x80) {
                if (i + 3 > crs.len) break;
                l = 3u + aml_rd_le(crs.u.bytes + i + 1, 2);
            } else {
                l = 1u + (tag & 7u);
                if (((tag >> 3) & 0x0F) == 0x0F) break;
                if (i + l <= crs.len && (tag & 0xF8) == 0x40 && l >= 8) /* I/O port */
                    ports[np++] = aml_rd_le(crs.u.bytes + i + 2, 2);
                else if (i + l <= crs.len && (tag & 0xF8) == 0x48 && l >= 4) /* fixed I/O */
                    ports[np++] = aml_rd_le(crs.u.bytes + i + 1, 2) & 0x3FF;
            }
            if (l > crs.len - i) break;
            i += l;
        }
    }
    aml_val_free(c, &crs);
    if (np < 2) return 0;
    aml_ec_init(c, ports[0], ports[1], ec);
    return 1;
}
