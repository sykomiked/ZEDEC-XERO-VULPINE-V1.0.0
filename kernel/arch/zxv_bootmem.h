/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_bootmem.h — early boot memory taken from the firmware's own map.
 *
 * Why it exists. The x86_64 kernel used to keep its large buffers (the vino
 * ledger, the triple ledger, the RAM disk, the DMA pool: about 86 MB) in BSS.
 * BSS sits in one fixed window starting at 1 MB, so the kernel claimed
 * 1 MB..91 MB whatever the machine had there; OVMF keeps ACPI NVS at 8 MB, so
 * the kernel was overwriting firmware memory even under QEMU (handoff v2
 * reported it). Here those buffers come from ranges the firmware itself
 * called usable, after the kernel's end, so the fixed window shrinks to the
 * image plus small statics.
 *
 * Evidence, in order: the stub's handoff v2 map (ZXH_MEM_USABLE only), else
 * the Multiboot1 memory map (type 1 only, x86_64) or the device tree's memory
 * nodes (arm64). With neither the table stays
 * empty and every take answers NULL: no evidence means no memory is handed
 * out, rather than guessing what lies past the kernel.
 *
 * Header-only and freestanding (no libc): the kernel and the host test
 * include it as is. A bump allocator: early buffers live for the whole boot
 * and are never freed; the frame allocator (kernel/src/pmm) takes over what
 * remains, by asking zxb_remaining. Not thread-safe (boot is single CPU). */
#ifndef ZXV_BOOTMEM_H
#define ZXV_BOOTMEM_H

#include <stdint.h>

#include "zxv_handoff.h"

#define ZXB_MAX  32u
#define ZXB_PAGE 4096u

typedef struct {
    uint64_t lo, hi; /* [lo, hi), page aligned */
} zxb_range_t;

typedef struct {
    zxb_range_t r[ZXB_MAX];
    uint32_t n;
    uint32_t dropped; /* ranges lost to a full table */
    uint64_t taken;   /* bytes handed out */
} zxb_t;

static inline uint64_t zxb_up(uint64_t a)
{
    return (a + (ZXB_PAGE - 1u)) & ~(uint64_t) (ZXB_PAGE - 1u);
}

static inline uint64_t zxb_down(uint64_t a)
{
    return a & ~(uint64_t) (ZXB_PAGE - 1u);
}

/* Add [base, base+len) clipped to [floor, ceil). Pages only: the ends round
 * inward, so a partial page is never handed out. */
static inline void zxb_add(zxb_t *b, uint64_t base, uint64_t len, uint64_t floor, uint64_t ceil)
{
    uint64_t lo = base, hi = base + len;
    if (hi < lo) hi = ~(uint64_t) 0; /* wrapped: clip below */
    if (lo < floor) lo = floor;
    if (hi > ceil) hi = ceil;
    lo = zxb_up(lo);
    hi = zxb_down(hi);
    if (hi <= lo) return;
    if (b->n >= ZXB_MAX) {
        b->dropped++;
        return;
    }
    b->r[b->n].lo = lo;
    b->r[b->n].hi = hi;
    b->n++;
}

/* Remove [lo, hi) from every range (an info block, a module, the handoff
 * page). A range split in two needs a free slot; without one the upper
 * part is dropped and counted, never kept by mistake. */
static inline void zxb_exclude(zxb_t *b, uint64_t lo, uint64_t hi)
{
    lo = zxb_down(lo);
    hi = zxb_up(hi);
    for (uint32_t i = 0; i < b->n; i++) {
        zxb_range_t *r = &b->r[i];
        if (hi <= r->lo || lo >= r->hi) continue;
        if (lo <= r->lo && hi >= r->hi) { /* fully covered */
            r->lo = r->hi;
            continue;
        }
        if (lo <= r->lo) {
            r->lo = hi;
        } else if (hi >= r->hi) {
            r->hi = lo;
        } else { /* split */
            uint64_t top = r->hi;
            r->hi = lo;
            if (b->n < ZXB_MAX) {
                b->r[b->n].lo = hi;
                b->r[b->n].hi = top;
                b->n++;
            } else {
                b->dropped++;
            }
        }
    }
}

