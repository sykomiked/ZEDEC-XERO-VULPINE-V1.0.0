/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_kern.h — the per-pixel stages of the vectoriser (zui_vtrace.h), written
 * once in the subset that is C11, CUDA C++ and Metal Shading Language.
 *
 * One source, every backend, so a GPU result is the CPU's bits by
 * construction (integer arithmetic has one answer in any order):
 *   - zui_vtrace.c compiles it as C (ZVT_KSRC(...) -> ...) for the CPU
 *     reference backend;
 *   - zvt_cuda.c stringifies it (ZVT_KSRC(...) -> "...") into the source
 *     NVRTC compiles at run time; zvt_metal.m does the same for Metal;
 *   - zvt_vk.comp is the GLSL port, line for line (GLSL cannot share C
 *     source); test_zvt_gpu.c checks it against this file on any Vulkan
 *     device, lavapipe included.
 * Rules for the body: no preprocessor lines and no `#` (it is stringified);
 * pointers only as ZK_DEV; types zi (int32) zu (uint32) zb (uint8); no 64-bit
 * arithmetic (every sum here fits 32 bits for the caps zui_vtrace.h sets).
 *
 * The stages (one thread per output element):
 *   zk_smooth_px  edge-preserving 3x3 smoothing: a neighbour counts only when
 *                 every channel is within thr of the centre (weights 4/2/1).
 *   zk_quant_px   nearest palette entry by weighted squared RGB distance
 *                 (2, 4, 3), ties to the lower index; alpha < 128 -> 255
 *                 (transparent, never a palette index).
 *   zk_edge_px    bit 0: the right neighbour's index differs; bit 1: the
 *                 lower neighbour's differs (image edges count as same).
 *   zk_sse_row    sum over a row of dr^2 + dg^2 + db^2.
 *   zk_win        sums of x, y, x^2, y^2, xy of BT.601 luma over a window.
 *   zk_diff_row   pixels of a row whose largest channel difference > thr;
 *                 writes 1/0 per pixel to a mask and returns the count.
 * Not a header to include anywhere else. */

ZVT_KSRC(

    static inline zu zk_luma(zu c) {
        return (((c >> 16) & 255u) * 77u + ((c >> 8) & 255u) * 150u + (c & 255u) * 29u + 128u) >> 8;
    }

    static inline zu zk_absd(zu a, zu b) { return a > b ? a - b : b - a; }

    static inline zu zk_maxd(zu a, zu b) {
        zu r = zk_absd((a >> 16) & 255u, (b >> 16) & 255u);
        zu g = zk_absd((a >> 8) & 255u, (b >> 8) & 255u);
        zu c = zk_absd(a & 255u, b & 255u);
        zu m = r > g ? r : g;
        return m > c ? m : c;
    }

    static inline zu zk_smooth_px(ZK_DEV const zu *in, zu w, zu h, zu x, zu y, zu thr) {
        zu c = in[y * w + x];
        if ((c >> 24) < 128u) return c;
        zu sr = 0u, sg = 0u, sb = 0u, n = 0u;
        for (zi dy = -1; dy <= 1; dy++) {
            for (zi dx = -1; dx <= 1; dx++) {
                zi nx = (zi) x + dx, ny = (zi) y + dy;
                if (nx < 0 || ny < 0 || nx >= (zi) w || ny >= (zi) h) continue;
                zu q = in[(zu) ny * w + (zu) nx];
                if ((q >> 24) < 128u || zk_maxd(c, q) > thr) continue;
                zu k = (dx == 0 && dy == 0) ? 4u : (dx == 0 || dy == 0) ? 2u : 1u;
                sr += ((q >> 16) & 255u) * k;
                sg += ((q >> 8) & 255u) * k;
                sb += (q & 255u) * k;
                n += k;
            }
        }
        return (c & 0xFF000000u) | (((sr + n / 2u) / n) << 16) | (((sg + n / 2u) / n) << 8) |
               ((sb + n / 2u) / n);
    }

    static inline zu zk_quant_px(zu c, ZK_DEV const zu *pal, zu npal) {
        if ((c >> 24) < 128u) return 255u;
        zu best = 0u, bd = 0xFFFFFFFFu;
        for (zu i = 0u; i < npal; i++) {
            zu p = pal[i];
            zu dr = zk_absd((c >> 16) & 255u, (p >> 16) & 255u);
            zu dg = zk_absd((c >> 8) & 255u, (p >> 8) & 255u);
            zu db = zk_absd(c & 255u, p & 255u);
            zu d = 2u * dr * dr + 4u * dg * dg + 3u * db * db;
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        return best;
    }

    static inline zu zk_edge_px(zu c, zu right, zu down, zu has_right, zu has_down) {
        return (has_right != 0u && right != c ? 1u : 0u) | (has_down != 0u && down != c ? 2u : 0u);
    }

    static inline zu zk_sse_row(ZK_DEV const zu *a, ZK_DEV const zu *b, zu w, zu y) {
        zu s = 0u;
        for (zu x = 0u; x < w; x++) {
            zu p = a[y * w + x], q = b[y * w + x];
            zu dr = zk_absd((p >> 16) & 255u, (q >> 16) & 255u);
            zu dg = zk_absd((p >> 8) & 255u, (q >> 8) & 255u);
            zu db = zk_absd(p & 255u, q & 255u);
            s += dr * dr + dg * dg + db * db;
        }
        return s;
    }

    static inline void zk_win(ZK_DEV const zu *a, ZK_DEV const zu *b, zu w, zu x0, zu y0, zu wx,
                              zu wy, ZK_DEV zu *out) {
        zu sx = 0u, sy = 0u, sxx = 0u, syy = 0u, sxy = 0u;
        for (zu y = y0; y < y0 + wy; y++) {
            for (zu x = x0; x < x0 + wx; x++) {
                zu p = zk_luma(a[y * w + x]), q = zk_luma(b[y * w + x]);
                sx += p;
                sy += q;
                sxx += p * p;
                syy += q * q;
                sxy += p * q;
            }
        }
        out[0] = sx;
        out[1] = sy;
        out[2] = sxx;
        out[3] = syy;
        out[4] = sxy;
    }

    static inline zu zk_diff_row(ZK_DEV const zu *a, ZK_DEV const zu *b, zu w, zu y, zu thr,
                                 ZK_DEV zb *mask) {
        zu n = 0u;
        for (zu x = 0u; x < w; x++) {
            zu ch = zk_maxd(a[y * w + x], b[y * w + x]) > thr ? 1u : 0u;
            mask[y * w + x] = (zb) ch;
            n += ch;
        }
        return n;
    }

)
