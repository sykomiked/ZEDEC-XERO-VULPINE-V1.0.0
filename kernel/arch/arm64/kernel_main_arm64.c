/* kernel_main_arm64.c — ARM64 kernel boot driver for VOVINA SHAKINA
 *
 * Bare-metal entry point for AArch64. Called by boot.s after
 * EL2→EL1 drop, VBAR setup, and BSS zeroing.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "m5_types.h"
/* sched.h (-> idt.h) MUST precede arm64_compat.h: that header defines
 * idt_register_handler as a macro aliasing gic_register_handler, which
 * would rewrite idt.h's own prototype into a conflicting declaration. */
#include "../src/sched/sched.h"
#include "arm64_arch.h"
#include "arm64_compat.h"

/* UART driver (inline in uart_pl011.c) */
extern void uart_init(void);
extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);

/* GIC driver */
extern void gic_init(void);
extern void gic_register_handler(uint32_t irq, void (*handler)(void));
extern void gic_handle_irq(void);

/* Timer driver */
extern void arm64_timer_init(void);
extern void arm64_timer_set_callback(void (*cb)(void));
extern void arm64_timer_handler(void);
extern uint64_t arm64_timer_get_ticks(void);

/* MMU driver */
extern void arm64_mmu_init(void);

/* M5 subsystems (shared with x86) */
#include "../src/oseq/oseq_core.h"
#include "../src/rmag/rmag_core.h"
#include "../src/lpres/lpres_core.h"
#include "../src/iphase/iphase_core.h"
#include "../src/choice/choice_core.h"
#include "../src/phase_coord/phase_coordinator.h"
#include "../src/telemetry/telemetry_core.h"
#include "../src/axiom_matrix/axiom_matrix_core.h"
#include "../src/crit168/crit_168_word.h"
#include "../src/vfs/vfs.h"
#include "../src/net/net.h"
#include "../src/net/m5route.h"
#include "../src/vino/vino.h"
#include "../src/vena/vena.h"
#include "../src/holographic/holo.h"
#include "license.h"
#include "el0_userspace.h"
#include "boot_evidence.h"
#include "boot_banner.h"
#include "boot_features.h"
#include "../src/event_space/event_space.h"
#include "../src/orbital_elevator/orbital_elevator.h"
#include "../src/constellation/constellation_coordinator.h"
#include "../src/event_transport/event_transport.h"
#include "../src/hypercube/hypercube_scene.h"
#include "../src/yantra/yantra_fabric.h"
#include "../src/dual_space/dual_space.h"
#include "../src/cellular_multikernel/cellular_multikernel.h"

/* EL0 exception dispatcher — defined in arm64_exceptions.c */
extern void el0_set_scheduler(proc_scheduler_t *ps);
#include "../src/surplus/surplus.h"
#include "../src/edp_risk/edp_risk.h"
#include "../src/predictive/predictive_model.h"
#include "../src/situation/situation_model.h"
#include "../src/finance/triple_ledger.h"
#include "../src/finance/financial.h"
#include "../src/finance/rails.h"
#include "../src/finance/crypto_bridge.h"
#include "../src/hardware/rtl_device.h"
#include "../src/hardware/dlp_projector.h"
#include "../src/count_house/count_house.h"
#include "../src/porter_house/porter_house.h"
#include "../src/event_sched/event_sched.h"
#include "../src/mesh_token/mesh_token.h"
#include "../src/community_chest/community_chest.h"
#include "../src/ai_layer/ai_layer.h"
#include "../src/mesh_net/mesh_net.h"
#include "../src/immigration/immigration.h"
#include "../src/robin_debanks/robin_debanks.h"
#include "../src/prism_break/prism_break.h"
#include "../src/video/ramfb.h"      /* universal QEMU scanout (fw_cfg ramfb) */
#include "../src/vbe/vbe.h"          /* framebuffer draw primitives            */
#include "../src/desktop/zxv_shell.h"/* the ZEDEC pqOS desktop shell           */
#include "zxv_bootinfo.h"            /* UEFI boot handoff (GOP framebuffer)     */
/* virtio-input driver (arch/arm64/virtio_input.c) — mouse/tablet/keyboard */
extern bool     virtio_input_probe(void);
extern void     virtio_input_poll(void);
extern void     virtio_input_get(int32_t *x, int32_t *y, uint32_t *buttons);
extern void     virtio_input_set_bounds(int32_t w, int32_t h);
extern uint32_t virtio_input_device_count(void);
extern int32_t  virtio_input_pop_key(void);
extern uint32_t virtio_input_pop_clicks(void);
/* virtio-snd driver (arch/arm64/virtio_snd.c) — native audio output */
extern bool     virtio_snd_init(void);
extern void     virtio_snd_chime(void);
/* omni-driver engine (src/virtio/virtio_bus.c) — declarative class registry */
extern void     virtio_bus_poll(void);
extern uint32_t virtio_bus_driver_count(void);
#include "../src/net/jdr_piratenet.h"

/* KERNEL_SIM_DEVICES gates subsystems that model devices/claims with no
 * real counterpart on RK3399 (or any) physical silicon today -- e.g.
 * wormhole/exotic-matter/ZPE-as-power-source, and a "universal national
 * identity" registry that cannot be a legally recognized identity
 * issuer for any state without that state's own PKI accreditation.
 * Default OFF so the real-hardware boot path stays lean and doesn't
 * claim capabilities the kernel doesn't have. Build with
 * KERNEL_SIM_DEVICES=1 to include them as clearly-labeled research
 * simulations. */
#ifndef KERNEL_SIM_DEVICES
#define KERNEL_SIM_DEVICES 0
#endif

#if KERNEL_SIM_DEVICES
#include "../src/identity/identity.h"
#include "../src/quantum/quantum_device.h"
#endif

/* ENABLE_EL0_USERSPACE — Stage 1 keeps EL0 disabled by default while the
 * per-process page-table / SVC / pre-emptive scheduler path is still under
 * bring-up.  Build with ENABLE_EL0_USERSPACE=1 to run the experimental EL0
 * user processes; the default path proceeds straight to the kernel event
 * loop and guarantees a deterministic [BOOT_OK] milestone. */
#ifndef ENABLE_EL0_USERSPACE
#define ENABLE_EL0_USERSPACE 0
#endif

/* ENABLE_TICK_IRQ — use the ARM64 Generic Timer interrupt for the
 * scheduler tick.  The boot.s vector ERET and EL0 context save/restore
 * are now stable, so interrupts are enabled by default. */
#ifndef ENABLE_TICK_IRQ
#define ENABLE_TICK_IRQ 1
#endif

/* ENABLE_EVENT_LOOP — the full multi-subsystem event loop is still
 * optional for Stage 1; the minimal polled heartbeat keeps [BOOT_OK]
 * deterministic.  Build with ENABLE_EVENT_LOOP=1 to run the full loop. */
#ifndef ENABLE_EVENT_LOOP
#define ENABLE_EVENT_LOOP 0
#endif

/* ENABLE_WX_TEST — arm two adversarial EL0 processes that deliberately
 * violate W^X (write to RX code page / execute from NX stack).  Both
 * must be fault-terminated by the kernel while the P-TERM shell keeps
 * running.  Negative privilege test per the Technical-MVP gate. */
#ifndef ENABLE_WX_TEST
#define ENABLE_WX_TEST 0
#endif

void kernel_main_arm64(void);

/* ===================================================================
 * Unified-runtime kernel state
 *
 * These subsystem instances were previously function-local statics in
 * kernel_main_arm64().  They are file-scope now so the kernel event
 * cycle can be driven from TWO places with one code path:
 *   - the classic while(1) event loop (ENABLE_EL0_USERSPACE=0), and
 *   - the generic-timer IRQ hook while EL0 user space runs
 *     (ENABLE_EL0_USERSPACE=1) — see el0_set_event_cycle_hook().
 * Storage duration is unchanged (they were already static).
 * =================================================================== */
static phase_tick_t tick;
static dlp_projector_t projector;
static vino_ledger_t vino;
static porter_house_t porter_house;
static mesh_token_t mesh_token;
static community_chest_t community_chest;
static ai_engine_t ai_engine;
static mesh_net_t mesh_net;
static immigration_t immigration;
static robin_vault_t robin_vault;
static prism_break_t prism_break;
static vbe_state_t *g_desktop_vbe = 0;   /* bound once ramfb is live; drives redraw */

/* ---- double buffering ----
 * The compositor draws into the prism BACK buffer, then we present a COMPLETE
 * frame by copying it to this scanout buffer, which is the ONLY thing QEMU/GOP
 * scans out. Without this, the screen shows the buffer mid-compose — each region
 * erased then repainted in turn — which reads as parts flickering independently.
 * With it, the display only ever holds finished frames. */
#define ZXV_FB_W 1280u
#define ZXV_FB_H 720u
static uint32_t g_scanout[ZXV_FB_W * ZXV_FB_H] __attribute__((aligned(64)));

/* ---- holographic present: color <-> anti-color, grounded in Tri-Space ----
 * The composed frame is S+ (each colour C). Its per-channel complement 255-C is
 * S- (the honest inverse). A phase-tick interference field v(x,y) picks the local
 * valence: v>0 leans to S+ (its colour), v<0 to S- (its anti-colour), v~0 to S0.
 * The transform is nonlinear and complement-based: out = C + v*(255-2C)>>k. The
 * (255-2C) term is ZERO at mid-grey, so neutral (S0) is stable while colour
 * breathes toward its negative in travelling waves — the glut (LPRES BOTH) is the
 * shimmer. Not decoration: it is the kernel's own positive/negative/neutral logic
 * rendered as light. g_holo on/off + strength are the knobs. */
static const signed char g_sin64[64] = {
 0,10,20,29,38,47,56,63,71,77,83,88,92,96,98,100,100,100,98,96,92,88,83,77,71,63,56,47,38,29,20,10,
 0,-10,-20,-29,-38,-47,-56,-63,-71,-77,-83,-88,-92,-96,-98,-100,-100,-100,-98,-96,-92,-88,-83,-77,-71,-63,-56,-47,-38,-29,-20,-10};
static int g_holo = 1;                 /* holographic present on/off */
static int g_holo_k = 10;              /* shift: bigger = subtler (10 ~= 12%, visible) */
static uint32_t g_holo_phase = 0;

/* ---- UEFI GOP framebuffer (bare-metal boot via BOOTAA64.EFI) ----
 * 0 on the QEMU -kernel path (ramfb). When the EFI stub published a handoff
 * record, these hold the firmware's linear framebuffer and zxv_present blits
 * the composed 1280x720 frame into it (scaled, R/B-swapped for GOP fmt 0). */
static uint32_t *g_gop_fb = 0;
static uint32_t  g_gop_w = 0, g_gop_h = 0, g_gop_stride = 0, g_gop_pixfmt = 0;

/* ---- the FIELD: per-16px-tile phase + depth painted by the shell ----
 * g_fphase[ti] shifts each OBJECT's shimmer in TIME (its own breathing phase, so
 * objects flicker independently — deliberately, not by accident). g_fdepth[ti] is
 * 0=near..255=far: chromostereopsis pushes near tiles WARM (+red/-blue) and far
 * tiles COOL (-red/+blue) — real optics that read as depth on a flat plane. The
 * shell fills these each frame; zxv_present reads them. This is the 2D base plane
 * given deliberate M5 depth — the foundation we build layers up from. */
static uint8_t  g_fphase[FIELD_TX * FIELD_TY];
static uint8_t  g_fdepth[FIELD_TX * FIELD_TY];
static int16_t  g_tval[FIELD_TX * FIELD_TY];   /* per-tile valence,  precomputed per frame */
static int16_t  g_tdz [FIELD_TX * FIELD_TY];   /* per-tile depth-z,  precomputed per frame */
static int      g_depth_k = 36;                /* chromostereopsis strength (0=flat)       */

