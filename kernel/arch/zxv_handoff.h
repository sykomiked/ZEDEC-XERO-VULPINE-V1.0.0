/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_handoff.h — boot handoff v2: what the EFI stub learned, for the kernel.
 *
 * Before this, the stub fetched the firmware memory map only to call
 * ExitBootServices and threw it away; the kernel received a framebuffer and
 * nothing else. This record carries what a kernel needs to own a real
 * machine: the memory map (compacted), the ACPI and SMBIOS table pointers,
 * the firmware clock, the kernel's full extent (image, BSS, boot stack), and two
 * verdicts the stub reached while firmware still existed.
 *
 * It sits in the stub's handoff page right after the 64-byte framebuffer
 * record (efifb_con.h on x86_64), so the old record and its readers are
 * unchanged. Header-only and freestanding: the stub (built -fpie, no libc)
 * and the kernel both include it; the host test includes it as is.
 *
 * Verdicts use lpres's numbering (NEITHER 0, TRUE 1, FALSE 2, BOTH 3):
 *   window  "is the kernel's load window [kernel_lo, kernel_hi) safe to
 *           overwrite after ExitBootServices?" The knowledge join of two
 *           witnesses: AllocatePages (TRUE reserved, NEITHER busy now, which
 *           says nothing about after ExitBootServices) and the final memory
 *           map (TRUE only reclaimable memory; BOTH it also covers ACPI
 *           memory, see ZXH_F_ACPI_*; FALSE runtime-services, reserved or
 *           MMIO memory, or the stub's own stack). FALSE returns to the
 *           firmware instead of copying over memory it does not own; BOTH
 *           boots with the damaged ACPI held, never silently trusted.
 *   models  TRUE every model on the disc hashed correctly, FALSE one or more
 *           did not, NEITHER the disc carries no models.
 * The kernel's acceptance of the record is itself a verdict (zxh_accept):
 * TRUE valid, FALSE present but damaged (never trusted), NEITHER absent (a
 * direct -kernel boot, where nothing changes). */
#ifndef ZXV_HANDOFF_H
#define ZXV_HANDOFF_H

#include <stdint.h>

#define ZXH_MAGIC   0x3248585Au /* "ZXH2" little-endian */
#define ZXH_VERSION 1u
#define ZXH_OFFSET  64u  /* after the 64-byte framebuffer record */
#define ZXH_MAX_MEM 160u /* a real PC has 40-120 EFI descriptors before merging */

/* Memory kinds, compacted from the EFI types. */
enum {
    ZXH_MEM_USABLE = 1,   /* conventional + boot-services code/data: free after EBS */
    ZXH_MEM_LOADER = 2,   /* loader code/data: the kernel window, handoff, stub */
    ZXH_MEM_RESERVED = 3, /* reserved, unusable, MMIO, PAL code: never touch */
    ZXH_MEM_ACPI_RECLAIM = 4,
    ZXH_MEM_ACPI_NVS = 5,
    ZXH_MEM_RUNTIME = 6, /* UEFI runtime services code/data: never touch */
    ZXH_MEM_PERSISTENT = 7,
};

/* flags */
#define ZXH_F_MEM_TRUNCATED 0x01u /* more regions than ZXH_MAX_MEM after merging */
#define ZXH_F_GOP           0x02u /* the framebuffer record before this one is valid */
/* The kernel window covered ACPI memory, which the kernel image or BSS has
 * now overwritten: hold the ACPI features that would read it. Today's
 * x86_64 kernel has a fixed 90 MB window at 1 MB (BSS: large static
 * buffers), and OVMF keeps 32 KB of ACPI NVS at 8 MB, so this is set under
 * QEMU. The fix is to move the big buffers out of BSS onto frames from the
 * memory map (docs/BAREMETAL_OS_GAPS.md, gap 3). */
#define ZXH_F_ACPI_NVS_LOST    0x04u /* sleep and AML methods that touch NVS */
#define ZXH_F_ACPI_TABLES_LOST 0x08u /* the tables themselves: do not trust rsdp */

typedef struct {
    uint64_t base;  /* physical, 4 KB aligned */
    uint64_t pages; /* 4 KB pages */
    uint32_t kind;  /* ZXH_MEM_* */
    uint32_t _r;
} zxh_mem_t;

typedef struct {
    uint32_t magic;                /* ZXH_MAGIC */
    uint32_t version;              /* ZXH_VERSION */
    uint32_t bytes;                /* sizeof(zxh_t) */
    uint32_t sum;                  /* FNV-1a 32 over the record with sum = 0 */
    uint64_t rsdp;                 /* ACPI 2.0+ RSDP (or 1.0 when only that exists); 0 = none */
    uint64_t smbios3;              /* SMBIOS 3 entry point; 0 = none */
    int64_t efi_time;              /* UTC seconds since 1970 from GetTime; 0 = no clock */
    uint64_t kernel_lo, kernel_hi; /* the window the kernel image, BSS and boot stack occupy */
    uint8_t window;                /* verdict, see above */
    uint8_t models;                /* verdict, see above */
    uint8_t flags;                 /* ZXH_F_* */
    uint8_t _r0;
    uint32_t models_ok, models_bad;
    uint32_t n_mem;
    zxh_mem_t mem[ZXH_MAX_MEM];
} zxh_t;

typedef char zxh_fits_in_handoff_page[(ZXH_OFFSET + sizeof(zxh_t) <= 4096u) ? 1 : -1];

/* FNV-1a 32 over the whole record, reading the sum field as zero, so the
 * kernel can check a record in place without copying 4 KB. */
static inline uint32_t zxh_digest(const zxh_t *r)
{
    const uint8_t *b = (const uint8_t *) r;
    const uint32_t sum_at = (uint32_t) __builtin_offsetof(zxh_t, sum);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < (uint32_t) sizeof(zxh_t); i++) {
        uint8_t v = (i >= sum_at && i < sum_at + 4u) ? 0u : b[i];
        h ^= v;
        h *= 16777619u;
    }
    return h;
}

