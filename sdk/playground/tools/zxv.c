/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv - start, run and test ZXV language projects from a shell.
 *
 *   zxv list                       the templates
 *   zxv new TEMPLATE [DIR]         copy a template into DIR (default: TEMPLATE)
 *   zxv run [DIR] [--json]         run the project in DIR (default: .)
 *   zxv run FILE [--lang L] [--inputs F] [--json]
 *                                  run one file; the language comes from its
 *                                  extension (.treaty .k .apl .kr .tb .eve
 *                                  .ads .myra .gmap .sutra .cpy) or --lang
 *   zxv test [DIR] [--update]      run and compare with DIR/expected.json
 *   zxv prompt [DIR] [REQUEST...]  the prompt the local AI gets to draft the
 *                                  project's draft file (pipe it to a model)
 *   zxv langs | version
 *
 * Exit status: 0 TRUE, 1 FALSE (refused), 2 usage or I/O, 3 BOTH (held for
 * review), 4 NEITHER (undecided). Scripts can tell a hold from a refusal.
 */
#include "zxvplay.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0755)
#endif

/* ------------------------------------------------------------ tiny JSON */

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jkind;

typedef struct jv {
    jkind k;
    char *s;           /* J_STR text, J_NUM text */
    int b;
    struct jv **items; /* J_ARR values, J_OBJ values */
    char **keys;       /* J_OBJ keys */
    size_t n;
} jv;

static const char *jp;

static void jws(void) {
    while (*jp == ' ' || *jp == '\n' || *jp == '\r' || *jp == '\t') jp++;
}

static jv *jnew(jkind k) {
    jv *v = (jv *) calloc(1, sizeof *v);
    if (v) v->k = k;
    return v;
}

static char *jstring(void) {
    size_t cap = 64, n = 0;
    char *o = (char *) malloc(cap);
    if (!o || *jp != '"') {
        free(o);
        return NULL;
    }
    jp++;
    while (*jp && *jp != '"') {
        char c = *jp++;
        if (c == '\\') {
            char e = *jp++;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'u') {
                unsigned u = 0;
                for (int i = 0; i < 4 && *jp; i++) {
                    char h = *jp++;
                    u = u * 16u + (unsigned) (h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                }
                c = (char) (u < 0x80u ? u : '?');
            } else c = e;
        }
        if (n + 2 > cap) {
            char *g = (char *) realloc(o, cap *= 2);
            if (!g) {
                free(o);
                return NULL;
            }
            o = g;
        }
        o[n++] = c;
    }
    if (*jp == '"') jp++;
    o[n] = '\0';
    return o;
}

static jv *jparse(int depth) {
    jv *v = NULL;
    if (depth > 32) return NULL;
    jws();
    if (*jp == '{' || *jp == '[') {
        int obj = *jp == '{';
        char close = obj ? '}' : ']';
        v = jnew(obj ? J_OBJ : J_ARR);
        if (!v) return NULL;
        jp++;
        jws();
        while (*jp && *jp != close) {
            char *key = NULL;
            jv *item;
            if (obj) {
                key = jstring();
                jws();
                if (*jp == ':') jp++;
            }
            item = jparse(depth + 1);
            v->items = (jv **) realloc(v->items, (v->n + 1) * sizeof *v->items);
            if (obj) v->keys = (char **) realloc(v->keys, (v->n + 1) * sizeof *v->keys);
            if (!v->items || (obj && !v->keys)) exit(2);
            v->items[v->n] = item;
            if (obj) v->keys[v->n] = key;
            v->n++;
            jws();
            if (*jp == ',') jp++;
            jws();
        }
        if (*jp) jp++;
    } else if (*jp == '"') {
        v = jnew(J_STR);
        if (v) v->s = jstring();
    } else if (strncmp(jp, "true", 4) == 0 || strncmp(jp, "false", 5) == 0) {
        v = jnew(J_BOOL);
        if (v) v->b = *jp == 't';
        jp += *jp == 't' ? 4 : 5;
    } else if (strncmp(jp, "null", 4) == 0) {
        v = jnew(J_NULL);
        jp += 4;
    } else {
        const char *s = jp;
        while (*jp == '-' || (*jp >= '0' && *jp <= '9')) jp++;
        v = jnew(J_NUM);
        if (v) {
            v->s = (char *) malloc((size_t) (jp - s) + 1);
            if (v->s) {
                memcpy(v->s, s, (size_t) (jp - s));
                v->s[jp - s] = '\0';
            }
        }
        if (jp == s) jp++;
    }
    return v;
}

