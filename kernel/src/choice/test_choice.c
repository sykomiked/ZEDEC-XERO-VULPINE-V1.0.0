/* test_choice.c — Test suite for Choice-Collapse Scheduler (BIOS Stage 4)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "m5_types.h"
#include "choice_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_collapse_identity(void)
{
    collapse_t input = {{0x00000001, 0x00000002}};
    choice_set_state(&input);
    choice_handoff();
    collapse_t result = choice_get_state();
    assert(result.bits[0] == 0x00000001);
    assert(result.bits[1] == 0x00000002);
    printf("Identity collapse test passed\n");
}

static void test_collapse_zero(void)
{
    collapse_t input = {{0, 0}};
    choice_set_state(&input);
    choice_handoff();
    collapse_t result = choice_get_state();
    assert(result.bits[0] == 0);
    assert(result.bits[1] == 0);
    printf("Zero collapse test passed\n");
}

static void test_collapse_all_ones(void)
{
    collapse_t input = {{0xffffffff, 0xffffffff}};
    choice_set_state(&input);
    choice_handoff();
    collapse_t result = choice_get_state();
    assert(result.bits[0] == 0xffffffff);
    assert(result.bits[1] == 0xffffffff);
    printf("All-ones collapse test passed\n");
}

int main(void)
{
    test_collapse_identity();
    test_collapse_zero();
    test_collapse_all_ones();
    printf("All CHOICE tests passed\n");
    return 0;
}