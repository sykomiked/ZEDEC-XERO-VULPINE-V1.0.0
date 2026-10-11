/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_golden.c — records written by GnuCOBOL (test/golden/records-*.hex,
 * made by cobol/GOLDEN.cbl without libzsutra) are read with the example
 * copybooks AND their Sutra twins, compared with records.expect, and then
 * rebuilt from those values: the rebuilt bytes must equal GnuCOBOL's,
 * byte for byte, in both sign conventions. Run from the pillar directory. */
#define _POSIX_C_SOURCE 200809L
#include "zs_test.h"

#define MAX_RECS 16
#define MAX_EXP  128

typedef struct {
    char kind[16];
    uint8_t bytes[1024];
    size_t n;
} grec;

typedef struct {
    int rec;
    char field[48];
    char value[64];
} gexp;

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    if (b) b[n] = '\0';
    fclose(f);
    return b;
}

static zs_layout *load(const char *path, int is_sutra) {
    char *t = slurp(path);
    zs_layout *l = NULL;
    zs_diag d;
    int32_t st;
    if (!t) {
        fprintf(stderr, "cannot read %s (run from sdk/pillars/cobol-sutra)\n", path);
        return NULL;
    }
    st = is_sutra ? zs_sutra_parse(t, strlen(t), &l, &d)
                  : zs_copybook_parse(t, strlen(t), ZS_CB_FIXED, &l, &d);
    if (st != ZXV_OK) fprintf(stderr, "%s: %s\n", path, d.msg);
    free(t);
    return l;
}

static size_t read_hex(const char *path, grec *r) {
    char *t = slurp(path), *line, *save = NULL;
    size_t n = 0;
    if (!t) return 0;
    for (line = strtok_r(t, "\n", &save); line && n < MAX_RECS; line = strtok_r(NULL, "\n", &save)) {
        char *sp;
        if (line[0] == '#' || !line[0]) continue;
        sp = strchr(line, ' ');
        if (!sp || (size_t) (sp - line) >= sizeof r[n].kind) continue;
        memcpy(r[n].kind, line, (size_t) (sp - line));
        r[n].kind[sp - line] = '\0';
        r[n].n = unhex(sp, r[n].bytes, sizeof r[n].bytes);
        n++;
    }
    free(t);
    return n;
}

static size_t read_expect(const char *path, gexp *e) {
    char *t = slurp(path), *line, *save = NULL;
    size_t n = 0;
    if (!t) return 0;
    for (line = strtok_r(t, "\n", &save); line && n < MAX_EXP; line = strtok_r(NULL, "\n", &save)) {
        char *eq;
        if (line[0] == '#' || !line[0]) continue;
        eq = strstr(line, " = ");
        if (!eq) continue;
        *eq = '\0';
        if (sscanf(line, "%d %47s", &e[n].rec, e[n].field) != 2) continue;
        snprintf(e[n].value, sizeof e[n].value, "%s", eq + 3);
        n++;
    }
    free(t);
    return n;
}

static void rtrim(char *s) {
    size_t k = strlen(s);
    while (k && s[k - 1u] == ' ') s[--k] = '\0';
}

static void run(const char *hexpath, int32_t style, zs_layout *const lay[4], const gexp *e,
                size_t ne) {
    static grec r[MAX_RECS];
    size_t nr = read_hex(hexpath, r);
    CHECK(nr == 6, "%s: %zu records, expected 6", hexpath, nr);
    for (int k = 0; k < 4; k++) zs_layout_set_overpunch(lay[k], style);
    for (size_t i = 0; i < nr; i++) {
        int is_stmt = strcmp(r[i].kind, "statement") == 0;
        for (int twin = 0; twin < 2; twin++) {
            const zs_layout *l = lay[(is_stmt ? 2 : 0) + twin];
            uint64_t size = 0;
            uint8_t rebuilt[1024];
            zs_layout_size(l, &size);
            CHECK(r[i].n == size, "%s record %zu: %zu bytes, layout %llu", hexpath, i + 1u, r[i].n,
                  (unsigned long long) size);
            if (r[i].n != size) continue;
            CHECK(zs_record_check(l, r[i].bytes, r[i].n) == ZXV_OK, "%s record %zu check", hexpath,
                  i + 1u);
            CHECK(zs_record_init(l, rebuilt, sizeof rebuilt) == ZXV_OK, "init");
            for (size_t k = 0; k < ne; k++) {
                char got[4096];
                if ((size_t) e[k].rec != i + 1u) continue;
                snprintf(got, sizeof got, "%s", val(l, r[i].bytes, r[i].n, e[k].field));
                rtrim(got);
                CHECK(strcmp(got, e[k].value) == 0, "%s record %zu %s: got '%s', expected '%s'",
                      hexpath, i + 1u, e[k].field, got, e[k].value);
                CHECK(zs_put_value(l, rebuilt, sizeof rebuilt, e[k].field, e[k].value,
                                   strlen(e[k].value)) == ZXV_OK,
                      "put %s = %s", e[k].field, e[k].value);
            }
            CHECK(memcmp(rebuilt, r[i].bytes, r[i].n) == 0,
                  "%s record %zu (%s): rebuilt bytes differ from GnuCOBOL's", hexpath, i + 1u,
                  twin ? "sutra" : "copybook");
        }
    }
}

int main(void) {
    static gexp e[MAX_EXP];
    zs_layout *lay[4] = {load("examples/payment.cpy", 0), load("examples/payment.sutra", 1),
                         load("examples/statement.cpy", 0), load("examples/statement.sutra", 1)};
    size_t ne = read_expect("test/golden/records.expect", e);
    static grec r[MAX_RECS];
    int64_t u;
    uint32_t sc;
    CHECK(lay[0] && lay[1] && lay[2] && lay[3], "example layouts parse");
    CHECK(ne >= 50, "records.expect has %zu values", ne);
    if (!lay[0] || !lay[1] || !lay[2] || !lay[3]) return report("test_golden");

    run("test/golden/records-ascii.hex", ZS_OVERPUNCH_ASCII, lay, e, ne);
    run("test/golden/records-ibm.hex", ZS_OVERPUNCH_IBM, lay, e, ne);

    /* the wrong convention is refused, never guessed: record 2's fee is
     * -12.34, 't' in ASCII and 'M' in IBM. 'M' is not an ASCII overpunch,
     * and 't' is not an IBM one. */
    CHECK(read_hex("test/golden/records-ascii.hex", r) == 6, "re-read");
    zs_layout_set_overpunch(lay[0], ZS_OVERPUNCH_IBM);
    CHECK(zs_get_decimal(lay[0], r[1].bytes, r[1].n, "PAY-FEE", &u, &sc) == ZS_E_DATA,
          "ASCII overpunch read as IBM is ZS_E_DATA");
    CHECK(read_hex("test/golden/records-ibm.hex", r) == 6, "re-read");
    zs_layout_set_overpunch(lay[0], ZS_OVERPUNCH_ASCII);
    CHECK(zs_get_decimal(lay[0], r[1].bytes, r[1].n, "PAY-FEE", &u, &sc) == ZS_E_DATA,
          "IBM overpunch read as ASCII is ZS_E_DATA");

    for (int k = 0; k < 4; k++) zs_layout_destroy(lay[k]);
    return report("test_golden");
}
