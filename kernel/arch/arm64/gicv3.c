/* gicv3.c — GIC Interrupt Controller driver for ARM64 (GICv2 and GICv3)
 *
 * Hardware-as-code: implements the ARM Generic Interrupt Controller as a
 * virtual device with distributor, redistributor and CPU-interface
 * register maps.
 *
 * The file keeps its gicv3 name because GICv3 is the register map it
 * speaks natively. What changed is that the VERSION is no longer an
 * assumption: gic_init() PROBES GICD_PIDR2 and branches at runtime.
 *
 *   GICv2  4KB distributor frame; PPI/SGI state banked inside the
 *          DISTRIBUTOR; SPI targeting via GICD_ITARGETSR; MMIO CPU
 *          interface (GICC_*). There are NO redistributors -- touching
 *          gicr_base on such a machine is a synchronous external abort,
 *          which is exactly how this was found (ESR EC=0x25,
 *          FAR_EL1=0x080A0014 = GICR_WAKER, on QEMU -M virt).
 *   GICv3  64KB distributor frame; PPI/SGI state in a per-CPU
 *          redistributor; affinity-routed SPIs (GICD_CTLR.ARE); CPU
 *          interface in system registers (ICC_*_EL1). There is no GICC
 *          frame -- touching gicc_base here aborts symmetrically.
 *   other  the driver reports exactly what it read and programs
 *          NOTHING. Every entry point below turns into a no-op, so an
 *          unrecognised controller costs interrupts, never a fault.
 *
 * The probed version is deliberately NOT written back into
 * board_profile_t. That table is `static const` and every field is
 * constant-folded into the instruction stream at compile time (verified
 * by objdump: gicd_base appears as a `movk` immediate, not a load), so a
 * value discovered at runtime cannot honestly live there. board_profile
 * carries board DATA -- where the frames are; gic_state_t below carries
 * what the hardware ANSWERED -- which frames actually exist, how many
 * interrupt lines there are, and which CPU interface to drive.
 *
 * Base addresses still come from board_profile.h (per-board data), not
 * compile-time constants. See board_profile.h for exactly which of those
 * addresses are verified vs. pending real hardware.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "arm64_arch.h"
#include "board_profile.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);

typedef void (*irq_handler_t)(void);

#define GIC_HANDLER_SLOTS 256
static irq_handler_t irq_handlers[GIC_HANDLER_SLOTS];

/* What the hardware answered. Lives in .bss, so before gic_init() runs
 * version == GIC_VERSION_UNKNOWN and every entry point below is inert --
 * an early gic_enable_irq() cannot poke a frame nobody has confirmed
 * exists yet. */
typedef struct {
    uint32_t version;       /* GIC_VERSION_* -- probed, never assumed */
    uint32_t arch_rev;      /* raw GICD_PIDR2 bits [7:4], kept even when unsupported */
    uint32_t pidr2;         /* the word the revision came from (evidence) */
    uint32_t pidr2_offset;  /* which of the two ID offsets answered */
    uint32_t cpuif_sysreg;  /* ID_AA64PFR0_EL1.GIC -- CPU-side corroboration */
    uint32_t num_intids;    /* from GICD_TYPER.ITLinesNumber, not a constant */
    uint64_t gicd_base;
    uint64_t gicc_base;     /* GICv2 MMIO CPU interface; 0 on v3 */
    uint32_t gicc_iidr;     /* GICC_IIDR cross-check word (v2 only) */
    uint64_t gicr_base;     /* THIS CPU's redistributor RD frame; 0 on v2 */
    uint32_t gicr_stride;   /* per-CPU frame stride, from GICR_TYPER.VLPIS */
    bool     gicr_matched;  /* true if a frame's affinity matched MPIDR_EL1 */
} gic_state_t;

static gic_state_t g_gic;

void gic_enable_irq(uint32_t irq);

/* ===== Probe ===== */

/* Read GICD_TYPER and turn ITLinesNumber into a real line count. This is
 * what replaces the old hardcoded "< 256" loop bounds: the distributor
 * is asked how wide it is. Clamped to the architectural maximum so a
 * garbage TYPER cannot run the configuration loops off the frame. */
