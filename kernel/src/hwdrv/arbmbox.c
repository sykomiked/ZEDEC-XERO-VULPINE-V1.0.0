/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* arbmbox.c — the arbiter mailbox as a PCI function (src/arbiter/arb_proto.h
 * has the protocol; this file finds the window and opens the session).
 *
 * MATCH. The interface is this project's own (OS_HARDWARE_INTERFACE.md section
 * 3 row 4), so it has no vendor ID of its own to match on. It presents as
 * class 0x08 (base system peripheral), subclass 0x80 (other), with programming
 * interface 0x5A ('Z'), a value no other function of that class is known to
 * use, and the bind then insists on the "ZXVARBMB" header in BAR 0. A function
 * that matches the class but not the header is reported FAILED, not adopted.
 *
 * BIND. BAR 0 must be a memory BAR of at least ARB_WINDOW_MIN bytes. The
 * driver validates the header, sends HELLO, and waits up to 100 ms for the
 * arbiter's nonce. Attestation is not done here: it needs the measurement of
 * the running image, which the boot path supplies through hw_arbiter().
 *   ONLINE  header valid and the arbiter answered.
 *   HELD    header valid but no answer yet (the arbiter firmware is still
 *           starting): the session stays open for later polls. A second
 *           arbiter is HELD as a contradiction: a machine has one.
 *   FAILED  no memory BAR, too small, or no valid header.
 */
#include "hwdrv.h"
#include "../arbiter/arb_host.h"

static arb_host_t g_arb;
static int g_arb_bound;

arb_host_t *hw_arbiter(void)
{
    return g_arb_bound ? &g_arb : 0;
}

void hw_arbiter_reset(void)
{
    hw_memset(&g_arb, 0, sizeof g_arb);
    g_arb_bound = 0;
}

static const char *header_reason(int r)
{
    switch (r) {
    case ARB_E_MAGIC:
        return "no ZXVARBMB header in BAR 0";
    case ARB_E_VERSION:
        return "unknown mailbox version";
    case ARB_E_RING:
        return "bad ring geometry";
    case ARB_E_FB:
        return "framebuffers overrun the BAR";
    default:
        return "BAR 0 smaller than the mailbox window";
    }
}

hw_verdict_t arbmbox_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r,
                          hw_line_t *note)
{
    (void) r;
    if (g_arb_bound) {
        hw_line_s(note, "a second arbiter; the first stays in charge");
        return HW_B;
    }
    if (!d->bar[0] || (d->bar_flags[0] & HW_BAR_IO) || d->bar_size[0] < ARB_WINDOW_MIN) {
        hw_line_s(note, "BAR 0 is not a memory window of at least 12 KiB");
        return HW_F;
    }
    hw_pci_enable(p, d);
    volatile uint8_t *w = (volatile uint8_t *) hw_pci_map_bar(p, d, 0);
    if (!w) {
        hw_line_s(note, "BAR 0 could not be mapped");
        return HW_F;
    }
    int hr = arb_host_init(&g_arb, w, d->bar_size[0]);
    if (hr != 0) {
        hw_line_s(note, header_reason(hr));
        return HW_F;
    }
    g_arb_bound = 1;
    if (arb_host_hello(&g_arb) != 0 || arb_host_state_request(&g_arb) != 0) {
        hw_line_s(note, "request ring full at bind");
        return HW_N;
    }
    for (uint32_t i = 0; i < 100 && g_arb.state == ARB_H_HELLO; i++) {
        p->delay_us(p->ctx, 1000);
        arb_host_poll(&g_arb);
    }
    if (g_arb.state == ARB_H_BROKEN) {
        hw_line_s(note, "the arbiter broke its reply ring");
        return HW_F;
    }
    if (g_arb.state != ARB_H_READY) {
        hw_line_s(note, "no answer to HELLO yet");
        return HW_N;
    }
    /* the state request was queued behind HELLO; give it the same budget */
    for (uint32_t i = 0; i < 100 && !(g_arb.have_kill && g_arb.have_interlock); i++) {
        p->delay_us(p->ctx, 1000);
        arb_host_poll(&g_arb);
    }
    hw_line_s(note, "mailbox v");
    hw_line_u(note, ARB_VERSION);
    hw_line_s(note, ", ring ");
    hw_line_u(note, g_arb.cap);
    uint32_t fw, fh, fs;
    if (arb_host_fb(&g_arb, 0, &fw, &fh, &fs)) {
        hw_line_s(note, ", ");
        hw_line_u(note, arb_rd32(w, ARB_OFF_FB_COUNT));
        hw_line_s(note, " panels ");
        hw_line_u(note, fw);
        hw_line_c(note, 'x');
        hw_line_u(note, fh);
    }
    hw_line_s(note, ", switches ");
    hw_line_x(note, g_arb.kill_on, 2);
    hw_line_c(note, '/');
    hw_line_x(note, g_arb.kill_present, 2);
    if (g_arb.have_interlock && !g_arb.interlock_permitted) hw_line_s(note, ", start held");
    if (g_arb.tampered) hw_line_s(note, ", TAMPER LATCHED");
    return HW_T;
}
