/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zkron_tool.c — the zkron command.
 *
 *   zkron eval   [opts] FILE|-e TEXT [NAME=VALUE ...]   evaluate; print the value
 *   zkron canon  [opts] FILE|-e TEXT                    print the canonical text
 *   zkron digest [opts] FILE|-e TEXT                    print the SHA3-256 digest
 *   zkron same   [opts] A B                             exit 0 when A and B have
 *                                                       one canonical form
 *   zkron check  [opts] FILE...                         parse; report refusals
 *   zkron free   [opts] FILE|-e TEXT                    list the free names
 *   zkron golden FILE...                                run golden cases
 *
 * Options: --apl, --k, --canon (default: detect), --display (eval: APL/K
 * style instead of canonical value text), --work N (work limit).
 * A VALUE is canonical KRON value text: 5, -1r3, "(vec 1 2 3)",
 * "(mat 2 2 1 0 0 1)", "(tvec true both)". A FILE of "-" is stdin.
 *
 * Golden files hold cases:
 *   === NAME
 *   spell: apl|k|canon|auto
 *   bind: NAME = VALUE          (any number)
 *   expect: VALUE               the exact value, as canonical value text
 *   refuse: STATUS              or the parse or the run must be refused so
 *   same: GROUP                 every case in GROUP has the same digest
 *   differ: GROUP               ... and no case here has GROUP's digest
 *   program:
 *   <program lines up to the next === or the end>
 * Each case also checks that the canonical text is a fixed point and that
 * the canonical program gives the same result as the original spelling.
 * Integer code only (R11). */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zkron.h"

static int32_t g_spell = KR_SPELL_AUTO;
static int g_display;
static uint64_t g_work;

static char *slurp(const char *path, size_t *out_len) {
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    char *b = NULL;
    size_t n = 0, cap = 0;
    if (!f) return NULL;
    for (;;) {
        size_t r;
        if (n + 4096u + 1u > cap) {
            char *nb;
            cap = cap ? cap * 2u : 8192u;
            nb = (char *) realloc(b, cap);
            if (!nb) {
                free(b);
                if (f != stdin) fclose(f);
                return NULL;
            }
            b = nb;
        }
        r = fread(b + n, 1, cap - n - 1u, f);
        n += r;
        if (r == 0) break;
    }
    if (f != stdin) fclose(f);
    b[n] = '\0';
    *out_len = n;
    return b;
}

/* Text through a two-call buffer. Caller frees. */
typedef int32_t (*textfn)(const void *, char *, size_t, size_t *);
static int32_t prog_text(const void *p, char *b, size_t c, size_t *n) { return kr_program_text((const kr_program *) p, b, c, n); }
static int32_t val_text(const void *v, char *b, size_t c, size_t *n) { return kr_value_text((const kr_value *) v, b, c, n); }
static int32_t val_disp(const void *v, char *b, size_t c, size_t *n) { return kr_value_display((const kr_value *) v, b, c, n); }

static char *text_of(textfn f, const void *x) {
    size_t need = 0;
    char *b;
    if (f(x, NULL, 0, &need) != ZXV_E_BUFFER) return NULL;
    b = (char *) malloc(need + 1u);
    if (!b) return NULL;
    if (f(x, b, need + 1u, &need) != ZXV_OK) {
        free(b);
        return NULL;
    }
    return b;
}

static void hex32(const uint8_t d[32], char out[65]) {
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = hx[d[i] >> 4];
        out[2 * i + 1] = hx[d[i] & 15];
    }
    out[64] = '\0';
}

static kr_program *load(const char *src, int inline_text, int32_t spell, int quiet) {
    size_t len = 0;
    char *t = inline_text ? strdup(src) : slurp(src, &len);
    kr_program *p = NULL;
    kr_diag d;
    int32_t st;
    if (!t) {
        fprintf(stderr, "%s: cannot read\n", src);
        return NULL;
    }
    if (inline_text) len = strlen(t);
    st = kr_parse(t, len, spell, &p, &d);
    free(t);
    if (st != ZXV_OK) {
        if (!quiet) fprintf(stderr, "%s: %s\n", inline_text ? "-e" : src, d.msg);
        return NULL;
    }
    return p;
}