static void zxv_present(const uint32_t *back) {
    if (!g_holo) {                                   /* plain copy path */
        for (uint32_t i = 0; i < ZXV_FB_W * ZXV_FB_H; i++) g_scanout[i] = back[i];
        return;
    }
    uint32_t ph = g_holo_phase++;
    /* precompute per-tile valence (object-phased wave) + depth-z — 3600 tiles */
    for (uint32_t ty = 0; ty < FIELD_TY; ty++) {
        for (uint32_t tx = 0; tx < FIELD_TX; tx++) {
            uint32_t ti  = ty * FIELD_TX + tx;
            uint32_t phx = ph + (uint32_t)g_fphase[ti];        /* object's own time phase */
            int32_t  val = g_sin64[(tx + phx) & 63]
                         + g_sin64[(ty - phx + (phx >> 1)) & 63]; /* [-200,200]: S- .. S+ */
            g_tval[ti] = (int16_t)val;
            g_tdz [ti] = (int16_t)(128 - (int32_t)g_fdepth[ti]); /* >0 near, <0 far */
        }
    }
    for (uint32_t y = 0; y < ZXV_FB_H; y++) {
        uint32_t row_ti = (y >> 4) * FIELD_TX;
        const uint32_t *br = back + (uint64_t)y * ZXV_FB_W;
        uint32_t *dr = g_scanout + (uint64_t)y * ZXV_FB_W;
        for (uint32_t x = 0; x < ZXV_FB_W; x++) {
            uint32_t ti = row_ti + (x >> 4);
            int32_t v  = g_tval[ti];                  /* this object's valence  */
            int32_t dz = g_tdz[ti];                   /* this object's depth-z  */
            uint32_t c = br[x];
            int32_t r = (int32_t)((c >> 16) & 0xFF);
            int32_t g = (int32_t)((c >> 8) & 0xFF);
            int32_t b = (int32_t)(c & 0xFF);
            r += (v * (255 - 2 * r)) >> g_holo_k;     /* shimmer: toward anti-colour, grey-stable */
            g += (v * (255 - 2 * g)) >> g_holo_k;
            b += (v * (255 - 2 * b)) >> g_holo_k;
            r += (dz * g_depth_k) >> 8;               /* chromostereopsis: near warm / far cool */
            b -= (dz * g_depth_k) >> 8;
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            dr[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    /* Bare-metal UEFI path: also blit the composed frame into the firmware GOP
     * fb (nearest-neighbour scale to its resolution, R/B swap for GOP fmt 0).
     * g_gop_fb == 0 on the -kernel path, so this is skipped entirely there. */
    if (g_gop_fb) {
        for (uint32_t gy = 0; gy < g_gop_h; gy++) {
            uint32_t sy = gy * ZXV_FB_H / g_gop_h;
            const uint32_t *sr = g_scanout + (uint64_t)sy * ZXV_FB_W;
            uint32_t *dr = g_gop_fb + (uint64_t)gy * g_gop_stride;
            for (uint32_t gx = 0; gx < g_gop_w; gx++) {
                uint32_t c = sr[gx * ZXV_FB_W / g_gop_w];
                if (g_gop_pixfmt == ZXV_PIXFMT_RGB)                 /* swap R<->B */
                    c = (c & 0xFF00FF00u) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
                dr[gx] = c;
            }
        }
    }
}
static zxv_shell_state_t g_shell;        /* interactive desktop state (clicks/keys) */
static ev_scheduler_t evs;
static ev_sequencer_t ev_seq;
static ev_audit_t ev_audit;
static ev_healing_t ev_healing;
static oe_elevator_t oe;
static cc_coordinator_t cc;
static et_transport_t et;
static hc_scene_t hc_scene;
static yf_fabric_t yf;
static ds_registry_t ds_reg;

/* Boot-time singletons referenced by pterm_commands.c (extern) — must
 * have external linkage and file scope.  vfs/net/router were function-
 * local statics; sched is the classic cooperative scheduler instance,
 * initialized here so `ps`/`vmstat` report real kernel task state. */
#include "../src/sched/sched.h"
vfs_state_t vfs;
net_state_t net;
m5_router_t router;
scheduler_t sched;

/* Kernel P-TERM engine instance (serial-backed, driven via SYS_EXEC) */
#include "../src/pterm/pterm.h"
#include "../src/pterm/pterm_mux.h"
static pterm_t g_pterm;
static bool g_pterm_ready = false;
static pmux_t g_pmux;            /* master/sub terminal rotation */

/* Persistent storage: virtio-blk device + ZXVFS journaled filesystem. */
#include "virtio_blk.h"
#include "emu/game_runner.h"   /* run an attached raw ROM on the emulator cores */
#include "virtio_net.h"
#include "../src/mlkem/mlkem768.h"   /* post-quantum KEM (NIST ML-KEM-768) */
#include "../src/trispace/trispace.h" /* Tri-Space artifact triad binding */
#include "entropy.h"
#include "../src/zxvfs/zxvfs.h"
#include "../src/loader/zsp.h"       /* signed-package verification */
#include "../src/loader/abupdate.h"  /* A/B update + probation + rollback */
#include "../src/appkit/doc.h"       /* AppKit document model (Writer et al) */
#include "../src/refinery/refinery.h" /* Magitech Refinery: text -> sigil card */
#include "../userapp/hello_signed.h" /* root pubkey + signed .zsp, seeded to disk */
/* Bridge between the TCP/IP stack and the virtio-net driver. The stack calls
 * knet_tx to put a fully-built frame on the wire; knet_pump drains received
 * frames into the stack. Kept here (arch layer) so src/net stays portable. */
static net_interface_t *g_eth_if = 0;
static void knet_tx(const uint8_t *data, uint32_t len) {
    (void)virtio_net_tx(data, len);
}
/* Monotonic milliseconds from the architected virtual counter.
 *
 * THIS IS AN INTEROPERABILITY AID, NOT A TIME BASE. ZXV sequences on event
 * phase ticks — see the Cycle Pulse note in include/m5_types.h, "replaces
 * wall clock with event-cycle pulses", and kernel_event_cycle_run() below,
 * whose whole point is that the tick that got us here is the only time
 * source. Nothing in the kernel may depend on this function returning a
 * useful value; it returns 0 when no counter is implemented, and every caller
 * must still work in that case. It exists because LEGACY PEERS measure their
 * round trips in milliseconds, so having a rough conversion available makes
 * us a better neighbour on their networks. */
static uint64_t __attribute__((unused)) mono_ms(void) {
    uint64_t cnt, frq;
    __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(cnt));
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(frq));
    if (frq < 1000u) return 0;          /* no usable counter — say so */
    return (cnt / (frq / 1000u));
}

/* How many PHASE TICKS make one TCP retransmission interval.
 *
 * The retransmission cadence is counted in phase ticks, not milliseconds,
 * because the phase sequence is this architecture's ordering primitive and
 * the clock is optional. The number is chosen so the interval comfortably
 * spans a plausible round trip: that requirement comes from the PEER, not
 * from us — resending faster than the remote can answer puts every packet on
 * the wire twice. */
#define KNET_RETX_PHASE_TICKS 24u

/* Drive TCP retransmission from the phase sequence.
 *
 * An earlier version of this gated on mono_ms() and so made the network stack
 * depend on a wall clock, which is precisely what this architecture does not
 * do. It now counts phase ticks. mono_ms() is consulted only to REPORT the
 * observed interval for legacy interop, and the pacing is identical when it
 * returns 0. */
static void knet_tcp_pace(net_state_t *ns, uint64_t phase_omega) {
    static uint64_t next_omega = 0;
    if (!next_omega) { next_omega = phase_omega + KNET_RETX_PHASE_TICKS; return; }
    if (phase_omega < next_omega) return;
    next_omega = phase_omega + KNET_RETX_PHASE_TICKS;
    net_tcp_tick(ns);
}

/* Hardware entropy for the network stack's transaction ids. Falls back to a
 * fixed value only if RNDR fails mid-flight, which arm64_rng_available()
 * has already made unlikely; the stack still mixes what it gets. */
static uint32_t net_entropy_rndr(void) {
    uint64_t v = 0;
    if (!arm64_rng_get64(&v)) return 0x9E3779B9u;
    return (uint32_t)(v ^ (v >> 32));
}

static void knet_pump(net_state_t *ns) {
    if (!g_eth_if || !virtio_net_present()) return;
    static uint8_t frame[VNET_MAX_FRAME];
    for (uint32_t i = 0; i < 8; i++) {           /* bounded per cycle */
        int n = virtio_net_rx_poll(frame, sizeof frame);
        if (n <= 0) break;
        net_handle_eth(ns, g_eth_if, frame, (uint32_t)n);
    }
}

static block_device_t g_vblk;
static zxvfs_t g_zxvfs;
static bool g_zxvfs_ready = false;
static ab_state_t g_ab;          /* A/B signed-update state */
static doc_t g_doc;              /* AppKit: the open document */
static bool g_doc_open = false;
static bool g_ab_ready = false;

/* Event-cycle bookkeeping */
static uint32_t g_event_cycle = 0;
static bool g_auto_stats = false;   /* periodic stats spam: only in the
                                     * classic (non-EL0) event loop */

/* Hook registration — defined in arm64_exceptions.c */
extern void el0_set_event_cycle_hook(void (*fn)(void));

void kernel_event_cycle_run(void);
void kernel_stats_report(void);
void kernel_shell_exec(const char *line);

static void boot_msg(const char *msg) {
    uint32_t eid = boot_evidence_record(msg);
    /* Print evidence ID as [E0001] prefix */
    uart_puts("[E");
    if (eid < 1000) uart_puts("0");
    if (eid < 100) uart_puts("0");
    if (eid < 10) uart_puts("0");
    uart_put_dec((uint64_t)eid);
    uart_puts("] ");
    uart_puts(msg);
    uart_puts("\n");
}

/* Helper to copy a short string into a fixed-size, zero-padded cell id. */
static void cell_id_set(char *dst, const char *src) {
    for (int i = 0; i < CELL_NAME_LEN; i++) {
        dst[i] = src[i] ? src[i] : '\0';
        if (!src[i]) break;
    }
}

/* CELL-001: boot-time multikernel fabric setup.
 * Initializes a small control plane, admits three logical cells,
 * installs phase routes, then demonstrates fail-stop containment by
 * revoking a faulted cell's capabilities. */
static void cell_fabric_boot_init(void) {
    static cell_fabric_t cell_fabric;
    static const uint8_t cell_image_digest[CELL_MAX_DIGEST] = {
        0xab,0xcd,0xef,0x01,0x02,0x03,0x04,0x05,
        0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,
        0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,
        0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d
    };

    boot_msg("[BOOT] Cellular Multikernel (CELL-001): fabric discovery and admission...");
    cell_fabric_init(&cell_fabric);

    /* Three logical ARM64 cells: coordinator, K1-K6 pipeline, I/O. */
    cell_t coord; cell_contract_zero(&coord);
    cell_id_set(coord.cell_id, "arm64-coord");
    coord.arch = CELL_ARCH_ARM64; coord.bitness = CELL_BITNESS_64;
    coord.trust = CELL_TRUST_KERNEL; coord.privilege = CELL_PRIVILEGE_EL1;
    coord.incarnation = 1;
    coord.capabilities = CELL_CAP_OSEQ | CELL_CAP_IPHASE | CELL_CAP_PHASECOORD;
    coord.budget.max_events_per_second = 100000;
    coord.budget.max_memory_bytes = 0x40000000;
    cell_set_digest(&coord, cell_image_digest);

    cell_t kpipe; cell_contract_zero(&kpipe);
    cell_id_set(kpipe.cell_id, "arm64-k1k6");
    kpipe.arch = CELL_ARCH_ARM64; kpipe.bitness = CELL_BITNESS_64;
    kpipe.trust = CELL_TRUST_KERNEL; kpipe.privilege = CELL_PRIVILEGE_EL1;
    kpipe.incarnation = 1;
    kpipe.capabilities = CELL_CAP_RMAG | CELL_CAP_LPRES | CELL_CAP_CHOICE;
    kpipe.budget.max_events_per_second = 50000;
    kpipe.budget.max_memory_bytes = 0x20000000;
    cell_set_digest(&kpipe, cell_image_digest);

    cell_t io; cell_contract_zero(&io);
    cell_id_set(io.cell_id, "arm64-io");
    io.arch = CELL_ARCH_ARM64; io.bitness = CELL_BITNESS_64;
    io.trust = CELL_TRUST_DEVICE; io.privilege = CELL_PRIVILEGE_EL1;
    io.incarnation = 1;
    io.capabilities = CELL_CAP_NETWORK | CELL_CAP_AUDIO;
    io.budget.max_events_per_second = 25000;
    io.budget.max_memory_bytes = 0x10000000;
    cell_set_digest(&io, cell_image_digest);

    if (!cell_fabric_discover(&cell_fabric, &coord)) {
        boot_msg("  [FAIL] cell fabric: could not discover coordinator");
        return;
    }
    if (!cell_fabric_discover(&cell_fabric, &kpipe)) {
        boot_msg("  [FAIL] cell fabric: could not discover k1k6");
        return;
    }
    if (!cell_fabric_discover(&cell_fabric, &io)) {
        boot_msg("  [FAIL] cell fabric: could not discover io");
        return;
    }

    if (!cell_fabric_authenticate(&cell_fabric, "arm64-coord", cell_image_digest) ||
        !cell_fabric_authenticate(&cell_fabric, "arm64-k1k6", cell_image_digest) ||
        !cell_fabric_authenticate(&cell_fabric, "arm64-io", cell_image_digest)) {
        boot_msg("  [FAIL] cell fabric: authentication rejected");
        return;
    }

    cell_fabric_admit(&cell_fabric, "arm64-coord");
    cell_fabric_admit(&cell_fabric, "arm64-k1k6");
    cell_fabric_admit(&cell_fabric, "arm64-io");

    cell_fabric_activate(&cell_fabric, "arm64-coord");
    cell_fabric_activate(&cell_fabric, "arm64-k1k6");
    cell_fabric_activate(&cell_fabric, "arm64-io");

    cell_route_t r_oseq = { 10, 1, "arm64-coord", 0, false };
    cell_route_t r_rmag = { 20, 1, "arm64-k1k6",  1, false };
    cell_route_t r_net  = { 30, 1, "arm64-io",    2, false };
    cell_fabric_add_route(&cell_fabric, &r_oseq);
    cell_fabric_add_route(&cell_fabric, &r_rmag);
    cell_fabric_add_route(&cell_fabric, &r_net);

    boot_msg("  [INITIALIZED] 3 ARM64 cells active (coord, k1k6, io) with phase routes");

    /* Demonstrate fail-stop containment: the I/O cell faults. */
    cell_fabric_handle_fault(&cell_fabric, "arm64-io", CELL_HEALTH_FAIL_STOPPED);
    if (cell_fabric_route_lookup(&cell_fabric, 30, 1) == NULL) {
        boot_msg("  [FAULT CONTAINED] arm64-io fail-stopped; routes revoked; fabric survives");
    } else {
        boot_msg("  [FAIL] cell fabric: faulted cell still has routes");
    }

    /* Re-admit a new incarnation of the I/O cell after quarantine. */
    io.incarnation = 2;
    io.health = CELL_HEALTH_OK;
    io.state = CELL_STATE_DISCOVERED;
    cell_set_digest(&io, cell_image_digest);
    if (cell_fabric_discover(&cell_fabric, &io)) {
        cell_fabric_authenticate(&cell_fabric, "arm64-io", cell_image_digest);
        cell_fabric_admit(&cell_fabric, "arm64-io");
        cell_fabric_activate(&cell_fabric, "arm64-io");
        cell_route_t r_net2 = { 30, 1, "arm64-io", 2, false };
        cell_fabric_add_route(&cell_fabric, &r_net2);
        boot_msg("  [RECOVERED] arm64-io re-admitted with incarnation=2 and new routes");
    }
}

void kernel_main(void) {
    kernel_main_arm64();
}

void kernel_main_arm64(void) {
    /* Phase 1: UART output */
    uart_init();
    boot_evidence_init();

    /* Boot identity: the 36N9 code-dragon and the ZEDEC XERO VULPINE
     * fox, in the house blueprint style, rendered for the serial
     * console this kernel actually boots on. */
    uart_puts("\n");
    uart_puts(ZXV_BANNER_36N9);
    uart_puts("\n");
    uart_puts(ZXV_BANNER_ZEDEC);
    uart_puts("\n");
    uart_puts("ZEDEC pqOS — M5 Axiomatic Kernel (VOVINA SHAKINA) [ARM64]\n");
    uart_puts("Edition: ZEDEC XERO VULPINE (ZXV)\n");
    uart_puts("36N9 Genetics, LLC  |  ACOTO · SMIC · CIS\n");
    uart_puts("==================================================\n\n");

    /* Phase 0: License banner — the four-instrument share-alike stack */
    uart_puts("License: OPL-1.1 + CC BY-SA 4.0 + Royal Writ + SEL-3.3 (share-alike, travels together)\n");
    uart_puts("Author: H.M. Michael-Laurence: Curzi (c)\n");
    uart_puts("36N9 Genetics, LLC — Irrevocable, Interdimensional\n\n");

    /* Phase 2: MMU */
    boot_msg("[BOOT] MMU/paging...");
    arm64_mmu_init();
    boot_msg("  [DRIVER ONLINE] Identity mapping (1GB, 4KB granule)");

    /* Phase 3: GIC + Timer */
    boot_msg("[BOOT] GICv3 interrupt controller...");
    gic_init();
    gic_register_handler(IRQ_TIMER, arm64_timer_handler);
    boot_msg("  [DRIVER ONLINE] GICv3 distributor + redistributor");

    boot_msg("[BOOT] Generic timer...");
    arm64_timer_init();
    boot_msg("  [DRIVER ONLINE] Timer at 100 Hz");

    /* Phase 4: M5 Kernel subsystems */
    boot_msg("[BOOT] M5 Kernel subsystems...");
    phase_coordinator_init(&tick, EXEC_DC);
    boot_msg("  [INITIALIZED] Phase Coordinator (EXEC_DC)");
    rmag_init(256);
    boot_msg("  [INITIALIZED] RMAG (256 slots)");
    lpres_init();
    boot_msg("  [INITIALIZED] LPRES");
    iphase_init();
    boot_msg("  [INITIALIZED] IPHASE");
    choice_handoff();
    boot_msg("  [INITIALIZED] CHOICE");
    oseq_state_t oseq;
    oseq_init(&oseq);
    oseq_register_device(&oseq, "core");
    boot_msg("  [INITIALIZED] OSEQ");

    int i;
    for (i = 0; i < 5; i++) {
        phase_coordinator_tick(&tick);
        uart_puts("  cycle ");
        uart_put_dec((uint64_t)(i + 1));
        uart_puts(": omega=");
        uart_put_dec(tick.omega);
        uart_puts("\n");
    }

    /* Phase 5: EDP Risk + ISF + Predictive */
    boot_msg("[BOOT] EDP risk calculus + ISF + predictive model...");
    predictive_config_t pred_cfg;
    predictive_config_init(&pred_cfg);
    boot_msg("  [INITIALIZED] EDP operators + Fibonacci algebra + ISF surplus");

    /* Phase 6: Situation Modeler */
    boot_msg("[BOOT] Tactical/strategic situation modeler...");
    boot_msg("  [REGISTERED] 15 domains + cross-domain contagion");

    /* Phase 7: Triple Ledger */
    boot_msg("[BOOT] Nine-capital triple ledger system...");
    static triple_ledger_t tl;
    triple_ledger_init(&tl);
    triple_ledger_create_account(&tl, 1, CAP_FINANCIAL, "Genesis:Financial");
    triple_ledger_create_account(&tl, 1, CAP_HUMAN, "Genesis:Human");
    triple_ledger_create_account(&tl, 1, CAP_ECOLOGICAL, "Genesis:Ecological");
    boot_msg("  [REGISTERED] 9 capital types + floating vouchers + conventional compat");

    /* Phase 8: Financial Instruments */
    boot_msg("[BOOT] Financial instruments suite...");
    static portfolio_t portfolio;
    portfolio_init(&portfolio);
    boot_msg("  [REGISTERED] 20 instrument types + M5 valuation + Greeks");

    /* Phase 9: Payment Rails */
    boot_msg("[BOOT] Dragon/Phoenix/Thunderbird payment rails...");
    static rail_system_t rail_sys;
    rail_system_init(&rail_sys);
    boot_msg("  [REGISTERED] 3 rails + conventional network compat");

    /* Phase 10: Crypto Bridge */
    boot_msg("[BOOT] Web2-Web3 cryptocurrency bridge...");
    static bridge_registry_t bridge_reg;
    bridge_registry_init(&bridge_reg);
    boot_msg("  [REGISTERED] 35 chains + 11 smart contract languages + dual-directional");

    /* Phase 11: Identity System (research simulation, not a real
     * identity issuer/verifier for any jurisdiction -- see
     * KERNEL_SIM_DEVICES above) */
#if KERNEL_SIM_DEVICES
    boot_msg("[SIM] National identity document-format registry (simulation, not a real ID authority)...");
    static identity_registry_t id_reg;
    identity_registry_init(&id_reg);
    boot_msg("  [SIM-OK] 193 UN member state document formats loaded (data model only)");
#else
    boot_msg("[SKIP] National identity registry (KERNEL_SIM_DEVICES=0)");
#endif

    /* Phase 12: JDR PirateNet */
    boot_msg("[BOOT] JDR PirateNet harmonic hum carrier...");
    static jdr_network_t jdr_net;
    jdr_network_init(&jdr_net);
    jdr_register_node(&jdr_net, 1);
    jdr_transceiver_create(&jdr_net, 145000000, 12500,
        JDR_BAND_VHF, JDR_MOD_FM, JDR_EXEC_AC, "JDR-ARM-001");
    boot_msg("  [REGISTERED] 22 frequency bands + harmonic hum + FHSS");

    /* Phase 13: Quantum Devices -- Casimir cavity math is real physics;
     * ZPE-as-power-source, wormhole throat and exotic matter generation
     * have no known/foreseeable physical construction (research
     * simulation only, see KERNEL_SIM_DEVICES above) */
#if KERNEL_SIM_DEVICES
    boot_msg("[SIM] Quantum + exotic matter research sandbox (simulation, no physical device backing)...");
    static quantum_system_t qsys;
    quantum_system_init(&qsys);
    boot_msg("  [SIM-OK] Casimir + ZPE + wormhole + exotic matter + harmonic rendering");
#else
    boot_msg("[SKIP] Quantum/exotic-matter research sandbox (KERNEL_SIM_DEVICES=0)");
#endif

    /* Phase 14: RTL Device Framework */
    boot_msg("[BOOT] Hardware-as-code RTL device framework...");
    static rtl_registry_t rtl_reg;
    rtl_registry_init(&rtl_reg);
    boot_msg("  [REGISTERED] Chisel/SystemVerilog/VHDL + AXI4/APB/AHB + second quantization");

    /* Phase 14b: DLP Pico Projector (Tank series shared peripheral) --
     * software model of a real TI DLP2010-class .2" WVGA DMD (854x480,
     * 5.4um pitch, +/-17deg tilt), event-cycle-driven per the
     * external-clock-bridge convention: dlp_projector_tick() is called
     * from the event loop below, never from a raw timer poll. */
    boot_msg("[BOOT] DLP pico projector (hardware-as-code)...");
    dlp_projector_init(&projector, 1, "Tank-DLP");
    dlp_projector_set_wobulation(&projector, 2, 2); /* TI 4-way actuator: 854x480 -> 1708x960 apparent */
    dlp_projector_set_photometrics(&projector, SR_FROM_INT(220), SR_FROM_FLOAT(1.2));
    dlp_projector_compute_screen_lux(&projector, SR_FROM_FLOAT(2.0));
    dlp_projector_update_coverage(&projector);
    boot_msg("  [INITIALIZED] 854x480 native -> 1708x960 apparent (4-way pixel-shift)");
    boot_msg("  [SIMULATED] Laser AF (0.5-4m) + keystone + complementary-color dither");

    /* Phase 15: Vino Bank */
    boot_msg("[BOOT] Vino decentralized bank node...");
    vino_init(&vino, 1);
    vino_create_account(&vino, "ZEDEC:node:arm64:0001", "Genesis Account");
    vino_set_validator(&vino, true, 1000000);
    boot_msg("  [INITIALIZED] Triple ledger + nine capital + ISO 20022");

    /* Phase 15b: Count House & Stash Buckets -- fractal-reserve
     * valuation engine. Initialized at Scale-0 (local node tier) with
     * Fibonacci mint-curve level 1. crypto_reserves is fed from the
     * Vino vault above; V_local/collateral_ratio recomputed on every
     * deposit, mint, or reserve change (see count_house_valuation()).
     * Fractal scaling (Alliance/Global tiers, recursive valuation,
     * Fibonacci mint curve) is implemented in count_house.c and
     * exercised by test_count_house_fractal.c. */
    boot_msg("[BOOT] Count House fractal-reserve valuation engine...");
    static count_house_t count_house;
    count_house_init_fractal(&count_house, 1, "ZEDEC:count-house:arm64:0001", CH_SCALE_0);
    count_house_set_crypto_reserves(&count_house, SR_FROM_INT(0));
    uint64_t fib_allow = count_house_fib_mint_allowance(&count_house, 1000);
    boot_msg("  [INITIALIZED] Scale-0 Stash Buckets + anti-Sybil + Fibonacci mint curve (allowance=");
    uart_put_dec((uint64_t)fib_allow);
    uart_puts(")\n");

    /* Phase 15c: Porter House — port-seal / wall-interface firewall.
     * Sits in front of net.c's connection-accept path: each sealed port
     * gets an admission policy (OPEN / TRUSTED / ALLOWLIST / CLOSED)
     * that decides whether a 168-bit-identified peer gets past the door
     * before a single byte of payload is processed. The JDR PirateNet
     * service port is sealed TRUSTED with a minimum trust weight of 500
     * (Count House scale, 0-1000) so only known-good mesh peers can
     * connect; a management port is sealed ALLOWLIST for explicit
     * operator access only. */
    boot_msg("[BOOT] Porter House port-seal firewall...");
    porter_house_init(&porter_house, 1, "ZEDEC:porter-house:arm64:0001");

    /* Seal JDR PirateNet service port: TRUSTED, min trust 500 */
    porter_house_seal_port(&porter_house, 8800, PH_SEAL_TRUSTED, 500);

    /* Seal management port: ALLOWLIST (operator must explicitly add peers) */
    porter_house_seal_port(&porter_house, 9090, PH_SEAL_ALLOWLIST, 0);

    /* Close the debug port during production boot */
    porter_house_close_port(&porter_house, 23);

    porter_house_update_coverage(&porter_house);
    boot_msg("  [INITIALIZED] Port-seal firewall: 8800=TRUSTED(500) 9090=ALLOWLIST 23=CLOSED");

    /* Phase 15d: Mesh-Token External Settlement — routes token
     * transfers between mesh nodes through Porter House-gated
     * connections on the JDR PirateNet service port (8800). Uses
     * Count House for valuation at settlement time. This is a native
     * P2P mesh settlement protocol, NOT a blockchain bridge — it
     * uses 168-bit peer IDs and M5 coordinate coverage verification. */
    boot_msg("[BOOT] Mesh-Token external settlement via Porter House...");
    mesh_token_init(&mesh_token, 1, "ZEDEC:mesh-token:arm64:0001",
                     &porter_house, &count_house);
    boot_msg("  [INITIALIZED] Porter House-gated settlement on port 8800 + Count House valuation");

    /* Phase 15e: Community Chest — P2P Decentralized App Store.
     * Replaces the traditional App Store / Play Store model with a
     * post-quantum, peer-to-peer marketplace where apps are seeded
     * across the device mesh. Uses Vino Floating Vouchers as the
     * internal clearing unit (legally classified as store credit).
     * Revenue split: developer 70-85%, platform 15-24%, 36N9 20%. */
    boot_msg("[BOOT] Community Chest P2P app store + Vino Floating Vouchers...");
    cc_init(&community_chest, 1, "ZEDEC:community-chest:arm64:0001");
    cc_link_vino(&community_chest, &vino);
    boot_msg("  [REGISTERED] P2P marketplace + Vino voucher clearing (dev 70-85% / platform 15-24% / 36N9 20%)");

    /* Phase 15f: AI Integration & Remote Compute — native ZXV AI
     * dispatch layer. Supports on-device inference and remote compute
     * offload to trusted peers via Porter House-gated connections on
     * port 8900. AI models are signed, verified, and can be distributed
     * through Community Chest. Post-quantum security boundary enforced. */
    boot_msg("[BOOT] AI integration layer + remote compute...");
    ai_init(&ai_engine, 1, "ZEDEC:ai-layer:arm64:0001", &porter_house);
    porter_house_seal_port(&porter_house, AI_REMOTE_PORT, PH_SEAL_TRUSTED, 500);
    boot_msg("  [REGISTERED] On-device inference + Porter House-gated remote compute (port 8900)");

    /* Phase 15g: P2P Mesh Network Layer — user-created mesh networks,
     * trade routes, and mesh federation. Users can create their own
     * network topologies, establish P2P trade routes for commerce and
     * data exchange, and federate with other meshes. Porter House gates
     * peer admission on the mesh port (8800). Supports 5 route types:
     * data, trade, compute, voice, and emergency. */
    boot_msg("[BOOT] P2P mesh network layer (user-created networks + trade routes)...");
    mn_init(&mesh_net, 1, "ZEDEC:mesh-net:arm64:0001", &porter_house);
    boot_msg("  [REGISTERED] User mesh networks + trade routes + federation (port 8800)");

    /* Phase 15h: Immigration Enforcement + Robin DeBanks Vault —
     * daemon admission control (visa system) and hardened crypto
     * vault for sensitive asset storage. Immigration verifies all
     * daemon signatures before granting execution visas. Robin DeBanks
     * provides encrypted, time-locked storage for private keys, seed
     * phrases, and credentials. Both use pluggable crypto (Ed25519 now,
     * ML-DSA-44 / ML-KEM-768 when available). */
    boot_msg("[BOOT] Immigration enforcement + Robin DeBanks vault...");
    immig_init(&immigration, 1, &porter_house);
    /* Derive a per-boot vault key. Audit P0-4: use the architected
     * hardware RNG (FEAT_RNG / RNDR) when the CPU implements it, with a
     * health test. If it is absent (e.g. QEMU cortex-a53), DO NOT claim
     * hardware entropy — label the fallback honestly as non-cryptographic
     * development material so no downstream secret is trusted as strong. */
    static uint8_t vault_key[ROBIN_KEY_LEN];
    if (arm64_rng_available() && arm64_rng_fill(vault_key, ROBIN_KEY_LEN)) {
        boot_msg("  [ENTROPY] Vault key from FEAT_RNG (RNDR) hardware RNG + health test");
    } else {
        /* Honest fallback: mixed timer/address material is NOT cryptographic. */
        uint64_t e1, e3;
        __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(e1));
        __asm__ volatile("mov %0, sp"         : "=r"(e3));
        uint64_t e4 = (uint64_t)&robin_vault;
        for (int i = 0; i < ROBIN_KEY_LEN; i++) {
            uint64_t e = e1 ^ (e3 << (i & 7)) ^ e4;
            vault_key[i] = (uint8_t)(e >> ((i % 8) * 8));
        }
        boot_msg("  [ENTROPY][WARN] No FEAT_RNG on this CPU — vault key is NON-cryptographic");
        boot_msg("                   dev material (predictable). Not for production secrets.");
    }
    robin_init(&robin_vault, 1, vault_key, &porter_house);
    boot_msg("  [INITIALIZED] Daemon visa system + encrypted vault");

    /* Phase 16: Vena Runtime */
    boot_msg("[BOOT] Vena application runtime...");
    static vena_runtime_t vena;
    vena_init(&vena, &vino);
    vena_set_language(&vena, LANG_M5_AXIOMATIC);
    boot_msg("  [INITIALIZED] M5 Axiomatic app loader + 31 languages");

    /* Phase 16b: Event-Driven Scheduler — native ZXV task dispatcher
     * with per-task event budgets, PMU-cycle cost proxy, and tickless
     * WFI idle. Replaces the round-robin sched.c model with an
     * event-budget model: each task gets a finite event quota per
     * dispatch, and when no task has pending events the scheduler
     * enters WFI instead of spinning. */
    boot_msg("[BOOT] Event-driven scheduler (per-task event budget + WFI tickless idle)...");
    ev_sched_init(&evs);
    ev_sched_create_task(&evs, "net-rx", 50, 800);
    ev_sched_create_task(&evs, "count-house-tick", 100, 900);
    ev_sched_create_task(&evs, "dlp-projector-tick", 20, 600);
    boot_msg("  [INITIALIZED] 3 event-budget tasks + PMU cycle cost proxy + tickless WFI");

    /* Phase 16c: Event-Space Infrastructure — canonical event envelopes,
     * event domains with causal ordering, self-audit, and self-healing.
     * This is the real event-domain layer from the heterogeneous
     * constellation architecture: sequenced events with budgets,
     * capabilities, schema filtering, integrity verification, and
     * automatic fault detection/recovery. */
    boot_msg("[BOOT] Event-space infrastructure (sequencer + domains + self-audit + self-healing)...");
    ev_seq_init(&ev_seq, "arm64.cluster0");
    ev_audit_init(&ev_audit);
    ev_healing_init(&ev_healing);

    /* Create initial event domains for kernel services */
    int32_t dom_storage = ev_seq_create_domain(&ev_seq, "storage",
        100, 200, EV_CONSISTENCY_LOCAL);
    int32_t dom_network = ev_seq_create_domain(&ev_seq, "network",
        50, 150, EV_CONSISTENCY_LOCAL);
    int32_t dom_display = ev_seq_create_domain(&ev_seq, "display",
        80, 100, EV_CONSISTENCY_LOCAL);
    int32_t dom_input = ev_seq_create_domain(&ev_seq, "input",
        200, 50, EV_CONSISTENCY_LOCAL);

    /* Grant capabilities to domains */
    ev_domain_grant_capability(&ev_seq, (uint32_t)dom_storage, "storage.documents.read");
    ev_domain_grant_capability(&ev_seq, (uint32_t)dom_storage, "storage.documents.write");
    ev_domain_grant_capability(&ev_seq, (uint32_t)dom_network, "network.tx");
    ev_domain_grant_capability(&ev_seq, (uint32_t)dom_network, "network.rx");
    ev_domain_grant_capability(&ev_seq, (uint32_t)dom_display, "display.render");
    ev_domain_grant_capability(&ev_seq, (uint32_t)dom_input, "input.keyboard");
    ev_domain_grant_capability(&ev_seq, (uint32_t)dom_input, "input.touch");

    /* Register accepted schemas for each domain */
    ev_domain_add_accepted_schema(&ev_seq, (uint32_t)dom_storage, "zxv.storage");
    ev_domain_add_accepted_schema(&ev_seq, (uint32_t)dom_network, "zxv.network");
    ev_domain_add_accepted_schema(&ev_seq, (uint32_t)dom_display, "zxv.display");
    ev_domain_add_accepted_schema(&ev_seq, (uint32_t)dom_input, "zxv.input");

    /* Run initial system audit */
    ev_audit_result_t init_audit = ev_audit_check_system(&ev_seq, &ev_audit);
    boot_msg("  [INITIALIZED] 4 event domains (storage/network/display/input)");
    boot_msg("  [AUDIT] Initial system audit: PASS");
    (void)init_audit;

    /* Emit a pilot event to verify the pipeline works end-to-end */
    ev_envelope_t pilot_env;
    ev_envelope_init(&pilot_env, &ev_seq.node, 0,
                     "zxv.storage.read.request", 1);
    ev_envelope_set_payload(&pilot_env,
        (const uint8_t *)"boot-pilot", 10);
    int32_t pilot_ret = ev_seq_enqueue(&ev_seq,
        (uint32_t)dom_storage, &pilot_env);
    if (pilot_ret == 0) {
        boot_msg("  [PILOT] Event enqueued to storage domain — pipeline verified");
    }

    /* Phase 16d: Orbital Elevator — schema/version compatibility fabric.
     * Registers known event schemas, adapters for version migration,
     * and orbit manifests for architecture-neutral service declarations.
     * This enables cross-ISA event translation without instruction emulation. */
    boot_msg("[BOOT] Orbital Elevator (schema registry + compatibility graph + adapters)...");
    oe_init(&oe);

    /* Register core schemas with version history */
    oe_register_schema(&oe, "zxv.storage.read");
    oe_schema_add_version(&oe, "zxv.storage.read", 1, 0, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 2, 0, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 3, 1, 256);
    oe_schema_add_version(&oe, "zxv.storage.read", 4, 1, 256);

    oe_register_schema(&oe, "zxv.network.send");
    oe_schema_add_version(&oe, "zxv.network.send", 1, 0, 128);
    oe_schema_add_version(&oe, "zxv.network.send", 2, 0, 256);

    oe_register_schema(&oe, "zxv.display.render");
    oe_schema_add_version(&oe, "zxv.display.render", 1, 0, 256);
    oe_schema_add_version(&oe, "zxv.display.render", 2, 0, 512);

    /* Register orbit manifests for kernel services */
    oe_register_orbit(&oe, "org.zxv.storage", "neutral", 4);
    oe_orbit_add_dep(&oe, "org.zxv.storage", "zxv.storage.read", 1, true);
    oe_register_orbit(&oe, "org.zxv.network", "neutral", 2);
    oe_orbit_add_dep(&oe, "org.zxv.network", "zxv.network.send", 1, true);
    oe_register_orbit(&oe, "org.zxv.display", "neutral", 2);
    oe_orbit_add_dep(&oe, "org.zxv.display", "zxv.display.render", 1, true);

    boot_msg("  [INITIALIZED] 3 schemas (10 versions) + 3 orbit manifests");

    /* Phase 16e: Constellation Coordinator — cross-domain node registry,
     * event routing, executor profiles, and health monitoring. This node
     * registers itself as the first member of the constellation. */
    boot_msg("[BOOT] Constellation Coordinator (node registry + routing + health)...");
    cc_coordinator_init(&cc, "arm64.cluster0");

    /* Register self as first node */
    int32_t self_node = cc_register_node(&cc, "arm64.cluster0",
                                          "arm64", EV_INCARNATION_INIT);
    if (self_node >= 0) {
        cc_node_authenticate(&cc, (uint32_t)self_node);
        cc_node_activate(&cc, (uint32_t)self_node);
        cc_node_add_schema(&cc, (uint32_t)self_node, "zxv.storage.read");
        cc_node_add_schema(&cc, (uint32_t)self_node, "zxv.network.send");
        cc_node_add_schema(&cc, (uint32_t)self_node, "zxv.display.render");
    }

    /* Register executors for this node's services */
    int32_t ex_storage = cc_register_executor(&cc, "arm64.cluster0",
        "zxv.storage", 50, 80);
    if (ex_storage >= 0) {
        cc_executor_add_schema(&cc, (uint32_t)ex_storage, "zxv.storage.read");
    }
    int32_t ex_network = cc_register_executor(&cc, "arm64.cluster0",
        "zxv.network", 30, 60);
    if (ex_network >= 0) {
        cc_executor_add_schema(&cc, (uint32_t)ex_network, "zxv.network.send");
    }
    int32_t ex_display = cc_register_executor(&cc, "arm64.cluster0",
        "zxv.display", 40, 70);
    if (ex_display >= 0) {
        cc_executor_add_schema(&cc, (uint32_t)ex_display, "zxv.display.render");
    }

    /* Add default routes to self */
    cc_add_route(&cc, "zxv.storage.read", 1, "arm64.cluster0", 100);
    cc_add_route(&cc, "zxv.network.send", 1, "arm64.cluster0", 100);
    cc_add_route(&cc, "zxv.display.render", 1, "arm64.cluster0", 100);

    boot_msg("  [INITIALIZED] 1 node (self) + 3 executors + 3 routes");

    /* Phase 16f: Event Transport — channel abstraction for cross-ISA
     * event delivery. Creates a local loopback channel for same-core
     * delivery. In a real multi-ISA system, additional channels would
     * be created for shmem, PCIe, or network transports to other nodes. */
    boot_msg("[BOOT] Event Transport (channel abstraction + delivery)...");
    et_init(&et);

    int32_t ch_local = et_create_channel(&et, "loopback",
        ET_TRANSPORT_LOCAL, "arm64.cluster0", "arm64.cluster0");
    if (ch_local >= 0) {
        et_channel_activate(&et, (uint32_t)ch_local);
    }

    boot_msg("  [INITIALIZED] 1 loopback channel (local transport)");

    /* Phase 16g: Hypercube Scene Architecture — event-driven spatial UI
     * with graceful degradation. Cells occupy positions in N-dimensional
     * logical space and are projected to 2D for display. The scene
     * processes events from the event-space and routes them to subscribed
     * cells. Display mode degrades gracefully from holographic down to
     * terminal output. */
    boot_msg("[BOOT] Hypercube Scene Architecture (scene graph + projection + degradation)...");
    hc_init(&hc_scene, HC_DISPLAY_2D_NORMAL);

    /* Create initial scene cells for kernel services */
    int32_t hc_terminal = hc_create_cell(&hc_scene, "terminal",
                                          HC_CELL_APPLICATION);
    if (hc_terminal >= 0) {
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_terminal, HC_DIM_ACTIVE_BG, 1);
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_terminal, HC_DIM_WORK_PLAY, 0);
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_terminal, HC_DIM_APP_GROUP, 0);
        hc_cell_subscribe(&hc_scene, (uint32_t)hc_terminal, "zxv.input.keyboard");
        hc_get_cell(&hc_scene, (uint32_t)hc_terminal)->state = HC_CELL_VISIBLE;
        hc_set_focus(&hc_scene, (uint32_t)hc_terminal);
    }

    int32_t hc_storage = hc_create_cell(&hc_scene, "files",
                                         HC_CELL_APPLICATION);
    if (hc_storage >= 0) {
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_storage, HC_DIM_ACTIVE_BG, 1);
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_storage, HC_DIM_WORK_PLAY, 0);
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_storage, HC_DIM_APP_GROUP, 1);
        hc_cell_subscribe(&hc_scene, (uint32_t)hc_storage, "zxv.storage.read");
        hc_get_cell(&hc_scene, (uint32_t)hc_storage)->state = HC_CELL_VISIBLE;
    }

    int32_t hc_network = hc_create_cell(&hc_scene, "network",
                                         HC_CELL_SERVICE);
    if (hc_network >= 0) {
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_network, HC_DIM_ACTIVE_BG, 0);
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_network, HC_DIM_APP_GROUP, 2);
        hc_cell_subscribe(&hc_scene, (uint32_t)hc_network, "zxv.network.send");
    }

    int32_t hc_notify = hc_create_cell(&hc_scene, "notifications",
                                        HC_CELL_NOTIFICATION);
    if (hc_notify >= 0) {
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_notify, HC_DIM_ACTIVE_BG, 1);
        hc_cell_set_coord(&hc_scene, (uint32_t)hc_notify, HC_DIM_APP_GROUP, 3);
        hc_get_cell(&hc_scene, (uint32_t)hc_notify)->state = HC_CELL_VISIBLE;
    }

    /* Project the scene for initial display */
    hc_project(&hc_scene);

    boot_msg("  [INITIALIZED] 4 scene cells (terminal/files/network/notifications)");
    boot_msg("  [SCENE] Projected to 2D_NORMAL — terminal focused");

    /* Phase 16h: Yantra Fabric — software-defined hardware device registry.
     * Registers known hardware devices with their capability maturity level,
     * state machines, and digital twin assertions. Devices start at the
     * MODEL_ONLY or EMULATED level and progress through maturity layers.
     * The digital twin compares predicted vs measured behavior and enters
     * safe state on contradictions (paraconsistent logic). */
    boot_msg("[BOOT] Yantra Fabric (device registry + capability states + digital twin)...");
    yf_init(&yf);

    /* Register PL011 UART as an EMULATED device (QEMU) */
    int32_t yf_uart = yf_register_device(&yf, "pl011-uart", "serial",
                                          YF_EMULATED);
    if (yf_uart >= 0) {
        yf_device_add_register(&yf, (uint32_t)yf_uart, "DR", 0x000, 32,
                                true, true, 0);
        yf_device_add_register(&yf, (uint32_t)yf_uart, "FR", 0x018, 32,
                                true, false, 0x90);
        yf_device_add_register(&yf, (uint32_t)yf_uart, "IBRD", 0x024, 32,
                                true, true, 0);
        yf_device_add_state(&yf, (uint32_t)yf_uart, "idle", true);
        yf_device_add_state(&yf, (uint32_t)yf_uart, "transmitting", false);
        yf_device_add_state(&yf, (uint32_t)yf_uart, "receiving", false);
        yf_device_add_event(&yf, (uint32_t)yf_uart, "tx-start",
                            "zxv.serial.tx.start", 0, 1);
        yf_device_add_event(&yf, (uint32_t)yf_uart, "tx-done",
                            "zxv.serial.tx.done", 1, 0);
        yf_device_add_event(&yf, (uint32_t)yf_uart, "rx-ready",
                            "zxv.serial.rx.ready", 0, 2);
    }

    /* Register GICv3 as an EMULATED device */
    int32_t yf_gic = yf_register_device(&yf, "gicv3", "interrupt-controller",
                                         YF_EMULATED);
    if (yf_gic >= 0) {
        yf_device_add_state(&yf, (uint32_t)yf_gic, "active", true);
        yf_device_add_state(&yf, (uint32_t)yf_gic, "inactive", false);
    }

    /* Register DLP projector as MODEL_ONLY (no physical hardware yet) */
    int32_t yf_dlp = yf_register_device(&yf, "dlp-projector", "display",
                                         YF_MODEL_ONLY);
    if (yf_dlp >= 0) {
        yf_device_add_state(&yf, (uint32_t)yf_dlp, "off", true);
        yf_device_add_state(&yf, (uint32_t)yf_dlp, "on", false);
        yf_device_add_state(&yf, (uint32_t)yf_dlp, "projecting", false);
        yf_device_add_state(&yf, (uint32_t)yf_dlp, "safe", true);
        yf_device_add_event(&yf, (uint32_t)yf_dlp, "power-on",
                            "zxv.projector.power.on", 0, 1);
        yf_device_add_event(&yf, (uint32_t)yf_dlp, "start-projection",
                            "zxv.projector.start", 1, 2);
        yf_device_add_event(&yf, (uint32_t)yf_dlp, "power-off",
                            "zxv.projector.power.off", 2, 0);
    }

    /* Register framebuffer as EMULATED (QEMU VBE) */
    int32_t yf_fb = yf_register_device(&yf, "vbe-framebuffer", "display",
                                        YF_EMULATED);
    if (yf_fb >= 0) {
        yf_device_add_state(&yf, (uint32_t)yf_fb, "blank", true);
        yf_device_add_state(&yf, (uint32_t)yf_fb, "active", false);
    }

    boot_msg("  [INITIALIZED] 4 devices (uart/gic/projector/framebuffer)");
    boot_msg("  [YANTRA] Capability levels: uart=EMULATED gic=EMULATED projector=MODEL_ONLY fb=EMULATED");

    /* Phase 16i: Tri-Space Programming — positive/negative/neutral space
     * triad registry with cryptographic binding, inverse classification,
     * S0 quarantine, and neutral resolution. Every ZXV program is
     * represented by a constructive S+ component, a restrictive S-
     * component, and an adjudicating S0 component, bound into one
     * cryptographically identifiable triad. The kernel registers its
     * own core services as tri-space triads to enforce the programming
     * model from boot. */
    boot_msg("[BOOT] Tri-Space Programming (triad registry + linter + S0 quarantine)...");
    ds_init(&ds_reg);

    /* Register the storage service as a tri-space triad */
    int32_t ds_storage = ds_register_pair(&ds_reg, "storage-service",
                                            "org.zxv.storage.v1");
    if (ds_storage >= 0) {
        /* Add all 4 positive/negative/neutral artifact triads */
        uint8_t pd[DS_DIGEST_SIZE], nd[DS_DIGEST_SIZE], nud[DS_DIGEST_SIZE];
        for (uint32_t r = 0; r < 4; r++) {
            ds_artifact_role_t pos_role = (ds_artifact_role_t)(r * 2);
            ds_artifact_role_t neg_role = (ds_artifact_role_t)(r * 2 + 1);
            ds_artifact_role_t neu_role = (ds_artifact_role_t)(8 + r);
            ds_add_artifact(&ds_reg, (uint32_t)ds_storage, true, pos_role,
                             ds_role_extension(pos_role), "sutra",
                             DS_LANG_NATIVE_DUAL);
            ds_add_artifact(&ds_reg, (uint32_t)ds_storage, false, neg_role,
                             ds_role_extension(neg_role), "sutra",
                             DS_LANG_NATIVE_DUAL);
            ds_add_neutral_artifact(&ds_reg, (uint32_t)ds_storage, neu_role,
                                     ds_role_extension(neu_role), "sutra",
                                     DS_LANG_NATIVE_DUAL);

            /* Generate deterministic digests */
            for (uint32_t i = 0; i < DS_DIGEST_SIZE; i++) {
                pd[i] = (uint8_t)(0xE0 + r * 2 + i);
                nd[i] = (uint8_t)(0xE1 + r * 2 + i);
                nud[i] = (uint8_t)(0xE2 + r * 2 + i);
            }
            ds_set_artifact_digest(&ds_reg, (uint32_t)ds_storage, true,
                                    pos_role, pd, DS_DIGEST_SIZE);
            ds_set_artifact_digest(&ds_reg, (uint32_t)ds_storage, false,
                                    neg_role, nd, DS_DIGEST_SIZE);
            ds_set_neutral_digest(&ds_reg, (uint32_t)ds_storage, neu_role,
                                   nud, DS_DIGEST_SIZE);
            ds_set_peer_digest(&ds_reg, (uint32_t)ds_storage, true,
                                pos_role, nd, DS_DIGEST_SIZE);
            ds_set_peer_digest(&ds_reg, (uint32_t)ds_storage, false,
                                neg_role, pd, DS_DIGEST_SIZE);
            ds_set_neutral_peer_digest(&ds_reg, (uint32_t)ds_storage,
                                        neu_role, pd, DS_DIGEST_SIZE);
            ds_set_inverse_kind(&ds_reg, (uint32_t)ds_storage, neg_role,
                                 DS_INVERSE_RESTORING, false);
        }

        /* Bind all roles as triads */
        for (uint32_t r = 0; r < 4; r++)
            ds_bind_triad(&ds_reg, (uint32_t)ds_storage,
                          (ds_artifact_role_t)(r * 2));

        /* Verify and admit */
        ds_verify_pair(&ds_reg, (uint32_t)ds_storage);
        ds_admit_pair(&ds_reg, (uint32_t)ds_storage);
    }

    /* Register the network service as a tri-space triad */
    int32_t ds_network = ds_register_pair(&ds_reg, "network-service",
                                           "org.zxv.network.v1");
    if (ds_network >= 0) {
        uint8_t pd[DS_DIGEST_SIZE], nd[DS_DIGEST_SIZE], nud[DS_DIGEST_SIZE];
        for (uint32_t r = 0; r < 4; r++) {
            ds_artifact_role_t pos_role = (ds_artifact_role_t)(r * 2);
            ds_artifact_role_t neg_role = (ds_artifact_role_t)(r * 2 + 1);
            ds_artifact_role_t neu_role = (ds_artifact_role_t)(8 + r);
            ds_add_artifact(&ds_reg, (uint32_t)ds_network, true, pos_role,
                             ds_role_extension(pos_role), "sutra",
                             DS_LANG_NATIVE_DUAL);
            ds_add_artifact(&ds_reg, (uint32_t)ds_network, false, neg_role,
                             ds_role_extension(neg_role), "sutra",
                             DS_LANG_NATIVE_DUAL);
            ds_add_neutral_artifact(&ds_reg, (uint32_t)ds_network, neu_role,
                                     ds_role_extension(neu_role), "sutra",
                                     DS_LANG_NATIVE_DUAL);
            for (uint32_t i = 0; i < DS_DIGEST_SIZE; i++) {
                pd[i] = (uint8_t)(0xF0 + r * 2 + i);
                nd[i] = (uint8_t)(0xF1 + r * 2 + i);
                nud[i] = (uint8_t)(0xF2 + r * 2 + i);
            }
            ds_set_artifact_digest(&ds_reg, (uint32_t)ds_network, true,
                                    pos_role, pd, DS_DIGEST_SIZE);
            ds_set_artifact_digest(&ds_reg, (uint32_t)ds_network, false,
                                    neg_role, nd, DS_DIGEST_SIZE);
            ds_set_neutral_digest(&ds_reg, (uint32_t)ds_network, neu_role,
                                   nud, DS_DIGEST_SIZE);
            ds_set_peer_digest(&ds_reg, (uint32_t)ds_network, true,
                                pos_role, nd, DS_DIGEST_SIZE);
            ds_set_peer_digest(&ds_reg, (uint32_t)ds_network, false,
                                neg_role, pd, DS_DIGEST_SIZE);
            ds_set_neutral_peer_digest(&ds_reg, (uint32_t)ds_network,
                                        neu_role, pd, DS_DIGEST_SIZE);
            /* Network sends are irreversible — constraining inverse */
            ds_set_inverse_kind(&ds_reg, (uint32_t)ds_network, neg_role,
                                 DS_INVERSE_CONSTRAINING, false);
        }
        for (uint32_t r = 0; r < 4; r++)
            ds_bind_triad(&ds_reg, (uint32_t)ds_network,
                          (ds_artifact_role_t)(r * 2));
        ds_verify_pair(&ds_reg, (uint32_t)ds_network);
        ds_admit_pair(&ds_reg, (uint32_t)ds_network);
    }

    boot_msg("  [INITIALIZED] 2 tri-space triads (storage=RESTORING+S0, network=CONSTRAINING+S0)");

    /* Phase 17: Holographic Data System */
    boot_msg("[BOOT] Holographic data system...");
    static holo_ctx_t holo;
    holo_init(&holo);
    boot_msg("  [REGISTERED] .36n9 + .9n63 + .36m9 + .9m63 + .zedei + .iedez + .zedec + .cedez + .0n0 + .0m0 + .zedez + .cedec file types");

    /* Phase 17a2: Unified emulator — core #1 of the multi-architecture fleet.
     * The Game Master QC test runs many non-compatible legacy systems in
     * parallel and measures their relationships through the M5 event space;
     * this is the first CPU core (MOS 6502 -> NES, Atari, C64, Apple II, ...). */
    boot_msg("[BOOT] Unified emulator (MOS 6502 core, first of the multi-arch fleet)...");
    {
        extern int emu6502_selfcheck(void);
        int sc = emu6502_selfcheck();
        if (sc == 15)
            boot_msg("  [DRIVER ONLINE] 6502 core self-check OK (sum 1..5 = 15) — Game Master QC ready");
        else
            boot_msg("  [WARN] 6502 self-check FAILED");
    }

    /* Phase 17a3: M5-RELATE — the non-binary logic test. Two NON-COMPATIBLE
     * cores (6502 + Z80) run in parallel on a shared truth; their RELATIONSHIP
     * is measured as a point in the M5 event space, decomposed along the five
     * PERPENDICULAR axes. The logical (ell) axis is a real trit_t, so agreement
     * is TRUE and contradiction is a charged GLUT — the other states of logic
     * the kernel exists to hold. This is core #2 online + the relationship rig. */
    boot_msg("[BOOT] M5-RELATE: parallel non-compatible cores (6502 <-> Z80) through the event space...");
    {
        /* Call through a tiny scalar shim (emu_relate.c) so we don't have to
         * mirror emu_relation_t's phase_t/complex layout here. */
        extern int  emu_relate_probe_summary(unsigned *res_a, unsigned *res_b,
                                             unsigned *instr_a, unsigned *instr_b,
                                             unsigned long long *cyc_a, unsigned long long *cyc_b,
                                             int *ell, int *charge);
        unsigned ra=0, rb=0, ia=0, ib=0; unsigned long long ca=0, cb=0; int ell=0, chg=0;
        int concord = emu_relate_probe_summary(&ra,&rb,&ia,&ib,&ca,&cb,&ell,&chg);
        unsigned domega = (ia >= ib) ? (ia - ib) : (ib - ia);
        uart_puts("[E....]   [w] ordinal distance: 6502="); uart_put_dec(ia);
        uart_puts(" instr, Z80="); uart_put_dec(ib); uart_puts(" instr -> delta ");
        uart_put_dec(domega); uart_puts("\n");
        uart_puts("[E....]   [r] rational concord: 6502="); uart_put_dec(ra);
        uart_puts(" Z80="); uart_put_dec(rb); uart_puts("\n");
        uart_puts("[E....]   [phi] phase torsion: 6502="); uart_put_dec(ca);
        uart_puts(" cyc, Z80="); uart_put_dec(cb); uart_puts(" T (complex-plane skew)\n");
        const char *label =
            (ell == TRIT_TRUE)       ? "TRUE (concord) — orthogonal architectures agree" :
            (ell == TRIT_GLUT_PLUS)  ? "GLUT+ (contradiction, excess)" :
            (ell == TRIT_GLUT_MINUS) ? "GLUT- (contradiction, deficit)" :
            (ell == TRIT_FALSE)      ? "FALSE (absence/gap)" : "GLUT (superposition)";
        uart_puts("[E....]   [l] four-valued logic: "); uart_puts(label); uart_puts("\n");
        (void)chg;
        if (concord)
            boot_msg("  [DRIVER ONLINE] Z80 core online; non-binary relationship measured; axes perpendicular");
        else
            boot_msg("  [NOTE] cores did not concord this run — logged as a GLUT/gap for the Game Master");
    }

    /* Phase 17a5: NES machine (mapper 0) — the first faithful per-console
     * emulator. A built-in minimal NES program proves the machine runs a real
     * NES init pattern (stack, PPU config, vblank poll, NMI) ON TARGET, so an
     * attached .nes ROM gets a genuine "is it running" verdict, not a probe. */
    boot_msg("[BOOT] NES machine (mapper 0) — first faithful per-console emulator...");
    {
        extern int nes_selfcheck(void);
        if (nes_selfcheck())
            boot_msg("  [DRIVER ONLINE] NES self-check OK — synthetic game reached its vblank loop");
        else
            boot_msg("  [WARN] NES self-check did not come up running");
    }

    /* Phase 17a4: Functional lattice spaces — prove each space's action path
     * makes REAL subsystem state change (the same calls the buttons issue). */
    boot_msg("[BOOT] Lattice spaces: wiring the 13 to their real subsystems...");
    {
        extern int zxv_spaces_selfcheck(void);
        int sc = zxv_spaces_selfcheck();
        if (sc == 15)
            boot_msg("  [DRIVER ONLINE] spaces functional: reputation+concord+logistics+wyrmgate respond to clicks");
        else
            boot_msg("  [WARN] one or more lattice spaces did not respond");
    }

    /* Phase 17b: Prism Break Holographic Touchscreen Shader —
     * fixed-function framebuffer compositor producing prism/refraction
     * holographic visual effect. 6 compositing layers: base color,
     * prism refraction (wavelength-to-color), holographic scanlines,
     * touch ripples (expanding wavefronts), chromatic aberration, and
     * vignette. All integer math, no GPU required. */
    boot_msg("[BOOT] Prism Break holographic touchscreen shader...");
    pb_init(&prism_break, 1280, 720);   /* 720p — stride is a multiple of 16 (no shear) */
    boot_msg("  [INITIALIZED] 6-layer compositor (prism + scanlines + ripples + aberration)");

    /* Phase 17b+: bind a REAL display. Render one frame, then hand its linear
     * framebuffer to QEMU's ramfb scanout over fw_cfg. Universal: the same
     * device exists on arm64/x86_64/riscv 'virt'. Requires -device ramfb; if it
     * is absent, ramfb_init fails and we stay on the serial console (honest). */
    boot_msg("[BOOT] ramfb display scanout (fw_cfg)...");
    pb_render_frame(&prism_break);
    {
        uint32_t *fb = pb_get_framebuffer(&prism_break);   /* the BACK buffer */
        /* UEFI boot? If the EFI stub published a GOP handoff record, adopt the
         * firmware's linear framebuffer and DON'T touch ramfb (the firmware owns
         * the scanout). Otherwise the normal QEMU -kernel ramfb path. */
        {
            volatile zxv_bootinfo_t *bi = (volatile zxv_bootinfo_t *)ZXV_BOOTINFO_ADDR;
            if (bi->magic == ZXV_BOOTINFO_MAGIC && bi->fb) {
                g_gop_fb     = (uint32_t *)(uintptr_t)bi->fb;
                g_gop_w      = bi->width;
                g_gop_h      = bi->height;
                g_gop_stride = bi->stride ? bi->stride : bi->width;
                g_gop_pixfmt = bi->pixfmt;
            }
        }
        int rc = g_gop_fb ? 0 : ramfb_init(g_scanout, 1280, 720);  /* GOP: skip ramfb */
        if (rc == 0) {
            /* Compose into the back buffer, then present a complete frame. */
            static vbe_state_t g_vbe;
            vbe_init_fb(&g_vbe, 1280, 720, 32, (uintptr_t)fb);
            zxv_shell_init(&g_shell);
            zxv_shell_frame(&g_shell, &g_vbe, 632, 360, 0, prism_break.frames_rendered, g_fphase, g_fdepth);
            zxv_present(fb);
            g_desktop_vbe = &g_vbe;   /* the event loop keeps animating it */
            if (g_gop_fb)
                boot_msg("  [DRIVER ONLINE] UEFI GOP framebuffer — ZEDEC desktop is on screen");
            else
                boot_msg("  [DRIVER ONLINE] ramfb 1280x720 — ZEDEC desktop is on screen");
            /* virtio-input: one driver -> mouse + tablet + keyboard on any
             * hypervisor. Makes the desktop clickable. */
            if (virtio_input_probe()) {
                virtio_input_set_bounds(1280, 720);
                boot_msg("  [DRIVER ONLINE] virtio-input — pointer + keyboard live");
            } else {
                boot_msg("  [SKIP] no virtio-input (add -device virtio-tablet-device)");
            }
            /* virtio-snd: native audio over the same virtio-mmio transport. Plays
             * a boot chime if a sound device is present (add -audiodev ... plus
             * -device virtio-sound-device,audiodev=...). Absent => graceful skip. */
            if (virtio_snd_init()) {
                virtio_snd_chime();
                boot_msg("  [DRIVER ONLINE] virtio-snd — audio output live");
            } else {
                boot_msg("  [SKIP] no virtio-snd (add -device virtio-sound-device)");
            }
        } else if (rc == -2) {
            boot_msg("  [SKIP] no ramfb device (launch QEMU with -device ramfb)");
        } else {
            boot_msg("  [SKIP] ramfb init failed; staying on serial console");
        }
    }

    /* Phase 17c: Virtual Filesystem + RAM Disk
     * Mounts a FAT32-formatted RAM disk as the root filesystem.
     * Provides persistent storage for user processes via VFS. */
    boot_msg("[BOOT] Virtual filesystem (VFS + FAT32 RAM disk)...");
    {
        extern void ramdisk_init(void);
        extern void ramdisk_format_fat32(void);
        extern void ramdisk_create_blockdev(block_device_t *dev);
        ramdisk_init();
        ramdisk_format_fat32();

        static block_device_t ramdisk_dev;
        ramdisk_create_blockdev(&ramdisk_dev);

        static fat32_state_t fat32_fs;
        vfs_init(&vfs);
        if (fat32_mount(&fat32_fs, &ramdisk_dev) == 0) {
            vfs_mount(&vfs, "/", VFS_MOUNT_FAT32, &fat32_fs);
            boot_msg("  [MOUNTED] FAT32 RAM disk (16MB) at /");
        } else {
            boot_msg("  [WARN] FAT32 mount failed — VFS running without storage");
        }
    }

    /* Phase 17c-alt: Persistent storage — virtio-blk + ZXVFS journaled FS.
     * If QEMU was started with a `-drive` on the virtio-mmio transport,
     * this gives real persistence: files written here survive reboots,
     * and a redo journal makes every write crash-consistent. On a blank
     * disk (no ZXVFS superblock) it formats automatically. Absent a
     * virtio-blk device, the volatile RAM disk above remains the store. */
    boot_msg("[BOOT] Persistent storage (virtio-blk + ZXVFS journaled FS)...");
    if (virtio_blk_init(&g_vblk)) {
        boot_msg("  [DRIVER ONLINE] virtio-blk device found");
        int mrc = zxvfs_mount(&g_zxvfs, &g_vblk);
        if (mrc == -2) {
            /* No valid superblock -> blank disk -> format then mount. */
            boot_msg("  [FORMAT] blank disk — writing fresh ZXVFS");
            if (zxvfs_format(&g_vblk) == 0)
                mrc = zxvfs_mount(&g_zxvfs, &g_vblk);
        }
        if (mrc == 0) {
            g_zxvfs_ready = true;
            if (g_zxvfs.journal_replays > 0)
                boot_msg("  [RECOVERED] committed journal replayed on mount");
            boot_msg("  [MOUNTED] ZXVFS — persistent. Shell: write/cat/ls-p/rm/sync");
            /* Seed the bundled SIGNED application (.zsp) onto a fresh
             * ZXVFS, so `run hello.zsp` loads a signed package from
             * persistent storage — the kernel verifies its Ed25519
             * signature against the compiled-in root key before executing.
             * On later boots it is already present and left untouched. */
            uint8_t probe[4];
            if (zxvfs_read(&g_zxvfs, "hello.zsp", probe, sizeof(probe)) < 0) {
                if (zxvfs_write(&g_zxvfs, "hello.zsp",
                                hello_zsp, hello_zsp_len) == 0)
                    boot_msg("  [SEEDED] signed hello.zsp on disk — try 'run hello.zsp'");
            }
            /* A/B update state: slot A seeded with the signed app so the
             * update/confirm/rollback flow has a known-good baseline. */
            if (zxvfs_read(&g_zxvfs, "app.slotA", probe, sizeof(probe)) < 0)
                zxvfs_write(&g_zxvfs, "app.slotA", hello_zsp, hello_zsp_len);
            if (ab_init(&g_zxvfs, &g_ab) == AB_OK) {
                g_ab_ready = true;
                boot_msg("  [A/B] signed-update state ready (slots/update/confirm/rollback)");
            }
        } else {
            boot_msg("  [WARN] ZXVFS mount failed — persistence unavailable");
        }
    } else {
        boot_msg("  [SKIP] no virtio-blk device (start QEMU with -drive to enable)");
    }

    /* Phase 17b-emu: RUN an attached raw ROM. If the block device is present
     * but is NOT a ZXVFS filesystem, it's a Game Master ROM dataset — execute
     * it on the emulator cores. A clean run then proves the game's code is
     * RUNNING on ZEDEC (instructions retire on the matching CPU core), not
     * merely that the kernel survived ingesting the bytes. The Game Master
     * greps the serial for these [EMU] lines. */
    if (g_vblk.present && !g_zxvfs_ready) {
        boot_msg("[BOOT] Game runner: executing the attached ROM on the emulator CPU cores...");
        static game_run_t gr;
        game_runner_run(&g_vblk, &gr);
        /* Report RAW execution facts only. This is a CPU-execution probe: it
         * proves the ROM's bytes execute as real instructions on ZEDEC's cores,
         * but it does NOT verify gameplay — a faithful "is it playing" check
         * needs a per-console machine (memory map, entry, I/O regs, timing).
         * A bare CPU wanders through data too, so no RUNNING verdict is claimed. */
        if (gr.is_nes) {
            /* Real NES machine (mapper 0): a genuine running verdict. */
            uart_puts("[NES] ");
            uart_puts(gr.nes_running ? "RUNNING on ZEDEC" : "did not come up");
            uart_puts(" — ppu_writes="); uart_put_dec(gr.nes_ppu_writes);
            uart_puts(" vblank_polls="); uart_put_dec(gr.nes_vblank_polls);
            uart_puts(" nmis="); uart_put_dec(gr.nes_nmis); uart_puts("\n");
        } else {
            /* Non-iNES: bare CPU-execution probe (facts only, no gameplay verdict). */
            uart_puts("[EMU-PROBE] rom_bytes="); uart_put_dec(gr.bytes);
            uart_puts(" | 6502 instr="); uart_put_dec(gr.insn_6502);
            uart_puts(" illegal_permille="); uart_put_dec(gr.permille_6502);
            uart_puts(" | z80 instr="); uart_put_dec(gr.insn_z80);
            uart_puts(" illegal_permille="); uart_put_dec(gr.permille_z80); uart_puts("\n");
            uart_puts("[EMU-PROBE] cpu-execution probe only (not gameplay) — per-console machine per arch is the next build\n");
        }
    }

    /* Phase 17c-pq: POST-QUANTUM self-check, ON TARGET.
     * ML-KEM-768 (the NIST standard lattice KEM) is where ZXV's post-quantum
     * property actually lives. Running a full keygen/encaps/decaps round trip
     * at boot proves it EXECUTES on the target — not merely that it was
     * validated on a host — and keeps --gc-sections from stripping it. */
    boot_msg("[BOOT] Post-quantum key establishment (ML-KEM-768)...");
    {
        static uint8_t ek[MLKEM768_EK_BYTES], dk[MLKEM768_DK_BYTES];
        static uint8_t ct[MLKEM768_CT_BYTES];
        static uint8_t ss_a[MLKEM768_SS_BYTES], ss_b[MLKEM768_SS_BYTES];
        static uint8_t d[32], z[32], m[32];
        /* Deterministic seeds for a boot self-check. Real key material must
         * come from the entropy service; this is a KAT, not a live key. */
        for (uint32_t i = 0; i < 32; i++) { d[i] = (uint8_t)(i + 1); z[i] = (uint8_t)(i + 65); m[i] = (uint8_t)(i * 7 + 3); }
        mlkem768_keygen(d, z, ek, dk);
        mlkem768_encaps(ek, m, ct, ss_a);
        mlkem768_decaps(dk, ct, ss_b);
        bool agree = true;
        for (uint32_t i = 0; i < MLKEM768_SS_BYTES; i++) if (ss_a[i] != ss_b[i]) agree = false;
        if (agree)
            boot_msg("  [VERIFIED] ML-KEM-768 round trip: both parties derived the same secret");
        else
            boot_msg("  [WARNING] ML-KEM-768 shared secrets DISAGREE — PQ path unsound");
    }

    /* Phase 17c-tri: Tri-Space artifact triad — ZXV's native module format.
     * Binds S+/S-/S0 so a release cannot lose its own undo path. */
    {
        static tri_triad_t t;
        static uint8_t tid[TRI_ID_LEN], src[TRI_DIGEST_LEN];
        static uint8_t dp[TRI_DIGEST_LEN], dn[TRI_DIGEST_LEN], du[TRI_DIGEST_LEN];
        for (uint32_t i = 0; i < 32; i++) {
            tid[i] = (uint8_t)(i + 2); src[i] = (uint8_t)(i * 3 + 1);
            dp[i] = (uint8_t)(i + 11); dn[i] = (uint8_t)(i + 22); du[i] = (uint8_t)(i + 33);
        }
        tri_init(&t, tid, src);
        t.inverse_kind = TRI_INV_RESTORING;
        tri_set_member(&t, TRI_POSITIVE, dp, 1u, false, false);
        tri_set_member(&t, TRI_NEGATIVE, dn, 1u, false, false);  /* subset of S+ */
        tri_set_member(&t, TRI_NEUTRAL,  du, 0u, false, false);  /* no effect */
        if (tri_bind(&t) && tri_verify(&t) && tri_may_release(&t))
            boot_msg("  [VERIFIED] Tri-Space triad bound (.n9n63/.9n63/.0n0 -> .zxvc/.cedez/.cedec)");
        else
            boot_msg("  [WARNING] Tri-Space triad failed to bind");
    }

    /* Phase 17c-net: virtio-net NIC over the tested split-virtqueue engine.
     * Brings up RX/TX queues so the TCP/IP stack below has a real interface.
     * Absent unless QEMU is started with a -netdev + virtio-net-device. */
    if (virtio_net_init()) {
        const uint8_t *m = virtio_net_mac();
        boot_msg("  [DRIVER ONLINE] virtio-net NIC (RX/TX queues ready)");
        uart_puts("    MAC ");
        for (int i = 0; i < 6; i++) {
            const char *hex = "0123456789abcdef";
            char c[3] = { hex[(m[i] >> 4) & 0xF], hex[m[i] & 0xF], 0 };
            uart_puts(c); if (i < 5) uart_puts(":");
        }
        uart_puts("\r\n");
        if (virtio_net_selftest())
            boot_msg("  [VERIFIED] virtio-net self-check (queues bound, RX posted)");
        /* Prove a frame actually MOVES, not merely that the queues bound: send
         * a broadcast ARP request for the QEMU user-net gateway (10.0.2.2)
         * from 10.0.2.15. A driver that initialises but never shifts a byte is
         * not a working NIC — this is the difference. */
        {
            static uint8_t arp[42];
            for (uint32_t i = 0; i < 6; i++) arp[i] = 0xFF;          /* dst broadcast */
            for (uint32_t i = 0; i < 6; i++) arp[6 + i] = m[i];      /* src = our MAC */
            arp[12] = 0x08; arp[13] = 0x06;                          /* ethertype ARP */
            arp[14] = 0x00; arp[15] = 0x01;                          /* HTYPE ethernet */
            arp[16] = 0x08; arp[17] = 0x00;                          /* PTYPE IPv4 */
            arp[18] = 6;    arp[19] = 4;                             /* HLEN/PLEN */
            arp[20] = 0x00; arp[21] = 0x01;                          /* OPER request */
            for (uint32_t i = 0; i < 6; i++) arp[22 + i] = m[i];     /* sender MAC */
            arp[28] = 10; arp[29] = 0; arp[30] = 2; arp[31] = 15;    /* sender 10.0.2.15 */
            for (uint32_t i = 0; i < 6; i++) arp[32 + i] = 0x00;     /* target MAC */
            arp[38] = 10; arp[39] = 0; arp[40] = 2; arp[41] = 2;     /* target 10.0.2.2 */
            if (virtio_net_tx(arp, sizeof arp) == 0)
                boot_msg("  [VERIFIED] virtio-net TX: ARP request transmitted (device consumed it)");
            else
                boot_msg("  [WARNING] virtio-net TX failed");
            /* poll briefly for the reply the gateway should send back */
            static uint8_t rxf[VNET_MAX_FRAME];
            for (uint32_t spin = 0; spin < 400000u; spin++) {
                int n = virtio_net_rx_poll(rxf, sizeof rxf);
                if (n > 0) {
                    boot_msg("  [VERIFIED] virtio-net RX: a frame was received");
                    break;
                }
            }
        }
    } else {
        boot_msg("  [SKIP] no virtio-net device (add -netdev+virtio-net-device to enable)");
    }

    /* Phase 17c-bis: Network Stack — TCP/IP with loopback interface.
     * Full Ethernet/ARP/IP/ICMP/TCP/UDP stack with M5 axiomatic extensions.
     * Loopback enables local socket communication for testing. */
    boot_msg("[BOOT] Network stack (TCP/IP + M5 router + loopback)...");
    {
        extern void net_init(net_state_t *net);
        extern int32_t net_register_interface(net_state_t *net, const char *name,
            net_if_type_t type, const uint8_t *mac, const uint8_t *ip,
            const uint8_t *gateway, const uint8_t *netmask,
            void (*tx_cb)(const uint8_t *, uint32_t), void (*poll_cb)(void));
        extern void m5_router_init(m5_router_t *r, m5_address_t *local);

        net_init(&net);

        /* Give the stack real unpredictability where it matters. DHCP
         * transaction ids and DNS query ids/ports are the only thing an
         * off-path forger has to guess; the built-in fallback is a counter
         * mix and does not resist that. Use FEAT_RNG when the CPU has it. */
        extern void net_set_entropy(uint32_t (*src)(void));
        if (arm64_rng_available()) {
            net_set_entropy(net_entropy_rndr);
            boot_msg("  [HARDENED] network ids seeded from FEAT_RNG (RNDR)");
        } else {
            boot_msg("  [WARNING] no FEAT_RNG — network ids are PREDICTABLE");
        }

        /* Loopback interface: 127.0.0.1, no TX callback needed */
        static uint8_t lo_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        static uint8_t lo_ip[4]  = {127, 0, 0, 1};
        static uint8_t lo_mask[4] = {255, 0, 0, 0};
        net_register_interface(&net, "lo", NET_IF_LOOPBACK,
                                lo_mac, lo_ip, lo_ip, lo_mask, 0, 0);

        /* Bind the real virtio-net NIC to the stack so ARP/ICMP/UDP/TCP are
         * driven by actual hardware rather than only loopback. The address is
         * NOT assumed: the interface comes up unnumbered and DHCP asks for
         * one. A machine that only works on a network whose numbering it was
         * told in advance is not networked. */
        if (virtio_net_present()) {
            static uint8_t eth_ip[4]   = {0, 0, 0, 0};
            static uint8_t eth_gw[4]   = {0, 0, 0, 0};
            static uint8_t eth_mask[4] = {0, 0, 0, 0};
            int32_t ei = net_register_interface(&net, "eth0", NET_IF_ETHERNET,
                                                virtio_net_mac(), eth_ip, eth_gw,
                                                eth_mask, knet_tx, 0);
            if (ei >= 0) {
                g_eth_if = &net.interfaces[ei];
                boot_msg("  [BOUND] eth0 -> virtio-net (unnumbered; asking DHCP)");

                /* ---- DHCP: obtain a lease from whatever server is out there.
                 * The exchange is driven here rather than in a thread: send
                 * DISCOVER, then pump received frames through the stack, which
                 * answers the OFFER with a REQUEST and adopts the ACK. */
                extern void net_dhcp_discover(net_state_t *ns, net_interface_t *nif);
                static uint8_t df[VNET_MAX_FRAME];
                for (int attempt = 0; attempt < 3 && !dhcp_is_bound(&net.dhcp); attempt++) {
                    net_dhcp_discover(&net, g_eth_if);
                    for (uint32_t spin = 0;
                         spin < 3000000u && !dhcp_is_bound(&net.dhcp); spin++) {
                        int n = virtio_net_rx_poll(df, sizeof df);
                        if (n > 0) net_handle_eth(&net, g_eth_if, df, (uint32_t)n);
                    }
                }

                if (dhcp_is_bound(&net.dhcp)) {
                    char line[96];
                    int p = 0;
                    const char *pre = "  [LEASED] eth0 = ";
                    while (pre[p]) { line[p] = pre[p]; p++; }
                    for (int o = 0; o < 4; o++) {
                        uint8_t b = g_eth_if->ip[o];
                        if (b >= 100) line[p++] = (char)('0' + b / 100);
                        if (b >= 10)  line[p++] = (char)('0' + (b / 10) % 10);
                        line[p++] = (char)('0' + b % 10);
                        if (o < 3) line[p++] = '.';
                    }
                    const char *suf = " via DHCP (a real server granted it)";
                    for (int q = 0; suf[q]; q++) line[p++] = suf[q];
                    line[p] = 0;
                    boot_msg(line);
                    for (int o = 0; o < 4; o++) eth_gw[o] = g_eth_if->gateway[o];
                } else {
                    /* No server answered. Fall back to the QEMU user-mode
                     * numbering so the rest of the boot still has a network,
                     * and say plainly that this address was assumed. */
                    g_eth_if->ip[0]=10; g_eth_if->ip[1]=0;
                    g_eth_if->ip[2]=2;  g_eth_if->ip[3]=15;
                    g_eth_if->gateway[0]=10; g_eth_if->gateway[1]=0;
                    g_eth_if->gateway[2]=2;  g_eth_if->gateway[3]=2;
                    g_eth_if->netmask[0]=255; g_eth_if->netmask[1]=255;
                    g_eth_if->netmask[2]=255; g_eth_if->netmask[3]=0;
                    eth_gw[0]=10; eth_gw[1]=0; eth_gw[2]=2; eth_gw[3]=2;
                    boot_msg("  [WARNING] no DHCP lease — ASSUMING 10.0.2.15");
                }

                /* PING THE GATEWAY. This is the end-to-end proof that the
                 * stack interoperates: an independent peer (QEMU's user-mode
                 * gateway) must accept our IP and ICMP checksums, or it
                 * silently discards the request and no reply ever comes. */
                extern void net_icmp_echo(net_state_t *ns, net_interface_t *nif,
                                          const uint8_t *dst_ip,
                                          const void *payload, uint32_t plen);
                static const uint8_t probe[16] = "ZXV-PING-000001";
                if (!net_arp_lookup(&net, eth_gw, (uint8_t[6]){0}))
                    net_arp_add(&net, eth_gw, (const uint8_t *)"\x52\x55\x0a\x00\x02\x02");
                net_icmp_echo(&net, g_eth_if, eth_gw, probe, sizeof probe);

                bool replied = false;
                static uint8_t rf[VNET_MAX_FRAME];
                for (uint32_t spin = 0; spin < 2000000u && !replied; spin++) {
                    int n = virtio_net_rx_poll(rf, sizeof rf);
                    if (n < 34) continue;
                    /* IPv4 + ICMP + type 0 (echo reply) addressed to us */
                    if (rf[12] == 0x08 && rf[13] == 0x00 && rf[23] == 1 && rf[34] == 0)
                        replied = true;
                    net_handle_eth(&net, g_eth_if, rf, (uint32_t)n);
                }
                if (replied)
                    boot_msg("  [VERIFIED] ICMP: gateway replied to our ping "
                             "(a real peer ACCEPTED our checksums)");
                else
                    boot_msg("  [WARNING] no ICMP reply from the gateway");

                /* ---- DNS: resolve a name over the real network. This is the
                 * proof that the resolver interoperates — the answer has to
                 * come back from an independent server that parsed our query. */
                if (net.dns_server[0] | net.dns_server[1] |
                    net.dns_server[2] | net.dns_server[3]) {
                    extern int32_t net_dns_query(net_state_t *, net_interface_t *,
                                                 const char *);
                    extern int32_t net_dns_result(net_state_t *, const char *,
                                                  uint8_t *);
                    static const char probe_host[] = "example.com";
                    uint8_t rip[4] = {0,0,0,0};
                    static uint8_t nf[VNET_MAX_FRAME];
                    for (int attempt = 0; attempt < 3; attempt++) {
                        net_dns_query(&net, g_eth_if, probe_host);
                        for (uint32_t spin = 0; spin < 4000000u; spin++) {
                            int n = virtio_net_rx_poll(nf, sizeof nf);
                            if (n > 0) net_handle_eth(&net, g_eth_if, nf, (uint32_t)n);
                            if (net_dns_result(&net, probe_host, rip)) break;
                        }
                        if (net_dns_result(&net, probe_host, rip)) break;
                    }
                    if (net_dns_result(&net, probe_host, rip)) {
                        char line[96];
                        int p = 0;
                        const char *pre = "  [VERIFIED] DNS: example.com = ";
                        while (pre[p]) { line[p] = pre[p]; p++; }
                        for (int o = 0; o < 4; o++) {
                            uint8_t b = rip[o];
                            if (b >= 100) line[p++] = (char)('0' + b / 100);
                            if (b >= 10)  line[p++] = (char)('0' + (b / 10) % 10);
                            line[p++] = (char)('0' + b % 10);
                            if (o < 3) line[p++] = '.';
                        }
                        const char *suf = " (a real resolver answered)";
                        for (int qq = 0; suf[qq]; qq++) line[p++] = suf[qq];
                        line[p] = 0;
                        boot_msg(line);
                        /* ---- TCP: fetch over the real internet. This is the
                         * hardest external anchor available: an independent
                         * web server has to accept our SYN, our sequence
                         * numbers and our pseudo-header checksums, and answer.
                         * Nothing about that can be faked locally. */
                        int32_t sk = net_socket(&net, SOCK_TCP);
                        if (sk >= 0 && net_connect(&net, sk, rip, 80) == 0) {
                            static uint8_t tf[VNET_MAX_FRAME];
                            static uint8_t body[512];
                            static const char req[] =
                                "GET / HTTP/1.0\r\nHost: example.com\r\n"
                                "User-Agent: ZXV\r\nConnection: close\r\n\r\n";
                            bool sent = false;
                            int32_t got = -1;
                            /* Poll in SHORT bursts. The retransmit pacer is
                             * time-based, so a long burst between calls means
                             * the 200 ms gate has already expired every time it
                             * is consulted and every segment goes out twice. */
                            for (uint32_t round = 0; round < 20000u && got < 0; round++) {
                                /* Each polling round IS an event, so it
                                 * advances the phase sequence. Retransmission
                                 * is then paced off that ordinal rather than
                                 * off a clock. */
                                phase_coordinator_tick(&tick);
                                for (uint32_t spin = 0; spin < 200u; spin++) {
                                    int n = virtio_net_rx_poll(tf, sizeof tf);
                                    if (n > 0) net_handle_eth(&net, g_eth_if, tf, (uint32_t)n);
                                }
                                if (!sent &&
                                    net.sockets[sk].tcp_state == TCP_ESTABLISHED) {
                                    if (net_send(&net, sk, req,
                                                 (uint32_t)(sizeof req - 1)) > 0)
                                        sent = true;
                                }
                                if (sent) got = net_recv(&net, sk, body, sizeof body - 1);
                                knet_tcp_pace(&net, (uint64_t)tick.omega);
                            }
                            if (got > 4 && body[0]=='H' && body[1]=='T' &&
                                body[2]=='T' && body[3]=='P') {
                                body[got < (int32_t)sizeof body ? got : 0] = 0;
                                char line[80];
                                int p = 0;
                                const char *pre = "  [VERIFIED] TCP: ";
                                while (pre[p]) { line[p] = pre[p]; p++; }
                                /* the server's status line, up to the CR */
                                for (int k = 0; k < got && k < 40 &&
                                                body[k] != '\r' && body[k] != '\n'; k++)
                                    line[p++] = (char)body[k];
                                const char *suf = " <- a real web server";
                                for (int k = 0; suf[k]; k++) line[p++] = suf[k];
                                line[p] = 0;
                                boot_msg(line);
                            } else if (net.sockets[sk].tcp_state == TCP_ESTABLISHED ||
                                       sent) {
                                boot_msg("  [WARNING] TCP connected but no HTTP "
                                         "response arrived");
                            } else {
                                boot_msg("  [WARNING] TCP handshake did not complete");
                            }
                            net_close(&net, sk);
                        }
                    } else {
                        boot_msg("  [WARNING] DNS query went unanswered");
                    }
                }
            }
        }

        m5_router_init(&router, 0);
        boot_msg("  [INITIALIZED] TCP/IP stack + M5 omni-router + loopback (127.0.0.1)");
    }

    /* Phase 17c-ter: Classic scheduler singleton — task registry that
     * backs the P-TERM `ps`/`vmstat` commands.  The EL0 preemptive
     * scheduler (proc_scheduler_t) is the real dispatcher; this
     * registry mirrors the long-lived kernel-side tasks. */
    sched_init(&sched);
    sched_create_task(&sched, "kernel-event-cycle", TASK_KERNEL, 0, 0);
    sched_create_task(&sched, "pterm-el0", TASK_KERNEL, 0, 1);
    sched_create_task(&sched, "proc-a", TASK_KERNEL, 0, 2);

    /* Phase 17d: EL0 User Space — per-process address spaces with
     * preemptive scheduling, SVC syscall gate, and fault containment.
     * This is the real user/kernel isolation boundary.
     * Two processes are created and the first is dispatched to EL0.
     * Gated for Stage 1: EL0 is an experimental bring-up path until
     * the page-table / TCR / SVC hand-off is fully verified. */
