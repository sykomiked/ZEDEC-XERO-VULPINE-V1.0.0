/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwdrv.h — standard class drivers for bare-metal ZXV (layer 1 of the driver
 * plan in docs/TURNKEY_OS_GAPS.md).
 *
 * Drivers are written against STANDARDS, not chips: NVMe, AHCI, xHCI + USB
 * classes, Intel HD Audio, the e1000 NIC family, and virtio-pci (net, blk,
 * scsi) for KVM cloud VMs. Each one talks to hardware
 * only through hw_plat_t (config space, MMIO mapping, DMA memory, a delay and a
 * log line), so the same driver source runs:
 *   - on x86_64 bare metal (kernel/arch/x86_64/hwplat_x86.c), tested in QEMU;
 *   - on the host against register-level fake devices (kernel/src/hwdrv/test).
 *
 * Integer only. No libc: the helpers below are all a driver gets.
 *
 * Every bind returns a four-valued verdict, the same shape the paraconsistent
 * firmware adapter (kernel/src/fwadapt, PR #103) joins over:
 *   HW_T  bound and its self-test passed
 *   HW_F  the device answered but failed (timeout, controller fatal status)
 *   HW_B  the device contradicts itself (e.g. class says NVMe, CAP says no
 *         NVM command set); held in a safe state, never used
 *   HW_N  nothing to drive (no BAR assigned, no media, no driver)
 * A failed or held device never stops the boot: it is logged and skipped. */
#ifndef ZXV_HWDRV_H
#define ZXV_HWDRV_H

#include <stdint.h>

typedef enum { HW_N = 0, HW_T = 1, HW_F = 2, HW_B = 3 } hw_verdict_t;

/* ---- platform ---------------------------------------------------------- */
typedef struct hw_plat {
    void *ctx;
    /* PCI configuration space, 32-bit aligned dword access. bdf = bus<<8 |
     * dev<<3 | fn. A missing function reads 0xFFFFFFFF. */
    uint32_t (*cfg_read)(void *ctx, uint32_t bdf, uint32_t off);
    void (*cfg_write)(void *ctx, uint32_t bdf, uint32_t off, uint32_t val);
    /* Map a device register window uncached; returns its address or 0. */
    volatile void *(*mmio_map)(void *ctx, uint64_t phys, uint64_t size);
    /* Zeroed DMA memory, physically contiguous; *phys is the bus address. */
    void *(*dma_alloc)(void *ctx, uint32_t size, uint32_t align, uint64_t *phys);
    /* Busy-wait. Every driver polling loop calls this, so a host fake device
     * advances its own state from here. */
    void (*delay_us)(void *ctx, uint32_t us);
    /* One console line, no trailing newline. */
    void (*log)(const char *line);
} hw_plat_t;

/* ---- MMIO and memory helpers -------------------------------------------- */
static inline uint32_t hw_rd32(volatile void *base, uint32_t off)
{
    return *(volatile uint32_t *) ((volatile uint8_t *) base + off);
}
static inline void hw_wr32(volatile void *base, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *) ((volatile uint8_t *) base + off) = v;
}
static inline uint16_t hw_rd16(volatile void *base, uint32_t off)
{
    return *(volatile uint16_t *) ((volatile uint8_t *) base + off);
}
static inline void hw_wr16(volatile void *base, uint32_t off, uint16_t v)
{
    *(volatile uint16_t *) ((volatile uint8_t *) base + off) = v;
}
static inline uint8_t hw_rd8(volatile void *base, uint32_t off)
{
    return *((volatile uint8_t *) base + off);
}
static inline void hw_wr8(volatile void *base, uint32_t off, uint8_t v)
{
    *((volatile uint8_t *) base + off) = v;
}
/* 64-bit registers as two dwords, low first (NVMe, xHCI and AHCI allow it, and
 * it keeps 32-bit-only bridges working). */
static inline uint64_t hw_rd64(volatile void *base, uint32_t off)
{
    uint64_t lo = hw_rd32(base, off), hi = hw_rd32(base, off + 4);
    return lo | (hi << 32);
}
static inline void hw_wr64(volatile void *base, uint32_t off, uint64_t v)
{
    hw_wr32(base, off, (uint32_t) v);
    hw_wr32(base, off + 4, (uint32_t) (v >> 32));
}
/* Compiler + store barrier before a doorbell write. x86 is ordered for UC
 * MMIO; arm64 would need dsb here, which the arm64 platform adds later. */
static inline void hw_wmb(void)
{
    __asm__ __volatile__("" ::: "memory");
}

void hw_memset(void *dst, uint8_t v, uint32_t n);
void hw_memcpy(void *dst, const void *src, uint32_t n);
int hw_memeq(const void *a, const void *b, uint32_t n);

/* Poll until (rd32(base,off) & mask) == want, at most timeout_us. 1 = met. */
int hw_wait32(const hw_plat_t *p, volatile void *base, uint32_t off, uint32_t mask, uint32_t want,
              uint32_t timeout_us);

/* ---- log line builder ------------------------------------------------------
 * Fixed buffer, never overflows, always NUL terminated. */
