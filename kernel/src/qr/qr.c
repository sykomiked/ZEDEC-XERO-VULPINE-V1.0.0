/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qr.c — QR Code encoder, byte mode, level M, versions 1-10. See qr.h.
 * The layout follows ISO/IEC 18004 section by section: function patterns,
 * data encoding, Reed-Solomon blocks, module placement, masking and the
 * format/version information. Freestanding C11, integer only. */
#include "qr.h"

/* Level M, index = version. ISO/IEC 18004 Table 9. */
static const uint8_t ECC_PER_BLOCK[QR_VERSION_MAX + 1] = {0,  10, 16, 26, 18, 24,
                                                          16, 18, 22, 22, 26};
static const uint8_t NUM_BLOCKS[QR_VERSION_MAX + 1] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
/* Alignment pattern centres (Annex E), second coordinate onward. */
static const uint8_t ALIGN[QR_VERSION_MAX + 1][3] = {
    {0, 0, 0},  {0, 0, 0},   {18, 0, 0},  {22, 0, 0},  {26, 0, 0},  {30, 0, 0},
    {34, 0, 0}, {22, 38, 0}, {24, 42, 0}, {26, 46, 0}, {28, 50, 0},
};

#define MAX_CW    346u /* raw codewords at version 10 */
#define MAX_BLOCK 128u

/* ===== sizes ========================================================== */

static unsigned raw_modules(unsigned v)
{
    unsigned r = (16u * v + 128u) * v + 64u;
    if (v >= 2) {
        unsigned na = v / 7u + 2u;
        r -= (25u * na - 10u) * na - 55u;
        if (v >= 7) r -= 36u;
    }
    return r;
}

static unsigned data_codewords(unsigned v)
{
    return raw_modules(v) / 8u - (unsigned) ECC_PER_BLOCK[v] * NUM_BLOCKS[v];
}

static unsigned count_bits(unsigned v)
{
    return v <= 9 ? 8u : 16u;
}

unsigned qr_capacity(int version)
{
    if (version < 1 || version > QR_VERSION_MAX) return 0;
    unsigned v = (unsigned) version;
    return (data_codewords(v) * 8u - 4u - count_bits(v)) / 8u;
}

/* ===== Reed-Solomon =================================================== */

static uint8_t gf_mul(uint8_t x, uint8_t y)
{
    unsigned z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11Du);
        z ^= ((unsigned) (y >> i) & 1u) * x;
    }
    return (uint8_t) z;
}

/* Generator polynomial of the given degree, leading 1 omitted, highest first. */
static void rs_generator(unsigned degree, uint8_t gen[MAX_BLOCK])
{
    if (degree == 0 || degree > MAX_BLOCK) return; /* never: degree is 10..26 */
    for (unsigned i = 0; i < degree; i++) gen[i] = 0;
    gen[degree - 1] = 1;
    uint8_t root = 1;
    for (unsigned i = 0; i < degree; i++) {
        for (unsigned j = 0; j < degree; j++) {
            gen[j] = gf_mul(gen[j], root);
            if (j + 1 < degree) gen[j] ^= gen[j + 1];
        }
        root = gf_mul(root, 0x02);
    }
}

static void rs_remainder(const uint8_t *data, unsigned len, const uint8_t *gen, unsigned degree,
                         uint8_t *out)
{
    for (unsigned i = 0; i < degree; i++) out[i] = 0;
    for (unsigned k = 0; k < len; k++) {
        uint8_t f = (uint8_t) (data[k] ^ out[0]);
        for (unsigned i = 0; i + 1 < degree; i++) out[i] = out[i + 1];
        out[degree - 1] = 0;
        for (unsigned i = 0; i < degree; i++) out[i] ^= gf_mul(gen[i], f);
    }
}

/* ===== function patterns ============================================== */

static void set_fn(qr_code_t *qr, int x, int y, int dark)
{
    if (x < 0 || y < 0 || x >= qr->size || y >= qr->size || qr->size > QR_SIZE_MAX) return;
    unsigned i = (unsigned) y * qr->size + (unsigned) x;
    qr->mod[i] = (uint8_t) (dark ? 1 : 0);
    qr->fn[i] = 1;
}

static int iabs(int a)
{
    return a < 0 ? -a : a;
}

static void draw_finder(qr_code_t *qr, int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= qr->size || y >= qr->size) continue;
            int d = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
            set_fn(qr, x, y, d != 2 && d != 4);
        }
}

static void draw_alignment(qr_code_t *qr, int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int d = iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy);
            set_fn(qr, cx + dx, cy + dy, d != 1);
        }
}