/* Fill from handoff v2: usable memory only (never loader, ACPI, runtime or
 * reserved). Returns the number of ranges taken. */
static inline uint32_t zxb_from_handoff(zxb_t *b, const zxh_t *h, uint64_t floor, uint64_t ceil)
{
    uint32_t before = b->n;
    for (uint32_t i = 0; i < h->n_mem && i < ZXH_MAX_MEM; i++)
        if (h->mem[i].kind == ZXH_MEM_USABLE)
            zxb_add(b, h->mem[i].base, h->mem[i].pages * ZXB_PAGE, floor, ceil);
    return b->n - before;
}

/* Fill from a Multiboot1 memory map (info flags bit 6): entries are
 * {u32 size, u64 base, u64 len, u32 type}, each followed by `size` bytes
 * after its size field. Type 1 is available RAM. */
static inline uint32_t zxb_from_multiboot1(zxb_t *b, const uint8_t *map, uint32_t len,
                                           uint64_t floor, uint64_t ceil)
{
    uint32_t before = b->n, at = 0;
    while (at + 24u <= len) {
        const uint8_t *e = map + at;
        uint32_t size =
            (uint32_t) e[0] | (uint32_t) e[1] << 8 | (uint32_t) e[2] << 16 | (uint32_t) e[3] << 24;
        uint64_t base = 0, l = 0;
        for (int k = 7; k >= 0; k--) {
            base = base << 8 | e[4 + k];
            l = l << 8 | e[12 + k];
        }
        uint32_t type = (uint32_t) e[20] | (uint32_t) e[21] << 8 | (uint32_t) e[22] << 16 |
                        (uint32_t) e[23] << 24;
        if (type == 1u) zxb_add(b, base, l, floor, ceil);
        if (size < 20u) break; /* malformed: stop rather than loop */
        at += size + 4u;
    }
    return b->n - before;
}

/* Flattened device tree (devicetree spec v0.4, big-endian), the arm64 direct
 * boot's statement of RAM: the `reg` of each top-level memory node, less the
 * /memreserve/ entries and the tree itself. Only what that needs is read;
 * every offset is checked against totalsize, and `max` bounds what the
 * caller allows the tree to claim. Returns the number of ranges taken, or 0
 * when this is not a well-formed tree (no evidence, nothing handed out). */
#define ZXB_FDT_MAGIC 0xd00dfeedu

static inline uint32_t zxb_be32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static inline uint64_t zxb_be_cells(const uint8_t *p, uint32_t cells)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < cells; i++) v = v << 32 | zxb_be32(p + 4u * i);
    return v;
}

static inline int zxb_streq(const uint8_t *a, uint32_t alen, const char *b)
{
    uint32_t i = 0;
    for (; b[i]; i++)
        if (i >= alen || a[i] != (uint8_t) b[i]) return 0;
    return i < alen && a[i] == 0;
}