typedef struct hw_line {
    char b[160];
    uint32_t n;
} hw_line_t;
void hw_line_init(hw_line_t *l);
void hw_line_s(hw_line_t *l, const char *s);
void hw_line_u(hw_line_t *l, uint64_t v);
void hw_line_x(hw_line_t *l, uint64_t v, uint32_t digits); /* zero padded hex */
void hw_line_c(hw_line_t *l, char c);
void hw_line_bdf(hw_line_t *l, uint32_t bdf); /* "bb:dd.f" */

/* ---- PCI ------------------------------------------------------------------ */
#define HW_BAR_IO       0x01
#define HW_BAR_64       0x02
#define HW_BAR_PREFETCH 0x04

#define HW_BDF(bus, dev, fn) (((uint32_t) (bus) << 8) | ((uint32_t) (dev) << 3) | (uint32_t) (fn))

typedef struct hw_pci_dev {
    uint32_t bdf;
    uint16_t vendor, device;
    uint8_t class_code, subclass, progif, rev;
    uint8_t header_type, irq_line;
    uint64_t bar[6];      /* base address (flag bits stripped); 0 = unassigned */
    uint64_t bar_size[6]; /* sized by the all-ones probe; 0 = absent */
    uint8_t bar_flags[6];
} hw_pci_dev_t;

/* Walk every bus/device/function (header type 0 endpoints and bridges both
 * listed); sizes BARs with decode disabled, then restores the command word.
 * Returns the number of functions written to out (at most max). */
uint32_t hw_pci_scan(const hw_plat_t *p, hw_pci_dev_t *out, uint32_t max);
/* Turn on memory decode and bus mastering (IO decode off, INTx off: drivers
 * poll). */
void hw_pci_enable(const hw_plat_t *p, const hw_pci_dev_t *d);
/* Offset of capability `id` in config space, 0 if absent. */
uint32_t hw_pci_find_cap(const hw_plat_t *p, const hw_pci_dev_t *d, uint8_t id);
/* Map BAR i (memory BARs only); 0 when unassigned, IO, or unmappable. */
volatile void *hw_pci_map_bar(const hw_plat_t *p, const hw_pci_dev_t *d, uint32_t i);

/* ---- device classes ------------------------------------------------------- */
typedef struct hw_blk {
    char name[16];
    uint32_t block_size;
    uint64_t blocks;
    void *drv;
    /* Read/write n blocks. 0 = ok, negative = error. buf needs no alignment. */
    int (*read)(struct hw_blk *b, uint64_t lba, uint32_t n, void *buf);
    int (*write)(struct hw_blk *b, uint64_t lba, uint32_t n, const void *buf);
} hw_blk_t;

typedef struct hw_nic {
    char name[16];
    uint8_t mac[6];
    uint8_t link_up;
    void *drv;
    int (*send)(struct hw_nic *n, const void *frame, uint32_t len); /* 0 = queued */
    int (*recv)(struct hw_nic *n, void *buf, uint32_t max);         /* bytes, 0 = none */
    /* Optional: stop receive and transmit, drop the MAC's link and power the
     * PHY down. 0 = PHY powered down, 1 = MAC stopped but the PHY left as it
     * is (no safe PHY access on this part), -1 = the PHY did not answer. */
    int (*phy_off)(struct hw_nic *n);
    uint8_t on_pci; /* bdf is the device's PCI function */
    uint32_t bdf;
} hw_nic_t;

/* Take a NIC off the bus for good (the post-hydration seal): phy_off when
 * the driver has one, then the PCI command register's Bus Master and Memory
 * Space bits cleared and read back, so the device can neither DMA nor be
 * reached until the machine is reset. Returns HW_NIC_OFF_PCI, plus
 * HW_NIC_OFF_PHY when the PHY was powered down; -1 when the NIC is not on PCI
 * or the bits did not clear. */
#define HW_NIC_OFF_PCI 1
#define HW_NIC_OFF_PHY 2
int hw_nic_power_off(const hw_plat_t *p, hw_nic_t *n);

/* Key event: USB HID usage page 7 code, the ASCII it maps to (0 if none),
 * modifier byte (HID boot report bit layout), pressed 1 / released 0. */
typedef struct hw_key {
    uint8_t usage, ascii, mods, pressed;
} hw_key_t;

typedef struct hw_input {
    char name[16];
    void *drv;
    int (*poll)(struct hw_input *in, hw_key_t *ev); /* 1 = event written */
} hw_input_t;

typedef struct hw_audio {
    char name[16];
    uint32_t codecs; /* bitmask of codec addresses that answered */
    void *drv;
    /* Play signed 16-bit stereo 48 kHz frames once; 0 = started. */
    int (*play)(struct hw_audio *a, const int16_t *frames, uint32_t nframes);
    /* Bytes the stream engine has consumed (DMA position), for a self-test. */
    uint32_t (*position)(struct hw_audio *a);
} hw_audio_t;

#define HW_MAX_BLK   8
#define HW_MAX_NIC   4
#define HW_MAX_INPUT 4
#define HW_MAX_AUDIO 2

