/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ferry_model.c — Ferry (see ferry_model.h). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ferry_model.h"

void ferry_init(ferry_t *f, zxm_link_t *link)
{
    memset(f, 0, sizeof *f);
    f->link = link;
    f->avail = -1;
    f->spare = FERRY_SPARE;
    f->room = FERRY_NO_ROOM;
}

/* src into dst[cap] whole, or false */
static bool put(char *dst, uint32_t cap, const char *src)
{
    if (!src) return false;
    size_t n = strlen(src);
    if (n >= cap) return false;
    memcpy(dst, src, n + 1u);
    return true;
}

static bool is_home(const char *id)
{
    return !id[0] || !strcmp(id, "home");
}

static bool owned(const ferry_space_t *s)
{
    return is_home(s->id) || !strcmp(s->owner, "me");
}

static int refuse(ferry_t *f, uint32_t fault, int rc)
{
    f->fault = fault;
    return rc;
}

/* FM4: the room and the fit (FR1, FR2) */
static void fit(ferry_t *f)
{
    if (f->avail < 0) {
        f->room = FERRY_NO_ROOM;
        f->fits = false;
        return;
    }
    f->room = ferry_room(f->avail, f->plan_bytes, f->spare);
    f->fits = ferry_fits(f->avail, f->plan_bytes, f->spare);
}

/* FM5: FR6 over the bytes not packed yet, or over the spaces */
static void progress(ferry_t *f)
{
    if (f->plan_bytes > 0) {
        f->progress_x100 = ferry_progress_x100(f->plan_bytes, f->plan_bytes - f->packed_bytes);
        return;
    }
    int64_t n = 0, k = 0;
    for (uint32_t i = 0; i < f->nspaces; i++)
        if (f->space[i].carry) {
            n++;
            k += f->space[i].packed;
        }
    f->progress_x100 = n ? ferry_progress_x100(n, n - k) : 0;
}

int ferry_set_dest(ferry_t *f, const char *dest)
{
    if (!dest || dest[0] != '/' || !put(f->dest, sizeof f->dest, dest)) return ZXM_E_ARG;
    return ZXM_OK;
}

int ferry_set_spare(ferry_t *f, int64_t spare)
{
    if (!ferry_bytes_ok(spare)) return refuse(f, FERRY_F_TOO_BIG, ZXM_E_ARG);
    f->spare = spare;
    fit(f);
    return ZXM_OK;
}

int ferry_set_avail(ferry_t *f, int64_t avail)
{
    if (!ferry_bytes_ok(avail)) return refuse(f, FERRY_F_TOO_BIG, ZXM_E_ARG);
    f->avail = avail;
    fit(f);
    return ZXM_OK;
}

int ferry_measure(ferry_t *f)
{
    zxm_link_t *l = f->link;
    uint64_t avail;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "avail", &avail)) return ZXM_E_PROTOCOL;
    /* more room than 9(15) bytes is more than any plan holds */
    f->avail = avail > (uint64_t) FERRY_BYTES_MAX ? FERRY_BYTES_MAX : (int64_t) avail;
    fit(f);
    return ZXM_OK;
}

int ferry_replan(ferry_t *f)
{
    static int64_t bytes[FERRY_SPACES], files[FERRY_SPACES];
    uint32_t n = 0;
    for (uint32_t i = 0; i < f->nspaces; i++)
        if (f->space[i].carry) {
            bytes[n] = f->space[i].bytes;
            files[n++] = f->space[i].files;
        }
    int64_t tb = ferry_total(bytes, n), tf = ferry_total(files, n); /* banker.total, FR4 */
    if (tb == FERRY_REFUSED || tf == FERRY_REFUSED) return refuse(f, FERRY_F_TOO_BIG, ZXM_E_ARG);
    f->plan_bytes = tb;
    f->plan_files = tf;
    for (uint32_t i = 0; i < f->nspaces; i++) { /* banker.share, FR5 */
        ferry_space_t *s = &f->space[i];
        s->share_x100 = s->carry ? ferry_share_x100(s->bytes, tb) : 0;
    }
    fit(f);
    progress(f);
    return ZXM_OK;
}