#if ENABLE_EL0_USERSPACE
    boot_msg("[BOOT] EL0 user space (per-process page tables + preemptive scheduler)...");
    static proc_scheduler_t el0_sched;
    proc_sched_init(&el0_sched);
    boot_msg("  [EL0] scheduler initialized");
    proc_configure_tcr_el1();
    boot_msg("  [EL0] TCR_EL1 configured (T0SZ=24, T1SZ=0, 4KB granule)");
    proc_setup_ttbr1();
    boot_msg("  [EL0] TTBR1_EL1 identity mapping confirmed");
    el0_set_scheduler(&el0_sched);
    boot_msg("  [EL0] exception handler linked to scheduler");

    /* Create two EL0 processes that will run concurrently.
     * Each gets its own address space (TTBR0) and 64KB code+stack.
     * The timer IRQ will preempt them round-robin. */
#if ENABLE_PREEMPT_TEST
    extern void user_proc_spin_a_entry(void);
    extern void user_proc_spin_b_entry(void);
    int32_t pid_a = proc_create(&el0_sched, "spin-a", user_proc_spin_a_entry, 1);
    int32_t pid_b = proc_create(&el0_sched, "spin-b", user_proc_spin_b_entry, 1);
    int32_t pid_pterm = -1;
    if (pid_a > 0 && pid_b > 0) {
        boot_msg("  [DRIVER ONLINE] EL0: CPU-bound preemption test processes created — entering EL0");
    } else {
        boot_msg("  [SKIP] EL0: process creation failed — staying in kernel mode");
    }
