/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_golden.c — golden files:
 *   - test/golden/(collate,canonic,dates).m-expect were written by GT.M itself (make golden-m):
 *     its $ORDER of collate.in, its x=+x on canonic.in and its $ZDATE on
 *     chosen $H days. libzmyra must agree with every line.
 *   - schemas.expect pins the digest of each example schema, which both
 *     its global map and its MYRA file must have.
 *   - records.expect: nodes decoded with both forms of a schema, with the
 *     status, canonical value and field values each must give. */
#include "zm_test.h"

/* The non-comment lines of a file, in place (the buffer is returned). */
static char *lines_of(const char *path, char ***out, size_t *out_n) {
    size_t len = 0, n = 0, cap = 0;
    char *b = slurp(path, &len), *p;
    char **v = NULL;
    *out = NULL;
    *out_n = 0;
    if (!b) return NULL;
    p = b;
    while (p < b + len) {
        char *e = memchr(p, '\n', (size_t) (b + len - p));
        if (!e) e = b + len;
        *e = '\0';
        if (p[0] != '#') {
            if (n == cap) {
                char **nv = (char **) realloc(v, (cap = cap ? cap * 2u : 64u) * sizeof *nv);
                if (!nv) break;
                v = nv;
            }
            v[n++] = p;
        }
        p = e + 1;
    }
    *out = v;
    *out_n = n;
    return b;
}

static void canonic(void) {
    char **v;
    size_t n;
    char *b = lines_of("test/golden/canonic.m-expect", &v, &n);
    CHECK(b && n >= 50, "canonic.m-expect has %zu lines", n);
    for (size_t i = 0; i < n; i++) {
        int want = v[i][0] == '1';
        const char *x = v[i] + 2;
        CHECK((v[i][0] == '0' || v[i][0] == '1') && v[i][1] == ' ', "line format: %s", v[i]);
        CHECK(zm_is_canonic(x, strlen(x)) == want, "GT.M says '%s' is %scanonical", x, want ? "" : "not ");
    }
    free(v);
    free(b);
}

static void dates(void) {
    char **v;
    size_t n;
    char *b = lines_of("test/golden/dates.m-expect", &v, &n);
    CHECK(b && n >= 300, "dates.m-expect has %zu lines", n);
    for (size_t i = 0; i < n; i++) {
        long long h = -1;
        int y = 0, m = 0, d = 0;
        int32_t yy, mm, dd;
        int64_t back = -1;
        CHECK(sscanf(v[i], "%lld %d-%d-%d", &h, &y, &m, &d) == 4, "line format: %s", v[i]);
        CHECK(zm_date_from_h(h, &yy, &mm, &dd) == ZXV_OK && yy == y && mm == m && dd == d,
              "$H %lld is %04d-%02d-%02d", h, y, m, d);
        CHECK(zm_date_to_h(y, m, d, &back) == ZXV_OK && back == h, "%04d-%02d-%02d is $H %lld", y, m, d, h);
    }
    free(v);
    free(b);
}

static int line_cmp(const void *a, const void *b) {
    const char *x = *(const char *const *) a, *y = *(const char *const *) b;
    int32_t c = 0;
    zm_collate(x, strlen(x), y, strlen(y), &c);
    return c;
}

static void collation(void) {
    char **in, **m;
    size_t nin, nm, k = 0;
    char *bi = lines_of("test/golden/collate.in", &in, &nin);
    char *bm = lines_of("test/golden/collate.m-expect", &m, &nm);
    CHECK(bi && bm && nm >= 50, "collate files: %zu and %zu lines", nin, nm);
    if (!bi || !bm) return;
    /* GT.M's order is strictly increasing for libzmyra */
    for (size_t i = 1; i < nm; i++)
        CHECK(line_cmp(&m[i - 1], &m[i]) == -1, "GT.M puts '%s' before '%s'", m[i - 1], m[i]);
    /* and libzmyra's sort of the input, without duplicates, is GT.M's list */
    qsort(in, nin, sizeof *in, line_cmp);
    for (size_t i = 0; i < nin; i++) {
        if (!in[i][0] || (i && line_cmp(&in[i - 1], &in[i]) == 0)) continue;
        CHECK(k < nm && strcmp(in[i], m[k]) == 0, "position %zu: libzmyra '%s', GT.M '%s'", k,
              in[i], k < nm ? m[k] : "(end)");
        k++;
    }
    CHECK(k == nm, "%zu distinct subscripts, GT.M has %zu", k, nm);
    free(in);
    free(m);
    free(bi);
    free(bm);
}