static uint32_t gic_probe_intids(uint64_t gicd) {
    uint32_t typer = mmio_read(gicd + GICD_TYPER);
    uint32_t n = GICD_TYPER_ITLINES(typer) * 32;
    if (n > GIC_MAX_INTIDS) n = GIC_MAX_INTIDS;
    if (n < 32) n = 32;   /* SGIs+PPIs always exist */
    return n;
}

/* GICv3: find THIS CPU's redistributor instead of assuming it is the
 * first frame. The architected walk is: read GICR_TYPER, compare its
 * Affinity_Value [63:32] against MPIDR_EL1's packed affinity, step by
 * the frame stride (which GICR_TYPER.VLPIS itself reports: 0x20000 for
 * GICv3, 0x40000 when virtual LPIs add two more frames), and stop at
 * GICR_TYPER.Last. Last is the real terminator -- gic_num_cpus is used
 * only as a runaway guard, since walking past the final implemented
 * frame lands in unmapped space (QEMU sizes the redistributor MMIO
 * region to the number of CPUs it actually created, not to the memmap
 * slot the DTB advertises). */
static void gic_probe_redist(const board_profile_t *bp) {
    uint64_t mpidr;
    __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
    uint32_t aff = (uint32_t)((mpidr & 0x00FFFFFFu) |
                              (((mpidr >> 32) & 0xFFu) << 24));

    uint32_t guard = bp->gic_num_cpus ? bp->gic_num_cpus : 1;
    uint64_t frame = bp->gicr_base;
    uint32_t i;

    g_gic.gicr_base = bp->gicr_base;
    g_gic.gicr_stride = GIC_REDIST_STRIDE;
    g_gic.gicr_matched = false;

    for (i = 0; i < guard; i++) {
        uint64_t typer = mmio_read64(frame + GICR_TYPER);
        g_gic.gicr_stride = (typer & GICR_TYPER_VLPIS) ? GIC_REDIST_STRIDE_VLPIS
                                                       : GIC_REDIST_STRIDE;
        if ((uint32_t)(typer >> 32) == aff) {
            g_gic.gicr_base = frame;
            g_gic.gicr_matched = true;
            return;
        }
        if (typer & GICR_TYPER_LAST) break;
        frame += g_gic.gicr_stride;
    }
    /* No affinity match: fall back to the board's base frame and let
     * gic_report() say so rather than silently claiming a match. */
}

/* GICv2: locate the MMIO CPU interface. Board data wins when it has one;
 * otherwise derive gicd_base + 0x10000 -- the 4KB distributor frame
 * padded out to a 64KB slot, which is how QEMU virt and the common SoC
 * layouts place GICC. GICC_IIDR bits [19:16] carry the architecture
 * version and are read back purely as a cross-check: the value is
 * LOGGED, NOT ENFORCED, and it must stay that way. Measured on QEMU
 * 6.2.0 -M virt, GICC_IIDR at 0x080100FC reads back 0x00000000 -- arch
 * field 0, not the 2 the architecture describes -- while the CPU
 * interface underneath it works perfectly (timer PPI 30 acknowledged
 * and EOI'd at a measured 100 Hz through it). Turning this cross-check
 * into a gate would therefore break the exact machine this fix was
 * written for. */
static void gic_probe_cpuif(const board_profile_t *bp) {
    g_gic.gicc_base = bp->gicc_base ? bp->gicc_base : (bp->gicd_base + 0x10000);
    g_gic.gicc_iidr = mmio_read(g_gic.gicc_base + GICC_IIDR);
}

