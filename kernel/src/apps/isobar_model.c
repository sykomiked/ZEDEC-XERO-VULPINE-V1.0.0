/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* isobar_model.c — Isobar, a local weather station network (see
 * isobar_model.h). Advisory only: the outlook never replaces an official
 * warning, and a station's heater changes only in Homestead, with the
 * owner's approval. */
#include <string.h>

#include "furrow_rules.h"
#include "homestead_rules.h"
#include "isobar_model.h"
#include "trellis_rules.h"
#include "weatherglass_model.h"
#include "wellspring_model.h"

static const char *const KIND[ISOBAR_KINDS + 1] = {
    "", "air temperature", "pressure", "humidity", "wind", "rain"};
static const char *const UNIT[ISOBAR_KINDS + 1] = {"", " C", " hPa", " %", " m/s", " mm"};

/* cos(k degrees) in ten-thousandths, k = 0..90 (isobar_apart) */
static const int32_t COS[91] = {
    10000, 9998, 9994, 9986, 9976, 9962, 9945, 9925, 9903, 9877, 9848, 9816, 9781, 9744, 9703, 9659,
    9613,  9563, 9511, 9455, 9397, 9336, 9272, 9205, 9135, 9063, 8988, 8910, 8829, 8746, 8660, 8572,
    8480,  8387, 8290, 8192, 8090, 7986, 7880, 7771, 7660, 7547, 7431, 7314, 7193, 7071, 6947, 6820,
    6691,  6561, 6428, 6293, 6157, 6018, 5878, 5736, 5592, 5446, 5299, 5150, 5000, 4848, 4695, 4540,
    4384,  4226, 4067, 3907, 3746, 3584, 3420, 3256, 3090, 2924, 2756, 2588, 2419, 2250, 2079, 1908,
    1736,  1564, 1392, 1219, 1045, 872,  698,  523,  349,  175,  0};

#define MICRO_DEG   INT64_C(1000000) /* place.* coordinates are millionths of a degree */
#define MM_PER_UDEG INT64_C(111195)  /* a millionth of a degree of arc, in thousandths of a mm */

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
    char d[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) d[--k] = '-';
    return put(out, cap, at, d + k);
}

/* " <v>" */
static bool put_sp(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    return put(out, cap, at, " ") && put_num(out, cap, at, v);
}

/* tenths as a decimal: -21 is "-2.1" */
static bool put_tenths(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[2] = {0, 0};
    int64_t w = v < 0 ? -v : v;
    d[0] = (char) ('0' + (int) (w % 10));
    return (v >= 0 || put(out, cap, at, "-")) && put_num(out, cap, at, w / 10) &&
           put(out, cap, at, ".") && put(out, cap, at, d);
}

/* The length of s, at most max (NULL is 0). */
static uint32_t bounded_len(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (s && n < max && s[n]) n++;
    return n;
}

static bool name_ok(const char *s, uint32_t n)
{
    if (!n || n > ISOBAR_NAME_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7f) return false;
    return true;
}

