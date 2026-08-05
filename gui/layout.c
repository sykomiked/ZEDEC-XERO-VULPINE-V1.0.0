#include "layout.h"

void layout_resolve(layout_rect_t rel, int32_t parent_x, int32_t parent_y, int32_t parent_w, int32_t parent_h, int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h) {
    *out_x = parent_x + (rel.x.den == 0 ? 0 : (int32_t)(((int64_t)parent_w * rel.x.num) / rel.x.den));
    *out_y = parent_y + (rel.y.den == 0 ? 0 : (int32_t)(((int64_t)parent_h * rel.y.num) / rel.y.den));
    *out_w = rel.w.den == 0 ? 0 : (int32_t)(((int64_t)parent_w * rel.w.num) / rel.w.den);
    *out_h = rel.h.den == 0 ? 0 : (int32_t)(((int64_t)parent_h * rel.h.num) / rel.h.den);
}

void layout_grid_cell(int32_t parent_x, int32_t parent_y, int32_t parent_w, int32_t parent_h, uint32_t cols, uint32_t rows, uint32_t col, uint32_t row, int32_t gap, int32_t *out_x, int32_t *out_y, int32_t *out_w, int32_t *out_h) {
    if (cols == 0 || rows == 0) {
        *out_x = parent_x;
        *out_y = parent_y;
        *out_w = parent_w;
        *out_h = parent_h;
        return;
    }

    col = col < cols ? col : cols - 1;
    row = row < rows ? row : rows - 1;

    int32_t cell_w = (parent_w - gap * (cols - 1)) / cols;
    int32_t cell_h = (parent_h - gap * (rows - 1)) / rows;

    *out_x = parent_x + col * (cell_w + gap);
    *out_y = parent_y + row * (cell_h + gap);
    *out_w = cell_w;
    *out_h = cell_h;
}