static void gic_probe(const board_profile_t *bp) {
    g_gic.version = GIC_VERSION_UNKNOWN;
    g_gic.gicd_base = bp->gicd_base;
    g_gic.gicc_base = 0;
    g_gic.gicr_base = 0;
    g_gic.gicr_stride = 0;
    g_gic.gicc_iidr = 0;

    /* Step 0 -- CPU-side evidence. A system-register read cannot
     * external-abort, so this costs nothing and is always available:
     * ID_AA64PFR0_EL1.GIC == 0 means this CPU has no ICC_*_EL1 CPU
     * interface at all, i.e. a GICv3 cannot be driven here even if one
     * were present. */
    uint64_t pfr0;
    __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(pfr0));
    g_gic.cpuif_sysreg = (uint32_t)ID_AA64PFR0_GIC(pfr0);

    /* Step 1 -- MMIO evidence at the offset that decodes on BOTH
     * layouts (see GICD_PIDR2_V2 in arm64_arch.h). This must come first:
     * the v3 offset is unmapped on a v2 machine and reading it there is
     * the very abort this probe exists to remove. */
    g_gic.pidr2_offset = GICD_PIDR2_V2;
    g_gic.pidr2 = mmio_read(bp->gicd_base + GICD_PIDR2_V2);
    g_gic.arch_rev = GICD_PIDR2_ARCH(g_gic.pidr2);

    /* Step 2 -- only when step 1 did not answer v1/v2, and only with
     * corroboration that this really is a wide v3+ frame: either the CPU
     * advertises a system-register interface, or 0x0FE8 read back as
     * RES0 (0x00000000), which is what the middle of a 64KB frame looks
     * like and what no conformant v1/v2 distributor can return (its
     * PIDR2 always carries a nonzero architecture revision plus JEP106
     * bits). Without one of those two we refuse the read rather than
     * gamble on an abort. */
    if (g_gic.arch_rev != 1 && g_gic.arch_rev != 2 &&
        (g_gic.cpuif_sysreg != 0 || g_gic.pidr2 == 0)) {
        g_gic.pidr2_offset = GICD_PIDR2_V3;
        g_gic.pidr2 = mmio_read(bp->gicd_base + GICD_PIDR2_V3);
        g_gic.arch_rev = GICD_PIDR2_ARCH(g_gic.pidr2);
    }

    /* Step 3 -- commit, and resolve the frames that version implies.
     * Only revisions this driver can actually DRIVE become a version;
     * v1, v4 and anything else (including the 0xF of an all-ones read)
     * stay UNKNOWN and are reported as such. GICv4 is a superset of v3
     * and its v3 frames would very likely work, but "very likely" is not
     * a measurement and there is no v4 silicon here to check it on. */
    if (g_gic.arch_rev == 2) {
        g_gic.version = GIC_VERSION_V2;
        gic_probe_cpuif(bp);
    } else if (g_gic.arch_rev == 3) {
        g_gic.version = GIC_VERSION_V3;
        gic_probe_redist(bp);
    }

    g_gic.num_intids = (g_gic.version == GIC_VERSION_UNKNOWN)
                     ? 0 : gic_probe_intids(bp->gicd_base);
}

/* One line of evidence per boot, printed before anything is programmed,
 * so the boot log states what was MEASURED rather than what was hoped.
 * Deliberately not routed through boot_msg(): the probe result belongs
 * to the driver, and gicv3.c must stay usable on a machine whose
 * evidence chain has not been brought up yet. */
