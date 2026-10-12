/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxsh_core.c — the paraconsistent shell's language and logic. See
 * zxsh_core.h (S1-S9). No allocation, no stdio. */
#include "zxsh_core.h"
#include <string.h>
#include "../../kernel/src/robin_debanks/sha256.h"

enum { K_WORD, K_NL, K_SEMI, K_AND, K_OR, K_PIPE, K_XCHK, K_LT, K_GT, K_GTGT, K_EOF };

#define R(run) ((run) && !sh->aborted && !sh->exiting && !sh->syntax && !sh->need_more)

static const char *const HOST_BUILTINS[] = {"cd", "pwd", "history", "qlog", "zxv", NULL};

/* ---------------------------------------------------------------- output */

static void emit(zxsh_t *sh, int fd, const char *s, size_t n)
{
    if (fd == 1 && sh->cap_buf) {
        for (size_t i = 0; i < n && *sh->cap_len < ZXSH_CAPTURE; i++) sh->cap_buf[(*sh->cap_len)++] = s[i];
        return;
    }
    if (sh->ops && sh->ops->out) sh->ops->out(sh->u, fd, s, n);
}

static void say(zxsh_t *sh, int fd, const char *s)
{
    emit(sh, fd, s, strlen(s));
}

static void say_u(zxsh_t *sh, int fd, uint32_t v)
{
    char b[12];
    int i = 11;
    b[i] = 0;
    do {
        b[--i] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && i > 0);
    say(sh, fd, b + i);
}

static void set_why(zxsh_t *sh, const char *a, const char *b)
{
    size_t n = 0;
    for (; a && *a && n + 1 < ZXSH_WHY_MAX; a++) sh->why[n++] = *a;
    for (; b && *b && n + 1 < ZXSH_WHY_MAX; b++) sh->why[n++] = *b;
    sh->why[n] = 0;
}

static void say_tag(zxsh_t *sh, int fd, lpx_t t)
{
    say(sh, fd, lpx_base_name(lpx_base(t)));
    say(sh, fd, "/");
    say(sh, fd, lpx_pol_name(lpx_pol(t)));
    say(sh, fd, "/");
    say(sh, fd, lpx_phase_name(lpx_phase(t)));
}

char zxsh_letter(lpx_t t)
{
    static const char L[4] = {'F', 'N', 'B', 'T'};
    return lpx_forbidden(t) ? '!' : L[lpx_base(t)];
}

const char *zxsh_base_word(lpx_t t)
{
    return lpx_base_name(lpx_base(t));
}

/* ------------------------------------------------------------- variables */

static bool name_ok(const char *s, size_t n)
{
    if (n == 0 || n >= ZXSH_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        if (!alpha && !(i > 0 && c >= '0' && c <= '9')) return false;
    }
    return true;
}

static zxsh_var_t *var_find(const zxsh_t *sh, const char *name, size_t n)
{
    for (unsigned i = 0; i < ZXSH_MAX_VARS; i++) {
        const zxsh_var_t *v = &sh->vars[i];
        if (v->name[0] && strlen(v->name) == n && memcmp(v->name, name, n) == 0)
            return (zxsh_var_t *) v;
    }
    return NULL;
}

const char *zxsh_get(const zxsh_t *sh, const char *name)
{
    zxsh_var_t *v = var_find(sh, name, strlen(name));
    return v ? v->value : NULL;
}

bool zxsh_set(zxsh_t *sh, const char *name, const char *value)
{
    size_t n = strlen(name), m = strlen(value);
    if (!name_ok(name, n) || m >= ZXSH_VALUE_MAX) return false;
    zxsh_var_t *v = var_find(sh, name, n);
    for (unsigned i = 0; !v && i < ZXSH_MAX_VARS; i++)
        if (!sh->vars[i].name[0]) v = &sh->vars[i];
    if (!v) return false;
    memcpy(v->name, name, n + 1);
    memcpy(v->value, value, m + 1);
    return true;
}

static void var_unset(zxsh_t *sh, const char *name)
{
    zxsh_var_t *v = var_find(sh, name, strlen(name));
    if (v) memset(v, 0, sizeof *v);
}

/* ---------------------------------------------------------- capabilities */

static const struct {
    const char *name;
    uint32_t bit;
} CAPS[] = {{"exec", ZXSH_CAP_EXEC},
            {"read", ZXSH_CAP_READ},
            {"write", ZXSH_CAP_WRITE},
            {"net", ZXSH_CAP_NET},
            {"resolve", ZXSH_CAP_RESOLVE}};
#define NCAPS (sizeof CAPS / sizeof CAPS[0])

uint32_t zxsh_cap_parse(const char *list)
{
    uint32_t m = 0;
    const char *p = list;
    while (p && *p) {
        const char *e = p;
        while (*e && *e != ',') e++;
        size_t n = (size_t) (e - p);
        bool found = false;
        if (n == 3 && memcmp(p, "all", 3) == 0) {
            m |= ZXSH_CAP_ALL;
            found = true;
        } else if (n == 4 && memcmp(p, "none", 4) == 0) {
            found = true;
        }
        for (unsigned i = 0; !found && i < NCAPS; i++)
            if (strlen(CAPS[i].name) == n && memcmp(CAPS[i].name, p, n) == 0) {
                m |= CAPS[i].bit;
                found = true;
            }
        if (!found) return 0;
        p = *e ? e + 1 : e;
    }
    return m;
}

/* ----------------------------------------------------------------- lexer */

