/* zxv_fab_app.c — apps through the program fabricator: pack an app's
 * manifest into its fabrication form and a PUBLIC recipe, and rebuild an
 * app from a recipe and accept it only through the gate (appfab.h).
 * docs/spec/FABRICATION.md, section 10.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 *   zxv-fab pack-app MANIFEST OUT --pk PK --sk SK [--root DIR] [--blocks DIR]...
 *           [--prev FORM] [--form FILE] [--store DIR] [--tsv FILE] [--layer N]
 *           [--window N]
 *   zxv-fab unpack-app RECIPE OUTDIR --store STORE [--trust PK]... [--tsv FILE]
 *           [--layer N] [--check "ENTRY NAME=V[:V...],... = ANSWER"]...
 *
 * pack-app makes the fabrication form: every `use` is pinned to the content
 * id of its block's own fabrication form (blocks are sealed the same way,
 * inside out), and every logic source read from a path is written inline
 * (<< ... .) or, when it cannot be (not text, a "." line, no final
 * newline), named cid:<CID> and carried as a part. Paths are read under
 * --root (default "."); unpinned uses are found among the manifests in the
 * --blocks directories: the newest compatible version, and two different
 * manifests with that version are refused, never picked between. A pinned
 * use must be a manifest in --blocks with exactly that content id. The
 * closure is checked (appfab_closure) before anything is signed. The
 * recipe is PUBLIC and signed with no randomness, so the same app and
 * parts always give the same bytes. --prev adds an older form as a block
 * the new one slices from. --tsv records the app in FABRICATED.tsv: id,
 * version, manifest CID, recipe CID, layer; the row for an id and version
 * is replaced when the manifest is the same and refused when it differs.
 * --layer N checks the matrix rule (F7), taking each part's layer from the
 * --tsv rows.
 *
 * unpack-app checks the recipe, fetches and checks every block, rebuilds
 * the form, and runs the gate with this tool's runtimes (KRON, Eve, Sutra,
 * Trebago, MYRA, c, Lingua's FORTRAN 77/BASIC/Forth, WebAssembly). Each
 * --check runs witnessed. Only a TRUE verdict writes OUTDIR/<CID>.zxapp
 * and the parts (OUTDIR/<CID>); exit 0 accepted, 1 refused (FALSE),
 * 4 held (BOTH, or NEITHER: unwitnessed or an empty scaffold).
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "zxv_fab.h"
#include "../../kernel/src/appfab/appfab.h"
#include "../../kernel/src/apps/zxapp_eve.h"
#include "../../kernel/src/apps/zxapp_kron.h"
#include "../../kernel/src/apps/zxapp_lingua.h"
#include "../../kernel/src/apps/zxapp_myra.h"
#include "../../kernel/src/apps/zxapp_pce.h"
#include "../../kernel/src/apps/zxapp_sutra.h"
#include "../../kernel/src/apps/zxapp_trebago.h"
#include "../../kernel/src/apps/zxapp_wasm.h"

#define MAX_CANDS  1024u
#define MAX_DIRS   16u
#define MAX_CHECKS 64u
#define SRC_MAX    (64u * 1024u) /* zxapp_host_t.file_buf */

static appfab_t G;

/* ---- growing text ----------------------------------------------------------- */

typedef struct {
    char *b;
    uint32_t n, cap;
} buf_t;

static void put(buf_t *o, const char *s, uint32_t n)
{
    if (o->n + n + 1u > o->cap) {
        uint32_t c = o->cap ? o->cap : 4096u;
        while (o->n + n + 1u > c) {
            if (c > MAX_FILE / 2u) die("form too large");
            c *= 2u;
        }
        char *nb = realloc(o->b, c);
        if (!nb) die("out of memory");
        o->b = nb;
        o->cap = c;
    }
    memcpy(o->b + o->n, s, n);
    o->n += n;
    o->b[o->n] = 0;
}

static void puts_(buf_t *o, const char *s)
{
    put(o, s, (uint32_t) strlen(s));
}

/* ---- parts: the closure, in the order it is found ------------------------------ */

static appfab_part_t PART[APPFAB_PARTS];
static uint32_t NPART;

static uint32_t add_part(const uint8_t *data, uint32_t len)
{
    ipfsn_cid_t c;
    if (!len) die("a part cannot be empty");
    if (len > FAB_BLOCK_MAX) die("a part is larger than one block");
    if (ipfsn_cid_sha256(IPFSN_MC_RAW, data, len, &c) != IPFSN_OK) die("cannot hash a part");
    for (uint32_t i = 0; i < NPART; i++)
        if (ipfsn_cid_equal(&c, &PART[i].cid)) return i;
    if (NPART >= APPFAB_PARTS) die("more parts than an app can use");
    uint8_t *copy = malloc(len);
    if (!copy) die("out of memory");
    memcpy(copy, data, len);
    PART[NPART] = (appfab_part_t) {copy, len, c, 0, false};
    return NPART++;
}

