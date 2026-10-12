/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* glint_model.c — Glint, photos and media (see glint_model.h). */
#include <string.h>

#include "glint_model.h"

const zxm_ai_action_t GLINT_AI[GLINT_AI_COUNT] = {
    {"Name this album", "Writer", 250},
};

const char *glint_mime(const char *name)
{
    static const char *const MAP[][2] = {
        {"png", "image/png"},       {"jpg", "image/jpeg"},  {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},       {"webp", "image/webp"}, {"bmp", "image/bmp"},
        {"svg", "image/svg+xml"},   {"avif", "image/avif"}, {"heic", "image/heic"},
        {"mp3", "audio/mpeg"},      {"m4a", "audio/mp4"},   {"wav", "audio/wav"},
        {"ogg", "audio/ogg"},       {"flac", "audio/flac"}, {"aac", "audio/aac"},
        {"opus", "audio/ogg"},      {"mp4", "video/mp4"},   {"m4v", "video/mp4"},
        {"mov", "video/quicktime"}, {"webm", "video/webm"},
    };
    const char *dot = strrchr(name, '.');
    if (dot && dot != name)
        for (size_t i = 0; i < sizeof MAP / sizeof *MAP; i++) {
            const char *a = dot + 1, *b = MAP[i][0];
            while (*a && *b && (*a | 0x20) == *b) a++, b++;
            if (!*a && !*b) return MAP[i][1];
        }
    return "application/octet-stream";
}

void glint_scan(glint_t *g, burrow_t *folder)
{
    g->folder = folder;
    g->n = 0;
    g->cur = -1;
    for (uint32_t i = 0; i < folder->n && g->n < GLINT_MAX; i++) {
        const burrow_entry_t *e = &folder->e[i];
        if (e->kind != BURROW_K_IMAGE && e->kind != BURROW_K_AUDIO && e->kind != BURROW_K_VIDEO)
            continue;
        glint_item_t *it = &g->item[g->n++];
        memcpy(it->name, e->name, sizeof it->name);
        it->kind = e->kind;
        it->size = e->size;
        it->mtime = e->mtime;
    }
}

int glint_view(glint_t *g, int32_t i)
{
    if (i < -1 || i >= (int32_t) g->n) return ZXM_E_ARG;
    g->cur = i;
    return ZXM_OK;
}

int glint_next(glint_t *g)
{
    if (g->cur < 0 || g->cur + 1 >= (int32_t) g->n) return ZXM_E_STATE;
    g->cur++;
    return ZXM_OK;
}

int glint_prev(glint_t *g)
{
    if (g->cur <= 0) return ZXM_E_STATE;
    g->cur--;
    return ZXM_OK;
}

/* strict RFC 4648, as zxvd writes it */
static int64_t unb64(const char *s, uint32_t n, uint8_t *out, uint64_t cap)
{
    uint64_t o = 0;
    if (n % 4u) return -1;
    for (uint32_t i = 0; i < n; i += 4) {
        uint32_t v = 0, pad = 0;
        for (uint32_t j = 0; j < 4; j++) {
            char c = s[i + j];
            uint32_t d;
            if (c >= 'A' && c <= 'Z')
                d = (uint32_t) (c - 'A');
            else if (c >= 'a' && c <= 'z')
                d = (uint32_t) (c - 'a') + 26u;
            else if (c >= '0' && c <= '9')
                d = (uint32_t) (c - '0') + 52u;
            else if (c == '+')
                d = 62;
            else if (c == '/')
                d = 63;
            else if (c == '=' && i + 4 == n && j >= 2)
                d = 0, pad++;
            else
                return -1;
            if (pad && c != '=') return -1;
            v = v << 6 | d;
        }
        if (o + 3u - pad > cap) return -1;
        out[o++] = (uint8_t) (v >> 16);
        if (pad < 2) out[o++] = (uint8_t) (v >> 8);
        if (pad < 1) out[o++] = (uint8_t) v;
    }
    return (int64_t) o;
}

int glint_load(glint_t *g, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    char path[BURROW_PATH_MAX + 1];
    *len = 0;
    if (g->cur < 0 || !g->folder) return ZXM_E_STATE;
    const glint_item_t *it = &g->item[g->cur];
    if (it->size > cap) return ZXM_E_TOOBIG;
    size_t dl = strlen(g->folder->dir), nl = strlen(it->name);
    if (dl + 1 + nl + 1 > sizeof path) return ZXM_E_ARG;
    if (dl) {
        memcpy(path, g->folder->dir, dl);
        path[dl++] = '/';
    }
    memcpy(path + dl, it->name, nl + 1);
    zxm_link_t *l = g->folder->link;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        zxm_p_str(l, "encoding", "base64");
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t next, size;
        bool eof;
        int32_t t = zxm_get(l, l->result, "base64");
        if (t < 0 || l->tok[t].type != W4_J_STR || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_u64(l, l->result, "size", &size) || zxm_get_bool(l, l->result, "eof", &eof))
            return ZXM_E_PROTOCOL;
        if (size > cap) return ZXM_E_TOOBIG; /* it grew since the listing */
        int64_t n =
            unb64(l->resp + l->tok[t].start, l->tok[t].end - l->tok[t].start, buf + off, cap - off);
        if (n < 0 || off + (uint64_t) n != next) return ZXM_E_PROTOCOL;
        off = next;
        if (eof) break;
    }
    *len = (uint32_t) off;
    return ZXM_OK;
}

int glint_ai(glint_t *g, int32_t action)
{
    static char names[ZXM_ASK_MAX];
    if (action < 0 || action >= GLINT_AI_COUNT) return ZXM_E_ARG;
    if (!g->n || !g->folder) return ZXM_E_STATE;
    uint32_t o = 0;
    for (uint32_t i = 0; i < g->n; i++) {
        size_t nl = strlen(g->item[i].name);
        if (o + nl + 1 > sizeof names) break;
        memcpy(names + o, g->item[i].name, nl);
        o += (uint32_t) nl;
        names[o++] = '\n';
    }
    return zxm_ai_ask(g->folder->link, &GLINT_AI[action], action,
                      "Suggest a short album title for photos and media with these file names. "
                      "Answer with the title only.",
                      names, o, &g->held);
}

int glint_ai_accept(glint_t *g)
{
    if (g->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    g->held.state = ZXM_HELD_ACCEPTED;
    return ZXM_OK;
}

int glint_ai_reject(glint_t *g)
{
    if (g->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    g->held.state = ZXM_HELD_REJECTED;
    g->held.len = 0;
    g->held.text[0] = 0;
    return ZXM_OK;
}
