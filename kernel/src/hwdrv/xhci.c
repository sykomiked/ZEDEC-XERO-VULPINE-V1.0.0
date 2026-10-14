/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* xhci.c — USB eXtensible Host Controller class driver (PCI class 0C/03/30),
 * polled, plus the USB core the class drivers use (usb.h).
 *
 * Follows xHCI 1.2 section 4.2 (initialisation): take the controller from the
 * BIOS (USB Legacy Support capability), halt, HCRST, DCBAA with scratchpad
 * buffers, a command ring and one event ring segment, run. No interrupts:
 * the event ring is consumed by its cycle bit from whatever loop is waiting.
 *
 * Then each connected root port is reset (USB2 needs PR; USB3 trains itself,
 * a warm reset if it does not) and its device enumerated: Enable Slot, Address
 * Device, descriptors, one interface picked (boot keyboard, bulk-only mass
 * storage, hub), Configure Endpoint, SET_CONFIGURATION, class attach. One
 * level of hubs: devices behind a root-port hub are enumerated too.
 *
 * Contradiction (HW_B): CAPLENGTH too small, MaxPorts or MaxSlots 0, no 4 KB
 * page size, or register blocks outside BAR0. A device that fails is noted
 * and skipped; only a controller that does not run is HW_F. */
#include "usb.h"

/* capability registers */
#define CAP_LENVER 0x00
#define CAP_HCS1   0x04
#define CAP_HCS2   0x08
#define CAP_HCC1   0x10
#define CAP_DBOFF  0x14
#define CAP_RTSOFF 0x18
/* operational registers (after CAPLENGTH) */
#define OP_USBCMD    0x00
#define OP_USBSTS    0x04
#define OP_PAGESIZE  0x08
#define OP_CRCR      0x18
#define OP_DCBAAP    0x30
#define OP_CONFIG    0x38
#define OP_PORTSC(n) (0x400u + 0x10u * ((n) - 1u))
/* runtime registers, interrupter 0 */
#define RT_IMAN   0x20
#define RT_ERSTSZ 0x28
#define RT_ERSTBA 0x30
#define RT_ERDP   0x38

#define CMD_RS    0x1u
#define CMD_HCRST 0x2u
#define STS_HCH   0x1u
#define STS_HSE   0x4u
#define STS_PCD   0x10u
#define STS_CNR   0x800u
#define STS_HCE   0x1000u

#define PS_CCS      0x1u
#define PS_PED      0x2u
#define PS_PR       0x10u
#define PS_PP       0x200u
#define PS_SPEED(v) (((v) >> 10) & 0xFu)
#define PS_WRC      (1u << 19)
#define PS_PRC      (1u << 21)
#define PS_CHANGES  (0x7Fu << 17) /* CSC PEC WRC OCC PRC PLC CEC: write 1 to clear */
#define PS_WPR      (1u << 31)
/* Bits that write back unchanged (read-only and sticky ones); PED and the
 * change bits are write-1-to-clear and must be written as 0. */
#define PS_KEEP 0x4E00FFE9u

/* TRB types */
#define TRB_NORMAL   1u
#define TRB_SETUP    2u
#define TRB_DATA     3u
#define TRB_STATUS   4u
#define TRB_LINK     6u
#define TRB_EN_SLOT  9u
#define TRB_ADDR_DEV 11u
#define TRB_CFG_EP   12u
#define TRB_EVAL_CTX 13u
#define TRB_RESET_EP 14u
#define TRB_STOP_EP  15u
#define TRB_SET_DEQ  16u
#define EVT_XFER     32u
#define EVT_CMD      33u

#define TRB_TC      0x2u /* link: toggle cycle */
#define TRB_ISP     0x4u
#define TRB_IOC     0x20u
#define TRB_IDT     0x40u
#define TRB_DIR     0x10000u /* data/status stage IN */
#define TRB_TYPE(t) ((t) << 10)

/* completion codes */
#define CC_SUCCESS 1
#define CC_STALL   6
#define CC_SHORT   13

#define RING_TRBS   64  /* 1 KB transfer and command rings */
#define EVT_TRBS    256 /* 4 KB event ring segment */
#define MAX_SLOTS   32
#define MAX_SCRATCH 256
#define STEP_US     20

#define CMD_TIMEOUT_US  1000000u
#define CTRL_TIMEOUT_US 1000000u

typedef struct xhci {
    const hw_plat_t *p;
    volatile uint8_t *cap, *op, *rt, *db;
    uint32_t ports, slots, csz;
    uint8_t major[256]; /* USB major revision per root port from the protocol caps */
    uint64_t *dcbaa;
    uint64_t dcbaa_phys;
    usb_ring_t cmd;
    uint8_t *evt;
    uint64_t evt_phys;
    uint32_t evt_deq;
    uint8_t evt_cycle;
    /* the command in flight */
    uint64_t cmd_ptr;
    uint8_t cmd_done, cmd_cc, cmd_slot;
    uint8_t dead;    /* a command timed out: nothing more is sent */
    uint8_t forced;  /* the BIOS never released ownership */
    uint32_t others; /* devices with no class driver here (or hubs below a hub) */
    usb_dev_t *slot_dev[MAX_SLOTS + 1];
} xhci_t;

static uint64_t get64(const volatile uint8_t *b)
{
    return (uint64_t) usb_get32(b) | ((uint64_t) usb_get32(b + 4) << 32);
}
static void put64(volatile uint8_t *b, uint64_t v)
{
    usb_put32(b, (uint32_t) v);
    usb_put32(b + 4, (uint32_t) (v >> 32));
}

static void delay(xhci_t *x, uint32_t us)
{
    x->p->delay_us(x->p->ctx, us);
}