/* A value from canonical value text. */
static int32_t parse_value(const char *text, kr_value **out, char *why, size_t whycap) {
    size_t n = strlen(text);
    char *t = (char *) malloc(n + 8u);
    kr_program *p = NULL;
    kr_diag d;
    int32_t st;
    if (!t) return ZXV_E_NOMEM;
    memcpy(t, "kron\n", 5);
    memcpy(t + 5, text, n + 1u);
    st = kr_parse(t, n + 5u, KR_SPELL_CANON, &p, &d);
    free(t);
    if (st == ZXV_OK) {
        uint32_t nf = 0;
        kr_free_count(p, &nf);
        if (nf) {
            snprintf(why, whycap, "a value cannot name variables");
            kr_program_destroy(p);
            return KR_E_NAME;
        }
        st = kr_eval(p, NULL, 0, 0, out, &d);
    }
    if (st != ZXV_OK) snprintf(why, whycap, "%s", d.msg);
    kr_program_destroy(p);
    return st;
}

typedef struct {
    char name[64];
    kr_value *v;
} bind_t;

static int32_t run(const kr_program *p, bind_t *b, uint32_t nb, kr_value **out, kr_diag *d) {
    kr_binding kb[64];
    for (uint32_t i = 0; i < nb; i++) {
        kb[i].name = b[i].name;
        kb[i].value = b[i].v;
    }
    return kr_eval(p, kb, nb, g_work, out, d);
}

static int parse_bind(const char *arg, bind_t *b, uint32_t *nb) {
    const char *eq = strchr(arg, '=');
    size_t kn;
    char why[300];
    int32_t st;
    if (!eq || *nb >= 64) {
        fprintf(stderr, "bad binding '%s' (NAME=VALUE)\n", arg);
        return 0;
    }
    kn = (size_t) (eq - arg);
    while (kn && arg[kn - 1] == ' ') kn--;
    if (kn == 0 || kn >= sizeof b[0].name) {
        fprintf(stderr, "bad binding name in '%s'\n", arg);
        return 0;
    }
    memcpy(b[*nb].name, arg, kn);
    b[*nb].name[kn] = '\0';
    eq++;
    while (*eq == ' ') eq++;
    st = parse_value(eq, &b[*nb].v, why, sizeof why);
    if (st != ZXV_OK) {
        fprintf(stderr, "binding %s: %s\n", b[*nb].name, why);
        return 0;
    }
    (*nb)++;
    return 1;
}

static int cmd_eval(int argc, char **argv) {
    kr_program *p;
    bind_t b[64];
    uint32_t nb = 0;
    kr_value *v = NULL;
    kr_diag d;
    int32_t st;
    int i = 0, rc = 0;
    char *s;
    if (argc < 1) return 2;
    if (strcmp(argv[0], "-e") == 0) {
        if (argc < 2) return 2;
        p = load(argv[1], 1, g_spell, 0);
        i = 2;
    } else {
        p = load(argv[0], 0, g_spell, 0);
        i = 1;
    }
    if (!p) return 1;
    for (; i < argc; i++)
        if (!parse_bind(argv[i], b, &nb)) {
            rc = 2;
            goto out;
        }
    st = run(p, b, nb, &v, &d);
    if (st != ZXV_OK) {
        fprintf(stderr, "%s\n", d.msg);
        rc = 1;
        goto out;
    }
    s = text_of(g_display ? val_disp : val_text, v);
    if (!s) {
        rc = 1;
        goto out;
    }
    printf("%s\n", s);
    free(s);
out:
    kr_value_destroy(v);
    for (uint32_t k = 0; k < nb; k++) kr_value_destroy(b[k].v);
    kr_program_destroy(p);
    return rc;
}

static kr_program *load_arg(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[0], "-e") == 0) return load(argv[1], 1, g_spell, 0);
    if (argc >= 1) return load(argv[0], 0, g_spell, 0);
    return NULL;
}

static int cmd_canon(int argc, char **argv) {
    kr_program *p = load_arg(argc, argv);
    char *t;
    if (!p) return 1;
    t = text_of(prog_text, p);
    if (t) fputs(t, stdout);
    free(t);
    kr_program_destroy(p);
    return t ? 0 : 1;
}

static int cmd_digest(int argc, char **argv) {
    kr_program *p = load_arg(argc, argv);
    uint8_t d[32];
    char hx[65];
    if (!p) return 1;
    kr_program_digest(p, d);
    hex32(d, hx);
    printf("%s\n", hx);
    kr_program_destroy(p);
    return 0;
}

static int cmd_free(int argc, char **argv) {
    kr_program *p = load_arg(argc, argv);
    uint32_t n = 0;
    if (!p) return 1;
    kr_free_count(p, &n);
    for (uint32_t i = 0; i < n; i++) {
        char nm[64];
        size_t len = 0;
        if (kr_free_at(p, i, nm, sizeof nm, &len) == ZXV_OK) printf("%s\n", nm);
    }
    kr_program_destroy(p);
    return 0;
}