static bool is_opch(char c)
{
    return c == ';' || c == '|' || c == '&' || c == '<' || c == '>' || c == '\n';
}

static bool push_tok(zxsh_t *sh, uint8_t kind, uint32_t off, uint32_t len)
{
    if (sh->ntok >= ZXSH_MAX_TOKENS - 1u) {
        sh->syntax = true;
        set_why(sh, "input too long", NULL);
        return false;
    }
    sh->tok[sh->ntok].kind = kind;
    sh->tok[sh->ntok].off = off;
    sh->tok[sh->ntok].len = len;
    sh->ntok++;
    return true;
}

static void lex(zxsh_t *sh)
{
    const char *s = sh->src;
    uint32_t i = 0;
    sh->ntok = 0;
    while (s[i]) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            i++;
            continue;
        }
        if (c == '\\' && s[i + 1] == '\n') {
            i += 2;
            continue;
        }
        if (c == '\\' && s[i + 1] == 0) { /* continuation at end of input */
            sh->need_more = true;
            return;
        }
        if (c == '#') {
            while (s[i] && s[i] != '\n') i++;
            continue;
        }
        uint8_t k = K_EOF;
        uint32_t n = 1;
        if (c == '\n') k = K_NL;
        else if (c == ';') k = K_SEMI;
        else if (c == '&' && s[i + 1] == '&') k = K_AND, n = 2;
        else if (c == '&') {
            sh->syntax = true;
            set_why(sh, "background jobs (&) are not supported; use && or ;", NULL);
            return;
        } else if (c == '|' && s[i + 1] == '|') k = K_OR, n = 2;
        else if (c == '|') k = K_PIPE;
        else if (c == '<' && s[i + 1] == '?' && s[i + 2] == '>') k = K_XCHK, n = 3;
        else if (c == '<') k = K_LT;
        else if (c == '>' && s[i + 1] == '>') k = K_GTGT, n = 2;
        else if (c == '>') k = K_GT;
        if (k != K_EOF) {
            if (!push_tok(sh, k, i, n)) return;
            i += n;
            continue;
        }
        /* a word */
        uint32_t st = i;
        while (s[i] && !(s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || is_opch(s[i]))) {
            if (s[i] == '\\') {
                if (!s[i + 1]) {
                    sh->need_more = true;
                    return;
                }
                i += 2;
            } else if (s[i] == '\'' || s[i] == '"') {
                char q = s[i++];
                while (s[i] && s[i] != q) {
                    if (q == '"' && s[i] == '\\' && s[i + 1]) i++;
                    i++;
                }
                if (!s[i]) {
                    sh->need_more = true;
                    return;
                }
                i++;
            } else {
                i++;
            }
        }
        if (!push_tok(sh, K_WORD, st, i - st)) return;
    }
    push_tok(sh, K_EOF, i, 0);
}

static bool tok_is(const zxsh_t *sh, uint32_t i, const char *kw)
{
    const zxsh_tok_t *t = &sh->tok[i];
    size_t n = strlen(kw);
    return t->kind == K_WORD && t->len == n && memcmp(sh->src + t->off, kw, n) == 0;
}

static bool at_terminator(const zxsh_t *sh)
{
    static const char *const T[] = {"then", "else", "fi", "do", "done", NULL};
    if (sh->tok[sh->pos].kind == K_EOF) return true;
    for (unsigned i = 0; T[i]; i++)
        if (tok_is(sh, sh->pos, T[i])) return true;
    if (sh->in_clause && (tok_is(sh, sh->pos, "both") || tok_is(sh, sh->pos, "neither"))) return true;
    return false;
}

static void syntax(zxsh_t *sh, const char *msg)
{
    if (sh->syntax || sh->need_more) return;
    if (sh->tok[sh->pos].kind == K_EOF) {
        sh->need_more = true; /* the construct may continue on the next line */
        return;
    }
    sh->syntax = true;
    set_why(sh, "syntax: ", msg);
}

static void skip_nl(zxsh_t *sh)
{
    while (sh->tok[sh->pos].kind == K_NL) sh->pos++;
}

/* ------------------------------------------------------------- expansion */

/* Expand token i into the arena. *glob is set when an unquoted * ? [ was
 * seen. NULL when the arena is full. */
static char *expand(zxsh_t *sh, uint32_t i, bool *glob)
{
    const char *s = sh->src + sh->tok[i].off;
    uint32_t n = sh->tok[i].len;
    char *d = sh->arena + sh->arena_used;
    uint32_t cap = ZXSH_ARENA - sh->arena_used, o = 0;
    bool dq = false;
    *glob = false;
#define PUT(ch)                                                                                    \
    do {                                                                                           \
        if (o + 1 >= cap) return NULL;                                                             \
        d[o++] = (ch);                                                                             \
    } while (0)
    for (uint32_t k = 0; k < n; k++) {
        char c = s[k];
        if (!dq && c == '\'') {
            for (k++; k < n && s[k] != '\''; k++) PUT(s[k]);
        } else if (c == '"') {
            dq = !dq;
        } else if (c == '\\' && k + 1 < n) {
            char x = s[++k];
            if (dq && x != '"' && x != '\\' && x != '$') PUT('\\');
            PUT(x);
        } else if (c == '$' && k + 1 < n) {
            const char *val = NULL;
            uint32_t a = k + 1, b;
            if (s[a] == '?') {
                val = zxsh_base_word(sh->last);
                b = a + 1;
            } else if (s[a] == '{') {
                b = a + 1;
                while (b < n && s[b] != '}') b++;
                if (b >= n) return NULL;
                zxsh_var_t *v = var_find(sh, s + a + 1, b - a - 1);
                val = v ? v->value : "";
                b++;
            } else {
                b = a;
                while (b < n && name_ok(s + a, b - a + 1)) b++;
                if (b == a) {
                    PUT('$');
                    continue;
                }
                zxsh_var_t *v = var_find(sh, s + a, b - a);
                val = v ? v->value : "";
            }
            for (; *val; val++) PUT(*val);
            k = b - 1;
        } else if (!dq && c == '~' && k == 0 && (n == 1 || s[1] == '/')) {
            const char *h = zxsh_get(sh, "HOME");
            if (!h) h = "~";
            for (; *h; h++) PUT(*h);
        } else {
            if (!dq && (c == '*' || c == '?' || c == '[')) *glob = true;
            PUT(c);
        }
    }
#undef PUT
    d[o] = 0;
    sh->arena_used += o + 1;
    return d;
}