static inline void zxh_seal(zxh_t *h)
{
    h->magic = ZXH_MAGIC;
    h->version = ZXH_VERSION;
    h->bytes = (uint32_t) sizeof(zxh_t);
    h->sum = zxh_digest(h);
}

/* EFI_MEMORY_TYPE -> ZXH_MEM_*. */
static inline uint32_t zxh_kind_of_efi(uint32_t t)
{
    switch (t) {
    case 3: /* BootServicesCode */
    case 4: /* BootServicesData */
    case 7: /* ConventionalMemory */
        return ZXH_MEM_USABLE;
    case 1: /* LoaderCode */
    case 2: /* LoaderData */
        return ZXH_MEM_LOADER;
    case 9:
        return ZXH_MEM_ACPI_RECLAIM;
    case 10:
        return ZXH_MEM_ACPI_NVS;
    case 5: /* RuntimeServicesCode */
    case 6: /* RuntimeServicesData */
        return ZXH_MEM_RUNTIME;
    case 14:
        return ZXH_MEM_PERSISTENT;
    default: /* 0 Reserved, 8 Unusable, 11/12 MMIO, 13 PalCode, unknown */
        return ZXH_MEM_RESERVED;
    }
}

/* Append a region, merging it into the previous one when it is the same
 * kind and directly adjacent. Regions must arrive in address order (the
 * EFI map is not guaranteed sorted; the stub sorts nothing, so unsorted
 * input simply merges less). Returns 0, or -1 when the table is full (the
 * caller sets ZXH_F_MEM_TRUNCATED). */
static inline int zxh_add_mem(zxh_t *h, uint64_t base, uint64_t pages, uint32_t kind)
{
    if (pages == 0) return 0;
    if (h->n_mem > 0) {
        zxh_mem_t *last = &h->mem[h->n_mem - 1];
        if (last->kind == kind && last->base + last->pages * 4096u == base) {
            last->pages += pages;
            return 0;
        }
    }
    if (h->n_mem >= ZXH_MAX_MEM) return -1;
    h->mem[h->n_mem].base = base;
    h->mem[h->n_mem].pages = pages;
    h->mem[h->n_mem].kind = kind;
    h->mem[h->n_mem]._r = 0;
    h->n_mem++;
    return 0;
}

/* Stub side: what does the final EFI memory map say about overwriting
 * [lo, hi) once boot services end? map/msz/dsz are as GetMemoryMap returned
 * them. Returns lpres numbering:
 *   1 TRUE   only conventional / boot-services memory (plus loader memory
 *            when allow_loader: our own reservation or our own image)
 *   3 BOTH   otherwise free but covering ACPI memory (NVS or reclaimable
 *            tables); *flags gets ZXH_F_ACPI_*, and the kernel holds every
 *            ACPI feature that would read it
 *   2 FALSE  runtime services, reserved, MMIO or foreign loader memory:
 *            overwriting it breaks the firmware
 * *worst_type / *worst_base name the first descriptor that made it worse
 * than TRUE (for the stub's log); left alone when the answer is TRUE. */
static inline int zxh_window_of_map(const uint8_t *map, uint64_t msz, uint64_t dsz, uint64_t lo,
                                    uint64_t hi, int allow_loader, uint8_t *flags,
                                    uint32_t *worst_type, uint64_t *worst_base)
{
    int v = 1;
    if (dsz < 32u) return 2; /* not an EFI_MEMORY_DESCRIPTOR array */
    for (uint64_t off = 0; off + dsz <= msz; off += dsz) {
        uint32_t type = *(const uint32_t *) (map + off);
        uint64_t b = *(const uint64_t *) (map + off + 8);
        uint64_t e = b + *(const uint64_t *) (map + off + 24) * 4096u;
        if (e <= lo || b >= hi) continue;
        uint32_t k = zxh_kind_of_efi(type);
        if (k == ZXH_MEM_USABLE) continue;
        if (k == ZXH_MEM_LOADER && allow_loader) continue;
        if (v == 1) {
            *worst_type = type;
            *worst_base = b;
        }
        if (k == ZXH_MEM_ACPI_NVS) {
            *flags |= ZXH_F_ACPI_NVS_LOST;
            v = 3;
        } else if (k == ZXH_MEM_ACPI_RECLAIM) {
            *flags |= ZXH_F_ACPI_TABLES_LOST;
            v = 3;
        } else {
            *worst_type = type;
            *worst_base = b;
            return 2;
        }
    }
    return v;
}

/* Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's
 * days_from_civil), integer only. */
static inline int64_t zxh_days_from_civil(int64_t y, uint32_t m, uint32_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t) (y - era * 400);
    uint32_t doy = (153u * (m + (m > 2 ? (uint32_t) -3 : 9u)) + 2u) / 5u + d - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t) doe - 719468;
}

/* EFI_TIME fields to UTC seconds. tz_min is EFI's TimeZone: minutes the
 * local time is offset from UTC, or 2047 (EFI_UNSPECIFIED_TIMEZONE), read
 * as local = UTC. Returns 0 for a date outside 1970..9999 or bad fields. */
static inline int64_t zxh_efi_time_to_unix(uint32_t year, uint32_t mon, uint32_t day, uint32_t hour,
                                           uint32_t min, uint32_t sec, int32_t tz_min)
{
    if (year < 1970 || year > 9999 || mon < 1 || mon > 12 || day < 1 || day > 31 || hour > 23 ||
        min > 59 || sec > 59)
        return 0;
    int64_t t = zxh_days_from_civil((int64_t) year, mon, day) * 86400 + (int64_t) hour * 3600 +
                (int64_t) min * 60 + (int64_t) sec;
    /* UEFI 2.x: Localtime = UTC - TimeZone, so UTC = Localtime + TimeZone. */
    if (tz_min != 2047 && tz_min >= -1440 && tz_min <= 1440) t += (int64_t) tz_min * 60;
    return t;
}

/* Kernel side: is the record at p trustworthy? 1 TRUE, 2 FALSE, 0 NEITHER
 * (lpres numbering). FALSE covers a wrong version, size, checksum or region
 * count: a damaged record is never half-used. */
static inline int zxh_accept(const zxh_t *p)
{
    if (!p || p->magic != ZXH_MAGIC) return 0;
    if (p->version != ZXH_VERSION || p->bytes != (uint32_t) sizeof(zxh_t) ||
        p->n_mem > ZXH_MAX_MEM || p->window > 3 || p->models > 3)
        return 2;
    return zxh_digest(p) == p->sum ? 1 : 2;
}

/* Pages of one kind (for the boot summary and, next, the frame allocator). */
static inline uint64_t zxh_pages_of(const zxh_t *p, uint32_t kind)
{
    uint64_t n = 0;
    for (uint32_t i = 0; i < p->n_mem; i++)
        if (p->mem[i].kind == kind) n += p->mem[i].pages;
    return n;
}

/* Kernel side: print an accepted record's summary through the arch's
 * console (puts, decimal, hex). The caller prints the verdict line itself
 * ([HANDOFF_OK] / [HANDOFF_BAD] / none) so it carries its boot-log prefix. */
static inline void zxh_report(const zxh_t *h, void (*puts)(const char *), void (*dec)(uint64_t),
                              void (*hex)(uint64_t))
{
    puts("  memory: ");
    dec(h->n_mem);
    puts(" regions, usable ");
    dec(zxh_pages_of(h, ZXH_MEM_USABLE) / 256u);
    puts(" MiB, loader ");
    dec(zxh_pages_of(h, ZXH_MEM_LOADER) / 256u);
    puts(" MiB, firmware runtime ");
    dec(zxh_pages_of(h, ZXH_MEM_RUNTIME) * 4u);
    puts(" KiB");
    if (h->flags & ZXH_F_MEM_TRUNCATED) puts(" (map truncated)");
    puts("\n  kernel window 0x");
    hex(h->kernel_lo);
    puts("-0x");
    hex(h->kernel_hi);
    puts(h->window == 1   ? " verified free\n"
         : h->window == 3 ? " free but covering ACPI memory (held)\n"
                          : " NOT verified\n");
    if (h->flags & ZXH_F_ACPI_NVS_LOST)
        puts("  ACPI NVS overwritten by the kernel window: sleep and AML held\n");
    puts("  ACPI RSDP 0x");
    hex(h->flags & ZXH_F_ACPI_TABLES_LOST ? 0 : h->rsdp);
    if (h->flags & ZXH_F_ACPI_TABLES_LOST) puts(" (tables overwritten: not trusted)");
    puts(h->smbios3 ? ", SMBIOS 3 present" : ", no SMBIOS 3");
    puts("\n  firmware clock ");
    if (h->efi_time) {
        dec((uint64_t) h->efi_time);
        puts(" s since 1970 UTC\n");
    } else {
        puts("unavailable\n");
    }
    puts("  disc models: ");
    puts(h->models == 1 ? "verified" : h->models == 2 ? "FAILED" : "none on this disc");
    puts("\n");
}

#endif /* ZXV_HANDOFF_H */
