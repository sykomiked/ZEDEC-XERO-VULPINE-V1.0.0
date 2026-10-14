/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* burrow_model.c — Burrow, the file manager (see burrow_model.h). */
#include <string.h>

#include "burrow_model.h"

const zxm_ai_action_t BURROW_AI[BURROW_AI_COUNT] = {
    {"Describe this folder", "Companion", 300},
    {"Suggest folders to sort into", "Planner", 400},
};

#define TRASH     ".Trash"
#define TRASH_LEN (sizeof TRASH - 1)

/* ===================== names ===================== */

static bool ends_ci(const char *ext, const char *const *list)
{
    for (; *list; list++) {
        const char *a = ext, *b = *list;
        while (*a && *b && (*a | 0x20) == *b) a++, b++;
        if (!*a && !*b) return true;
    }
    return false;
}

uint8_t burrow_kind(const char *name)
{
    static const char *const IMG[] = {"png", "jpg", "jpeg", "gif",  "webp",
                                      "bmp", "svg", "avif", "heic", NULL};
    static const char *const AUD[] = {"mp3", "m4a", "wav", "ogg", "flac", "aac", "opus", NULL};
    static const char *const VID[] = {"mp4", "m4v", "mov", "webm", NULL};
    static const char *const TXT[] = {
        "txt",   "md",  "markdown", "json", "csv", "tsv", "log",   "yaml", "yml", "toml",
        "ini",   "xml", "html",     "css",  "js",  "ts",  "c",     "h",    "py",  "sh",
        "swift", "rs",  "go",       "java", "cob", "cbl", "sutra", "zxt",  "f90", "ada",
        "adb",   "ads", "apl",      "k",    "m",   "sql", NULL};
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return BURROW_K_TEXT; /* no extension: try it as text */
    const char *ext = dot + 1;
    if (ends_ci(ext, IMG)) return BURROW_K_IMAGE;
    if (ends_ci(ext, AUD)) return BURROW_K_AUDIO;
    if (ends_ci(ext, VID)) return BURROW_K_VIDEO;
    if (ends_ci(ext, TXT)) return BURROW_K_TEXT;
    return BURROW_K_OTHER;
}

static int join(const char *dir, const char *name, char *out, uint32_t cap)
{
    size_t dl = strlen(dir), nl = strlen(name);
    if (!nl || dl + 1 + nl + 1 > cap) return ZXM_E_ARG;
    if (dl) {
        memcpy(out, dir, dl);
        out[dl++] = '/';
    }
    memcpy(out + dl, name, nl + 1);
    return ZXM_OK;
}

bool burrow_in_trash(const burrow_t *b)
{
    return !strncmp(b->dir, TRASH, TRASH_LEN) &&
           (b->dir[TRASH_LEN] == 0 || b->dir[TRASH_LEN] == '/');
}

int burrow_path(const burrow_t *b, uint32_t i, char *out, uint32_t cap)
{
    if (i >= b->n) return ZXM_E_ARG;
    return join(b->dir, b->e[i].name, out, cap);
}

/* ===================== reading a folder ===================== */

