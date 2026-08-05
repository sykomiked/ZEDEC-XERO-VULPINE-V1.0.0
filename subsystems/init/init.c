/* init.c — Init/Service Manager Implementation
 * Boots all services in dependency order, manages lifecycle.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "init.h"
#include "../kernel/src/gdt/gdt.h"
#include "../kernel/src/idt/idt.h"
#include "../kernel/src/pic/pic.h"
#include "../kernel/src/timer/timer.h"
#include "../kernel/src/keyboard/keyboard.h"
#include "../kernel/src/mouse/mouse.h"

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void str_copy(char *d, const char *s) { int i = 0; while (s[i]) { d[i] = s[i]; i++; } d[i] = 0; }

void init_register_services(init_state_t *init) {
    init->num_services = 0;

    init_service_t svcs[] = {
        {SVC_GDT,    "GDT",       SERVICE_STOPPED, {0}, 0, true},
        {SVC_IDT,    "IDT",       SERVICE_STOPPED, {SVC_GDT}, 1, true},
        {SVC_PIC,    "PIC",       SERVICE_STOPPED, {SVC_IDT}, 1, true},
        {SVC_TIMER,  "Timer",     SERVICE_STOPPED, {SVC_PIC}, 1, true},
        {SVC_KEYBOARD,"Keyboard", SERVICE_STOPPED, {SVC_PIC}, 1, false},
        {SVC_MOUSE,  "Mouse",     SERVICE_STOPPED, {SVC_PIC}, 1, false},
        {SVC_PCI,    "PCI Bus",   SERVICE_STOPPED, {SVC_IDT}, 1, false},
        {SVC_ACPI,   "ACPI",      SERVICE_STOPPED, {0}, 0, false},
        {SVC_VBE,    "VBE Graphics",SERVICE_STOPPED,{SVC_PCI},1, false},
        {SVC_ATA,    "ATA Disk",  SERVICE_STOPPED, {SVC_PCI}, 1, false},
        {SVC_FAT32,  "FAT32 FS",  SERVICE_STOPPED, {SVC_ATA}, 1, false},
        {SVC_GUI,    "GUI",       SERVICE_STOPPED, {SVC_VBE, SVC_KEYBOARD, SVC_MOUSE}, 3, false},
        {SVC_LATTICE,"Lattice",   SERVICE_STOPPED, {SVC_TIMER}, 1, false},
        {SVC_NEON,   "Neon",      SERVICE_STOPPED, {SVC_LATTICE}, 1, false},
        {SVC_GRIDCHAIN,"GridChain",SERVICE_STOPPED,{SVC_LATTICE},1, false},
        {SVC_SECURITY,"Security", SERVICE_STOPPED, {SVC_LATTICE}, 1, false},
        {SVC_PHYSICS,"Physics",   SERVICE_STOPPED, {SVC_LATTICE}, 1, false},
        {SVC_HCCS,   "HCCS",      SERVICE_STOPPED, {SVC_LATTICE}, 1, false},
        {SVC_AUDIOGENOMICS,"AudioGenomics",SERVICE_STOPPED,{SVC_LATTICE},1,false},
        {SVC_GOVERNANCE,"Governance",SERVICE_STOPPED,{SVC_LATTICE},1,false},
    };

    for (int i = 0; i < 20; i++) {
        init->services[i] = svcs[i];
        init->num_services++;
    }
}

int init_start_service(init_state_t *init, service_id_t id) {
    if (id >= init->num_services) return -1;
    init_service_t *svc = &init->services[id];

    for (uint32_t i = 0; i < svc->num_deps; i++) {
        if (init->services[svc->dependencies[i]].status != SERVICE_RUNNING) {
            svc->status = SERVICE_FAILED;
            return -2;
        }
    }

    svc->status = SERVICE_STARTING;

#ifndef TEST_HOST
    switch (id) {
        case SVC_GDT:       gdt_init(); break;
        case SVC_IDT:       idt_init(); break;
        case SVC_PIC:       pic_init(); break;
        case SVC_TIMER:     timer_init(TIMER_DEFAULT_HZ); break;
        case SVC_KEYBOARD:  keyboard_init(); break;
        case SVC_MOUSE:     mouse_init(); break;
        case SVC_PCI:       pci_init(&init->pci); break;
        case SVC_ACPI:      acpi_init(&init->acpi); break;
        case SVC_VBE:       vbe_init(&init->vbe, VBE_DEFAULT_WIDTH, VBE_DEFAULT_HEIGHT, VBE_DEFAULT_BPP); break;
        case SVC_ATA:       ata_init(&init->ata); break;
        case SVC_FAT32:
            if (init->ata.num_devices > 0)
                fat32_mount(&init->fs, &init->ata.devices[0]);
            break;
        case SVC_GUI:
            gui_init(&init->gui, &init->vbe);
            init->gui_mode = true;
            break;
        default:
            break;
    }
#endif

    svc->status = SERVICE_RUNNING;
    return 0;
}

int init_stop_service(init_state_t *init, service_id_t id) {
    if (id >= init->num_services) return -1;
    init->services[id].status = SERVICE_STOPPING;
    init->services[id].status = SERVICE_STOPPED;
    return 0;
}

void init_boot_sequence(init_state_t *init) {
    init_register_services(init);
    init->gui_mode = false;
    init->boot_time_ms = 0;

    service_id_t boot_order[] = {
        SVC_GDT, SVC_IDT, SVC_PIC, SVC_TIMER,
        SVC_KEYBOARD, SVC_MOUSE, SVC_PCI, SVC_ACPI,
        SVC_VBE, SVC_ATA, SVC_FAT32, SVC_GUI,
        SVC_LATTICE, SVC_NEON, SVC_GRIDCHAIN,
        SVC_SECURITY, SVC_PHYSICS, SVC_HCCS,
        SVC_AUDIOGENOMICS, SVC_GOVERNANCE
    };

    for (int i = 0; i < 20; i++) {
        init_start_service(init, boot_order[i]);
    }
}

service_status_t init_get_status(init_state_t *init, service_id_t id) {
    if (id >= init->num_services) return SERVICE_STOPPED;
    return init->services[id].status;
}

const char *init_status_str(service_status_t status) {
    switch (status) {
        case SERVICE_STOPPED:   return "STOPPED";
        case SERVICE_STARTING:  return "STARTING";
        case SERVICE_RUNNING:   return "RUNNING";
        case SERVICE_FAILED:    return "FAILED";
        case SERVICE_STOPPING:  return "STOPPING";
        default:                return "UNKNOWN";
    }
}

void init_setup(init_state_t *init) {
    for (uint32_t i = 0; i < INIT_MAX_SERVICES; i++) {
        init->services[i].status = SERVICE_STOPPED;
        init->services[i].num_deps = 0;
        init->services[i].critical = false;
    }
    init->num_services = 0;
    init->gui_mode = false;
}
