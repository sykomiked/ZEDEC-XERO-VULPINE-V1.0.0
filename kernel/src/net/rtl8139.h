/* rtl8139.h — RTL8139 Ethernet NIC Driver
 * Works with QEMU, Bochs, VirtualBox, and real hardware.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>
#include <stdbool.h>
#include "../net/net.h"

#define RTL8139_VENDOR_ID   0x10EC
#define RTL8139_DEVICE_ID   0x8139

/* I/O register offsets */
#define RTL_MAC0        0x00
#define RTL_MAR0        0x08
#define RTL_TX_STATUS0  0x10
#define RTL_TX_ADDR0    0x20
#define RTL_RX_BUF      0x30
#define RTL_RX_BUFPTR   0x38
#define RTL_RX_BUFSIZE  0x3C
#define RTL_IMR         0x3C
#define RTL_ISR         0x3E
#define RTL_TX_CONFIG   0x40
#define RTL_RX_CONFIG   0x44
#define RTL_CONFIG1     0x52
#define RTL_COMMAND     0x37

/* Command register bits */
#define CMD_TX_ENABLE   0x04
#define CMD_RX_ENABLE   0x08
#define CMD_RESET       0x10

/* ISR bits */
#define ISR_RX_OK       0x01
#define ISR_RX_ERR      0x02
#define ISR_TX_OK       0x04
#define ISR_TX_ERR      0x08
#define ISR_RX_OVERFLOW 0x10
#define ISR_LINK_CHANGE 0x20
#define ISR_RX_FIFO     0x40
#define ISR_LEN_CHANGE  0x80

#define RTL_RX_BUFFER_LEN 8192
#define RTL_TX_BUFFER_LEN 1792
#define RTL_NUM_TX_DESC  4

typedef struct rtl8139_state {
    uint32_t io_base;
    uint8_t  mac[6];
    uint8_t  rx_buffer[RTL_RX_BUFFER_LEN + 16];
    uint32_t rx_offset;
    bool     initialized;
    net_state_t *net;
    net_interface_t *iface;
} rtl8139_state_t;

void rtl8139_init(rtl8139_state_t *rtl, uint32_t io_base, net_state_t *net);
void rtl8139_reset(rtl8139_state_t *rtl);
void rtl8139_tx(rtl8139_state_t *rtl, const uint8_t *data, uint32_t len);
void rtl8139_poll(rtl8139_state_t *rtl);
void rtl8139_irq_handler(rtl8139_state_t *rtl);

#endif
