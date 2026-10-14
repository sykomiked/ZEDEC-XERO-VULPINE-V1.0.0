/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* usbkbd.c — USB HID boot keyboard (interface class 3, subclass 1,
 * protocol 1), on the USB core in xhci.c.
 *
 * The keyboard is switched to the boot protocol (fixed 8-byte reports:
 * modifiers, reserved, six usage codes) with idle 0 (report on change only)
 * and its LEDs off.
 * One transfer stays queued on the interrupt IN endpoint; poll() reaps it,
 * diffs the report against the previous one into press/release events, and
 * queues the next. Usage page 7 codes map to ASCII for letters, digits and
 * common punctuation, shifted by either shift key. */
#include "usb.h"

#define KQ      32 /* pending key events: one report makes at most 8 + 6 + 6 */
#define REP_MAX 64

typedef struct kbd {
    usb_dev_t *d;
    usb_ep_t *ep;
    uint8_t *rep; /* DMA report buffer */
    uint64_t rep_phys;
    uint32_t rep_len;
    uint8_t prev[8];
    hw_key_t q[KQ];
    uint32_t qh, qt;
    uint32_t errors;
    uint8_t stopped;
    hw_input_t in;
} kbd_t;

static uint32_t g_kbd_n; /* usbkbd0, usbkbd1, ... across controllers */

/* Usage 0x04.. : unshifted and shifted ASCII (0 = no ASCII). */
static const char k_plain[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\\0;'`,./";
static const char k_shift[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|\0:\"~<>?";

static uint8_t to_ascii(uint8_t usage, uint8_t mods)
{
    if (usage < 0x04 || usage > 0x38) return 0;
    const char *t = (mods & 0x22) ? k_shift : k_plain; /* left or right shift */
    return (uint8_t) t[usage - 0x04];
}

static void push(kbd_t *k, uint8_t usage, uint8_t mods, uint8_t pressed)
{
    hw_key_t *e = &k->q[k->qt];
    e->usage = usage;
    e->ascii = pressed ? to_ascii(usage, mods) : 0;
    e->mods = mods;
    e->pressed = pressed;
    k->qt = (k->qt + 1) % KQ;
}

static int has(const uint8_t *keys, uint8_t u)
{
    for (int i = 0; i < 6; i++)
        if (keys[i] == u) return 1;
    return 0;
}

/* Diff one boot report against the previous: releases first, then presses;
 * modifier keys report as usages 0xE0..0xE7. */
static void report(kbd_t *k, const uint8_t *r)
{
    uint8_t now[8];
    hw_memcpy(now, r, 8);
    for (int i = 2; i < 8; i++)
        if (now[i] == 0x01) return; /* rollover error: too many keys, keep the old state */
    uint8_t mods = now[0], chg = (uint8_t) (mods ^ k->prev[0]);
    for (int b = 0; b < 8; b++)
        if (chg & (1u << b)) push(k, (uint8_t) (0xE0 + b), mods, (uint8_t) ((mods >> b) & 1));
    for (int i = 2; i < 8; i++)
        if (k->prev[i] > 3 && !has(now + 2, k->prev[i])) push(k, k->prev[i], mods, 0);
    for (int i = 2; i < 8; i++)
        if (now[i] > 3 && !has(k->prev + 2, now[i])) push(k, now[i], mods, 1);
    hw_memcpy(k->prev, now, 8);
}

static void refill(kbd_t *k)
{
    int rc = usb_reap(k->d, k->ep);
    if (rc == USB_EPENDING) return;
    if (rc >= 8) {
        report(k, k->rep);
    } else if (rc < 0) {
        /* a stall or error halts the endpoint; a dead controller ends it */
        if (rc == USB_EDEAD || ++k->errors > 8 || usb_clear_halt(k->d, k->ep) != 0) {
            k->stopped = 1;
            return;
        }
    }
    if (usb_submit(k->d, k->ep, k->rep_phys, k->rep_len) != 0) k->stopped = 1;
}

static int kbd_poll(hw_input_t *in, hw_key_t *ev)
{
    kbd_t *k = (kbd_t *) in->drv;
    if (k->qh == k->qt && !k->stopped) refill(k);
    if (k->qh == k->qt) return 0;
    *ev = k->q[k->qh];
    k->qh = (k->qh + 1) % KQ;
    return 1;
}

int usbkbd_attach(usb_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    const hw_plat_t *p = d->p;
    usb_ep_t *ep = usb_find_ep(d, 3, 0x80);
    if (!ep) {
        hw_line_s(note, "keyboard without interrupt IN endpoint");
        return -1;
    }
    kbd_t *k = (kbd_t *) p->dma_alloc(p->ctx, sizeof(kbd_t), 64, 0);
    uint64_t rp = 0;
    uint8_t *rep = (uint8_t *) p->dma_alloc(p->ctx, REP_MAX, 64, &rp);
    if (!k || !rep) {
        hw_line_s(note, "keyboard: out of DMA memory");
        return -1;
    }
    k->d = d;
    k->ep = ep;
    k->rep = rep;
    k->rep_phys = rp;
    k->rep_len = ep->mps > REP_MAX ? REP_MAX : (ep->mps < 8 ? 8 : ep->mps);
    /* SET_PROTOCOL boot; SET_IDLE 0. Some keyboards stall SET_IDLE: fine. */
    if (usb_control(d, 0x21, 0x0B, 0, d->intf, 0, 0) < 0) {
        hw_line_s(note, "keyboard refused the boot protocol");
        return -1;
    }
    usb_control(d, 0x21, 0x0A, 0, d->intf, 0, 0);
    uint8_t leds = 0; /* SET_REPORT(output): LEDs off, whatever the firmware left lit */
    usb_control(d, 0x21, 0x09, 0x0200, d->intf, 1, &leds);
    if (usb_submit(d, ep, rp, k->rep_len) != 0) {
        hw_line_s(note, "keyboard: interrupt transfer refused");
        return -1;
    }
    hw_input_t *in = &k->in;
    hw_memcpy(in->name, "usbkbd", 6);
    in->name[6] = (char) ('0' + g_kbd_n % 10);
    in->name[7] = 0;
    in->drv = k;
    in->poll = kbd_poll;
    hw_line_s(note, in->name);
    if (hw_reg_input(r, in) != 0) {
        hw_line_s(note, " (registry full)");
        return 0;
    }
    g_kbd_n++;
    return 1;
}