int32_t ferry_find(const ferry_t *f, const char *id)
{
    if (!id) return -1;
    for (uint32_t i = 0; i < f->nspaces; i++)
        if (!strcmp(f->space[i].id, id) || (is_home(id) && is_home(f->space[i].id)))
            return (int32_t) i;
    return -1;
}

/* FM2: an owned space's bytes and files, folder by folder (files.list) */
static int walk(ferry_t *f, ferry_space_t *s)
{
    static char dir[FERRY_DIRS][FERRY_PATH + 1];
    static char name[FERRY_PATH + 1];
    zxm_link_t *l = f->link;
    uint32_t nd = 1, head = 0;
    int64_t bytes = 0, files = 0;
    dir[0][0] = 0;
    for (; head < nd; head++) {
        zxm_begin(l, "files.list");
        zxm_p_str(l, "path", dir[head]);
        zxm_p_space(l, s->id);
        int rc = zxm_call(l);
        if (rc) return rc;
        int32_t a = zxm_get(l, l->result, "entries");
        if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
        uint32_t n = zxm_len(l, a);
        for (uint32_t i = 0; i < n; i++) {
            int32_t o = zxm_at(l, a, i);
            bool isdir;
            uint64_t size;
            if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_bool(l, o, "dir", &isdir) ||
                zxm_get_str(l, o, "name", name, sizeof name) || !name[0])
                return ZXM_E_PROTOCOL;
            if (isdir) {
                if (nd == FERRY_DIRS) return refuse(f, FERRY_F_LIMITS, ZXM_E_TOOBIG);
                int k = dir[head][0] ? snprintf(dir[nd], sizeof dir[nd], "%s/%s", dir[head], name)
                                     : snprintf(dir[nd], sizeof dir[nd], "%s", name);
                if (k < 0 || (size_t) k >= sizeof dir[nd])
                    return refuse(f, FERRY_F_LIMITS, ZXM_E_TOOBIG);
                nd++;
                continue;
            }
            if (zxm_get_u64(l, o, "size", &size)) return ZXM_E_PROTOCOL;
            if (size > (uint64_t) FERRY_BYTES_MAX || bytes > FERRY_BYTES_MAX - (int64_t) size)
                return refuse(f, FERRY_F_TOO_BIG, ZXM_E_TOOBIG);
            bytes += (int64_t) size;
            files++;
        }
    }
    s->bytes = bytes;
    s->files = files;
    return ZXM_OK;
}

int ferry_peers(ferry_t *f)
{
    zxm_link_t *l = f->link;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t a = zxm_get(l, l->result, "peers");
    if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    static ferry_peer_t got[FERRY_PEERS];
    uint32_t n = zxm_len(l, a) < FERRY_PEERS ? zxm_len(l, a) : FERRY_PEERS;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, a, i);
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "id", got[i].id, sizeof got[i].id) ||
            strlen(got[i].id) != FERRY_NODE ||
            zxm_get_str(l, o, "addr", got[i].addr, sizeof got[i].addr))
            return ZXM_E_PROTOCOL;
    }
    memcpy(f->peer, got, n * sizeof *got);
    f->npeers = n;
    return ZXM_OK;
}

int ferry_set_target(ferry_t *f, uint32_t peer)
{
    if (peer >= f->npeers) return ZXM_E_ARG;
    memcpy(f->target, f->peer[peer].id, sizeof f->target);
    return ZXM_OK;
}