static inline uint32_t zxb_from_fdt(zxb_t *b, const uint8_t *fdt, uint32_t max, uint64_t floor,
                                    uint64_t ceil)
{
    if (max < 40u || zxb_be32(fdt) != ZXB_FDT_MAGIC) return 0;
    uint32_t total = zxb_be32(fdt + 4), off_s = zxb_be32(fdt + 8), off_str = zxb_be32(fdt + 12);
    uint32_t off_rsv = zxb_be32(fdt + 16), ver = zxb_be32(fdt + 20);
    uint32_t sz_str = zxb_be32(fdt + 32), sz_s = zxb_be32(fdt + 36);
    if (total > max || total < 40u || ver < 16u) return 0;
    if (off_s > total || sz_s > total - off_s || off_str > total || sz_str > total - off_str ||
        off_rsv > total || (off_s & 3u) || (off_rsv & 7u))
        return 0;

    uint32_t before = b->n, ac = 2, sc = 1; /* spec defaults for the root */
    uint32_t depth = 0, in_mem = 0, at = off_s, end = off_s + sz_s;
    while (at + 4u <= end) {
        uint32_t tok = zxb_be32(fdt + at);
        at += 4u;
        if (tok == 1u) { /* BEGIN_NODE: name, NUL, pad to 4 */
            const uint8_t *name = fdt + at;
            uint32_t n = 0;
            while (at + n < end && name[n]) n++;
            if (at + n >= end) return 0;
            depth++;
            if (depth == 2) /* "memory" or "memory@<addr>" */
                in_mem = n >= 6u && name[0] == 'm' && name[1] == 'e' && name[2] == 'm' &&
                         name[3] == 'o' && name[4] == 'r' && name[5] == 'y' &&
                         (n == 6u || name[6] == '@');
            at += (n + 4u) & ~3u;
        } else if (tok == 2u) { /* END_NODE */
            if (depth == 0) return 0;
            if (depth == 2) in_mem = 0;
            depth--;
        } else if (tok == 3u) { /* PROP: len, nameoff, value, pad */
            if (at + 8u > end) return 0;
            uint32_t len = zxb_be32(fdt + at), nameoff = zxb_be32(fdt + at + 4);
            at += 8u;
            if (len > end - at || nameoff >= sz_str) return 0;
            const uint8_t *pn = fdt + off_str + nameoff, *val = fdt + at;
            uint32_t pmax = sz_str - nameoff;
            if (depth == 1 && len == 4u && zxb_streq(pn, pmax, "#address-cells"))
                ac = zxb_be32(val);
            else if (depth == 1 && len == 4u && zxb_streq(pn, pmax, "#size-cells"))
                sc = zxb_be32(val);
            else if (depth == 2 && in_mem && zxb_streq(pn, pmax, "reg") && ac >= 1u && ac <= 2u &&
                     sc >= 1u && sc <= 2u) {
                uint32_t step = 4u * (ac + sc);
                for (uint32_t k = 0; k + step <= len; k += step)
                    zxb_add(b, zxb_be_cells(val + k, ac), zxb_be_cells(val + k + 4u * ac, sc),
                            floor, ceil);
            }
            at += (len + 3u) & ~3u;
        } else if (tok == 4u) { /* NOP */
        } else if (tok == 9u) { /* END */
            break;
        } else {
            return 0;
        }
    }
    if (b->n == before) return 0;
    for (uint32_t r = off_rsv; r + 16u <= total; r += 16u) {
        uint64_t base = zxb_be_cells(fdt + r, 2), size = zxb_be_cells(fdt + r + 8, 2);
        if (!base && !size) break;
        zxb_exclude(b, base, base + size);
    }
    uint64_t self = (uint64_t) (uintptr_t) fdt;
    zxb_exclude(b, self, self + total);
    return b->n - before;
}

/* Take `bytes` (rounded up to pages) from the first range that fits.
 * Returns the physical address, or 0 when nothing fits. Not zeroed: the
 * caller zeroes (the kernel through its identity map). */
static inline uint64_t zxb_take(zxb_t *b, uint64_t bytes)
{
    if (bytes == 0) return 0;
    uint64_t need = zxb_up(bytes);
    if (need < bytes) return 0;
    for (uint32_t i = 0; i < b->n; i++) {
        zxb_range_t *r = &b->r[i];
        if (r->hi - r->lo >= need) {
            uint64_t pa = r->lo;
            r->lo += need;
            b->taken += need;
            return pa;
        }
    }
    return 0;
}

static inline uint64_t zxb_remaining(const zxb_t *b)
{
    uint64_t n = 0;
    for (uint32_t i = 0; i < b->n; i++) n += b->r[i].hi - b->r[i].lo;
    return n;
}

#endif /* ZXV_BOOTMEM_H */