/* ---- rings ---------------------------------------------------------------- */
static int ring_init(const hw_plat_t *p, usb_ring_t *r)
{
    /* 1 KB aligned to 1 KB: never crosses a 64 KB boundary. */
    r->trb = (uint8_t *) p->dma_alloc(p->ctx, RING_TRBS * 16, 1024, &r->phys);
    if (!r->trb) return 0;
    r->enq = 0;
    r->cycle = 1;
    uint8_t *l = r->trb + (RING_TRBS - 1) * 16; /* link back to the start, toggle cycle */
    put64(l, r->phys);
    usb_put32(l + 12, TRB_TYPE(TRB_LINK) | TRB_TC);
    return 1;
}

/* Write one TRB at the enqueue pointer; the cycle bit goes last, it hands the
 * TRB to the controller. Returns the TRB's bus address. */
static uint64_t ring_push(usb_ring_t *r, uint64_t param, uint32_t status, uint32_t ctl)
{
    uint8_t *t = r->trb + r->enq * 16;
    uint64_t at = r->phys + r->enq * 16;
    put64(t, param);
    usb_put32(t + 8, status);
    hw_wmb();
    usb_put32(t + 12, (ctl & ~1u) | r->cycle);
    if (++r->enq == RING_TRBS - 1) {
        uint8_t *l = r->trb + r->enq * 16;
        hw_wmb();
        usb_put32(l + 12, TRB_TYPE(TRB_LINK) | TRB_TC | r->cycle);
        r->enq = 0;
        r->cycle ^= 1;
    }
    return at;
}

static uint64_t ring_deq(const usb_ring_t *r)
{
    return (r->phys + r->enq * 16) | r->cycle;
}

/* ---- contexts --------------------------------------------------------------- */
static uint8_t *in_ctx(usb_dev_t *d, uint32_t i) /* 0 control, 1 slot, 1 + dci endpoint */
{
    return d->ictx + i * d->hc->csz;
}

static uint32_t ep_state(usb_dev_t *d, usb_ep_t *e)
{
    return usb_get32(d->octx + e->dci * d->hc->csz) & 7;
}

static void fill_slot(usb_dev_t *d, uint8_t *s)
{
    usb_put32(s, d->route | ((uint32_t) d->speed << 20) | ((uint32_t) d->hub << 26) |
                     ((uint32_t) d->max_dci << 27));
    usb_put32(s + 4, ((uint32_t) d->root_port << 16) | ((uint32_t) d->hub_ports << 24));
    usb_put32(s + 8, d->tt_slot | ((uint32_t) d->tt_port << 8) | ((uint32_t) d->hub_ttt << 16));
}

/* Interrupt endpoint context interval: an exponent of 125 us microframes. */
static uint32_t ep_interval(const usb_dev_t *d, const usb_ep_t *e)
{
    if (e->type != 3) return 0;
    uint32_t b = e->interval ? e->interval : 1;
    if (d->speed == USB_SPEED_HS || d->speed >= USB_SPEED_SS) return b > 16 ? 15 : b - 1;
    uint32_t us = b * 8, iv = 0; /* LS/FS interrupt: b frames rounded down to 2^n (3..10) */
    while ((2u << iv) <= us) iv++;
    return iv;
}

static void fill_ep(usb_dev_t *d, uint8_t *c, usb_ep_t *e)
{
    uint32_t in = (e->addr & 0x80) ? 4 : 0;
    uint32_t type = e->type == 0 ? 4 : e->type + in;
    uint32_t esit = e->type == 3 ? (uint32_t) e->mps * (e->burst + 1u) : 0;
    uint32_t avg = e->type == 0 ? 8 : (e->type == 3 ? esit : 3072);
    usb_put32(c, (ep_interval(d, e) << 16) | ((esit >> 16) << 24));
    usb_put32(c + 4,
              (3u << 1) | (type << 3) | ((uint32_t) e->burst << 8) | ((uint32_t) e->mps << 16));
    put64(c + 8, ring_deq(&e->ring));
    usb_put32(c + 16, avg | ((esit & 0xFFFF) << 16));
}

/* ---- events and commands ------------------------------------------------------ */
static usb_ep_t *ep_by_dci(usb_dev_t *d, uint32_t dci)
{
    if (dci == 1) return &d->ep0;
    for (uint32_t i = 0; i < d->n_ep; i++)
        if (d->ep[i].dci == dci) return &d->ep[i];
    return 0;
}

static void xfer_event(xhci_t *x, uint32_t slot, uint32_t dci, uint64_t ptr, uint32_t st)
{
    if (slot == 0 || slot > MAX_SLOTS || !x->slot_dev[slot]) return;
    usb_ep_t *e = ep_by_dci(x->slot_dev[slot], dci);
    if (!e || !e->busy || e->done) return;
    uint32_t cc = st >> 24, res = st & 0xFFFFFF;
    int ours = ptr == e->pend || (e->data && ptr == e->data) || (e->setup && ptr == e->setup);
    if (!ours) return;                      /* a stopped earlier transfer */
    if (cc == CC_SHORT && ptr != e->pend) { /* short data stage: the status stage follows */
        e->residual = res;
        return;
    }
    if (ptr == e->pend && !e->setup) e->residual = res; /* bulk/interrupt: one TRB */
    if (cc != CC_SUCCESS && cc != CC_SHORT && ptr == e->data) e->residual = res;
    e->cc = (uint8_t) cc;
    e->done = 1;
}

/* Consume every event the controller has posted, then move the dequeue
 * pointer (clearing Event Handler Busy). */
