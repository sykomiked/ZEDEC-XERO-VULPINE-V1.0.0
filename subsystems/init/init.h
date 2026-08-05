/* init.h — Init/Service Manager
 * Boots all OS services in dependency order, manages lifecycle.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef INIT_H
#define INIT_H

#include <stdint.h>
#include <stdbool.h>
#include "../kernel/src/vbe/vbe.h"
#include "../kernel/src/pci/pci.h"
#include "../kernel/src/acpi/acpi.h"
#include "../kernel/src/ata/ata.h"
#include "../kernel/src/fat32/fat32.h"
#include "../gui/gui.h"

#define INIT_MAX_SERVICES 32

typedef enum {
    SERVICE_STOPPED = 0,
    SERVICE_STARTING = 1,
    SERVICE_RUNNING = 2,
    SERVICE_FAILED = 3,
    SERVICE_STOPPING = 4
} service_status_t;

typedef enum {
    SVC_GDT = 0,
    SVC_IDT = 1,
    SVC_PIC = 2,
    SVC_TIMER = 3,
    SVC_KEYBOARD = 4,
    SVC_MOUSE = 5,
    SVC_PCI = 6,
    SVC_ACPI = 7,
    SVC_VBE = 8,
    SVC_ATA = 9,
    SVC_FAT32 = 10,
    SVC_GUI = 11,
    SVC_LATTICE = 12,
    SVC_NEON = 13,
    SVC_GRIDCHAIN = 14,
    SVC_SECURITY = 15,
    SVC_PHYSICS = 16,
    SVC_HCCS = 17,
    SVC_AUDIOGENOMICS = 18,
    SVC_GOVERNANCE = 19
} service_id_t;

typedef struct init_service {
    service_id_t id;
    char name[32];
    service_status_t status;
    uint32_t dependencies[8];
    uint32_t num_deps;
    bool critical;
} init_service_t;

typedef struct init_state {
    init_service_t services[INIT_MAX_SERVICES];
    uint32_t num_services;
    pci_state_t pci;
    acpi_state_t acpi;
    ata_state_t ata;
    fat32_state_t fs;
    vbe_state_t vbe;
    gui_desktop_t gui;
    bool gui_mode;
    uint64_t boot_time_ms;
} init_state_t;

void init_setup(init_state_t *init);
int init_start_service(init_state_t *init, service_id_t id);
int init_stop_service(init_state_t *init, service_id_t id);
void init_boot_sequence(init_state_t *init);
void init_register_services(init_state_t *init);
service_status_t init_get_status(init_state_t *init, service_id_t id);
const char *init_status_str(service_status_t status);

#endif
