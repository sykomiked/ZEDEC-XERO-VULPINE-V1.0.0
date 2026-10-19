/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zp_eve.c - Eve steps (sdk/pillars/ada-eve): range-checked record types.
 *
 * The step's file is an Eve layout ("eve NAME" first) or an Ada package
 * specification (anything else); a parse refusal is reported at its line
 * and column. The step then builds ONE value of the record type named by
 * `type=` and fills it from the inputs: every scalar of the record has a
 * path ("amount", "breakdown(1)", "grid(usd, 2).qty") and the input of
 * that name is stored with ze_put_value, exactly. A value outside its
 * range, with more decimals than its type, or not a literal of its
 * enumeration is REFUSED (never rounded, never clipped), at the line of
 * the component's declaration. A field with no input leaves the step
 * NEITHER (pending) and names it.
 *
 * When every field is in, each field's canonical text is read back and
 * output under its path (a String's trailing pad spaces are dropped), and
 * `digest` is the SHA3-256 of the layout's canonical Eve text: the
 * identity of the record format.
 *
 * Step options:
 *   type=NAME          the record type to fill (without it the step only
 *                      checks the layout and outputs its digest)
 *   same=FILE          another file of the template declaring the same
 *                      types in the other spelling (an Ada spec for an
 *                      Eve file, or the reverse). Outputs same = true or
 *                      false; false is a refusal naming the first
 *                      canonical line where the two differ.
 *   sensors=A,B        two fields of the record that are independent
 *                      readings of one quantity (two pressure sensors).
 *                      Each must fit its own field type. When both are in
 *                      and they differ by more than `tolerance`, the step
 *                      is HELD (BOTH): two sources disagree and a person
 *                      decides which to trust; nothing picks one silently.
 *                      A missing reading leaves the step NEITHER.
 *   tolerance=DEC      exact decimal text, compared as integers at the
 *                      sensors' scale (default 0: they must be equal).
 *                      More decimals than the scale is a refusal.
 */
#include "zp_internal.h"

#include <zeve.h>
#include <zxv_api.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_LEAVES 256u
#define PATH_CAP 192u

typedef struct {
    char path[MAX_LEAVES][PATH_CAP];
    char top[MAX_LEAVES][ZE_MAX_NAME + 1]; /* the top-level component */
    uint32_t n;
    int over;
} leaves;

static const char *last_err(char *b, size_t cap) {
    size_t n = 0;
    if (ze_last_error(b, cap, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

/* Eve when the first word that is not a comment is "eve". */
static int is_eve(const char *t, size_t n) {
    size_t i = 0;
    for (;;) {
        while (i < n && isspace((unsigned char) t[i])) i++;
        if (i < n && t[i] == '#') {
            while (i < n && t[i] != '\n') i++;
            continue;
        }
        break;
    }
    return i + 3u < n && memcmp(t + i, "eve", 3) == 0 && isspace((unsigned char) t[i + 3u]);
}

static int32_t parse(const char *t, size_t n, ze_pkg **p, ze_diag *d) {
    memset(d, 0, sizeof *d);
    return is_eve(t, n) ? ze_eve_parse(t, n, p, d) : ze_ada_parse(t, n, p, d);
}

/* ze_diag.msg starts "line L, column C: "; the reason is what follows. */
static const char *diag_reason(const ze_diag *d) {
    const char *m = strstr(d->msg, ": ");
    return m ? m + 2 : d->msg;
}

static int is_ident(int c) { return isalnum(c) || c == '_'; }

/* Offset of the first whole-word, case-insensitive `name` outside
 * comments ('#' in Eve, "--" in Ada). With `decl`, the word must be
 * followed by ':' or ',' (a component declaration). -1 if none. */
static long find_word(const char *t, size_t n, const char *name, int decl) {
    size_t nl = strlen(name);
    int eve = is_eve(t, n);
    for (size_t i = 0; i < n; i++) {
        if ((eve && t[i] == '#') || (!eve && t[i] == '-' && i + 1u < n && t[i + 1u] == '-')) {
            while (i < n && t[i] != '\n') i++;
            continue;
        }
        if (i + nl <= n && (i == 0 || !is_ident((unsigned char) t[i - 1u])) && strncasecmp(t + i, name, nl) == 0 &&
            (i + nl == n || !is_ident((unsigned char) t[i + nl]))) {
            size_t k = i + nl;
            if (!decl) return (long) i;
            while (k < n && (t[k] == ' ' || t[k] == '\t')) k++;
            if (k < n && (t[k] == ':' || t[k] == ',') && !(k + 1u < n && t[k + 1u] == '=')) return (long) i;
        }
    }
    return -1;
}

static void decl_line(const char *t, size_t n, const char *comp, uint32_t *line, uint32_t *col) {
    long off = find_word(t, n, comp, 1);
    *line = *col = 0;
    if (off >= 0) zp_line_col(t, n, (size_t) off, line, col);
}

/* Every scalar path of type `t` (as zeve decode walks them). */
static void walk(const ze_pkg *p, uint32_t t, char *path, size_t pl, const char *top, leaves *lv) {
    ze_type ty;
    if (ze_type_at(p, t, &ty) != ZXV_OK) return;
    if (ty.kind == ZE_K_RECORD) {
        for (uint32_t c = 0; c < ty.count; c++) {
            ze_component cp;
            size_t k = pl, nl;
            if (ze_component_at(p, t, c, &cp) != ZXV_OK) continue;
            nl = strlen(cp.name);
            if (pl + nl + 2u >= PATH_CAP) {
                lv->over = 1;
                continue;
            }
            if (pl) path[k++] = '.';
            memcpy(path + k, cp.name, nl + 1u);
            walk(p, cp.type, path, k + nl, top ? top : cp.name, lv);
            path[pl] = '\0';
        }
    } else if (ty.kind == ZE_K_ARRAY) {
        ze_dim dm[ZE_MAX_DIMS];
        int64_t sub[ZE_MAX_DIMS];
        if (ty.count > MAX_LEAVES) {
            lv->over = 1;
            return;
        }
        for (uint32_t d = 0; d < ty.dims && d < ZE_MAX_DIMS; d++) {
            ze_array_dim(p, t, d, &dm[d]);
            sub[d] = dm[d].lo;
        }
        for (uint32_t e = 0; e < ty.count && !lv->over; e++) {
            char s[PATH_CAP];
            size_t sl = 0;
            s[sl++] = '(';
            for (uint32_t d = 0; d < ty.dims; d++) {
                char one[80];
                size_t ol = 0;
                if (dm[d].index != UINT32_MAX) {
                    ze_type it;
                    if (ze_type_at(p, dm[d].index, &it) == ZXV_OK && it.kind == ZE_K_ENUM)
                        ze_enum_literal(p, dm[d].index, (uint32_t) sub[d], one, sizeof one, &ol);
                }
                if (!ol) ol = (size_t) snprintf(one, sizeof one, "%lld", (long long) sub[d]);
                if (sl + ol + 4u >= sizeof s) break;
                if (d) {
                    s[sl++] = ',';
                    s[sl++] = ' ';
                }
                memcpy(s + sl, one, ol);
                sl += ol;
            }
            s[sl++] = ')';
            s[sl] = '\0';
            if (pl + sl + 1u < PATH_CAP) {
                memcpy(path + pl, s, sl + 1u);
                walk(p, ty.elem, path, pl + sl, top, lv);
                path[pl] = '\0';
            } else {
                lv->over = 1;
            }
            for (uint32_t d = ty.dims; d > 0; d--) {
                if (++sub[d - 1u] <= dm[d - 1u].hi) break;
                sub[d - 1u] = dm[d - 1u].lo;
            }
        }
    } else {
        if (lv->n >= MAX_LEAVES) {
            lv->over = 1;
            return;
        }
        snprintf(lv->path[lv->n], PATH_CAP, "%s", path);
        snprintf(lv->top[lv->n], sizeof lv->top[0], "%s", top ? top : path);
        lv->n++;
    }
}

static char *canon(const ze_pkg *p) {
    size_t need = 0;
    char *b;
    ze_pkg_eve(p, NULL, 0, &need);
    if (need == 0) return NULL;
    b = (char *) malloc(need + 1u);
    if (!b) return NULL;
    if (ze_pkg_eve(p, b, need + 1u, &need) != ZXV_OK) {
        free(b);
        return NULL;
    }
    b[need] = '\0';
    return b;
}

/* Compare with `same=`: 1 same, 0 refused (reported), -1 out of memory. */
static int compare_same(zp_step *s, const ze_pkg *p, const char *fname) {
    const zp_file *f = zp_step_file(s, fname);
    ze_pkg *q = NULL;
    ze_diag d;
    int32_t st, same = 0;
    char why[600];
    if (!f) {
        snprintf(why, sizeof why, "same=%s: the template has no such file", fname);
        zp_refuse(s, ZP_E_FILE, NULL, 0, 0, why);
        return 0;
    }
    st = parse(f->text, f->len, &q, &d);
    if (st != ZXV_OK) {
        snprintf(why, sizeof why, "%s line %u, column %u: %s", fname, (unsigned) d.line, (unsigned) d.col,
                 diag_reason(&d));
        zp_refuse(s, st, ze_status_name(st), 0, 0, why);
        return 0;
    }
    ze_pkg_same(p, q, &same);
    zp_output(s, "same", same ? "true" : "false");
    if (!same) {
        char *a = canon(p), *b = canon(q);
        const char *x = a, *y = b;
        uint32_t ln = 1, fl = 0, fc = 0;
        if (!a || !b) {
            free(a);
            free(b);
            ze_pkg_destroy(q);
            return -1;
        }
        while (*x && *x == *y) {
            if (*x == '\n') ln++;
            x++;
            y++;
        }
        while (x > a && x[-1] != '\n') x--;
        y = b + (x - a);
        {
            size_t xl = strcspn(x, "\n"), yl = strcspn(y, "\n");
            char word[ZE_MAX_NAME + 1];
            size_t wl = 0;
            const char *w = y;
            long off;
            while (*w == ' ') w++;
            while (is_ident((unsigned char) w[wl]) && wl < ZE_MAX_NAME) wl++;
            memcpy(word, w, wl);
            word[wl] = '\0';
            off = wl ? find_word(f->text, f->len, word, 0) : -1;
            if (off >= 0) zp_line_col(f->text, f->len, (size_t) off, &fl, &fc);
            snprintf(why, sizeof why,
                     "the two spellings declare different types (digests differ); first difference at canonical line "
                     "%u: this file has '%.*s', %s has '%.*s'",
                     (unsigned) ln, (int) (xl > 120u ? 120u : xl), x, fname, (int) (yl > 120u ? 120u : yl), y);
            if (fl) {
                size_t k = strlen(why);
                snprintf(why + k, sizeof why - k, " (%s line %u, column %u)", fname, (unsigned) fl, (unsigned) fc);
            }
        }
        zp_refuse(s, ZXV_E_VERIFY, "ZXV_E_VERIFY", 0, 0, why);
        free(a);
        free(b);
    }
    ze_pkg_destroy(q);
    return same ? 1 : 0;
}

/* Exact decimal text -> units at `scale`. 0 ok, -1 malformed, -2 too many
 * decimals, -3 too large. */
static int parse_units(const char *t, uint32_t scale, uint64_t *out) {
    uint64_t v = 0;
    uint32_t fd = 0;
    int dot = 0, any = 0;
    for (; *t; t++) {
        if (*t == '.' && !dot) {
            dot = 1;
            continue;
        }
        if (!isdigit((unsigned char) *t)) return -1;
        any = 1;
        if (dot && fd >= scale) {
            if (*t != '0') return -2;
            continue;
        }
        if (v > (UINT64_MAX - 9u) / 10u) return -3;
        v = v * 10u + (uint64_t) (*t - '0');
        if (dot) fd++;
    }
    if (!any) return -1;
    for (; fd < scale; fd++) {
        if (v > UINT64_MAX / 10u) return -3;
        v *= 10u;
    }
    *out = v;
    return 0;
}

static void units_text(uint64_t u, uint32_t scale, char *b, size_t cap) {
    char d[48];
    int n;
    if (scale > 18u) scale = 18u;
    n = snprintf(d, sizeof d, "%0*llu", (int) scale + 1, (unsigned long long) u);
    if (scale == 0) snprintf(b, cap, "%s", d);
    else snprintf(b, cap, "%.*s.%s", n - (int) scale, d, d + n - (int) scale);
}

static void sensors(zp_step *s, const ze_pkg *p, uint32_t rec, const uint8_t *v, size_t vl, const char *spec) {
    char a[PATH_CAP], b[PATH_CAP], why[1200], ta[64], tb[64], td[64], tt[64];
    const char *comma = strchr(spec, ',');
    const char *tol = zp_step_opt(s, "tolerance");
    int64_t ua = 0, ub = 0;
    uint32_t sa = 0, sb = 0;
    uint64_t diff, tu = 0;
    int32_t st;
    int pr;
    if (!comma || (size_t) (comma - spec) >= sizeof a || strlen(comma + 1) >= sizeof b) {
        zp_refuse(s, ZXV_E_ARG, "ZXV_E_ARG", 0, 0, "sensors= takes two field names: sensors=A,B");
        return;
    }
    memcpy(a, spec, (size_t) (comma - spec));
    a[comma - spec] = '\0';
    snprintf(b, sizeof b, "%s", comma + 1);
    if (!zp_input(s, a) || !zp_input(s, b)) return; /* already pending */
    st = ze_get_decimal(p, rec, v, vl, a, &ua, &sa);
    if (st == ZXV_OK) st = ze_get_decimal(p, rec, v, vl, b, &ub, &sb);
    if (st != ZXV_OK || sa != sb) {
        snprintf(why, sizeof why, "sensors=%s: both must be numeric fields of the same scale", spec);
        if (st == ZXV_OK) st = ZE_E_CONSTRAINT;
        zp_refuse(s, st, ze_status_name(st), 0, 0, why);
        return;
    }
    pr = parse_units(tol ? tol : "0", sa, &tu);
    if (pr) {
        snprintf(why, sizeof why, "tolerance=%s: %s", tol ? tol : "0",
                 pr == -2 ? "more decimals than the sensors' type has (not rounded)"
                 : pr == -3 ? "too large" : "not a plain decimal number");
        st = pr == -1 ? ZXV_E_FORMAT : ZE_E_FIT;
        zp_refuse(s, st, ze_status_name(st), 0, 0, why);
        return;
    }
    diff = ua >= ub ? (uint64_t) ua - (uint64_t) ub : (uint64_t) ub - (uint64_t) ua;
    {
        size_t n = 0;
        ze_get_value(p, rec, v, vl, a, ta, sizeof ta, &n);
        ze_get_value(p, rec, v, vl, b, tb, sizeof tb, &n);
    }
    units_text(diff, sa, td, sizeof td);
    units_text(tu, sa, tt, sizeof tt);
    zp_output(s, "sensor_difference", td);
    if (diff > tu) {
        snprintf(why, sizeof why,
                 "sensors disagree: %s = %s and %s = %s differ by %s, more than the tolerance %s; held for an operator "
                 "to check the instruments",
                 a, ta, b, tb, td, tt);
        zp_hold(s, why);
    } else {
        zp_notef(s, "sensors agree: %s and %s differ by %s, within the tolerance %s", a, b, td, tt);
    }
}

/* Is `path` one of the two names in "A,B"? */
static int is_sensor(const char *spec, const char *path) {
    size_t n = strlen(path);
    const char *c = strchr(spec, ',');
    if (!c) return 0;
    return ((size_t) (c - spec) == n && strncmp(spec, path, n) == 0) || strcmp(c + 1, path) == 0;
}

void zp_lang_eve(zp_step *s, const char *src, size_t len) {
    ze_pkg *p = NULL;
    ze_diag d;
    int32_t st;
    const char *tname = zp_step_opt(s, "type");
    const char *same = zp_step_opt(s, "same");
    const char *sens = zp_step_opt(s, "sensors");
    uint8_t dg[32];
    char hex[65], why[600], eb[400];
    leaves *lv = NULL;
    uint8_t *val = NULL;
    ze_type ty;
    uint32_t rec = 0, missing = 0;
    int refused = 0;

    st = parse(src, len, &p, &d);
    if (st != ZXV_OK) {
        zp_refuse(s, st, ze_status_name(st), d.line, d.col, diag_reason(&d));
        return;
    }
    ze_pkg_digest(p, dg);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", dg[i]);

    if (same) {
        int r = compare_same(s, p, same);
        if (r < 0) zp_refuse(s, ZXV_E_NOMEM, ze_status_name(ZXV_E_NOMEM), 0, 0, "out of memory");
        if (r <= 0) goto done;
    }
    if (!tname) {
        zp_output(s, "digest", hex);
        goto done;
    }
    if (ze_type_find(p, tname, &rec) != ZXV_OK || ze_type_at(p, rec, &ty) != ZXV_OK || ty.kind != ZE_K_RECORD) {
        snprintf(why, sizeof why, "type=%s: the layout declares no record type of that name", tname);
        zp_refuse(s, ZE_E_NAME, ze_status_name(ZE_E_NAME), 0, 0, why);
        goto done;
    }
    lv = (leaves *) calloc(1, sizeof *lv);
    val = (uint8_t *) calloc(ty.size ? (size_t) ty.size : 1u, 1);
    if (!lv || !val) {
        zp_refuse(s, ZXV_E_NOMEM, ze_status_name(ZXV_E_NOMEM), 0, 0, "out of memory");
        goto done;
    }
    {
        char path[PATH_CAP] = "";
        walk(p, rec, path, 0, NULL, lv);
    }
    if (lv->over) {
        snprintf(why, sizeof why, "%s has more than %u fields (or a path too long) for one Playground step", tname,
                 MAX_LEAVES);
        zp_refuse(s, ZE_E_LIMIT, ze_status_name(ZE_E_LIMIT), 0, 0, why);
        goto done;
    }
    ze_value_init(p, rec, val, (size_t) ty.size);
    for (uint32_t i = 0; i < lv->n; i++) {
        const char *in = zp_input(s, lv->path[i]);
        if (!in) {
            if (sens && is_sensor(sens, lv->path[i]))
                snprintf(why, sizeof why, "the sensor reading %s is missing; nothing is proposed until both readings are in",
                         lv->path[i]);
            else
                snprintf(why, sizeof why, "needs an input named %s", lv->path[i]);
            zp_pending(s, why);
            missing++;
            continue;
        }
        st = ze_put_value(p, rec, val, (size_t) ty.size, lv->path[i], in, strlen(in));
        if (st != ZXV_OK) {
            uint32_t l, c;
            decl_line(src, len, lv->top[i], &l, &c);
            snprintf(why, sizeof why, "%s", last_err(eb, sizeof eb));
            zp_refuse(s, st, ze_status_name(st), l, c, why);
            refused = 1;
            break;
        }
    }
    if (!refused && !missing) {
        for (uint32_t i = 0; i < lv->n; i++) {
            char b[1024];
            size_t bl = 0;
            ze_type ft;
            uint32_t fti = 0;
            if (ze_get_value(p, rec, val, (size_t) ty.size, lv->path[i], b, sizeof b, &bl) != ZXV_OK) continue;
            b[bl < sizeof b ? bl : sizeof b - 1u] = '\0';
            if (ze_find(p, rec, lv->path[i], &fti, NULL) == ZXV_OK && ze_type_at(p, fti, &ft) == ZXV_OK &&
                ft.kind == ZE_K_TEXT)
                while (bl > 0 && b[bl - 1u] == ' ') b[--bl] = '\0';
            zp_output(s, lv->path[i], b);
        }
    }
    if (!refused && sens) sensors(s, p, rec, val, (size_t) ty.size, sens);
    zp_output(s, "digest", hex);
done:
    free(val);
    free(lv);
    ze_pkg_destroy(p);
}