static void cid_str(const ipfsn_cid_t *c, char s[IPFSN_CID_STR_MAX])
{
    if (ipfsn_cid_to_string(c, s, IPFSN_CID_STR_MAX) < 0) die("cannot print a CID");
}

/* ---- candidate manifests (--blocks) ---------------------------------------------- */

typedef struct {
    char *path;
    char *text; /* NUL-terminated copy */
    uint32_t len;
    zxapp_t *m; /* parsed; NULL when the file is not a manifest */
    char pin[ZXAPP_CID_MAX];
    char *form; /* its fabrication form, once sealed */
    uint32_t form_len;
    char form_pin[ZXAPP_CID_MAX];
    int state; /* 0 not sealed, 1 sealing, 2 sealed */
} cand_t;

static cand_t C[MAX_CANDS];
static uint32_t NC;

static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *) a, *(char *const *) b);
}

/* every regular file of dir, in name order (the order never depends on
 * the filesystem) */
static void scan_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "zxv-fab: %s: %s\n", dir, strerror(errno));
        exit(1);
    }
    char **names = 0;
    uint32_t n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[4096];
        struct stat st;
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int) sizeof path)
            die("path too long");
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (n == cap) {
            cap = cap ? cap * 2u : 64u;
            char **nn = realloc(names, cap * sizeof *names);
            if (!nn) die("out of memory");
            names = nn;
        }
        names[n] = strdup(path);
        if (!names[n++]) die("out of memory");
    }
    closedir(d);
    if (n) qsort(names, n, sizeof *names, name_cmp);
    for (uint32_t i = 0; i < n; i++) {
        if (NC >= MAX_CANDS) die("too many files in the --blocks directories");
        cand_t *k = &C[NC++];
        uint8_t *raw = read_file(names[i], &k->len, FAB_BLOCK_MAX);
        k->path = names[i];
        k->text = malloc(k->len + 1u);
        if (!k->text) die("out of memory");
        memcpy(k->text, raw, k->len);
        k->text[k->len] = 0;
        free(raw);
        k->m = calloc(1, sizeof *k->m);
        if (!k->m) die("out of memory");
        if (zxapp_parse(k->m, k->text, k->len) != ZXAPP_OK) {
            free(k->m);
            k->m = 0;
            continue;
        }
        zxapp_cid(k->text, k->len, k->pin);
    }
    free(names);
}

static int ver_cmp(const uint16_t a[3], const uint16_t b[3])
{
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* B1: same major, at least this minor.patch; exact: this version. */
static bool ver_ok(const zxapp_use_t *U, const uint16_t v[3])
{
    if (U->exact) return ver_cmp(U->ver, v) == 0;
    return v[0] == U->ver[0] && ver_cmp(v, U->ver) >= 0;
}

static void ver_str(const uint16_t v[3], char *s, uint32_t cap)
{
    snprintf(s, cap, "%u.%u.%u", (unsigned) v[0], (unsigned) v[1], (unsigned) v[2]);
}

static cand_t *find_use(const zxapp_use_t *U)
{
    cand_t *best = 0;
    for (uint32_t i = 0; i < NC; i++) {
        cand_t *k = &C[i];
        if (!k->m || strcmp(k->m->id, U->id) != 0) continue;
        if (U->cid[0]) {
            if (!strcmp(k->pin, U->cid)) return k;
            continue;
        }
        if (!(k->m->ver[0] | k->m->ver[1] | k->m->ver[2]) || !ver_ok(U, k->m->ver)) continue;
        if (!best || ver_cmp(k->m->ver, best->m->ver) > 0) {
            best = k;
        } else if (ver_cmp(k->m->ver, best->m->ver) == 0 && strcmp(k->pin, best->pin) != 0) {
            fprintf(stderr, "zxv-fab: %s and %s are both %s %s; name one by its pin\n", best->path,
                    k->path, U->id, "at the newest compatible version");
            exit(1);
        }
    }
    if (!best) {
        char v[24];
        ver_str(U->ver, v, sizeof v);
        fprintf(stderr, "zxv-fab: no manifest for use %s %s%s%s in the --blocks directories\n",
                U->id, v, U->cid[0] ? " " : "", U->cid);
        exit(1);
    }
    return best;
}

/* ---- sealing: a manifest's fabrication form ------------------------------------------ */

static const char *ROOT = ".";

/* The end of the k-th word of a line (k from 1), as zxapp's parser splits it. */
static uint32_t word_end(const char *p, uint32_t n, uint32_t k)
{
    uint32_t i = 0;
    for (uint32_t w = 0; w < k; w++) {
        while (i < n && (p[i] == ' ' || p[i] == '\t')) i++;
        while (i < n && p[i] != ' ' && p[i] != '\t') i++;
    }
    return i;
}

/* Text that can sit between "<<" and "." unchanged: no NUL, ends in a
 * newline, and no line is "." (CR stripped, as the parser reads it). */
static bool inlinable(const uint8_t *s, uint32_t n)
{
    if (!n || s[n - 1] != '\n') return false;
    uint32_t b = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!s[i]) return false;
        if (s[i] != '\n') continue;
        uint32_t e = i;
        if (e > b && s[e - 1] == '\r') e--;
        if (e - b == 1u && s[b] == '.') return false;
        b = i + 1u;
    }
    return true;
}

