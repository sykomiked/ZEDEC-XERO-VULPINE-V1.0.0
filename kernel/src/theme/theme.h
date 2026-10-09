/* theme.h — the ZXV palette as override-able tokens (a customizable interface)
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * WHY THIS EXISTS
 * --------------
 * "The interface should be customizable" is only real if nothing hard-codes a
 * colour. So every drawable in ZXV — icons, windows, the terminal — names a
 * THEME TOKEN (THEME_ACCENT, THEME_GOLD, ...) instead of a raw RGB value, and
 * the theme resolves the token to a colour. Change the theme and everything
 * that draws through it recolours at once; that is the whole customization
 * mechanism, and it is enforced by making the tokens the only palette icons
 * and widgets are given.
 *
 * The default is the ZXV house style: black voids, blood and dragon reds, gold,
 * fire orange, warm white, ember. A user overrides any slot; a reset restores
 * the default.
 *
 * Colours are 0x00RRGGBB. Freestanding: integer only, no allocation.
 */
#ifndef ZXV_THEME_H
#define ZXV_THEME_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    THEME_VOID = 0,     /* the black behind everything        */
    THEME_PANEL,        /* taskbar / window chrome            */
    THEME_PANEL_HI,     /* a raised panel                     */
    THEME_BORDER,       /* blood-red edges                    */
    THEME_SCALE,        /* dark dragon-scale fill             */
    THEME_EMBER,        /* dull glowing red                   */
    THEME_ACCENT,       /* fire orange — the primary accent   */
    THEME_GOLD,         /* the hoard                          */
    THEME_TEXT,         /* warm off-white                     */
    THEME_DANGER,       /* alarms / destructive               */
    THEME_JADE,         /* success / "ok" (a cool green)      */
    THEME_ARCANE,       /* sigils / chiglet / AI (purple)     */
    THEME_AZURE,        /* network / cold (blue)              */
    THEME__COUNT
} theme_color_t;

/* The mage hats have literal pigments (white/black/red/...) so a hat icon can
 * be drawn IN its colour. Indexed to match mage_hat_t (NONE..GREY). */
typedef enum {
    HATCOL_NONE = 0, HATCOL_WHITE, HATCOL_BLACK, HATCOL_RED, HATCOL_BLUE,
    HATCOL_PURPLE, HATCOL_GREEN, HATCOL_CLEAR, HATCOL_YELLOW, HATCOL_ORANGE,
    HATCOL_GREY, HATCOL__COUNT
} theme_hat_t;

typedef struct {
    uint32_t color[THEME__COUNT];
} theme_t;

/* Load the default ZXV house palette. */
void theme_init(theme_t *t);
/* Override / read a slot. Override is the customization mechanism. */
bool theme_set(theme_t *t, theme_color_t slot, uint32_t rgb);
uint32_t theme_get(const theme_t *t, theme_color_t slot);
/* Restore one slot (or all, with slot == THEME__COUNT) to the default. */
void theme_reset(theme_t *t, theme_color_t slot);

/* The literal pigment for a hat. Independent of the theme (a red hat is red
 * whatever the desktop palette is). */
uint32_t theme_hat_color(theme_hat_t hat);

const char *theme_slot_name(theme_color_t slot);

#endif /* ZXV_THEME_H */
