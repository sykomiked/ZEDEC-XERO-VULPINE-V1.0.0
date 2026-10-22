/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* homestead_model.c — Homestead, the owner's home devices and scenes (see
 * homestead_model.h). Locks, heaters and ovens change only with the
 * owner's approval; an AI's suggestion is held, never run. */
#include <string.h>

#include "homestead_model.h"

static const char *const KIND[HOMESTEAD_KINDS + 1] = {"",     "light",  "plug", "blind",
                                                      "lock", "heater", "oven"};

/* ---- text ------------------------------------------------------------------ */

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* " <v>" */
static bool put_sp(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    return put(out, cap, at, " ") && put_num(out, cap, at, v);
}

/* The whole file for h (HM1-HM4); its length, or -1 when it does not fit. */
static int32_t compose(const homestead_t *h, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!put(out, cap, &at, "homestead 1\n")) return -1;
    for (uint32_t i = 0; i < h->ndev; i++) {
        const homestead_device_t *d = &h->dev[i];
        if (!put(out, cap, &at, "device") || !put_sp(out, cap, &at, d->id) ||
            !put_sp(out, cap, &at, d->kind) || !put_sp(out, cap, &at, d->lo) ||
            !put_sp(out, cap, &at, d->hi) || !put_sp(out, cap, &at, d->setpoint) ||
            !put(out, cap, &at, " ") || !put(out, cap, &at, d->node[0] ? d->node : "-") ||
            !put(out, cap, &at, " ") || !put(out, cap, &at, d->name) || !put(out, cap, &at, "\n"))
            return -1;
    }
    for (uint32_t i = 0; i < h->nstep; i++)
        if (!put(out, cap, &at, "step") || !put_sp(out, cap, &at, h->step[i].scene) ||
            !put_sp(out, cap, &at, h->step[i].device) ||
            !put_sp(out, cap, &at, h->step[i].setpoint) || !put(out, cap, &at, "\n"))
            return -1;
    for (uint32_t i = 0; i < h->nuse; i++)
        if (!put(out, cap, &at, "use") || !put_sp(out, cap, &at, h->use[i].device) ||
            !put_sp(out, cap, &at, h->use[i].wh) || !put(out, cap, &at, "\n"))
            return -1;
    return (int32_t) at;
}

/* A plain decimal of 1 to 9 digits (HM5), a minus sign allowed before one
 * other than 0, at s[*at], ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && s[i] == '-';
    if (neg) i++;
    uint32_t first = i;
    int64_t x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 10u) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > 9u || i >= n || s[i] != end) return false;
    if (s[first] == '0' && (digits > 1u || neg)) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

static bool node_ok(const char *s, uint32_t n)
{
    if (n != HOMESTEAD_NODE_HEX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

static bool name_ok(const char *s, uint32_t n)
{
    if (!n || n > HOMESTEAD_NAME_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7f) return false;
    return true;
}

/* The length of s, at most max (NULL is 0). */
static uint32_t bounded_len(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (s && n < max && s[n]) n++;
    return n;
}

static int32_t find_dev(const homestead_t *h, int64_t id)
{
    for (uint32_t i = 0; i < h->ndev; i++)
        if (h->dev[i].id == id) return (int32_t) i;
    return -1;
}

static bool inside(const homestead_device_t *d, int64_t setpoint)
{
    return setpoint >= d->lo && setpoint <= d->hi;
}

/* HM2's record, its range and its setpoint. */
static bool device_fits(int64_t id, int64_t kind, int64_t lo, int64_t hi, int64_t setpoint)
{
    return homestead_device_ok(id, kind, lo, hi, setpoint) && lo <= hi && setpoint >= lo &&
           setpoint <= hi;
}