int ferry_survey(ferry_t *f)
{
    static ferry_space_t sp[FERRY_SPACES];
    static ferry_setting_t st[FERRY_SETTINGS];
    zxm_link_t *l = f->link;
    zxm_begin(l, "spaces.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t a = zxm_get(l, l->result, "spaces");
    if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t ns = zxm_len(l, a);
    if (ns > FERRY_SPACES) return refuse(f, FERRY_F_LIMITS, ZXM_E_TOOBIG);
    memset(sp, 0, sizeof sp);
    for (uint32_t i = 0; i < ns; i++) {
        int32_t o = zxm_at(l, a, i);
        ferry_space_t *s = &sp[i];
        if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_str(l, o, "id", s->id, sizeof s->id) ||
            zxm_get_str(l, o, "kind", s->kind, sizeof s->kind) ||
            zxm_get_str(l, o, "name", s->name, sizeof s->name) ||
            zxm_get_str(l, o, "owner", s->owner, sizeof s->owner) ||
            zxm_get_str(l, o, "role", s->role, sizeof s->role) || !s->id[0])
            return ZXM_E_PROTOCOL;
        s->carry = true;
    }
    for (uint32_t i = 0; i < ns; i++)
        if (owned(&sp[i]) && (rc = walk(f, &sp[i])) != 0) return rc;

    zxm_begin(l, "features.get");
    if ((rc = zxm_call(l)) != 0) return rc;
    a = zxm_get(l, l->result, "features");
    if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t nt = zxm_len(l, a);
    if (nt > FERRY_SETTINGS) return refuse(f, FERRY_F_LIMITS, ZXM_E_TOOBIG);
    for (uint32_t i = 0; i < nt; i++) {
        int32_t o = zxm_at(l, a, i);
        ferry_setting_t *t = &st[i];
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "name", t->name, sizeof t->name) || !t->name[0] ||
            zxm_get_bool(l, o, "enabled", &t->enabled) ||
            zxm_get_bool(l, o, "regulated", &t->regulated))
            return ZXM_E_PROTOCOL;
        t->carry = true;
    }
    memcpy(f->space, sp, ns * sizeof *sp);
    f->nspaces = ns;
    memcpy(f->setting, st, nt * sizeof *st);
    f->nsettings = nt;
    f->packed_bytes = 0;
    f->fault = FERRY_F_NONE;
    return ferry_replan(f);
}

int ferry_choose_space(ferry_t *f, uint32_t i, bool carry)
{
    if (i >= f->nspaces) return ZXM_E_ARG;
    if (is_home(f->space[i].id) && !carry) return ZXM_E_STATE; /* the plan rides in home */
    bool was = f->space[i].carry;
    f->space[i].carry = carry;
    int rc = ferry_replan(f);
    if (rc) {
        f->space[i].carry = was;
        ferry_replan(f);
    }
    return rc;
}

int ferry_choose_setting(ferry_t *f, uint32_t i, bool carry)
{
    if (i >= f->nsettings) return ZXM_E_ARG;
    f->setting[i].carry = carry;
    return ZXM_OK;
}

/* the carried owned space a gathered file goes into, or -1 */
static int32_t gather_into(const ferry_t *f, const char *to_space)
{
    int32_t k = ferry_find(f, to_space);
    return k >= 0 && f->space[k].carry && owned(&f->space[k]) ? k : -1;
}

/* the space's size again after a file came in (FM8) */
static int regather(ferry_t *f, int32_t k)
{
    ferry_space_t s = f->space[k];
    int rc = walk(f, &s);
    if (rc) return rc;
    ferry_space_t was = f->space[k];
    f->space[k] = s;
    if ((rc = ferry_replan(f)) != 0) {
        f->space[k] = was;
        ferry_replan(f);
    }
    return rc;
}

int ferry_gather(ferry_t *f, const char *from_space, const char *path, const char *to_space,
                 const char *to, uint32_t *ticket)
{
    if (!ticket) return ZXM_E_ARG;
    *ticket = 0;
    if (!from_space || !path || !path[0] || !to || !to[0] || strlen(from_space) > ZXM_SPACE_MAX ||
        strlen(path) > FERRY_PATH || strlen(to) > FERRY_PATH)
        return ZXM_E_ARG;
    int32_t k = gather_into(f, to_space);
    if (k < 0) return ZXM_E_STATE;
    zxm_link_t *l = f->link;
    bool held;
    uint64_t t = 0;
    zxm_begin(l, "files.cross");
    zxm_p_str(l, "from_space", is_home(from_space) ? "home" : from_space);
    zxm_p_str(l, "path", path);
    zxm_p_str(l, "to_space", is_home(f->space[k].id) ? "home" : f->space[k].id);
    zxm_p_str(l, "to", to);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "held", &held)) return ZXM_E_PROTOCOL;
    if (held) {
        if (zxm_get_u64(l, l->result, "ticket", &t) || !t || t > UINT32_MAX) return ZXM_E_PROTOCOL;
        *ticket = (uint32_t) t;
        return ZXM_OK; /* the owner confirms it, or it never happens */
    }
    return regather(f, k);
}

