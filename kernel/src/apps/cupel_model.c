/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cupel_model.c — Cupel's model (cupel_model.h). */
#include <string.h>

#include "cupel_model.h"

#define CUPEL_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

static void run_reset(cupel_t *c)
{
    c->taken = 0;
    c->e0 = 0;
    c->energy = 0;
    c->halt = CUPEL_GOING;
}

void cupel_init(cupel_t *c, zxm_link_t *link)
{
    memset(c, 0, sizeof *c);
    c->link = link;
    c->model = 1;
    memcpy(c->name, "model", 6);
    c->scale = 1000;
    c->dt = 1;
    c->declared = 1000;
    c->bound = 1000;
    c->seconds = 60;
    run_reset(c);
}

/* a line of text: 1 .. max bytes, no control byte (a tab or a newline
 * would break the file's lines) */
static bool text_ok(const char *s, uint32_t max)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 1u || n > max) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7f) return false;
    return true;
}

/* ---- CM2: the model ---- */

int cupel_set_model(cupel_t *c, int64_t model, const char *name)
{
    if (!cupel_in(model, 0, CUPEL_MODEL_MAX) || !text_ok(name, CUPEL_NAME_MAX)) return ZXM_E_ARG;
    c->model = model;
    memcpy(c->name, name, strlen(name) + 1u);
    c->dirty = true;
    return ZXM_OK;
}

int cupel_set_bounds(cupel_t *c, int64_t scale, int64_t dt, int64_t declared, int64_t bound,
                     int64_t seconds)
{
    if (!cupel_in(dt, 1, CUPEL_DT_MAX) || declared < 1 ||
        !cupel_precision_ok(scale, declared, bound) || !retort_run_limits_ok(1, 1, seconds))
        return ZXM_E_ARG;
    c->scale = scale;
    c->dt = dt;
    c->declared = declared;
    c->bound = bound;
    c->seconds = seconds;
    run_reset(c);
    c->dirty = true;
    return ZXM_OK;
}

static bool atom_ok(const cupel_t *c, uint32_t n, int64_t molecule, int64_t element,
                    const int64_t x[3], const int64_t v[3])
{
    if (!x || !v || !cupel_in(molecule, 0, CUPEL_MOLECULE) ||
        !cupel_in(element, 1, CUPEL_ELEMENT_MAX))
        return false;
    for (uint32_t a = 0; a < 3u; a++)
        if (!cupel_in(x[a], -CUPEL_COORD_MAX, CUPEL_COORD_MAX) ||
            !cupel_in(v[a], -CUPEL_VEL_MAX, CUPEL_VEL_MAX))
            return false;
    return cupel_tree_node_ok(c->model, molecule, n, element, x[0], x[1], x[2]);
}

int cupel_add_atom(cupel_t *c, int64_t molecule, int64_t element, const int64_t x[3],
                   const int64_t v[3], uint32_t *n)
{
    if (!atom_ok(c, c->natom, molecule, element, x, v)) return ZXM_E_ARG;
    if (c->natom >= CUPEL_ATOMS) return ZXM_E_TOOBIG;
    cupel_atom_t *a = &c->atom[c->natom];
    a->molecule = molecule;
    a->element = element;
    memcpy(a->x, x, sizeof a->x);
    memcpy(a->v, v, sizeof a->v);
    if (n) *n = c->natom;
    c->natom++;
    c->side = 0;
    c->spacing = 0;
    run_reset(c);
    c->dirty = true;
    return ZXM_OK;
}

static bool pair_ok(const cupel_t *c, uint32_t p, uint32_t q, int64_t k, const int64_t rest[3])
{
    if (!rest || p >= c->natom || q >= c->natom || p == q || !cupel_in(k, 1, CUPEL_K_MAX))
        return false;
    for (uint32_t a = 0; a < 3u; a++)
        if (!cupel_in(rest[a], -CUPEL_COORD_MAX, CUPEL_COORD_MAX)) return false;
    return true;
}