static void evt_pump(xhci_t *x)
{
    uint32_t n = 0;
    for (; n < EVT_TRBS; n++) {
        const volatile uint8_t *e = x->evt + x->evt_deq * 16;
        uint32_t ctl = usb_get32(e + 12);
        if ((ctl & 1) != x->evt_cycle) break;
        uint64_t ptr = get64(e);
        uint32_t st = usb_get32(e + 8), type = (ctl >> 10) & 0x3F;
        if (type == EVT_CMD && ptr == x->cmd_ptr && !x->cmd_done) {
            x->cmd_cc = (uint8_t) (st >> 24);
            x->cmd_slot = (uint8_t) (ctl >> 24);
            x->cmd_done = 1;
        } else if (type == EVT_XFER) {
            xfer_event(x, ctl >> 24, (ctl >> 16) & 0x1F, ptr, st);
        } /* port status change and others: ports are scanned, not event driven */
        if (++x->evt_deq == EVT_TRBS) {
            x->evt_deq = 0;
            x->evt_cycle ^= 1;
        }
    }
    if (n) hw_wr64(x->rt, RT_ERDP, (x->evt_phys + x->evt_deq * 16) | 0x8);
}

/* Run one command; returns its completion code, -1 on timeout (and the
 * controller is then left alone: a ring that stopped consuming commands is
 * not recovered here). */
static int xhci_cmd(xhci_t *x, uint64_t param, uint32_t ctl, uint32_t *slot)
{
    if (x->dead) return -1;
    x->cmd_done = 0;
    x->cmd_ptr = ring_push(&x->cmd, param, 0, ctl);
    hw_wmb();
    hw_wr32(x->db, 0, 0);
    for (uint32_t w = 0;; w += STEP_US) {
        evt_pump(x);
        if (x->cmd_done) {
            if (slot) *slot = x->cmd_slot;
            return x->cmd_cc;
        }
        if (w >= CMD_TIMEOUT_US) {
            x->dead = 1;
            return -1;
        }
        delay(x, STEP_US);
    }
}

static int slot_cmd(usb_dev_t *d, uint32_t type, uint64_t param, uint32_t extra)
{
    return xhci_cmd(d->hc, param, TRB_TYPE(type) | extra | ((uint32_t) d->slot << 24), 0);
}

/* ---- endpoint recovery ----------------------------------------------------------- */
#define EP_RUNNING 1
#define EP_HALTED  2

/* Bring a halted or busy endpoint to Stopped with an empty ring. */
static int ep_abort(usb_dev_t *d, usb_ep_t *e)
{
    e->busy = 0;
    uint32_t dci = (uint32_t) e->dci << 16, s = ep_state(d, e);
    if (s == EP_HALTED && slot_cmd(d, TRB_RESET_EP, 0, dci) != CC_SUCCESS) return -1;
    if (s == EP_RUNNING && slot_cmd(d, TRB_STOP_EP, 0, dci) < 0) return -1;
    return slot_cmd(d, TRB_SET_DEQ, ring_deq(&e->ring), dci) == CC_SUCCESS ? 0 : -1;
}

/* Drop and re-add the endpoint: the only way to zero the host data toggle of
 * an endpoint that is not halted. */
static int ep_readd(usb_dev_t *d, usb_ep_t *e)
{
    if (ep_state(d, e) == EP_RUNNING && slot_cmd(d, TRB_STOP_EP, 0, (uint32_t) e->dci << 16) < 0)
        return -1;
    hw_memset(d->ictx, 0, 33 * d->hc->csz);
    usb_put32(in_ctx(d, 0), 1u << e->dci);            /* drop */
    usb_put32(in_ctx(d, 0) + 4, 1u | (1u << e->dci)); /* add slot + endpoint */
    fill_slot(d, in_ctx(d, 1));
    fill_ep(d, in_ctx(d, 1 + e->dci), e);
    return slot_cmd(d, TRB_CFG_EP, d->ictx_phys, 0) == CC_SUCCESS ? 0 : -1;
}

int usb_clear_halt(usb_dev_t *d, usb_ep_t *e)
{
    int halted = ep_state(d, e) == EP_HALTED;
    if ((halted ? ep_abort(d, e) : ep_readd(d, e)) != 0) return -1;
    if (e->dci == 1) return 0;
    return usb_control(d, 0x02, 0x01, 0, e->addr, 0, 0) >= 0 ? 0 : -1;
}

/* ---- transfers ------------------------------------------------------------------- */
static void ep_start(usb_ep_t *e, uint32_t len)
{
    e->busy = 1;
    e->done = 0;
    e->cc = 0;
    e->residual = 0;
    e->len = len;
    e->setup = e->data = e->pend = 0;
}

static void ring_db(usb_dev_t *d, usb_ep_t *e)
{
    hw_wmb();
    hw_wr32(d->hc->db, 4u * d->slot, e->dci);
}

static int ep_result(usb_ep_t *e)
{
    e->busy = 0;
    if (e->cc == CC_SUCCESS || e->cc == CC_SHORT)
        return (int) (e->len - (e->residual < e->len ? e->residual : e->len));
    return e->cc == CC_STALL ? USB_ESTALL : USB_EIO;
}

static int ep_wait(usb_dev_t *d, usb_ep_t *e, uint32_t timeout_us)
{
    xhci_t *x = d->hc;
    for (uint32_t w = 0;; w += STEP_US) {
        evt_pump(x);
        if (e->done) return ep_result(e);
        if (x->dead) return USB_EDEAD;
        if (w >= timeout_us) {
            ep_abort(d, e);
            return x->dead ? USB_EDEAD : USB_ETIMEOUT;
        }
        delay(x, STEP_US);
    }
}

