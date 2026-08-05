/* rtl8139.c — RTL8139 Ethernet NIC Driver Implementation
 * Hardware interface only — protocol logic is in the M5 net layer.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "rtl8139.h"

#ifndef TEST_HOST
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret; __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port)); return ret;
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret; __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port)); return ret;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t ret; __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port)); return ret;
}
#else
static inline void outb(uint16_t p, uint8_t v) { (void)p; (void)v; }
static inline void outl(uint16_t p, uint32_t v) { (void)p; (void)v; }
static inline uint8_t inb(uint16_t p) { (void)p; return 0; }
static inline uint32_t inl(uint16_t p) { (void)p; return 0; }
static inline void outw(uint16_t p, uint16_t v) { (void)p; (void)v; }
static inline uint16_t inw(uint16_t p) { (void)p; return 0; }
#endif

static void mem_copy(void *d, const void *s, uint32_t n) {
    uint8_t *dst = d; const uint8_t *src = s;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

void rtl8139_reset(rtl8139_state_t *rtl) {
    outb(rtl->io_base + RTL_COMMAND, CMD_RESET);
    /* Wait for reset to complete */
    while (inb(rtl->io_base + RTL_COMMAND) & CMD_RESET) { }
}

void rtl8139_init(rtl8139_state_t *rtl, uint32_t io_base, net_state_t *net) {
    rtl->io_base = io_base;
    rtl->net = net;
    rtl->rx_offset = 0;
    rtl->initialized = false;

    rtl8139_reset(rtl);

    /* Read MAC address from EEPROM */
    for (int i = 0; i < 6; i++)
        rtl->mac[i] = inb(io_base + RTL_MAC0 + i);

    /* Enable RX/TX */
    outb(io_base + RTL_COMMAND, CMD_RX_ENABLE | CMD_TX_ENABLE);

    /* Set RX buffer size: 8K + 16, no wrap */
    outl(io_base + RTL_RX_CONFIG, 0x00000700 | 0x0000000E);

    /* Set TX config: DMA burst 256 bytes */
    outl(io_base + RTL_TX_CONFIG, 0x03000700);

    /* Enable interrupts */
    outw(io_base + RTL_IMR, 0x0005);

    /* Lock config register */
    outb(io_base + RTL_CONFIG1, 0x00);

    rtl->initialized = true;
}

void rtl8139_tx(rtl8139_state_t *rtl, const uint8_t *data, uint32_t len) {
    if (!rtl->initialized || len > RTL_TX_BUFFER_LEN) return;

    /* Use TX descriptor 0 (simplified) */
    uint32_t tx_addr = (uint32_t)(uintptr_t)data;

    /* Wait for TX descriptor to be free */
    while (inl(rtl->io_base + RTL_TX_STATUS0) & 0x2000) { }

    outl(rtl->io_base + RTL_TX_ADDR0, tx_addr);
    outl(rtl->io_base + RTL_TX_STATUS0, len | 0x03000000);
}

void rtl8139_poll(rtl8139_state_t *rtl) {
    if (!rtl->initialized) return;

    uint16_t isr = inw(rtl->io_base + RTL_ISR);
    if (isr & ISR_RX_OK) {
        /* Read received packet from RX buffer */
        uint16_t status = inw(rtl->io_base + RTL_RX_BUFPTR);
        uint16_t pkt_len = inw(rtl->io_base + RTL_RX_BUFPTR + 2);

        if (pkt_len > 0 && pkt_len < RTL_RX_BUFFER_LEN && rtl->net) {
            /* Copy packet to net stack */
            if (rtl->iface) {
                net_rx_packet(rtl->net, rtl->iface,
                              rtl->rx_buffer + rtl->rx_offset, pkt_len);
            }
            rtl->rx_offset = (rtl->rx_offset + pkt_len + 4) & (RTL_RX_BUFFER_LEN - 1);
        }
        /* Acknowledge interrupt */
        outw(rtl->io_base + RTL_ISR, ISR_RX_OK);
    }
    if (isr & ISR_TX_OK) {
        outw(rtl->io_base + RTL_ISR, ISR_TX_OK);
    }
}

void rtl8139_irq_handler(rtl8139_state_t *rtl) {
    rtl8139_poll(rtl);
}