static void seal(const char *text, uint32_t len, const char *what, uint32_t depth, buf_t *out);

/* A source named cid:<CID> in a manifest: the file must be among the
 * --blocks files (any file, by its bytes). */
static void cid_source(const char *path)
{
    ipfsn_cid_t want, c;
    if (ipfsn_cid_parse(path + 4, (uint32_t) strlen(path + 4), &want) != IPFSN_OK)
        die("a cid: source does not parse");
    for (uint32_t i = 0; i < NC; i++) {
        if (ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) C[i].text, C[i].len, &c) == IPFSN_OK &&
            ipfsn_cid_equal(&c, &want)) {
            add_part((const uint8_t *) C[i].text, C[i].len);
            return;
        }
    }
    fprintf(stderr, "zxv-fab: source %s is not in the --blocks directories\n", path);
    exit(1);
}

/* A use's part: its form (sealed inside out); returns the pin. */
static const char *use_part(const zxapp_use_t *U, uint32_t depth)
{
    cand_t *k = find_use(U);
    if (U->cid[0]) {
        /* pinned already: carried as it is, its own uses resolved the same way */
        if (k->state == 0) {
            k->state = 1;
            buf_t o = {0, 0, 0};
            seal(k->text, k->len, k->path, depth + 1u, &o);
            if (o.n != k->len || memcmp(o.b, k->text, k->len) != 0) {
                fprintf(stderr, "zxv-fab: %s is pinned but not sealed (it changes when sealed)\n",
                        k->path);
                exit(1);
            }
            free(o.b);
            k->state = 2;
        }
        add_part((const uint8_t *) k->text, k->len);
        return k->pin;
    }
    if (k->state == 1) {
        fprintf(stderr, "zxv-fab: %s uses itself\n", k->path);
        exit(1);
    }
    if (k->state == 0) {
        k->state = 1;
        buf_t o = {0, 0, 0};
        seal(k->text, k->len, k->path, depth + 1u, &o);
        k->form = o.b;
        k->form_len = o.n;
        zxapp_cid(k->form, k->form_len, k->form_pin);
        k->state = 2;
    }
    add_part((const uint8_t *) k->form, k->form_len);
    return k->form_pin;
}

static void seal(const char *text, uint32_t len, const char *what, uint32_t depth, buf_t *out)
{
    if (depth > APPFAB_DEPTH) die("parts nest too deep");
    zxapp_t *m = calloc(1, sizeof *m);
    if (!m) die("out of memory");
    if (zxapp_parse(m, text, len) != ZXAPP_OK) {
        fprintf(stderr, "zxv-fab: %s: line %u: %s\n", what, (unsigned) m->err_line, m->err);
        exit(1);
    }
    uint32_t at = 0, line = 0;
    while (at < len) {
        uint32_t b = at;
        while (at < len && text[at] != '\n') at++;
        uint32_t e = at; /* the line, without its newline */
        bool nl = at < len;
        if (nl) at++;
        line++;
        const char *p = text + b;
        uint32_t n = e - b;
        const zxapp_logic_t *L = 0;
        const zxapp_use_t *U = 0;
        for (uint32_t i = 0; i < m->n; i++)
            if (m->logic[i].line == line && m->logic[i].path[0]) L = &m->logic[i];
        for (uint32_t i = 0; i < m->nuse; i++)
            if (m->use[i].line == line) U = &m->use[i];
        if (L) {
            uint32_t w3 = word_end(p, n, 3);
            if (!strncmp(L->path, "cid:", 4)) {
                cid_source(L->path);
                put(out, p, n);
            } else {
                char path[4096];
                uint32_t sl;
                if (snprintf(path, sizeof path, "%s/%s", ROOT, L->path) >= (int) sizeof path)
                    die("path too long");
                uint8_t *src = read_file(path, &sl, SRC_MAX);
                put(out, p, w3);
                if (inlinable(src, sl)) {
                    puts_(out, " <<\n");
                    put(out, (const char *) src, sl);
                    put(out, ".", 1);
                } else {
                    char s[IPFSN_CID_STR_MAX];
                    cid_str(&PART[add_part(src, sl)].cid, s);
                    puts_(out, " cid:");
                    puts_(out, s);
                }
                free(src);
            }
        } else if (U) {
            const char *pin = use_part(U, depth);
            if (U->cid[0]) {
                put(out, p, n);
            } else {
                uint32_t w3 = word_end(p, n, 3);
                put(out, p, w3);
                put(out, " ", 1);
                puts_(out, pin);
                put(out, p + w3, n - w3);
            }
        } else {
            put(out, p, n);
        }
        if (nl) put(out, "\n", 1);
    }
    free(m);
}