int usb_control(usb_dev_t *d, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len,
                void *data)
{
    usb_ep_t *e = &d->ep0;
    if (d->hc->dead) return USB_EDEAD;
    if (len > USB_CTRL_BUF) return USB_EIO;
    int in = (rt & 0x80) != 0;
    if (!in && len) hw_memcpy(d->buf, data, len);
    uint64_t setup = rt | ((uint32_t) req << 8) | ((uint32_t) val << 16) | ((uint64_t) idx << 32) |
                     ((uint64_t) len << 48);
    uint32_t trt = len ? (in ? 3u : 2u) : 0u;
    ep_start(e, len);
    e->setup = ring_push(&e->ring, setup, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | (trt << 16));
    if (len)
        e->data = ring_push(&e->ring, d->buf_phys, len,
                            TRB_TYPE(TRB_DATA) | TRB_ISP | (in ? TRB_DIR : 0));
    /* status stage runs opposite to the data, IN when there is none */
    e->pend =
        ring_push(&e->ring, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | ((len && in) ? 0 : TRB_DIR));
    ring_db(d, e);
    int rc = ep_wait(d, e, CTRL_TIMEOUT_US);
    if (rc > 0 && in) hw_memcpy(data, d->buf, (uint32_t) rc);
    if (rc == USB_ESTALL || rc == USB_EIO) ep_abort(d, e); /* EP0 halts; next SETUP clears */
    return rc;
}

int usb_submit(usb_dev_t *d, usb_ep_t *e, uint64_t phys, uint32_t len)
{
    if (d->hc->dead) return USB_EDEAD;
    if (e->busy || len == 0 || len > 0x10000) return USB_EIO;
    ep_start(e, len);
    e->pend = ring_push(&e->ring, phys, len, TRB_TYPE(TRB_NORMAL) | TRB_ISP | TRB_IOC);
    ring_db(d, e);
    return 0;
}

int usb_reap(usb_dev_t *d, usb_ep_t *e)
{
    if (d->hc->dead) return USB_EDEAD;
    if (!e->busy) return USB_EIO;
    evt_pump(d->hc);
    return e->done ? ep_result(e) : USB_EPENDING;
}

int usb_xfer(usb_dev_t *d, usb_ep_t *e, uint64_t phys, uint32_t len, uint32_t timeout_us)
{
    int rc = usb_submit(d, e, phys, len);
    return rc < 0 ? rc : ep_wait(d, e, timeout_us);
}

usb_ep_t *usb_find_ep(usb_dev_t *d, uint8_t type, uint8_t dir)
{
    for (uint32_t i = 0; i < d->n_ep; i++)
        if (d->ep[i].type == type && (d->ep[i].addr & 0x80) == dir) return &d->ep[i];
    return 0;
}

int usb_dead(const usb_dev_t *d)
{
    return d->hc->dead;
}

/* ---- enumeration ------------------------------------------------------------------ */
static void note_cc(hw_line_t *l, const char *what, int cc)
{
    hw_line_s(l, what);
    if (cc < 0) {
        hw_line_s(l, " timed out");
        return;
    }
    hw_line_s(l, " failed (cc ");
    hw_line_u(l, (uint32_t) cc);
    hw_line_c(l, ')');
}

static void note_sep(hw_line_t *l)
{
    if (l->n) hw_line_s(l, ", ");
}

/* Which interface to use: 1 boot keyboard, 2 BOT mass storage, 3 hub. */
static int class_of(uint8_t cls, uint8_t sub, uint8_t proto)
{
    if (cls == 3 && sub == 1 && proto == 1) return 1;
    if (cls == 8 && sub == 6 && proto == 0x50) return 2;
    if (cls == 9) return 3;
    return 0;
}

/* Walk the configuration descriptor: pick the first usable interface (alt 0)
 * and collect its endpoints. Returns its class_of() kind, 0 if none. */
static int parse_config(usb_dev_t *d, const uint8_t *c, uint32_t tot)
{
    int kind = 0, cur = 0;
    for (uint32_t off = 0; off + 2 <= tot;) {
        uint32_t bl = c[off], ty = c[off + 1];
        if (bl < 2 || off + bl > tot) break;
        if (ty == 4 && bl >= 9) {
            if (kind) {
                cur = 0; /* past ours: an alternate setting or the next interface */
            } else if (c[off + 3] == 0 && (cur = class_of(c[off + 5], c[off + 6], c[off + 7]))) {
                kind = cur;
                d->intf = c[off + 2];
                d->cls = c[off + 5];
                d->sub = c[off + 6];
                d->proto = c[off + 7];
            }
        } else if (ty == 5 && bl >= 7 && cur && d->n_ep < USB_MAX_EP) {
            usb_ep_t *e = &d->ep[d->n_ep];
            uint32_t mps = usb_get16(c + off + 4);
            e->addr = c[off + 2];
            e->type = c[off + 3] & 3;
            e->mps = (uint16_t) (mps & 0x7FF);
            e->interval = c[off + 6];
            e->dci = (uint8_t) (2 * (e->addr & 0xF) + ((e->addr & 0x80) ? 1 : 0));
            if (d->speed == USB_SPEED_HS && e->type == 3) e->burst = (uint8_t) ((mps >> 11) & 3);
            if ((e->addr & 0xF) && e->type >= 2) d->n_ep++; /* bulk and interrupt only */
        } else if (ty == 0x30 && bl >= 6 && cur && d->n_ep) {
            d->ep[d->n_ep - 1].burst = c[off + 2] > 15 ? 15 : c[off + 2]; /* SS companion */
        }
        off += bl;
    }
    return kind;
}

static int dev_configure(usb_dev_t *d, int with_eps)
{
    uint32_t add = 1;
    hw_memset(d->ictx, 0, 33 * d->hc->csz);
    if (with_eps) {
        for (uint32_t i = 0; i < d->n_ep; i++) {
            usb_ep_t *e = &d->ep[i];
            add |= 1u << e->dci;
            if (e->dci > d->max_dci) d->max_dci = e->dci;
            fill_ep(d, in_ctx(d, 1 + e->dci), e);
        }
    }
    usb_put32(in_ctx(d, 0) + 4, add);
    fill_slot(d, in_ctx(d, 1));
    return slot_cmd(d, TRB_CFG_EP, d->ictx_phys, 0);
}