/* ------------------------------------------------------------ quarantine */

static void quarantine(zxsh_t *sh, lpx_t v, uint32_t off, uint32_t end)
{
    uint8_t dig[32];
    sha256((const uint8_t *) sh->src + off, end - off, dig);
    if (!lpx_qlog_append(&sh->qlog, v, 1, 0, dig)) {
        sh->locked = true; /* S6: a full log fails closed */
        say(sh, 2, "zxsh: quarantine log full; the shell is locked\n");
    } else if (sh->ops->quarantine) {
        char cmd[ZXSH_HOLD_TEXT];
        uint32_t n = end - off < sizeof cmd - 1 ? end - off : (uint32_t) sizeof cmd - 1;
        memcpy(cmd, sh->src + off, n);
        cmd[n] = 0;
        sh->ops->quarantine(sh->u, &sh->qlog.rec[sh->qlog.n - 1], cmd);
    }
    sh->aborted = true;
    say(sh, 2, "zxsh: forbidden (");
    say(sh, 2, sh->why[0] ? sh->why : "a check refused it");
    say(sh, 2, "): quarantined as record ");
    say_u(sh, 2, sh->qlog.n);
    say(sh, 2, ", stopping\n");
}

/* ----------------------------------------------------------------- holds */

static void hold(zxsh_t *sh, lpx_t cond, bool is_if, uint32_t off, uint32_t end)
{
    zxsh_hold_t *h = NULL;
    for (unsigned i = 0; i < ZXSH_MAX_HELD && !h; i++)
        if (!sh->held[i].used) h = &sh->held[i];
    while (end > off && (sh->src[end - 1] == ' ' || sh->src[end - 1] == '\t' ||
                         sh->src[end - 1] == '\n' || sh->src[end - 1] == ';'))
        end--;
    if (!h || end - off >= ZXSH_HOLD_TEXT) {
        say(sh, 2, "zxsh: cannot hold (queue full or text too long); the rest is skipped\n");
        return;
    }
    h->used = true;
    h->id = ++sh->next_hold;
    h->tag = lpx_make(lpx_base(cond), lpx_pol(cond), LPX_PH_POTENTIAL);
    h->is_if = is_if;
    memcpy(h->text, sh->src + off, end - off);
    h->text[end - off] = 0;
    say(sh, 2, "zxsh: held #");
    say_u(sh, 2, h->id);
    say(sh, 2, " (");
    say_tag(sh, 2, cond);
    say(sh, 2, " -> ");
    say(sh, 2, lpx_action_name(lpx_action(cond)));
    say(sh, 2, "): ");
    say(sh, 2, h->text);
    say(sh, 2, "\n");
}

unsigned zxsh_held_count(const zxsh_t *sh)
{
    unsigned n = 0;
    for (unsigned i = 0; i < ZXSH_MAX_HELD; i++) n += sh->held[i].used;
    return n;
}

static zxsh_hold_t *hold_find(zxsh_t *sh, const char *id)
{
    uint32_t v = 0;
    if (*id == '#') id++;
    if (!*id) return NULL;
    for (; *id; id++) {
        if (*id < '0' || *id > '9' || v > 100000000u) return NULL;
        v = v * 10u + (uint32_t) (*id - '0');
    }
    for (unsigned i = 0; i < ZXSH_MAX_HELD; i++)
        if (sh->held[i].used && sh->held[i].id == v) return &sh->held[i];
    return NULL;
}

/* ------------------------------------------------------------- builtins */

static int base_word(const char *w)
{
    if (!strcmp(w, "true")) return LPX_B_TRUE;
    if (!strcmp(w, "false")) return LPX_B_FALSE;
    if (!strcmp(w, "both")) return LPX_B_BOTH;
    if (!strcmp(w, "neither")) return LPX_B_NEITHER;
    return -1;
}

static int pol_word(const char *w)
{
    if (!strcmp(w, "negentropic")) return LPX_P_NEGENTROPIC;
    if (!strcmp(w, "neutral")) return LPX_P_NEUTRAL;
    if (!strcmp(w, "entropic")) return LPX_P_ENTROPIC;
    return -1;
}

static const lpx_t T_OK = (LPX_B_TRUE << 4) | (LPX_P_NEUTRAL << 2) | LPX_PH_MANIFEST;
static const lpx_t T_NO = (LPX_B_FALSE << 4) | (LPX_P_NEUTRAL << 2) | LPX_PH_MANIFEST;