static void jfree(jv *v) {
    if (!v) return;
    for (size_t i = 0; i < v->n; i++) {
        jfree(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->s);
    free(v);
}

static jv *jget(const jv *o, const char *key) {
    if (!o || o->k != J_OBJ) return NULL;
    for (size_t i = 0; i < o->n; i++)
        if (o->keys[i] && strcmp(o->keys[i], key) == 0) return o->items[i];
    return NULL;
}

static const char *jstr(const jv *o, const char *key) {
    jv *v = jget(o, key);
    return v && (v->k == J_STR || v->k == J_NUM) && v->s ? v->s : "";
}

/* ------------------------------------------------------------ output */

static int exit_for(const char *verdict) {
    if (strcmp(verdict, "TRUE") == 0) return 0;
    if (strcmp(verdict, "FALSE") == 0) return 1;
    if (strcmp(verdict, "BOTH") == 0) return 3;
    return 4;
}

static void print_list(const jv *a, const char *label) {
    if (!a || a->k != J_ARR) return;
    for (size_t i = 0; i < a->n; i++)
        if (a->items[i] && a->items[i]->s) printf("    %s%s\n", label, a->items[i]->s);
}

static int render(const char *json) {
    jv *r;
    jv *steps;
    const char *verdict;
    int rc;
    jp = json;
    r = jparse(0);
    if (!r) {
        fputs(json, stdout);
        return 2;
    }
    verdict = jstr(r, "verdict");
    printf("%s%s%s\n", jstr(r, "template"), *jstr(r, "title") ? ": " : "", jstr(r, "title"));
    printf("verdict %s  %s\n", verdict, jstr(r, "headline"));
    if (strcmp(jstr(r, "approval"), "required") == 0) printf("approval required: a person must review this result\n");
    if (*jstr(r, "ledger")) printf("ledger: %s\n", jstr(r, "ledger"));
    print_list(jget(r, "input_errors"), "input: ");
    steps = jget(r, "steps");
    for (size_t i = 0; steps && steps->k == J_ARR && i < steps->n; i++) {
        jv *s = steps->items[i];
        jv *ran = jget(s, "ran");
        jv *outs = jget(s, "outputs"), *tr = jget(s, "truths"), *err = jget(s, "error");
        printf("\n[%s] %s (%s, %s)%s%s%s\n", jstr(s, "verdict"), jstr(s, "name"), jstr(s, "lang"), jstr(s, "file"),
               ran && ran->k == J_BOOL && !ran->b ? " - not run" : "",
               jget(s, "gate") && jget(s, "gate")->b ? " [gate]" : "",
               jget(s, "advisory") && jget(s, "advisory")->b ? " [advisory]" : "");
        for (size_t k = 0; outs && outs->k == J_ARR && k < outs->n; k++)
            printf("    %s = %s\n", jstr(outs->items[k], "name"), jstr(outs->items[k], "value"));
        for (size_t k = 0; tr && tr->k == J_ARR && k < tr->n; k++) {
            jv *vals = jget(tr->items[k], "values");
            printf("    %s =", jstr(tr->items[k], "name"));
            for (size_t m = 0; vals && vals->k == J_ARR && m < vals->n; m++)
                printf(" %s", vals->items[m] && vals->items[m]->s ? vals->items[m]->s : "?");
            printf("\n");
        }
        print_list(jget(s, "held"), "HELD: ");
        print_list(jget(s, "pending"), "pending: ");
        print_list(jget(s, "notes"), "note: ");
        if (err) {
            if (*jstr(err, "line") && strcmp(jstr(err, "line"), "0") != 0)
                printf("    REFUSED at line %s, column %s: %s [%s]\n", jstr(err, "line"), jstr(err, "col"),
                       jstr(err, "message"), jstr(err, "name"));
            else
                printf("    REFUSED: %s [%s]\n", jstr(err, "message"), jstr(err, "name"));
        }
    }
    rc = exit_for(verdict);
    jfree(r);
    return rc;
}

/* ------------------------------------------------------------ files */

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || n > (long) ZP_MAX_FILE) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    b = (char *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    if (b) {
        b[n] = '\0';
        *len = (size_t) n;
    }
    return b;
}