/* ---- FABRICATED.tsv ------------------------------------------------------------------- */

#define TSV_HEAD "# id\tversion\tmanifest_cid\trecipe_cid\tlayer\n"

typedef struct {
    char id[ZXAPP_ID_MAX], ver[24], mcid[IPFSN_CID_STR_MAX], rcid[IPFSN_CID_STR_MAX], layer[16];
} row_t;

static row_t *ROWS;
static uint32_t NROWS;

static void tsv_read(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT) return;
        fprintf(stderr, "zxv-fab: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    char line[1024];
    uint32_t cap = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (!strchr(line, '\n')) die("FABRICATED.tsv: a line is too long");
        if (NROWS == cap) {
            cap = cap ? cap * 2u : 64u;
            row_t *nr = realloc(ROWS, cap * sizeof *ROWS);
            if (!nr) die("out of memory");
            ROWS = nr;
        }
        row_t *r = &ROWS[NROWS];
        char *f5[5];
        char *s = line;
        for (int i = 0; i < 5; i++) {
            f5[i] = s;
            s += strcspn(s, "\t\n");
            if (*s == 0 || (i < 4 && *s != '\t') || (i == 4 && *s != '\n'))
                die("FABRICATED.tsv: a row needs five tab-separated fields");
            *s++ = 0;
        }
        if (strlen(f5[0]) >= sizeof r->id || strlen(f5[1]) >= sizeof r->ver ||
            strlen(f5[2]) >= sizeof r->mcid || strlen(f5[3]) >= sizeof r->rcid ||
            strlen(f5[4]) >= sizeof r->layer)
            die("FABRICATED.tsv: a field is too long");
        strcpy(r->id, f5[0]);
        strcpy(r->ver, f5[1]);
        strcpy(r->mcid, f5[2]);
        strcpy(r->rcid, f5[3]);
        strcpy(r->layer, f5[4]);
        for (uint32_t i = 0; i < NROWS; i++)
            if (!strcmp(ROWS[i].mcid, r->mcid) && strcmp(ROWS[i].layer, r->layer) != 0)
                die("FABRICATED.tsv: one manifest in two layers");
        NROWS++;
    }
    fclose(f);
}

static bool parse_u32(const char *s, uint32_t *v)
{
    char *end;
    errno = 0;
    unsigned long long x = strtoull(s, &end, 10);
    if (!*s || *end || errno || x > 0xFFFFFFFEull || *s == '-' || *s == '+') return false;
    *v = (uint32_t) x;
    return true;
}

/* F7: each part's layer, from the row of the manifest it is */
static void part_layers(void)
{
    for (uint32_t i = 0; i < NPART; i++) {
        char s[IPFSN_CID_STR_MAX];
        cid_str(&PART[i].cid, s);
        for (uint32_t r = 0; r < NROWS; r++)
            if (!strcmp(ROWS[r].mcid, s) && parse_u32(ROWS[r].layer, &PART[i].layer))
                PART[i].layered = true;
    }
}

static void tsv_write(const char *path, const row_t *n)
{
    buf_t o = {0, 0, 0};
    puts_(&o, TSV_HEAD);
    bool done = false;
    for (uint32_t i = 0; i < NROWS; i++) {
        const row_t *r = &ROWS[i];
        if (!strcmp(r->id, n->id) && !strcmp(r->ver, n->ver)) {
            if (strcmp(r->mcid, n->mcid) != 0) {
                fprintf(stderr,
                        "zxv-fab: %s %s is already fabricated as %s; a new manifest needs a new "
                        "version\n",
                        n->id, n->ver, r->mcid);
                exit(1);
            }
            r = n;
            done = true;
        }
        char line[1024];
        snprintf(line, sizeof line, "%s\t%s\t%s\t%s\t%s\n", r->id, r->ver, r->mcid, r->rcid,
                 r->layer);
        puts_(&o, line);
    }
    if (!done) {
        char line[1024];
        snprintf(line, sizeof line, "%s\t%s\t%s\t%s\t%s\n", n->id, n->ver, n->mcid, n->rcid,
                 n->layer);
        puts_(&o, line);
    }
    write_file(path, (const uint8_t *) o.b, o.n, 0644);
    free(o.b);
}

