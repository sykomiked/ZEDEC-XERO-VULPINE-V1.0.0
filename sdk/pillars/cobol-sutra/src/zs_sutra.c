/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_sutra.c — Sutra, the short form of a copybook. One field per line,
 * nesting by indentation (spaces only), the same pictures as COBOL:
 *
 *   sutra   = header { field }
 *   header  = "record" name                         (column 1)
 *   field   = indent name [ "[" count "]" ] ":" [ spec ]
 *   spec    = picture { usage | sign }
 *   usage   = "display" | "comp" | "binary" | "comp-3" | "packed"
 *   sign    = "sign" ( "leading" | "trailing" ) [ "separate" ]
 *   comment = "#" to the end of the line
 *
 * A field with no spec is a group; the fields indented under it are its
 * items. Siblings line up exactly. Keywords and names are
 * case-insensitive. Integer code only (R11). */
#include <string.h>

#include "zs_internal.h"

typedef struct {
    const char *text;
    size_t len;
    zs_diag *d;
} sp;

#define ERR(st, off, ...) zs_diag_set(s->d, (st), s->text, s->len, (off), __VA_ARGS__)

static int name_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

/* case-insensitive match of x[a, b) with a lower-case word */
static int weq(const char *x, size_t a, size_t b, const char *w) {
    size_t k = strlen(w);
    if (b - a != k) return 0;
    for (size_t i = 0; i < k; i++) {
        char c = x[a + i];
        if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
        if (c != w[i]) return 0;
    }
    return 1;
}

static void copy_name(char *dst, const char *src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char) (src[i] - 32) : src[i];
    dst[n] = '\0';
}

#define MAX_STACK (ZS_MAX_DEPTH + 1u)

