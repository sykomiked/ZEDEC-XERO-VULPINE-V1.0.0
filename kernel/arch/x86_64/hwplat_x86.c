/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwplat_x86.c — hw_plat_t for x86_64 bare metal (see kernel/src/hwdrv/hwdrv.h).
 *
 *   config space  legacy CAM ports 0xCF8/0xCFC (every PC chipset and q35)
 *   MMIO          identity-mapped on demand with 2 MB pages, PCD|PWT set; any
 *                 address below 2^48, so 64-bit BARs above 4 GB work
 *   DMA           a static 8 MB pool in BSS; the boot tables identity-map the
 *                 low 1 GB, so its virtual address IS the bus address
 *   delay         TSC, calibrated once against PIT channel 2 (10 ms gate)
 *
 * Integer only. */
#include "hwplat_x86.h"

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outl(uint16_t port, uint32_t v)
{
    __asm__ __volatile__("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ __volatile__("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t) hi << 32) | lo;
}

/* ---- config space ---------------------------------------------------------- */
static uint32_t cfg_read(void *ctx, uint32_t bdf, uint32_t off)
{
    (void) ctx;
    if (off > 0xFC) return 0xFFFFFFFFu; /* CAM reaches the first 256 bytes only */
    outl(0xCF8, 0x80000000u | (bdf << 8) | (off & 0xFC));
    return inl(0xCFC);
}

static void cfg_write(void *ctx, uint32_t bdf, uint32_t off, uint32_t val)
{
    (void) ctx;
    if (off > 0xFC) return;
    outl(0xCF8, 0x80000000u | (bdf << 8) | (off & 0xFC));
    outl(0xCFC, val);
}

/* ---- MMIO mapping ----------------------------------------------------------- */
#define PTE_P    0x001ULL
#define PTE_RW   0x002ULL
#define PTE_PWT  0x008ULL
#define PTE_PCD  0x010ULL
#define PTE_PS   0x080ULL
#define PTE_ADDR 0x000FFFFFFFFFF000ULL

#define N_L3 4
#define N_L2 24
static uint64_t g_l3[N_L3][512] __attribute__((aligned(4096)));
static uint64_t g_l2[N_L2][512] __attribute__((aligned(4096)));
static uint32_t g_l3_used, g_l2_used;

static uint64_t *table_of(uint64_t e)
{
    return (uint64_t *) (uintptr_t) (e & PTE_ADDR);
}

/* Map one 2 MB page at phys (2 MB aligned) uncached. 1 = mapped. The low 1 GB
 * is RAM the kernel lives in and is left exactly as boot set it up. */
static int map_2m(uint64_t phys)
{
    if (phys < 0x40000000ULL) return 1;
    if (phys >> 48) return 0;
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *l4 = table_of(cr3);
    uint32_t i4 = (uint32_t) (phys >> 39) & 511, i3 = (uint32_t) (phys >> 30) & 511,
             i2 = (uint32_t) (phys >> 21) & 511;
    if (!(l4[i4] & PTE_P)) {
        if (g_l3_used == N_L3) return 0;
        uint64_t *t = g_l3[g_l3_used++];
        for (int i = 0; i < 512; i++) t[i] = 0;
        l4[i4] = (uint64_t) (uintptr_t) t | PTE_P | PTE_RW;
    }
    uint64_t *l3 = table_of(l4[i4]);
    if (l3[i3] & PTE_PS) { /* a 1 GB page already covers it: make it uncached */
        l3[i3] |= PTE_PCD | PTE_PWT;
        return 1;
    }
    if (!(l3[i3] & PTE_P)) {
        if (g_l2_used == N_L2) return 0;
        uint64_t *t = g_l2[g_l2_used++];
        for (int i = 0; i < 512; i++) t[i] = 0;
        l3[i3] = (uint64_t) (uintptr_t) t | PTE_P | PTE_RW;
    }
    uint64_t *l2 = table_of(l3[i3]);
    if ((l2[i2] & PTE_P) && !(l2[i2] & PTE_PS)) return 1; /* split by someone: leave it */
    l2[i2] = (phys & ~0x1FFFFFULL) | PTE_P | PTE_RW | PTE_PS | PTE_PCD | PTE_PWT;
    return 1;
}

