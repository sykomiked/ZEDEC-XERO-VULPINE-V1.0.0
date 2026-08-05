#include <stdio.h>
#include "vino.h"

static vino_ledger_t ledger;
int main(void) {
    printf("1: about to vino_init\n"); fflush(stdout);
    vino_init(&ledger, 1);
    printf("2: vino_init done\n"); fflush(stdout);
    int32_t r1 = vino_create_account(&ledger, "alice@vino", "Alice");
    printf("3: create alice -> %d\n", r1); fflush(stdout);
    int32_t r2 = vino_create_account(&ledger, "genesis@vino", "Genesis");
    printf("4: create genesis -> %d\n", r2); fflush(stdout);
    vino_account_t *g = vino_get_account(&ledger, "genesis@vino");
    printf("5: get genesis -> %p\n", (void*)g); fflush(stdout);
    if (g) {
        g->balance[CAP_FINANCIAL] = 1000;
        printf("6: set balance done\n"); fflush(stdout);
    }
    int32_t r3 = vino_transfer(&ledger, "genesis@vino", "alice@vino", 700, CAP_FINANCIAL, RAIL_VINO_NATIVE, "fund alice");
    printf("7: transfer -> %d\n", r3); fflush(stdout);
    return 0;
}