int32_t zs_sutra_parse(const char *text, size_t len, zs_layout **out_layout, zs_diag *diag) {
    sp ss, *s = &ss;
    zs_builder b = {0};
    uint32_t stack[MAX_STACK], depth = 0;
    size_t pos = 0;
    int32_t st;
    int have_header = 0;
    if (diag) memset(diag, 0, sizeof *diag);
    if (!out_layout || (!text && len)) return ZXV_E_ARG;
    *out_layout = NULL;
    if (!text) text = "";
    s->text = text;
    s->len = len;
    s->d = diag;
    if (len > ZS_MAX_TEXT) return ERR(ZS_E_SYNTAX, 0, "Sutra text is longer than 1 MiB");
    if (zs_utf8_check((const uint8_t *) text, len) < len)
        return ERR(ZS_E_SYNTAX, zs_utf8_check((const uint8_t *) text, len), "not valid UTF-8");
    if ((st = zs_builder_init(&b)) != ZXV_OK) return st;

    while (pos < len) {
        size_t ls = pos, le = pos, e, i, ns;
        uint32_t indent;
        zs_item it;
        uint32_t idx;
        while (le < len && text[le] != '\n') le++;
        pos = le < len ? le + 1u : le;
        /* the code part: up to '#', without trailing spaces or CR */
        e = ls;
        while (e < le && text[e] != '#') {
            uint8_t u = (uint8_t) text[e];
            if (text[e] == '\t') {
                st = ERR(ZS_E_SYNTAX, e, "tabs are not allowed; indent with spaces");
                goto done;
            }
            if (u >= 0x80u || (u < 0x20u && text[e] != '\r')) {
                st = ERR(ZS_E_SYNTAX, e, "only ASCII may appear outside comments");
                goto done;
            }
            e++;
        }
        while (e > ls && (text[e - 1u] == ' ' || text[e - 1u] == '\r')) e--;
        i = ls;
        while (i < e && text[i] == ' ') i++;
        if (i == e) continue; /* blank or comment line */
        indent = (uint32_t) (i - ls);

        if (!have_header) {
            size_t a;
            if (indent != 0 || i + 6u > e || !weq(text, i, i + 6u, "record") ||
                (i + 6u < e && text[i + 6u] != ' ')) {
                st = ERR(ZS_E_SYNTAX, i, "a Sutra file starts with 'record NAME' in column 1");
                goto done;
            }
            i += 6u;
            while (i < e && text[i] == ' ') i++;
            a = i;
            while (i < e && name_char(text[i])) i++;
            if (i != e || !zs_valid_name(text + a, i - a) ||
                (zs_reserved(text + a, i - a) && !weq(text, a, i, "filler"))) {
                st = ERR(ZS_E_SYNTAX, a, "expected a record name (1-30 letters, digits and hyphens)");
                goto done;
            }
            memset(&it, 0, sizeof it);
            copy_name(it.f.name, text + a, i - a);
            it.f.kind = ZS_K_GROUP;
            it.pos.offset = ls;
            if ((st = zs_builder_add(&b, &it, &idx)) != ZXV_OK) goto done;
            stack[0] = idx;
            depth = 0;
            have_header = 1;
            continue;
        }
        if (indent == 0) {
            st = ERR(ZS_E_SYNTAX, i,
                     weq(text, i, i + (e - i < 6u ? e - i : 6u), "record")
                         ? "only one record per Sutra file"
                         : "fields are indented under the record");
            goto done;
        }
        memset(&it, 0, sizeof it);
        it.level = (int32_t) indent;
        it.pos.offset = i;
        /* where does it go? */
        while (depth > 0 && (uint32_t) b.l->items[stack[depth]].level >= indent) depth--;
        {
            const zs_item *p = &b.l->items[stack[depth]];
            uint32_t pi = stack[depth];
            if (p->f.kind != ZS_K_GROUP) {
                st = ERR(ZS_E_SYNTAX, i, "%s has a picture, so no fields can be indented under it",
                         p->f.name);
                goto done;
            }
            if (p->nchild > 0)
                for (uint32_t k = pi + 1u; k < b.l->n; k++)
                    if (b.l->items[k].f.parent == pi) {
                        if ((uint32_t) b.l->items[k].level != indent) {
                            st = ERR(ZS_E_SYNTAX, i,
                                     "indented %u spaces; the fields before it in %s are indented %u",
                                     (unsigned) indent, p->f.name,
                                     (unsigned) b.l->items[k].level);
                            goto done;
                        }
                        break;
                    }
            if (depth + 1u >= MAX_STACK) {
                st = ERR(ZS_E_SYNTAX, i, "nested more than 48 levels deep");
                goto done;
            }
            it.f.parent = pi;
            it.f.depth = p->f.depth + 1u;
        }
        /* name [count] : */
        ns = i;
        while (i < e && name_char(text[i])) i++;
        if (weq(text, ns, i, "filler")) {
            memcpy(it.f.name, "FILLER", 7);
        } else if (!zs_valid_name(text + ns, i - ns)) {
            st = ERR(ZS_E_SYNTAX, ns, "expected a field name (1-30 letters, digits and hyphens)");
            goto done;
        } else if (zs_reserved(text + ns, i - ns)) {
            st = ERR(ZS_E_SYNTAX, ns, "'%.*s' is a COBOL reserved word; it cannot name a field",
                     (int) (i - ns), text + ns);
            goto done;
        } else {
            copy_name(it.f.name, text + ns, i - ns);
        }
        if (i < e && text[i] == '[') {
            uint64_t n = 0;
            size_t a = ++i;
            while (i < e && text[i] >= '0' && text[i] <= '9' && i - a < 9u)
                n = n * 10u + (uint64_t) (text[i++] - '0');
            if (i == a || i >= e || text[i] != ']' || n < 1 || n > ZS_MAX_OCCURS) {
                st = ERR(ZS_E_SYNTAX, a, "expected a count 1..65535 and ']', as in lines[3]");
                goto done;
            }
            it.f.occurs = (uint32_t) n;
            i++;
        }
        while (i < e && text[i] == ' ') i++;
        if (i >= e || text[i] != ':') {
            st = ERR(ZS_E_SYNTAX, i, "expected ':' after the field name");
            goto done;
        }
        i++;
        it.f.kind = ZS_K_GROUP;
        /* spec: picture then words */
        {
            int have_pic = 0, have_usage = 0, have_sign = 0;
            for (;;) {
                size_t a;
                while (i < e && text[i] == ' ') i++;
                if (i >= e) break;
                a = i;
                while (i < e && text[i] != ' ') i++;
                if (!have_pic) {
                    char msg[160];
                    size_t at = 0;
                    int32_t sg = 0;
                    st = zs_picture_parse(text + a, i - a, &it.f.kind, &it.f.digits, &it.f.scale,
                                          &sg, &at, msg, sizeof msg);
                    if (st != ZXV_OK) {
                        st = ERR(st, a + at, "%s", msg);
                        goto done;
                    }
                    it.is_signed = sg;
                    have_pic = 1;
                } else if (weq(text, a, i, "comp") || weq(text, a, i, "binary") ||
                           weq(text, a, i, "comp-3") || weq(text, a, i, "packed") ||
                           weq(text, a, i, "display")) {
                    if (have_usage++) {
                        st = ERR(ZS_E_SYNTAX, a, "usage given twice");
                        goto done;
                    }
                    it.f.usage = (weq(text, a, i, "comp") || weq(text, a, i, "binary")) ? ZS_U_COMP
                                 : weq(text, a, i, "display")                            ? ZS_U_DISPLAY
                                                                                         : ZS_U_COMP3;
                    it.usage_set = 1;
                } else if (weq(text, a, i, "sign")) {
                    size_t c0;
                    int lead;
                    if (have_sign++) {
                        st = ERR(ZS_E_SYNTAX, a, "sign given twice");
                        goto done;
                    }
                    while (i < e && text[i] == ' ') i++;
                    c0 = i;
                    while (i < e && text[i] != ' ') i++;
                    if (!weq(text, c0, i, "leading") && !weq(text, c0, i, "trailing")) {
                        st = ERR(ZS_E_SYNTAX, c0, "expected 'leading' or 'trailing' after sign");
                        goto done;
                    }
                    lead = weq(text, c0, i, "leading");
                    {
                        size_t save = i, c1;
                        while (i < e && text[i] == ' ') i++;
                        c1 = i;
                        while (i < e && text[i] != ' ') i++;
                        if (weq(text, c1, i, "separate")) {
                            it.sign_clause = lead ? ZS_SIGN_LEADING_SEP : ZS_SIGN_TRAILING_SEP;
                        } else {
                            i = save;
                            it.sign_clause = lead ? ZS_SIGN_LEADING : ZS_SIGN_TRAILING;
                        }
                    }
                } else if (weq(text, a, i, "comp-1") || weq(text, a, i, "comp-2")) {
                    st = ERR(ZS_E_UNSUPPORTED, a,
                             "floating point (comp-1, comp-2) is refused by design: ZXV has no "
                             "floats (R11)");
                    goto done;
                } else {
                    st = ERR(ZS_E_SYNTAX, a,
                             "expected comp, comp-3, display or sign ... after the picture, found "
                             "'%.*s'",
                             (int) (i - a > 40 ? 40 : i - a), text + a);
                    goto done;
                }
            }
        }
        if ((st = zs_builder_add(&b, &it, &idx)) != ZXV_OK) {
            if (st == ZXV_E_CAPACITY) st = ERR(ZS_E_UNSUPPORTED, ns, "more than 4096 fields");
            goto done;
        }
        stack[++depth] = idx;
    }
    if (!have_header) {
        st = ERR(ZS_E_SYNTAX, len, "a Sutra file starts with 'record NAME'");
        goto done;
    }
    st = zs_builder_finish(&b, text, len, diag, out_layout);
done:
    zs_builder_free(&b);
    return st;
}