static lpx_t refuse(zxsh_t *sh, const char *a, const char *b)
{
    set_why(sh, a, b);
    say(sh, 2, "zxsh: ");
    say(sh, 2, sh->why);
    say(sh, 2, "\n");
    return T_NO;
}

static void print_held(zxsh_t *sh)
{
    for (unsigned i = 0; i < ZXSH_MAX_HELD; i++) {
        const zxsh_hold_t *h = &sh->held[i];
        if (!h->used) continue;
        say(sh, 1, "#");
        say_u(sh, 1, h->id);
        say(sh, 1, "  ");
        say_tag(sh, 1, h->tag);
        say(sh, 1, "  ");
        say(sh, 1, lpx_action_name(lpx_action(h->tag)));
        say(sh, 1, "  ");
        say(sh, 1, h->text);
        say(sh, 1, "\n");
    }
}

/* Returns true when argv[0] is a core builtin (and *v is its verdict). */
static bool builtin(zxsh_t *sh, const zxsh_stage_t *st, lpx_t *v)
{
    int argc = st->argc;
    char *const *a = st->argv;
    int b = base_word(a[0]);
    sh->why[0] = 0;
    if (!strcmp(a[0], "verdict")) { /* verdict both [polarity] */
        if (argc < 2 || (b = base_word(a[1])) < 0) {
            *v = refuse(sh, "usage: ", "verdict true|false|both|neither [polarity]");
            return true;
        }
        a++;
        argc--;
    }
    if (b >= 0) {
        int p = argc > 1 ? pol_word(a[1]) : LPX_P_NEUTRAL;
        if (p < 0 || argc > 2) {
            *v = refuse(sh, "usage: ", "true|false|both|neither [negentropic|neutral|entropic]");
            return true;
        }
        *v = zxsh_tag((uint8_t) b, (uint8_t) p);
        set_why(sh, "stated as ", a[0]);
        return true;
    }
    if (!strcmp(a[0], "exit")) {
        sh->exiting = true;
        sh->exit_code = 0;
        if (argc > 1) {
            int eb = base_word(a[1]);
            sh->exit_code = eb == LPX_B_TRUE ? 0 : eb == LPX_B_FALSE ? 1 : eb >= 0 ? 2 : 0;
            for (const char *p = a[1]; eb < 0 && *p >= '0' && *p <= '9'; p++)
                sh->exit_code = sh->exit_code * 10 + (*p - '0');
        }
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "set")) {
        for (unsigned i = 0; i < ZXSH_MAX_VARS; i++)
            if (sh->vars[i].name[0]) {
                say(sh, 1, sh->vars[i].name);
                say(sh, 1, "=");
                say(sh, 1, sh->vars[i].value);
                say(sh, 1, "\n");
            }
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "unset")) {
        for (int i = 1; i < argc; i++) var_unset(sh, a[i]);
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "export")) {
        *v = T_OK;
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(a[i], '=');
            if (eq) {
                *eq = 0;
                if (!zxsh_set(sh, a[i], eq + 1)) *v = refuse(sh, "bad variable: ", a[i]);
            }
            const char *val = zxsh_get(sh, a[i]);
            if (val && sh->ops->setenv) sh->ops->setenv(sh->u, a[i], val);
        }
        return true;
    }
    if (!strcmp(a[0], "held")) {
        print_held(sh);
        unsigned n = zxsh_held_count(sh);
        /* nothing waiting is a plain truth; something waiting is too */
        *v = T_OK;
        set_why(sh, n ? "items are held" : "nothing is held", NULL);
        return true;
    }
    if (!strcmp(a[0], "resolve") || !strcmp(a[0], "drop")) {
        bool drop = a[0][0] == 'd';
        if (!(sh->caps & ZXSH_CAP_RESOLVE)) {
            *v = refuse(sh, a[0], ": needs the resolve capability (a person at the keyboard)");
            return true;
        }
        if (argc != (drop ? 2 : 3) || (!drop && base_word(a[2]) < 0)) {
            *v = refuse(sh, "usage: ", drop ? "drop ID" : "resolve ID true|false|both|neither");
            return true;
        }
        zxsh_hold_t *h = hold_find(sh, a[1]);
        if (!h) {
            *v = refuse(sh, "no such hold: ", a[1]);
            return true;
        }
        /* S5: potential -> emerging (decided) -> manifest (applied) ->
         * dissolving (cleared), one Gray step each. */
        lpx_advance(&h->tag, LPX_PH_EMERGING);
        if (!drop) {
            size_t n = 0;
            const char *pre = h->is_if ? "if verdict " : "verdict ";
            for (; *pre; pre++) sh->pending[n++] = *pre;
            for (const char *w = a[2]; *w; w++) sh->pending[n++] = *w;
            sh->pending[n++] = h->is_if ? ';' : ' ';
            sh->pending[n++] = ' ';
            size_t m = strlen(h->text);
            memcpy(sh->pending + n, h->text, m + 1);
            sh->has_pending = true;
        }
        lpx_advance(&h->tag, LPX_PH_MANIFEST);
        lpx_advance(&h->tag, LPX_PH_DISSOLVING);
        h->used = false;
        *v = T_OK;
        set_why(sh, drop ? "dropped" : "resolved", NULL);
        return true;
    }
    if (!strcmp(a[0], "why")) {
        say_tag(sh, 1, sh->last);
        say(sh, 1, " -> ");
        say(sh, 1, lpx_action_name(lpx_action(sh->last)));
        if (sh->last_why[0]) {
            say(sh, 1, ": ");
            say(sh, 1, sh->last_why);
        }
        say(sh, 1, "\n");
        *v = sh->last; /* why does not change the verdict it explains */
        memcpy(sh->why, sh->last_why, sizeof sh->why);
        return true;
    }
    if (!strcmp(a[0], "cap")) {
        if (argc == 1) {
            for (unsigned i = 0; i < NCAPS; i++)
                if (sh->caps & CAPS[i].bit) {
                    say(sh, 1, CAPS[i].name);
                    say(sh, 1, "\n");
                }
            *v = T_OK;
            return true;
        }
        if (strcmp(a[1], "drop") || argc < 3) {
            *v = refuse(sh, "usage: ", "cap [drop NAME[,NAME]...]");
            return true;
        }
        for (int i = 2; i < argc; i++) {
            uint32_t m = zxsh_cap_parse(a[i]);
            if (!m && strcmp(a[i], "none")) {
                *v = refuse(sh, "unknown capability: ", a[i]);
                return true;
            }
            sh->caps &= ~m; /* S9: only ever narrows */
        }
        *v = T_OK;
        return true;
    }
    if (!strcmp(a[0], "help")) {
        say(sh, 1,
            "zxsh: every command returns true, false, both or neither.\n"
            "  a && b      run b only if a proceeds      a || b   only if a is refused\n"
            "  a | b       verdict of every stage         a <?> b  run both, compare\n"
            "  ! a         negate (both/neither stay)     $?       last verdict word\n"
            "  if c then A else B both C neither D fi     for x in ...; do ...; done\n"
            "  held / resolve ID VERDICT / drop ID        contradictions wait for you\n"
            "  why         explain the last verdict       cap [drop X]  capabilities\n"
            "  sandbox CAPS -- cmd   one command with fewer capabilities\n"
            "  history [verify] / qlog / zxv METHOD [JSON] / cd / export / exit\n");
        *v = T_OK;
        return true;
    }
    return false;
}

