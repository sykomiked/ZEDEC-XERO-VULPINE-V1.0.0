/* pci.c — PCI bus enumeration implementation
 * Scans bus 0 for devices, reads vendor/device/class/BARs.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "pci.h"

/* ---- MODBIND DECLARATION — L4 devices -------------------------------------
 * Comment, not code, pending the ZXV_PROVIDES mechanism -- see the fuller note
 * in kernel/src/pic/pic.c and PROVENANCE/X86_REHOME.md.
 *
 *   ZXV_PROVIDES(pci_bus_ready)
 *   ZXV_REQUIRES()                 -- nothing
 *   ZXV_BRINGUP(pci_init)
 *
 * Enumeration is pure CAM access through the 0xCF8/0xCFC configuration ports
 * and nothing else; `nm -u` on the object is empty. In particular this module
 * does NOT require irq_ctrl_ready: it READS each device's interrupt line from
 * config offset 0x3C and records it, but never unmasks or routes anything.
 * Recording an IRQ number is not consuming an interrupt controller.
 */

#ifndef TEST_HOST
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
static inline void outl(uint16_t port, uint32_t val) { (void)port; (void)val; }
static inline uint32_t inl(uint16_t port) { (void)port; return 0xFFFFFFFF; }
#endif

uint32_t pci_config_read(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset) {
    uint32_t addr = (1 << 31) | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset, uint32_t value) {
    uint32_t addr = (1 << 31) | (bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

void pci_init(pci_state_t *state) {
    state->num_devices = 0;

    for (uint32_t bus = 0; bus < PCI_MAX_BUSES; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            for (uint32_t func = 0; func < PCI_MAX_FUNCS; func++) {
                if (state->num_devices >= PCI_MAX_DEVICES) return;

                uint32_t vendor_device = pci_config_read(bus, dev, func, 0);
                uint16_t vendor_id = (uint16_t)(vendor_device & 0xFFFF);
                uint16_t device_id = (uint16_t)(vendor_device >> 16);

                if (vendor_id == 0xFFFF) continue;

                pci_device_t *d = &state->devices[state->num_devices++];
                d->bus = (uint16_t)bus;
                d->device = (uint16_t)dev;
                d->func = (uint16_t)func;
                d->vendor_id = vendor_id;
                d->device_id = device_id;
                d->valid = true;

                uint32_t class_info = pci_config_read(bus, dev, func, 0x08);
                d->class_code = (uint16_t)((class_info >> 24) & 0xFF);
                d->subclass = (uint16_t)((class_info >> 16) & 0xFF);

                uint32_t irq_info = pci_config_read(bus, dev, func, 0x3C);
                d->irq = (uint8_t)(irq_info & 0xFF);

                for (int i = 0; i < 6; i++) {
                    d->bar[i] = pci_config_read(bus, dev, func, 0x10 + i * 4);
                }

                if (func == 0) {
                    uint32_t header_type = pci_config_read(bus, dev, func, 0x0C);
                    if (!((header_type >> 16) & 0x80)) break;
                }
            }
        }
    }
}

pci_device_t *pci_find_device(pci_state_t *state, uint16_t vendor, uint16_t device) {
    for (uint32_t i = 0; i < state->num_devices; i++) {
        if (state->devices[i].vendor_id == vendor &&
            state->devices[i].device_id == device)
            return &state->devices[i];
    }
    return 0;
}

pci_device_t *pci_find_class(pci_state_t *state, uint16_t class_code) {
    for (uint32_t i = 0; i < state->num_devices; i++) {
        if (state->devices[i].class_code == class_code)
            return &state->devices[i];
    }
    return 0;
}