static const char *state_name(lpres_state_t s)
{
    return s == LPRES_STATE_TRUE    ? "TRUE"
           : s == LPRES_STATE_FALSE ? "FALSE"
           : s == LPRES_STATE_BOTH  ? "BOTH"
                                    : "NEITHER";
}

/* ---- pack-app ------------------------------------------------------------------------- */

int cmd_pack_app(int argc, char **argv)
{
    const char *pkp = 0, *skp = 0, *prev = 0, *formp = 0, *store = 0, *tsv = 0, *pos[2];
    const char *dirs[MAX_DIRS];
    uint32_t ndirs = 0, window = 32, layer = APPFAB_NO_LAYER;
    int np = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--pk") && i + 1 < argc)
            pkp = argv[++i];
        else if (!strcmp(argv[i], "--sk") && i + 1 < argc)
            skp = argv[++i];
        else if (!strcmp(argv[i], "--root") && i + 1 < argc)
            ROOT = argv[++i];
        else if (!strcmp(argv[i], "--blocks") && i + 1 < argc && ndirs < MAX_DIRS)
            dirs[ndirs++] = argv[++i];
        else if (!strcmp(argv[i], "--prev") && i + 1 < argc)
            prev = argv[++i];
        else if (!strcmp(argv[i], "--form") && i + 1 < argc)
            formp = argv[++i];
        else if (!strcmp(argv[i], "--store") && i + 1 < argc)
            store = argv[++i];
        else if (!strcmp(argv[i], "--tsv") && i + 1 < argc)
            tsv = argv[++i];
        else if (!strcmp(argv[i], "--layer") && i + 1 < argc) {
            if (!parse_u32(argv[++i], &layer)) die("--layer needs a layer number");
        } else if (!strcmp(argv[i], "--window") && i + 1 < argc) {
            if (!parse_u32(argv[++i], &window)) die("--window needs a number");
        } else if (np < 2 && argv[i][0] != '-')
            pos[np++] = argv[i];
        else
            die("unexpected argument");
    }
    if (np != 2 || !pkp || !skp)
        die("usage: pack-app MANIFEST OUT --pk PK --sk SK [--root DIR] [--blocks DIR]... "
            "[--prev FORM] [--form FILE] [--store DIR] [--tsv FILE] [--layer N] [--window N]");
    if (layer != APPFAB_NO_LAYER && !tsv) die("--layer needs --tsv (the parts' layers)");
    for (uint32_t i = 0; i < ndirs; i++) scan_dir(dirs[i]);
    if (tsv) tsv_read(tsv);

    uint32_t ml;
    uint8_t *raw = read_file(pos[0], &ml, FAB_BLOCK_MAX);
    char *text = malloc(ml + 1u);
    if (!text) die("out of memory");
    memcpy(text, raw, ml);
    text[ml] = 0;
    free(raw);
    buf_t form = {0, 0, 0};
    seal(text, ml, pos[0], 0, &form);
    if (!form.n) die("an empty manifest");

    /* the closure, before anything is signed */
    if (layer != APPFAB_NO_LAYER) part_layers();
    lpres_state_t cl = appfab_closure(&G, (const uint8_t *) form.b, form.n, layer, PART, NPART);
    if (cl != LPRES_STATE_TRUE) {
        fprintf(stderr, "zxv-fab: closure %s: %s\n", state_name(cl), G.why);
        exit(cl == LPRES_STATE_NEITHER ? 4 : 1);
    }

    /* the recipe: the form over its parts (and the previous form) */
    uint32_t nsrc = NPART + (prev ? 1u : 0u);
    fab_src_t *src = calloc(nsrc ? nsrc : 1u, sizeof *src);
    if (!src) die("out of memory");
    for (uint32_t i = 0; i < NPART; i++) {
        src[i].data = PART[i].data;
        src[i].len = PART[i].len;
        src[i].cid = PART[i].cid;
    }
    if (prev) {
        fab_src_t *s = &src[NPART];
        uint8_t *b = read_file(prev, &s->len, FAB_BLOCK_MAX);
        if (!s->len) die("the previous form is empty");
        s->data = b;
        if (ipfsn_cid_sha256(IPFSN_MC_RAW, b, s->len, &s->cid) != IPFSN_OK)
            die("cannot hash the previous form");
        for (uint32_t i = 0; i < NPART; i++)
            if (ipfsn_cid_equal(&s->cid, &PART[i].cid)) {
                nsrc--; /* already a part */
                break;
            }
    }
    uint32_t tlen = form.n, bits = 20, ops_cap = tlen / 4u + 16u, no = 0, dl = 0, pl = 0, rl = 0;
    if (ops_cap > FAB_MAX_OPS) ops_cap = FAB_MAX_OPS;
    fab_ient_t *table = malloc(sizeof *table << bits);
    uint8_t *ops = malloc((size_t) ops_cap * FAB_OP_REC), *diff = malloc(tlen);
    if (!table || !ops || !diff) die("out of memory");
    int rc = fab_build(src, nsrc, (const uint8_t *) form.b, tlen, window, table, bits, ops, ops_cap,
                       &no, diff, tlen, &dl);
    if (rc) fail("build", rc);
    ipfsn_cid_t tc;
    if ((rc = fab_target_cid((const uint8_t *) form.b, tlen, &W, &tc))) fail("target cid", rc);
    uint32_t pcap = FAB_PLAN_HDR + nsrc * FAB_BLOCK_REC + no * FAB_OP_REC;
    uint8_t *plan = malloc(pcap);
    if (!plan) die("out of memory");
    if ((rc = fab_plan_write(&tc, tlen, 0, src, nsrc, ops, no, plan, pcap, &pl))) fail("plan", rc);
    uint8_t *pk = read_key(pkp, FAB_SIG_PK), *sk = read_key(skp, FAB_SIG_SK);
    fab_seal_args_t a = {FAB_KIND_FILE, FAB_MODE_PUBLIC, pk, sk, 0, 0, 0};
    uint32_t rcap = pl + dl + FAB_HDR + 2u * FAB_TAG + FAB_SIG_PK + FAB_SIG;
    uint8_t *rec = malloc(rcap);
    if (!rec) die("out of memory");
    if ((rc = fab_recipe_seal(&a, plan, pl, diff, dl, 0, rec, rcap, &rl))) fail("seal", rc);

    /* written only now that every check has passed */
    if (store) {
        for (uint32_t i = 0; i < nsrc; i++) {
            char s[IPFSN_CID_STR_MAX], path[4096];
            cid_str(&src[i].cid, s);
            if (snprintf(path, sizeof path, "%s/%s", store, s) >= (int) sizeof path)
                die("path too long");
            write_file(path, src[i].data, src[i].len, 0644);
        }
    }
    if (formp) write_file(formp, (const uint8_t *) form.b, form.n, 0644);
    write_file(pos[1], rec, rl, 0644);
    char ts[IPFSN_CID_STR_MAX], rs[IPFSN_CID_STR_MAX];
    cid_str(&tc, ts);
    ipfsn_cid_t rcid;
    if ((rc = fab_target_cid(rec, rl, &W, &rcid))) fail("recipe cid", rc);
    cid_str(&rcid, rs);
    if (tsv) {
        row_t n;
        memset(&n, 0, sizeof n);
        snprintf(n.id, sizeof n.id, "%s", G.app.id);
        if (G.app.ver[0] | G.app.ver[1] | G.app.ver[2])
            ver_str(G.app.ver, n.ver, sizeof n.ver);
        else
            snprintf(n.ver, sizeof n.ver, "-");
        snprintf(n.mcid, sizeof n.mcid, "%s", ts);
        snprintf(n.rcid, sizeof n.rcid, "%s", rs);
        if (layer == APPFAB_NO_LAYER)
            snprintf(n.layer, sizeof n.layer, "-");
        else
            snprintf(n.layer, sizeof n.layer, "%u", (unsigned) layer);
        tsv_write(tsv, &n);
    }
    printf("app %s %s\n", G.app.id, ts);
    printf("recipe %s %u bytes\n", rs, rl);
    printf("parts %u, ops %u, diff %u bytes\n", nsrc, no, dl);
    return 0;
}

