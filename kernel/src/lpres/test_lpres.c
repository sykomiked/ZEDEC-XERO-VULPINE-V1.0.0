/*
 * test_lpres.c — Tests for Logical Presence Attestor (LPRES)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */

#include "m5_types.h"
#include "lpres_core.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    // Initialize the LPRES subsystem
    lpres_init();

    // Test setting and getting presence for a resource
    ordinal_t resource_id = 42;
    trit_t presence = TRIT_TRUE;
    lpres_set_presence(resource_id, presence);
    assert(lpres_get_presence(resource_id) == presence);

    // Test handling of TRIT_GLUT
    presence = TRIT_GLUT;
    lpres_set_presence(resource_id, presence);
    assert(lpres_get_presence(resource_id) == presence);

    // Test getting presence for an unattested resource
    ordinal_t unattested_resource_id = 1337;
    assert(lpres_get_presence(unattested_resource_id) == TRIT_FALSE);

    return 0;
}
