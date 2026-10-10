#include "axiom_matrix_core.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Entries are Q16.16 complex integers (zxv_cq16_t): 3+4i is (3<<16, 4<<16). */
int main(void)
{
    axiom_matrix_t *m = malloc(sizeof(axiom_matrix_t));
    m->size = 1024;
    m->entries = calloc(m->size, sizeof(zxv_cq16_t));

    rational_t r = {1, 1};
    phase_t p = {Q16_ONE, 0};
    collapse_t c = {{1, 0}};
    const zxv_cq16_t v34 = cq16(3 * Q16_ONE, 4 * Q16_ONE);

    axiom_matrix_set(m, 1, r, TRIT_TRUE, p, c, v34);
    zxv_cq16_t v = axiom_matrix_get(m, 1, r, TRIT_TRUE, p, c);
    assert(cq16_eq(v, v34));
    assert(cq16_abs(v) == (uint64_t)5 * Q16_ONE); /* |3+4i| = 5, exact */

    zxv_cq16_t proj = axiom_matrix_project_tick(m, 1);
    assert(cq16_eq(proj, v34));

    /* a second, real entry (a different key: the ordinal is shifted out of
     * the 20-bit hash, so the collapse bits tell it apart) adds into the
     * projection */
    collapse_t c2 = {{2, 0}};
    axiom_matrix_set(m, 2, r, TRIT_TRUE, p, c2, cq16(Q16_ONE / 2, 0));
    proj = axiom_matrix_project_tick(m, 1);
    assert(proj.re == 3 * Q16_ONE + Q16_ONE / 2 && proj.im == 4 * Q16_ONE);

    bool sym = axiom_matrix_is_symmetric(m, ISOMETRY_IDENTITY);
    assert(!sym); /* 3+4i is not self-conjugate */

    axiom_matrix_t *m2 = malloc(sizeof(axiom_matrix_t));
    m2->size = 4;
    m2->entries = calloc(m2->size, sizeof(zxv_cq16_t));
    m2->entries[0] = cq16(1 * Q16_ONE, 2 * Q16_ONE);
    assert(!axiom_matrix_is_symmetric(m2, ISOMETRY_IDENTITY));
    assert(!axiom_matrix_is_symmetric(m2, ISOMETRY_NEGATE_PHASE));
    m2->entries[0] = cq16(1 * Q16_ONE, 0);
    assert(axiom_matrix_is_symmetric(m2, ISOMETRY_IDENTITY));
    assert(axiom_matrix_is_symmetric(m2, ISOMETRY_NEGATE_PHASE));
    assert(!axiom_matrix_is_symmetric(m2, ISOMETRY_SWAP_R_L));
    m2->entries[3] = cq16(1 * Q16_ONE, 0);
    assert(axiom_matrix_is_symmetric(m2, ISOMETRY_SWAP_R_L));

    free(m->entries);
    free(m);
    free(m2->entries);
    free(m2);
    printf("All Axiom Matrix tests passed\n");
    return 0;
}