static bool node_ok(const char *s, uint32_t n)
{
    if (n != ISOBAR_NODE_HEX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

/* ---- the network's tables -------------------------------------------------- */

static int32_t find_site(const isobar_t *t, int64_t id)
{
    for (uint32_t i = 0; i < t->nsite; i++)
        if (t->site[i].id == id) return (int32_t) i;
    return -1;
}

static int32_t find_station(const isobar_t *t, int64_t id)
{
    for (uint32_t i = 0; i < t->nst; i++)
        if (t->st[i].id == id) return (int32_t) i;
    return -1;
}

static int32_t find_sensor(const isobar_t *t, int64_t station, int64_t kind)
{
    for (uint32_t i = 0; i < t->nsensor; i++)
        if (t->sensor[i].station == station && t->sensor[i].kind == kind) return (int32_t) i;
    return -1;
}

static int32_t find_heater(const isobar_t *t, int64_t station)
{
    for (uint32_t i = 0; i < t->nheater; i++)
        if (t->heater[i].station == station) return (int32_t) i;
    return -1;
}

/* IM2: a node's record and its place in the tree */
static bool site_fits(const isobar_t *t, int64_t id, int64_t parent, int64_t kind, int64_t elev)
{
    if (!isobar_site_node_ok(parent, id, kind, elev) || id < 1 || id > ISOBAR_ID_MAX) return false;
    if (kind == 1) return parent == 0;
    if (kind != 2) return false;
    return find_site(t, parent) >= 0;
}

/* IM4 */
static bool sensor_fits(int64_t kind, int64_t lo, int64_t hi, int64_t step)
{
    return kind >= ISOBAR_TEMP && kind <= ISOBAR_RAIN && lo >= -ISOBAR_POINT_MAX &&
           hi <= ISOBAR_POINT_MAX && lo <= hi && hi - lo <= ISOBAR_MARGIN_MAX && step >= 0 &&
           step <= ISOBAR_STEP_MAX;
}

/* IM5: Homestead's heater record (homestead.home_device_ok) */
static bool heater_fits(int64_t station, int64_t lo, int64_t hi, int64_t setpoint)
{
    return homestead_device_ok(station, HOMESTEAD_HEATER, lo, hi, setpoint) && lo <= setpoint &&
           setpoint <= hi;
}

/* The newest observation of a sensor, any state: its index, or -1. */
static int32_t newest(const isobar_t *t, int64_t station, int64_t kind)
{
    for (uint32_t i = t->nobs; i-- > 0;)
        if (t->obs[i].station == station && t->obs[i].kind == kind) return (int32_t) i;
    return -1;
}

/* ---- the file (IM1-IM6) ---------------------------------------------------- */

/* The whole file for t; its length, or -1 when it does not fit. */
static int32_t compose(const isobar_t *t, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!put(out, cap, &at, "isobar 1\n")) return -1;
    for (uint32_t i = 0; i < t->nsite; i++) {
        const isobar_site_t *s = &t->site[i];
        if (!put(out, cap, &at, "site") || !put_sp(out, cap, &at, s->id) ||
            !put_sp(out, cap, &at, s->parent) || !put_sp(out, cap, &at, s->kind) ||
            !put_sp(out, cap, &at, s->elev) || !put(out, cap, &at, " ") ||
            !put(out, cap, &at, s->name) || !put(out, cap, &at, "\n"))
            return -1;
    }
    for (uint32_t i = 0; i < t->nst; i++) {
        const isobar_station_t *s = &t->st[i];
        if (!put(out, cap, &at, "station") || !put_sp(out, cap, &at, s->id) ||
            !put_sp(out, cap, &at, s->site) || !put(out, cap, &at, " ") ||
            !put(out, cap, &at, s->node[0] ? s->node : "-") || !put(out, cap, &at, " ") ||
            !put(out, cap, &at, s->name) || !put(out, cap, &at, "\n"))
            return -1;
    }
    for (uint32_t i = 0; i < t->nsensor; i++) {
        const isobar_sensor_t *s = &t->sensor[i];
        if (!put(out, cap, &at, "sensor") || !put_sp(out, cap, &at, s->station) ||
            !put_sp(out, cap, &at, s->kind) || !put_sp(out, cap, &at, s->lo) ||
            !put_sp(out, cap, &at, s->hi) || !put_sp(out, cap, &at, s->step) ||
            !put(out, cap, &at, "\n"))
            return -1;
    }
    for (uint32_t i = 0; i < t->nheater; i++) {
        const isobar_heater_t *h = &t->heater[i];
        if (!put(out, cap, &at, "heater") || !put_sp(out, cap, &at, h->station) ||
            !put_sp(out, cap, &at, h->lo) || !put_sp(out, cap, &at, h->hi) ||
            !put_sp(out, cap, &at, h->setpoint) || !put(out, cap, &at, "\n"))
            return -1;
    }
    for (uint32_t i = 0; i < t->nobs; i++) {
        const isobar_obs_t *o = &t->obs[i];
        if (!put(out, cap, &at, "obs") || !put_sp(out, cap, &at, o->station) ||
            !put_sp(out, cap, &at, o->stamp) || !put_sp(out, cap, &at, o->kind) ||
            !put_sp(out, cap, &at, o->reading) || !put_sp(out, cap, &at, o->state) ||
            !put(out, cap, &at, "\n"))
            return -1;
    }
    return (int32_t) at;
}

/* A plain decimal of 1 to 12 digits (IM6), a minus sign allowed before one
 * other than 0, at s[*at], ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && s[i] == '-';
    if (neg) i++;
    uint32_t first = i;
    int64_t x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 13u) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > 12u || i >= n || s[i] != end) return false;
    if (s[first] == '0' && (digits > 1u || neg)) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

/* The rest of the line from *at as a name (IM2, IM3) into out. */
static bool get_name(const char *s, uint32_t n, uint32_t *at, char *out)
{
    uint32_t i = *at;
    while (i < n && s[i] != '\n') i++;
    if (i >= n || !name_ok(s + *at, i - *at)) return false;
    memcpy(out, s + *at, i - *at);
    out[i - *at] = 0;
    *at = i + 1u;
    return true;
}

static bool parse_site(isobar_t *x, const char *s, uint32_t n, uint32_t *at)
{
    int64_t v[4];
    for (uint32_t k = 0; k < 4u; k++)
        if (!get_num(s, n, at, ' ', &v[k])) return false;
    if (!site_fits(x, v[0], v[1], v[2], v[3]) || x->nsite >= ISOBAR_SITES ||
        (x->nsite && x->site[x->nsite - 1u].id >= v[0]))
        return false;
    isobar_site_t *e = &x->site[x->nsite];
    *e = (isobar_site_t){(int32_t) v[0], (int32_t) v[1], (int32_t) v[2], v[3], {0}};
    if (!get_name(s, n, at, e->name)) return false;
    x->nsite++;
    return true;
}

static bool parse_station(isobar_t *x, const char *s, uint32_t n, uint32_t *at)
{
    int64_t id, site;
    if (!get_num(s, n, at, ' ', &id) || !get_num(s, n, at, ' ', &site) || id < 1 ||
        id > ISOBAR_ID_MAX || x->nst >= ISOBAR_STATIONS || (x->nst && x->st[x->nst - 1u].id >= id))
        return false;
    int32_t k = find_site(x, site);
    if (k < 0 || x->site[k].kind != 2) return false;
    isobar_station_t *e = &x->st[x->nst];
    memset(e, 0, sizeof *e);
    e->id = (int32_t) id;
    e->site = (int32_t) site;
    uint32_t i = *at;
    while (i < n && s[i] != ' ' && s[i] != '\n') i++;
    if (i >= n || s[i] != ' ') return false;
    if (i - *at == 1u && s[*at] == '-')
        e->node[0] = 0;
    else if (node_ok(s + *at, i - *at))
        memcpy(e->node, s + *at, ISOBAR_NODE_HEX); /* e->node ends in the memset's 0 */
    else
        return false;
    *at = i + 1u;
    if (!get_name(s, n, at, e->name)) return false;
    x->nst++;
    return true;
}

static bool parse_sensor(isobar_t *x, const char *s, uint32_t n, uint32_t *at)
{
    int64_t v[5];
    for (uint32_t k = 0; k < 5u; k++)
        if (!get_num(s, n, at, k == 4u ? '\n' : ' ', &v[k])) return false;
    if (find_station(x, v[0]) < 0 || !sensor_fits(v[1], v[2], v[3], v[4]) ||
        x->nsensor >= ISOBAR_SENSORS)
        return false;
    if (x->nsensor) {
        const isobar_sensor_t *p = &x->sensor[x->nsensor - 1u];
        if (p->station > v[0] || (p->station == v[0] && p->kind >= v[1])) return false;
    }
    x->sensor[x->nsensor++] = (isobar_sensor_t){(int32_t) v[0], (int32_t) v[1], v[2], v[3], v[4]};
    return true;
}

static bool parse_heater(isobar_t *x, const char *s, uint32_t n, uint32_t *at)
{
    int64_t v[4];
    for (uint32_t k = 0; k < 4u; k++)
        if (!get_num(s, n, at, k == 3u ? '\n' : ' ', &v[k])) return false;
    if (find_station(x, v[0]) < 0 || !heater_fits(v[0], v[1], v[2], v[3]) ||
        x->nheater >= ISOBAR_STATIONS || (x->nheater && x->heater[x->nheater - 1u].station >= v[0]))
        return false;
    x->heater[x->nheater++] = (isobar_heater_t){(int32_t) v[0], v[1], v[2], v[3]};
    return true;
}

static bool parse_obs(isobar_t *x, const char *s, uint32_t n, uint32_t *at)
{
    int64_t v[5];
    for (uint32_t k = 0; k < 5u; k++)
        if (!get_num(s, n, at, k == 4u ? '\n' : ' ', &v[k])) return false;
    if (!isobar_obs_record_ok(v[0], v[1], v[2], v[3], v[4]) || v[1] < 0 || v[4] < 0 || v[4] > 1 ||
        find_sensor(x, v[0], v[2]) < 0 || x->nobs >= ISOBAR_OBS)
        return false;
    int32_t p = newest(x, v[0], v[2]);
    if (p >= 0 && x->obs[p].stamp >= v[1]) return false;
    x->obs[x->nobs++] = (isobar_obs_t){(int32_t) v[0], (int32_t) v[2], (int32_t) v[4], v[1], v[3]};
    return true;
}

/* Parse text into x (IM1-IM6): false when it is not Isobar's file. */
static bool parse(isobar_t *x, const char *s, uint32_t n)
{
    static const char HEAD[] = "isobar 1\n";
    static const char *const WORD[5] = {"site ", "station ", "sensor ", "heater ", "obs "};
    uint32_t at = (uint32_t) strlen(HEAD), part = 0;
    if (n < at || memcmp(s, HEAD, at)) return false;
    x->nsite = x->nst = x->nsensor = x->nheater = x->nobs = 0;
    while (at < n) {
        uint32_t w = part;
        while (w < 5u && (n - at < strlen(WORD[w]) || memcmp(s + at, WORD[w], strlen(WORD[w]))))
            w++;
        if (w >= 5u) return false; /* an unknown line, or a part out of order */
        part = w;
        at += (uint32_t) strlen(WORD[w]);
        bool ok = w == 0u   ? parse_site(x, s, n, &at)
                  : w == 1u ? parse_station(x, s, n, &at)
                  : w == 2u ? parse_sensor(x, s, n, &at)
                  : w == 3u ? parse_heater(x, s, n, &at)
                            : parse_obs(x, s, n, &at);
        if (!ok) return false;
    }
    return true;
}

/* ---- zxvd ------------------------------------------------------------------ */

/* One request's text: whole lines of at most this many bytes. Names hold
 * no control byte, so each byte escapes to at most two, well inside the
 * request line. */
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
        zxm_p_str(l, "path", ISOBAR_FILE);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static void keep(isobar_t *t, const isobar_t *x)
{
    memcpy(t->site, x->site, (size_t) x->nsite * sizeof t->site[0]);
    t->nsite = x->nsite;
    memcpy(t->st, x->st, (size_t) x->nst * sizeof t->st[0]);
    t->nst = x->nst;
    memcpy(t->sensor, x->sensor, (size_t) x->nsensor * sizeof t->sensor[0]);
    t->nsensor = x->nsensor;
    memcpy(t->heater, x->heater, (size_t) x->nheater * sizeof t->heater[0]);
    t->nheater = x->nheater;
    memcpy(t->obs, x->obs, (size_t) x->nobs * sizeof t->obs[0]);
    t->nobs = x->nobs;
}

static isobar_t NEXT; /* a change before it is written (IM6) */

/* Write NEXT whole, then keep it (IM6). */
static int commit(isobar_t *t)
{
    static char text[ISOBAR_TEXT_MAX];
    if (!t->loaded) return ZXM_E_STATE;
    int32_t len = compose(&NEXT, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(t->link, text, (uint32_t) len);
    if (rc) return rc;
    keep(t, &NEXT);
    return ZXM_OK;
}

void isobar_init(isobar_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
}

int isobar_load(isobar_t *t)
{
    static char text[ISOBAR_TEXT_MAX];
    static char chunk[ISOBAR_TEXT_MAX];
    zxm_link_t *l = t->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", ISOBAR_FILE);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == ISOBAR_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            isobar_init(t, l); /* not made yet: an empty network */
            t->loaded = true;
            return ZXM_OK;
        }
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t k = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || k < 0)
            return ZXM_E_PROTOCOL;
        if (size >= ISOBAR_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[k], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= ISOBAR_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    if (!parse(&NEXT, text, len)) {
        t->loaded = false; /* IM6: never overwritten */
        return ZXM_E_PROTOCOL;
    }
    keep(t, &NEXT);
    for (uint32_t i = 0; i < t->nst; i++) t->st[i].placed = t->st[i].up = false;
    t->loaded = true;
    return ZXM_OK;
}

/* ---- the network (IM2-IM5) ------------------------------------------------- */

int isobar_add_site(isobar_t *t, int64_t id, int64_t parent, int64_t kind, int64_t elev,
                    const char *name)
{
    uint32_t ln = bounded_len(name, ISOBAR_NAME_MAX + 1u);
    if (!name_ok(name, ln) || !isobar_site_node_ok(parent, id, kind, elev) || id < 1 ||
        id > ISOBAR_ID_MAX)
        return ZXM_E_ARG;
    if (!t->loaded) return ZXM_E_STATE;
    if (!site_fits(t, id, parent, kind, elev) || find_site(t, id) >= 0) return ZXM_E_ARG;
    if (t->nsite >= ISOBAR_SITES) return ZXM_E_TOOBIG;
    keep(&NEXT, t);
    uint32_t at = 0;
    while (at < NEXT.nsite && NEXT.site[at].id < id) at++;
    /* a parent is kept above its child (IM2): only a child's id may come later */
    if (kind == 2 && find_site(t, parent) >= (int32_t) at) return ZXM_E_ARG;
    memmove(&NEXT.site[at + 1u], &NEXT.site[at], (size_t) (NEXT.nsite - at) * sizeof NEXT.site[0]);
    NEXT.site[at] = (isobar_site_t){(int32_t) id, (int32_t) parent, (int32_t) kind, elev, {0}};
    memcpy(NEXT.site[at].name, name, ln);
    NEXT.nsite++;
    return commit(t);
}

int isobar_add_station(isobar_t *t, int64_t id, int64_t site, const char *node, const char *name)
{
    uint32_t nn = bounded_len(node, ISOBAR_NODE_HEX + 1u);
    uint32_t ln = bounded_len(name, ISOBAR_NAME_MAX + 1u);
    if (id < 1 || id > ISOBAR_ID_MAX || (nn && !node_ok(node, nn)) || !name_ok(name, ln))
        return ZXM_E_ARG;
    if (!t->loaded) return ZXM_E_STATE;
    int32_t k = find_site(t, site);
    if (k < 0 || t->site[k].kind != 2 || find_station(t, id) >= 0) return ZXM_E_ARG;
    if (t->nst >= ISOBAR_STATIONS) return ZXM_E_TOOBIG;
    keep(&NEXT, t);
    uint32_t at = 0;
    while (at < NEXT.nst && NEXT.st[at].id < id) at++;
    memmove(&NEXT.st[at + 1u], &NEXT.st[at], (size_t) (NEXT.nst - at) * sizeof NEXT.st[0]);
    isobar_station_t *e = &NEXT.st[at];
    memset(e, 0, sizeof *e);
    e->id = (int32_t) id;
    e->site = (int32_t) site;
    if (nn) memcpy(e->node, node, nn);
    memcpy(e->name, name, ln);
    NEXT.nst++;
    return commit(t);
}

int isobar_set_sensor(isobar_t *t, int64_t station, int64_t kind, int64_t lo, int64_t hi,
                      int64_t step)
{
    if (!sensor_fits(kind, lo, hi, step)) return ZXM_E_ARG;
    if (!t->loaded) return ZXM_E_STATE;
    if (find_station(t, station) < 0) return ZXM_E_ARG;
    keep(&NEXT, t);
    int32_t k = find_sensor(&NEXT, station, kind);
    if (k >= 0) {
        NEXT.sensor[k].lo = lo;
        NEXT.sensor[k].hi = hi;
        NEXT.sensor[k].step = step;
        return commit(t);
    }
    if (NEXT.nsensor >= ISOBAR_SENSORS) return ZXM_E_TOOBIG;
    uint32_t at = 0;
    while (at < NEXT.nsensor &&
           (NEXT.sensor[at].station < station ||
            (NEXT.sensor[at].station == station && NEXT.sensor[at].kind < kind)))
        at++;
    memmove(&NEXT.sensor[at + 1u], &NEXT.sensor[at],
            (size_t) (NEXT.nsensor - at) * sizeof NEXT.sensor[0]);
    NEXT.sensor[at] = (isobar_sensor_t){(int32_t) station, (int32_t) kind, lo, hi, step};
    NEXT.nsensor++;
    return commit(t);
}

int isobar_set_heater(isobar_t *t, int64_t station, int64_t lo, int64_t hi, int64_t setpoint)
{
    if (!heater_fits(station, lo, hi, setpoint)) return ZXM_E_ARG;
    if (!t->loaded) return ZXM_E_STATE;
    if (find_station(t, station) < 0) return ZXM_E_ARG;
    keep(&NEXT, t);
    int32_t k = find_heater(&NEXT, station);
    if (k < 0) {
        uint32_t at = 0;
        while (at < NEXT.nheater && NEXT.heater[at].station < station) at++;
        memmove(&NEXT.heater[at + 1u], &NEXT.heater[at],
                (size_t) (NEXT.nheater - at) * sizeof NEXT.heater[0]);
        NEXT.nheater++;
        k = (int32_t) at;
    }
    NEXT.heater[k] = (isobar_heater_t){(int32_t) station, lo, hi, setpoint};
    int rc = commit(t);
    if (!rc) {
        uint32_t at = 0;
        t->notice[0] = 0;
        if (!put(t->notice, sizeof t->notice, &at, "Heater of station") ||
            !put_sp(t->notice, sizeof t->notice, &at, station) ||
            !put(t->notice, sizeof t->notice, &at,
                 " kept. It changes only in Homestead, with the owner's approval."))
            t->notice[0] = 0;
    }
    return rc;
}

/* ---- readings (IM7) -------------------------------------------------------- */

static void reading_notice(isobar_t *t, const isobar_station_t *s, int64_t kind, int64_t reading,
                           const char *what)
{
    uint32_t at = 0;
    char *o = t->notice;
    uint32_t cap = sizeof t->notice;
    o[0] = 0;
    if (!put(o, cap, &at, s->name) || !put(o, cap, &at, ": ") || !put(o, cap, &at, KIND[kind]) ||
        !put(o, cap, &at, " ") || !put_tenths(o, cap, &at, reading) ||
        !put(o, cap, &at, UNIT[kind]) || !put(o, cap, &at, what))
        o[0] = 0;
}

int isobar_take(isobar_t *t, int64_t station, int64_t kind, int64_t stamp, int64_t reading)
{
    /* check */
    if (stamp < 0 || stamp > ISOBAR_STAMP_MAX || reading < -ISOBAR_RECORD_MAX ||
        reading > ISOBAR_RECORD_MAX)
        return ZXM_E_ARG;
    if (!t->loaded) return ZXM_E_STATE;
    int32_t s = find_station(t, station), k = find_sensor(t, station, kind);
    if (s < 0 || k < 0) return ZXM_E_ARG;
    int32_t p = newest(t, station, kind);
    if (p >= 0 && t->obs[p].stamp >= stamp) return ZXM_E_ARG;
    /* reserve */
    if (t->nobs >= ISOBAR_OBS) {
        uint32_t at = 0;
        t->notice[0] = 0;
        if (!put(t->notice, sizeof t->notice, &at,
                 "The observation table is full: prune old observations to take more."))
            t->notice[0] = 0;
        return ZXM_E_TOOBIG;
    }
    /* compute */
    const isobar_sensor_t *e = &t->sensor[k];
    int64_t last = 0, when = 0, m[4];
    bool has_last = isobar_last(t, station, kind, stamp, &last, &when) == ZXM_OK;
    bool ok = isobar_margins(reading, e->lo, e->hi, last, e->step, has_last, m) &&
              isobar_reading_accept_ok(m[0], m[1], m[2], m[3]);
    /* mutate, then commit */
    keep(&NEXT, t);
    NEXT.obs[NEXT.nobs++] =
        (isobar_obs_t){(int32_t) station, (int32_t) kind, ok ? 0 : 1, stamp, reading};
    int rc = commit(t);
    if (rc) return rc;
    const isobar_station_t *st = &t->st[s];
    if (ok) {
        reading_notice(t, st, kind, reading, " accepted.");
        return ZXM_OK;
    }
    int32_t band = wellspring_band(reading, e->lo, e->hi); /* wellspring.band */
    reading_notice(t, st, kind, reading,
                   band < 0   ? " refused: below the sensor's range. Kept for the owner to see."
                   : band > 0 ? " refused: above the sensor's range. Kept for the owner to see."
                              : " refused: it moved more than the sensor's step limit since the "
                                "last reading. Kept for the owner to see.");
    return ISOBAR_REFUSED_READING;
}

int isobar_prune(isobar_t *t, int64_t before, uint32_t *dropped)
{
    if (!dropped) return ZXM_E_ARG;
    if (!t->loaded) return ZXM_E_STATE;
    keep(&NEXT, t);
    uint32_t j = 0;
    for (uint32_t i = 0; i < NEXT.nobs; i++)
        if (NEXT.obs[i].stamp >= before) NEXT.obs[j++] = NEXT.obs[i];
    uint32_t gone = NEXT.nobs - j;
    NEXT.nobs = j;
    int rc = commit(t);
    if (!rc) *dropped = gone;
    return rc;
}

int isobar_last(const isobar_t *t, int64_t station, int64_t kind, int64_t at, int64_t *reading,
                int64_t *stamp)
{
    if (!reading || !stamp) return ZXM_E_ARG;
    for (uint32_t i = t->nobs; i-- > 0;) {
        const isobar_obs_t *o = &t->obs[i];
        if (o->station == station && o->kind == kind && o->state == 0 && o->stamp <= at) {
            *reading = o->reading;
            *stamp = o->stamp;
            return ZXM_OK;
        }
    }
    return ZXM_E_STATE;
}

/* ---- the outlook (IM8) ----------------------------------------------------- */

static const char *const OUTLOOK[8] = {"",          "storm likely",      "rain likely",
                                       "unsettled", "no change",         "fair",
                                       "improving", "clearing and windy"};

int isobar_outlook(isobar_t *t, int64_t station, int64_t now, int64_t *code)
{
    int64_t p_now, at_now, p_then, at_then;
    if (!code) return ZXM_E_ARG;
    int32_t s = find_station(t, station);
    if (s < 0) return ZXM_E_ARG;
    if (isobar_last(t, station, ISOBAR_PRESSURE, now, &p_now, &at_now) ||
        isobar_last(t, station, ISOBAR_PRESSURE, now - ISOBAR_TEND_S, &p_then, &at_then) ||
        at_now < now - ISOBAR_TEND_SLACK || at_then < now - ISOBAR_TEND_S - ISOBAR_TEND_SLACK)
        return ZXM_E_STATE;
    int64_t tendency = p_now - p_then;
    int64_t c = isobar_pressure_outlook(tendency, p_now);
    if (c == ISOBAR_REFUSED) return ZXM_E_ARG;
    *code = c;
    uint32_t at = 0;
    char *o = t->notice;
    uint32_t cap = sizeof t->notice;
    o[0] = 0;
    if (!put(o, cap, &at, t->st[s].name) || !put(o, cap, &at, ": pressure ") ||
        !put_tenths(o, cap, &at, p_now) || !put(o, cap, &at, " hPa, ") ||
        !put(o, cap, &at, tendency < 0 ? "down " : "up ") ||
        !put_tenths(o, cap, &at, tendency < 0 ? -tendency : tendency) ||
        !put(o, cap, &at, " hPa in 3 hours: ") || !put(o, cap, &at, OUTLOOK[c]) ||
        !put(o, cap, &at,
             ". Advisory only: this is Isobar's own reading of its barometers, never an "
             "official forecast; official warnings always take precedence."))
        o[0] = 0;
    return ZXM_OK;
}

/* ---- the grid and the parts (IR3, IM10) ------------------------------------ */

static int64_t G_STATION[ISOBAR_OBS], G_HOUR[ISOBAR_OBS], G_READING[ISOBAR_OBS];
static int64_t G_LO[ISOBAR_OBS], G_HI[ISOBAR_OBS];

/* One sensor's accepted readings into the G_ vectors; how many. */
static uint32_t gather(const isobar_t *t, int64_t station, int64_t kind, int64_t from, int64_t to)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < t->nobs; i++) {
        const isobar_obs_t *o = &t->obs[i];
        if (o->station != station || o->kind != kind || o->state != 0 || o->stamp < from ||
            o->stamp > to)
            continue;
        G_STATION[n] = o->station;
        G_HOUR[n] = o->stamp / ISOBAR_HOUR_S;
        G_READING[n] = o->reading;
        n++;
    }
    return n;
}

