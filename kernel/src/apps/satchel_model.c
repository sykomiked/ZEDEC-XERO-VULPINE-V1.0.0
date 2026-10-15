/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* satchel_model.c — Satchel, the packager (satchel_model.h SM1-SM4). */
#include "satchel_model.h"

#include <stdio.h>
#include <string.h>

#define SATCHEL_E_FILES (-32014) /* zxvd's refusal by the Hoard (ZXVD_E_FILES) */

void satchel_init(satchel_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
}

/* SM1: 1..SATCHEL_NAME_MAX bytes of A-Z a-z 0-9 . _ -, not starting with '.' */
static bool name_ok(const char *n, size_t len)
{
    if (len < 1u || len > SATCHEL_NAME_MAX || n[0] == '.') return false;
    for (size_t i = 0; i < len; i++) {
        char c = n[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

/* a path of the Hoard Satchel keeps: 1..SATCHEL_PATH_MAX bytes ending in ext */
static bool path_ok(const char *p, const char *ext)
{
    size_t n = p ? strlen(p) : 0u;
    return n >= 1u && n <= SATCHEL_PATH_MAX && satchel_ends(p, n, ext);
}

/* rel joined to dir ("" the top) into out; false when it does not fit */
static bool join(char *out, size_t cap, const char *dir, const char *rel)
{
    int k = dir[0] ? snprintf(out, cap, "%s/%s", dir, rel) : snprintf(out, cap, "%s", rel);
    return k > 0 && (size_t) k < cap;
}

/* files.root, kept once asked */
static int ask_root(satchel_t *s)
{
    zxm_link_t *l = s->link;
    if (s->root[0]) return ZXM_OK;
    zxm_begin(l, "files.root");
    int rc = zxm_call(l);
    if (rc) return rc;
    rc = zxm_get_str(l, l->result, "root", s->root, sizeof s->root);
    if (rc || s->root[0] != '/') {
        s->root[0] = 0;
        return rc ? rc : ZXM_E_PROTOCOL;
    }
    return ZXM_OK;
}

/* the absolute path of a path of the Hoard */
static bool absolute(const satchel_t *s, char *out, size_t cap, const char *rel)
{
    return join(out, cap, s->root, rel);
}

/* SR2-SR4 over a package whose names and sizes are in; false outside SR1 */
static bool settle(satchel_pkg_t *p)
{
    static int64_t size[SATCHEL_FILES_MAX], kind[SATCHEL_FILES_MAX];
    p->largest = 0;
    for (uint32_t i = 0; i < p->nfiles; i++) {
        size[i] = p->file[i].size;
        if (size[i] > p->largest) p->largest = size[i];
        p->file[i].kind = kind[i] = satchel_kind_of(p->file[i].name);
    }
    p->total = satchel_total(size, p->nfiles);
    if (p->total < 0 || !satchel_package_ok((int64_t) p->nfiles, p->total, p->largest))
        return false;
    for (uint32_t i = 0; i < p->nfiles; i++)
        p->file[i].share = satchel_share_x100(p->file[i].size, p->total);
    for (int64_t k = 0; k < SATCHEL_KINDS; k++)
        p->count[k] = witness_kind_count(kind, p->nfiles, k);
    return true;
}

int satchel_open(satchel_t *s, const char *folder, const char *name)
{
    static satchel_pkg_t next;
    char fname[SATCHEL_NAME_MAX + 2];
    zxm_link_t *l = s->link;
    size_t fl = folder ? strlen(folder) : 0u, nl = name ? strlen(name) : 0u;
    if (!folder || fl > SATCHEL_PATH_MAX - SATCHEL_NAME_MAX - 1u || !name_ok(name, nl))
        return ZXM_E_ARG;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", folder);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    memset(&next, 0, sizeof next);
    memcpy(next.name, name, nl + 1u);
    int32_t fault = SATCHEL_FINE;
    uint32_t total = zxm_len(l, arr);
    for (uint32_t i = 0; i < total && fault == SATCHEL_FINE; i++) {
        int32_t e = zxm_at(l, arr, i);
        bool dir = false;
        uint64_t size = 0;
        if (zxm_get_bool(l, e, "dir", &dir)) return ZXM_E_PROTOCOL;
        if (dir) continue;
        rc = zxm_get_str(l, e, "name", fname, sizeof fname);
        if (rc == ZXM_E_TOOBIG) {
            fault = SATCHEL_F_NAME;
            break;
        }
        if (rc || zxm_get_u64(l, e, "size", &size)) return ZXM_E_PROTOCOL;
        size_t n = strlen(fname);
        if (satchel_ends(fname, n, ".zxpkg") || satchel_ends(fname, n, ".fab")) continue;
        if (!name_ok(fname, n))
            fault = SATCHEL_F_NAME;
        else if (size == 0u)
            fault = SATCHEL_F_EMPTY;
        else if (next.nfiles >= SATCHEL_FILES_MAX || size > (uint64_t) SATCHEL_FILE_MAX)
            fault = SATCHEL_F_LIMITS;
        else {
            satchel_file_t *f = &next.file[next.nfiles++];
            memcpy(f->name, fname, n + 1u);
            f->size = (int64_t) size;
        }
    }
    if (fault == SATCHEL_FINE && !settle(&next)) fault = SATCHEL_F_LIMITS;
    if (fault == SATCHEL_FINE) {
        int32_t h = satchel_index(&next, s->head, sizeof s->head);
        if (h < 0) fault = SATCHEL_F_LIMITS;
        next.head_len = h;
    }
    s->fault = fault;
    if (fault != SATCHEL_FINE) return ZXM_E_ARG;
    s->pkg = next;
    memcpy(s->folder, folder, fl + 1u);
    s->open = true;
    s->built = s->is_signed = false;
    s->bundle[0] = s->recipe[0] = s->cid[0] = 0;
    s->bundle_len = s->ops = s->recipe_len = 0;
    return ZXM_OK;
}

int32_t satchel_index(const satchel_pkg_t *p, char *out, uint32_t cap)
{
    uint32_t o = SATCHEL_FIRST;
    int k;
    if (!p || !out || cap <= SATCHEL_FIRST || cap > 99999999u || p->nfiles > SATCHEL_FILES_MAX)
        return -1;
    k = snprintf(out + o, cap - o, "name %s\nfiles %u\nbytes %lld\n", p->name, p->nfiles,
                 (long long) p->total);
    if (k < 0 || (uint32_t) k >= cap - o) return -1;
    o += (uint32_t) k;
    for (uint32_t i = 0; i < p->nfiles; i++) {
        k = snprintf(out + o, cap - o, "file %lld %lld %s\n", (long long) p->file[i].size,
                     (long long) p->file[i].kind, p->file[i].name);
        if (k < 0 || (uint32_t) k >= cap - o) return -1;
        o += (uint32_t) k;
    }
    k = snprintf(out + o, cap - o, "end\n");
    if (k < 0 || (uint32_t) k >= cap - o) return -1;
    o += (uint32_t) k;
    char first[SATCHEL_FIRST + 1];
    snprintf(first, sizeof first, "zxpkg 1 %08u\n", o);
    memcpy(out, first, SATCHEL_FIRST);
    return (int32_t) o;
}

/* files.stat: a file's size, or -1 when it is not there (ZXM_OK either
 * way); another error as it came */
static int stat_size(zxm_link_t *l, const char *path, int64_t *size)
{
    uint64_t v = 0;
    *size = -1;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == SATCHEL_E_FILES && strstr(l->err, "no such file"))
        return ZXM_OK;
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "size", &v) || v > (uint64_t) INT64_MAX) return ZXM_E_PROTOCOL;
    *size = (int64_t) v;
    return ZXM_OK;
}