static int cmd_same(int argc, char **argv) {
    kr_program *a, *b;
    uint8_t da[32], db[32];
    char ha[65], hb[65];
    int same;
    if (argc != 2) return 2;
    a = load(argv[0], 0, g_spell, 0);
    b = load(argv[1], 0, g_spell, 0);
    if (!a || !b) {
        kr_program_destroy(a);
        kr_program_destroy(b);
        return 1;
    }
    kr_program_digest(a, da);
    kr_program_digest(b, db);
    hex32(da, ha);
    hex32(db, hb);
    same = memcmp(da, db, 32) == 0;
    printf("%s  %s\n%s  %s\n%s\n", ha, argv[0], hb, argv[1], same ? "same program" : "DIFFERENT programs");
    kr_program_destroy(a);
    kr_program_destroy(b);
    return same ? 0 : 1;
}

static int cmd_check(int argc, char **argv) {
    int bad = 0;
    for (int i = 0; i < argc; i++) {
        kr_program *p = load(argv[i], 0, g_spell, 0);
        if (!p) {
            bad++;
            continue;
        }
        printf("%s: ok\n", argv[i]);
        kr_program_destroy(p);
    }
    return bad ? 1 : 0;
}

/* ===== golden ===== */

static int32_t status_by_name(const char *n) {
    static const int32_t codes[] = {KR_E_SYNTAX, KR_E_NAME,  KR_E_RANK,    KR_E_LENGTH,
                                    KR_E_DOMAIN, KR_E_INDEX, KR_E_DIVZERO, KR_E_LIMIT};
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++)
        if (strcmp(n, kr_status_name(codes[i])) == 0) return codes[i];
    for (int32_t c = 0; c >= -99; c--)
        if (strcmp(n, zxv_status_name(c)) == 0) return c;
    return 1;
}

#define MAXG 4096
typedef struct {
    char name[64];
    uint8_t digest[32];
    int differ; /* 1: must not equal the group's digest */
} gmember;

static gmember g_groups[MAXG];
static uint32_t g_ngroups;
static unsigned long long g_cases, g_checks;

typedef struct {
    char name[128];
    int32_t spell;
    bind_t b[64];
    uint32_t nb;
    char *expect;
    int32_t refuse; /* 1: none */
    char same[64], differ[64];
    uint64_t work;
    char *prog;
    size_t plen, pcap;
    int bad_header;
} gcase;

static void gcase_free(gcase *c) {
    for (uint32_t k = 0; k < c->nb; k++) kr_value_destroy(c->b[k].v);
    free(c->expect);
    free(c->prog);
    memset(c, 0, sizeof *c);
}

static int fail_case(const char *path, const gcase *c, const char *msg, const char *detail) {
    fprintf(stderr, "%s: case %s: %s%s%s\n", path, c->name, msg, detail ? ": " : "", detail ? detail : "");
    return 1;
}

