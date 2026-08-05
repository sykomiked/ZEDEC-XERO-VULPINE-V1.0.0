/* pci.h — PCI bus enumeration driver
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_MAX_DEVICES 32
#define PCI_MAX_BUSES   256
#define PCI_MAX_FUNCS   8

typedef struct pci_device {
    uint16_t bus;
    uint16_t device;
    uint16_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t class_code;
    uint16_t subclass;
    uint8_t  irq;
    uint32_t bar[6];
    bool     valid;
} pci_device_t;

typedef struct pci_state {
    pci_device_t devices[PCI_MAX_DEVICES];
    uint32_t num_devices;
} pci_state_t;

void pci_init(pci_state_t *state);
uint32_t pci_config_read(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset);
void pci_config_write(uint32_t bus, uint32_t dev, uint32_t func, uint32_t offset, uint32_t value);
pci_device_t *pci_find_device(pci_state_t *state, uint16_t vendor, uint16_t device);
pci_device_t *pci_find_class(pci_state_t *state, uint16_t class_code);

#endif