/* One "device" line after its word (HM2). */
static bool parse_device(homestead_t *next, const char *s, uint32_t n, uint32_t *at)
{
    int64_t v[5];
    for (uint32_t k = 0; k < 5u; k++)
        if (!get_num(s, n, at, ' ', &v[k])) return false;
    if (!device_fits(v[0], v[1], v[2], v[3], v[4]) || next->ndev >= HOMESTEAD_DEVICES ||
        (next->ndev && next->dev[next->ndev - 1u].id >= v[0]))
        return false;
    homestead_device_t *d = &next->dev[next->ndev];
    memset(d, 0, sizeof *d);
    d->id = (int32_t) v[0];
    d->kind = (int32_t) v[1];
    d->lo = v[2];
    d->hi = v[3];
    d->setpoint = v[4];
    uint32_t i = *at;
    while (i < n && s[i] != ' ' && s[i] != '\n') i++;
    if (i >= n || s[i] != ' ') return false;
    if (i - *at == 1u && s[*at] == '-')
        d->node[0] = 0;
    else if (node_ok(s + *at, i - *at))
        memcpy(d->node, s + *at, HOMESTEAD_NODE_HEX); /* d->node ends in the memset's 0 */
    else
        return false;
    uint32_t st = ++i;
    while (i < n && s[i] != '\n') i++;
    if (i >= n || !name_ok(s + st, i - st)) return false;
    memcpy(d->name, s + st, i - st);
    d->name[i - st] = 0;
    *at = i + 1u;
    next->ndev++;
    return true;
}

/* One "step" line after its word (HM3). */
static bool parse_step(homestead_t *next, const char *s, uint32_t n, uint32_t *at)
{
    int64_t sc, dv, sp;
    if (!get_num(s, n, at, ' ', &sc) || !get_num(s, n, at, ' ', &dv) ||
        !get_num(s, n, at, '\n', &sp) || !homestead_scene_ok(sc, dv, sp) ||
        sc < HOMESTEAD_SCENE_FIRST || next->nstep >= HOMESTEAD_STEPS)
        return false;
    int32_t k = find_dev(next, dv);
    if (k < 0 || !inside(&next->dev[k], sp)) return false;
    if (next->nstep) {
        const homestead_step_t *p = &next->step[next->nstep - 1u];
        if (p->scene > sc || (p->scene == sc && p->device >= dv)) return false;
    }
    next->step[next->nstep++] = (homestead_step_t){(int32_t) sc, (int32_t) dv, sp};
    return true;
}

/* One "use" line after its word (HM4). */
static bool parse_use(homestead_t *next, const char *s, uint32_t n, uint32_t *at)
{
    int64_t dv, wh;
    if (!get_num(s, n, at, ' ', &dv) || !get_num(s, n, at, '\n', &wh) || find_dev(next, dv) < 0 ||
        wh < 0 || wh > HOMESTEAD_WH_MAX || next->nuse >= HOMESTEAD_READINGS)
        return false;
    next->use[next->nuse++] = (homestead_reading_t){(int32_t) dv, wh};
    return true;
}

/* Parse text into next (HM1-HM5): false when it is not Homestead's file. */
static bool parse(homestead_t *next, const char *s, uint32_t n)
{
    static const char HEAD[] = "homestead 1\n";
    static const char *const WORD[3] = {"device ", "step ", "use "};
    uint32_t at = (uint32_t) strlen(HEAD), part = 0;
    if (n < at || memcmp(s, HEAD, at)) return false;
    next->ndev = next->nstep = next->nuse = 0;
    while (at < n) {
        uint32_t w = part;
        while (w < 3u && (n - at < strlen(WORD[w]) || memcmp(s + at, WORD[w], strlen(WORD[w]))))
            w++;
        if (w >= 3u) return false; /* an unknown line, or a part out of order */
        part = w;
        at += (uint32_t) strlen(WORD[w]);
        bool ok = w == 0u   ? parse_device(next, s, n, &at)
                  : w == 1u ? parse_step(next, s, n, &at)
                            : parse_use(next, s, n, &at);
        if (!ok) return false;
    }
    return true;
}