static usb_dev_t *dev_alloc(xhci_t *x)
{
    const hw_plat_t *p = x->p;
    usb_dev_t *d = (usb_dev_t *) p->dma_alloc(p->ctx, sizeof(usb_dev_t), 64, 0);
    if (!d) return 0;
    d->hc = x;
    d->p = p;
    /* contexts: 64-byte aligned, never across a page */
    d->ictx = (uint8_t *) p->dma_alloc(p->ctx, 33 * x->csz, 4096, &d->ictx_phys);
    d->octx = (uint8_t *) p->dma_alloc(p->ctx, 32 * x->csz, 4096, &d->octx_phys);
    d->buf = (uint8_t *) p->dma_alloc(p->ctx, USB_CTRL_BUF, USB_CTRL_BUF, &d->buf_phys);
    if (!d->ictx || !d->octx || !d->buf || !ring_init(p, &d->ep0.ring)) return 0;
    for (uint32_t i = 0; i < USB_MAX_EP; i++)
        if (!ring_init(p, &d->ep[i].ring)) return 0;
    return d;
}

static void hub_attach(xhci_t *x, usb_dev_t *h, hw_registry_t *r, hw_line_t *devs);

/* Enumerate the device on root port `port` (route 0) or on port `hport` of
 * hub h. Appends what became of it to devs. */
static void usb_attach(xhci_t *x, uint32_t port, uint32_t speed, usb_dev_t *h, uint32_t hport,
                       hw_registry_t *r, hw_line_t *devs)
{
    hw_line_t why;
    hw_line_init(&why);
    uint32_t slot = 0;
    int cc = xhci_cmd(x, 0, TRB_TYPE(TRB_EN_SLOT), &slot);
    if (cc != CC_SUCCESS || slot == 0 || slot > MAX_SLOTS) {
        note_cc(&why, "Enable Slot", cc == CC_SUCCESS ? 0 : cc);
        goto fail;
    }
    usb_dev_t *d = dev_alloc(x);
    if (!d) {
        hw_line_s(&why, "out of DMA memory");
        goto fail;
    }
    d->slot = (uint8_t) slot;
    d->root_port = (uint8_t) port;
    d->speed = (uint8_t) speed;
    if (h) {
        d->depth = 1;
        d->route = hport;
        if (h->speed == USB_SPEED_HS && (speed == USB_SPEED_FS || speed == USB_SPEED_LS)) {
            d->tt_slot = h->slot;
            d->tt_port = (uint8_t) hport;
        }
    }
    d->max_dci = 1;
    x->slot_dev[slot] = d;
    x->dcbaa[slot] = d->octx_phys;

    /* Address Device: slot context + EP0, MPS0 from the port speed */
    uint16_t mps0 = speed >= USB_SPEED_SS ? 512 : (speed == USB_SPEED_HS ? 64 : 8);
    d->ep0.dci = 1;
    d->ep0.mps = mps0;
    usb_put32(in_ctx(d, 0) + 4, 3); /* add slot and EP0 */
    fill_slot(d, in_ctx(d, 1));
    fill_ep(d, in_ctx(d, 2), &d->ep0);
    hw_wmb();
    cc = slot_cmd(d, TRB_ADDR_DEV, d->ictx_phys, 0);
    if (cc != CC_SUCCESS) {
        note_cc(&why, "Address Device", cc);
        goto fail;
    }
    delay(x, 2000); /* SET_ADDRESS recovery */

    uint8_t dd[18];
    if (speed == USB_SPEED_FS) { /* full speed: MPS0 is 8..64, learn it from 8 bytes */
        if (usb_control(d, 0x80, 6, 0x0100, 0, 8, dd) != 8) {
            hw_line_s(&why, "no device descriptor");
            goto fail;
        }
        uint8_t m = dd[7];
        if (m != 8 && m != 16 && m != 32 && m != 64) {
            hw_line_s(&why, "bad bMaxPacketSize0");
            goto fail;
        }
        if (m != mps0) {
            d->ep0.mps = m;
            hw_memset(d->ictx, 0, 33 * x->csz);
            usb_put32(in_ctx(d, 0) + 4, 2); /* EP0 only */
            fill_ep(d, in_ctx(d, 2), &d->ep0);
            cc = slot_cmd(d, TRB_EVAL_CTX, d->ictx_phys, 0);
            if (cc != CC_SUCCESS) {
                note_cc(&why, "Evaluate Context", cc);
                goto fail;
            }
        }
    }
    if (usb_control(d, 0x80, 6, 0x0100, 0, 18, dd) != 18 || dd[1] != 1) {
        hw_line_s(&why, "no device descriptor");
        goto fail;
    }
    d->vid = usb_get16(dd + 8);
    d->pid = usb_get16(dd + 10);
    uint8_t ch[9];
    if (usb_control(d, 0x80, 6, 0x0200, 0, 9, ch) != 9 || ch[1] != 2) {
        hw_line_s(&why, "no configuration descriptor");
        goto fail;
    }
    uint32_t tot = usb_get16(ch + 2);
    if (tot < 9) {
        hw_line_s(&why, "bad configuration descriptor");
        goto fail;
    }
    if (tot > USB_CTRL_BUF) tot = USB_CTRL_BUF;
    static uint8_t cfg[USB_CTRL_BUF];
    int got = usb_control(d, 0x80, 6, 0x0200, 0, (uint16_t) tot, cfg);
    if (got < 9) {
        hw_line_s(&why, "no configuration descriptor");
        goto fail;
    }
    int kind = parse_config(d, cfg, (uint32_t) got);
    if (!kind) { /* nothing we drive: leave it addressed, unconfigured */
        x->others++;
        return;
    }
    if (kind == 3 && h) { /* one level of hubs only */
        x->others++;
        return;
    }
    cc = dev_configure(d, 1);
    if (cc != CC_SUCCESS) {
        note_cc(&why, "Configure Endpoint", cc);
        goto fail;
    }
    if (usb_control(d, 0x00, 9, cfg[5], 0, 0, 0) < 0) {
        hw_line_s(&why, "SET_CONFIGURATION failed");
        goto fail;
    }
    note_sep(devs);
    if (kind == 1)
        usbkbd_attach(d, r, devs);
    else if (kind == 2)
        usbmsc_attach(d, r, devs);
    else
        hub_attach(x, d, r, devs);
    return;
fail:
    note_sep(devs);
    hw_line_s(devs, "port ");
    hw_line_u(devs, port);
    if (h) {
        hw_line_c(devs, '.');
        hw_line_u(devs, hport);
    }
    hw_line_s(devs, ": ");
    hw_line_s(devs, why.b);
}