static int write_file(const char *path, const char *text, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (fwrite(text, 1, len, f) != len) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}

typedef struct {
    zp_file f[ZP_MAX_FILES];
    uint32_t n;
} project;

static void project_free(project *p) {
    for (uint32_t i = 0; i < p->n; i++) {
        free((char *) p->f[i].name);
        free((char *) p->f[i].text);
    }
    p->n = 0;
}

/* Every regular file in dir, except expected.json and hidden files. */
static int load_project(const char *dir, project *p) {
    DIR *d = opendir(dir);
    struct dirent *e;
    p->n = 0;
    if (!d) {
        fprintf(stderr, "zxv: cannot open %s: %s\n", dir, strerror(errno));
        return 0;
    }
    while ((e = readdir(d)) != NULL) {
        char path[4096];
        struct stat st;
        size_t len = 0;
        char *text;
        if (e->d_name[0] == '.' || strcmp(e->d_name, "expected.json") == 0) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (p->n >= ZP_MAX_FILES) {
            fprintf(stderr, "zxv: %s has more than %u files\n", dir, ZP_MAX_FILES);
            closedir(d);
            project_free(p);
            return 0;
        }
        text = slurp(path, &len);
        if (!text) {
            fprintf(stderr, "zxv: cannot read %s (or it is larger than 256 KiB)\n", path);
            closedir(d);
            project_free(p);
            return 0;
        }
        p->f[p->n].name = strdup(e->d_name);
        p->f[p->n].text = text;
        p->f[p->n].len = len;
        p->n++;
    }
    closedir(d);
    {
        int has = 0;
        for (uint32_t i = 0; i < p->n; i++)
            if (strcmp(p->f[i].name, "template.zxv") == 0) has = 1;
        if (!has) {
            fprintf(stderr, "zxv: %s has no template.zxv (start one with: zxv new TEMPLATE)\n", dir);
            project_free(p);
            return 0;
        }
    }
    return 1;
}

static const char *lang_of(const char *file) {
    static const struct {
        const char *ext, *lang;
    } map[] = {{".treaty", "treaty"}, {".zxt", "treaty"}, {".k", "kron"},     {".apl", "kron"},
               {".kr", "kron"},       {".tb", "trebago"}, {".eve", "eve"},    {".ads", "eve"},
               {".myra", "myra"},     {".gmap", "myra"},  {".sutra", "sutra"}, {".cpy", "sutra"}};
    const char *dot = strrchr(file, '.');
    if (!dot) return NULL;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (strcmp(dot, map[i].ext) == 0) return map[i].lang;
    return NULL;
}

/* ------------------------------------------------------------ commands */