/* ---- zxvd ------------------------------------------------------------------ */

/* One request's text: whole lines of at most this many bytes. Names hold
 * no control byte, so each byte escapes to at most two ("\n", "\\"),
 * well inside the request line. */
#define PIECE 6000u

static int write_text(zxm_link_t *l, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) {
            n = PIECE;
            while (n && text[at + n - 1u] != '\n') n--;
            if (!n) return ZXM_E_TOOBIG;
        }
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", HOMESTEAD_FILE);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static void keep(homestead_t *h, const homestead_t *next)
{
    memcpy(h->dev, next->dev, (size_t) next->ndev * sizeof h->dev[0]);
    h->ndev = next->ndev;
    memcpy(h->step, next->step, (size_t) next->nstep * sizeof h->step[0]);
    h->nstep = next->nstep;
    memcpy(h->use, next->use, (size_t) next->nuse * sizeof h->use[0]);
    h->nuse = next->nuse;
}

/* Write next whole, then keep it (HM5). */
static int commit(homestead_t *h, const homestead_t *next)
{
    static char text[HOMESTEAD_TEXT_MAX];
    if (!h->loaded) return ZXM_E_STATE;
    int32_t len = compose(next, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(h->link, text, (uint32_t) len);
    if (rc) return rc;
    keep(h, next);
    return ZXM_OK;
}

static homestead_t NEXT; /* a change before it is written (HM5) */

static void begin_change(const homestead_t *h)
{
    keep(&NEXT, h);
}

void homestead_init(homestead_t *h, zxm_link_t *link)
{
    memset(h, 0, sizeof *h);
    h->link = link;
}

int homestead_load(homestead_t *h)
{
    static char text[HOMESTEAD_TEXT_MAX];
    static char chunk[HOMESTEAD_TEXT_MAX];
    static homestead_t next;
    zxm_link_t *l = h->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", HOMESTEAD_FILE);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == HOMESTEAD_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            homestead_init(h, l); /* not made yet: an empty home */
            h->loaded = true;
            return ZXM_OK;
        }
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size >= HOMESTEAD_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= HOMESTEAD_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    if (!parse(&next, text, len)) {
        h->loaded = false; /* HM5: never overwritten */
        return ZXM_E_PROTOCOL;
    }
    keep(h, &next);
    h->held.pending = false;
    h->loaded = true;
    return ZXM_OK;
}

const homestead_device_t *homestead_find(const homestead_t *h, int64_t id)
{
    int32_t k = find_dev(h, id);
    return k < 0 ? NULL : &h->dev[k];
}

/* ---- notices --------------------------------------------------------------- */

static void notice(homestead_t *h, const homestead_device_t *d, const char *what, int64_t setpoint,
                   const char *tail)
{
    char *s = h->notice;
    uint32_t cap = sizeof h->notice, at = 0;
    s[0] = 0;
    bool ok = put(s, cap, &at, what) && put(s, cap, &at, d->name) && put(s, cap, &at, " (") &&
              put(s, cap, &at, KIND[d->kind]) && put(s, cap, &at, ") to") &&
              put_sp(s, cap, &at, setpoint) && put(s, cap, &at, tail);
    if (!ok) s[0] = 0;
}

static void outside_notice(homestead_t *h, const homestead_device_t *d, int64_t setpoint)
{
    char *s = h->notice;
    uint32_t cap = sizeof h->notice, at = 0;
    s[0] = 0;
    bool ok = put(s, cap, &at, "Setpoint") && put_sp(s, cap, &at, setpoint) &&
              put(s, cap, &at, " is outside ") && put(s, cap, &at, d->name) &&
              put(s, cap, &at, "'s range ") && put_num(s, cap, &at, d->lo) &&
              put(s, cap, &at, "..") && put_num(s, cap, &at, d->hi) &&
              put(s, cap, &at, ": nothing changed.");
    if (!ok) s[0] = 0;
}