/* ---- hubs (one level) -------------------------------------------------------------- */
#define HUB_PORT_POWER   8
#define HUB_PORT_RESET   4
#define HUB_C_CONNECTION 16
#define HUB_C_RESET      20

static int hub_status(usb_dev_t *h, uint32_t port, uint32_t *st)
{
    uint8_t b[4];
    if (usb_control(h, 0xA3, 0, 0, (uint16_t) port, 4, b) != 4) return -1;
    *st = usb_get32(b); /* wPortStatus | wPortChange << 16 */
    return 0;
}

static void hub_attach(xhci_t *x, usb_dev_t *h, hw_registry_t *r, hw_line_t *devs)
{
    int ss = h->speed >= USB_SPEED_SS;
    uint8_t hd[16];
    int n = usb_control(h, 0xA0, 6, ss ? 0x2A00 : 0x2900, 0, sizeof hd, hd);
    if (n < 7 || hd[2] == 0) {
        hw_line_s(devs, "hub without descriptor");
        return;
    }
    uint32_t ports = hd[2], chars = usb_get16(hd + 3), pgood = hd[5] * 2u;
    h->hub = 1;
    h->hub_ports = (uint8_t) ports;
    h->hub_ttt = h->speed == USB_SPEED_HS ? (uint8_t) ((chars >> 5) & 3) : 0;
    int cc = dev_configure(h, 0); /* slot context only: it is a hub */
    if (cc != CC_SUCCESS) {
        note_cc(devs, "hub Configure Endpoint", cc);
        return;
    }
    if (ss) usb_control(h, 0x20, 12, 0, 0, 0, 0); /* SET_HUB_DEPTH 0 */
    hw_line_s(devs, "hub(");
    hw_line_u(devs, ports);
    hw_line_s(devs, ")");
    if (ports > 15) ports = 15; /* a route string nibble addresses ports 1..15 */
    for (uint32_t i = 1; i <= ports; i++)
        usb_control(h, 0x23, 3, HUB_PORT_POWER, (uint16_t) i, 0, 0);
    delay(x, (pgood > 100 ? pgood : 100) * 1000u); /* power good, then connect debounce */
    hw_line_t sub;
    hw_line_init(&sub);
    for (uint32_t i = 1; i <= ports; i++) {
        uint32_t st;
        if (hub_status(h, i, &st) != 0 || !(st & 1)) continue;
        if (!ss || !(st & 2)) { /* reset; a USB3 hub port that trained is already enabled */
            usb_control(h, 0x23, 3, HUB_PORT_RESET, (uint16_t) i, 0, 0);
            for (uint32_t t = 0; t < 50; t++) {
                delay(x, 10000);
                if (hub_status(h, i, &st) != 0 || ((st >> 16) & 0x10)) break;
            }
            usb_control(h, 0x23, 1, HUB_C_RESET, (uint16_t) i, 0, 0);
        }
        usb_control(h, 0x23, 1, HUB_C_CONNECTION, (uint16_t) i, 0, 0);
        if (hub_status(h, i, &st) != 0 || (st & 3) != 3) {
            note_sep(&sub);
            hw_line_s(&sub, "port ");
            hw_line_u(&sub, i);
            hw_line_s(&sub, " reset failed");
            continue;
        }
        delay(x, 10000); /* reset recovery */
        uint32_t sp = ss             ? USB_SPEED_SS
                      : (st & 0x200) ? USB_SPEED_LS
                                     : ((st & 0x400) ? USB_SPEED_HS : USB_SPEED_FS);
        usb_attach(x, h->root_port, sp, h, i, r, &sub);
    }
    if (sub.n) {
        hw_line_s(devs, ": ");
        hw_line_s(devs, sub.b);
    }
}

/* ---- root ports -------------------------------------------------------------------- */
static uint32_t ps_rd(xhci_t *x, uint32_t port)
{
    return hw_rd32(x->op, OP_PORTSC(port));
}

static void ps_wr(xhci_t *x, uint32_t port, uint32_t set)
{
    hw_wr32(x->op, OP_PORTSC(port), (ps_rd(x, port) & PS_KEEP) | set);
}

/* Wait until any of `bits` is set in PORTSC; 1 = seen. */
static int ps_wait(xhci_t *x, uint32_t port, uint32_t bits, uint32_t timeout_us)
{
    for (uint32_t w = 0;; w += 1000) {
        if (ps_rd(x, port) & bits) return 1;
        if (w >= timeout_us) return 0;
        delay(x, 1000);
    }
}

