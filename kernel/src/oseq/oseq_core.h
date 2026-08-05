#ifndef OSEQ_CORE_H
#define OSEQ_CORE_H
#include "m5_types.h"
#include <stdint.h>

#define OSEQ_MAX_DEVICES 256

typedef struct oseq_device_t {
    char name[32];
    ordinal_t ordinal;
} oseq_device_t;

typedef struct oseq_state_t {
    ordinal_t current_cycle;
    oseq_device_t devices[OSEQ_MAX_DEVICES];
    uint32_t num_devices;
} oseq_state_t;

void oseq_init(oseq_state_t *state);
ordinal_t oseq_register_device(oseq_state_t *state, const char *name);
bool oseq_is_valid_ordinal(const ordinal_t ordinal);
bool oseq_is_later(const ordinal_t a, const ordinal_t b);

#endif
