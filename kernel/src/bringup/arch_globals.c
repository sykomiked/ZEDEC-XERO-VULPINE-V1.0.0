/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* arch_globals.c — shared bring-up symbols for the NON-arm64 architectures.
 *
 * The arm64 kernel_main (kernel_main_arm64.c) defines the kernel-wide subsystem
 * global instances (vfs/net/router/sched) and the tree resolves fb_puts through
 * its own display path. The x86_64 / riscv / riscv32 / arm32 kernel_mains are
 * minimal bring-up stubs that do not yet instantiate these, so portable modules
 * (pterm_commands, synthesis, …) fail to link on those arches.
 *
 * This file provides exactly those symbols so the bring-up kernels LINK and BOOT
 * a core profile. It is linked into every arch EXCEPT arm64 (which already
 * defines them — linking this there would be a duplicate-symbol error). fb_puts
 * is headless here: on a bring-up kernel the "framebuffer" is the serial console. */
#include "vfs/vfs.h"
#include "net/net.h"
#include "net/m5route.h"
#include "sched/sched.h"

vfs_state_t vfs;
net_state_t net;
m5_router_t router;
scheduler_t  sched;

extern void uart_puts(const char *s);

/* Headless framebuffer text output = serial. The graphical framebuffer path is
 * arch-layer work (ramfb/vbe) brought up per-arch later. */
void fb_puts(const char *s) { uart_puts(s); }

/* libgcc's integer-divide helpers call raise() on divide-by-zero. A freestanding
 * kernel has no signals; provide a no-op so the runtime library links. */
int raise(int sig) { (void)sig; return 0; }