static void draw_format(qr_code_t *qr, unsigned mask)
{
    /* level M = 00 */
    unsigned data = (0u << 3) | mask;
    unsigned rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537u);
    unsigned bits = ((data << 10) | rem) ^ 0x5412u;
    int s = qr->size;
    for (int i = 0; i <= 5; i++) set_fn(qr, 8, i, (bits >> i) & 1u);
    set_fn(qr, 8, 7, (bits >> 6) & 1u);
    set_fn(qr, 8, 8, (bits >> 7) & 1u);
    set_fn(qr, 7, 8, (bits >> 8) & 1u);
    for (int i = 9; i < 15; i++) set_fn(qr, 14 - i, 8, (bits >> i) & 1u);
    for (int i = 0; i < 8; i++) set_fn(qr, s - 1 - i, 8, (bits >> i) & 1u);
    for (int i = 8; i < 15; i++) set_fn(qr, 8, s - 15 + i, (bits >> i) & 1u);
    set_fn(qr, 8, s - 8, 1); /* the dark module */
}

static void draw_version(qr_code_t *qr)
{
    if (qr->version < 7) return;
    unsigned rem = qr->version;
    for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25u);
    unsigned long bits = ((unsigned long) qr->version << 12) | rem;
    for (int i = 0; i < 18; i++) {
        int bit = (int) ((bits >> i) & 1u);
        int a = qr->size - 11 + i % 3, b = i / 3;
        set_fn(qr, a, b, bit);
        set_fn(qr, b, a, bit);
    }
}

static void draw_function_patterns(qr_code_t *qr)
{
    int s = qr->size;
    for (int i = 0; i < s; i++) {
        set_fn(qr, 6, i, i % 2 == 0);
        set_fn(qr, i, 6, i % 2 == 0);
    }
    draw_finder(qr, 3, 3);
    draw_finder(qr, s - 4, 3);
    draw_finder(qr, 3, s - 4);
    int pos[4], n = 0;
    if (qr->version >= 2) {
        pos[n++] = 6;
        for (int i = 0; i < 3 && ALIGN[qr->version][i]; i++) pos[n++] = ALIGN[qr->version][i];
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0)) continue;
            draw_alignment(qr, pos[i], pos[j]);
        }
    draw_format(qr, 0);
    draw_version(qr);
}

/* ===== data =========================================================== */

static void put_bits(uint8_t *buf, unsigned *bitlen, unsigned val, unsigned n)
{
    for (int i = (int) n - 1; i >= 0; i--) {
        if ((val >> i) & 1u) buf[*bitlen >> 3] |= (uint8_t) (0x80u >> (*bitlen & 7u));
        (*bitlen)++;
    }
}

static void place_codewords(qr_code_t *qr, const uint8_t *cw, unsigned n)
{
    unsigned i = 0;
    int s = qr->size;
    for (int right = s - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < s; vert++)
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                int up = ((right + 1) & 2) == 0;
                int y = up ? s - 1 - vert : vert;
                unsigned k = (unsigned) y * (unsigned) s + (unsigned) x;
                if (qr->fn[k]) continue;
                if (i < n * 8u) {
                    qr->mod[k] = (uint8_t) ((cw[i >> 3] >> (7u - (i & 7u))) & 1u);
                    i++;
                } /* remainder bits stay light */
            }
    }
}

static int mask_bit(unsigned m, unsigned x, unsigned y)
{
    switch (m) {
    case 0:
        return (x + y) % 2u == 0;
    case 1:
        return y % 2u == 0;
    case 2:
        return x % 3u == 0;
    case 3:
        return (x + y) % 3u == 0;
    case 4:
        return (x / 3u + y / 2u) % 2u == 0;
    case 5:
        return x * y % 2u + x * y % 3u == 0;
    case 6:
        return (x * y % 2u + x * y % 3u) % 2u == 0;
    default:
        return ((x + y) % 2u + x * y % 3u) % 2u == 0;
    }
}

static void apply_mask(qr_code_t *qr, unsigned m)
{
    unsigned s = qr->size;
    for (unsigned y = 0; y < s; y++)
        for (unsigned x = 0; x < s; x++) {
            unsigned k = y * s + x;
            if (!qr->fn[k] && mask_bit(m, x, y)) qr->mod[k] ^= 1u;
        }
}

/* ===== penalty (N1 runs, N2 boxes, N3 finder-like, N4 balance) ========= */

static void hist_add(int run, int h[7], int size)
{
    if (h[0] == 0) run += size; /* the light border before the first run */
    for (int i = 6; i > 0; i--) h[i] = h[i - 1];
    h[0] = run;
}

