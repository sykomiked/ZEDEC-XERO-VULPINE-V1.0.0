/* uart_pl011.c — UART driver for ARM64 (multi-board)
 *
 * Hardware-as-code: dispatches between two real, industry-standard
 * UART register protocols based on board_profile.h's uart_is_pl011
 * flag, since "board" is data here, not a forked copy of this driver:
 *
 *   - PL011 (ARM PrimeCell): DR/FR/IBRD/FBRD/LCRH/CR. Used by QEMU's
 *     virt machine.
 *   - 16550-class / "snps,dw-apb-uart" / "mediatek,mt6577-uart":
 *     THR/RBR/DLL/DLH/LCR/LSR/FCR, register stride (1 << reg_shift)
 *     bytes. This is what REAL RK3399 silicon uses (mainline
 *     rk3399-base.dtsi: uart2 @ 0xff1a0000, reg-shift=2,
 *     compatible="rockchip,rk3399-uart","snps,dw-apb-uart") and what
 *     every MediaTek Tank board profile in board_profile.h assumes
 *     (mt6577-uart-compatible family) -- so this same branch is what
 *     both non-QEMU targets in this repo actually need, not just Tank.
 *
 * Retained under the historical filename (uart_pl011.c) since PL011
 * was the only protocol when QEMU virt was the only target; the
 * dispatch below is what actually ships now.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "arm64_arch.h"
#include "board_profile.h"

/* ===== PL011 (ARM PrimeCell) register offsets ===== */
#define PL011_DR    0x00
#define PL011_FR    0x18
#define PL011_IBRD  0x24
#define PL011_FBRD  0x28
#define PL011_LCRH  0x2C
#define PL011_CR    0x30

/* ===== 16550-class / dw-apb-uart register offsets (logical index;
 * multiply by (1 << reg_shift) for the byte offset -- RK3399's real
 * UARTs and MediaTek's mt6577-uart-compatible family both publish
 * reg-shift=2 in their device trees). ===== */
#define U16550_RBR_THR_DLL  0   /* RBR (read) / THR (write) when DLAB=0; DLL when DLAB=1 */
#define U16550_IER_DLH      1   /* IER when DLAB=0; DLH when DLAB=1 */
#define U16550_FCR          2   /* FCR (write) */
#define U16550_LCR          3
#define U16550_LSR          5

#define LCR_DLAB    (1u << 7)
#define LCR_8N1     0x03
#define LSR_DR      (1u << 0)   /* Data Ready (RX) */
#define LSR_THRE    (1u << 5)   /* THR Empty (TX ready) */
#define FCR_FIFO_EN (1u << 0)
#define FCR_RX_RST  (1u << 1)
#define FCR_TX_RST  (1u << 2)

static inline uint64_t u16550_reg(const board_profile_t *bp, uint32_t logical_offset) {
    return bp->uart_base + ((uint64_t)logical_offset << bp->uart_reg_shift);
}

/* ===== PL011 low-level ===== */