int cupel_add_pair(cupel_t *c, uint32_t p, uint32_t q, int64_t k, const int64_t rest[3])
{
    if (!pair_ok(c, p, q, k, rest)) return ZXM_E_ARG;
    if (c->npair >= CUPEL_PAIRS) return ZXM_E_TOOBIG;
    cupel_pair_t *s = &c->pair[c->npair++];
    s->p = p;
    s->q = q;
    s->k = k;
    memcpy(s->rest, rest, sizeof s->rest);
    run_reset(c);
    c->dirty = true;
    return ZXM_OK;
}

/* ---- CM3: a lattice ---- */

int cupel_lattice(cupel_t *c, int64_t side, int64_t spacing, int64_t element, int64_t k)
{
    if (!cupel_in(side, 1, CUPEL_SIDE_MAX) || !cupel_in(spacing, 1, CUPEL_COORD_MAX) ||
        spacing * (side - 1) > CUPEL_COORD_MAX || !cupel_in(element, 1, CUPEL_ELEMENT_MAX) ||
        !cupel_in(k, 1, CUPEL_K_MAX) || !cupel_tree_node_ok(c->model, 1, 0, element, 0, 0, 0))
        return ZXM_E_ARG;
    if (3 * side * side * (side - 1) > (int64_t) CUPEL_PAIRS) return ZXM_E_TOOBIG;
    c->natom = (uint32_t) (side * side * side);
    c->npair = 0;
    for (int64_t ix = 0; ix < side; ix++)
        for (int64_t iy = 0; iy < side; iy++)
            for (int64_t iz = 0; iz < side; iz++) {
                cupel_atom_t *a = &c->atom[cupel_lattice_site(ix, iy, iz, side)];
                a->molecule = 1;
                a->element = element;
                a->x[0] = ix * spacing;
                a->x[1] = iy * spacing;
                a->x[2] = iz * spacing;
                a->v[0] = a->v[1] = a->v[2] = 0;
            }
    for (int64_t ix = 0; ix < side; ix++)
        for (int64_t iy = 0; iy < side; iy++)
            for (int64_t iz = 0; iz < side; iz++) {
                const int64_t at[3] = {ix, iy, iz};
                uint32_t p = (uint32_t) cupel_lattice_site(ix, iy, iz, side);
                for (uint32_t ax = 0; ax < 3u; ax++) {
                    if (at[ax] + 1 >= side) continue; /* open faces: no spring wraps round */
                    int64_t to[3] = {ix, iy, iz};
                    to[ax]++;
                    cupel_pair_t *s = &c->pair[c->npair++];
                    s->p = p;
                    s->q = (uint32_t) cupel_lattice_site(to[0], to[1], to[2], side);
                    s->k = k;
                    s->rest[0] = s->rest[1] = s->rest[2] = 0;
                    s->rest[ax] = spacing;
                }
            }
    c->side = side;
    c->spacing = spacing;
    run_reset(c);
    c->dirty = true;
    return ZXM_OK;
}

int cupel_face_area(const cupel_t *c, int64_t *area)
{
    if (!area) return ZXM_E_ARG;
    if (!c->side) return ZXM_E_STATE;
    int64_t l = c->spacing * (c->side - 1);
    return gnomon_triangle_area2(0, 0, l, 0, 0, l, area) ? ZXM_OK : ZXM_E_ARG;
}

/* ---- CM5: energy ---- */

static void stretch_of(const cupel_t *c, const cupel_pair_t *s, int64_t out[3])
{
    for (uint32_t a = 0; a < 3u; a++) out[a] = c->atom[s->q].x[a] - c->atom[s->p].x[a] - s->rest[a];
}