/* SM2: copy one file into the bundle being written, in parts */
static int copy_file(satchel_t *s, const char *src, int64_t size, const char *out, bool last,
                     int64_t *done, int64_t all)
{
    zxm_link_t *l = s->link;
    uint64_t off = 0;
    while ((int64_t) off < size) {
        uint64_t got_size = 0, next = 0;
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", src);
        zxm_p_u64(l, "offset", off);
        zxm_p_u64(l, "max", SATCHEL_CHUNK);
        zxm_p_str(l, "encoding", "base64");
        int rc = zxm_call(l);
        if (rc) return rc;
        if (zxm_get_u64(l, l->result, "size", &got_size) ||
            zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_str(l, l->result, "base64", s->b64, sizeof s->b64))
            return ZXM_E_PROTOCOL;
        if ((int64_t) got_size != size) return ZXM_E_STATE; /* changed since it was opened */
        if (next <= off || next > got_size || next - off > SATCHEL_CHUNK) return ZXM_E_PROTOCOL;
        bool end = last && next == got_size;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", out);
        zxm_p_str(l, "mode", end ? "end" : "add");
        zxm_p_str(l, "base64", s->b64);
        if (end) zxm_p_bool(l, "exclusive", true);
        if ((rc = zxm_call(l)) != ZXM_OK) return rc;
        *done += (int64_t) (next - off);
        zxm_progress(l, (uint64_t) *done, (uint64_t) all);
        off = next;
    }
    return ZXM_OK;
}