static int cmd_list(void) {
    char *cat = NULL;
    jv *a;
    if (zp_catalogue_json(&cat, NULL) != ZP_OK) return 2;
    jp = cat;
    a = jparse(0);
    for (size_t i = 0; a && a->k == J_ARR && i < a->n; i++) {
        jv *t = a->items[i];
        printf("%-26s %-8s %-10s %s\n", jstr(t, "id"), jstr(t, "lang"), jstr(t, "safety"), jstr(t, "title"));
        printf("%-26s %s\n", "", jstr(t, "summary"));
    }
    printf("\nStart one with: zxv new TEMPLATE\n");
    jfree(a);
    free(cat);
    return 0;
}

static int cmd_new(const char *id, const char *dir) {
    const zp_template *t = zp_template_find(id);
    char *out = NULL;
    size_t len = 0;
    if (!t) {
        fprintf(stderr, "zxv: no template '%s' (see: zxv list)\n", id);
        return 2;
    }
    if (!dir) dir = id;
    {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL)
                if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
                    closedir(d);
                    fprintf(stderr, "zxv: %s exists and is not empty; nothing written\n", dir);
                    return 2;
                }
            closedir(d);
        } else if (MKDIR(dir) != 0) {
            fprintf(stderr, "zxv: cannot create %s: %s\n", dir, strerror(errno));
            return 2;
        }
    }
    for (uint32_t i = 0; i < t->n_files; i++) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, t->files[i].name);
        if (!write_file(path, t->files[i].text, t->files[i].len)) {
            fprintf(stderr, "zxv: cannot write %s\n", path);
            return 2;
        }
    }
    if (zp_run(t, NULL, 0, &out, &len) == ZP_OK) {
        char path[4096];
        snprintf(path, sizeof path, "%s/expected.json", dir);
        write_file(path, out, len);
        free(out);
    }
    printf("Created %s/ from '%s' (%u files, plus expected.json for zxv test).\n", dir, id, t->n_files);
    printf("Next:\n  cd %s\n  zxv run\n", dir);
    return 0;
}

static int show(const char *json, int as_json) {
    if (as_json) {
        fputs(json, stdout);
        jp = json;
        {
            jv *r = jparse(0);
            int rc = exit_for(jstr(r, "verdict"));
            jfree(r);
            return rc;
        }
    }
    return render(json);
}

static int cmd_run(int argc, char **argv) {
    const char *target = ".", *lang = NULL, *inputs = NULL;
    int as_json = 0, rc;
    char *out = NULL;
    struct stat st;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) as_json = 1;
        else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) lang = argv[++i];
        else if (strcmp(argv[i], "--inputs") == 0 && i + 1 < argc) inputs = argv[++i];
        else if (argv[i][0] == '-') {
            fprintf(stderr, "zxv run: unknown option %s\n", argv[i]);
            return 2;
        } else target = argv[i];
    }
    if (stat(target, &st) != 0) {
        fprintf(stderr, "zxv: %s: %s\n", target, strerror(errno));
        return 2;
    }
    if (S_ISDIR(st.st_mode)) {
        project p;
        if (!load_project(target, &p)) return 2;
        if (zp_run(NULL, p.f, p.n, &out, NULL) != ZP_OK) {
            project_free(&p);
            fprintf(stderr, "zxv: out of memory\n");
            return 2;
        }
        project_free(&p);
    } else {
        size_t len = 0, il = 0;
        char *src = slurp(target, &len), *in = NULL;
        if (!lang) lang = lang_of(target);
        if (!lang) {
            fprintf(stderr, "zxv: cannot tell the language of %s; pass --lang\n", target);
            free(src);
            return 2;
        }
        if (!src) {
            fprintf(stderr, "zxv: cannot read %s\n", target);
            return 2;
        }
        if (inputs && !(in = slurp(inputs, &il))) {
            fprintf(stderr, "zxv: cannot read %s\n", inputs);
            free(src);
            return 2;
        }
        rc = zp_run_one(lang, src, len, in, il, &out, NULL);
        free(src);
        free(in);
        if (rc != ZP_OK) return 2;
    }
    rc = show(out, as_json);
    free(out);
    return rc;
}