int cupel_energy(const cupel_t *c, int64_t lo, int64_t hi, int64_t *e)
{
    static int64_t pos[CUPEL_ATOMS + CUPEL_PAIRS], los[CUPEL_ATOMS + CUPEL_PAIRS],
        his[CUPEL_ATOMS + CUPEL_PAIRS], cell[CUPEL_ATOMS + CUPEL_PAIRS];
    uint32_t n = 0;
    int64_t total = 0, s[3];
    if (!e) return ZXM_E_ARG;
    for (uint32_t i = 0; i < c->natom; i++, n++) {
        if (!cupel_kinetic2(c->atom[i].v, &cell[n])) return ZXM_E_ARG;
        pos[n] = c->atom[i].molecule;
    }
    for (uint32_t i = 0; i < c->npair; i++, n++) {
        stretch_of(c, &c->pair[i], s);
        if (!cupel_spring_energy2(c->pair[i].k, s, c->scale, &cell[n])) return ZXM_E_ARG;
        pos[n] = c->atom[c->pair[i].p].molecule;
    }
    for (uint32_t i = 0; i < n; i++) los[i] = lo, his[i] = hi;
    if (!furrow_range_total(pos, los, his, cell, n, &total)) return ZXM_E_ARG;
    *e = total / 2; /* every term is 0 or more */
    return ZXM_OK;
}

/* ---- CM4: the step ---- */

int cupel_begin(cupel_t *c)
{
    int64_t e = 0;
    if (!c->natom) return ZXM_E_STATE;
    if (!cupel_precision_ok(c->scale, c->declared, c->bound)) return ZXM_E_ARG;
    int rc = cupel_energy(c, 0, CUPEL_MOLECULE, &e);
    if (rc) return rc;
    run_reset(c);
    c->e0 = e;
    c->energy = e;
    return ZXM_OK;
}

int cupel_step(cupel_t *c)
{
    static int64_t net[CUPEL_ATOMS][3], nx[CUPEL_ATOMS][3], nv[CUPEL_ATOMS][3];
    int64_t s[3], f = 0, gap[2], e = 0;
    if (c->halt != CUPEL_GOING || !c->natom) return ZXM_E_STATE;
    if (c->taken >= c->declared) { /* CR4: never past the declared steps */
        c->halt = CUPEL_HALT_STEPS;
        return ZXM_E_STATE;
    }
    /* check and compute: the forces from the pair table (CR8, CR1) */
    memset(net, 0, c->natom * sizeof net[0]);
    for (uint32_t i = 0; i < c->npair; i++) {
        const cupel_pair_t *p = &c->pair[i];
        stretch_of(c, p, s);
        for (uint32_t a = 0; a < 3u; a++) {
            if (!cupel_spring_force(p->k, s[a], c->scale, &f) ||
                !cupel_in(f, -CUPEL_FORCE_MAX, CUPEL_FORCE_MAX)) {
                c->halt = CUPEL_HALT_RANGE;
                return ZXM_E_ARG;
            }
            net[p->p][a] += f; /* at most 1020 * 10^6 */
            net[p->q][a] -= f;
        }
    }
    /* the kick and the drift (CR2, CR3) */
    for (uint32_t i = 0; i < c->natom; i++)
        for (uint32_t a = 0; a < 3u; a++) {
            const cupel_atom_t *t = &c->atom[i];
            nv[i][a] = cupel_leapfrog_velocity(t->v[a], net[i][a], c->dt);
            nx[i][a] = cupel_leapfrog_position(t->x[a], t->v[a], net[i][a], c->dt);
            if (!cupel_in(nv[i][a], -CUPEL_VEL_MAX, CUPEL_VEL_MAX) ||
                !cupel_in(nx[i][a], -CUPEL_COORD_MAX, CUPEL_COORD_MAX)) {
                c->halt = CUPEL_HALT_RANGE; /* refused, or past what the next step takes */
                return ZXM_E_ARG;
            }
        }
    /* mutate */
    for (uint32_t i = 0; i < c->natom; i++) {
        memcpy(c->atom[i].v, nv[i], sizeof nv[i]);
        memcpy(c->atom[i].x, nx[i], sizeof nx[i]);
    }
    c->taken++;
    /* the bounds after it (CR4): the step is kept, to be seen */
    if (cupel_energy(c, 0, CUPEL_MOLECULE, &e)) {
        c->halt = CUPEL_HALT_RANGE;
        return ZXM_OK;
    }
    c->energy = e;
    cupel_precision_gaps(c->declared, c->taken, c->bound, c->e0, e, gap);
    if (!cupel_precision_ok(c->scale, gap[0], gap[1])) c->halt = CUPEL_HALT_DRIFT;
    return ZXM_OK;
}