/* ---- devices --------------------------------------------------------------- */

int homestead_add_device(homestead_t *h, int64_t id, int64_t kind, int64_t lo, int64_t hi,
                         int64_t setpoint, const char *node, const char *name)
{
    uint32_t nn = bounded_len(node, HOMESTEAD_NODE_HEX + 1u);
    uint32_t ln = bounded_len(name, HOMESTEAD_NAME_MAX + 1u);
    if (!device_fits(id, kind, lo, hi, setpoint) || (nn && !node_ok(node, nn)) ||
        !name_ok(name, ln))
        return ZXM_E_ARG;
    if (!h->loaded) return ZXM_E_STATE;
    if (find_dev(h, id) >= 0) return ZXM_E_ARG;
    if (h->ndev >= HOMESTEAD_DEVICES) return ZXM_E_TOOBIG;
    begin_change(h);
    uint32_t at = 0;
    while (at < NEXT.ndev && NEXT.dev[at].id < id) at++;
    memmove(&NEXT.dev[at + 1u], &NEXT.dev[at], (size_t) (NEXT.ndev - at) * sizeof NEXT.dev[0]);
    homestead_device_t *d = &NEXT.dev[at];
    *d = (homestead_device_t){(int32_t) id, (int32_t) kind, lo, hi, setpoint, {0}, {0}, false};
    if (nn) memcpy(d->node, node, nn);
    memcpy(d->name, name, ln);
    NEXT.ndev++;
    return commit(h, &NEXT);
}

int homestead_remove_device(homestead_t *h, int64_t id)
{
    if (!h->loaded) return ZXM_E_STATE;
    int32_t k = find_dev(h, id);
    if (k < 0) return ZXM_E_ARG;
    begin_change(h);
    memmove(&NEXT.dev[k], &NEXT.dev[k + 1],
            (size_t) (NEXT.ndev - (uint32_t) k - 1u) * sizeof NEXT.dev[0]);
    NEXT.ndev--;
    uint32_t j = 0;
    for (uint32_t i = 0; i < NEXT.nstep; i++)
        if (NEXT.step[i].device != id) NEXT.step[j++] = NEXT.step[i];
    NEXT.nstep = j;
    j = 0;
    for (uint32_t i = 0; i < NEXT.nuse; i++)
        if (NEXT.use[i].device != id) NEXT.use[j++] = NEXT.use[i];
    NEXT.nuse = j;
    int rc = commit(h, &NEXT);
    if (!rc && h->held.pending && h->held.device == id) h->held.pending = false;
    return rc;
}

/* ---- commands (HM6, HM7) --------------------------------------------------- */

/* HR2 for one command: ZXM_OK when it may run, HOMESTEAD_OWNER when an
 * approval is owed, HOMESTEAD_HELD for an AI's, ZXM_E_ARG outside the range. */
static int judge(const homestead_device_t *d, int64_t setpoint, int64_t owner, int64_t approved,
                 int64_t ai)
{
    int64_t above = setpoint - d->lo, below = d->hi - setpoint;
    int64_t owed = homestead_owed(d->kind, owner, approved);
    if (homestead_command_ok(above, below, owed, ai)) return ZXM_OK;
    if (above < 0 || below < 0 || above > HOMESTEAD_MARGIN_MAX || below > HOMESTEAD_MARGIN_MAX)
        return ZXM_E_ARG;
    return ai ? HOMESTEAD_HELD : HOMESTEAD_OWNER;
}