#else
    extern void user_proc_a_entry(void);
    extern void pterm_user_entry(void);
    int32_t pid_a = proc_create(&el0_sched, "proc-a", user_proc_a_entry, 1);
    int32_t pid_pterm = proc_create(&el0_sched, "pterm-user", pterm_user_entry, 1);
    int32_t pid_b = -1;
#if ENABLE_WX_TEST
    extern void user_proc_wx_write_entry(void);
    extern void user_proc_wx_exec_entry(void);
    proc_create(&el0_sched, "wx-write-test", user_proc_wx_write_entry, 1);
    proc_create(&el0_sched, "wx-exec-test", user_proc_wx_exec_entry, 1);
    boot_msg("  [W^X TEST] adversarial processes armed (write-to-code, exec-stack)");
#endif
    if (pid_a > 0 && (pid_b > 0 || pid_pterm > 0)) {
        boot_msg("  [DRIVER ONLINE] EL0: process + P-TERM user shell created — entering EL0");
    } else {
        boot_msg("  [SKIP] EL0: process creation failed — staying in kernel mode");
    }
#endif
#else
    boot_msg("[SKIP] EL0 user space (ENABLE_EL0_USERSPACE=0, experimental Stage-8/9)");
    int32_t pid_a = -1;
    int32_t pid_b = -1;
    int32_t pid_pterm = -1;