int isobar_hour_high(const isobar_t *t, int64_t station, int64_t kind, int64_t hour, int64_t *high)
{
    if (!high || find_sensor(t, station, kind) < 0) return ZXM_E_ARG;
    uint32_t n = gather(t, station, kind, 0, ISOBAR_STAMP_MAX);
    for (uint32_t i = 0; i < n; i++)
        if (G_HOUR[i] == hour) {
            int64_t h = isobar_station_hour_high(G_STATION, G_HOUR, G_READING, n, i);
            if (h == ISOBAR_REFUSED) return ZXM_E_PROTOCOL;
            *high = h;
            return ZXM_OK;
        }
    return ZXM_E_STATE;
}

int isobar_rain_total(const isobar_t *t, int64_t station, int64_t from, int64_t to, int64_t *total)
{
    if (!total || from > to || find_sensor(t, station, ISOBAR_RAIN) < 0) return ZXM_E_ARG;
    uint32_t n = gather(t, station, ISOBAR_RAIN, 0, ISOBAR_STAMP_MAX);
    for (uint32_t i = 0; i < n; i++) {
        G_LO[i] = from;
        G_HI[i] = to;
    }
    /* furrow.range_total: the hours are the positions, the rain the cells */
    return furrow_range_total(G_HOUR, G_LO, G_HI, G_READING, n, total) ? ZXM_OK : ZXM_E_TOOBIG;
}