static int run_case(const char *path, gcase *c) {
    kr_program *p = NULL, *q = NULL;
    kr_diag d;
    int32_t st;
    kr_value *v = NULL, *w = NULL;
    char *t1 = NULL, *t2 = NULL, *vs = NULL, *ws = NULL;
    int bad = 0;
    uint64_t saved = g_work;
    g_cases++;
    if (c->bad_header) return fail_case(path, c, "bad header", NULL);
    if (!c->expect && c->refuse == 1) return fail_case(path, c, "no expect: or refuse:", NULL);
    g_work = c->work;
    st = kr_parse(c->prog ? c->prog : "", c->plen, c->spell, &p, &d);
    g_checks++;
    if (st != ZXV_OK) {
        if (c->refuse == st) goto done;
        bad = fail_case(path, c, "parse refused", d.msg);
        goto done;
    }
    /* canonical fixed point */
    t1 = text_of(prog_text, p);
    st = t1 ? kr_parse(t1, strlen(t1), KR_SPELL_CANON, &q, &d) : ZXV_E_NOMEM;
    g_checks++;
    if (st != ZXV_OK) {
        bad = fail_case(path, c, "the canonical text does not parse", d.msg);
        goto done;
    }
    t2 = text_of(prog_text, q);
    g_checks++;
    if (!t2 || strcmp(t1, t2) != 0) {
        bad = fail_case(path, c, "the canonical text is not a fixed point", t1);
        goto done;
    }
    {
        uint8_t da[32], db[32];
        kr_program_digest(p, da);
        kr_program_digest(q, db);
        g_checks++;
        if (memcmp(da, db, 32) != 0) {
            bad = fail_case(path, c, "digest changed through the canonical text", NULL);
            goto done;
        }
        for (int k = 0; k < 2; k++) {
            const char *grp = k == 0 ? c->same : c->differ;
            uint32_t i;
            if (!grp[0]) continue;
            for (i = 0; i < g_ngroups; i++)
                if (strcmp(g_groups[i].name, grp) == 0 && !g_groups[i].differ) break;
            g_checks++;
            if (i == g_ngroups) {
                if (k == 1) {
                    bad = fail_case(path, c, "differ: names a group not seen yet", grp);
                    goto done;
                }
                if (g_ngroups < MAXG) {
                    snprintf(g_groups[g_ngroups].name, sizeof g_groups[0].name, "%s", grp);
                    memcpy(g_groups[g_ngroups].digest, da, 32);
                    g_ngroups++;
                }
            } else if ((memcmp(g_groups[i].digest, da, 32) == 0) != (k == 0)) {
                bad = fail_case(path, c, k == 0 ? "not the same program as its group" : "the same program as the group it must differ from", grp);
                goto done;
            }
        }
    }
    {
        kr_binding kb[64];
        kr_diag d2;
        int32_t st2;
        for (uint32_t i = 0; i < c->nb; i++) {
            kb[i].name = c->b[i].name;
            kb[i].value = c->b[i].v;
        }
        st = kr_eval(p, kb, c->nb, g_work, &v, &d);
        st2 = kr_eval(q, kb, c->nb, g_work, &w, &d2);
        g_checks++;
        if (st != st2) {
            bad = fail_case(path, c, "the canonical program gives another status", d2.msg);
            goto done;
        }
        if (st != ZXV_OK) {
            g_checks++;
            if (c->refuse != st) {
                char m[400];
                snprintf(m, sizeof m, "%s (expected %s)", d.msg,
                         c->refuse == 1 ? c->expect : kr_status_name(c->refuse));
                bad = fail_case(path, c, "refused", m);
            }
            goto done;
        }
        vs = text_of(val_text, v);
        ws = text_of(val_text, w);
        g_checks += 2;
        if (!vs || !ws || strcmp(vs, ws) != 0) {
            bad = fail_case(path, c, "the canonical program gives another value", ws);
            goto done;
        }
        if (c->refuse != 1) {
            char m[600];
            snprintf(m, sizeof m, "got %s, expected refusal %s", vs, kr_status_name(c->refuse));
            bad = fail_case(path, c, "not refused", m);
            goto done;
        }
        if (strcmp(vs, c->expect) != 0) {
            size_t n = strlen(vs) + strlen(c->expect) + 32u;
            char *m = (char *) malloc(n);
            if (m) snprintf(m, n, "got %s, expected %s", vs, c->expect);
            bad = fail_case(path, c, "wrong value", m);
            free(m);
        }
    }
done:
    g_work = saved;
    free(t1);
    free(t2);
    free(vs);
    free(ws);
    kr_value_destroy(v);
    kr_value_destroy(w);
    kr_program_destroy(p);
    kr_program_destroy(q);
    return bad;
}

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
}

static int header(gcase *c, char *line) {
    char *v = strchr(line, ':');
    if (!v) return 0;
    *v++ = '\0';
    while (*v == ' ') v++;
    if (strcmp(line, "spell") == 0) {
        c->spell = strcmp(v, "apl") == 0 ? KR_SPELL_APL : strcmp(v, "k") == 0 ? KR_SPELL_K
                   : strcmp(v, "canon") == 0 ? KR_SPELL_CANON : KR_SPELL_AUTO;
    } else if (strcmp(line, "bind") == 0) {
        return parse_bind(v, c->b, &c->nb);
    } else if (strcmp(line, "expect") == 0) {
        free(c->expect);
        c->expect = strdup(v);
    } else if (strcmp(line, "refuse") == 0) {
        c->refuse = status_by_name(v);
        if (c->refuse == 1) return 0;
    } else if (strcmp(line, "same") == 0) {
        snprintf(c->same, sizeof c->same, "%s", v);
    } else if (strcmp(line, "differ") == 0) {
        snprintf(c->differ, sizeof c->differ, "%s", v);
    } else if (strcmp(line, "work") == 0) {
        c->work = strtoull(v, NULL, 10);
    } else {
        return 0;
    }
    return 1;
}

static void add_prog(gcase *c, const char *line) {
    size_t n = strlen(line);
    if (c->plen + n + 2u > c->pcap) {
        size_t nc = (c->pcap + n + 2u) * 2u;
        char *nb = (char *) realloc(c->prog, nc);
        if (!nb) {
            c->bad_header = 1;
            return;
        }
        c->prog = nb;
        c->pcap = nc;
    }
    memcpy(c->prog + c->plen, line, n);
    c->plen += n;
    c->prog[c->plen] = '\0';
}

