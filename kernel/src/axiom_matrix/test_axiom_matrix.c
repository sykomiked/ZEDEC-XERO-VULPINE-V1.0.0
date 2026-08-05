#include "axiom_matrix_core.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    axiom_matrix_t *m = malloc(sizeof(axiom_matrix_t));
    m->size = 1024;
    m->entries = calloc(m->size, sizeof(double complex));

    rational_t r = {1, 1};
    phase_t p = {1.0, 0.0};
    collapse_t c = {{1, 0}};

    axiom_matrix_set(m, 1, r, TRIT_TRUE, p, c, 3.0 + 4.0 * I);
    double complex v = axiom_matrix_get(m, 1, r, TRIT_TRUE, p, c);
    assert(v == 3.0 + 4.0 * I);

    double complex proj = axiom_matrix_project_tick(m, 1);
    assert(proj == 3.0 + 4.0 * I);

    bool sym = axiom_matrix_is_symmetric(m, ISOMETRY_IDENTITY);
    (void)sym;

    axiom_matrix_t *m2 = malloc(sizeof(axiom_matrix_t));
    m2->size = 4;
    m2->entries = calloc(m2->size, sizeof(double complex));
    m2->entries[0] = 1.0 + 2.0 * I;
    assert(!axiom_matrix_is_symmetric(m2, ISOMETRY_IDENTITY));

    free(m->entries);
    free(m);
    free(m2->entries);
    free(m2);
    printf("All Axiom Matrix tests passed\n");
    return 0;
}
