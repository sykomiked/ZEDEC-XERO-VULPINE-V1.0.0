/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_qr.c — host tests for the QR encoder (qr.h).
 *
 * Reference symbols: qr_fixtures.h, on which python-qrcode and Project
 * Nayuki's qrcodegen agree module for module and which the zxing-cpp
 * decoder reads back (gen_qr_fixtures.py). The automatic mask choices are
 * qrcodegen's. Known answers from ISO/IEC
 * 18004: the level-M format-information words (Annex C, Table C.1), the
 * version-information words for versions 7-10 (Annex D, Table D.1) and the
 * byte-mode capacities at level M (Table 7). */
#include "qr.h"
#include "qr_fixtures.h"
#include <stdio.h>
#include <string.h>

static int checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (c) {                                                                                   \
            printf("[PASS] ");                                                                     \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        } else {                                                                                   \
            printf("[FAIL] ");                                                                     \
            printf(__VA_ARGS__);                                                                   \
            printf(" (line %d)\n", __LINE__);                                                      \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static unsigned hexval(char c)
{
    return (unsigned) (c <= '9' ? c - '0' : c - 'a' + 10);
}

static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++)
        out[i] = (uint8_t) (hexval(h[2 * i]) << 4 | hexval(h[2 * i + 1]));
    return n;
}

static int matrix_matches(const qr_code_t *q, const char *rows_hex)
{
    unsigned rb = (q->size + 7u) / 8u;
    if (strlen(rows_hex) != (size_t) rb * q->size * 2u) return 0;
    for (int y = 0; y < q->size; y++)
        for (int x = 0; x < q->size; x++) {
            const char *p = rows_hex + 2u * (rb * (unsigned) y + (unsigned) x / 8u);
            unsigned byte = hexval(p[0]) << 4 | hexval(p[1]);
            int want = (byte >> (7 - x % 8)) & 1;
            if (want != qr_module(q, x, y)) return 0;
        }
    return 1;
}

static unsigned read_format(const qr_code_t *q)
{
    static const int xs[15][2] = {{0, 8}, {1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}, {7, 8}, {8, 8},
                                  {8, 7}, {8, 5}, {8, 4}, {8, 3}, {8, 2}, {8, 1}, {8, 0}};
    unsigned v = 0;
    for (int i = 0; i < 15; i++) v = v << 1 | (unsigned) qr_module(q, xs[i][0], xs[i][1]);
    return v;
}

static unsigned read_format2(const qr_code_t *q)
{
    unsigned v = 0;
    int s = q->size;
    for (int i = 0; i < 7; i++) v = v << 1 | (unsigned) qr_module(q, 8, s - 1 - i);
    for (int i = 0; i < 8; i++) v = v << 1 | (unsigned) qr_module(q, s - 8 + i, 8);
    return v;
}

static unsigned read_version(const qr_code_t *q, int which)
{
    unsigned v = 0;
    for (int i = 17; i >= 0; i--) {
        int a = q->size - 11 + i % 3, b = i / 3;
        v = v << 1 | (unsigned) (which ? qr_module(q, b, a) : qr_module(q, a, b));
    }
    return v;
}