static int cmd_golden(int argc, char **argv) {
    int fails = 0;
    for (int f = 0; f < argc; f++) {
        size_t len = 0;
        char *text = slurp(argv[f], &len);
        char *line, *next;
        gcase c;
        int started = 0, inprog = 0;
        if (!text) {
            fprintf(stderr, "%s: cannot read\n", argv[f]);
            fails++;
            continue;
        }
        memset(&c, 0, sizeof c);
        for (line = text; line && *line; line = next) {
            char *nl = strchr(line, '\n');
            next = nl ? nl + 1 : NULL;
            if (strncmp(line, "===", 3) == 0) {
                char nm[128];
                size_t k = nl ? (size_t) (nl - line) : strlen(line);
                if (started) {
                    fails += run_case(argv[f], &c);
                    gcase_free(&c);
                }
                memset(&c, 0, sizeof c);
                c.refuse = 1;
                started = 1;
                inprog = 0;
                if (k > 3) {
                    size_t s = 3;
                    while (s < k && line[s] == ' ') s++;
                    if (k - s >= sizeof nm) k = s + sizeof nm - 1u;
                    memcpy(nm, line + s, k - s);
                    nm[k - s] = '\0';
                    trim(nm);
                    snprintf(c.name, sizeof c.name, "%s", nm);
                }
                continue;
            }
            if (!started) continue;
            if (inprog) {
                size_t k = nl ? (size_t) (nl - line) + 1u : strlen(line);
                char *seg = (char *) malloc(k + 1u);
                if (!seg) {
                    c.bad_header = 1;
                    continue;
                }
                memcpy(seg, line, k);
                seg[k] = '\0';
                add_prog(&c, seg);
                free(seg);
                continue;
            }
            {
                size_t k = nl ? (size_t) (nl - line) : strlen(line);
                char *h = (char *) malloc(k + 1u);
                if (!h) {
                    c.bad_header = 1;
                    continue;
                }
                memcpy(h, line, k);
                h[k] = '\0';
                trim(h);
                if (h[0] == '\0' || h[0] == '#') {
                } else if (strcmp(h, "program:") == 0) {
                    inprog = 1;
                } else if (!header(&c, h)) {
                    fprintf(stderr, "%s: case %s: bad line '%s'\n", argv[f], c.name, h);
                    c.bad_header = 1;
                }
                free(h);
            }
        }
        if (started) {
            fails += run_case(argv[f], &c);
            gcase_free(&c);
        }
        free(text);
    }
    printf("golden: %llu cases, %llu checks, %d failed\n", g_cases, g_checks, fails);
    return fails ? 1 : 0;
}

static int usage(void) {
    fprintf(stderr,
            "usage: zkron [--apl|--k|--canon] [--display] [--work N] COMMAND ...\n"
            "  eval FILE|-e TEXT [NAME=VALUE ...]   canon FILE|-e TEXT   digest FILE|-e TEXT\n"
            "  same A B   check FILE...   free FILE|-e TEXT   golden FILE...\n");
    return 2;
}

int main(int argc, char **argv) {
    int i = 1;
    while (i < argc && strncmp(argv[i], "--", 2) == 0) {
        if (strcmp(argv[i], "--apl") == 0) g_spell = KR_SPELL_APL;
        else if (strcmp(argv[i], "--k") == 0) g_spell = KR_SPELL_K;
        else if (strcmp(argv[i], "--canon") == 0) g_spell = KR_SPELL_CANON;
        else if (strcmp(argv[i], "--display") == 0) g_display = 1;
        else if (strcmp(argv[i], "--work") == 0 && i + 1 < argc) g_work = strtoull(argv[++i], NULL, 10);
        else return usage();
        i++;
    }
    if (i >= argc) return usage();
    {
        const char *cmd = argv[i];
        int n = argc - i - 1;
        char **a = argv + i + 1;
        if (strcmp(cmd, "eval") == 0) return cmd_eval(n, a);
        if (strcmp(cmd, "canon") == 0) return cmd_canon(n, a);
        if (strcmp(cmd, "digest") == 0) return cmd_digest(n, a);
        if (strcmp(cmd, "same") == 0) return cmd_same(n, a);
        if (strcmp(cmd, "check") == 0) return cmd_check(n, a);
        if (strcmp(cmd, "free") == 0) return cmd_free(n, a);
        if (strcmp(cmd, "golden") == 0) return cmd_golden(n, a);
    }
    return usage();
}