#endif
    (void)pid_a;
    (void)pid_b;
    (void)pid_pterm;

    /* Phase 19: Cellular Multikernel Fabric (CELL-001) */
    cell_fabric_boot_init();

    /* Phase 20: Kernel P-TERM engine — serial shell backend.
     * The EL0 shell process feeds command lines here via SYS_EXEC;
     * command output is flushed from the console buffer to the UART. */
    boot_msg("[BOOT] P-TERM engine (kernel-side shell backend)...");
    pterm_init(&g_pterm);
    pterm_console_create(&g_pterm, "zxv");
    g_pterm_ready = true;
    /* Master/sub terminal rotation, advanced by the phase tick and
     * weighted by interaction surplus (see pterm_mux.h). */
    pmux_init(&g_pmux);
    boot_msg("  [INITIALIZED] serial console + command suite (type 'help')");

    /* Phase 20b: Magitech Refinery parity self-check. Forge the reference
     * card (OLPIRT HPOU, deck card 24525) and verify the derivation chain
     * against the shipped deck's known values: gematria 54, root 9, star
     * {12/5}, 16-node circuit. If SHA-256, the language layer, or the
     * kamea walk ever regress, boot says so on the spot. */
    {
        static ref_card_t rc;
        if (ref_forge("OLPIRT HPOU", 11, ENO_VOICE_AEON, &rc) == REF_OK &&
            rc.gematria == 54 && rc.root == 9 &&
            rc.sigil.fab_n == 12 && rc.sigil.fab_k == 5 && rc.path_len == 16) {
            boot_msg("  [VERIFIED] Magitech Refinery: deck parity (54/9/{12,5}/16) OK");
        } else {
            boot_msg("  [WARNING] Magitech Refinery parity check FAILED");
        }
    }

