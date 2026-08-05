/* theme.h — Semantic color theming layer over the existing VBE RGB
 * system (vbe.h). Apps/GUI reference colors by semantic SLOT rather
 * than hardcoded hex values, so switching themes doesn't require
 * touching every app's rendering code.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ZXV_THEME_H
#define ZXV_THEME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../kernel/src/vbe/vbe.h"

typedef enum {
    THEME_SLOT_BACKGROUND = 0,
    THEME_SLOT_PANEL,
    THEME_SLOT_PRIMARY,
    THEME_SLOT_ACCENT,
    THEME_SLOT_TEXT,
    THEME_SLOT_TEXT_MUTED,
    THEME_SLOT_BORDER,
    THEME_SLOT_SUCCESS,
    THEME_SLOT_WARNING,
    THEME_SLOT_ERROR,
    THEME_SLOT_MAX
} theme_slot_t;

typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT = 1,
    THEME_HIGH_CONTRAST = 2,
} theme_id_t;

typedef struct theme {
    theme_id_t id;
    uint32_t colors[THEME_SLOT_MAX];
} theme_t;

void theme_init(theme_t *t, theme_id_t id);
uint32_t theme_color(const theme_t *t, theme_slot_t slot);
void theme_set_active(theme_t *t);
uint32_t theme_get(theme_slot_t slot); /* reads from the process-wide active theme */
const char *theme_slot_name(theme_slot_t slot);
const char *theme_name(theme_id_t id);

#endif