typedef struct hw_registry {
    hw_blk_t *blk[HW_MAX_BLK];
    hw_nic_t *nic[HW_MAX_NIC];
    hw_input_t *input[HW_MAX_INPUT];
    hw_audio_t *audio[HW_MAX_AUDIO];
    uint32_t n_blk, n_nic, n_input, n_audio;
} hw_registry_t;

int hw_reg_blk(hw_registry_t *r, hw_blk_t *b);
int hw_reg_nic(hw_registry_t *r, hw_nic_t *n);
int hw_reg_input(hw_registry_t *r, hw_input_t *in);
int hw_reg_audio(hw_registry_t *r, hw_audio_t *a);

/* ---- driver table ---------------------------------------------------------
 * A driver matches by vendor/device pairs (ids, terminated by {0,0}) or, when
 * ids is 0, by class/subclass/progif (0xFF = any). A specific-id match always
 * wins over a class match; the probe tries specific drivers first. */
typedef struct hw_id {
    uint16_t vendor, device;
} hw_id_t;

typedef struct hw_driver {
    const char *name;
    uint8_t class_code, subclass, progif;
    const hw_id_t *ids;
    /* note: one short phrase for the boot line (model, size, ...). */
    hw_verdict_t (*bind)(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                         hw_line_t *note);
} hw_driver_t;

/* Built-in table and its length (hwdrv_probe.c). */
extern const hw_driver_t hw_drivers[];
extern const uint32_t hw_n_drivers;

/* The matching driver for d, specific ids first; 0 if none. */
const hw_driver_t *hw_match(const hw_driver_t *tab, uint32_t n, const hw_pci_dev_t *d);

typedef struct hw_probe_stats {
    uint32_t functions, bound, failed, held, unclaimed;
} hw_probe_stats_t;

/* Scan the bus, bind every matching driver, log one line per device, register
 * what binds. */
hw_probe_stats_t hw_probe_all(const hw_plat_t *p, hw_registry_t *r);

/* Boot self-tests over what registered: read LBA 1 of each disk (reports the
 * GPT signature), ARP the QEMU/most-routers gateway on each NIC, poll each
 * input briefly, start a short tone on each audio device. Logs one line each;
 * returns how many passed. */
uint32_t hw_selftest_all(const hw_plat_t *p, hw_registry_t *r);

/* ---- drivers (one bind each; see the .c files) ---------------------------- */
extern const hw_id_t e1000_ids[]; /* e1000.c: the Intel PRO/1000 family it drives */
extern const hw_id_t igb_ids[];   /* igb.c: 82576 / I350 / I210 / I211 */
extern const hw_id_t igc_ids[];   /* igc.c: I225 / I226 2.5 GbE (and I220/I221) */
extern const hw_id_t r8169_ids[]; /* r8169.c: Realtek RTL8169/8168/8111/8101, RTL8125 */
extern const hw_id_t vnet_ids[];  /* vnet.c: virtio-net, transitional and modern */
extern const hw_id_t vblk_ids[];  /* vblk.c: virtio-blk, transitional and modern */
extern const hw_id_t vscsi_ids[]; /* vscsi.c: virtio-scsi, transitional and modern */
hw_verdict_t nvme_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                       hw_line_t *note);
/* nvme.c: Security Send (send 1, opcode 0x81) or Receive (0x82) of len bytes
 * (1..8192) with SECP/SPSP. 0 = done; -1 = bad length or the controller
 * refused or timed out; -2 = b is not an NVMe disk (checked first). */
int nvme_security(hw_blk_t *b, int send, uint8_t secp, uint16_t spsp, uint8_t *buf, uint32_t len);
hw_verdict_t ahci_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                       hw_line_t *note);
hw_verdict_t xhci_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                       hw_line_t *note);
hw_verdict_t e1000_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                        hw_line_t *note);
hw_verdict_t hda_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note);
hw_verdict_t igb_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note);
hw_verdict_t igc_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note);
hw_verdict_t r8169_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                        hw_line_t *note);
/* arbmbox.c: the arbiter mailbox (class 08/80/5A + "ZXVARBMB" in BAR 0). The
 * session it opens is hw_arbiter() (0 until one binds); hw_arbiter_reset()
 * forgets it (tests, and a re-probe after the arbiter resets). */
struct arb_host;
hw_verdict_t arbmbox_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                          hw_line_t *note);
struct arb_host *hw_arbiter(void);
void hw_arbiter_reset(void);

/* hw_sed.c: TCG Opal unlock of every NVMe disk at boot (src/opal). A disk
 * that is not an SED, or not locked, is logged and left alone; a locked one
 * is unlocked as Admin1 of the Locking SP with the arbiter's key slot N for
 * the Nth NVMe disk, only when `arb` is attested. One line per disk. Returns
 * the number of disks unlocked. */
uint32_t hw_sed_unlock_all(const hw_plat_t *p, hw_registry_t *r, struct arb_host *arb);
hw_verdict_t vnet_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                       hw_line_t *note);
hw_verdict_t vblk_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                       hw_line_t *note);
hw_verdict_t vscsi_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                        hw_line_t *note);

#endif
