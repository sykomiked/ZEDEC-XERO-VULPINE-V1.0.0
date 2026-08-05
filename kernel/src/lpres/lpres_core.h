/*
 * lpres_core.h — Logical Presence Attestor (BIOS Stage 2)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */

#ifndef LPRES_CORE_H
#define LPRES_CORE_H

#include "m5_types.h"

#define LPRES_TABLE_SIZE 1024

/*
 * Attestation table structure.
 * Each entry contains the resource ID (ordinal_t), its allocated magnitude (rational_t),
 * and its current presence state (trit_t).
 */
typedef struct lpres_entry {
    ordinal_t resource_id;
    rational_t magnitude;
    trit_t presence;
} lpres_entry_t;

/*
 * Attestation table: a 2D array of lpres_entry_t elements.
 * The first dimension is the resource ID modulo LPRES_TABLE_SIZE.
 * The second dimension is the Fibonacci-scaled cycle level.
 */
extern lpres_entry_t lpres_table[LPRES_TABLE_SIZE][8];

/*
 * Initialize the Logical Presence Attestor (LPRES) subsystem.
 * Logs any GLUT states in the table.
 */
void lpres_init(void);

/*
 * Get the presence state of a resource.
 * Returns the presence state (trit_t) and updates the last_seen_cycle field.
 */
trit_t lpres_get_presence(ordinal_t resource_id);

/*
 * Set the presence state of a resource.
 * Updates the presence state (trit_t) and the last_seen_cycle field.
 */
void lpres_set_presence(ordinal_t resource_id, trit_t presence);

#endif
