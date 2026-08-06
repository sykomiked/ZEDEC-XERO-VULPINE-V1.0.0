/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* ramfb.c — QEMU ramfb scanout via fw_cfg DMA. See ramfb.h.
 *
 * The fw_cfg data/DMA interface is BIG-ENDIAN on every arch; we byte-swap every
 * multi-byte field. The kernel is identity-mapped, so &struct == its physical
 * address for QEMU's DMA. No libc, no malloc, integer only. */
#include "ramfb.h"

#define FW_CFG_FILE_DIR       0x0019u
#define FW_CFG_DMA_CTL_ERROR  0x01u
#define FW_CFG_DMA_CTL_READ   0x02u
#define FW_CFG_DMA_CTL_SELECT 0x08u
#define FW_CFG_DMA_CTL_WRITE  0x10u
#define RAMFB_FOURCC_XRGB8888 0x34325258u   /* 'XR24' little-endian DRM fourcc */

static bool g_live = false;

static inline uint16_t be16(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static inline uint32_t be32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint64_t be64(uint64_t x) { return __builtin_bswap64(x); }

/* fw_cfg DMA descriptor (all fields big-endian) */
struct fw_cfg_dma { uint32_t control; uint32_t length; uint64_t address; }
    __attribute__((packed));

/* one entry of the fw_cfg file directory (fields big-endian) */
struct fw_cfg_file { uint32_t size; uint16_t select; uint16_t reserved; char name[56]; }
    __attribute__((packed));

/* the ramfb configuration written to the 'etc/ramfb' fw_cfg file (big-endian) */
struct ramfb_cfg { uint64_t addr; uint32_t fourcc; uint32_t flags;
                   uint32_t width; uint32_t height; uint32_t stride; }
    __attribute__((packed));

static volatile struct fw_cfg_dma g_desc __attribute__((aligned(16)));

/* Kick one fw_cfg DMA transfer and spin until QEMU clears the control word. */
static bool fw_cfg_dma(uint32_t control, uint32_t length, uint64_t buf_phys) {
    g_desc.control = be32(control);
    g_desc.length  = be32(length);
    g_desc.address = be64(buf_phys);
    __asm__ volatile("dsb sy" ::: "memory");

    uint64_t desc = (uint64_t)(uintptr_t)&g_desc;          /* identity-mapped */
    volatile uint32_t *reg = (volatile uint32_t *)(RAMFB_FWCFG_BASE + 0x10);
    reg[0] = be32((uint32_t)(desc >> 32));                 /* high half   */
    reg[1] = be32((uint32_t)(desc & 0xffffffffu));         /* low half triggers */
    __asm__ volatile("dsb sy" ::: "memory");

    /* wait for completion: QEMU clears every control bit but ERROR */
    for (uint32_t spins = 0; spins < 100000000u; spins++) {
        uint32_t c = be32(g_desc.control);
        if (c & FW_CFG_DMA_CTL_ERROR) return false;
        if (c == 0) return true;
        __asm__ volatile("" ::: "memory");
    }
    return false;
}

static bool name_eq(const char *a, const char *b) {
    for (uint32_t i = 0; i < 56u; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
    return true;
}

/* Find 'etc/ramfb' in the fw_cfg file directory; return its selector key. */
static bool fw_cfg_find(const char *want, uint16_t *sel_out) {
    static uint8_t dir[16384] __attribute__((aligned(16)));
    if (!fw_cfg_dma((FW_CFG_FILE_DIR << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_READ,
                    (uint32_t)sizeof(dir), (uint64_t)(uintptr_t)dir))
        return false;
    uint32_t count = be32(*(volatile uint32_t *)(void *)dir);
    const struct fw_cfg_file *f = (const struct fw_cfg_file *)(void *)(dir + 4);
    for (uint32_t i = 0; i < count; i++) {
        if ((4u + (i + 1u) * sizeof(struct fw_cfg_file)) > sizeof(dir)) break;
        if (name_eq(f[i].name, want)) { *sel_out = be16(f[i].select); return true; }
    }
    return false;
}

int ramfb_init(volatile uint32_t *fb, uint32_t width, uint32_t height) {
    g_live = false;
    if (!fb || !width || !height) return -1;

    uint16_t sel;
    if (!fw_cfg_find("etc/ramfb", &sel)) return -2;   /* no ramfb device present */

    static volatile struct ramfb_cfg cfg __attribute__((aligned(16)));
    cfg.addr   = be64((uint64_t)(uintptr_t)fb);
    cfg.fourcc = be32(RAMFB_FOURCC_XRGB8888);
    cfg.flags  = 0;
    cfg.width  = be32(width);
    cfg.height = be32(height);
    cfg.stride = be32(width * 4u);

    if (!fw_cfg_dma(((uint32_t)sel << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE,
                    (uint32_t)sizeof(cfg), (uint64_t)(uintptr_t)&cfg))
        return -3;

    g_live = true;
    return 0;
}

bool ramfb_is_live(void) { return g_live; }