int ferry_gather_confirm(ferry_t *f, uint32_t ticket, const char *to_space)
{
    if (!ticket) return ZXM_E_ARG;
    int32_t k = gather_into(f, to_space);
    if (k < 0) return ZXM_E_STATE;
    zxm_begin(f->link, "files.cross");
    zxm_p_u64(f->link, "confirm", ticket);
    int rc = zxm_call(f->link);
    if (rc) return rc;
    return regather(f, k);
}

/* ----- the plan as text ----- */

typedef struct {
    char *p;
    uint32_t cap, n;
    bool over;
} out_t;

static void say(out_t *o, const char *fmt, ...)
{
    if (o->over) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(o->p + o->n, o->cap - o->n, fmt, ap);
    va_end(ap);
    if (k < 0 || (uint32_t) k >= o->cap - o->n)
        o->over = true;
    else
        o->n += (uint32_t) k;
}

/* a field the plan can hold: no tab, no line break */
static bool field(const char *s)
{
    return !strpbrk(s, "\t\r\n");
}

int32_t ferry_plan_text(const ferry_t *f, char *out, uint32_t cap)
{
    if (!out || !cap) return -1;
    out_t o = {out, cap, 0, false};
    say(&o, "ferry 1\nto\t%s\n", f->target);
    for (uint32_t i = 0; i < f->nspaces; i++) {
        const ferry_space_t *s = &f->space[i];
        if (!field(s->id) || !field(s->kind) || !field(s->role) || !field(s->owner) ||
            !field(s->name))
            return -1;
        say(&o, "space\t%s\t%s\t%s\t%d\t%lld\t%lld\t%u\t%u\t%s\t%s\n", s->id, s->kind, s->role,
            s->carry ? 1 : 0, (long long) s->bytes, (long long) s->files, s->snapshot, s->snapfiles,
            s->owner, s->name);
    }
    for (uint32_t i = 0; i < f->nsettings; i++) {
        const ferry_setting_t *t = &f->setting[i];
        if (!field(t->name)) return -1;
        say(&o, "setting\t%s\t%d\t%d\t%d\n", t->name, t->enabled ? 1 : 0, t->regulated ? 1 : 0,
            t->carry ? 1 : 0);
    }
    say(&o, "end\n");
    return o.over ? -1 : (int32_t) o.n;
}

/* the next tab-separated field of a line into out[cap]; false when none or too long */
static bool next_field(const char **s, const char *end, char *out, uint32_t cap)
{
    const char *p = *s;
    if (p > end) return false;
    const char *q = p;
    while (q < end && *q != '\t') q++;
    if ((uint32_t) (q - p) >= cap) return false;
    memcpy(out, p, (size_t) (q - p));
    out[q - p] = 0;
    *s = q + 1;
    return true;
}

/* a whole number 0..FERRY_BYTES_MAX written in decimal */
static bool number(const char *s, int64_t *v)
{
    int64_t x = 0;
    if (!s[0]) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || x > (FERRY_BYTES_MAX - (*s - '0')) / 10) return false;
        x = x * 10 + (*s - '0');
    }
    *v = x;
    return true;
}

/* a whole number 0..UINT32_MAX written in decimal */
static bool count(const char *s, uint32_t *v)
{
    int64_t x;
    if (!number(s, &x) || x > UINT32_MAX) return false;
    *v = (uint32_t) x;
    return true;
}