int isobar_week(const isobar_t *t, int64_t station, int64_t now, int64_t out[3])
{
    if (!out || find_sensor(t, station, ISOBAR_TEMP) < 0) return ZXM_E_ARG;
    uint32_t n = gather(t, station, ISOBAR_TEMP, now - ISOBAR_WEEK_S + 1, now);
    if (!n) return ZXM_E_STATE;
    /* weatherglass.week_temps */
    return weatherglass_week(G_READING, n, out) ? ZXM_E_ARG : ZXM_OK;
}

int isobar_warnings(isobar_t *t, int64_t station, int64_t now, int32_t *flags)
{
    int64_t temp, wind = 0, when, num, den;
    if (!flags) return ZXM_E_ARG;
    int32_t s = find_station(t, station);
    if (s < 0) return ZXM_E_ARG;
    if (isobar_last(t, station, ISOBAR_TEMP, now, &temp, &when)) return ZXM_E_STATE;
    if (isobar_last(t, station, ISOBAR_WIND, now, &wind, &when)) wind = 0; /* no wind kept */
    int32_t f = weatherglass_warnings(temp, wind);                         /* warn_flags */
    if (f == WEATHERGLASS_REFUSED || weatherglass_fahrenheit(temp, &num, &den)) return ZXM_E_ARG;
    *flags = f;
    uint32_t at = 0;
    char *o = t->notice;
    uint32_t cap = sizeof t->notice;
    o[0] = 0;
    bool ok = put(o, cap, &at, t->st[s].name) && put(o, cap, &at, ": ") &&
              put_tenths(o, cap, &at, temp) && put(o, cap, &at, " C (") &&
              put_tenths(o, cap, &at, weatherglass_floor(num, den)) && put(o, cap, &at, " F)");
    if (ok && f)
        ok = put(o, cap, &at, ". Advisory:") && (!(f & 1) || put(o, cap, &at, " frost")) &&
             (!(f & 2) || put(o, cap, &at, " heat")) && (!(f & 4) || put(o, cap, &at, " gale")) &&
             put(o, cap, &at, ". Official warnings always take precedence");
    if (!ok || !put(o, cap, &at, ".")) o[0] = 0;
    return ZXM_OK;
}