int homestead_command(homestead_t *h, int64_t id, int64_t setpoint, int64_t owner, int64_t approved)
{
    if (!h->loaded) return ZXM_E_STATE;
    int32_t k = find_dev(h, id);
    if (k < 0) return ZXM_E_ARG;
    const homestead_device_t *d = &h->dev[k];
    int v = judge(d, setpoint, owner, approved, 0);
    if (v == ZXM_E_ARG) {
        outside_notice(h, d, setpoint);
        return v;
    }
    if (v == HOMESTEAD_OWNER) {
        notice(h, d, "Waiting for the owner's approval to set ", setpoint,
               ": a lock, a heater or an oven changes only with it. Nothing changed.");
        return v;
    }
    begin_change(h);
    NEXT.dev[k].setpoint = setpoint;
    int rc = commit(h, &NEXT);
    if (!rc) notice(h, &h->dev[k], "Set ", setpoint, ".");
    return rc;
}

int homestead_suggest(homestead_t *h, int64_t id, int64_t setpoint)
{
    if (!h->loaded) return ZXM_E_STATE;
    int32_t k = find_dev(h, id);
    if (k < 0) return ZXM_E_ARG;
    const homestead_device_t *d = &h->dev[k];
    int v = judge(d, setpoint, 0, 0, 1);
    if (v == ZXM_E_ARG) {
        outside_notice(h, d, setpoint);
        return v;
    }
    h->held = (homestead_suggestion_t){true, d->id, setpoint};
    notice(h, d, "An AI suggests setting ", setpoint,
           ". It is held: nothing runs until the owner accepts it.");
    return HOMESTEAD_HELD;
}

int homestead_accept(homestead_t *h, int64_t owner, int64_t approved)
{
    if (!h->held.pending) return ZXM_E_STATE;
    int rc = homestead_command(h, h->held.device, h->held.setpoint, owner, approved);
    if (rc == ZXM_OK || rc == ZXM_E_ARG) h->held.pending = false;
    return rc;
}

int homestead_reject(homestead_t *h)
{
    if (!h->held.pending) return ZXM_E_STATE;
    h->held.pending = false;
    h->notice[0] = 0;
    return ZXM_OK;
}

/* ---- scenes (HM3, HM8) ----------------------------------------------------- */

int homestead_set_step(homestead_t *h, int64_t scene, int64_t device, int64_t setpoint)
{
    if (!homestead_scene_ok(scene, device, setpoint) || scene < HOMESTEAD_SCENE_FIRST)
        return ZXM_E_ARG;
    if (!h->loaded) return ZXM_E_STATE;
    int32_t k = find_dev(h, device);
    if (k < 0 || !inside(&h->dev[k], setpoint)) return ZXM_E_ARG;
    begin_change(h);
    uint32_t at = 0;
    while (at < NEXT.nstep && (NEXT.step[at].scene < scene ||
                               (NEXT.step[at].scene == scene && NEXT.step[at].device < device)))
        at++;
    if (at < NEXT.nstep && NEXT.step[at].scene == scene && NEXT.step[at].device == device) {
        NEXT.step[at].setpoint = setpoint;
    } else {
        if (NEXT.nstep >= HOMESTEAD_STEPS) return ZXM_E_TOOBIG;
        memmove(&NEXT.step[at + 1u], &NEXT.step[at],
                (size_t) (NEXT.nstep - at) * sizeof NEXT.step[0]);
        NEXT.step[at] = (homestead_step_t){(int32_t) scene, (int32_t) device, setpoint};
        NEXT.nstep++;
    }
    return commit(h, &NEXT);
}

int homestead_remove_scene(homestead_t *h, int64_t scene)
{
    if (!h->loaded) return ZXM_E_STATE;
    begin_change(h);
    uint32_t j = 0;
    for (uint32_t i = 0; i < NEXT.nstep; i++)
        if (NEXT.step[i].scene != scene) NEXT.step[j++] = NEXT.step[i];
    if (j == NEXT.nstep) return ZXM_E_ARG;
    NEXT.nstep = j;
    return commit(h, &NEXT);
}