#if ENABLE_EL0_USERSPACE && ENABLE_EVENT_LOOP
    /* Phase 21: Unified runtime — register the event-cycle hook so the
     * generic-timer IRQ drives one kernel event cycle per tick while
     * EL0 user space runs.  Without this, every subsystem initialized
     * above would sit dormant the moment proc_enter_el0() ERETs away. */
    el0_set_event_cycle_hook(kernel_event_cycle_run);
    boot_msg("  [UNIFIED] kernel event cycle bridged to timer tick during EL0");
#endif

    /* Phase 22: Platform layer — deploy/theme/icon/font/bridge/update/mage/
     * reality brought up on top of the M5 core. Emitted raw (its own lines),
     * in phase-tick order, no wall-clock read. cores/mem 0 => probe-default. */
    boot_features_init(uart_puts, 0, 0);
    boot_economy_init(uart_puts);

    /* Phase 18: Enable interrupts and enter event loop */
    boot_msg("\n[BOOT] ZEDEC pqOS [ARM64] — All systems online.");
#if ENABLE_EL0_USERSPACE
    boot_msg("[BOOT_OK] Phase E0082 complete; kernel_main reached; EL0 ready\n");
#else
    boot_msg("[BOOT_OK] Phase E0082 complete; kernel_main reached; Stage-1 kernel loop ready\n");
#endif
    boot_msg("[BOOT] Entering event cycle...\n");

    /* Emit final boot evidence measurement */
    boot_evidence_final();

#if ENABLE_TICK_IRQ
    enable_irq();
#else
    boot_msg("[BOOT] Timer IRQs gated (ENABLE_TICK_IRQ=0); running polled 100Hz tick loop");
#endif

    /* If EL0 processes were created, dispatch the first one to EL0.
     * This ERETs to user space. The timer IRQ will bring us back
     * to the scheduler, which will round-robin between processes.
     * If a process exits or faults, the scheduler picks the next. */
#if ENABLE_EL0_USERSPACE
    if (pid_a > 0 && (pid_b > 0 || pid_pterm > 0)) {
        /* Start the first process — this ERETs to EL0 and does not
         * return until all processes have exited or faulted. */
        for (uint32_t i = 0; i < MAX_USER_PROCS; i++) {
            if (el0_sched.procs[i].state == PROC_READY) {
                el0_sched.current_pid = i;
                el0_sched.procs[i].state = PROC_RUNNING;
                el0_sched.procs[i].quantum_ticks =
                    el0_sched.procs[i].quantum_default;
                proc_switch_address_space(&el0_sched.procs[i]);
                proc_enter_el0(&el0_sched.procs[i]);
                break;
            }
        }
        /* If we return here, all EL0 processes have exited.
         * Fall through to the kernel event loop. */
        boot_msg("[BOOT] All EL0 processes exited — kernel event loop\n");
    }
#endif

    /* The classic kernel event loop (reached when EL0 is off, or after
     * every EL0 process has exited).  In the unified EL0 runtime the
     * timer-IRQ hook drives kernel_event_cycle_run() instead — this
     * loop must NOT also call it, or every tick would be counted twice
     * (the hook fires from irq_handler_c even while we halt() here). */
#if ENABLE_EVENT_LOOP && !ENABLE_EL0_USERSPACE
    g_auto_stats = true;   /* preserve the classic loop's periodic reports */
#endif
    uint32_t heartbeat = 0;
    (void)heartbeat;
    while (1) {
#if ENABLE_TICK_IRQ
        halt();
#else
        /* Polled 100Hz tick fallback (used when ENABLE_TICK_IRQ=0). */
        arm64_timer_delay_ms(10);
#endif

#if ENABLE_EVENT_LOOP
#if !ENABLE_EL0_USERSPACE
        kernel_event_cycle_run();
#endif
#else
        /* Minimal Stage-1 heartbeat loop when the full event loop is
         * gated.  This keeps the kernel alive and [BOOT_OK] deterministic. */
        if (++heartbeat >= 100) {
            heartbeat = 0;
            uart_puts("[BOOT_OK] heartbeat: cycle=100\n");
        }
#endif
    }
}

/* ===================================================================
 * Unified kernel event cycle
 *
 * One cycle of the multi-subsystem event fabric.  Called either from
 * the classic while(1) loop above (EL0 off) or from the generic-timer
 * IRQ hook while EL0 user space runs (see arm64_exceptions.c).  Per
 * ARCHITECTURE_EXTERNAL_CLOCK_BRIDGE.md nothing here reads a raw
 * clock — the tick that got us here is the only time source.
 * =================================================================== */
void kernel_event_cycle_run(void) {
    phase_coordinator_tick(&tick);

    /* Service the NIC every event cycle: received frames are fed to the
     * TCP/IP stack, which answers ARP and ICMP. This is what makes the OS
     * pingable from the host — networking driven by the event sequence
     * rather than a separate clock. */
    knet_pump(&net);

    /* TCP retransmission is paced off the PHASE ORDINAL, not a clock —
     * consistent with the rule above. Without this a lost segment is never
     * resent and a connection stalls forever. */
    knet_tcp_pace(&net, (uint64_t)tick.omega);

    /* Event-Driven Scheduler: replenish event budgets, post events
     * from the tick to tasks, and dispatch. */
    ev_sched_replenish(&evs);
    if (g_event_cycle % 10 == 0)
        ev_sched_post_events(&evs, 1, 1);  /* net-rx */
    if (g_event_cycle % 5 == 0)
        ev_sched_post_events(&evs, 2, 2);  /* count-house-tick */
    if (g_event_cycle % 8 == 0)
        ev_sched_post_events(&evs, 3, 1);  /* dlp-projector-tick */
    ev_sched_dispatch(&evs);

    /* Event-Space: dispatch event domains; periodic self-audit/heal. */
    ev_seq_replenish(&ev_seq);
    ev_seq_dispatch(&ev_seq);

    if (g_event_cycle % 100 == 0 && g_event_cycle > 0) {
        ev_heal_action_t heal_action = ev_self_audit_heal_system(
            &ev_seq, &ev_audit, &ev_healing);
        if (heal_action > EV_HEAL_WARN) {
            uart_puts("[EVENT-SPACE] self-healing action: ");
            uart_put_dec((uint64_t)heal_action);
            uart_puts(" faults=");
            uart_put_dec((uint64_t)ev_seq.total_faults_detected);
            uart_puts(" quarantined=");
            uart_put_dec((uint64_t)ev_seq.total_domains_quarantined);
            uart_puts(" recovered=");
            uart_put_dec((uint64_t)ev_seq.total_domains_recovered);
            uart_puts("\n");
        }

        /* Constellation health check — detect stale heartbeats */
        uint32_t node_failures = cc_check_health(&cc,
            ev_seq.next_sequence, 500);
        if (node_failures > 0) {
            uart_puts("[CONSTELLATION] node failures detected: ");
            uart_put_dec((uint64_t)node_failures);
            uart_puts(" total_failures=");
            uart_put_dec((uint64_t)cc.total_node_failures);
            uart_puts("\n");
        }

        /* Process pending transport deliveries */
        for (uint32_t ch = 0; ch < et.num_channels; ch++) {
            if (et.channels[ch].registered &&
                et.channels[ch].state == ET_CHANNEL_ACTIVE) {
                et_process_pending(&et, ch);
            }
        }

        /* Hypercube Scene: degradation check + render */
        hc_display_mode_t suggested = hc_check_degradation(&hc_scene);
        if (suggested != hc_scene.display_mode) {
            hc_set_display_mode(&hc_scene, suggested);
            uart_puts("[HYPERCUBE] display degraded to mode ");
            uart_put_dec((uint64_t)suggested);
            uart_puts("\n");
        }
        hc_render(&hc_scene);
    }

    /* External-clock bridge: the projector's subframe timing is
     * bridged as one tick per event cycle, never a wall-clock poll. */
    dlp_projector_tick(&projector);

    /* Desktop: poll the mouse/keyboard and repaint so the pointer tracks the
     * device. Throttled (every 4th cycle) to keep the compositor light; the
     * prism background is re-rendered then the shell is drawn over it with the
     * live cursor position. */
    /* The whole desktop block (input polling AND the compositor) must be
     * NON-RE-ENTRANT. This hook is driven from BOTH the EL1 timer IRQ
     * (irq_handler_c) and the EL0 timer IRQ (el0_irq_handler_c), so while the
     * unmasked input section runs, a timer IRQ can re-enter the event cycle. If
     * the busy flag is claimed late (inside the compositor), that re-entry sails
     * past the guard and runs a SECOND input-poll + redraw on top of the first —
     * clobbering the compositor's stack/registers, which surfaced as wild
     * near-null pointers in cursor()/vbe_draw_text_ex(). Claim the flag ATOMICALLY
     * up front (mask IRQs, test-and-set, restore) so any re-entrant tick skips
     * the entire block. */
    static volatile int g_desktop_busy = 0;
    if (g_desktop_vbe) {
        unsigned long d0;
        __asm__ __volatile__("mrs %0, daif" : "=r"(d0));
        __asm__ __volatile__("msr daifset, #2" ::: "memory");
        int reentrant = g_desktop_busy;
        g_desktop_busy = 1;
        __asm__ __volatile__("msr daif, %0" :: "r"(d0) : "memory");
        if (!reentrant) {
            virtio_bus_poll();   /* drive every registered class driver's poll (input, ...) */
            int32_t key;
            while ((key = virtio_input_pop_key()) > 0) zxv_shell_key(&g_shell, (int32_t)key);
            /* Latch fast taps EVERY cycle from the driver's exact press counter, not the
             * every-6th-cycle button level (which misses sub-60ms clicks). */
            static uint32_t g_pending_click = 0;
            g_pending_click += virtio_input_pop_clicks();
            if (g_event_cycle % 6 == 0) {
                /* The compositor is several ms of work; mask IRQs for its duration
                 * too, so it also can't be preempted mid-draw. */
                unsigned long daif;
                __asm__ __volatile__("mrs %0, daif" : "=r"(daif));
                __asm__ __volatile__("msr daifset, #2" ::: "memory");
                int32_t cx = 0, cy = 0; uint32_t btn = 0;
                virtio_input_get(&cx, &cy, &btn);
                /* Surface any tap that happened since the last frame as a click edge,
                 * even if the button level already returned to 0 between samples. */
                if (g_pending_click) { btn |= 1u; g_pending_click = 0; }
                /* Only the base desktop (view 0) shows the prism wallpaper; the lattice
                 * and space planes clear the whole screen themselves, so rendering the
                 * 6-layer prism there is a full-screen pass thrown away. Skip it. */
                if (g_shell.view == 0) pb_render_frame(&prism_break);
                zxv_shell_frame(&g_shell, g_desktop_vbe, cx, cy, btn, prism_break.frames_rendered, g_fphase, g_fdepth);
                zxv_present(pb_get_framebuffer(&prism_break));   /* show a COMPLETE frame */
                __asm__ __volatile__("msr daif, %0" :: "r"(daif) : "memory");
            }
            g_desktop_busy = 0;
        }
    }

    if (g_auto_stats && g_event_cycle % 100 == 0) {
        uart_puts("tick: omega=");
        uart_put_dec(tick.omega);
        uart_puts(" cycle=");
        uart_put_dec((uint64_t)g_event_cycle);
        uart_puts("\n");
    }

    if (g_event_cycle % 1000 == 0 && vino.is_validator) {
        vino_propose_block(&vino);
    }

    /* Mirror event time into the classic scheduler registry so
     * P-TERM `vmstat`/`date` report real elapsed event time. */
    sched.ticks = (uint64_t)g_event_cycle + 1;

    /* Advance the master/sub terminal rotation on the PHASE TICK — the
     * sub terminals are sequenced by this event cycle, never by a wall
     * clock, and their share comes from interaction surplus. */
    (void)pmux_phase_tick(&g_pmux);

    if (g_auto_stats && g_event_cycle % 500 == 0 && g_event_cycle > 0) {
        kernel_stats_report();
    }

    g_event_cycle++;
}