static int cmd_test(int argc, char **argv) {
    const char *dir = ".";
    int update = 0, rc;
    project p;
    char *out = NULL, *want;
    size_t wl = 0;
    char path[4096];
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--update") == 0) update = 1;
        else dir = argv[i];
    }
    if (!load_project(dir, &p)) return 2;
    rc = zp_run(NULL, p.f, p.n, &out, NULL);
    project_free(&p);
    if (rc != ZP_OK) return 2;
    snprintf(path, sizeof path, "%s/expected.json", dir);
    if (update) {
        rc = write_file(path, out, strlen(out)) ? 0 : 2;
        printf(rc ? "zxv: cannot write %s\n" : "updated %s\n", path);
        free(out);
        return rc;
    }
    want = slurp(path, &wl);
    if (!want) {
        printf("no expected.json yet: run zxv test --update to record this result\n");
        free(out);
        return 2;
    }
    if (strcmp(want, out) == 0) {
        printf("PASS %s: the result matches expected.json\n", dir);
        rc = 0;
    } else {
        printf("FAIL %s: the result differs from expected.json. This run:\n\n", dir);
        render(out);
        printf("\n(if the change is intended: zxv test --update)\n");
        rc = 1;
    }
    free(want);
    free(out);
    return rc;
}

static int cmd_prompt(int argc, char **argv) {
    const char *dir = ".";
    char req[2048] = "";
    project p;
    char *out = NULL;
    int32_t st;
    int i = 0;
    if (argc > 0) {
        struct stat s;
        if (stat(argv[0], &s) == 0 && S_ISDIR(s.st_mode)) {
            dir = argv[0];
            i = 1;
        }
    }
    for (; i < argc; i++) {
        if (req[0]) strncat(req, " ", sizeof req - strlen(req) - 1);
        strncat(req, argv[i], sizeof req - strlen(req) - 1);
    }
    if (!load_project(dir, &p)) return 2;
    {
        /* Use the project's own manifest: a template with no built-in files. */
        zp_template t = {"project", NULL, 0};
        st = zp_draft_prompt(&t, p.f, p.n, req, &out, NULL);
    }
    project_free(&p);
    if (st == ZP_E_NOTFOUND) {
        fprintf(stderr, "zxv: this project has no draft line in template.zxv\n");
        return 2;
    }
    if (st != ZP_OK) {
        fprintf(stderr, "zxv: %s\n", zp_status_name(st));
        return 2;
    }
    fputs(out, stdout);
    free(out);
    return 0;
}

static int usage(void) {
    fprintf(stderr,
            "usage: zxv list\n"
            "       zxv new TEMPLATE [DIR]\n"
            "       zxv run [DIR | FILE [--lang L] [--inputs F]] [--json]\n"
            "       zxv test [DIR] [--update]\n"
            "       zxv prompt [DIR] [REQUEST...]\n"
            "       zxv langs | version\n"
            "exit status: 0 TRUE, 1 FALSE (refused), 3 BOTH (held for review), 4 NEITHER (undecided), 2 usage\n");
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    if (strcmp(argv[1], "list") == 0) return cmd_list();
    if (strcmp(argv[1], "new") == 0) return argc >= 3 ? cmd_new(argv[2], argc >= 4 ? argv[3] : NULL) : usage();
    if (strcmp(argv[1], "run") == 0) return cmd_run(argc - 2, argv + 2);
    if (strcmp(argv[1], "test") == 0) return cmd_test(argc - 2, argv + 2);
    if (strcmp(argv[1], "prompt") == 0) return cmd_prompt(argc - 2, argv + 2);
    if (strcmp(argv[1], "langs") == 0) {
        for (uint32_t i = 0; i < zp_lang_count(); i++) printf("%s\n", zp_lang_name(i));
        return 0;
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("zxv %s (ZXV Playground)\n", ZP_VERSION);
        return 0;
    }
    return usage();
}