static volatile void *mmio_map(void *ctx, uint64_t phys, uint64_t size)
{
    (void) ctx;
    if (!phys || !size) return 0;
    uint64_t end = phys + size;
    if (end < phys) return 0;
    for (uint64_t a = phys & ~0x1FFFFFULL; a < end; a += 0x200000ULL)
        if (!map_2m(a)) return 0;
    __asm__ __volatile__("mov %%cr3, %%rax\n\tmov %%rax, %%cr3" : : : "rax", "memory");
    return (volatile void *) (uintptr_t) phys;
}

/* The same mapper for the interrupt layer (LAPIC, IOAPIC, ACPI tables above
 * 1 GB), which runs before the class drivers are initialised. */
volatile void *hwplat_x86_map_mmio(uint64_t phys, uint64_t size)
{
    return mmio_map(0, phys, size);
}

/* ---- DMA pool --------------------------------------------------------------- */
#define DMA_POOL (8u << 20)
static uint8_t g_dma[DMA_POOL] __attribute__((aligned(4096)));
static uint32_t g_dma_used;

static void *dma_alloc(void *ctx, uint32_t size, uint32_t align, uint64_t *phys)
{
    (void) ctx;
    if (align < 64) align = 64;
    uint32_t at = (g_dma_used + align - 1) & ~(align - 1);
    if (at > DMA_POOL || size > DMA_POOL - at) return 0;
    g_dma_used = at + size;
    uint8_t *p = g_dma + at;
    for (uint32_t i = 0; i < size; i++) p[i] = 0;
    if (phys) *phys = (uint64_t) (uintptr_t) p;
    return p;
}

/* ---- delay ------------------------------------------------------------------ */
static uint64_t g_tsc_per_us;

static uint64_t calibrate_tsc(void)
{
    /* PIT channel 2, mode 0, 11932 ticks of 1.193182 MHz = 10.000 ms. Gate on,
     * speaker off; OUT2 (port 0x61 bit 5) goes high when the count ends. */
    uint8_t g = inb(0x61);
    outb(0x61, (uint8_t) ((g & ~0x02) | 0x01));
    outb(0x43, 0xB0);
    outb(0x42, 11932 & 0xFF);
    outb(0x42, 11932 >> 8);
    /* Restart the count by toggling the gate. */
    g = inb(0x61);
    outb(0x61, (uint8_t) (g & ~0x01));
    outb(0x61, (uint8_t) (g | 0x01));
    uint64_t t0 = rdtsc();
    uint32_t guard = 0;
    while (!(inb(0x61) & 0x20))
        if (++guard > 50000000u) return 0;
    uint64_t t1 = rdtsc();
    return (t1 - t0) / 10000;
}

static void delay_us(void *ctx, uint32_t us)
{
    (void) ctx;
    uint64_t t0 = rdtsc(), want = (uint64_t) us * g_tsc_per_us;
    while (rdtsc() - t0 < want) __asm__ __volatile__("pause");
}

static void (*g_log)(const char *);

static void log_line(const char *s)
{
    g_log(s);
}

static hw_plat_t g_plat;

const hw_plat_t *hwplat_x86_init(void (*log)(const char *), uint64_t *tsc_per_us)
{
    g_log = log;
    g_tsc_per_us = calibrate_tsc();
    /* No PIT (some recent boards gate it off): assume 2 GHz. Timeouts then run
     * long on slower parts, never short. */
    if (g_tsc_per_us == 0) g_tsc_per_us = 2000;
    if (tsc_per_us) *tsc_per_us = g_tsc_per_us;
    g_plat.ctx = 0;
    g_plat.cfg_read = cfg_read;
    g_plat.cfg_write = cfg_write;
    g_plat.mmio_map = mmio_map;
    g_plat.dma_alloc = dma_alloc;
    g_plat.delay_us = delay_us;
    g_plat.log = log_line;
    return &g_plat;
}
