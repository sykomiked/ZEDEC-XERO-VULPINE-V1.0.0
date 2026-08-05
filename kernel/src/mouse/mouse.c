/* mouse.c — PS/2 Mouse driver implementation
 * Handles 3-byte protocol packets, maintains position and button state.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "mouse.h"
#include "../pic/pic.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static mouse_state_t ms;

void mouse_wait_write(void) {
    while (inb(MOUSE_STATUS_PORT) & 0x02);
}

void mouse_wait_read(void) {
    while (!(inb(MOUSE_STATUS_PORT) & 0x01));
}

void mouse_init(void) {
    ms.pkt_head = 0;
    ms.pkt_tail = 0;
    ms.pkt_count = 0;
    ms.x = 0;
    ms.y = 0;
    ms.buttons = 0;
    ms.cycle = 0;

    outb(MOUSE_CMD_PORT, 0xA8);
    mouse_wait_write();
    outb(MOUSE_CMD_PORT, 0x20);
    mouse_wait_read();
    uint8_t status = inb(MOUSE_DATA_PORT);
    status |= 0x02;
    status &= ~0x20;
    mouse_wait_write();
    outb(MOUSE_CMD_PORT, 0x60);
    mouse_wait_write();
    outb(MOUSE_DATA_PORT, status);

    outb(MOUSE_CMD_PORT, 0xD4);
    mouse_wait_write();
    outb(MOUSE_DATA_PORT, 0xF4);
    mouse_wait_read();

    pic_unmask(IRQ_MOUSE);
}

void mouse_handler(registers_t *regs) {
    (void)regs;
    uint8_t byte = inb(MOUSE_DATA_PORT);
    ms.byte[ms.cycle] = byte;
    ms.cycle = (ms.cycle + 1) % 3;

    if (ms.cycle == 0) {
        if (ms.byte[0] & 0x08) {
            mouse_packet_t pkt;
            pkt.buttons = (int8_t)(ms.byte[0] & 0x07);
            pkt.dx = (int8_t)ms.byte[1];
            pkt.dy = (int8_t)ms.byte[2];

            ms.x += pkt.dx;
            ms.y -= pkt.dy;
            ms.buttons = (uint8_t)pkt.buttons;

            if (ms.pkt_count < MOUSE_BUFFER_SIZE) {
                ms.packets[ms.pkt_head] = pkt;
                ms.pkt_head = (ms.pkt_head + 1) % MOUSE_BUFFER_SIZE;
                ms.pkt_count++;
            }
        }
    }
}

int mouse_has_packet(void) {
    return ms.pkt_count > 0;
}

int mouse_get_packet(mouse_packet_t *pkt) {
    if (ms.pkt_count == 0 || !pkt) return -1;
    *pkt = ms.packets[ms.pkt_tail];
    ms.pkt_tail = (ms.pkt_tail + 1) % MOUSE_BUFFER_SIZE;
    ms.pkt_count--;
    return 0;
}

mouse_state_t *mouse_get_state(void) {
    return &ms;
}