/* ---- CM6: the machine and a run ---- */

int cupel_machine(cupel_t *c)
{
    zxm_link_t *l = c->link;
    cupel_machine_t m;
    memset(&m, 0, sizeof m);
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "arch", m.arch, sizeof m.arch) ||
        zxm_get_u64(l, l->result, "cpus", &m.cpus) ||
        zxm_get_u64(l, l->result, "mem_total", &m.mem_total) ||
        zxm_get_u64(l, l->result, "mem_avail", &m.mem_avail) ||
        zxm_get_u64(l, l->result, "uptime_s", &m.uptime_s))
        return ZXM_E_PROTOCOL;
    m.known = true;
    c->machine = m;
    return ZXM_OK;
}

int cupel_run(cupel_t *c, int64_t steps, const char *note, cupel_run_t *out)
{
    if (!out || !text_ok(note, CUPEL_NOTE_MAX) || !cupel_in(steps, 1, CUPEL_STEPS_MAX))
        return ZXM_E_ARG;
    if (!c->natom) return ZXM_E_STATE;
    int64_t work = steps * (int64_t) (c->natom + c->npair);
    if (!retort_run_limits_ok((int64_t) strlen(note), work, c->seconds)) return ZXM_E_ARG;
    int rc = cupel_begin(c);
    if (rc) return rc;
    rc = cupel_machine(c);
    if (rc) return rc;
    uint64_t up0 = c->machine.uptime_s;
    while (c->taken < steps && c->halt == CUPEL_GOING) (void) cupel_step(c);
    rc = cupel_machine(c);
    if (rc) return rc;
    uint64_t up1 = c->machine.uptime_s;
    int64_t drift = c->energy >= c->e0 ? c->energy - c->e0 : c->e0 - c->energy;
    memset(out, 0, sizeof *out);
    out->run = c->nrun;
    out->model = c->model;
    out->steps = c->taken;
    out->energy = c->energy;
    out->drift = drift;
    out->halted = c->halt != CUPEL_GOING;
    out->score = cupel_score(drift, c->bound, out->halted);
    out->ms = up1 > up0 && up1 - up0 < UINT64_C(9000000000) ? (int64_t) (up1 - up0) * 1000 : 0;
    out->work = c->taken * (int64_t) (c->natom + c->npair);
    memcpy(out->note, note, strlen(note) + 1u);
    return ZXM_OK;
}

/* ---- text out ---- */

typedef struct {
    char *s;
    uint32_t n, cap;
    bool over;
} out_t;

static void put_ch(out_t *o, char ch)
{
    if (o->n + 1u >= o->cap) {
        o->over = true;
        return;
    }
    o->s[o->n++] = ch;
}

static void put_str(out_t *o, const char *s)
{
    while (*s) put_ch(o, *s++);
}

static void put_int(out_t *o, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    if (v < 0) put_ch(o, '-');
    do {
        t[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u && k < sizeof t);
    while (k) put_ch(o, t[--k]);
}

static void put_ints(out_t *o, const int64_t *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        put_ch(o, ' ');
        put_int(o, v[i]);
    }
}

static char TEXT[CUPEL_FILE_MAX];

/* CM1: the model as text; its length, or -1 when it does not fit */
static int32_t compose_model(const cupel_t *c)
{
    out_t o = {TEXT, 0, sizeof TEXT, false};
    const int64_t head[8] = {c->model,           c->scale,          c->dt,
                             c->declared,        c->bound,          c->seconds,
                             (int64_t) c->natom, (int64_t) c->npair};
    put_str(&o, "cupel 1");
    put_ints(&o, head, 8);
    put_str(&o, "\nn ");
    put_str(&o, c->name);
    put_ch(&o, '\n');
    for (uint32_t i = 0; i < c->natom; i++) {
        const cupel_atom_t *a = &c->atom[i];
        const int64_t v[8] = {a->molecule, a->element, a->x[0], a->x[1],
                              a->x[2],     a->v[0],    a->v[1], a->v[2]};
        put_ch(&o, 'a');
        put_ints(&o, v, 8);
        put_ch(&o, '\n');
    }
    for (uint32_t i = 0; i < c->npair; i++) {
        const cupel_pair_t *s = &c->pair[i];
        const int64_t v[6] = {s->p, s->q, s->k, s->rest[0], s->rest[1], s->rest[2]};
        put_ch(&o, 'p');
        put_ints(&o, v, 6);
        put_ch(&o, '\n');
    }
    return o.over ? -1 : (int32_t) o.n;
}