/* ---- unpack-app ------------------------------------------------------------------------ */

typedef struct {
    char name[ZXAPP_ID_MAX];
    int64_t v[16];
    uint32_t n;
} carg_t;

typedef struct {
    char entry[ZXAPP_ENTRY_MAX];
    carg_t arg[ZXAPP_INPUTS];
    zxapp_arg_t za[ZXAPP_INPUTS];
    zxapp_num_t want[16];
} cspec_t;

static bool parse_i64(const char *s, uint32_t n, int64_t *v)
{
    char tmp[32];
    if (!n || n >= sizeof tmp) return false;
    memcpy(tmp, s, n);
    tmp[n] = 0;
    char *end;
    errno = 0;
    long long x = strtoll(tmp, &end, 10);
    if (*end || errno) return false;
    *v = (int64_t) x;
    return true;
}

/* "ENTRY NAME=V[:V...],... = ANSWER[:ANSWER...]", ANSWER an integer, N/D,
 * true, false or both (the languages must disagree). */
static void parse_check(const char *s, cspec_t *cs, appfab_check_t *c)
{
    memset(cs, 0, sizeof *cs);
    memset(c, 0, sizeof *c);
    const char *eq = strstr(s, " = ");
    if (!eq) die("--check needs \"ENTRY ARGS = ANSWER\"");
    uint32_t el = (uint32_t) strcspn(s, " ");
    if (!el || el >= sizeof cs->entry) die("--check: a bad entry");
    memcpy(cs->entry, s, el);
    const char *p = s + el;
    uint32_t na = 0;
    while (p < eq) {
        while (p < eq && (*p == ' ' || *p == ',')) p++;
        if (p >= eq) break;
        if (na >= ZXAPP_INPUTS) die("--check: too many arguments");
        carg_t *A = &cs->arg[na];
        uint32_t nl = (uint32_t) strcspn(p, "=");
        if (!nl || nl >= sizeof A->name || p + nl >= eq) die("--check: an argument needs NAME=V");
        memcpy(A->name, p, nl);
        p += nl + 1u;
        for (;;) {
            uint32_t vl = (uint32_t) strcspn(p, ":, ");
            if (A->n >= 16u || !parse_i64(p, vl, &A->v[A->n++])) die("--check: a bad value");
            p += vl;
            if (*p != ':') break;
            p++;
        }
        cs->za[na] = (zxapp_arg_t) {A->name, A->v, A->n};
        na++;
    }
    const char *q = eq + 3;
    c->entry = cs->entry;
    c->args = cs->za;
    c->nargs = na;
    if (!strcmp(q, "both")) {
        c->want_both = true;
        return;
    }
    uint32_t nw = 0;
    for (;;) {
        uint32_t ql = (uint32_t) strcspn(q, ":");
        zxapp_num_t *w = &cs->want[nw];
        if (nw >= 16u) die("--check: too many answers");
        if (ql == 4 && !strncmp(q, "true", 4))
            *w = (zxapp_num_t) {ZXAPP_TRUE, 0};
        else if (ql == 5 && !strncmp(q, "false", 5))
            *w = (zxapp_num_t) {ZXAPP_FALSE, 0};
        else {
            const char *sl = memchr(q, '/', ql);
            int64_t num, den = 1;
            if (!parse_i64(q, sl ? (uint32_t) (sl - q) : ql, &num) ||
                (sl && (!parse_i64(sl + 1, ql - (uint32_t) (sl - q) - 1u, &den) || den <= 0)))
                die("--check: a bad answer");
            *w = (zxapp_num_t) {num, den};
        }
        nw++;
        q += ql;
        if (*q != ':') break;
        q++;
    }
    c->want = cs->want;
    c->nwant = nw;
}