/* --------------------------------------------------------------- parser */

static lpx_t eval_list(zxsh_t *sh, bool run);

static bool is_host_builtin(const char *a)
{
    for (unsigned i = 0; HOST_BUILTINS[i]; i++)
        if (!strcmp(a, HOST_BUILTINS[i])) return true;
    return false;
}

/* Run one parsed pipeline (already expanded). */
static lpx_t dispatch(zxsh_t *sh, zxsh_stage_t *st, unsigned n, char *cap, uint32_t *caplen)
{
    lpx_t v;
    uint32_t saved_caps = sh->caps;
    bool sandboxed = false;
    sh->why[0] = 0;
    if (n == 1 && !strcmp(st[0].argv[0], "sandbox")) {
        int dd = 0;
        for (int i = 1; i < st[0].argc; i++)
            if (!strcmp(st[0].argv[i], "--")) {
                dd = i;
                break;
            }
        if (dd != 2 || dd + 1 >= st[0].argc)
            return refuse(sh, "usage: ", "sandbox CAP[,CAP] -- command [args]");
        uint32_t m = zxsh_cap_parse(st[0].argv[1]);
        if (!m && strcmp(st[0].argv[1], "none"))
            return refuse(sh, "unknown capability: ", st[0].argv[1]);
        sandboxed = true;
        sh->caps &= m;
        int k = 0;
        for (int i = dd + 1; i < st[0].argc; i++, k++) st[0].argv[k] = st[0].argv[i];
        st[0].argv[k] = NULL;
        st[0].glob >>= (dd + 1);
        st[0].argc = k;
    }
    if (n == 1 && !is_host_builtin(st[0].argv[0])) {
        if (cap) {
            *caplen = 0;
            sh->cap_buf = cap;
            sh->cap_len = caplen;
        }
        bool was = builtin(sh, &st[0], &v);
        sh->cap_buf = NULL;
        if (was) {
            if (st[0].in || st[0].out)
                v = refuse(sh, "redirects are not supported on builtin ", st[0].argv[0]);
            if (sandboxed) sh->caps = saved_caps;
            return v;
        }
    }
    for (unsigned i = 0; i < n; i++) {
        if (st[i].in && !(sh->caps & ZXSH_CAP_READ)) {
            v = refuse(sh, "denied: < needs the read capability", NULL);
            goto out;
        }
        if (st[i].out && !(sh->caps & ZXSH_CAP_WRITE)) {
            v = refuse(sh, "denied: > needs the write capability", NULL);
            goto out;
        }
        if (!is_host_builtin(st[i].argv[0]) && !(sh->caps & ZXSH_CAP_EXEC)) {
            v = refuse(sh, "denied: running a program needs the exec capability: ", st[i].argv[0]);
            goto out;
        }
        if (!strcmp(st[i].argv[0], "zxv") && !(sh->caps & ZXSH_CAP_NET)) {
            v = refuse(sh, "denied: zxv needs the net capability", NULL);
            goto out;
        }
    }
    v = sh->ops->exec(sh->u, sh, st, n, cap, ZXSH_CAPTURE, caplen, sh->why, ZXSH_WHY_MAX);
out:
    sh->caps = saved_caps;
    return v;
}

static lpx_t eval_if(zxsh_t *sh, bool run);
static lpx_t eval_if_clauses(zxsh_t *sh, bool run, lpx_t cond);
static lpx_t eval_for(zxsh_t *sh, bool run);

