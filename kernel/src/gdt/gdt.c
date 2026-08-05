/* gdt.c — GDT implementation
 * Sets up null, kernel code, kernel data, user code, user data, TSS segments.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "gdt.h"

static gdt_entry_t gdt_entries[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

void gdt_set_entry(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[idx].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt_entries[idx].base_low    = (uint16_t)(base & 0xFFFF);
    gdt_entries[idx].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt_entries[idx].access      = access;
    gdt_entries[idx].granularity = (uint8_t)((gran & 0xF0) | ((limit >> 16) & 0x0F));
    gdt_entries[idx].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt_entry_t) * GDT_ENTRIES - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    gdt_set_entry(0, 0, 0, 0, 0);                /* Null segment */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Kernel code */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Kernel data */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* User code */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* User data */
    gdt_set_entry(5, 0, 0, 0, 0);                /* TSS placeholder */

    __asm__ __volatile__("lgdt %0" : : "m"(gdt_ptr));
    __asm__ __volatile__("mov $0x10, %%ax\n\t"
                         "mov %%ax, %%ds\n\t"
                         "mov %%ax, %%es\n\t"
                         "mov %%ax, %%fs\n\t"
                         "mov %%ax, %%gs\n\t"
                         "mov %%ax, %%ss\n\t"
                         "ljmp $0x08, $1f\n\t"
                         "1:" : : : "eax");
}