static bool flag(const char *s, bool *v)
{
    if (strcmp(s, "0") && strcmp(s, "1")) return false;
    *v = s[0] == '1';
    return true;
}

int ferry_plan_parse(ferry_t *f, const char *text, uint32_t n)
{
    static ferry_space_t sp[FERRY_SPACES];
    static ferry_setting_t st[FERRY_SETTINGS];
    char target[FERRY_NODE + 1] = "";
    char w[FERRY_NAME + 1], c[8];
    uint32_t ns = 0, nt = 0;
    bool head = false, tail = false;
    if (!text) return ZXM_E_ARG;
    memset(sp, 0, sizeof sp);
    memset(st, 0, sizeof st);
    const char *p = text, *stop = text + n;
    while (p < stop) {
        const char *e = memchr(p, '\n', (size_t) (stop - p));
        if (!e || tail) return refuse(f, FERRY_F_PLAN, ZXM_E_ARG);
        const char *s = p;
        p = e + 1;
        if (!head) {
            if (e - s != 7 || memcmp(s, "ferry 1", 7)) return refuse(f, FERRY_F_PLAN, ZXM_E_ARG);
            head = true;
            continue;
        }
        if (e - s == 3 && !memcmp(s, "end", 3)) {
            tail = true;
            continue;
        }
        if (!next_field(&s, e, w, sizeof w)) return refuse(f, FERRY_F_PLAN, ZXM_E_ARG);
        if (!strcmp(w, "to")) {
            if (!next_field(&s, e, target, sizeof target) || s <= e)
                return refuse(f, FERRY_F_PLAN, ZXM_E_ARG);
        } else if (!strcmp(w, "space")) {
            if (ns == FERRY_SPACES) return refuse(f, FERRY_F_LIMITS, ZXM_E_TOOBIG);
            ferry_space_t *x = &sp[ns++];
            if (!next_field(&s, e, x->id, sizeof x->id) || !x->id[0] ||
                !next_field(&s, e, x->kind, sizeof x->kind) ||
                !next_field(&s, e, x->role, sizeof x->role) || !next_field(&s, e, c, sizeof c) ||
                !flag(c, &x->carry) || !next_field(&s, e, w, sizeof w) || !number(w, &x->bytes) ||
                !next_field(&s, e, w, sizeof w) || !number(w, &x->files) ||
                !next_field(&s, e, w, sizeof w) || !count(w, &x->snapshot) ||
                !next_field(&s, e, w, sizeof w) || !count(w, &x->snapfiles) ||
                !next_field(&s, e, x->owner, sizeof x->owner) ||
                !next_field(&s, e, x->name, sizeof x->name) || s <= e)
                return refuse(f, FERRY_F_PLAN, ZXM_E_ARG);
        } else if (!strcmp(w, "setting")) {
            if (nt == FERRY_SETTINGS) return refuse(f, FERRY_F_LIMITS, ZXM_E_TOOBIG);
            ferry_setting_t *t = &st[nt++];
            if (!next_field(&s, e, t->name, sizeof t->name) || !t->name[0] ||
                !next_field(&s, e, c, sizeof c) || !flag(c, &t->enabled) ||
                !next_field(&s, e, c, sizeof c) || !flag(c, &t->regulated) ||
                !next_field(&s, e, c, sizeof c) || !flag(c, &t->carry) || s <= e)
                return refuse(f, FERRY_F_PLAN, ZXM_E_ARG);
        } else {
            return refuse(f, FERRY_F_PLAN, ZXM_E_ARG);
        }
    }
    if (!head || !tail) return refuse(f, FERRY_F_PLAN, ZXM_E_ARG);
    memcpy(f->space, sp, ns * sizeof *sp);
    f->nspaces = ns;
    memcpy(f->setting, st, nt * sizeof *st);
    f->nsettings = nt;
    memcpy(f->target, target, sizeof target);
    f->packed_bytes = 0;
    f->fault = FERRY_F_NONE;
    return ferry_replan(f);
}

/* ----- FM5: packing ----- */