static int hist_count(const int h[7])
{
    int n = h[1];
    int core = n > 0 && h[2] == n && h[3] == n * 3 && h[4] == n && h[5] == n;
    return (core && h[0] >= n * 4 && h[6] >= n ? 1 : 0) +
           (core && h[6] >= n * 4 && h[0] >= n ? 1 : 0);
}

static int hist_end(int color, int run, int h[7], int size)
{
    if (color) {
        hist_add(run, h, size);
        run = 0;
    }
    run += size;
    hist_add(run, h, size);
    return hist_count(h);
}

static long penalty(const qr_code_t *qr)
{
    int s = qr->size;
    long score = 0;
    for (int pass = 0; pass < 2; pass++) /* 0 = rows, 1 = columns */
        for (int a = 0; a < s; a++) {
            int color = 0, run = 0, h[7] = {0, 0, 0, 0, 0, 0, 0};
            for (int b = 0; b < s; b++) {
                int m = pass == 0 ? qr->mod[a * s + b] : qr->mod[b * s + a];
                if (m == color) {
                    run++;
                    if (run == 5)
                        score += 3;
                    else if (run > 5)
                        score++;
                } else {
                    hist_add(run, h, s);
                    if (!color) score += hist_count(h) * 40;
                    color = m;
                    run = 1;
                }
            }
            score += hist_end(color, run, h, s) * 40;
        }
    for (int y = 0; y + 1 < s; y++)
        for (int x = 0; x + 1 < s; x++) {
            int c = qr->mod[y * s + x];
            if (c == qr->mod[y * s + x + 1] && c == qr->mod[(y + 1) * s + x] &&
                c == qr->mod[(y + 1) * s + x + 1])
                score += 3;
        }
    int dark = 0, total = s * s;
    for (int i = 0; i < total; i++) dark += qr->mod[i];
    int diff = dark * 20 - total * 10;
    if (diff < 0) diff = -diff;
    int k = (diff + total - 1) / total - 1;
    score += (long) k * 10;
    return score;
}

/* ===== encode ========================================================= */

qr_err_t qr_encode_bytes(qr_code_t *qr, const uint8_t *data, size_t len, int min_version, int mask)
{
    if (!qr || (!data && len)) return QR_E_ARG;
    if (min_version == 0) min_version = 1;
    if (min_version < 1 || min_version > QR_VERSION_MAX) return QR_E_VERSION;
    if (mask != QR_MASK_AUTO && (mask < 0 || mask > 7)) return QR_E_MASK;
    unsigned v = (unsigned) min_version;
    while (v <= QR_VERSION_MAX && len > qr_capacity((int) v)) v++;
    if (v > QR_VERSION_MAX) return QR_E_TOO_LONG;

    /* data codewords (Q2) */
    uint8_t cw[MAX_CW];
    for (unsigned i = 0; i < MAX_CW; i++) cw[i] = 0;
    unsigned dcw = data_codewords(v), bl = 0;
    put_bits(cw, &bl, 4u, 4);
    put_bits(cw, &bl, (unsigned) len, count_bits(v));
    for (size_t i = 0; i < len; i++) put_bits(cw, &bl, data[i], 8);
    unsigned cap_bits = dcw * 8u;
    unsigned term = cap_bits - bl < 4u ? cap_bits - bl : 4u;
    put_bits(cw, &bl, 0, term);
    put_bits(cw, &bl, 0, (8u - bl % 8u) % 8u);
    for (uint8_t pad = 0xEC; bl < cap_bits; pad ^= 0xEC ^ 0x11) put_bits(cw, &bl, pad, 8);

    /* blocks and interleaving (Q3) */
    unsigned nb = NUM_BLOCKS[v], ecl = ECC_PER_BLOCK[v], raw = raw_modules(v) / 8u;
    unsigned nshort = nb - raw % nb, short_len = raw / nb;
    uint8_t blocks[5][MAX_BLOCK], gen[MAX_BLOCK];
    rs_generator(ecl, gen);
    for (unsigned i = 0, k = 0; i < nb; i++) {
        unsigned dl = short_len - ecl + (i < nshort ? 0u : 1u);
        for (unsigned j = 0; j < dl; j++) blocks[i][j] = cw[k + j];
        if (i < nshort) blocks[i][dl] = 0; /* placeholder, skipped below */
        rs_remainder(cw + k, dl, gen, ecl, &blocks[i][short_len - ecl + 1u]);
        k += dl;
    }
    uint8_t out[MAX_CW];
    unsigned o = 0;
    for (unsigned i = 0; i < short_len + 1u; i++)
        for (unsigned j = 0; j < nb; j++)
            if (i != short_len - ecl || j >= nshort) out[o++] = blocks[j][i];

    /* layout */
    qr->version = (uint8_t) v;
    qr->size = (uint8_t) (17u + 4u * v);
    for (unsigned i = 0; i < sizeof qr->mod; i++) qr->mod[i] = qr->fn[i] = 0;
    draw_function_patterns(qr);
    place_codewords(qr, out, o);

    /* mask (Q4) */
    if (mask == QR_MASK_AUTO) {
        long best = -1;
        for (unsigned m = 0; m < 8; m++) {
            apply_mask(qr, m);
            draw_format(qr, m);
            long p = penalty(qr);
            if (best < 0 || p < best) {
                best = p;
                mask = (int) m;
            }
            apply_mask(qr, m); /* undo (XOR) */
        }
    }
    qr->mask = (uint8_t) mask;
    apply_mask(qr, (unsigned) mask);
    draw_format(qr, (unsigned) mask);
    return QR_OK;
}