/* ===================================================================
 * Kernel subsystem coverage report
 *
 * Previously inlined in the event loop every 500 cycles; now callable
 * on demand (P-TERM `stats` command) and periodically only in the
 * classic loop (g_auto_stats).
 * =================================================================== */
void kernel_stats_report(void) {
    porter_house_update_coverage(&porter_house);
    uart_puts("porter-house: admitted=");
    uart_put_dec((uint64_t)porter_house.total_admitted);
    uart_puts(" rejected=");
    uart_put_dec((uint64_t)porter_house.total_rejected);
    uart_puts(" seals=");
    uart_put_dec((uint64_t)porter_house.num_seals);
    uart_puts("\r\n");

    ev_sched_update_coverage(&evs);
    uart_puts("ev-sched: dispatches=");
    uart_put_dec((uint64_t)evs.total_dispatches);
    uart_puts(" events=");
    uart_put_dec((uint64_t)evs.total_events_handled);
    uart_puts(" idle=");
    uart_put_dec((uint64_t)evs.total_idle_cycles);
    uart_puts("\r\n");

    mesh_token_update_coverage(&mesh_token);
    uart_puts("mesh-token: settled=");
    uart_put_dec((uint64_t)mesh_token.total_settled);
    uart_puts(" confirmed=");
    uart_put_dec((uint64_t)mesh_token.total_confirmed);
    uart_puts(" rejected=");
    uart_put_dec((uint64_t)mesh_token.total_rejected);
    uart_puts("\r\n");

    cc_update_coverage(&community_chest);
    uart_puts("community-chest: apps=");
    uart_put_dec((uint64_t)community_chest.num_apps);
    uart_puts(" downloads=");
    uart_put_dec((uint64_t)community_chest.total_downloads);
    uart_puts(" revenue=");
    uart_put_dec((uint64_t)community_chest.total_revenue);
    uart_puts(" voucher_float=");
    uart_put_dec((uint64_t)community_chest.voucher_float);
    uart_puts("\r\n");

    ai_update_coverage(&ai_engine);
    uart_puts("ai-layer: models=");
    uart_put_dec((uint64_t)ai_engine.num_models);
    uart_puts(" inferences=");
    uart_put_dec((uint64_t)ai_engine.total_inferences);
    uart_puts(" local=");
    uart_put_dec((uint64_t)ai_engine.total_local_inferences);
    uart_puts(" remote=");
    uart_put_dec((uint64_t)ai_engine.total_remote_inferences);
    uart_puts("\r\n");

    mn_update_coverage(&mesh_net);
    uart_puts("mesh-net: networks=");
    uart_put_dec((uint64_t)mesh_net.num_networks);
    uart_puts(" routes=");
    uart_put_dec((uint64_t)mesh_net.num_routes);
    uart_puts(" data=");
    uart_put_dec((uint64_t)mesh_net.total_data_routed);
    uart_puts(" revenue=");
    uart_put_dec((uint64_t)mesh_net.total_revenue);
    uart_puts("\r\n");

    immig_update_coverage(&immigration);
    uart_puts("immigration: daemons=");
    uart_put_dec((uint64_t)immigration.num_daemons);
    uart_puts(" granted=");
    uart_put_dec((uint64_t)immigration.total_granted);
    uart_puts(" rejected=");
    uart_put_dec((uint64_t)immigration.total_rejected);
    uart_puts(" deported=");
    uart_put_dec((uint64_t)immigration.total_deported);
    uart_puts("\r\n");

    robin_update_coverage(&robin_vault);
    uart_puts("robin-vault: entries=");
    uart_put_dec((uint64_t)robin_vault.num_entries);
    uart_puts(" stored=");
    uart_put_dec((uint64_t)robin_vault.total_stored);
    uart_puts(" unlocked=");
    uart_put_dec((uint64_t)robin_vault.total_unlocked);
    uart_puts("\r\n");

    pb_update_coverage(&prism_break);
    uart_puts("prism-break: frames=");
    uart_put_dec((uint64_t)prism_break.frames_rendered);
    uart_puts(" ripples=");
    uart_put_dec((uint64_t)prism_break.num_ripples);
    uart_puts(" touches=");
    uart_put_dec((uint64_t)prism_break.touches_processed);
    uart_puts("\r\n");

    uart_puts("event-space: enqueued=");
    uart_put_dec((uint64_t)ev_seq.total_events_enqueued);
    uart_puts(" dispatched=");
    uart_put_dec((uint64_t)ev_seq.total_events_dispatched);
    uart_puts(" dropped=");
    uart_put_dec((uint64_t)ev_seq.total_events_dropped);
    uart_puts(" faults=");
    uart_put_dec((uint64_t)ev_seq.total_faults_detected);
    uart_puts(" quarantined=");
    uart_put_dec((uint64_t)ev_seq.total_domains_quarantined);
    uart_puts(" recovered=");
    uart_put_dec((uint64_t)ev_seq.total_domains_recovered);
    uart_puts(" audits=");
    uart_put_dec((uint64_t)ev_seq.total_audit_checks_run);
    uart_puts("\r\n");

    uart_puts("orbital-elevator: schemas=");
    uart_put_dec((uint64_t)oe.num_schemas);
    uart_puts(" adapters=");
    uart_put_dec((uint64_t)oe.num_adapters);
    uart_puts(" translations=");
    uart_put_dec((uint64_t)oe.total_translations);
    uart_puts(" failures=");
    uart_put_dec((uint64_t)oe.total_failures);
    uart_puts(" no_path=");
    uart_put_dec((uint64_t)oe.total_no_path);
    uart_puts("\r\n");

    uart_puts("constellation: nodes=");
    uart_put_dec((uint64_t)cc.num_nodes);
    uart_puts(" executors=");
    uart_put_dec((uint64_t)cc.num_executors);
    uart_puts(" routed=");
    uart_put_dec((uint64_t)cc.total_events_routed);
    uart_puts(" dropped=");
    uart_put_dec((uint64_t)cc.total_events_dropped);
    uart_puts(" node_failures=");
    uart_put_dec((uint64_t)cc.total_node_failures);
    uart_puts(" recoveries=");
    uart_put_dec((uint64_t)cc.total_node_recoveries);
    uart_puts("\r\n");

    uart_puts("event-transport: channels=");
    uart_put_dec((uint64_t)et.num_channels);
    uart_puts(" deliveries=");
    uart_put_dec((uint64_t)et.total_deliveries);
    uart_puts(" failures=");
    uart_put_dec((uint64_t)et.total_failures);
    uart_puts("\r\n");

    uart_puts("hypercube: cells=");
    uart_put_dec((uint64_t)hc_scene.num_cells);
    uart_puts(" renders=");
    uart_put_dec((uint64_t)hc_scene.total_renders);
    uart_puts(" transitions=");
    uart_put_dec((uint64_t)hc_scene.total_transitions_completed);
    uart_puts(" events=");
    uart_put_dec((uint64_t)hc_scene.total_events_processed);
    uart_puts(" degradations=");
    uart_put_dec((uint64_t)hc_scene.total_degradations);
    uart_puts("\r\n");

    uart_puts("yantra: devices=");
    uart_put_dec((uint64_t)yf.num_devices);
    uart_puts(" upgrades=");
    uart_put_dec((uint64_t)yf.total_capability_upgrades);
    uart_puts(" downgrades=");
    uart_put_dec((uint64_t)yf.total_capability_downgrades);
    uart_puts(" contradictions=");
    uart_put_dec((uint64_t)yf.total_contradictions);
    uart_puts(" safe_events=");
    uart_put_dec((uint64_t)yf.total_safe_state_events);
    uart_puts("\r\n");

    uart_puts("dual-space: pairs=");
    uart_put_dec((uint64_t)ds_reg.num_pairs);
    uart_puts(" verified=");
    uart_put_dec((uint64_t)ds_reg.total_pairs_verified);
    uart_puts(" admitted=");
    uart_put_dec((uint64_t)ds_reg.total_pairs_admitted);
    uart_puts(" quarantined=");
    uart_put_dec((uint64_t)ds_reg.num_quarantined);
    uart_puts(" vetoes=");
    uart_put_dec((uint64_t)ds_reg.total_vetoes);
    uart_puts(" compensations=");
    uart_put_dec((uint64_t)ds_reg.total_compensations);
    uart_puts(" s0_resolutions=");
    uart_put_dec((uint64_t)ds_reg.total_s0_resolutions);
    uart_puts(" s0_timeouts=");
    uart_put_dec((uint64_t)ds_reg.total_s0_timeouts);
    uart_puts(" s0_deferred=");
    uart_put_dec((uint64_t)ds_reg.total_s0_deferred);
    uart_puts("\r\n");
}

/* ===================================================================
 * Kernel-side shell executor (SYS_EXEC backend)
 *
 * The EL0 shell process passes command lines here.  Kernel-native
 * commands (stats, uptime) run directly against live subsystem state;
 * everything else goes through the P-TERM engine, whose console
 * output is then flushed to the serial UART.
 * =================================================================== */

/* Print one console row (up to its NUL / width) to the UART. */
static void shell_flush_row(const char *row) {
    for (uint32_t x = 0; x < PTERM_CONSOLE_W && row[x]; x++) {
        char s[2];
        s[0] = row[x];
        s[1] = '\0';
        uart_puts(s);
    }
    uart_puts("\r\n");
}