static void gic_hex(uint64_t v) {
    /* uart_put_hex() always emits 16 digits, which turns a probe line
     * into a wall of zeroes. Trim to the significant nibbles (minimum
     * two) so the boot log stays readable as evidence. */
    char buf[19];
    int n = 0, i;
    int top = 15;
    while (top > 1 && ((v >> (top * 4)) & 0xF) == 0) top--;
    buf[n++] = '0';
    buf[n++] = 'x';
    for (i = top; i >= 0; i--) {
        int nib = (int)((v >> (i * 4)) & 0xF);
        buf[n++] = (char)(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
    buf[n] = 0;
    uart_puts(buf);
}

static void gic_report(void) {
    uart_puts("  [GIC PROBE] GICD_PIDR2@+");
    gic_hex(g_gic.pidr2_offset);
    uart_puts(" = ");
    gic_hex(g_gic.pidr2);
    uart_puts(" -> arch rev ");
    uart_put_dec(g_gic.arch_rev);
    uart_puts(", ID_AA64PFR0_EL1.GIC=");
    uart_put_dec(g_gic.cpuif_sysreg);
    uart_puts("\n");

    switch (g_gic.version) {
    case GIC_VERSION_V2:
        uart_puts("  [GIC PROBE] GICv2: dist ");
        gic_hex(g_gic.gicd_base);
        uart_puts(" cpuif ");
        gic_hex(g_gic.gicc_base);
        uart_puts(" (GICC_IIDR ");
        gic_hex(g_gic.gicc_iidr);
        uart_puts(", arch field ");
        uart_put_dec(GICC_IIDR_ARCH(g_gic.gicc_iidr));
        uart_puts("), no redistributors, ");
        uart_put_dec(g_gic.num_intids);
        uart_puts(" INTIDs\n");
        break;
    case GIC_VERSION_V3:
        uart_puts("  [GIC PROBE] GICv3: dist ");
        gic_hex(g_gic.gicd_base);
        uart_puts(" redist ");
        gic_hex(g_gic.gicr_base);
        uart_puts(" stride ");
        gic_hex(g_gic.gicr_stride);
        uart_puts(g_gic.gicr_matched ? " (affinity matched), "
                                     : " (NO affinity match -- using base frame), ");
        uart_put_dec(g_gic.num_intids);
        uart_puts(" INTIDs\n");
        break;
    default:
        uart_puts("  [GIC DEGRADED] architecture revision ");
        uart_put_dec(g_gic.arch_rev);
        uart_puts(" is not GICv2 or GICv3 -- this driver will program NO GIC "
                  "register and every gic_* call is a no-op. Interrupt-driven "
                  "paths (timer PPI, UART SPI) will NOT run; polled paths are "
                  "unaffected.\n");
        break;
    }
}

/* ===== Version-specific init ===== */

static void gic_init_v2(void) {
    uint64_t gicd = g_gic.gicd_base;
    uint64_t gicc = g_gic.gicc_base;
    uint32_t n = g_gic.num_intids;
    uint32_t i;

    /* Quiesce both halves while reconfiguring. */
    mmio_write(gicd + GICD_CTLR, 0);
    mmio_write(gicc + GICC_CTLR, 0);

    /* Priority 0xA0 for every implemented INTID, packed 4 per word --
     * GICD_IPRIORITYR is a byte-per-interrupt array, so writing it one
     * byte at a time through a 32-bit mmio_write() would hit unaligned
     * addresses and trap. Same reasoning as the v3 path below; the
     * bound is now the probed line count, not a fixed 256. */
    for (i = 0; i < n; i += 4) {
        mmio_write(gicd + GICD_IPRIORITYR(i), 0xA0A0A0A0);
    }

    /* SPI targeting. This is the register GICv3 replaced with affinity
     * routing, and its absence from the old v3-only driver is why that
     * driver could never have worked on a v2 even if it had survived the
     * redistributor. INTIDs 0-31 are banked and read-only here, so the
     * loop starts at 32. 0x01010101 = target CPU 0 (the only CPU this
     * kernel brings up). */
    for (i = 32; i < n; i += 4) {
        mmio_write(gicd + GICD_ITARGETSR(i), 0x01010101);
    }

    /* SPIs level-triggered: 2 bits per INTID, 16 INTIDs per ICFGR word.
     * PPI/SGI configuration (words 0 and 1) is left alone -- it is
     * read-only or implementation-fixed, and the generic timer PPI is
     * level-sensitive by architecture. */
    for (i = 32; i < n; i += 16) {
        mmio_write(gicd + GICD_ICFGR(i / 16), 0x00000000);
    }

    /* No GICD_IGROUPR pass here, unlike the v3 path. On a GICv2 without
     * the security extensions -- the configuration QEMU virt builds and
     * the one a non-secure-only boot sees -- IGROUPR is RAZ/WI and every
     * interrupt is Group 0, which is what GICC_CTLR.Enable and the
     * GICC_IAR/EOIR pair below act on. Writing it would be a no-op that
     * reads as intent. */

    /* CPU interface. PMR must be numerically HIGHER than the 0xA0
     * priority set above or the interface masks everything it is handed;
     * 0xF0 leaves headroom in the 5 priority bits a GICv2 typically
     * implements. */
    mmio_write(gicc + GICC_PMR, 0xF0);

    mmio_write(gicd + GICD_CTLR, GICD_CTLR_ENABLE_G0);
    mmio_write(gicc + GICC_CTLR, GICC_CTLR_ENABLE);
}

static void gic_init_v3(void) {
    uint64_t gicd = g_gic.gicd_base;
    uint64_t gicr = g_gic.gicr_base;
    uint32_t n = g_gic.num_intids;
    uint32_t i;

    /* The CPU interface must be switched to system registers BEFORE any
     * ICC_* access. ICC_SRE_EL1.SRE (S3_0_C12_C12_5, bit 0) is RAO on a
     * CPU with no lower-EL alternative, but on real silicon whose
     * firmware left SRE clear every ICC_* below would trap as
     * undefined. Set it, isb, and read it back: if it will not stick,
     * the sysreg interface is unusable and we degrade rather than
     * execute a string of traps. */
    uint64_t sre;
    __asm__ __volatile__("mrs %0, S3_0_C12_C12_5" : "=r"(sre));
    if (!(sre & 1)) {
        sre |= 1;
        __asm__ __volatile__("msr S3_0_C12_C12_5, %0" :: "r"(sre));
        __asm__ __volatile__("isb");
        __asm__ __volatile__("mrs %0, S3_0_C12_C12_5" : "=r"(sre));
    }
    if (!(sre & 1)) {
        g_gic.version = GIC_VERSION_UNKNOWN;
        uart_puts("  [GIC DEGRADED] GICv3 found but ICC_SRE_EL1.SRE will not "
                  "set -- no system-register CPU interface. Programming NO GIC "
                  "register; every gic_* call is a no-op.\n");
        return;
    }

    /* Enable GIC distributor. Writing just `1` (EnableGrp0) is not
     * enough: every interrupt this path configures is Group 1 (see the
     * IGROUPR writes below), so EnableGrp1 must also be set or the
     * distributor drops them before they ever reach a CPU interface,
     * regardless of their individual enable/priority bits. ARE is set
     * too since GICv3's native SPI affinity targeting depends on it --
     * that bit is RES0 on a GICv2, which is why the v2 path above writes
     * GICD_ITARGETSR instead of inheriting this line. */
    mmio_write(gicd + GICD_CTLR,
               GICD_CTLR_ARE | GICD_CTLR_ENABLE_G1 | GICD_CTLR_ENABLE_G0);

    /* Configure all SPIs (Shared Peripheral Interrupts) as group 0 (secure)
     * or group 1 (non-secure). We use group 1. */
    for (i = 32; i < n; i += 32) {
        mmio_write(gicd + GICD_IGROUPR(i / 32), 0xFFFFFFFF);
    }

    /* Set priority for all interrupts to 0xA0 (mid priority).
     * GICD_IPRIORITYR is a byte-per-interrupt register array; writing
     * it one byte at a time via a 32-bit mmio_write() hits unaligned
     * addresses for every n not a multiple of 4 (e.g. n=1 -> 0x...401)
     * and traps as an alignment fault. Pack 4 interrupts per word and
     * write on 4-byte-aligned boundaries instead. */
    for (i = 0; i < n; i += 4) {
        mmio_write(gicd + GICD_IPRIORITYR(i), 0xA0A0A0A0);
    }

    /* Wake up the redistributor. GICR_CTLR has no self-setting "ready"
     * bit -- the architected wake sequence is: clear GICR_WAKER's
     * ProcessorSleep (bit 1), then poll ChildrenAsleep (bit 2) until
     * the redistributor clears it. This whole block, and the SGI frame
     * writes after it, exist ONLY on GICv3+ -- there is no v2
     * translation, which is why gic_init_v2() skips it entirely rather
     * than aiming it somewhere else. */
    uint32_t waker = mmio_read(gicr + GICR_WAKER);
    waker &= ~(1u << 1);
    mmio_write(gicr + GICR_WAKER, waker);
    while (mmio_read(gicr + GICR_WAKER) & (1u << 2)) { }

    /* PPIs/SGIs (IRQ 0-31 -- this includes IRQ_TIMER=30, the generic
     * timer's PPI) live in the redistributor's SGI_base frame, not the
     * distributor. Without this, GICD_IGROUPR/ISENABLER above (which
     * only ever touched SPIs, i >= 32) leave every PPI in its
     * power-on-reset Group 0 state with its enable bit clear, so the
     * timer interrupt can never reach the Group-1 IAR/EOI path used by
     * gic_handle_irq() -- the CPU wakes from WFI never, and the whole
     * event-cycle loop hangs silently forever after boot. Route PPIs
     * into Group 1 and give them the same mid priority as SPIs here. */
    uint64_t sgi_base = gicr + GIC_REDIST_SGI_OFFSET;
    mmio_write(sgi_base + GICR_IGROUPR0, 0xFFFFFFFF);
    for (i = 0; i < 32; i += 4) {
        mmio_write(sgi_base + GICR_IPRIORITYR0 + i, 0xA0A0A0A0);
    }

    /* Set priority mask to allow all priorities */
    __asm__ __volatile__("msr S3_0_C4_C6_0, %0" :: "r"(0xFF));

    /* Enable group 1 interrupts at CPU interface (ICC_IGRPEN1_EL1 =
     * S3_0_C12_C12_7, a read-write register). Using op2=0 here would hit
     * ICC_IAR1_EL1 -- a read-only register -- and trap as an undefined
     * instruction the moment it was written. */
    uint64_t ctlr;
    __asm__ __volatile__("mrs %0, S3_0_C12_C12_7" : "=r"(ctlr));
    ctlr |= 1; /* Enable GIC */
    __asm__ __volatile__("msr S3_0_C12_C12_7, %0" :: "r"(ctlr));
}

void gic_init(void) {
    const board_profile_t *bp = board_get_profile();

    int i;
    for (i = 0; i < GIC_HANDLER_SLOTS; i++) irq_handlers[i] = 0;

    gic_probe(bp);
    gic_report();

    switch (g_gic.version) {
    case GIC_VERSION_V2: gic_init_v2(); break;
    case GIC_VERSION_V3: gic_init_v3(); break;
    default:             /* reported above; program nothing */ break;
    }
}

/* ===== Queries (for the boot log and the hardware model) ===== */

uint32_t gic_get_version(void) {
    return g_gic.version;
}

/* Static strings, safe to hand to boot_msg()/boot_evidence_record().
 * Says what was found, so a reader of the boot log is looking at
 * evidence rather than at a banner someone typed. */
const char *gic_status_msg(void) {
    switch (g_gic.version) {
    case GIC_VERSION_V2:
        return "  [DRIVER ONLINE] GICv2 distributor + MMIO CPU interface (probed)";
    case GIC_VERSION_V3:
        return "  [DRIVER ONLINE] GICv3 distributor + redistributor (probed)";
    default:
        return "  [DEGRADED] GIC version not recognised -- controller left "
               "unprogrammed, no interrupts";
    }
}

const char *gic_device_name(void) {
    switch (g_gic.version) {
    case GIC_VERSION_V2: return "gicv2";
    case GIC_VERSION_V3: return "gicv3";
    default:             return "gic-unknown";
    }
}

/* ===== Interrupt routing =====
 *
 * Every public entry point below branches on the probed version. Putting
 * the check only in gic_init() would not have been enough: gic_enable_irq
 * and gic_disable_irq reach the redistributor independently for any
 * irq < 32, and gic_register_handler(IRQ_TIMER=30) is called one line
 * after gic_init(), so a version check confined to init would simply
 * move the abort down one line. arm64_compat.h aliases pic_mask/
 * pic_unmask onto these two as well. */

void gic_register_handler(uint32_t irq, irq_handler_t handler) {
    if (irq < GIC_HANDLER_SLOTS) irq_handlers[irq] = handler;
    gic_enable_irq(irq);
}

void gic_enable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = 1u << (irq % 32);

    if (g_gic.version == GIC_VERSION_V3 && irq < 32) {
        /* PPI/SGI: per-CPU enable lives in the redistributor's SGI_base
         * frame (see GIC_REDIST_SGI_OFFSET); GICD_ISENABLER only ever
         * reaches SPIs on a v3. */
        mmio_write(g_gic.gicr_base + GIC_REDIST_SGI_OFFSET + GICR_ISENABLER0, bit);
        return;
    }
    if (g_gic.version == GIC_VERSION_UNKNOWN) return;

    /* GICv2 (any INTID) and GICv3 (SPIs only): the distributor. On a v2
     * GICD_ISENABLER word 0 IS the per-CPU banked PPI/SGI enable, so the
     * irq < 32 special case simply does not exist there. */
    mmio_write(g_gic.gicd_base + GICD_ISENABLER(reg), bit);
}

void gic_disable_irq(uint32_t irq) {
    uint32_t reg = irq / 32;
    uint32_t bit = 1u << (irq % 32);

    if (g_gic.version == GIC_VERSION_V3 && irq < 32) {
        mmio_write(g_gic.gicr_base + GIC_REDIST_SGI_OFFSET + GICR_ICENABLER0, bit);
        return;
    }
    if (g_gic.version == GIC_VERSION_UNKNOWN) return;

    mmio_write(g_gic.gicd_base + GICD_ICENABLER(reg), bit);
}

void gic_handle_irq(void) {
    uint64_t iar;

    if (g_gic.version == GIC_VERSION_V2) {
        /* GICv2 MMIO acknowledge. The FULL IAR word (INTID + the CPUID
         * field in bits [12:10]) must be echoed back to GICC_EOIR, not
         * the masked INTID -- the CPU interface uses those bits to
         * retire the right entry. */
        iar = mmio_read(g_gic.gicc_base + GICC_IAR);
    } else if (g_gic.version == GIC_VERSION_V3) {
        /* ICC_IAR1_EL1 (S3_0_C12_C12_0) -- Group 1 ack, matching how the
         * v3 path configured its interrupts. ICC_IAR0_EL1
         * (S3_0_C12_C8_0) is the Group 0 register and never reflects
         * Group 1 interrupts. */
        __asm__ __volatile__("mrs %0, S3_0_C12_C12_0" : "=r"(iar));
    } else {
        return;   /* unprobed or unsupported: no CPU interface to ack */
    }

    uint32_t irq = (uint32_t)(iar & GIC_INTID_MASK);

    /* Spurious (1023) means "nothing pending" and must NOT be EOI'd.
     * The dispatch bound is the handler-table size, not 1023: reading
     * irq_handlers[irq] for irq up to 1022 walked off a 256-entry array. */
    if (irq == GIC_INTID_SPURIOUS) return;
    if (irq < GIC_HANDLER_SLOTS && irq_handlers[irq]) {
        irq_handlers[irq]();
    }

    /* End of interrupt: GICC_EOIR on v2, ICC_EOIR1_EL1
     * (S3_0_C12_C12_1) on v3. */
    if (g_gic.version == GIC_VERSION_V2) {
        mmio_write(g_gic.gicc_base + GICC_EOIR, (uint32_t)iar);
    } else {
        __asm__ __volatile__("msr S3_0_C12_C12_1, %0" :: "r"(iar));
    }
}

void gic_eoi(uint32_t irq) {
    if (g_gic.version == GIC_VERSION_V2) {
        mmio_write(g_gic.gicc_base + GICC_EOIR, irq);
        return;
    }
    if (g_gic.version == GIC_VERSION_UNKNOWN) return;
    __asm__ __volatile__("msr S3_0_C12_C12_1, %0" :: "r"((uint64_t)irq));
}
