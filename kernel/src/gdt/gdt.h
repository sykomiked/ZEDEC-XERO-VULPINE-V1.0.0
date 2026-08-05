/* gdt.h — Global Descriptor Table for x86 protected mode
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_ENTRIES 6

typedef struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

void gdt_init(void);
void gdt_set_entry(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

#endif