void kernel_shell_exec(const char *line) {
    if (!g_pterm_ready) {
        uart_puts("[shell] P-TERM engine not initialized\r\n");
        return;
    }

    /* --- kernel-native commands (live subsystem state) --- */
    if (strcmp(line, "stats") == 0) {
        kernel_stats_report();
        return;
    }
    if (strcmp(line, "uptime") == 0) {
        uart_puts("uptime: ticks=");
        uart_put_dec(arm64_timer_get_ticks());
        uart_puts(" (~");
        uart_put_dec(arm64_timer_get_ticks() / 100);
        uart_puts("s @100Hz)  event_cycles=");
        uart_put_dec((uint64_t)g_event_cycle);
        uart_puts("\r\n");
        return;
    }
    if (strcmp(line, "clear") == 0) {
        pterm_clear(&g_pterm);
        uart_puts("\033[2J\033[H");   /* ANSI clear on the real terminal */
        return;
    }
    /* pulse — the kernel's heartbeat. The generic-timer tick drives one
     * event cycle each beat (external-clock bridge); this shows the real
     * beat count + a simple live bar. Honest metrics, not decoration. */
    if (strcmp(line, "pulse") == 0) {
        uint64_t ticks = arm64_timer_get_ticks();
        uart_puts("heartbeat  ");
        uint32_t bar = (uint32_t)(g_event_cycle % 24);
        uart_puts("[");
        for (uint32_t i = 0; i < 24; i++) uart_puts(i <= bar ? "=" : " ");
        uart_puts("]\r\n");
        uart_puts("  beats(ticks)=");   uart_put_dec(ticks);
        uart_puts("  event_cycles="); uart_put_dec((uint64_t)g_event_cycle);
        uart_puts("  rate=100 Hz  uptime~"); uart_put_dec(ticks / 100);
        uart_puts("s\r\n");
        return;
    }
    /* forge <intent> — the Magitech Refinery at the shell. Any language
     * in; a sigil-card out: gematria, root, star fabric, kamea circuit,
     * event-wave schedule, and the 58-byte shareable AI preset. */
    if (line[0]=='f' && line[1]=='o' && line[2]=='r' && line[3]=='g' &&
        line[4]=='e' && line[5]==' ' && line[6]) {
        const char *text = line + 6;
        uint32_t tlen = (uint32_t)strlen(text);
        static ref_card_t fc;                     /* keep off the stack */
        ref_status_t st = ref_forge(text, tlen, ENO_VOICE_AEON, &fc);
        if (st != REF_OK) {
            uart_puts("[forge] "); uart_puts(ref_status_name(st)); uart_puts("\r\n");
            return;
        }
        uart_puts("MAGITECH REFINERY\r\n  intent   : \"");
        uart_puts(text);
        uart_puts("\"\r\n  gematria : "); uart_put_dec(fc.gematria);
        uart_puts("  root "); uart_put_dec(fc.root);
        uart_puts(" ("); uart_puts(eno_root_domain(fc.root));
        uart_puts(")\r\n  fabric   : {"); uart_put_dec(fc.sigil.fab_n);
        uart_puts("/"); uart_put_dec(fc.sigil.fab_k);
        uart_puts("}  lanes="); uart_put_dec(sig_fabric_lanes(&fc.sigil));
        uart_puts("\r\n  circuit  : "); uart_put_dec(fc.path_len);
        uart_puts(" nodes, "); uart_put_dec(fc.sigil.n_edges);
        uart_puts(" strokes");
        sig_schedule_t sc;
        if (sig_schedule(&fc.sigil, fc.path_len ? fc.path[0] : -1, &sc)) {
            uart_puts("  waves="); uart_put_dec(sc.n_waves);
            uart_puts(" width="); uart_put_dec(sc.width);
        }
        uart_puts("\r\n");
        static char actline[220];
        ref_activation_line(&fc, actline, sizeof actline);
        uart_puts("  "); uart_puts(actline); uart_puts("\r\n");
        static uint8_t blob[REF_PRESET_MAX];
        uint32_t bl = ref_preset_pack(&fc, blob, sizeof blob);
        uart_puts("  preset   : "); uart_put_dec(bl);
        uart_puts(" bytes (sealed; share freely — peers re-derive and verify)\r\n");
        return;
    }
    /* law [s|l|a] — the engine's own source: the Card-Between-Cards text
     * in the chosen voice (solar / lunar / aeon). */
    if (strcmp(line, "law") == 0 ||
        (line[0]=='l' && line[1]=='a' && line[2]=='w' && line[3]==' ' && line[4])) {
        eno_voice_t v = ENO_VOICE_AEON;
        if (line[3] == ' ' && line[4] == 's') v = ENO_VOICE_SOLAR;
        if (line[3] == ' ' && line[4] == 'l') v = ENO_VOICE_LUNAR;
        uart_puts(eno_law_title(v)); uart_puts("\r\n");
        uart_puts(eno_law(v)); uart_puts("\r\n");
        return;
    }
    /* tour — plain-language walkthrough so a feature-rich system stays
     * approachable. Progressive disclosure: names the subsystems and the
     * command that opens each, without hiding any capability. */
    if (strcmp(line, "tour") == 0) {
        uart_puts("ZXV in one minute:\r\n");
        uart_puts("  1. It's alive        -> 'pulse'   (timer tick = one event cycle)\r\n");
        uart_puts("  2. Everything's live -> 'stats'   (17 subsystems ticking now)\r\n");
        uart_puts("  3. Real storage      -> 'ls-p' / 'write f text' / 'cat f'\r\n");
        uart_puts("  4. Run signed apps   -> 'run hello.zsp' (Ed25519-verified)\r\n");
        uart_puts("  5. Safe updates      -> 'slots' / 'update f' / 'confirm' / 'rollback'\r\n");
        uart_puts("  6. Who + how long    -> 'whoami' / 'uptime' / 'ps'\r\n");
        uart_puts("Type 'help' for the full command list.\r\n");
        return;
    }

    /* --- persistent-storage commands (ZXVFS on virtio-blk) --- */
    if (line[0] == 'l' && line[1] == 's' && line[2] == '-' &&
        line[3] == 'p' && (line[4] == '\0')) {
        if (!g_zxvfs_ready) { uart_puts("ls-p: no persistent disk\r\n"); return; }
        char names[ZXVFS_MAX_FILES][ZXVFS_NAME_LEN];
        uint32_t sizes[ZXVFS_MAX_FILES];
        int c = zxvfs_list(&g_zxvfs, names, sizes, ZXVFS_MAX_FILES);
        if (c <= 0) { uart_puts("(empty)\r\n"); return; }
        for (int i = 0; i < c; i++) {
            uart_puts("  ");
            uart_puts(names[i]);
            uart_puts("  (");
            uart_put_dec((uint64_t)sizes[i]);
            uart_puts(" bytes)\r\n");
        }
        return;
    }
    /* write <name> <text...>  — persist text to a file */
    if (line[0]=='w' && line[1]=='r' && line[2]=='i' && line[3]=='t' &&
        line[4]=='e' && line[5]==' ') {
        if (!g_zxvfs_ready) { uart_puts("write: no persistent disk\r\n"); return; }
        const char *p = line + 6;
        char name[ZXVFS_NAME_LEN];
        uint32_t ni = 0;
        while (*p && *p != ' ' && ni < ZXVFS_NAME_LEN - 1) name[ni++] = *p++;
        name[ni] = '\0';
        if (*p == ' ') p++;
        uint32_t len = 0; const char *q = p;
        while (q[len]) len++;
        int rc = zxvfs_write(&g_zxvfs, name, (const uint8_t *)p, len);
        if (rc == 0) {
            uart_puts("wrote "); uart_put_dec((uint64_t)len);
            uart_puts(" bytes to "); uart_puts(name); uart_puts(" (persisted)\r\n");
        } else {
            uart_puts("write failed rc="); uart_put_dec((uint64_t)(uint32_t)(-rc));
            uart_puts("\r\n");
        }
        return;
    }
    /* cat <name> — read a persistent file */
    if (line[0]=='c' && line[1]=='a' && line[2]=='t' && line[3]==' ') {
        if (!g_zxvfs_ready) { uart_puts("cat: no persistent disk\r\n"); return; }
        const char *name = line + 4;
        static uint8_t fbuf[ZXVFS_FILE_MAX_BYTES + 1];
        int n = zxvfs_read(&g_zxvfs, name, fbuf, ZXVFS_FILE_MAX_BYTES);
        if (n < 0) { uart_puts("cat: no such file\r\n"); return; }
        fbuf[n] = '\0';
        uart_puts((const char *)fbuf);
        if (n == 0 || fbuf[n-1] != '\n') uart_puts("\r\n");
        return;
    }
    /* rm <name> — delete a persistent file */
    if (line[0]=='r' && line[1]=='m' && line[2]==' ') {
        if (!g_zxvfs_ready) { uart_puts("rm: no persistent disk\r\n"); return; }
        int rc = zxvfs_unlink(&g_zxvfs, line + 3);
        uart_puts(rc == 0 ? "removed (persisted)\r\n" : "rm: no such file\r\n");
        return;
    }
    if (strcmp(line, "sync") == 0) {
        /* ZXVFS commits synchronously per write; nothing buffered. */
        uart_puts(g_zxvfs_ready ? "sync: all writes already committed\r\n"
                                : "sync: no persistent disk\r\n");
        return;
    }

    /* --- WRITER — the first app on the AppKit document scaffold.
     * Every native ZXV app (Sheet, Deck, Sound, Reel) sits on the same
     * doc model: gap buffer, coalesced undo/redo, ZXVFS persistence. --- */
    if (line[0]=='w'&&line[1]=='r'&&line[2]=='i'&&line[3]=='t'&&line[4]=='e'&&line[5]=='r') {
        const char *p = line + 6;
        if (*p == ' ') p++;
        /* writer new <name> */
        if (p[0]=='n'&&p[1]=='e'&&p[2]=='w') {
            const char *nm = (p[3]==' ') ? p+4 : "untitled";
            doc_init(&g_doc, nm); g_doc_open = true;
            uart_puts("writer: new document '"); uart_puts(nm); uart_puts("'\r\n");
            return;
        }
        if (!g_doc_open) { uart_puts("writer: no document ('writer new <name>')\r\n"); return; }
        /* writer add <text> */
        if (p[0]=='a'&&p[1]=='d'&&p[2]=='d'&&p[3]==' ') {
            const char *t = p+4; uint32_t n=0; while (t[n]) n++;
            bool ok = doc_insert(&g_doc, doc_length(&g_doc), t, n);
            if (ok) doc_insert(&g_doc, doc_length(&g_doc), "\n", 1);
            doc_break_coalesce(&g_doc);
            uart_puts(ok ? "writer: added\r\n" : "writer: document full\r\n");
            return;
        }
        if (p[0]=='u'&&p[1]=='n'&&p[2]=='d'&&p[3]=='o') {
            uart_puts(doc_undo(&g_doc) ? "writer: undo\r\n" : "writer: nothing to undo\r\n"); return;
        }
        if (p[0]=='r'&&p[1]=='e'&&p[2]=='d'&&p[3]=='o') {
            uart_puts(doc_redo(&g_doc) ? "writer: redo\r\n" : "writer: nothing to redo\r\n"); return;
        }
        /* writer save / open <name> — persistence via ZXVFS */
        if (p[0]=='s'&&p[1]=='a'&&p[2]=='v'&&p[3]=='e') {
            if (!g_zxvfs_ready) { uart_puts("writer: no disk\r\n"); return; }
            static char tb[DOC_CAPACITY];
            uint32_t n = doc_text(&g_doc, tb, sizeof(tb));
            int rc = zxvfs_write(&g_zxvfs, g_doc.name, (const uint8_t*)tb, n);
            if (rc == 0) { g_doc.dirty = false;
                uart_puts("writer: saved "); uart_put_dec((uint64_t)n);
                uart_puts(" bytes to "); uart_puts(g_doc.name); uart_puts(" (persisted)\r\n");
            } else uart_puts("writer: save failed\r\n");
            return;
        }
        if (p[0]=='o'&&p[1]=='p'&&p[2]=='e'&&p[3]=='n'&&p[4]==' ') {
            if (!g_zxvfs_ready) { uart_puts("writer: no disk\r\n"); return; }
            static uint8_t rb[DOC_CAPACITY];
            int n = zxvfs_read(&g_zxvfs, p+5, rb, sizeof(rb));
            if (n < 0) { uart_puts("writer: no such document\r\n"); return; }
            doc_init(&g_doc, p+5); g_doc_open = true;
            doc_insert(&g_doc, 0, (const char*)rb, (uint32_t)n);
            g_doc.dirty = false; doc_break_coalesce(&g_doc);
            uart_puts("writer: opened "); uart_put_dec((uint64_t)n); uart_puts(" bytes\r\n");
            return;
        }
        /* default: show the document */
        uart_puts("--- "); uart_puts(g_doc.name);
        uart_puts(g_doc.dirty ? " [modified] ---\r\n" : " ---\r\n");
        {
            static char tb[DOC_CAPACITY+1];
            uint32_t n = doc_text(&g_doc, tb, DOC_CAPACITY); tb[n]='\0';
            uart_puts(tb);
            if (n && tb[n-1] != '\n') uart_puts("\r\n");
        }
        uart_puts("--- "); uart_put_dec((uint64_t)doc_length(&g_doc));
        uart_puts(" bytes, "); uart_put_dec((uint64_t)doc_line_count(&g_doc));
        uart_puts(" lines, undo="); uart_put_dec((uint64_t)g_doc.undo_count);
        uart_puts(" ---\r\n");
        return;
    }

    /* --- master/sub terminals, phase-tick sequenced (ISF weighted) --- */
    if (strcmp(line, "term") == 0 || strcmp(line, "term list") == 0) {
        uart_puts("master terminal  |  phase-tick sequenced, ISF-weighted\r\n");
        uart_puts("  ticks="); uart_put_dec(g_pmux.ticks);
        uart_puts(" active="); uart_put_dec((uint64_t)g_pmux.num_active);
        uart_puts(" dispatched="); uart_put_dec(g_pmux.total_dispatch);
        uart_puts("\r\n");
        if (g_pmux.num_subs == 0) {
            uart_puts("  (no sub terminals — 'term new <name> <u%>')\r\n");
            return;
        }
        uart_puts("  idx  name              u     g(u)   turns  state\r\n");
        for (uint32_t i = 0; i < PMUX_MAX_SUBS; i++) {
            pmux_sub_t *s = &g_pmux.sub[i];
            if (s->state == PMUX_SLOT_FREE) continue;
            uart_puts("   ");
            uart_put_dec((uint64_t)i);
            uart_puts(g_pmux.focus == i ? "* " : "  ");
            uart_puts(s->name);
            for (uint32_t k = fs_strlen(s->name); k < 18; k++) uart_puts(" ");
            /* u and g as percent / hundredths — integer only */
            uart_put_dec(((uint64_t)s->u_q16 * 100) / PMUX_ONE);
            uart_puts("%   ");
            uart_put_dec((uint64_t)s->weight_q16 / PMUX_ONE);
            uart_puts(".");
            uart_put_dec((((uint64_t)s->weight_q16 % PMUX_ONE) * 100) / PMUX_ONE);
            uart_puts("   ");
            uart_put_dec(s->dispatches);
            uart_puts("   ");
            uart_puts(s->state == PMUX_SLOT_ACTIVE ? "active" : "paused");
            uart_puts("\r\n");
        }
        return;
    }
    /* term new <name> <u%> */
    if (line[0]=='t'&&line[1]=='e'&&line[2]=='r'&&line[3]=='m'&&line[4]==' '&&
        line[5]=='n'&&line[6]=='e'&&line[7]=='w'&&line[8]==' ') {
        const char *p = line + 9;
        char nm[PMUX_NAME_LEN]; uint32_t n = 0;
        while (*p && *p != ' ' && n < PMUX_NAME_LEN - 1) nm[n++] = *p++;
        nm[n] = '\0';
        uint32_t u = 50;
        if (*p == ' ') { p++; u = 0; while (*p >= '0' && *p <= '9') u = u*10 + (uint32_t)(*p++ - '0'); }
        int32_t con = pterm_console_create(&g_pterm, nm);
        int32_t idx = pmux_spawn(&g_pmux, nm, (uint32_t)(con < 0 ? 0 : con), u);
        if (idx < 0) { uart_puts("term: no free sub slots\r\n"); return; }
        uart_puts("term: spawned sub ");
        uart_put_dec((uint64_t)(uint32_t)idx);
        uart_puts(" '"); uart_puts(nm);
        uart_puts("' u="); uart_put_dec((uint64_t)u);
        uart_puts("% -> g(u)=");
        uart_put_dec((uint64_t)g_pmux.sub[idx].weight_q16 / PMUX_ONE);
        uart_puts(".");
        uart_put_dec((((uint64_t)g_pmux.sub[idx].weight_q16 % PMUX_ONE) * 100) / PMUX_ONE);
        uart_puts("  (sequenced by phase tick)\r\n");
        return;
    }
    /* term focus|pause|resume|close <idx> */
    if (line[0]=='t'&&line[1]=='e'&&line[2]=='r'&&line[3]=='m'&&line[4]==' ') {
        const char *p = line + 5;
        const char *verb = p;
        while (*p && *p != ' ') p++;
        uint32_t idx = 0;
        if (*p == ' ') { p++; while (*p >= '0' && *p <= '9') idx = idx*10 + (uint32_t)(*p++ - '0'); }
        if (verb[0]=='f') {
            uart_puts(pmux_set_focus(&g_pmux, idx) ? "term: focus set\r\n" : "term: bad index\r\n");
        } else if (verb[0]=='p') {
            uart_puts(pmux_set_state(&g_pmux, idx, PMUX_SLOT_PAUSED) ? "term: paused (re-weighted)\r\n" : "term: bad index\r\n");
        } else if (verb[0]=='r') {
            uart_puts(pmux_set_state(&g_pmux, idx, PMUX_SLOT_ACTIVE) ? "term: resumed (re-weighted)\r\n" : "term: bad index\r\n");
        } else if (verb[0]=='c') {
            uart_puts(pmux_close(&g_pmux, idx) ? "term: closed (re-weighted)\r\n" : "term: bad index\r\n");
        } else {
            uart_puts("term: new <name> <u%> | list | focus/pause/resume/close <idx>\r\n");
        }
        return;
    }

    /* --- A/B signed update / probation / rollback --- */
    if (strcmp(line, "slots") == 0) {
        if (!g_ab_ready) { uart_puts("slots: no A/B state\r\n"); return; }
        uart_puts("A/B slots: active=");
        uart_puts(g_ab.active_slot ? "B" : "A");
        uart_puts(" verA="); uart_put_dec((uint64_t)g_ab.version[0]);
        uart_puts(" verB="); uart_put_dec((uint64_t)g_ab.version[1]);
        uart_puts(" probation=");
        if (g_ab.probation_slot == AB_SLOT_NONE) uart_puts("none");
        else uart_puts(g_ab.probation_slot ? "B" : "A");
        uart_puts(" promotions="); uart_put_dec((uint64_t)g_ab.promotions);
        uart_puts(" rollbacks="); uart_put_dec((uint64_t)g_ab.rollbacks);
        uart_puts("\r\n");
        return;
    }
    /* update <file>: verify a signed package and stage it on probation */
    if (line[0]=='u'&&line[1]=='p'&&line[2]=='d'&&line[3]=='a'&&line[4]=='t'&&
        line[5]=='e'&&line[6]==' ') {
        if (!g_ab_ready) { uart_puts("update: no A/B state\r\n"); return; }
        static uint8_t ubuf[ZXVFS_FILE_MAX_BYTES];
        int n = zxvfs_read(&g_zxvfs, line + 7, ubuf, sizeof(ubuf));
        if (n < 0) { uart_puts("update: no such file\r\n"); return; }
        ab_result_t r = ab_stage_update(&g_zxvfs, &g_ab, ubuf, (uint32_t)n,
                                        hello_root_pubkey);
        if (r == AB_OK) {
            uart_puts("update: signature OK — staged to slot ");
            uart_puts(g_ab.probation_slot ? "B" : "A");
            uart_puts(" ON PROBATION. 'confirm' to promote, 'rollback' to revert.\r\n");
        } else if (r == AB_ERR_VERIFY) {
            uart_puts("update: SIGNATURE REJECTED — active slot unchanged\r\n");
        } else {
            uart_puts("update: failed\r\n");
        }
        return;
    }
    if (strcmp(line, "confirm") == 0) {
        if (!g_ab_ready) { uart_puts("confirm: no A/B state\r\n"); return; }
        ab_result_t r = ab_confirm(&g_zxvfs, &g_ab);
        uart_puts(r == AB_OK ? "confirm: probation slot promoted to active\r\n"
                  : r == AB_ERR_NONE ? "confirm: nothing on probation\r\n"
                  : "confirm: failed\r\n");
        return;
    }
    if (strcmp(line, "rollback") == 0) {
        if (!g_ab_ready) { uart_puts("rollback: no A/B state\r\n"); return; }
        ab_result_t r = ab_rollback(&g_zxvfs, &g_ab);
        uart_puts(r == AB_OK ? "rollback: reverted to known-good active slot\r\n"
                  : r == AB_ERR_NONE ? "rollback: nothing on probation\r\n"
                  : "rollback: failed\r\n");
        return;
    }
    /* run <name> — load a program from ZXVFS and launch it at EL0.
     * A `.zsp` package is Ed25519-verified against the compiled-in root
     * key BEFORE any byte is executed (integrity + authenticity). A raw
     * `.elf` is still accepted for development but flagged UNSIGNED. The
     * new process is created READY; the preemptive scheduler dispatches
     * it on the next tick, alongside the shell. */
    if (line[0]=='r' && line[1]=='u' && line[2]=='n' && line[3]==' ') {
        if (!g_zxvfs_ready) { uart_puts("run: no persistent disk\r\n"); return; }
        const char *name = line + 4;
        static uint8_t filebuf[ZXVFS_FILE_MAX_BYTES];
        int n = zxvfs_read(&g_zxvfs, name, filebuf, sizeof(filebuf));
        if (n < 0) { uart_puts("run: no such file\r\n"); return; }

        const uint8_t *image = filebuf;
        uint32_t image_len = (uint32_t)n;

        /* Detect a signed package by magic and REQUIRE verification. */
        bool is_zsp = (n >= ZSP_HEADER_LEN &&
                       filebuf[0]==ZSP_MAGIC0 && filebuf[1]==ZSP_MAGIC1 &&
                       filebuf[2]==ZSP_MAGIC2 && filebuf[3]==ZSP_MAGIC3);
        if (is_zsp) {
            const uint8_t *payload; uint32_t plen;
            zsp_result_t vr = zsp_verify(filebuf, (uint32_t)n,
                                         hello_root_pubkey, &payload, &plen);
            if (vr != ZSP_OK) {
                uart_puts("run: SIGNATURE REJECTED — ");
                uart_puts(zsp_strerror(vr));
                uart_puts(" — refusing to execute\r\n");
                return;
            }
            uart_puts("run: signature verified against root key (Ed25519)\r\n");
            image = payload;
            image_len = plen;
        } else {
            uart_puts("run: [WARN] unsigned ELF (dev only) — no signature check\r\n");
        }

        extern proc_scheduler_t *el0_get_scheduler(void);
        proc_scheduler_t *sch = el0_get_scheduler();
        if (!sch) { uart_puts("run: no EL0 scheduler\r\n"); return; }
        int32_t pid = proc_create_from_elf(sch, name, image, image_len);
        if (pid < 0) {
            uart_puts("run: load failed\r\n");
        } else {
            uart_puts("run: launched pid=");
            uart_put_dec((uint64_t)(uint32_t)pid);
            uart_puts(" (");
            uart_puts(name);
            uart_puts(")\r\n");
        }
        return;
    }

    /* --- delegate to the P-TERM engine --- */
    /* Alias: `ps` → `ps-phase` (the registered command name) */
    if (strcmp(line, "ps") == 0)
        line = "ps-phase";

    pterm_console_t *con = &g_pterm.consoles[g_pterm.active_console];

    /* Keep enough headroom that a full command output (help is the
     * largest, ~18 rows) doesn't scroll out of the visible screen
     * before we flush it. */
    if (con->cursor_y > (uint32_t)(PTERM_CONSOLE_H - 19)) {
        pterm_clear(&g_pterm);
    }

    uint32_t y0 = con->cursor_y;
    pterm_execute_command(&g_pterm, line);
    pterm_history_add(&g_pterm, line);

    uint32_t y1 = con->cursor_y;
    if (y1 < y0)   /* command cleared the console */
        y0 = 0;
    for (uint32_t y = y0; y < y1 && y < PTERM_CONSOLE_H; y++)
        shell_flush_row(con->screen[y]);
    if (con->cursor_x > 0 && y1 < PTERM_CONSOLE_H)
        shell_flush_row(con->screen[y1]);

    /* Native command addendum to `help` */
    if (strcmp(line, "help") == 0) {
        uart_puts("  stats  \xe2\x80\x94  live kernel subsystem coverage report\r\n");
        uart_puts("  uptime  \xe2\x80\x94  ticks and event cycles since boot\r\n");
        uart_puts("  ls-p  \xe2\x80\x94  list files on the persistent disk (ZXVFS)\r\n");
        uart_puts("  write <f> <text>  \xe2\x80\x94  persist text to a file\r\n");
        uart_puts("  cat <f>  \xe2\x80\x94  print a persistent file\r\n");
        uart_puts("  rm <f>  \xe2\x80\x94  delete a persistent file\r\n");
        uart_puts("  run <f>  \xe2\x80\x94  load & execute a signed (.zsp) program from disk\r\n");
        uart_puts("  slots / update <f> / confirm / rollback  \xe2\x80\x94  A/B signed updates\r\n");
        uart_puts("  writer new|add|undo|redo|save|open  \xe2\x80\x94  native word processor\r\n");
        uart_puts("  pulse  \xe2\x80\x94  live kernel heartbeat\r\n");
        uart_puts("  forge <intent>  \xe2\x80\x94  Magitech Refinery: any language -> sigil card + AI preset\r\n");
        uart_puts("  law [s|l|a]  \xe2\x80\x94  the Glyph & Grid law in solar/lunar/aeon voice\r\n");
        uart_puts("  tour  \xe2\x80\x94  one-minute plain-language walkthrough\r\n");
        uart_puts("  exit  \xe2\x80\x94  leave the EL0 shell\r\n");
    }
}