/* Get a connected root port enabled. 1 = enabled. */
static int port_enable(xhci_t *x, uint32_t port)
{
    uint32_t v = ps_rd(x, port);
    int usb3 = x->major[port] == 3 || (x->major[port] == 0 && (v & PS_PED));
    if (usb3) {
        /* USB3 links train on their own; a link stuck in Inactive or
         * Compliance needs a warm reset. */
        if (!ps_wait(x, port, PS_PED, 100000)) {
            ps_wr(x, port, PS_WPR);
            ps_wait(x, port, PS_WRC | PS_PRC, 500000);
        }
    } else {
        ps_wr(x, port, PS_PR);
        ps_wait(x, port, PS_PRC, 500000);
    }
    v = ps_rd(x, port);
    ps_wr(x, port, v & PS_CHANGES); /* acknowledge every change */
    delay(x, 10000);                /* reset recovery */
    v = ps_rd(x, port);
    return (v & (PS_CCS | PS_PED)) == (PS_CCS | PS_PED) && !(v & PS_PR);
}

static void scan_ports(xhci_t *x, hw_registry_t *r, hw_line_t *devs)
{
    for (uint32_t port = 1; port <= x->ports && !x->dead; port++) {
        uint32_t v = ps_rd(x, port);
        if (!(v & PS_CCS)) {
            ps_wr(x, port, v & PS_CHANGES);
            continue;
        }
        if (!port_enable(x, port)) {
            note_sep(devs);
            hw_line_s(devs, "port ");
            hw_line_u(devs, port);
            hw_line_s(devs, ": reset failed");
            continue;
        }
        usb_attach(x, port, PS_SPEED(ps_rd(x, port)), 0, 0, r, devs);
    }
    hw_wr32(x->op, OP_USBSTS, STS_PCD); /* port changes handled */
}

/* ---- controller -------------------------------------------------------------------- */
/* Walk the extended capabilities: take ownership from the BIOS (USB Legacy
 * Support, ID 1) and learn which ports are USB2 or USB3 (Supported Protocol,
 * ID 2). */
static void ext_caps(xhci_t *x, uint32_t off, uint64_t bar_size)
{
    for (uint32_t guard = 0; off && off + 16 <= bar_size && guard < 64; guard++) {
        uint32_t w = hw_rd32(x->cap, off);
        if (w == 0xFFFFFFFFu) return;
        uint32_t id = w & 0xFF;
        if (id == 1) {
            if (w & (1u << 16)) { /* BIOS owned: ask for it, wait up to 1 s */
                hw_wr32(x->cap, off, w | (1u << 24));
                if (!hw_wait32(x->p, x->cap, off, 1u << 16, 0, 1000000)) {
                    /* firmware never let go: take it, as Linux does */
                    hw_wr32(x->cap, off, (hw_rd32(x->cap, off) & ~(1u << 16)) | (1u << 24));
                    x->forced = 1;
                }
            } else {
                hw_wr32(x->cap, off, w | (1u << 24));
            }
            /* SMIs off: enables cleared, pending SMI status written back to clear */
            uint32_t c = hw_rd32(x->cap, off + 4);
            hw_wr32(x->cap, off + 4, (c & 0x000E1FEEu) | 0xE0000000u);
        } else if (id == 2) {
            uint32_t major = w >> 24, pw = hw_rd32(x->cap, off + 8);
            uint32_t first = pw & 0xFF, count = (pw >> 8) & 0xFF;
            for (uint32_t i = 0; i < count && first + i < 256; i++)
                if (first + i >= 1) x->major[first + i] = (uint8_t) major;
        }
        uint32_t next = (w >> 8) & 0xFF;
        if (!next) return;
        off += next * 4;
    }
}

/* Intel Panther Point and Lynx Point boot with their ports routed to the EHCI
 * companion; switch every port the chipset allows over to xHCI (USB3
 * SuperSpeed enable, then USB2 routing), as Linux does. */
static void intel_route_ports(const hw_plat_t *p, const hw_pci_dev_t *d)
{
    if (d->vendor != 0x8086 || (d->device != 0x1E31 && d->device != 0x8C31 && d->device != 0x9C31))
        return;
    p->cfg_write(p->ctx, d->bdf, 0xD8, p->cfg_read(p->ctx, d->bdf, 0xDC)); /* USB3_PSSEN */
    p->cfg_write(p->ctx, d->bdf, 0xD0, p->cfg_read(p->ctx, d->bdf, 0xD4)); /* XUSB2PR */
}

static const char *vendor_name(uint16_t v)
{
    switch (v) {
    case 0x8086:
        return "Intel";
    case 0x1022:
        return "AMD";
    case 0x1b36:
        return "QEMU";
    case 0x1033:
    case 0x1912:
        return "Renesas";
    case 0x1b21:
        return "ASMedia";
    case 0x1106:
        return "VIA";
    case 0x1b73:
        return "Fresco";
    }
    return 0;
}