/* CM7: the runs as text, with one more when r is given */
static int32_t compose_runs(const cupel_t *c, const cupel_run_t *r)
{
    out_t o = {TEXT, 0, sizeof TEXT, false};
    put_str(&o, "cupel-runs 1\n");
    for (uint32_t i = 0; i <= c->nrun; i++) {
        const cupel_run_t *x = i < c->nrun ? &c->run[i] : r;
        if (!x) break;
        const int64_t v[9] = {x->run,   x->model, x->steps, x->energy, x->drift, x->halted ? 1 : 0,
                              x->score, x->ms,    x->work};
        put_ch(&o, 'r');
        put_ints(&o, v, 9);
        put_ch(&o, ' ');
        put_str(&o, x->note);
        put_ch(&o, '\n');
    }
    return o.over ? -1 : (int32_t) o.n;
}

/* ---- zxvd ---- */

/* One request's text: whole lines of at most this many bytes; each byte
 * escapes to at most two ("\n"), well inside the request line. */
#define PIECE 6000u

static int write_text(zxm_link_t *l, const char *space, const char *path, const char *text,
                      uint32_t len)
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
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

/* The whole file into TEXT; *missing when zxvd says there is none. */
static int read_text(zxm_link_t *l, const char *space, const char *path, uint32_t *len,
                     bool *missing)
{
    static char chunk[CUPEL_FILE_MAX];
    uint64_t off = 0;
    *len = 0;
    *missing = false;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == CUPEL_E_FILES && !off &&
            strstr(l->err, "no such file"))
            *missing = true;
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size >= CUPEL_FILE_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || *len + (uint32_t) n >= CUPEL_FILE_MAX) return ZXM_E_TOOBIG;
        memcpy(TEXT + *len, chunk, (size_t) n);
        *len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    return ZXM_OK;
}

static bool name_ok(const char *space, const char *path)
{
    return space && path && path[0] && strlen(space) <= ZXM_SPACE_MAX &&
           strlen(path) <= CUPEL_PATH_MAX;
}

/* ---- text in ---- */

typedef struct {
    const char *s;
    uint32_t n, at;
} in_t;

/* The next integer on the line, after one space: at most 18 digits. */
static bool get_int(in_t *in, int64_t *v)
{
    if (in->at >= in->n || in->s[in->at] != ' ') return false;
    in->at++;
    bool neg = in->at < in->n && in->s[in->at] == '-';
    if (neg) in->at++;
    uint32_t st = in->at;
    int64_t u = 0;
    while (in->at < in->n && in->s[in->at] >= '0' && in->s[in->at] <= '9') {
        if (in->at - st >= 18u) return false;
        u = u * 10 + (in->s[in->at] - '0');
        in->at++;
    }
    if (in->at == st) return false;
    *v = neg ? -u : u;
    return true;
}

static bool get_ints(in_t *in, int64_t *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!get_int(in, &v[i])) return false;
    return true;
}

static bool get_word(in_t *in, const char *w)
{
    uint32_t k = (uint32_t) strlen(w);
    if (in->n - in->at < k || memcmp(in->s + in->at, w, k)) return false;
    in->at += k;
    return true;
}

static bool line_end(in_t *in)
{
    if (in->at >= in->n || in->s[in->at] != '\n') return false;
    in->at++;
    return true;
}