/* ---- places and microclimates (IM9) ---------------------------------------- */

int isobar_places(isobar_t *t)
{
    zxm_link_t *l = t->link;
    bool known = false;
    int64_t lat = 0, lon = 0;
    zxm_begin(l, "place.here");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "known", &known)) return ZXM_E_PROTOCOL;
    if (known && (zxm_get_i64(l, l->result, "lat", &lat) || zxm_get_i64(l, l->result, "lon", &lon)))
        return ZXM_E_PROTOCOL;
    zxm_begin(l, "place.list");
    rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "places");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    for (uint32_t k = 0; k < t->nst; k++) t->st[k].placed = false;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        char name[ISOBAR_NAME_MAX + 2];
        int64_t plat, plon;
        int32_t obj = zxm_at(l, arr, i);
        if (obj < 0 || zxm_get_str(l, obj, "name", name, sizeof name) ||
            zxm_get_i64(l, obj, "lat", &plat) || zxm_get_i64(l, obj, "lon", &plon))
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; k < t->nst; k++)
            if (!strcmp(t->st[k].name, name)) {
                t->st[k].placed = true;
                t->st[k].lat = plat;
                t->st[k].lon = plon;
            }
    }
    t->here_known = known;
    t->here_lat = lat;
    t->here_lon = lon;
    return ZXM_OK;
}