static zm_schema *load(const char *path) {
    size_t len = 0;
    char *t = slurp(path, &len);
    zm_schema *s = NULL;
    zm_diag d;
    if (!t) {
        fprintf(stderr, "cannot read %s\n", path);
        return NULL;
    }
    if (zm_schema_parse(t, len, &s, &d) != ZXV_OK) fprintf(stderr, "%s: %s\n", path, d.msg);
    free(t);
    return s;
}

static void hexof(const uint8_t *d, char *out) {
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
}

static void schemas(void) {
    char **v;
    size_t n;
    char *b = lines_of("test/golden/schemas.expect", &v, &n);
    CHECK(b && n == 2, "schemas.expect: %zu lines", n);
    for (size_t i = 0; i < n; i++) {
        char name[64], want[80], path[160], got[65];
        uint8_t d[32];
        CHECK(sscanf(v[i], "%63s %79s", name, want) == 2, "line format");
        for (int form = 0; form < 2; form++) {
            zm_schema *s, *p;
            char text[16384];
            size_t len = 0;
            int32_t same = 0;
            snprintf(path, sizeof path, "examples/%s.%s", name, form ? "myra" : "gmap");
            s = load(path);
            CHECK(s != NULL, "%s parses", path);
            if (!s) continue;
            zm_schema_digest(s, d);
            hexof(d, got);
            CHECK(strcmp(got, want) == 0, "%s digest %s, pinned %s", path, got, want);
            /* both printers parse back to the same schema */
            zm_schema_gmap(s, text, sizeof text, &len);
            p = gmap(text);
            CHECK(p && zm_schema_same(s, p, &same) == ZXV_OK && same, "%s: printed map", path);
            zm_schema_destroy(p);
            zm_schema_myra(s, text, sizeof text, &len);
            p = myra(text);
            CHECK(p && zm_schema_same(s, p, &same) == ZXV_OK && same, "%s: canonical MYRA", path);
            zm_schema_destroy(p);
            zm_schema_destroy(s);
        }
    }
    free(v);
    free(b);
}

static int status_of(const char *name) {
    static const int32_t all[] = {ZXV_OK, ZM_E_SYNTAX, ZM_E_UNSUPPORTED, ZM_E_DATA, ZM_E_FIT, ZM_E_KEY};
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        if (strcmp(zm_status_name(all[i]), name) == 0) return all[i];
    return 1;
}

static void records(void) {
    char **v;
    size_t n;
    char *b = lines_of("test/golden/records.expect", &v, &n);
    zm_schema *pay[2], *stk[2];
    CHECK(b && n >= 30, "records.expect: %zu cases", n);
    pay[0] = load("examples/payment.gmap");
    pay[1] = load("examples/payment.myra");
    stk[0] = load("examples/stock.gmap");
    stk[1] = load("examples/stock.myra");
    for (size_t i = 0; i < n && pay[0] && pay[1] && stk[0] && stk[1]; i++) {
        char *f[6] = {0};
        int k = 0;
        f[k++] = v[i];
        for (char *c = v[i]; *c && k < 6; c++)
            if (*c == '|') {
                *c = '\0';
                f[k++] = c + 1;
            }
        CHECK(k == 4 || k == 6, "case %zu: %d fields", i, k);
        if (k != 4 && k != 6) continue;
        for (int form = 0; form < 2; form++) {
            zm_schema *s = strcmp(f[0], "payment") == 0 ? pay[form] : stk[form];
            zm_record *r = NULL;
            int32_t st = zm_node_decode(s, f[1], strlen(f[1]), f[2], strlen(f[2]), &r);
            CHECK(st == status_of(f[3]), "case %zu (%s) %s = %s: %s, want %s", i,
                  form ? "myra" : "gmap", f[1], f[2], zm_status_name(st), f[3]);
            if (st == ZXV_OK && r && k == 6) {
                char *fv = f[5];
                CHECK(strcmp(enc(r), f[4]) == 0, "case %zu canonical '%s', want '%s'", i, enc(r), f[4]);
                while (fv && *fv) {
                    char *semi = strchr(fv, ';'), *eq;
                    if (semi) *semi = '\0';
                    eq = strchr(fv, '=');
                    CHECK(eq != NULL, "case %zu field spec %s", i, fv);
                    if (eq) {
                        *eq = '\0';
                        CHECK(strcmp(val(r, fv), eq + 1) == 0, "case %zu %s = '%s', want '%s'", i, fv,
                              val(r, fv), eq + 1);
                        *eq = '=';
                    }
                    if (semi) *semi = ';';
                    fv = semi ? semi + 1 : NULL;
                }
            }
            zm_record_destroy(r);
        }
    }
    for (int i = 0; i < 2; i++) {
        zm_schema_destroy(pay[i]);
        zm_schema_destroy(stk[i]);
    }
    free(v);
    free(b);
}

int main(void) {
    canonic();
    dates();
    collation();
    schemas();
    records();
    return report("test_golden");
}