/* the plan file in home, its folder made when missing */
static int write_plan(ferry_t *f)
{
    static char text[FERRY_PLAN_MAX];
    zxm_link_t *l = f->link;
    int32_t n = ferry_plan_text(f, text, sizeof text);
    if (n < 0) return refuse(f, FERRY_F_LIMITS, ZXM_E_TOOBIG);
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", FERRY_PLAN_DIR);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) {
        zxm_begin(l, "files.mkdir");
        zxm_p_str(l, "path", FERRY_PLAN_DIR);
        rc = zxm_call(l);
    }
    if (rc) return rc;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", FERRY_PLAN_FILE);
    zxm_p_strn(l, "text", text, (uint32_t) n);
    return zxm_call(l);
}

/* one owned space backed up to dest and read back whole */
static int pack_one(ferry_t *f, ferry_space_t *s)
{
    zxm_link_t *l = f->link;
    uint64_t snap, files, bad, missing;
    bool ok;
    zxm_begin(l, "backup.run");
    zxm_p_space(l, s->id);
    zxm_p_str(l, "dest", f->dest);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "snapshot", &snap) || !snap || snap > UINT32_MAX ||
        zxm_get_u64(l, l->result, "files", &files) || files > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    zxm_begin(l, "backup.verify");
    zxm_p_space(l, s->id);
    zxm_p_str(l, "dest", f->dest);
    zxm_p_u64(l, "snapshot", snap);
    if ((rc = zxm_call(l)) != 0) return rc;
    if (zxm_get_bool(l, l->result, "ok", &ok) || zxm_get_u64(l, l->result, "bad", &bad) ||
        zxm_get_u64(l, l->result, "missing", &missing) || ok != (!bad && !missing))
        return ZXM_E_PROTOCOL;
    if (!ok) return refuse(f, FERRY_F_CHECK, ZXM_E_STATE);
    s->snapshot = (uint32_t) snap;
    s->snapfiles = (uint32_t) files;
    s->packed = true;
    return ZXM_OK;
}

int ferry_pack(ferry_t *f)
{
    if (!f->dest[0] || !f->nspaces || ferry_find(f, "home") < 0) return ZXM_E_STATE;
    int rc = ferry_replan(f);
    if (rc) return rc;
    if (!f->fits) return refuse(f, FERRY_F_NO_ROOM, ZXM_E_STATE);
    for (uint32_t i = 0; i < f->nspaces; i++) {
        f->space[i].packed = false;
        f->space[i].snapshot = f->space[i].snapfiles = 0;
    }
    f->packed_bytes = 0;
    progress(f);
    uint32_t home = (uint32_t) ferry_find(f, "home");
    for (uint32_t pass = 0; pass < 2;
         pass++) /* home last: the plan, with every backup, rides in it */
        for (uint32_t i = 0; i < f->nspaces; i++) {
            ferry_space_t *s = &f->space[i];
            if (!s->carry || (pass == 0) == (i == home)) continue;
            if (pass == 1 && (rc = write_plan(f)) != 0) return rc;
            if (owned(s) && (rc = pack_one(f, s)) != 0) return rc;
            s->packed = true; /* a joined space travels in the plan as its membership */
            f->packed_bytes += s->bytes;
            progress(f);
            zxm_progress(f->link, (uint64_t) f->packed_bytes, (uint64_t) f->plan_bytes);
        }
    f->fault = FERRY_F_NONE;
    return ZXM_OK;
}

/* ----- FM6: arriving ----- */