static zxapp_host_t HOST;
static zxapp_wasm_t WASM;

static void host_init(void)
{
    zxapp_host_init(&HOST);
    if (zxapp_kron_register(&HOST) != ZXAPP_OK || zxapp_eve_register(&HOST) != ZXAPP_OK ||
        zxapp_sutra_register(&HOST) != ZXAPP_OK || zxapp_trebago_register(&HOST) != ZXAPP_OK ||
        zxapp_myra_register(&HOST) != ZXAPP_OK || zxapp_pce_register(&HOST) != ZXAPP_OK ||
        zxapp_lingua_register(&HOST, 0) != ZXAPP_OK ||
        zxapp_wasm_register(&HOST, &WASM, 0) != ZXAPP_OK)
        die("a language runtime did not start");
}

int cmd_unpack_app(int argc, char **argv)
{
    const char *pos[2], *store = 0, *tsv = 0, *trust[64];
    static cspec_t cs[MAX_CHECKS];
    static appfab_check_t ck[MAX_CHECKS];
    uint32_t nck = 0, layer = APPFAB_NO_LAYER, ntrust = 0;
    int np = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--store") && i + 1 < argc)
            store = argv[++i];
        else if (!strcmp(argv[i], "--trust") && i + 1 < argc && ntrust < 64)
            trust[ntrust++] = argv[++i];
        else if (!strcmp(argv[i], "--tsv") && i + 1 < argc)
            tsv = argv[++i];
        else if (!strcmp(argv[i], "--layer") && i + 1 < argc) {
            if (!parse_u32(argv[++i], &layer)) die("--layer needs a layer number");
        } else if (!strcmp(argv[i], "--check") && i + 1 < argc) {
            if (nck >= MAX_CHECKS) die("too many checks");
            parse_check(argv[++i], &cs[nck], &ck[nck]);
            nck++;
        } else if (np < 2 && argv[i][0] != '-')
            pos[np++] = argv[i];
        else
            die("unexpected argument");
    }
    if (np != 2 || !store)
        die("usage: unpack-app RECIPE OUTDIR --store STORE [--trust PK]... [--tsv FILE] "
            "[--layer N] [--check SPEC]...");
    if (layer != APPFAB_NO_LAYER && !tsv) die("--layer needs --tsv (the parts' layers)");
    if (tsv) tsv_read(tsv);

    uint32_t len, olen = 0;
    uint8_t *buf = read_file(pos[0], &len, MAX_FILE);
    fab_recipe_t r;
    int rc = fab_recipe_parse(buf, len, &r);
    if (rc) fail("parse", rc);
    uint8_t(*ids)[FAB_ID] = 0;
    if (ntrust) {
        ids = calloc(ntrust, FAB_ID);
        if (!ids) die("out of memory");
        for (uint32_t i = 0; i < ntrust; i++) {
            uint8_t *k = read_key(trust[i], FAB_SIG_PK);
            fab_key_id(k, FAB_SIG_PK, ids[i]);
            free(k);
        }
    }
    if ((rc = fab_recipe_check(&r, (const uint8_t(*)[FAB_ID]) ids, ntrust))) fail("check", rc);
    if (r.mode != FAB_MODE_PUBLIC || r.kind != FAB_KIND_FILE)
        die("an app recipe is a PUBLIC file recipe");
    uint8_t *plan = malloc(r.plan_len ? r.plan_len : 1u);
    fab_plan_t p;
    if (!plan) die("out of memory");
    if ((rc = fab_open_plan(&r, 0, plan, r.plan_len, &p))) fail("plan", rc);
    if (p.target_len > FAB_BLOCK_MAX) die("an app manifest is at most one block");
    if (p.n_blocks > APPFAB_PARTS) die("more parts than an app can use");
    fab_io_t io = {.get = store_get,
                   .get_ctx = (void *) store,
                   .diff_buf = malloc(r.diff_len ? r.diff_len : 1u),
                   .diff_cap = r.diff_len,
                   .out = malloc(p.target_len ? (size_t) p.target_len : 1u),
                   .out_cap = (uint32_t) p.target_len,
                   .work = &W};
    if (!io.diff_buf || !io.out) die("out of memory");
    if ((rc = fab_hydrate(&p, &io, 0))) fail("hydrate", rc);
    if ((rc = fab_assemble(&r, &p, 0, &io, 0, &olen))) fail("assemble", rc);

    /* the parts are the plan's blocks; the gate checks each again (F1) */
    for (uint32_t i = 0; i < p.n_blocks; i++) {
        uint32_t want;
        if (fab_plan_block(&p, i, &PART[i].cid, &want)) die("plan block");
        uint8_t *b = malloc(want ? want : 1u);
        uint32_t got = 0;
        if (!b || store_get((void *) store, &PART[i].cid, b, want, &got) != 0 || got != want)
            die("a block changed in the store after it was checked");
        PART[i].data = b;
        PART[i].len = got;
    }
    NPART = p.n_blocks;
    if (layer != APPFAB_NO_LAYER) part_layers();

    host_init();
    lpres_state_t fit = appfab_fit_layered(&G, io.out, olen, layer, PART, NPART, &HOST);
    printf("fit %s%s%s\n", state_name(fit), G.why[0] ? ": " : "", G.why);
    lpres_state_t fn = appfab_function(&G, &HOST, ck, nck);
    if (fit == LPRES_STATE_TRUE) printf("function %s%s%s\n", state_name(fn), G.why[0] ? ": " : "", G.why);
    lpres_state_t v = appfab_verdict(fit, fn);
    char ts[IPFSN_CID_STR_MAX];
    cid_str(&p.target, ts);
    printf("verdict %s %s\n", state_name(v), ts);
    appfab_unload(&G, &HOST);
    zxapp_wasm_close(&WASM);
    if (v != LPRES_STATE_TRUE) return v == LPRES_STATE_FALSE ? 1 : 4;

    /* accepted: only now is anything written */
    char path[4096];
    if (mkdir(pos[1], 0755) != 0 && errno != EEXIST) die("cannot create OUTDIR");
    for (uint32_t i = 0; i < NPART; i++) {
        char s[IPFSN_CID_STR_MAX];
        cid_str(&PART[i].cid, s);
        if (snprintf(path, sizeof path, "%s/%s", pos[1], s) >= (int) sizeof path)
            die("path too long");
        write_file(path, PART[i].data, PART[i].len, 0644);
    }
    if (snprintf(path, sizeof path, "%s/%s.zxapp", pos[1], ts) >= (int) sizeof path)
        die("path too long");
    write_file(path, io.out, olen, 0644);
    free(buf);
    return 0;
}