static int load(burrow_t *b, const char *dir)
{
    zxm_link_t *l = b->link;
    zxm_begin(l, "files.list");
    zxm_p_space(l, b->space);
    zxm_p_str(l, "path", dir);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    uint32_t total = zxm_len(l, arr);
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    size_t dl = strlen(dir);
    if (dl > BURROW_PATH_MAX) return ZXM_E_ARG;
    memcpy(b->dir, dir, dl + 1);
    b->n = 0;
    b->sel = -1;
    b->truncated = total > BURROW_MAX;
    for (uint32_t i = 0; i < total && b->n < BURROW_MAX; i++) {
        int32_t o = zxm_at(l, arr, i);
        burrow_entry_t *e = &b->e[b->n];
        if (zxm_get_str(l, o, "name", e->name, sizeof e->name) != ZXM_OK ||
            zxm_get_bool(l, o, "dir", &e->dir) != ZXM_OK ||
            zxm_get_u64(l, o, "size", &e->size) != ZXM_OK ||
            zxm_get_i64(l, o, "mtime", &e->mtime) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        e->kind = e->dir ? BURROW_K_DIR : burrow_kind(e->name);
        b->n++;
    }
    return ZXM_OK;
}

int burrow_init(burrow_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
    b->sel = -1;
    zxm_begin(link, "files.root");
    int rc = zxm_call(link);
    if (rc) return rc;
    if (zxm_get_str(link, link->result, "root", b->root, sizeof b->root) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    return load(b, "");
}

int burrow_set_space(burrow_t *b, const char *space)
{
    char was[sizeof b->space];
    size_t n = strlen(space);
    if (n > ZXM_SPACE_MAX) return ZXM_E_ARG;
    memcpy(was, b->space, sizeof was);
    memcpy(b->space, space, n + 1);
    if (!strcmp(b->space, "home")) b->space[0] = 0;
    int rc = load(b, "");
    if (rc) memcpy(b->space, was, sizeof was);
    return rc;
}

int burrow_open(burrow_t *b, const char *dir)
{
    return load(b, dir);
}

int burrow_refresh(burrow_t *b)
{
    char d[BURROW_PATH_MAX + 1];
    memcpy(d, b->dir, sizeof d);
    char keep[BURROW_NAME_MAX + 1] = "";
    if (b->sel >= 0) memcpy(keep, b->e[b->sel].name, sizeof keep);
    int rc = load(b, d);
    for (uint32_t i = 0; !rc && keep[0] && i < b->n; i++)
        if (!strcmp(b->e[i].name, keep)) b->sel = (int32_t) i;
    return rc;
}

int burrow_up(burrow_t *b)
{
    if (!b->dir[0]) return ZXM_E_STATE;
    char d[BURROW_PATH_MAX + 1];
    memcpy(d, b->dir, sizeof d);
    char *sl = strrchr(d, '/');
    if (sl)
        *sl = 0;
    else
        d[0] = 0;
    return load(b, d);
}

int burrow_enter(burrow_t *b, uint32_t i)
{
    char p[BURROW_PATH_MAX + 1];
    if (i >= b->n || !b->e[i].dir) return ZXM_E_STATE;
    int rc = burrow_path(b, i, p, sizeof p);
    return rc ? rc : load(b, p);
}

int burrow_select(burrow_t *b, int32_t i)
{
    if (i < -1 || i >= (int32_t) b->n) return ZXM_E_ARG;
    b->sel = i;
    return ZXM_OK;
}

/* ===================== changes ===================== */

static int one_path(burrow_t *b, const char *method, const char *path)
{
    zxm_begin(b->link, method);
    zxm_p_space(b->link, b->space);
    zxm_p_str(b->link, "path", path);
    int rc = zxm_call(b->link);
    return rc ? rc : burrow_refresh(b);
}

int burrow_mkdir(burrow_t *b, const char *name)
{
    char p[BURROW_PATH_MAX + 1];
    if (burrow_in_trash(b)) return ZXM_E_STATE;
    int rc = join(b->dir, name, p, sizeof p);
    return rc ? rc : one_path(b, "files.mkdir", p);
}

int burrow_new_text(burrow_t *b, const char *name)
{
    char p[BURROW_PATH_MAX + 1];
    if (burrow_in_trash(b)) return ZXM_E_STATE;
    int rc = join(b->dir, name, p, sizeof p);
    if (rc) return rc;
    zxm_begin(b->link, "files.write");
    zxm_p_space(b->link, b->space);
    zxm_p_str(b->link, "path", p);
    zxm_p_str(b->link, "text", "");
    zxm_p_bool(b->link, "exclusive", true);
    rc = zxm_call(b->link);
    return rc ? rc : burrow_refresh(b);
}

static int move(burrow_t *b, uint32_t i, const char *to)
{
    char from[BURROW_PATH_MAX + 1];
    int rc = burrow_path(b, i, from, sizeof from);
    if (rc) return rc;
    zxm_begin(b->link, "files.rename");
    zxm_p_space(b->link, b->space);
    zxm_p_str(b->link, "from", from);
    zxm_p_str(b->link, "to", to);
    rc = zxm_call(b->link);
    return rc ? rc : burrow_refresh(b);
}

int burrow_rename(burrow_t *b, uint32_t i, const char *name)
{
    char to[BURROW_PATH_MAX + 1];
    if (i >= b->n) return ZXM_E_ARG;
    if (burrow_in_trash(b)) return ZXM_E_STATE;
    int rc = join(b->dir, name, to, sizeof to);
    return rc ? rc : move(b, i, to);
}

int burrow_put_back(burrow_t *b, uint32_t i, const char *to)
{
    if (i >= b->n) return ZXM_E_ARG;
    if (!burrow_in_trash(b)) return ZXM_E_STATE;
    return move(b, i, to);
}

int burrow_trash(burrow_t *b, uint32_t i)
{
    char p[BURROW_PATH_MAX + 1];
    int rc = burrow_path(b, i, p, sizeof p);
    return rc ? rc : one_path(b, "files.trash", p);
}

int burrow_empty_trash(burrow_t *b, uint32_t *removed)
{
    uint64_t n = 0;
    *removed = 0;
    zxm_begin(b->link, "files.empty_trash");
    zxm_p_space(b->link, b->space);
    int rc = zxm_call(b->link);
    if (rc) return rc;
    if (zxm_get_u64(b->link, b->link->result, "removed", &n) != ZXM_OK) return ZXM_E_PROTOCOL;
    *removed = (uint32_t) n;
    return burrow_in_trash(b) ? burrow_open(b, TRASH) : ZXM_OK;
}

int burrow_cross(burrow_t *b, uint32_t i, const char *to_space, const char *to, uint32_t *ticket)
{
    char from[BURROW_PATH_MAX + 1];
    uint64_t t = 0;
    bool held = false;
    *ticket = 0;
    if (i >= b->n || b->e[i].dir) return ZXM_E_ARG;
    int rc = burrow_path(b, i, from, sizeof from);
    if (rc) return rc;
    zxm_link_t *l = b->link;
    zxm_begin(l, "files.cross");
    zxm_p_str(l, "from_space", b->space[0] ? b->space : "home");
    zxm_p_str(l, "path", from);
    zxm_p_str(l, "to_space", to_space[0] ? to_space : "home");
    zxm_p_str(l, "to", to);
    if ((rc = zxm_call(l)) != 0) return rc;
    if (zxm_get_bool(l, l->result, "held", &held)) return ZXM_E_PROTOCOL;
    if (held && (zxm_get_u64(l, l->result, "ticket", &t) || !t || t > UINT32_MAX))
        return ZXM_E_PROTOCOL;
    *ticket = (uint32_t) t;
    return ZXM_OK;
}

int burrow_cross_confirm(burrow_t *b, uint32_t ticket)
{
    if (!ticket) return ZXM_E_ARG;
    zxm_begin(b->link, "files.cross");
    zxm_p_u64(b->link, "confirm", ticket);
    return zxm_call(b->link);
}

/* 9000 bytes = 12000 base64 characters per request, well inside a line */
#define CHUNK 9000u

int burrow_import(burrow_t *b, const char *name, const uint8_t *data, uint32_t len)
{
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static char enc[(CHUNK / 3u) * 4u + 4u];
    char p[BURROW_PATH_MAX + 1];
    if (burrow_in_trash(b)) return ZXM_E_STATE;
    int rc = join(b->dir, name, p, sizeof p);
    if (rc) return rc;
    uint32_t parts = len ? (len + CHUNK - 1u) / CHUNK : 1u;
    for (uint32_t k = 0; k < parts; k++) {
        uint32_t off = k * CHUNK, n = len - off < CHUNK ? len - off : CHUNK, o = 0;
        for (uint32_t i = 0; i < n; i += 3) {
            const uint8_t *d = data + off + i;
            uint32_t v = (uint32_t) d[0] << 16 | (i + 1 < n ? (uint32_t) d[1] << 8 : 0) |
                         (i + 2 < n ? d[2] : 0);
            enc[o++] = B64[v >> 18 & 63];
            enc[o++] = B64[v >> 12 & 63];
            enc[o++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
            enc[o++] = i + 2 < n ? B64[v & 63] : '=';
        }
        zxm_begin(b->link, "files.write");
        zxm_p_space(b->link, b->space);
        zxm_p_str(b->link, "path", p);
        zxm_p_strn(b->link, "base64", enc, o);
        zxm_p_bool(b->link, "exclusive", true);
        if (parts > 1)
            zxm_p_str(b->link, "mode", k == 0 ? "begin" : k == parts - 1 ? "end" : "add");
        if ((rc = zxm_call(b->link)) != 0) return rc;
        zxm_progress(b->link, off + n, len);
    }
    return burrow_refresh(b);
}

/* ===================== AI ===================== */

int burrow_ai(burrow_t *b, int32_t action)
{
    static const char *const ASK[BURROW_AI_COUNT] = {
        "In two or three sentences, say what this folder seems to hold, from its file names.",
        "Suggest up to five folder names that these files could be sorted into, one per line, "
        "each followed by the files that belong in it.",
    };
    static char names[ZXM_ASK_MAX];
    if (action < 0 || action >= BURROW_AI_COUNT) return ZXM_E_ARG;
    if (!b->n) return ZXM_E_STATE;
    uint32_t o = 0;
    for (uint32_t i = 0; i < b->n; i++) {
        size_t nl = strlen(b->e[i].name);
        if (o + nl + 2 > sizeof names) break;
        memcpy(names + o, b->e[i].name, nl);
        o += (uint32_t) nl;
        if (b->e[i].dir) names[o++] = '/';
        names[o++] = '\n';
    }
    return zxm_ai_ask(b->link, &BURROW_AI[action], action, ASK[action], names, o, &b->held);
}

int burrow_ai_accept(burrow_t *b)
{
    if (b->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    b->held.state = ZXM_HELD_ACCEPTED;
    return ZXM_OK;
}

int burrow_ai_reject(burrow_t *b)
{
    if (b->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    b->held.state = ZXM_HELD_REJECTED;
    b->held.len = 0;
    b->held.text[0] = 0;
    return ZXM_OK;
}