static void pl011_init(const board_profile_t *bp) {
    mmio_write(bp->uart_base + PL011_CR, 0);

    /* BAUDDIV = UARTCLK / (16 * baud); IBRD = integer part, FBRD =
     * round(fraction * 64). divisor_x64 = BAUDDIV*64 = UARTCLK*4/baud
     * computed as one integer division to avoid a separate float path.
     * At 24MHz/115200 this reproduces this driver's original hardcoded
     * IBRD=13, FBRD=1 exactly (verified by inspection: 24e6*4/115200 =
     * 833, 833/64=13 rem 1). */
    uint32_t baud = 115200;
    uint64_t divisor_x64 = ((uint64_t)bp->uart_clock_hz * 4) / baud;
    uint32_t ibrd = (uint32_t)(divisor_x64 / 64);
    uint32_t fbrd = (uint32_t)(divisor_x64 % 64);

    mmio_write(bp->uart_base + PL011_IBRD, ibrd);
    mmio_write(bp->uart_base + PL011_FBRD, fbrd);
    /* 8N1, FIFO enabled */
    mmio_write(bp->uart_base + PL011_LCRH, (1 << 4) | (1 << 5) | (1 << 6));
    /* Enable UART, TX, RX */
    mmio_write(bp->uart_base + PL011_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

static void pl011_putc(const board_profile_t *bp, char c) {
    while (mmio_read(bp->uart_base + PL011_FR) & (1 << 5)) { } /* TX FIFO full */
    mmio_write(bp->uart_base + PL011_DR, (uint32_t)(uint8_t)c);
}

static char pl011_getc(const board_profile_t *bp) {
    while (mmio_read(bp->uart_base + PL011_FR) & (1 << 4)) { } /* RX FIFO empty */
    return (char)mmio_read(bp->uart_base + PL011_DR);
}

static bool pl011_rx_ready(const board_profile_t *bp) {
    return !(mmio_read(bp->uart_base + PL011_FR) & (1 << 4));
}

/* ===== 16550-class low-level ===== */

static void u16550_init(const board_profile_t *bp) {
    uint64_t reg_rbr_dll = u16550_reg(bp, U16550_RBR_THR_DLL);
    uint64_t reg_ier_dlh = u16550_reg(bp, U16550_IER_DLH);
    uint64_t reg_fcr     = u16550_reg(bp, U16550_FCR);
    uint64_t reg_lcr     = u16550_reg(bp, U16550_LCR);

    uint32_t baud = 115200;
    /* Standard 16550 baud generator: divisor = clock / (16 * baud),
     * rounded to nearest for minimum baud error. */
    uint32_t divisor = (uint32_t)(((uint64_t)bp->uart_clock_hz + 8u * baud) / (16u * baud));
    if (divisor == 0) divisor = 1;

    mmio_write(reg_ier_dlh, 0);               /* disable interrupts during setup */
    mmio_write(reg_lcr, LCR_DLAB);             /* DLAB=1: access divisor latch */
    mmio_write(reg_rbr_dll, divisor & 0xFF);   /* DLL */
    mmio_write(reg_ier_dlh, (divisor >> 8) & 0xFF); /* DLH */
    mmio_write(reg_lcr, LCR_8N1);              /* DLAB=0, 8 data bits, no parity, 1 stop */
    mmio_write(reg_fcr, FCR_FIFO_EN | FCR_RX_RST | FCR_TX_RST);
}

static void u16550_putc(const board_profile_t *bp, char c) {
    uint64_t reg_lsr = u16550_reg(bp, U16550_LSR);
    uint64_t reg_thr = u16550_reg(bp, U16550_RBR_THR_DLL);
    while (!(mmio_read(reg_lsr) & LSR_THRE)) { }
    mmio_write(reg_thr, (uint32_t)(uint8_t)c);
}

static char u16550_getc(const board_profile_t *bp) {
    uint64_t reg_lsr = u16550_reg(bp, U16550_LSR);
    uint64_t reg_rbr = u16550_reg(bp, U16550_RBR_THR_DLL);
    while (!(mmio_read(reg_lsr) & LSR_DR)) { }
    return (char)mmio_read(reg_rbr);
}

static bool u16550_rx_ready(const board_profile_t *bp) {
    uint64_t reg_lsr = u16550_reg(bp, U16550_LSR);
    return (mmio_read(reg_lsr) & LSR_DR) != 0;
}

/* ===== Public dispatch (board-profile-driven) ===== */

void uart_putc(char c) {
    const board_profile_t *bp = board_get_profile();
    if (bp->uart_is_pl011) pl011_putc(bp, c);
    else u16550_putc(bp, c);
}

char uart_getc(void) {
    const board_profile_t *bp = board_get_profile();
    return bp->uart_is_pl011 ? pl011_getc(bp) : u16550_getc(bp);
}

void uart_init(void) {
    const board_profile_t *bp = board_get_profile();
    if (bp->uart_is_pl011) pl011_init(bp);
    else u16550_init(bp);
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_put_hex(uint64_t val) {
    char buf[17];
    int i;
    for (i = 15; i >= 0; i--) {
        int nibble = (val >> (i * 4)) & 0xF;
        buf[15 - i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    buf[16] = 0;
    uart_puts(buf);
}

void uart_put_dec(uint64_t val) {
    if (val == 0) { uart_putc('0'); return; }
    char tmp[20];
    int i = 0;
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) uart_putc(tmp[--i]);
}

bool uart_rx_ready(void) {
    const board_profile_t *bp = board_get_profile();
    return bp->uart_is_pl011 ? pl011_rx_ready(bp) : u16550_rx_ready(bp);
}