int satchel_build(satchel_t *s, const char *out_path)
{
    static char cid[SATCHEL_FILES_MAX][SATCHEL_CID_MAX + 1];
    char src[SATCHEL_PATH_MAX + 1], abs[SATCHEL_ABS_MAX + 1];
    zxm_link_t *l = s->link;
    if (!s->open) return ZXM_E_STATE;
    if (!path_ok(out_path, ".zxpkg")) return ZXM_E_ARG;
    for (uint32_t i = 0; i < s->pkg.nfiles; i++)
        if (!join(src, sizeof src, s->folder, s->pkg.file[i].name)) return ZXM_E_ARG;
    int rc = ask_root(s);
    if (rc) return rc;
    for (uint32_t i = 0; i < s->pkg.nfiles; i++) {
        join(src, sizeof src, s->folder, s->pkg.file[i].name);
        if (!absolute(s, abs, sizeof abs, src)) return ZXM_E_ARG;
    }
    int32_t h = satchel_index(&s->pkg, s->head, sizeof s->head);
    if (h < 0) return ZXM_E_STATE;
    int64_t all = h + s->pkg.total, done = h, have = 0;
    /* never over a file that is there */
    if ((rc = stat_size(l, out_path, &have)) != ZXM_OK) return rc;
    if (have >= 0) return ZXM_E_STATE;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", out_path);
    zxm_p_str(l, "mode", "begin");
    zxm_p_str(l, "text", s->head);
    zxm_p_bool(l, "exclusive", true);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    zxm_progress(l, (uint64_t) done, (uint64_t) all);
    for (uint32_t i = 0; i < s->pkg.nfiles; i++) {
        join(src, sizeof src, s->folder, s->pkg.file[i].name);
        rc = copy_file(s, src, s->pkg.file[i].size, out_path, i + 1u == s->pkg.nfiles, &done, all);
        if (rc) return rc;
    }
    if ((rc = stat_size(l, out_path, &have)) != ZXM_OK) return rc;
    if (have != all) return ZXM_E_PROTOCOL;
    /* one block per file */
    for (uint32_t i = 0; i < s->pkg.nfiles; i++) {
        uint64_t len = 0;
        join(src, sizeof src, s->folder, s->pkg.file[i].name);
        absolute(s, abs, sizeof abs, src);
        zxm_begin(l, "fab.put");
        zxm_p_str(l, "path", abs);
        if ((rc = zxm_call(l)) != ZXM_OK) return rc;
        if (zxm_get_str(l, l->result, "cid", cid[i], sizeof cid[i]) ||
            zxm_get_u64(l, l->result, "len", &len) || len != (uint64_t) s->pkg.file[i].size)
            return ZXM_E_PROTOCOL;
        for (const char *c = cid[i]; *c; c++)
            if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')))
                return ZXM_E_PROTOCOL;
        if (!cid[i][0]) return ZXM_E_PROTOCOL;
    }
    for (uint32_t i = 0; i < s->pkg.nfiles; i++) memcpy(s->pkg.file[i].cid, cid[i], sizeof cid[i]);
    s->pkg.head_len = h;
    memcpy(s->bundle, out_path, strlen(out_path) + 1u);
    s->bundle_len = all;
    s->built = true;
    s->is_signed = false;
    s->recipe[0] = s->cid[0] = 0;
    return ZXM_OK;
}

