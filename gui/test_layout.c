#include <stdio.h>
#include "layout.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static rational_t R(int64_t n, int64_t d) { rational_t r = {n, d}; return r; }

int main(void) {
    printf("=== layout_resolve ===\n");
    {
        layout_rect_t rel = { R(1, 4), R(1, 2), R(1, 2), R(1, 4) }; /* x=25%, y=50%, w=50%, h=25% */
        int32_t x, y, w, h;
        layout_resolve(rel, 100, 200, 400, 800, &x, &y, &w, &h);
        CHECK(x == 100 + 100, "x = parent_x + 25% of parent_w (100 + 400*1/4 = 200)");
        CHECK(y == 200 + 400, "y = parent_y + 50% of parent_h (200 + 800*1/2 = 600)");
        CHECK(w == 200, "w = 50% of parent_w (400*1/2 = 200)");
        CHECK(h == 200, "h = 25% of parent_h (800*1/4 = 200)");
    }
    {
        /* Full-size rect (0,0,1,1) should exactly reproduce the parent bounds. */
        layout_rect_t full = { R(0, 1), R(0, 1), R(1, 1), R(1, 1) };
        int32_t x, y, w, h;
        layout_resolve(full, 10, 20, 300, 400, &x, &y, &w, &h);
        CHECK(x == 10 && y == 20 && w == 300 && h == 400, "full (0,0,1,1) rect reproduces exact parent bounds");
    }
    {
        /* Malformed rational (den=0) must not crash and should contribute 0. */
        layout_rect_t bad = { R(1, 0), R(0, 1), R(1, 2), R(1, 2) };
        int32_t x, y, w, h;
        layout_resolve(bad, 0, 0, 100, 100, &x, &y, &w, &h);
        CHECK(x == 0, "malformed x fraction (den=0) contributes 0, does not crash");
    }

    printf("\n=== layout_grid_cell ===\n");
    {
        int32_t x, y, w, h;
        layout_grid_cell(0, 0, 220, 220, 4, 4, 0, 0, 5, &x, &y, &w, &h);
        CHECK(w == 51, "4-col grid with gap=5 over width 220: cell_w = (220-15)/4 = 51");
        layout_grid_cell(0, 0, 220, 220, 4, 4, 1, 0, 5, &x, &y, &w, &h);
        CHECK(x == 56, "column 1's x = 0 + 1*(51+5) = 56");
        layout_grid_cell(0, 0, 220, 220, 4, 4, 3, 3, 5, &x, &y, &w, &h);
        CHECK(x == 168 && y == 168, "last cell (col=3,row=3) x=y= 3*(51+5)=168");
    }
    {
        /* Degenerate cols=0 must not crash, returns full parent rect. */
        int32_t x, y, w, h;
        layout_grid_cell(5, 5, 100, 100, 0, 4, 0, 0, 2, &x, &y, &w, &h);
        CHECK(x == 5 && y == 5 && w == 100 && h == 100, "cols=0 returns the full parent rect, does not divide by zero");
    }
    {
        /* Out-of-range col/row must be clamped, not read/write OOB. */
        int32_t x, y, w, h;
        layout_grid_cell(0, 0, 100, 100, 4, 4, 99, 99, 0, &x, &y, &w, &h);
        CHECK(x == 75 && y == 75, "out-of-range col/row=99 is clamped to the last valid cell (3,3)");
    }

    if (failures == 0) printf("\n=== ALL LAYOUT TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
