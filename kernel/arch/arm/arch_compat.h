/* arch_compat.h — Architecture compatibility shim
 * Provides x86 API names that map to ARM equivalents,
 * so shared kernel code compiles unchanged on both architectures.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ARCH_COMPAT_H
#define ARCH_COMPAT_H

#ifdef __ARM_ARCH__

/* On ARM, pull in the ARM-specific headers that provide
 * the same API surface as x86 headers */

#include "arch.h"
#include "registers.h"
#include "gic.h"

/* x86 outb/inb → ARM MMIO (for shared code that uses port I/O) */
#ifndef TEST_HOST
static inline void outb(uint16_t port, uint8_t val) {
    /* On ARM, port I/O maps to MMIO at a fixed offset.
     * QEMU virt doesn't have ISA port I/O, so this is a no-op. */
    (void)port; (void)val;
}

static inline uint8_t inb(uint16_t port) {
    (void)port;
    return 0;
}
#endif

/* x86 ISR handler type compatibility */
typedef void (*isr_handler_t)(registers_t *regs);

/* x86 IDT functions → ARM GIC equivalents */
#define idt_init()              gic_init()
#define idt_register_handler(irq, handler) arm_irq_register(irq, handler)

/* x86 PIC functions → ARM GIC equivalents (already defined in gic.h) */
/* pic_init, pic_unmask, pic_mask, pic_send_eoi are #defined in gic.h */

/* x86 GDT → no-op on ARM (MMU is configured differently) */
#define gdt_init()              ((void)0)

/* x86 timer → ARM timer */
#include "timer_arm.h"
#define PIT_FREQUENCY           24000000
#define PIT_CHANNEL0            0
#define PIT_COMMAND             0

/* x86 keyboard/mouse → stubs (ARM virt has no PS/2) */
typedef struct {
    uint8_t buffer[256];
    uint32_t buf_head, buf_tail, buf_count;
    int shift, ctrl, alt, caps_lock, num_lock, scroll_lock;
} keyboard_state_t;

typedef struct {
    int8_t buttons;
    int8_t dx;
    int8_t dy;
} mouse_packet_t;

typedef struct {
    mouse_packet_t packets[256];
    uint32_t pkt_head, pkt_tail, pkt_count;
    int32_t x, y;
    uint8_t buttons, cycle, byte[3];
} mouse_state_t;

static inline void keyboard_init(void) {}
static inline void keyboard_handler(registers_t *r) { (void)r; }
static inline int keyboard_getchar(void) { return -1; }
static inline int keyboard_has_data(void) { return 0; }
static inline keyboard_state_t *keyboard_get_state(void) { return 0; }

static inline void mouse_init(void) {}
static inline void mouse_handler(registers_t *r) { (void)r; }
static inline int mouse_has_packet(void) { return 0; }
static inline int mouse_get_packet(mouse_packet_t *p) { (void)p; return 0; }
static inline mouse_state_t *mouse_get_state(void) { return 0; }

/* x86 PCI/ACPI/VBE/ATA → stubs (ARM virt uses device tree) */
typedef struct { uint32_t num_devices; } pci_state_t;
static inline void pci_init(pci_state_t *s) { s->num_devices = 0; }

typedef struct { uint32_t rsdp_address; } acpi_state_t;
static inline void acpi_init(acpi_state_t *s) { s->rsdp_address = 0; }

typedef struct { uint32_t width, height, bpp, fb_addr; } vbe_state_t;
#define VBE_DEFAULT_WIDTH   1024
#define VBE_DEFAULT_HEIGHT  768
#define VBE_DEFAULT_BPP     32
static inline void vbe_init_fb(vbe_state_t *v, uint16_t w, uint16_t h, uint8_t bpp, uint32_t addr) {
    v->width = w; v->height = h; v->bpp = bpp; v->fb_addr = addr;
}

typedef struct { uint32_t num_devices; int mounted; } ata_state_t;
static inline void ata_init(ata_state_t *a) { a->num_devices = 0; a->mounted = 0; }

/* x86 multiboot → no-op on ARM */
typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower, mem_upper, boot_device, cmdline;
    uint32_t mods_count, mods_addr, syms[4];
    uint32_t mmap_length, mmap_addr, drives_length, drives_addr;
    uint32_t config_table, boot_loader_name, apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
} multiboot_info_t;

/* x86 framebuffer → UART on ARM */
#define FB_WIDTH    80
#define FB_HEIGHT   25
#define FB_MEMORY   ((volatile uint16_t *)0)
#define FB_WHITE_ON_BLACK 0x0F

static inline void fb_init(void) { uart_init(); }
static inline void fb_clear(void) { uart_clear(); }
static inline void fb_putc(char c) { uart_putc(c); }
static inline void fb_puts(const char *s) { uart_puts(s); }

/* x86 inline assembly → ARM equivalents */
#define __asm__ __asm__
#define __volatile__ __volatile__

/* sti → enable interrupts, hlt → wfi */
#define sti() arch_enable_interrupts()
#define hlt() arch_halt()

#endif /* __ARM_ARCH__ */

#endif /* ARCH_COMPAT_H */
