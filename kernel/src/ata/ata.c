/* ata.c — ATA/IDE driver implementation
 * PIO mode LBA28 read/write, identify command.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "ata.h"

/* ---- MODBIND DECLARATION — L4 devices -------------------------------------
 * Comment, not code, pending the ZXV_PROVIDES mechanism -- see the fuller note
 * in kernel/src/pic/pic.c and PROVENANCE/X86_REHOME.md.
 *
 *   ZXV_PROVIDES(ata_block_ready)
 *   ZXV_REQUIRES()                 -- nothing
 *   ZXV_BRINGUP(ata_init)
 *
 * The tempting declaration here is REQUIRES(pci_bus_ready), and it would be
 * wrong. ata_init probes the LEGACY fixed port pairs 0x1F0/0x3F6 and
 * 0x170/0x376 straight from ata.h -- it never calls pci_config_read and never
 * consults a BAR. `nm -u` on the object is empty, which is the check that
 * settles it. A PCI-native (BAR-addressed) ATA path would be a DIFFERENT
 * provider of ata_block_ready that does require pci_bus_ready; modbind treats
 * two providers of one capability as alternative provision, not conflict,
 * which is exactly the case this is.
 *
 * Transfers are polled PIO (ata_wait spins on BSY/DRQ), so no IRQ 14/15
 * dependency either -- hence no irq_ctrl_ready.
 */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void ata_wait(uint16_t cmd_port) {
    while (inb(cmd_port) & 0x80);
    while (!(inb(cmd_port) & 0x40));
}

static void ata_select_device(ata_device_t *dev, uint32_t lba) {
    uint8_t drive_val = 0xE0 | (dev->is_master ? 0 : 0x10) | ((lba >> 24) & 0x0F);
    outb(dev->drive_port, drive_val);
    for (int i = 0; i < 4; i++) inb(dev->command_port);
}

void ata_init(ata_state_t *state) {
    state->num_devices = 0;

    uint16_t bases[2] = {ATA_PRIMARY_DATA, ATA_SECONDARY_DATA};
    uint16_t ctrls[2] = {ATA_PRIMARY_CONTROL, ATA_SECONDARY_CONTROL};

    for (uint8_t bus = 0; bus < 2; bus++) {
        for (uint8_t master = 0; master < 2; master++) {
            ata_device_t *dev = &state->devices[state->num_devices];
            dev->bus = bus;
            dev->is_master = (master == 0);
            dev->data_port = bases[bus];
            dev->error_port = bases[bus] + 1;
            dev->count_port = bases[bus] + 2;
            dev->lba_lo_port = bases[bus] + 3;
            dev->lba_mid_port = bases[bus] + 4;
            dev->lba_hi_port = bases[bus] + 5;
            dev->drive_port = bases[bus] + 6;
            dev->command_port = bases[bus] + 7;
            dev->control_port = ctrls[bus];
            dev->present = false;

            outb(dev->control_port, 0x02);
            outb(dev->drive_port, dev->is_master ? 0xA0 : 0xB0);
            for (int i = 0; i < 4; i++) inb(dev->command_port);

            outb(dev->count_port, 0);
            outb(dev->lba_lo_port, 0);
            outb(dev->lba_mid_port, 0);
            outb(dev->lba_hi_port, 0);
            outb(dev->command_port, ATA_CMD_IDENTIFY);

            uint8_t status = inb(dev->command_port);
            if (status == 0) continue;

            while (inb(dev->command_port) & 0x80);
            uint8_t mid = inb(dev->lba_mid_port);
            uint8_t hi = inb(dev->lba_hi_port);
            if (mid != 0 || hi != 0) continue;

            while (!(inb(dev->command_port) & 0x40));

            uint16_t ident[256];
            for (int i = 0; i < 256; i++)
                ident[i] = inw(dev->data_port);

            dev->present = true;
            dev->total_sectors = ident[60] | ((uint32_t)ident[61] << 16);

            for (int i = 0; i < 40; i += 2) {
                dev->model[i] = (char)(ident[27 + i/2] >> 8);
                dev->model[i + 1] = (char)(ident[27 + i/2] & 0xFF);
            }
            dev->model[40] = 0;

            state->num_devices++;
        }
    }
}

int ata_identify(ata_device_t *dev) {
    if (!dev->present) return -1;
    return 0;
}

int ata_read_sector(ata_device_t *dev, uint32_t lba, uint8_t *buffer) {
    if (!dev->present) return -1;

    ata_select_device(dev, lba);
    outb(dev->count_port, 1);
    outb(dev->lba_lo_port, (uint8_t)(lba & 0xFF));
    outb(dev->lba_mid_port, (uint8_t)((lba >> 8) & 0xFF));
    outb(dev->lba_hi_port, (uint8_t)((lba >> 16) & 0xFF));
    outb(dev->command_port, ATA_CMD_READ_PIO);

    ata_wait(dev->command_port);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++)
        buf[i] = inw(dev->data_port);

    return 0;
}

int ata_write_sector(ata_device_t *dev, uint32_t lba, const uint8_t *buffer) {
    if (!dev->present) return -1;

    ata_select_device(dev, lba);
    outb(dev->count_port, 1);
    outb(dev->lba_lo_port, (uint8_t)(lba & 0xFF));
    outb(dev->lba_mid_port, (uint8_t)((lba >> 8) & 0xFF));
    outb(dev->lba_hi_port, (uint8_t)((lba >> 16) & 0xFF));
    outb(dev->command_port, ATA_CMD_WRITE_PIO);

    ata_wait(dev->command_port);

    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < 256; i++)
        outw(dev->data_port, buf[i]);

    return 0;
}