int satchel_sign(satchel_t *s, const char *recipe_path, int64_t now_ms)
{
    static char blocks[SATCHEL_FILES_MAX * (SATCHEL_CID_MAX + 12u) + 16u];
    char target[SATCHEL_ABS_MAX + 1], out[SATCHEL_ABS_MAX + 1], cid[SATCHEL_CID_MAX + 1], mode[16];
    zxm_link_t *l = s->link;
    if (!s->built) return ZXM_E_STATE;
    if (!path_ok(recipe_path, ".fab") ||
        !satchel_record_fits(s->seq + 1, (int64_t) s->pkg.nfiles, s->pkg.total, 1, now_ms) ||
        now_ms < 0)
        return ZXM_E_ARG;
    if (!absolute(s, target, sizeof target, s->bundle) ||
        !absolute(s, out, sizeof out, recipe_path))
        return ZXM_E_ARG;
    uint32_t o = 0;
    int k = snprintf(blocks, sizeof blocks, "{\"blocks\":[");
    o = (uint32_t) k;
    for (uint32_t i = 0; i < s->pkg.nfiles; i++) {
        k = snprintf(blocks + o, sizeof blocks - o, "%s{\"cid\":\"%s\"}", i ? "," : "",
                     s->pkg.file[i].cid);
        if (k < 0 || (size_t) k >= sizeof blocks - o) return ZXM_E_TOOBIG;
        o += (uint32_t) k;
    }
    k = snprintf(blocks + o, sizeof blocks - o, "]}");
    if (k < 0 || (size_t) k >= sizeof blocks - o) return ZXM_E_TOOBIG;
    o += (uint32_t) k;
    zxm_begin(l, "fab.send");
    zxm_p_raw(l, blocks, o);
    zxm_p_str(l, "target", target);
    zxm_p_str(l, "out", out);
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t len = 0, ops = 0, rlen = 0;
    if (zxm_get_str(l, l->result, "cid", cid, sizeof cid) ||
        zxm_get_str(l, l->result, "mode", mode, sizeof mode) ||
        zxm_get_u64(l, l->result, "len", &len) || zxm_get_u64(l, l->result, "ops", &ops) ||
        zxm_get_u64(l, l->result, "recipe", &rlen) || !cid[0] || strcmp(mode, "public") ||
        len != (uint64_t) s->bundle_len || ops > (uint64_t) INT64_MAX ||
        rlen > (uint64_t) INT64_MAX)
        return ZXM_E_PROTOCOL;
    memcpy(s->cid, cid, sizeof cid);
    memcpy(s->recipe, recipe_path, strlen(recipe_path) + 1u);
    s->ops = (int64_t) ops;
    s->recipe_len = (int64_t) rlen;
    s->is_signed = true;
    s->seq++;
    s->stamp = now_ms;
    return ZXM_OK;
}

void satchel_confirm(satchel_t *s, bool yes)
{
    s->confirmed = yes;
}

static bool hex_id(const char *id)
{
    size_t n = id ? strlen(id) : 0u;
    if (n != SATCHEL_ID_HEX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

/* a decimal of 1..18 digits at *p, up to the byte stop */
static bool read_num(const char **p, char stop, int64_t *v)
{
    int64_t x = 0;
    uint32_t d = 0;
    while (**p >= '0' && **p <= '9' && d < 18u) {
        x = x * 10 + (**p - '0');
        (*p)++, d++;
    }
    if (!d || **p != stop) return false;
    (*p)++;
    *v = x;
    return true;
}

/* a line "<word> " at *p */
static bool word(const char **p, const char *w)
{
    size_t n = strlen(w);
    if (strncmp(*p, w, n)) return false;
    *p += n;
    return true;
}

/* SM2: the index text (head_len bytes) into a package; false when it is not SM2's */
static bool parse_index(const char *t, int64_t head_len, satchel_pkg_t *p)
{
    const char *q = t + SATCHEL_FIRST;
    int64_t files = 0, bytes = 0;
    memset(p, 0, sizeof *p);
    if ((int64_t) strlen(t) != head_len || !word(&q, "name ")) return false;
    const char *e = strchr(q, '\n');
    if (!e || !name_ok(q, (size_t) (e - q))) return false;
    memcpy(p->name, q, (size_t) (e - q));
    q = e + 1;
    if (!word(&q, "files ") || !read_num(&q, '\n', &files) || files < 1 ||
        files > SATCHEL_FILES_MAX || !word(&q, "bytes ") || !read_num(&q, '\n', &bytes))
        return false;
    for (int64_t i = 0; i < files; i++) {
        satchel_file_t *f = &p->file[i];
        int64_t kind = 0;
        if (!word(&q, "file ") || !read_num(&q, ' ', &f->size) || !read_num(&q, ' ', &kind))
            return false;
        e = strchr(q, '\n');
        if (!e || !name_ok(q, (size_t) (e - q)) || f->size < 1) return false;
        memcpy(f->name, q, (size_t) (e - q));
        q = e + 1;
        if (kind != satchel_kind_of(f->name)) return false;
    }
    p->nfiles = (uint32_t) files;
    if (strcmp(q, "end\n") || !settle(p) || p->total != bytes) return false;
    p->head_len = head_len;
    return true;
}

int satchel_inspect(satchel_t *s, const char *bundle_path)
{
    static satchel_pkg_t next;
    char first[SATCHEL_FIRST + 1];
    zxm_link_t *l = s->link;
    uint64_t size = 0;
    int64_t head_len = 0;
    if (!path_ok(bundle_path, ".zxpkg")) return ZXM_E_ARG;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", bundle_path);
    zxm_p_u64(l, "max", SATCHEL_FIRST);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "size", &size)) return ZXM_E_PROTOCOL;
    rc = zxm_get_str(l, l->result, "text", first, sizeof first);
    const char *q = first + 8;
    if (rc || strlen(first) != SATCHEL_FIRST || strncmp(first, "zxpkg 1 ", 8) ||
        !read_num(&q, '\n', &head_len) || head_len <= (int64_t) SATCHEL_FIRST ||
        head_len > (int64_t) SATCHEL_HEAD_MAX) {
        s->fault = SATCHEL_F_INDEX;
        return ZXM_E_ARG;
    }
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", bundle_path);
    zxm_p_u64(l, "max", (uint64_t) head_len);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    rc = zxm_get_str(l, l->result, "text", s->head, sizeof s->head);
    if (rc || !parse_index(s->head, head_len, &next) ||
        size != (uint64_t) (head_len + next.total)) {
        s->fault = SATCHEL_F_INDEX;
        return ZXM_E_ARG;
    }
    s->fault = SATCHEL_FINE;
    s->got = next;
    return ZXM_OK;
}