hw_verdict_t xhci_bind(const hw_plat_t *p, const hw_pci_dev_t *d, hw_registry_t *r, hw_line_t *note)
{
    volatile uint8_t *cap = (volatile uint8_t *) hw_pci_map_bar(p, d, 0);
    if (!cap) {
        hw_line_s(note, "BAR0 not assigned");
        return HW_N;
    }
    xhci_t *x = (xhci_t *) p->dma_alloc(p->ctx, sizeof(xhci_t), 64, 0);
    if (!x) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    x->p = p;
    x->cap = cap;
    hw_pci_enable(p, d);
    uint32_t lv = hw_rd32(cap, CAP_LENVER);
    if (lv == 0xFFFFFFFFu) {
        hw_line_s(note, "registers read all-ones");
        return HW_F;
    }
    uint32_t caplen = lv & 0xFF, hcs1 = hw_rd32(cap, CAP_HCS1), hcs2 = hw_rd32(cap, CAP_HCS2);
    uint32_t hcc1 = hw_rd32(cap, CAP_HCC1);
    uint32_t dboff = hw_rd32(cap, CAP_DBOFF) & ~3u, rtsoff = hw_rd32(cap, CAP_RTSOFF) & ~0x1Fu;
    x->ports = hcs1 >> 24;
    x->slots = hcs1 & 0xFF;
    x->csz = (hcc1 & 4) ? 64 : 32;
    if (caplen < 0x20) {
        hw_line_s(note, "CAPLENGTH below 0x20");
        return HW_B;
    }
    if (x->ports == 0 || x->slots == 0) {
        hw_line_s(note, "MaxPorts or MaxSlots is 0");
        return HW_B;
    }
    uint64_t sz = d->bar_size[0];
    if (caplen + OP_PORTSC(x->ports) + 16 > sz || rtsoff + 0x40 > sz ||
        dboff + 4 * (x->slots + 1) > sz) {
        hw_line_s(note, "register blocks outside BAR0");
        return HW_B;
    }
    x->op = cap + caplen;
    x->rt = cap + rtsoff;
    x->db = cap + dboff;
    if (x->slots > MAX_SLOTS) x->slots = MAX_SLOTS;

    /* 1. ownership, then halt */
    ext_caps(x, (hcc1 >> 16) * 4, sz);
    hw_wr32(x->op, OP_USBCMD, hw_rd32(x->op, OP_USBCMD) & ~CMD_RS);
    if (!hw_wait32(p, x->op, OP_USBSTS, STS_HCH, STS_HCH, 32000)) {
        hw_line_s(note, "controller did not halt");
        return HW_F;
    }
    /* 2. reset; registers are written only once CNR clears */
    hw_wr32(x->op, OP_USBCMD, CMD_HCRST);
    if (!hw_wait32(p, x->op, OP_USBCMD, CMD_HCRST, 0, 1000000)) {
        hw_line_s(note, "reset timed out");
        return HW_F;
    }
    if (!hw_wait32(p, x->op, OP_USBSTS, STS_CNR, 0, 1000000)) {
        hw_line_s(note, "controller never ready");
        return HW_F;
    }
    if (!(hw_rd32(x->op, OP_PAGESIZE) & 1)) {
        hw_line_s(note, "no 4 KB page size");
        return HW_B;
    }
    hw_wr32(x->op, OP_CONFIG, x->slots);

    /* 3. DCBAA, scratchpad buffers, command ring, event ring */
    uint32_t nsp = (((hcs2 >> 21) & 0x1F) << 5) | ((hcs2 >> 27) & 0x1F);
    if (nsp > MAX_SCRATCH) {
        hw_line_s(note, "too many scratchpad buffers");
        return HW_F;
    }
    x->dcbaa = (uint64_t *) p->dma_alloc(p->ctx, 8 * (MAX_SLOTS + 1), 4096, &x->dcbaa_phys);
    uint64_t erst_phys = 0;
    uint8_t *erst = (uint8_t *) p->dma_alloc(p->ctx, 64, 64, &erst_phys);
    x->evt = (uint8_t *) p->dma_alloc(p->ctx, EVT_TRBS * 16, 4096, &x->evt_phys);
    if (!x->dcbaa || !erst || !x->evt || !ring_init(p, &x->cmd)) {
        hw_line_s(note, "out of DMA memory");
        return HW_F;
    }
    if (nsp) {
        uint64_t arr_phys = 0;
        uint64_t *arr = (uint64_t *) p->dma_alloc(p->ctx, 8 * nsp, 4096, &arr_phys);
        if (!arr) {
            hw_line_s(note, "out of DMA memory");
            return HW_F;
        }
        for (uint32_t i = 0; i < nsp; i++) {
            if (!p->dma_alloc(p->ctx, 4096, 4096, &arr[i])) {
                hw_line_s(note, "out of DMA memory");
                return HW_F;
            }
        }
        x->dcbaa[0] = arr_phys;
    }
    hw_wr64(x->op, OP_DCBAAP, x->dcbaa_phys);
    hw_wr64(x->op, OP_CRCR, x->cmd.phys | 1); /* RCS = 1 */
    put64(erst, x->evt_phys);
    usb_put32(erst + 8, EVT_TRBS);
    x->evt_cycle = 1;
    hw_wr32(x->rt, RT_ERSTSZ, 1);
    hw_wr64(x->rt, RT_ERDP, x->evt_phys);
    hw_wr64(x->rt, RT_ERSTBA, erst_phys);
    hw_wr32(x->rt, RT_IMAN, 1); /* clear IP; IE stays 0: polled */

    /* 4. run */
    hw_wr32(x->op, OP_USBCMD, CMD_RS);
    if (!hw_wait32(p, x->op, OP_USBSTS, STS_HCH, 0, 32000)) {
        hw_line_s(note, "controller did not start");
        return HW_F;
    }
    if (hw_rd32(x->op, OP_USBSTS) & (STS_HSE | STS_HCE)) {
        hw_line_s(note, "host system or controller error");
        return HW_F;
    }

    /* 5. ports: route them here, power them if software switches power, let
     * connections settle */
    intel_route_ports(p, d);
    if (hcc1 & 8)
        for (uint32_t i = 1; i <= x->ports; i++)
            if (!(ps_rd(x, i) & PS_PP)) ps_wr(x, i, PS_PP);
    delay(x, 100000);

    const char *vn = vendor_name(d->vendor);
    if (vn) {
        hw_line_s(note, vn);
        hw_line_c(note, ' ');
    }
    hw_line_s(note, "xHCI ");
    hw_line_u(note, x->ports);
    hw_line_s(note, " ports");
    if (x->forced) hw_line_s(note, " (BIOS kept ownership, taken)");
    hw_line_t devs;
    hw_line_init(&devs);
    scan_ports(x, r, &devs);
    hw_line_s(note, "; ");
    if (x->others) {
        note_sep(&devs);
        hw_line_u(&devs, x->others);
        hw_line_s(&devs, " other");
    }
    hw_line_s(note, devs.n ? devs.b : "no USB devices");
    if (x->dead) {
        hw_line_s(note, "; command ring stopped");
        return HW_F;
    }
    return HW_T;
}