/* the floor of the square root of v (Newton's method on integers) */
static int64_t whole_root(int64_t v)
{
    if (v < 2) return v < 0 ? 0 : v;
    int64_t x = v, y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return x;
}

int isobar_apart(const isobar_t *t, int64_t a, int64_t b, int64_t *metres)
{
    int32_t i = find_station(t, a), j = find_station(t, b);
    if (!metres || i < 0 || j < 0) return ZXM_E_ARG;
    const isobar_station_t *p = &t->st[i], *q = &t->st[j];
    if (!p->placed || !q->placed) return ZXM_E_STATE;
    int64_t dlat = q->lat - p->lat, dlon = q->lon - p->lon;
    if (dlon > 180 * MICRO_DEG) dlon -= 360 * MICRO_DEG;
    if (dlon < -180 * MICRO_DEG) dlon += 360 * MICRO_DEG;
    if (dlat > MICRO_DEG || dlat < -MICRO_DEG || dlon > MICRO_DEG || dlon < -MICRO_DEG)
        return ZXM_E_ARG; /* farther than the reckoning holds */
    int64_t mid = (p->lat + q->lat) / 2;
    if (mid < 0) mid = -mid;
    int64_t deg = mid / MICRO_DEG, part = mid % MICRO_DEG;
    int64_t c = deg >= 90 ? 0 : COS[deg] + (COS[deg + 1] - COS[deg]) * part / MICRO_DEG;
    int64_t dy = dlat * MM_PER_UDEG / 1000;             /* mm */
    int64_t dx = dlon * MM_PER_UDEG / 1000 * c / 10000; /* mm */
    *metres = whole_root(dx * dx + dy * dy) / 1000;     /* each at most 1.2e8 mm */
    return ZXM_OK;
}