static void finish(zxsh_t *sh, lpx_t v, uint32_t off, uint32_t end)
{
    sh->last = v;
    memcpy(sh->last_why, sh->why, sizeof sh->why);
    if (lpx_forbidden(v)) quarantine(sh, v, off, end);
}

static lpx_t eval_pipeline(zxsh_t *sh, bool run, bool capture, char *cap, uint32_t *caplen)
{
    bool neg = false;
    lpx_t v = T_OK;
    if (tok_is(sh, sh->pos, "!")) {
        neg = true;
        sh->pos++;
    }
    if (tok_is(sh, sh->pos, "if") || tok_is(sh, sh->pos, "for")) {
        if (capture) {
            syntax(sh, "an if/for cannot be cross-checked; wrap it in a script");
            return v;
        }
        v = tok_is(sh, sh->pos, "if") ? eval_if(sh, run) : eval_for(sh, run);
        if (sh->tok[sh->pos].kind == K_PIPE) syntax(sh, "an if/for cannot be piped");
        if (neg && R(run)) sh->last = v = lpx_not(v);
        return v;
    }
    uint32_t mark = sh->arena_used, off = sh->tok[sh->pos].off, end = off;
    zxsh_stage_t st[ZXSH_MAX_STAGES];
    unsigned n = 0;
    bool overflow = false;
    for (;;) {
        if (n >= ZXSH_MAX_STAGES) {
            syntax(sh, "too many pipeline stages");
            return v;
        }
        zxsh_stage_t *s = &st[n];
        memset(s, 0, sizeof *s);
        for (;;) {
            uint8_t k = sh->tok[sh->pos].kind;
            if (k == K_WORD) {
                if (R(run)) {
                    bool g;
                    char *w = expand(sh, sh->pos, &g);
                    if (!w || s->argc >= (int) ZXSH_MAX_ARGS) {
                        overflow = true;
                        if (s->argc < (int) ZXSH_MAX_ARGS) s->argv[s->argc++] = (char *) "";
                    } else {
                        if (g) s->glob |= 1u << s->argc;
                        s->argv[s->argc++] = w;
                    }
                } else {
                    s->argc++;
                }
                end = sh->tok[sh->pos].off + sh->tok[sh->pos].len;
                sh->pos++;
            } else if (k == K_LT || k == K_GT || k == K_GTGT) {
                sh->pos++;
                if (sh->tok[sh->pos].kind != K_WORD) {
                    syntax(sh, "a redirect needs a file name");
                    return v;
                }
                if (R(run)) {
                    bool g;
                    char *w = expand(sh, sh->pos, &g);
                    if (!w) overflow = true;
                    else if (k == K_LT) s->in = w;
                    else s->out = w, s->append = (k == K_GTGT);
                }
                end = sh->tok[sh->pos].off + sh->tok[sh->pos].len;
                sh->pos++;
            } else {
                break;
            }
        }
        if (s->argc == 0) {
            syntax(sh, "expected a command");
            return v;
        }
        n++;
        if (sh->tok[sh->pos].kind != K_PIPE) break;
        sh->pos++;
        skip_nl(sh);
        if (tok_is(sh, sh->pos, "if") || tok_is(sh, sh->pos, "for") || at_terminator(sh)) {
            syntax(sh, "expected a command after |");
            return v;
        }
    }
    if (!R(run)) return v;
    if (overflow) {
        v = refuse(sh, "too many arguments or words too long", NULL);
    } else if (n == 1 && st[0].argc == 1 && !st[0].in && !st[0].out && strchr(st[0].argv[0], '=') &&
               st[0].argv[0][0] != '=' &&
               name_ok(st[0].argv[0], (size_t) (strchr(st[0].argv[0], '=') - st[0].argv[0]))) {
        char *eq = strchr(st[0].argv[0], '=');
        *eq = 0;
        sh->why[0] = 0;
        v = zxsh_set(sh, st[0].argv[0], eq + 1) ? T_OK : refuse(sh, "no room for variable ", st[0].argv[0]);
    } else {
        v = dispatch(sh, st, n, capture ? cap : NULL, caplen);
    }
    if (neg) v = lpx_not(v);
    sh->arena_used = mark;
    finish(sh, v, off, end);
    return v;
}

/* S4 */
static lpx_t eval_check(zxsh_t *sh, bool run)
{
    uint32_t save = sh->pos;
    eval_pipeline(sh, false, false, NULL, NULL);
    bool x = sh->tok[sh->pos].kind == K_XCHK;
    sh->pos = save;
    if (!x || sh->syntax || sh->need_more) return eval_pipeline(sh, run, false, NULL, NULL);
    uint32_t la = 0, lb = 0;
    lpx_t a = eval_pipeline(sh, run, true, sh->capa, &la);
    sh->pos++; /* <?> */
    skip_nl(sh);
    if (at_terminator(sh)) {
        syntax(sh, "expected a command after <?>");
        return a;
    }
    lpx_t b = eval_pipeline(sh, run, true, sh->capb, &lb);
    if (sh->tok[sh->pos].kind == K_XCHK) syntax(sh, "one <?> per check; group the rest");
    if (!R(run)) return a;
    bool same = la == lb && memcmp(sh->capa, sh->capb, la) == 0;
    emit(sh, 1, sh->capa, la);
    if (!same) {
        say(sh, 1, "--- <?> the second command said:\n");
        emit(sh, 1, sh->capb, lb);
    }
    uint8_t ja = (uint8_t) lpx_base_to_lpres(lpx_base(a)), jb = (uint8_t) lpx_base_to_lpres(lpx_base(b));
    uint8_t base = lpx_base_from_lpres((lpres_state_t) (ja | jb));
    if (lpx_base(a) == LPX_B_TRUE && lpx_base(b) == LPX_B_TRUE && !same) base = LPX_B_BOTH;
    uint8_t pol = (same && lpx_base(a) == lpx_base(b)) ? LPX_P_NEGENTROPIC : LPX_P_ENTROPIC;
    lpx_t v = lpx_make(base, pol, LPX_PH_MANIFEST);
    if (la >= ZXSH_CAPTURE || lb >= ZXSH_CAPTURE) set_why(sh, "compared the first 8192 bytes only", NULL);
    else if (!same) set_why(sh, "the two commands' outputs differ", NULL);
    else if (lpx_base(a) != lpx_base(b)) set_why(sh, "the two commands' verdicts differ", NULL);
    else set_why(sh, "two commands agree", NULL);
    sh->last = v;
    memcpy(sh->last_why, sh->why, sizeof sh->why);
    return v;
}