/* the rest of the line after one space, 1 .. max bytes, and its end */
static bool get_rest(in_t *in, char *out, uint32_t max)
{
    if (in->at >= in->n || in->s[in->at] != ' ') return false;
    in->at++;
    uint32_t k = 0;
    while (in->at < in->n && in->s[in->at] != '\n') {
        if (k >= max) return false;
        out[k++] = in->s[in->at++];
    }
    out[k] = 0;
    return text_ok(out, max) && line_end(in);
}

static cupel_t NEXT; /* a model as it is read, before it is kept */

static bool parse_model(cupel_t *d, const char *s, uint32_t n)
{
    in_t in = {s, n, 0};
    int64_t h[8], v[8];
    char name[CUPEL_NAME_MAX + 1];
    if (!get_word(&in, "cupel 1") || !get_ints(&in, h, 8) || !line_end(&in) ||
        !get_word(&in, "n") || !get_rest(&in, name, CUPEL_NAME_MAX) ||
        cupel_set_model(d, h[0], name) || cupel_set_bounds(d, h[1], h[2], h[3], h[4], h[5]) ||
        !cupel_in(h[6], 0, CUPEL_ATOMS) || !cupel_in(h[7], 0, CUPEL_PAIRS))
        return false;
    for (int64_t i = 0; i < h[6]; i++)
        if (!get_word(&in, "a") || !get_ints(&in, v, 8) || !line_end(&in) ||
            cupel_add_atom(d, v[0], v[1], &v[2], &v[5], NULL))
            return false;
    for (int64_t i = 0; i < h[7]; i++)
        if (!get_word(&in, "p") || !get_ints(&in, v, 6) || !line_end(&in) ||
            !cupel_in(v[0], 0, CUPEL_ATOMS) || !cupel_in(v[1], 0, CUPEL_ATOMS) ||
            cupel_add_pair(d, (uint32_t) v[0], (uint32_t) v[1], v[2], &v[3]))
            return false;
    return in.at == in.n;
}

/* ---- CM1 ---- */

int cupel_save(cupel_t *c, const char *space, const char *path, bool owner_confirmed)
{
    if (!name_ok(space, path)) return ZXM_E_ARG;
    if (!c->natom) return ZXM_E_STATE;
    for (uint32_t i = 0; i < c->natom; i++) {
        const cupel_atom_t *a = &c->atom[i];
        if (!cupel_tree_node_ok(c->model, a->molecule, i, a->element, a->x[0], a->x[1], a->x[2]))
            return ZXM_E_ARG;
    }
    if (!owner_confirmed) return ZXM_E_STATE;
    int32_t len = compose_model(c);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(c->link, space, path, TEXT, (uint32_t) len);
    if (rc) return rc;
    if (c->space != space) memcpy(c->space, space, strlen(space) + 1u);
    if (c->path != path) memcpy(c->path, path, strlen(path) + 1u);
    c->dirty = false;
    return ZXM_OK;
}

int cupel_open(cupel_t *c, const char *space, const char *path)
{
    uint32_t len = 0;
    bool missing = false;
    if (!name_ok(space, path)) return ZXM_E_ARG;
    int rc = read_text(c->link, space, path, &len, &missing);
    if (rc) return rc;
    cupel_init(&NEXT, c->link);
    if (!parse_model(&NEXT, TEXT, len)) return ZXM_E_ARG; /* CM1: the open model is kept */
    c->model = NEXT.model;
    memcpy(c->name, NEXT.name, sizeof c->name);
    c->scale = NEXT.scale;
    c->dt = NEXT.dt;
    c->declared = NEXT.declared;
    c->bound = NEXT.bound;
    c->seconds = NEXT.seconds;
    memcpy(c->atom, NEXT.atom, NEXT.natom * sizeof NEXT.atom[0]);
    c->natom = NEXT.natom;
    memcpy(c->pair, NEXT.pair, NEXT.npair * sizeof NEXT.pair[0]);
    c->npair = NEXT.npair;
    c->side = 0;
    c->spacing = 0;
    run_reset(c);
    if (c->space != space) memcpy(c->space, space, strlen(space) + 1u);
    if (c->path != path) memcpy(c->path, path, strlen(path) + 1u);
    c->dirty = false;
    return ZXM_OK;
}

