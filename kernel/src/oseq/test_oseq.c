#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m5_types.h"
#include "oseq_core.h"

#define ASSERT(cond, msg)                                                                          \
    if (!(cond)) {                                                                                 \
        printf("Assertion failed: %s\n", msg);                                                     \
        abort();                                                                                   \
    }

static oseq_state_t oseq_state;

static void test_ordinal_successor(void)
{
    ordinal_t a = 10;
    ordinal_t b = a + 1;
    ASSERT(oseq_is_valid_ordinal(a), "Invalid ordinal");
    ASSERT(oseq_is_valid_ordinal(b), "Invalid ordinal");
    ASSERT(oseq_is_later(b, a), "Successor not later");
}

static void test_ordinal_predecessor(void)
{
    ordinal_t a = 10;
    ordinal_t b = a - 1;
    ASSERT(oseq_is_valid_ordinal(a), "Invalid ordinal");
    ASSERT(oseq_is_valid_ordinal(b), "Invalid ordinal");
    ASSERT(oseq_is_later(a, b), "Predecessor not later");
}

static void test_ordinal_comparison(void)
{
    ordinal_t a = 10;
    ordinal_t b = 20;
    ASSERT(oseq_is_valid_ordinal(a), "Invalid ordinal");
    ASSERT(oseq_is_valid_ordinal(b), "Invalid ordinal");
    ASSERT(oseq_is_later(b, a), "Comparison failed");
}

static void test_device_registration(void)
{
    oseq_init(&oseq_state);
    ASSERT(oseq_register_device(&oseq_state, "Device1") == 0,
           "First device not registered at cycle 0");
    ASSERT(strcmp(oseq_state.devices[0].name, "Device1") == 0,
           "Device name not registered correctly");
    ASSERT(oseq_register_device(&oseq_state, "Device2") == 1,
           "Second device not registered at cycle 1");
    ASSERT(strcmp(oseq_state.devices[1].name, "Device2") == 0,
           "Device name not registered correctly");
    ASSERT(oseq_state.num_devices == 2, "Incorrect number of registered devices");
}

int main(void)
{
    test_ordinal_successor();
    test_ordinal_predecessor();
    test_ordinal_comparison();
    test_device_registration();
    printf("All OSEQ tests passed\n");
    return 0;
}
