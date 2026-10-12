/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwfake.h — a host hw_plat_t with register-level fake PCI devices.
 *
 * A fake device is a config space (64 dwords) plus up to six BARs, each backed
 * by host memory. BARs are 64-bit memory BARs whose address IS the host
 * pointer, so mmio_map returns it unchanged and DMA addresses are host
 * pointers too. Sizing works like hardware: writing all-ones reads back the
 * size mask.
 *
 * Time only moves in delay_us: each call advances the clock and runs every
 * device's tick(), which is where a fake reacts to doorbells and register
 * writes. Drivers always poll through delay_us, so this is enough. */
#ifndef ZXV_HWFAKE_H
#define ZXV_HWFAKE_H

#include "../hwdrv.h"

typedef struct fake_dev {
    uint32_t bdf;
    uint32_t cfg[64];
    uint8_t *bar_mem[6];  /* host memory behind the BAR, 0 for address-only BARs */
    uint64_t bar_size[6]; /* 0 = no BAR */
    uint8_t bar_kind[6];  /* FAKE_BAR_MEM64 / MEM32 / IO */
    void (*tick)(struct fake_dev *d, uint64_t now_us);
    void *state;
} fake_dev_t;

typedef struct fake_plat {
    hw_plat_t plat;
    fake_dev_t *devs[16];
    uint32_t n;
    uint64_t now_us;
    uint32_t dma_bytes;
    uint32_t dma_fail_after; /* 0 = never fail; else fail the Nth allocation */
    uint32_t dma_calls;
    char log[64][160];
    uint32_t nlog;
} fake_plat_t;

/* Set up the platform; every hook points back at fp. */
void fake_plat_init(fake_plat_t *fp);
/* Add a device: class triple, ids. Its BARs come from fake_dev_bar. */
fake_dev_t *fake_dev_new(fake_plat_t *fp, uint32_t bdf, uint16_t vendor, uint16_t device,
                         uint8_t cls, uint8_t sub, uint8_t progif);
/* Give the device BAR i backed by `size` bytes of zeroed host memory (size a
 * power of two >= 16). Returns the register memory. */
uint8_t *fake_dev_bar(fake_dev_t *d, uint32_t i, uint64_t size);
/* An address-only BAR for scan tests: kind FAKE_BAR_MEM32 or FAKE_BAR_IO at a
 * fixed address (no memory behind it), or FAKE_BAR_MEM64. */
#define FAKE_BAR_MEM64 0
#define FAKE_BAR_MEM32 1
#define FAKE_BAR_IO    2
void fake_dev_bar_at(fake_dev_t *d, uint32_t i, uint64_t addr, uint64_t size, uint8_t kind);
/* Free everything fake_plat_init / fake_dev_new / dma_alloc made. */
void fake_plat_free(fake_plat_t *fp);
/* 1 if any logged line contains s. */
int fake_log_has(const fake_plat_t *fp, const char *s);

/* Little-endian helpers for fakes reading/writing descriptors in "DMA" memory. */
static inline uint32_t fk_rd32(const void *p)
{
    const uint8_t *b = (const uint8_t *) p;
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) |
           ((uint32_t) b[3] << 24);
}
static inline void fk_wr32(void *p, uint32_t v)
{
    uint8_t *b = (uint8_t *) p;
    b[0] = (uint8_t) v;
    b[1] = (uint8_t) (v >> 8);
    b[2] = (uint8_t) (v >> 16);
    b[3] = (uint8_t) (v >> 24);
}
static inline uint64_t fk_rd64(const void *p)
{
    return (uint64_t) fk_rd32(p) | ((uint64_t) fk_rd32((const uint8_t *) p + 4) << 32);
}
static inline void fk_wr64(void *p, uint64_t v)
{
    fk_wr32(p, (uint32_t) v);
    fk_wr32((uint8_t *) p + 4, (uint32_t) (v >> 32));
}
static inline void *fk_ptr(uint64_t phys)
{
    return (void *) (uintptr_t) phys;
}

/* Tiny test harness: CHECK counts every check; failures print and count. */
extern int fk_checks, fk_failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        fk_checks++;                                                                               \
        if (!(cond)) {                                                                             \
            fk_failures++;                                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                          \
    } while (0)

#endif