/* ---- CM7: the runs ---- */

static bool run_fits(const cupel_run_t *r)
{
    return text_ok(r->note, CUPEL_NOTE_MAX) && cupel_in(r->score, 0, 100) &&
           cupel_run_node_ok(r->run, r->model, r->steps, r->energy, r->drift, r->halted ? 1 : 0) &&
           retort_run_record_fits(r->run, r->model, CUPEL_RETORT_KIND, (int64_t) strlen(r->note),
                                  r->work, r->ms, r->score, 0);
}

int cupel_load_runs(cupel_t *c)
{
    static cupel_run_t got[CUPEL_RUNS];
    uint32_t len = 0, n = 0;
    bool missing = false;
    int rc = read_text(c->link, "", CUPEL_RUNS_FILE, &len, &missing);
    if (rc && missing) { /* none recorded yet */
        c->nrun = 0;
        c->runs_loaded = true;
        return ZXM_OK;
    }
    if (rc) return rc;
    in_t in = {TEXT, len, 0};
    if (!get_word(&in, "cupel-runs 1") || !line_end(&in)) return ZXM_E_ARG;
    while (in.at < in.n) {
        int64_t v[9];
        if (n >= CUPEL_RUNS || !get_word(&in, "r") || !get_ints(&in, v, 9) || v[5] < 0 ||
            v[5] > 1 || !get_rest(&in, got[n].note, CUPEL_NOTE_MAX))
            return ZXM_E_ARG;
        got[n].run = v[0];
        got[n].model = v[1];
        got[n].steps = v[2];
        got[n].energy = v[3];
        got[n].drift = v[4];
        got[n].halted = v[5] == 1;
        got[n].score = v[6];
        got[n].ms = v[7];
        got[n].work = v[8];
        if (got[n].run != (int64_t) n || !run_fits(&got[n])) return ZXM_E_ARG;
        n++;
    }
    memcpy(c->run, got, n * sizeof got[0]);
    c->nrun = n;
    c->runs_loaded = true;
    return ZXM_OK;
}

int cupel_record(cupel_t *c, const cupel_run_t *r, bool owner_confirmed)
{
    if (!r) return ZXM_E_ARG;
    if (!c->runs_loaded) return ZXM_E_STATE;
    if (r->run != (int64_t) c->nrun || !run_fits(r)) return ZXM_E_ARG;
    if (c->nrun >= CUPEL_RUNS) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int32_t len = compose_runs(c, r);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(c->link, "", CUPEL_RUNS_FILE, TEXT, (uint32_t) len);
    if (rc) return rc;
    c->run[c->nrun++] = *r;
    return ZXM_OK;
}

int64_t cupel_model_total(const cupel_t *c, int64_t model)
{
    static int64_t m[CUPEL_RUNS], s[CUPEL_RUNS];
    for (uint32_t i = 0; i < c->nrun; i++) m[i] = c->run[i].model, s[i] = c->run[i].score;
    return retort_model_score_total(m, model, s, c->nrun);
}

/* ---- CM8 and the view ---- */

lpres_state_t cupel_compare(int64_t a, int64_t b, int64_t tol, int64_t shown[2])
{
    return cupel_energy_state(a, b, tol, shown);
}

int cupel_screen(const cupel_t *c, uint32_t atom, int64_t cos, int64_t sin, int64_t scale,
                 int64_t focal, int64_t *sx, int64_t *sy)
{
    if (!sx || !sy || atom >= c->natom) return ZXM_E_ARG;
    const int64_t *x = c->atom[atom].x;
    if (!relief_screen_x(x[0], x[2], cos, sin, scale, focal, sx) ||
        !relief_screen_y(x[0], x[1], x[2], cos, sin, scale, focal, sy))
        return ZXM_E_ARG;
    return ZXM_OK;
}

bool cupel_drawable(const cupel_t *c)
{
    return relief_mesh_limits_ok(c->natom, c->npair);
}
