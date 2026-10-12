/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* usb.h — the USB core that xhci.c offers its class drivers (usbkbd.c,
 * usbmsc.c). Internal to src/hwdrv: the platform contract stays hwdrv.h.
 *
 * Everything is synchronous and polled. One transfer is in flight per
 * endpoint at a time; usb_submit/usb_reap let an interrupt endpoint keep a
 * transfer queued while the caller polls. Data buffers are DMA memory the
 * caller allocated through p->dma_alloc (bulk, interrupt) or the device's own
 * bounce buffer (control, which copies). */
#ifndef ZXV_HWDRV_USB_H
#define ZXV_HWDRV_USB_H

#include "hwdrv.h"

/* Transfer results: >= 0 is the byte count moved. */
#define USB_ETIMEOUT (-1) /* no completion in time; the endpoint was stopped */
#define USB_ESTALL   (-2) /* the device stalled: endpoint halted until cleared */
#define USB_EIO      (-3) /* any other completion code (babble, transaction error) */
#define USB_EDEAD    (-4) /* the controller stopped answering commands */
#define USB_EPENDING (-5) /* usb_reap: still in flight */

/* Port speeds as the xHCI default protocol speed IDs number them. */
#define USB_SPEED_FS 1
#define USB_SPEED_LS 2
#define USB_SPEED_HS 3
#define USB_SPEED_SS 4

#define USB_CTRL_BUF 1024 /* largest control data stage (configuration descriptor) */
#define USB_MAX_EP   4    /* endpoints of the one interface a device is used through */

typedef struct usb_ring {
    uint8_t *trb;
    uint64_t phys;
    uint32_t enq;
    uint8_t cycle;
} usb_ring_t;

typedef struct usb_ep {
    uint8_t addr;     /* bEndpointAddress: number, 0x80 = IN */
    uint8_t type;     /* bmAttributes & 3: 0 control, 2 bulk, 3 interrupt (no isoch) */
    uint16_t mps;     /* max packet size (11 bits) */
    uint8_t interval; /* bInterval as the descriptor gives it */
    uint8_t burst;    /* SuperSpeed bMaxBurst, or high-speed periodic extra transactions */
    uint8_t dci;      /* xHCI device context index: 2 * number + IN */
    usb_ring_t ring;
    /* the transfer in flight: TRB addresses of its stages */
    uint64_t setup, data, pend; /* pend = the TRB that interrupts on completion */
    uint32_t len, residual;
    uint8_t busy, done, cc;
} usb_ep_t;

struct xhci;

typedef struct usb_dev {
    struct xhci *hc;
    const hw_plat_t *p;
    uint8_t slot, root_port, speed, depth;
    uint32_t route;                  /* hub port numbers below the root port, 4 bits each */
    uint8_t tt_slot, tt_port;        /* transaction translator for LS/FS behind a HS hub */
    uint8_t hub, hub_ports, hub_ttt; /* slot context hub fields */
    uint8_t max_dci;
    uint8_t intf, cls, sub, proto; /* the interface the device is used through */
    uint16_t vid, pid;
    usb_ep_t ep0;
    usb_ep_t ep[USB_MAX_EP];
    uint32_t n_ep;
    uint8_t *ictx, *octx; /* input and output (device) contexts */
    uint64_t ictx_phys, octx_phys;
    uint8_t *buf; /* control transfer bounce buffer, USB_CTRL_BUF bytes */
    uint64_t buf_phys;
} usb_dev_t;

/* Control transfer on endpoint 0; data is copied through the bounce buffer
 * (len <= USB_CTRL_BUF). Returns bytes moved or a USB_E* code. A stall is
 * cleared on the host side before returning. */
int usb_control(usb_dev_t *d, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len,
                void *data);
/* Bulk or interrupt transfer of len bytes at DMA address phys; waits. */
int usb_xfer(usb_dev_t *d, usb_ep_t *e, uint64_t phys, uint32_t len, uint32_t timeout_us);
/* Queue a transfer without waiting (0 = queued); usb_reap reports its end. */
int usb_submit(usb_dev_t *d, usb_ep_t *e, uint64_t phys, uint32_t len);
int usb_reap(usb_dev_t *d, usb_ep_t *e);
/* Clear a halt: reset the host endpoint (data toggle back to 0, ring
 * emptied) and send CLEAR_FEATURE(ENDPOINT_HALT) to the device. 0 = done. */
int usb_clear_halt(usb_dev_t *d, usb_ep_t *e);
/* The first endpoint of the interface with this type and direction (0x80 =
 * IN, 0 = OUT); 0 if none. */
usb_ep_t *usb_find_ep(usb_dev_t *d, uint8_t type, uint8_t dir);
/* 1 once the controller stopped answering: no transfer will succeed. */
int usb_dead(const usb_dev_t *d);

/* Class drivers (attach after the core configured the interface's endpoints).
 * Each appends a short phrase to note: the name it registered, or why not.
 * Returns 1 bound and registered, 0 nothing to drive, -1 failed. */
int usbkbd_attach(usb_dev_t *d, hw_registry_t *r, hw_line_t *note);
int usbmsc_attach(usb_dev_t *d, hw_registry_t *r, hw_line_t *note);

/* Little-endian access to descriptors and DMA structures. */
static inline uint16_t usb_get16(const volatile uint8_t *b)
{
    return (uint16_t) (b[0] | (b[1] << 8));
}
static inline uint32_t usb_get32(const volatile uint8_t *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) |
           ((uint32_t) b[3] << 24);
}
static inline void usb_put32(volatile uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t) v;
    b[1] = (uint8_t) (v >> 8);
    b[2] = (uint8_t) (v >> 16);
    b[3] = (uint8_t) (v >> 24);
}

#endif