int ferry_load_plan(ferry_t *f)
{
    static char landed[FERRY_PATH + 1];
    static char text[FERRY_PLAN_MAX + 1];
    if (!f->dest[0]) return ZXM_E_STATE;
    zxm_link_t *l = f->link;
    bool eof;
    zxm_begin(l, "backup.restore");
    zxm_p_str(l, "dest", f->dest);
    zxm_p_str(l, "path", FERRY_PLAN_FILE);
    zxm_p_str(l, "to", FERRY_PLAN_IN);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "to", landed, sizeof landed) || !landed[0]) return ZXM_E_PROTOCOL;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", landed);
    zxm_p_u64(l, "max", FERRY_PLAN_MAX);
    if ((rc = zxm_call(l)) != 0) return rc;
    if (zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return refuse(f, FERRY_F_PLAN, ZXM_E_TOOBIG);
    if (zxm_get_str(l, l->result, "text", text, sizeof text)) return ZXM_E_PROTOCOL;
    return ferry_plan_parse(f, text, (uint32_t) strlen(text));
}

/* the space there again: kept when it is, else made (spaces.create or spaces.join) */
static int remake(ferry_t *f, const ferry_space_t *s)
{
    static char id[ZXM_SPACE_MAX + 1];
    zxm_link_t *l = f->link;
    if (is_home(s->id)) return ZXM_OK;
    zxm_begin(l, "spaces.get");
    zxm_p_str(l, "space", s->id);
    int rc = zxm_call(l);
    if (rc != ZXM_E_REMOTE) return rc;
    if (owned(s)) {
        zxm_begin(l, "spaces.create");
        zxm_p_str(l, "kind", s->kind);
        zxm_p_str(l, "name", s->name);
    } else {
        zxm_begin(l, "spaces.join");
        zxm_p_str(l, "id", s->id);
        zxm_p_str(l, "kind", s->kind);
        zxm_p_str(l, "name", s->name);
        zxm_p_str(l, "owner", s->owner);
        zxm_p_str(l, "role", s->role);
    }
    if ((rc = zxm_call(l)) != 0) return rc;
    if (zxm_get_str(l, l->result, "id", id, sizeof id)) return ZXM_E_PROTOCOL;
    /* a space made under another id would not find its backups */
    return strcmp(id, s->id) ? refuse(f, FERRY_F_SPACE, ZXM_E_STATE) : ZXM_OK;
}

int ferry_arrive(ferry_t *f)
{
    if (!f->dest[0] || !f->nspaces) return ZXM_E_STATE;
    zxm_link_t *l = f->link;
    f->restored = 0;
    for (uint32_t i = 0; i < f->nspaces; i++) {
        ferry_space_t *s = &f->space[i];
        uint64_t files;
        if (!s->carry) continue;
        int rc = remake(f, s);
        if (rc) return rc;
        /* home holds the plan, so its newest backup has files; another owned space
         * is restored from the backup that packed it, when that holds any */
        if (owned(s) && (is_home(s->id) || s->snapfiles)) {
            zxm_begin(l, "backup.restore");
            zxm_p_space(l, s->id);
            zxm_p_str(l, "dest", f->dest);
            if (!is_home(s->id)) zxm_p_u64(l, "snapshot", s->snapshot);
            zxm_p_str(l, "to", FERRY_ARRIVED);
            if ((rc = zxm_call(l)) != 0) return rc;
            if (zxm_get_u64(l, l->result, "files", &files) || !files ||
                files > UINT32_MAX - f->restored)
                return ZXM_E_PROTOCOL;
            f->restored += (uint32_t) files;
        }
        s->arrived = true;
    }
    f->fault = FERRY_F_NONE;
    return ZXM_OK;
}

/* ----- FM7: settings ----- */

int ferry_apply_settings(ferry_t *f, bool approved)
{
    zxm_link_t *l = f->link;
    f->applied = f->held = 0;
    for (uint32_t i = 0; i < f->nsettings; i++) {
        const ferry_setting_t *t = &f->setting[i];
        if (!t->carry) continue;
        if (!ferry_move_ok(t->regulated ? FERRY_K_REGULATED : FERRY_K_SETTING, approved ? 1 : 0)) {
            f->held++; /* FR3: the owner's word first */
            continue;
        }
        zxm_begin(l, "features.set");
        zxm_p_str(l, "feature", t->name);
        zxm_p_bool(l, "enabled", t->enabled);
        int rc = zxm_call(l);
        if (rc) return rc;
        f->applied++;
    }
    return ZXM_OK;
}