int satchel_install(satchel_t *s, const char *recipe_path, const char *out_path,
                    const char *const *trust, uint32_t ntrust)
{
    static char list[SATCHEL_TRUST_MAX * (SATCHEL_ID_HEX + 12u) + 16u];
    char recipe[SATCHEL_ABS_MAX + 1], out[SATCHEL_ABS_MAX + 1], cid[SATCHEL_CID_MAX + 1],
        sender[SATCHEL_ID_HEX + 2];
    zxm_link_t *l = s->link;
    if (!path_ok(recipe_path, ".fab") || !path_ok(out_path, ".zxpkg") || !trust || ntrust < 1u ||
        ntrust > SATCHEL_TRUST_MAX)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < ntrust; i++)
        if (!hex_id(trust[i])) return ZXM_E_ARG;
    if (!s->confirmed) return ZXM_E_STATE;
    s->confirmed = false; /* the owner's word is spent by this install */
    s->installed = false;
    int rc = ask_root(s);
    if (rc) return rc;
    if (!absolute(s, recipe, sizeof recipe, recipe_path) || !absolute(s, out, sizeof out, out_path))
        return ZXM_E_ARG;
    int64_t have = 0;
    if ((rc = stat_size(l, out_path, &have)) != ZXM_OK) return rc;
    if (have >= 0) return ZXM_E_STATE; /* never over a file that is there */
    uint32_t o = (uint32_t) snprintf(list, sizeof list, "{\"trust\":[");
    for (uint32_t i = 0; i < ntrust; i++)
        o += (uint32_t) snprintf(list + o, sizeof list - o, "%s{\"id\":\"%s\"}", i ? "," : "",
                                 trust[i]);
    o += (uint32_t) snprintf(list + o, sizeof list - o, "]}");
    zxm_begin(l, "fab.receive");
    zxm_p_raw(l, list, o);
    zxm_p_str(l, "recipe", recipe);
    zxm_p_str(l, "out", out);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    uint64_t len = 0;
    if (zxm_get_str(l, l->result, "cid", cid, sizeof cid) ||
        zxm_get_str(l, l->result, "sender", sender, sizeof sender) ||
        zxm_get_u64(l, l->result, "len", &len) || !hex_id(sender) || !cid[0])
        return ZXM_E_PROTOCOL;
    /* a recipe this model signed must rebuild the very bundle it signed */
    if (s->is_signed && !strcmp(recipe_path, s->recipe) &&
        (strcmp(cid, s->cid) || len != (uint64_t) s->bundle_len))
        return ZXM_E_PROTOCOL;
    if ((rc = satchel_inspect(s, out_path)) != ZXM_OK) return rc;
    if (len != (uint64_t) (s->got.head_len + s->got.total)) return ZXM_E_PROTOCOL;
    memcpy(s->installed_at, out_path, strlen(out_path) + 1u);
    memcpy(s->sender, sender, SATCHEL_ID_HEX + 1u);
    s->installed = true;
    return ZXM_OK;
}
