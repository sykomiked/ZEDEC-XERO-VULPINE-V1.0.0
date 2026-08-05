/* mouse.h — PS/2 Mouse driver
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "../idt/idt.h"

#define MOUSE_BUFFER_SIZE 256
#define MOUSE_DATA_PORT   0x60
#define MOUSE_STATUS_PORT 0x64
#define MOUSE_CMD_PORT    0x64

typedef struct mouse_packet {
    int8_t buttons;
    int8_t dx;
    int8_t dy;
} mouse_packet_t;

typedef struct mouse_state {
    mouse_packet_t packets[MOUSE_BUFFER_SIZE];
    uint32_t pkt_head;
    uint32_t pkt_tail;
    uint32_t pkt_count;
    int32_t x;
    int32_t y;
    uint8_t buttons;
    uint8_t cycle;
    uint8_t byte[3];
} mouse_state_t;

void mouse_init(void);
void mouse_handler(registers_t *regs);
int mouse_has_packet(void);
int mouse_get_packet(mouse_packet_t *pkt);
mouse_state_t *mouse_get_state(void);
void mouse_wait_write(void);
void mouse_wait_read(void);

#endif