/* S3 */
static lpx_t eval_andor(zxsh_t *sh, bool run)
{
    lpx_t v = eval_check(sh, run);
    bool held = false;
    uint32_t hold_off = 0;
    lpx_t hold_tag = v;
    while (sh->tok[sh->pos].kind == K_AND || sh->tok[sh->pos].kind == K_OR) {
        bool is_and = sh->tok[sh->pos].kind == K_AND;
        uint32_t op_off = sh->tok[sh->pos].off;
        sh->pos++;
        skip_nl(sh);
        if (at_terminator(sh)) {
            syntax(sh, "expected a command after && or ||");
            return v;
        }
        bool go = false;
        if (R(run) && !held) {
            lpx_action_t act = lpx_action(v);
            if (act == LPX_ACT_PROCEED) go = is_and;
            else if (act == LPX_ACT_REFUSE) go = !is_and;
            else if (act == LPX_ACT_WAIT || act == LPX_ACT_ESCALATE) {
                held = true;
                hold_off = op_off;
                hold_tag = v;
            }
        }
        lpx_t r = eval_check(sh, go);
        if (go) v = r;
    }
    if (held && R(run)) hold(sh, hold_tag, false, hold_off, sh->tok[sh->pos].off);
    return v;
}

static lpx_t eval_list(zxsh_t *sh, bool run)
{
    lpx_t v = T_OK;
    for (;;) {
        while (sh->tok[sh->pos].kind == K_NL || sh->tok[sh->pos].kind == K_SEMI) sh->pos++;
        if (at_terminator(sh) || sh->syntax || sh->need_more) return v;
        uint8_t k = sh->tok[sh->pos].kind;
        if (k != K_WORD && k != K_LT && k != K_GT && k != K_GTGT) {
            syntax(sh, "expected a command");
            return v;
        }
        v = eval_andor(sh, run);
        k = sh->tok[sh->pos].kind;
        if (k != K_NL && k != K_SEMI && !at_terminator(sh)) {
            syntax(sh, "expected ; or a new line");
            return v;
        }
    }
}

/* S7 */
static lpx_t eval_if(zxsh_t *sh, bool run)
{
    sh->pos++; /* if */
    bool outer = sh->in_clause;
    sh->in_clause = false;
    lpx_t cond = eval_list(sh, run);
    sh->in_clause = outer;
    if (!tok_is(sh, sh->pos, "then")) {
        syntax(sh, "expected then");
        return cond;
    }
    sh->in_clause = true;
    lpx_t res = eval_if_clauses(sh, run, cond);
    sh->in_clause = outer;
    return res;
}

static lpx_t eval_if_clauses(zxsh_t *sh, bool run, lpx_t cond)
{
    uint32_t then_tok = sh->pos;
    /* which clauses exist? (a dry pass over the rest of the if) */
    bool has[4] = {false, false, false, false}; /* then else both neither */
    static const char *const CL[4] = {"then", "else", "both", "neither"};
    for (int pass = 0; pass < 2; pass++) {
        int choose = -1;
        bool run_any = pass == 1 && R(run);
        if (run_any) {
            lpx_action_t act = lpx_action(cond);
            if (act == LPX_ACT_PROCEED) choose = 0;
            else if (act == LPX_ACT_REFUSE) choose = 1;
            else if (act != LPX_ACT_ROLLBACK) {
                if (lpx_base(cond) == LPX_B_BOTH && has[2]) choose = 2;
                else if (lpx_base(cond) == LPX_B_NEITHER && has[3]) choose = 3;
                else choose = 4; /* hold */
            }
        }
        sh->pos = then_tok;
        lpx_t v = T_OK;
        bool seen[4] = {false, false, false, false};
        while (!sh->syntax && !sh->need_more) {
            int c = -1;
            for (int i = 0; i < 4; i++)
                if (tok_is(sh, sh->pos, CL[i])) c = i;
            if (c < 0) break;
            if (seen[c] || (c == 0) != (sh->pos == then_tok)) {
                syntax(sh, "misplaced or repeated if clause");
                return cond;
            }
            seen[c] = true;
            sh->pos++;
            lpx_t r = eval_list(sh, run_any && choose == c);
            if (run_any && choose == c) v = r;
        }
        if (!tok_is(sh, sh->pos, "fi")) {
            syntax(sh, "expected fi");
            return cond;
        }
        sh->pos++;
        if (pass == 0) {
            memcpy(has, seen, sizeof has);
            if (!R(run)) return cond;
            continue;
        }
        if (choose == 4) {
            hold(sh, cond, true, sh->tok[then_tok].off, sh->tok[sh->pos - 1].off + 2);
            return cond;
        }
        if (choose < 0) return cond;
        if (!has[choose]) v = T_OK; /* S7: nothing was asked */
        sh->last = v;
        return v;
    }
    return cond;
}