int main(void)
{
    static qr_code_t q;
    static uint8_t data[512];
    static char buf[40000];

    /* ---- reference symbols ---- */
    for (int i = 0; i < QR_N_FIXTURES; i++) {
        const qr_fixture_t *f = &QR_FIXTURES[i];
        size_t n = unhex(f->data_hex, data);
        qr_err_t e = qr_encode_bytes(&q, data, n, f->version_req, f->mask_req);
        CHECK(e == QR_OK && n == f->len && q.version == f->version && q.mask == f->mask &&
                  matrix_matches(&q, f->rows_hex),
              "fixture %2d: %3u bytes, version %2d, mask %d%s matches the reference symbol module "
              "for module",
              i, f->len, f->version, f->mask, f->mask_req < 0 ? " (auto)" : "");
    }

    /* ---- capacities (ISO/IEC 18004 Table 7, byte mode, level M) ---- */
    {
        static const unsigned cap[11] = {0, 14, 26, 42, 62, 84, 106, 122, 152, 180, 213};
        int ok = qr_capacity(0) == 0 && qr_capacity(11) == 0 && qr_capacity(-1) == 0;
        for (int v = 1; v <= 10; v++) ok &= qr_capacity(v) == cap[v];
        CHECK(ok, "byte capacities at level M for versions 1-10 match the standard");
        int vok = 1;
        for (int v = 1; v <= 10; v++) {
            memset(data, 'a', 256);
            vok &= qr_encode_bytes(&q, data, cap[v], 0, 0) == QR_OK && q.version == v &&
                   q.size == 17 + 4 * v;
            if (v < 10)
                vok &= qr_encode_bytes(&q, data, cap[v] + 1, 0, 0) == QR_OK && q.version == v + 1;
        }
        CHECK(vok, "smallest version chosen at every capacity boundary");
        CHECK(qr_encode_bytes(&q, data, 214, 0, 0) == QR_E_TOO_LONG &&
                  qr_encode_bytes(&q, data, 213, 0, 0) == QR_OK,
              "214 bytes: QR_E_TOO_LONG; 213 bytes fit version 10");
        CHECK(qr_encode_bytes(&q, data, 5, 4, 0) == QR_OK && q.version == 4,
              "a minimum version is honoured");
    }

    /* ---- format and version information (Annex C and D) ---- */
    {
        static const unsigned fmt[8] = {0x5412, 0x5125, 0x5E7C, 0x5B4B,
                                        0x45F9, 0x40CE, 0x4F97, 0x4AA0};
        int ok = 1;
        for (int m = 0; m < 8; m++) {
            qr_encode_bytes(&q, (const uint8_t *) "x", 1, 0, m);
            ok &= read_format(&q) == fmt[m] && read_format2(&q) == fmt[m] &&
                  qr_module(&q, 8, q.size - 8) == 1;
        }
        CHECK(ok, "both copies of the format word equal Table C.1 for level M, masks 0-7; dark "
                  "module set");
        static const unsigned ver[4] = {0x07C94, 0x085BC, 0x09A99, 0x0A4D3};
        ok = 1;
        for (int v = 7; v <= 10; v++) {
            qr_encode_bytes(&q, (const uint8_t *) "x", 1, v, 0);
            ok &= read_version(&q, 0) == ver[v - 7] && read_version(&q, 1) == ver[v - 7];
        }
        CHECK(ok, "both copies of the version word equal Table D.1 for versions 7-10");
    }

    /* ---- arguments ---- */
    CHECK(qr_encode_bytes(NULL, data, 1, 0, 0) == QR_E_ARG &&
              qr_encode_bytes(&q, NULL, 1, 0, 0) == QR_E_ARG &&
              qr_encode_bytes(&q, data, 1, 11, 0) == QR_E_VERSION &&
              qr_encode_bytes(&q, data, 1, -2, 0) == QR_E_VERSION &&
              qr_encode_bytes(&q, data, 1, 0, 8) == QR_E_MASK &&
              qr_encode_bytes(&q, data, 1, 0, -2) == QR_E_MASK,
          "NULL code or data, version 11 or -2, mask 8 or -2 refused");

    /* ---- rendering ---- */
    {
        qr_encode_bytes(&q, (const uint8_t *) "hello world", 11, 0, QR_MASK_AUTO);
        size_t need = qr_render_text(&q, 4, NULL, 0);
        CHECK(need == 29u * (29u * 2u + 1u), "text size: 29 rows of 58 chars + newline");
        memset(buf, 'Z', sizeof buf);
        CHECK(qr_render_text(&q, 4, buf, need) == need && buf[0] == 'Z',
              "text: a buffer without room for the NUL is left untouched");
        qr_render_text(&q, 4, buf, sizeof buf);
        int ok = buf[need] == '\0' && buf[58] == '\n';
        for (int y = 0; y < 29 && ok; y++)
            for (int x = 0; x < 29; x++) {
                int d = qr_module(&q, x - 4, y - 4);
                const char *c = buf + y * 59 + x * 2;
                if (c[0] != (d ? '#' : ' ') || c[1] != c[0]) ok = 0;
            }
        CHECK(ok, "text: every module drawn as \"##\" or \"  \" with a 4-module quiet zone");

        size_t sl = qr_render_svg(&q, 4, 8, buf, sizeof buf);
        int dark = 0;
        for (int y = 0; y < q.size; y++)
            for (int x = 0; x < q.size; x++) dark += qr_module(&q, x, y);
        int sum = 0;
        for (const char *p = buf; (p = strchr(p, 'h')) != NULL; p++) {
            if (p[1] < '0' || p[1] > '9') continue;
            int v = 0;
            for (const char *r = p + 1; *r >= '0' && *r <= '9'; r++) v = v * 10 + (*r - '0');
            sum += v;
        }
        static const char head[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"232\" "
                                   "height=\"232\" viewBox=\"0 0 29 29\"";
        CHECK(sl == strlen(buf) && strncmp(buf, head, sizeof head - 1) == 0 &&
                  strstr(buf, "</svg>\n") == buf + sl - 7,
              "svg: 232 px for 29 modules at scale 8, well-formed ending");
        CHECK(sum == dark && dark > 0,
              "svg: dark runs in the path cover exactly the %d dark modules", dark);
        CHECK(qr_render_svg(&q, 4, 0, buf, sizeof buf) == 0 &&
                  qr_render_svg(NULL, 4, 1, buf, 10) == 0,
              "svg: scale 0 or NULL code refused");
        CHECK(qr_module(&q, -1, 0) == 0 && qr_module(&q, 0, q.size) == 0 &&
                  qr_module(&q, 0, 0) == 1,
              "qr_module: outside is light, the finder corner is dark");
    }

    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "OK", checks, fails);
    return fails ? 1 : 0;
}