int isobar_microclimate(isobar_t *t, int64_t a, int64_t b, int64_t now, isobar_micro_t *out)
{
    int64_t ra, rb, wa, wb, apart;
    int32_t i = find_station(t, a), j = find_station(t, b);
    if (!out || i < 0 || j < 0 || a == b || t->st[i].site != t->st[j].site) return ZXM_E_ARG;
    int rc = isobar_apart(t, a, b, &apart);
    if (rc) return rc;
    if (apart > ISOBAR_NEAR_M) return ZXM_E_ARG;
    if (isobar_last(t, a, ISOBAR_TEMP, now, &ra, &wa) ||
        isobar_last(t, b, ISOBAR_TEMP, now, &rb, &wb))
        return ZXM_E_STATE;
    /* each station is a source about one subject, "the site is at frost
     * risk" (its accepted reading at or below the line, or above it); the
     * sources are joined with the knowledge join (lpres.h) */
    lpres_state_t risk =
        lpres_kjoin(ra <= ISOBAR_FROST_RISK ? LPRES_STATE_TRUE : LPRES_STATE_FALSE,
                    rb <= ISOBAR_FROST_RISK ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    /* a second subject that must hold, each reading fresh (an hour old at
     * most): one that is not says nothing about now (NEITHER), and the
     * conjoin of the two subjects is then NEITHER, never TRUE */
    lpres_state_t fresh_a = wa >= now - ISOBAR_HOUR_S ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
    lpres_state_t fresh_b = wb >= now - ISOBAR_HOUR_S ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
    out->state = lpres_conjoin(risk, lpres_conjoin(fresh_a, fresh_b));
    out->a_reading = ra;
    out->b_reading = rb;
    out->apart_m = apart;
    uint32_t at = 0;
    char *o = t->notice;
    uint32_t cap = sizeof t->notice;
    const char *head = out->state == LPRES_STATE_BOTH    ? "Microclimate at "
                       : out->state == LPRES_STATE_TRUE  ? "Frost risk at "
                       : out->state == LPRES_STATE_FALSE ? "No frost risk at "
                                                         : "No fresh reading at ";
    int32_t k = find_site(t, t->st[i].site);
    o[0] = 0;
    if (!put(o, cap, &at, head) || !put(o, cap, &at, k >= 0 ? t->site[k].name : "") ||
        !put(o, cap, &at, ": ") || !put(o, cap, &at, t->st[i].name) ||
        !put(o, cap, &at, " reads ") || !put_tenths(o, cap, &at, ra) ||
        !put(o, cap, &at, " C and ") || !put(o, cap, &at, t->st[j].name) ||
        !put(o, cap, &at, " ") || !put_tenths(o, cap, &at, rb) || !put(o, cap, &at, " C,") ||
        !put_sp(o, cap, &at, apart) ||
        !put(o, cap, &at, " m apart. Both readings are kept as they are, never averaged."))
        o[0] = 0;
    return ZXM_OK;
}

/* ---- station nodes --------------------------------------------------------- */

int isobar_nodes(isobar_t *t, int64_t *active, int64_t *revoked)
{
    static int64_t state[ISOBAR_STATIONS], want[ISOBAR_STATIONS];
    static bool up[ISOBAR_STATIONS];
    zxm_link_t *l = t->link;
    if (!active || !revoked) return ZXM_E_ARG;
    zxm_begin(l, "devmesh.devices");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "devices");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), hosts = 0;
    memset(up, 0, sizeof up);
    for (uint32_t i = 0; i < n; i++) {
        char id[ISOBAR_NODE_HEX + 2], status[16];
        bool is_up = false;
        int32_t obj = zxm_at(l, arr, i);
        if (obj < 0 || zxm_get_str(l, obj, "id", id, sizeof id) ||
            zxm_get_str(l, obj, "status", status, sizeof status) ||
            zxm_get_bool(l, obj, "up", &is_up))
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; id[k]; k++)
            if (id[k] >= 'A' && id[k] <= 'F') id[k] = (char) (id[k] - 'A' + 'a');
        bool on = !strcmp(status, "active"), off = !strcmp(status, "revoked");
        if (!on && !off) return ZXM_E_PROTOCOL;
        bool hosts_one = false;
        for (uint32_t k = 0; k < t->nst; k++)
            if (t->st[k].node[0] && !strcmp(t->st[k].node, id)) {
                hosts_one = true;
                up[k] = on && is_up;
            }
        if (hosts_one && hosts < ISOBAR_STATIONS) {
            state[hosts] = on ? TRELLIS_ACTIVE : TRELLIS_REVOKED;
            hosts++;
        }
    }
    zxm_begin(l, "net.peers");
    rc = zxm_call(l);
    if (rc) return rc;
    int32_t peers = zxm_get(l, l->result, "peers");
    if (peers < 0 || l->tok[peers].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    /* trellis.peers_in_state: the hosting devices active, then revoked */
    int64_t a = 0, r = 0;
    if (hosts) {
        for (uint32_t i = 0; i < hosts; i++) want[i] = TRELLIS_ACTIVE;
        a = trellis_peers_in_state(state, want, hosts);
        for (uint32_t i = 0; i < hosts; i++) want[i] = TRELLIS_REVOKED;
        r = trellis_peers_in_state(state, want, hosts);
        if (a == TRELLIS_REFUSED || r == TRELLIS_REFUSED) return ZXM_E_PROTOCOL;
    }
    for (uint32_t k = 0; k < t->nst; k++) t->st[k].up = up[k];
    t->peers = zxm_len(l, peers);
    *active = a;
    *revoked = r;
    return ZXM_OK;
}