int qr_module(const qr_code_t *qr, int x, int y)
{
    if (!qr || x < 0 || y < 0 || x >= qr->size || y >= qr->size) return 0;
    return qr->mod[y * qr->size + x];
}

/* ===== rendering ====================================================== */

typedef struct {
    char *out;
    size_t cap, len;
    int write;
} sink_t;

static void s_putc(sink_t *s, char c)
{
    if (s->write) s->out[s->len] = c;
    s->len++;
}

static void s_puts(sink_t *s, const char *t)
{
    while (*t) s_putc(s, *t++);
}

static void s_putu(sink_t *s, unsigned v)
{
    char b[10];
    int n = 0;
    do {
        b[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n) s_putc(s, b[--n]);
}

static void text_pass(const qr_code_t *qr, unsigned quiet, sink_t *s)
{
    int n = qr->size + 2 * (int) quiet;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            int d = qr_module(qr, x - (int) quiet, y - (int) quiet);
            s_puts(s, d ? "##" : "  ");
        }
        s_putc(s, '\n');
    }
}

static void svg_pass(const qr_code_t *qr, unsigned quiet, unsigned scale, sink_t *s)
{
    unsigned n = qr->size + 2u * quiet;
    s_puts(s, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"");
    s_putu(s, n * scale);
    s_puts(s, "\" height=\"");
    s_putu(s, n * scale);
    s_puts(s, "\" viewBox=\"0 0 ");
    s_putu(s, n);
    s_putc(s, ' ');
    s_putu(s, n);
    s_puts(s, "\" shape-rendering=\"crispEdges\"><rect width=\"");
    s_putu(s, n);
    s_puts(s, "\" height=\"");
    s_putu(s, n);
    s_puts(s, "\" fill=\"#fff\"/><path fill=\"#000\" d=\"");
    for (int y = 0; y < qr->size; y++)
        for (int x = 0; x < qr->size;) {
            if (!qr_module(qr, x, y)) {
                x++;
                continue;
            }
            int run = 0;
            while (x + run < qr->size && qr_module(qr, x + run, y)) run++;
            s_putc(s, 'M');
            s_putu(s, (unsigned) x + quiet);
            s_putc(s, ' ');
            s_putu(s, (unsigned) y + quiet);
            s_putc(s, 'h');
            s_putu(s, (unsigned) run);
            s_puts(s, "v1h-");
            s_putu(s, (unsigned) run);
            s_putc(s, 'z');
            x += run;
        }
    s_puts(s, "\"/></svg>\n");
}

size_t qr_render_text(const qr_code_t *qr, unsigned quiet, char *out, size_t cap)
{
    if (!qr || qr->size == 0) return 0;
    sink_t s = {out, cap, 0, 0};
    text_pass(qr, quiet, &s);
    size_t need = s.len;
    if (out && cap > need) {
        sink_t w = {out, cap, 0, 1};
        text_pass(qr, quiet, &w);
        out[need] = '\0';
    }
    return need;
}

size_t qr_render_svg(const qr_code_t *qr, unsigned quiet, unsigned scale, char *out, size_t cap)
{
    if (!qr || qr->size == 0 || scale == 0) return 0;
    sink_t s = {out, cap, 0, 0};
    svg_pass(qr, quiet, scale, &s);
    size_t need = s.len;
    if (out && cap > need) {
        sink_t w = {out, cap, 0, 1};
        svg_pass(qr, quiet, scale, &w);
        out[need] = '\0';
    }
    return need;
}
