/* layout.h — Exact-rational layout engine.
 * Widget positions/sizes are specified as EXACT rational fractions of
 * their parent's bounds (e.g. x=1/2 means "50% from the left edge"),
 * resolved to absolute pixels via integer arithmetic only -- no
 * floating point, matching the codebase's RMAG philosophy, and no
 * rounding drift when a window is resized and re-laid-out repeatedly.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ZXV_LAYOUT_H
#define ZXV_LAYOUT_H

#include <stdint.h>
#include "../kernel/include/m5_types.h"

typedef struct layout_rect {
    rational_t x, y, w, h; /* fractions of the parent's bounds, typically in [0,1] */
} layout_rect_t;

/* Resolves a fractional rect against an absolute parent rectangle,
 * writing absolute pixel coordinates into *out_x/*out_y/*out_w/*out_h. */
void layout_resolve(layout_rect_t rel, int32_t parent_x, int32_t parent_y,
                     int32_t parent_w, int32_t parent_h,
                     int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h);

/* Computes the absolute pixel rect of one cell in a uniform grid
 * (e.g. a calculator button grid), given a pixel gap between cells. */
void layout_grid_cell(int32_t parent_x, int32_t parent_y, int32_t parent_w, int32_t parent_h,
                       uint32_t cols, uint32_t rows, uint32_t col, uint32_t row, int32_t gap,
                       int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h);

#endif