int homestead_run_scene(homestead_t *h, int64_t scene, int64_t owner, int64_t approved)
{
    if (!h->loaded) return ZXM_E_STATE;
    uint32_t steps = 0;
    for (uint32_t i = 0; i < h->nstep; i++) {
        if (h->step[i].scene != scene) continue;
        const homestead_device_t *d = homestead_find(h, h->step[i].device);
        if (!d) return ZXM_E_PROTOCOL; /* HM3 keeps every step's device */
        int v = judge(d, h->step[i].setpoint, owner, approved, 0);
        if (v == ZXM_E_ARG) {
            outside_notice(h, d, h->step[i].setpoint);
            return v;
        }
        if (v == HOMESTEAD_OWNER) {
            notice(h, d, "The scene waits for the owner's approval to set ", h->step[i].setpoint,
                   ". No step ran.");
            return v;
        }
        steps++;
    }
    if (!steps) return ZXM_E_ARG;
    begin_change(h);
    for (uint32_t i = 0; i < NEXT.nstep; i++)
        if (NEXT.step[i].scene == scene)
            NEXT.dev[find_dev(&NEXT, NEXT.step[i].device)].setpoint = NEXT.step[i].setpoint;
    int rc = commit(h, &NEXT);
    if (!rc) {
        uint32_t at = 0;
        h->notice[0] = 0;
        if (!put(h->notice, sizeof h->notice, &at, "Scene") ||
            !put_sp(h->notice, sizeof h->notice, &at, scene) ||
            !put(h->notice, sizeof h->notice, &at, " ran:") ||
            !put_sp(h->notice, sizeof h->notice, &at, steps) ||
            !put(h->notice, sizeof h->notice, &at, steps == 1u ? " device set." : " devices set."))
            h->notice[0] = 0;
    }
    return rc;
}

/* ---- energy (HM4, HM9) ----------------------------------------------------- */

int homestead_add_reading(homestead_t *h, int64_t device, int64_t wh)
{
    if (wh < 0 || wh > HOMESTEAD_WH_MAX) return ZXM_E_ARG;
    if (!h->loaded) return ZXM_E_STATE;
    if (find_dev(h, device) < 0) return ZXM_E_ARG;
    if (h->nuse >= HOMESTEAD_READINGS) return ZXM_E_TOOBIG;
    begin_change(h);
    NEXT.use[NEXT.nuse++] = (homestead_reading_t){(int32_t) device, wh};
    return commit(h, &NEXT);
}

int homestead_device_energy(const homestead_t *h, int64_t device, int64_t *total)
{
    static int64_t dev[HOMESTEAD_READINGS], wh[HOMESTEAD_READINGS];
    if (!total || find_dev(h, device) < 0) return ZXM_E_ARG;
    for (uint32_t i = 0; i < h->nuse; i++) {
        dev[i] = h->use[i].device;
        wh[i] = h->use[i].wh;
    }
    return homestead_energy(dev, wh, h->nuse, device, total) ? ZXM_E_TOOBIG : ZXM_OK;
}

/* ---- bridges --------------------------------------------------------------- */

int homestead_bridges(homestead_t *h, uint32_t *count)
{
    static bool seen[HOMESTEAD_DEVICES];
    zxm_link_t *l = h->link;
    if (!count) return ZXM_E_ARG;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    memset(seen, 0, sizeof seen);
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        char id[HOMESTEAD_NODE_HEX + 2];
        int32_t obj = zxm_at(l, arr, i);
        if (obj < 0 || zxm_get_str(l, obj, "id", id, sizeof id) ||
            !node_ok(id, (uint32_t) strlen(id)))
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; k < h->ndev; k++)
            if (h->dev[k].node[0] && !strcmp(h->dev[k].node, id)) seen[k] = true;
    }
    uint32_t c = 0;
    for (uint32_t k = 0; k < h->ndev; k++) {
        h->dev[k].reachable = seen[k];
        c += seen[k];
    }
    *count = c;
    return ZXM_OK;
}