/* S8 */
static lpx_t eval_for(zxsh_t *sh, bool run)
{
    sh->pos++; /* for */
    const zxsh_tok_t *nt = &sh->tok[sh->pos];
    if (nt->kind != K_WORD || !name_ok(sh->src + nt->off, nt->len)) {
        syntax(sh, "for needs a variable name");
        return T_OK;
    }
    char name[ZXSH_NAME_MAX];
    memcpy(name, sh->src + nt->off, nt->len);
    name[nt->len] = 0;
    sh->pos++;
    if (!tok_is(sh, sh->pos, "in")) {
        syntax(sh, "expected in");
        return T_OK;
    }
    sh->pos++;
    uint32_t w0 = sh->pos;
    while (sh->tok[sh->pos].kind == K_WORD && !tok_is(sh, sh->pos, "do")) sh->pos++;
    uint32_t w1 = sh->pos;
    while (sh->tok[sh->pos].kind == K_NL || sh->tok[sh->pos].kind == K_SEMI) sh->pos++;
    if (!tok_is(sh, sh->pos, "do")) {
        syntax(sh, "expected do");
        return T_OK;
    }
    sh->pos++;
    uint32_t body = sh->pos;
    lpx_t acc = T_OK;
    bool outer = sh->in_clause;
    sh->in_clause = false;
    bool ran = false;
    if (R(run)) {
        for (uint32_t i = w0; i < w1 && R(run); i++) {
            uint32_t mark = sh->arena_used;
            bool g;
            char *w = expand(sh, i, &g);
            char val[ZXSH_VALUE_MAX];
            size_t n = w ? strlen(w) : 0;
            if (!w || n >= sizeof val) {
                sh->arena_used = mark;
                acc = lpx_and(acc, refuse(sh, "for: word too long", NULL));
                continue;
            }
            memcpy(val, w, n + 1);
            sh->arena_used = mark;
            zxsh_set(sh, name, val);
            sh->pos = body;
            acc = lpx_and(acc, eval_list(sh, true));
            ran = true;
        }
    }
    if (!ran) {
        sh->pos = body;
        eval_list(sh, false);
    } else {
        sh->pos = body;
        eval_list(sh, false); /* reposition after the body */
    }
    sh->in_clause = outer;
    if (!tok_is(sh, sh->pos, "done")) {
        syntax(sh, "expected done");
        return acc;
    }
    sh->pos++;
    if (R(run)) sh->last = acc;
    return acc;
}

/* ---------------------------------------------------------------- driver */

void zxsh_init(zxsh_t *sh, const zxsh_ops_t *ops, void *u, bool interactive)
{
    memset(sh, 0, sizeof *sh);
    sh->ops = ops;
    sh->u = u;
    sh->interactive = interactive;
    sh->caps = ZXSH_CAP_ALL & ~(interactive ? 0u : ZXSH_CAP_RESOLVE); /* S5 */
    sh->last = T_OK;
    lpx_qlog_init(&sh->qlog, sh->qrec, ZXSH_QLOG_CAP);
}

static zxsh_status_t eval_once(zxsh_t *sh, const char *src)
{
    sh->src = src;
    sh->syntax = sh->need_more = sh->aborted = sh->in_clause = false;
    sh->cap_buf = NULL;
    sh->why[0] = 0;
    lex(sh);
    if (sh->syntax) goto bad;
    if (sh->need_more) return ZXSH_NEED_MORE;
    /* pass 1: parse only, so a syntax error means nothing ran */
    sh->pos = 0;
    eval_list(sh, false);
    if (!sh->syntax && !sh->need_more && sh->tok[sh->pos].kind != K_EOF)
        syntax(sh, "unexpected keyword");
    if (sh->need_more) return ZXSH_NEED_MORE;
    if (sh->syntax) goto bad;
    /* pass 2: run */
    sh->pos = 0;
    sh->arena_used = 0;
    eval_list(sh, true);
    if (sh->exiting) return ZXSH_EXIT;
    return sh->aborted ? ZXSH_ABORTED : ZXSH_OK;
bad:
    say(sh, 2, "zxsh: ");
    say(sh, 2, sh->why);
    say(sh, 2, "\n");
    return ZXSH_SYNTAX;
}

zxsh_status_t zxsh_eval(zxsh_t *sh, const char *src)
{
    if (sh->locked) {
        say(sh, 2, "zxsh: locked (quarantine log full)\n");
        return ZXSH_ABORTED;
    }
    if (sh->depth >= ZXSH_MAX_DEPTH) return ZXSH_SYNTAX;
    sh->depth++;
    zxsh_status_t st = eval_once(sh, src);
    /* S5: a resolved hold runs after the line that resolved it */
    while (st == ZXSH_OK && sh->has_pending) {
        char buf[sizeof sh->pending];
        memcpy(buf, sh->pending, sizeof buf);
        sh->has_pending = false;
        st = eval_once(sh, buf);
        if (st == ZXSH_NEED_MORE) st = ZXSH_SYNTAX;
    }
    sh->has_pending = false;
    sh->depth--;
    return st;
}
