/*
 * lpres_core.c — Logical Presence Attestor (BIOS Stage 2)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */

#include "m5_types.h"
#include "lpres_core.h"
#include <stdio.h>
#include <stdlib.h>

// Global variable to keep track of the current cycle
static ordinal_t current_cycle = 0;

// Initialize the attestation table with all resources absent
void lpres_init(void) {
    for (int i = 0; i < LPRES_TABLE_SIZE; i++) {
        for (int j = 0; j < 8; j++) {
            lpres_table[i][j].resource_id = 0;
            lpres_table[i][j].magnitude.num = 0;
            lpres_table[i][j].magnitude.den = 1;
            lpres_table[i][j].presence = TRIT_FALSE;
        }
    }
}

// Get the presence state of a resource
trit_t lpres_get_presence(ordinal_t resource_id) {
    // Compute the index into the table
    int index = resource_id % LPRES_TABLE_SIZE;

    // Find the entry with the matching resource ID and highest cycle level.
    // NOTE: slot 0 is a legitimate storage slot (the first entry for any
    // resource lands there, since current_cycle starts at 0), so "found"
    // must be tracked separately from max_cycle -- max_cycle==0 does NOT
    // mean "not found".
    int max_cycle = 0;
    int found = 0;
    trit_t presence = TRIT_FALSE;
    for (int i = 7; i >= 0; i--) {
        if (lpres_table[index][i].resource_id == resource_id) {
            max_cycle = i;
            presence = lpres_table[index][i].presence;
            found = 1;
            break;
        }
    }

    // If the resource is not found, return TRIT_FALSE
    if (!found) {
        return TRIT_FALSE;
    }

    // Update the last_seen_cycle field
    lpres_table[index][max_cycle].presence = presence;
    lpres_table[index][max_cycle].resource_id = resource_id;

    // Return the presence state
    return presence;
}

// Set the presence state of a resource
void lpres_set_presence(ordinal_t resource_id, trit_t presence) {
    // Compute the index into the table
    int index = resource_id % LPRES_TABLE_SIZE;

    // Find the entry with the matching resource ID and highest cycle level.
    // NOTE: slot 0 is a legitimate storage slot, so "found" must be tracked
    // separately from max_cycle -- max_cycle==0 does NOT mean "not found"
    // (same sentinel-collision hazard documented in lpres_get_presence).
    int max_cycle = 0;
    int found = 0;
    for (int i = 7; i >= 0; i--) {
        if (lpres_table[index][i].resource_id == resource_id) {
            max_cycle = i;
            found = 1;
            break;
        }
    }

    // If the resource is not found, create a new entry at the current cycle level
    if (!found) {
        max_cycle = current_cycle % 8;
        lpres_table[index][max_cycle].resource_id = resource_id;
        lpres_table[index][max_cycle].magnitude = (rational_t){1, 1};
    }

    // Handle GLUT state: if presence is TRIT_GLUT, mark as unresolved
    if (presence == TRIT_GLUT) {
        lpres_table[index][max_cycle].presence = TRIT_GLUT;
    } else {
        lpres_table[index][max_cycle].presence = presence;
    }

    current_cycle++;
}
