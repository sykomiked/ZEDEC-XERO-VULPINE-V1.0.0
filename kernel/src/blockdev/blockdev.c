/* blockdev.c — Generic block device implementation
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#include "../../include/blockdev.h"
#include <stddef.h>

void blockdev_init(block_device_t *dev,
                    int (*read)(block_device_t *, uint32_t, uint8_t *),
                    int (*write)(block_device_t *, uint32_t, const uint8_t *),
                    uint32_t total_sectors, const char *model) {
    dev->present = true;
    dev->total_sectors = total_sectors;
    dev->read_sector = read;
    dev->write_sector = write;
    dev->driver_data = 0;

    int i;
    for (i = 0; i < 40 && model[i]; i++)
        dev->model[i] = model[i];
    dev->model[i] = 0;
}
