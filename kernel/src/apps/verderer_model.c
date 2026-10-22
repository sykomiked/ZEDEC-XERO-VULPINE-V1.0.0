/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* verderer_model.c — Verderer (see verderer_model.h). */
#include <string.h>

#include "verderer_model.h"

#define VERDERER_LINE_MAX 320u /* one line of the file */
#define VERDERER_FIELDS   17u  /* an "s" line's numbers */

void verderer_init(verderer_t *v, zxm_link_t *link)
{
    memset(v, 0, sizeof *v);
    v->link = link;
}

/* ---- text, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < cap && s[n]) n++;
    return n;
}

/* n bytes of well-formed UTF-8 with no control byte */
static bool text_ok(const char *s, uint32_t n)
{
    const unsigned char *p = (const unsigned char *) s;
    uint32_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        uint32_t more;
        if (c < 0x20u || c == 0x7fu) return false;
        if (c < 0x80u) {
            i++;
            continue;
        }
        if (c >= 0xc2u && c <= 0xdfu)
            more = 1;
        else if (c >= 0xe0u && c <= 0xefu)
            more = 2;
        else if (c >= 0xf0u && c <= 0xf4u)
            more = 3;
        else
            return false;
        if (more > n - 1u - i) return false;
        for (uint32_t k = 1; k <= more; k++)
            if ((p[i + k] & 0xc0u) != 0x80u) return false;
        if (c == 0xe0u && p[i + 1] < 0xa0u) return false;
        if (c == 0xedu && p[i + 1] > 0x9fu) return false;
        if (c == 0xf0u && p[i + 1] < 0x90u) return false;
        if (c == 0xf4u && p[i + 1] > 0x8fu) return false;
        i += more + 1u;
    }
    return true;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n >= cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        t[k++] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) t[k++] = '-';
    if (*at + k >= cap) return false;
    while (k) out[(*at)++] = t[--k];
    out[*at] = 0;
    return true;
}

/* a degree in millionths, as "d.dddddd" */
static bool put_deg(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    char frac[8];
    uint64_t f = u % 1000000u;
    for (int i = 5; i >= 0; i--) {
        frac[i] = (char) ('0' + (int) (f % 10u));
        f /= 10u;
    }
    frac[6] = 0;
    return (v >= 0 || put(out, cap, at, "-")) && put_i64(out, cap, at, (int64_t) (u / 1000000u)) &&
           put(out, cap, at, ".") && put(out, cap, at, frac);
}

/* the next whole number of s[*i..n), skipping spaces; false at none */
static bool next_num(const char *s, uint32_t n, uint32_t *i, int64_t *v)
{
    uint32_t k = *i, digits = 0;
    bool neg = false;
    int64_t x = 0;
    while (k < n && s[k] == ' ') k++;
    if (k < n && s[k] == '-') neg = true, k++;
    while (k < n && s[k] >= '0' && s[k] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (s[k] - '0');
        k++;
    }
    if (!digits || (k < n && s[k] != ' ')) return false;
    *v = neg ? -x : x;
    *i = k;
    return true;
}

/* ---- VM1: the species tree ---- */

static const verderer_species_t *species_of(const verderer_t *v, int64_t species)
{
    for (uint32_t i = 0; i < v->nsp; i++)
        if (v->sp[i].species == species) return &v->sp[i];
    return NULL;
}

static bool species_fits(int64_t kingdom, int64_t family, int64_t genus, int64_t species,
                         int64_t protected_)
{
    return verderer_tree_ok(kingdom, family, genus, species, protected_) && kingdom >= 0 &&
           family >= 1 && genus >= 1 && species >= 1 && (protected_ == 0 || protected_ == 1);
}

int verderer_add_species(verderer_t *v, int64_t kingdom, int64_t family, int64_t genus,
                         int64_t species, int64_t protected_)
{
    if (!v || !species_fits(kingdom, family, genus, species, protected_)) return ZXM_E_ARG;
    if (species_of(v, species) || v->nsp >= VERDERER_SPECIES) return ZXM_E_STATE;
    verderer_species_t *t = &v->sp[v->nsp++];
    t->kingdom = kingdom;
    t->family = family;
    t->genus = genus;
    t->species = species;
    t->protected_ = protected_;
    v->dirty = true;
    return ZXM_OK;
}

int64_t verderer_protected(const verderer_t *v, int64_t species)
{
    const verderer_species_t *t = v ? species_of(v, species) : NULL;
    return t ? t->protected_ : 1;
}

/* ---- VM2: sightings ---- */

static bool species_key(int64_t s)
{
    return s >= 1 && s <= VERDERER_KEY_MAX;
}

int verderer_sight(verderer_t *v, int64_t observer, int64_t count, int64_t lat, int64_t lon,
                   int64_t stamp, int64_t *n)
{
    if (!v || !species_key(observer) || count < 1 || count > VERDERER_COUNT_MAX ||
        !verderer_place_ok(lat, lon) || stamp < 0 || stamp > INT64_C(99999999999))
        return ZXM_E_ARG;
    if (v->ns >= VERDERER_SIGHTINGS) return ZXM_E_STATE;
    verderer_sighting_t *s = &v->s[v->ns];
    memset(s, 0, sizeof *s);
    s->n = (int64_t) v->ns + 1;
    s->observer = observer;
    s->count = count;
    s->lat = lat;
    s->lon = lon;
    s->stamp = stamp;
    s->said = LPRES_STATE_TRUE; /* the observer's word, not yet witnessed */
    v->ns++;
    v->dirty = true;
    if (n) *n = s->n;
    return ZXM_OK;
}

int verderer_sight_here(verderer_t *v, int64_t observer, int64_t count, int64_t stamp, int64_t *n)
{
    if (!v || !v->link || !species_key(observer) || count < 1 || count > VERDERER_COUNT_MAX)
        return ZXM_E_ARG;
    int rc = verderer_here(v);
    if (rc) return rc;
    if (!v->here_known) return ZXM_E_STATE;
    return verderer_sight(v, observer, count, v->here_lat, v->here_lon, stamp, n);
}

verderer_sighting_t *verderer_find(verderer_t *v, int64_t n)
{
    if (!v || n < 1 || n > (int64_t) v->ns) return NULL;
    return &v->s[n - 1];
}

/* ---- VM3: a photo ---- */

int verderer_photo(verderer_t *v, int64_t n, int64_t bytes, int64_t free_bytes, int64_t reserve,
                   int64_t width, int64_t height, int64_t stamp)
{
    verderer_sighting_t *s = verderer_find(v, n);
    if (!s) return ZXM_E_STATE;
    if (bytes < 1 || width < 1 || height < 1 || stamp < 0 ||
        !pinhole_record_ok(n, 0, bytes, width, height, 0, stamp))
        return ZXM_E_ARG;
    if (!pinhole_photo_fits(bytes, free_bytes, reserve)) return ZXM_E_STATE; /* no room */
    s->photo_bytes = bytes;
    s->photo_width = width;
    s->photo_height = height;
    s->photo_stamp = stamp;
    v->dirty = true;
    return ZXM_OK;
}

/* ---- VM4: identification ---- */

int verderer_identify(verderer_t *v, int64_t n, int64_t lead)
{
    verderer_sighting_t *s = verderer_find(v, n);
    if (!s) return ZXM_E_STATE;
    if (lead >= 0 || lead < -VERDERER_KEY_MAX) return ZXM_E_ARG; /* no taxon reached */
    if (s->confirmed) return ZXM_E_STATE;                        /* settled */
    s->key = -lead;
    s->said = verderer_identity(s->observer, s->key);
    v->dirty = true;
    return ZXM_OK;
}

int verderer_confirm(verderer_t *v, int64_t n, int64_t species, bool owner_confirmed)
{
    verderer_sighting_t *s = verderer_find(v, n);
    if (!s) return ZXM_E_STATE;
    if (!species_key(species)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    s->confirmed = species;
    s->said = LPRES_STATE_TRUE;
    v->dirty = true;
    return ZXM_OK;
}

int verderer_rank(const int64_t *score, const int64_t *taxon, uint32_t n, int64_t reached,
                  int64_t *rank)
{
    int64_t key[VERDERER_HITS];
    if (!score || !taxon || !rank || n > VERDERER_HITS) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        key[i] = fieldguide_match_key(score[i], reached && taxon[i] == reached);
        if (key[i] == FIELDGUIDE_REFUSED) return ZXM_E_ARG;
    }
    for (uint32_t i = 0; i < n; i++) rank[i] = assay_score_rank(key, n, i);
    return ZXM_OK;
}

/* ---- VM5: the weather ---- */

int verderer_reading(verderer_t *v, int64_t n, int64_t stamp, int64_t temp, int64_t wind,
                     int64_t rain)
{
    verderer_sighting_t *s = verderer_find(v, n);
    if (!s) return ZXM_E_STATE;
    if (stamp < 1 || !verderer_reading_ok(stamp, temp, wind, rain) || temp < -VERDERER_TEMP_MAX ||
        temp > VERDERER_TEMP_MAX || wind < 0 || wind > VERDERER_WIND_MAX || rain < 0 ||
        rain > VERDERER_RAIN_MAX)
        return ZXM_E_ARG;
    s->wstamp = stamp;
    s->temp = temp;
    s->wind = wind;
    s->rain = rain;
    v->dirty = true;
    return ZXM_OK;
}

/* ---- VM6: share this place ---- */

int32_t verderer_share(const verderer_t *v, int64_t n, char *out, uint32_t cap)
{
    if (!v || !out || cap < 2u) return ZXM_E_ARG;
    if (n < 1 || n > (int64_t) v->ns) return ZXM_E_STATE;
    const verderer_sighting_t *s = &v->s[n - 1];
    int64_t species = s->confirmed ? s->confirmed : s->observer;
    int64_t lat = s->lat, lon = s->lon, cell = 0;
    bool coarse = verderer_protected(v, species) || (s->key && verderer_protected(v, s->key));
    if (coarse) { /* VR2, then VR3 on what would leave */
        cell = verderer_lon_cell(s->lat);
        lat = verderer_coarse(s->lat, VERDERER_CELL, 1);
        lon = verderer_coarse(s->lon, cell, 1);
        if (cell == VERDERER_REFUSED || lat == VERDERER_REFUSED || lon == VERDERER_REFUSED ||
            !verderer_share_ok(lat - verderer_coarse(lat, VERDERER_CELL, 1),
                               lon - verderer_coarse(lon, cell, 1), cell))
            return ZXM_E_STATE; /* it stays on the device */
    }
    uint32_t at = 0;
    out[0] = 0;
    bool ok = put(out, cap, &at, "Sighting ") && put_i64(out, cap, &at, s->n) &&
              put(out, cap, &at, ": species ") && put_i64(out, cap, &at, species);
    if (ok && !s->confirmed && s->said == LPRES_STATE_BOTH)
        ok = put(out, cap, &at, " (unconfirmed: the observer says ") &&
             put_i64(out, cap, &at, s->observer) && put(out, cap, &at, ", the key says ") &&
             put_i64(out, cap, &at, s->key) && put(out, cap, &at, ")");
    ok = ok && put(out, cap, &at, ", ") && put_i64(out, cap, &at, s->count) &&
         put(out, cap, &at, " seen at ") && put_deg(out, cap, &at, lat) &&
         put(out, cap, &at, ",") && put_deg(out, cap, &at, lon);
    if (ok && coarse) ok = put(out, cap, &at, " (10 km grid: a protected species)");
    if (!ok) {
        out[0] = 0;
        return ZXM_E_TOOBIG;
    }
    return (int32_t) at;
}

/* ---- VM7: the file ---- */

static bool put_line(char *out, uint32_t cap, uint32_t *at, const char *tag, const int64_t *x,
                     uint32_t n)
{
    if (!put(out, cap, at, tag)) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!put(out, cap, at, " ") || !put_i64(out, cap, at, x[i])) return false;
    return put(out, cap, at, "\n");
}

static bool sighting_fits(const verderer_sighting_t *s)
{
    if (!species_key(s->observer) || (s->key && !species_key(s->key)) ||
        (s->confirmed && !species_key(s->confirmed)) || s->count < 1 ||
        s->count > VERDERER_COUNT_MAX || !verderer_place_ok(s->lat, s->lon) || s->stamp < 0 ||
        s->stamp > INT64_C(99999999999) || s->said < LPRES_STATE_NEITHER ||
        s->said > LPRES_STATE_BOTH)
        return false;
    if (s->photo_bytes && (s->photo_width < 1 || s->photo_height < 1 ||
                           !pinhole_record_ok(s->n, 0, s->photo_bytes, s->photo_width,
                                              s->photo_height, 0, s->photo_stamp)))
        return false;
    if (s->wstamp && (!verderer_reading_ok(s->wstamp, s->temp, s->wind, s->rain) ||
                      s->temp < -VERDERER_TEMP_MAX || s->temp > VERDERER_TEMP_MAX || s->wind < 0 ||
                      s->wind > VERDERER_WIND_MAX || s->rain < 0 || s->rain > VERDERER_RAIN_MAX))
        return false;
    return s->said == (s->confirmed ? LPRES_STATE_TRUE : verderer_identity(s->observer, s->key));
}

int verderer_save(verderer_t *v, bool owner_confirmed)
{
    static char text[VERDERER_TEXT_MAX];
    uint32_t at = 0;
    if (!v || !v->link) return ZXM_E_ARG;
    text[0] = 0;
    bool ok = put(text, sizeof text, &at, "verderer 1\n");
    for (uint32_t i = 0; ok && i < v->nsp; i++) {
        const verderer_species_t *t = &v->sp[i];
        int64_t x[5] = {t->kingdom, t->family, t->genus, t->species, t->protected_};
        ok = put_line(text, sizeof text, &at, "t", x, 5);
    }
    for (uint32_t i = 0; ok && i < v->ns; i++) {
        const verderer_sighting_t *s = &v->s[i];
        int64_t x[VERDERER_FIELDS] = {
            s->n,           s->observer,     s->key,         s->confirmed,      s->lat,
            s->lon,         s->stamp,        s->count,       (int64_t) s->said, s->photo_bytes,
            s->photo_width, s->photo_height, s->photo_stamp, s->wstamp,         s->temp,
            s->wind,        s->rain};
        ok = put_line(text, sizeof text, &at, "s", x, VERDERER_FIELDS);
    }
    if (!ok) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_begin(v->link, "files.write");
    zxm_p_str(v->link, "path", VERDERER_FILE);
    zxm_p_strn(v->link, "text", text, at);
    int rc = zxm_call(v->link);
    if (rc) return rc;
    v->dirty = false;
    return ZXM_OK;
}

static int parse(verderer_t *into, const char *text, uint32_t n)
{
    uint32_t i = 0, line = 0;
    while (i < n) {
        uint32_t e = i;
        while (e < n && text[e] != '\n') e++;
        const char *l = text + i;
        uint32_t ln = e - i, k = 1;
        i = e + 1u;
        if (ln > VERDERER_LINE_MAX) return ZXM_E_STATE;
        if (line++ == 0) {
            if (ln != 10u || memcmp(l, "verderer 1", 10)) return ZXM_E_STATE;
            continue;
        }
        if (!ln) continue;
        if (ln < 2u || l[1] != ' ') return ZXM_E_STATE;
        int64_t x[VERDERER_FIELDS];
        uint32_t want = l[0] == 't' ? 5u : l[0] == 's' ? VERDERER_FIELDS : 0u;
        if (!want) return ZXM_E_STATE;
        for (uint32_t f = 0; f < want; f++)
            if (!next_num(l, ln, &k, &x[f])) return ZXM_E_STATE;
        if (k != ln) return ZXM_E_STATE;
        if (l[0] == 't') {
            if (verderer_add_species(into, x[0], x[1], x[2], x[3], x[4])) return ZXM_E_STATE;
            continue;
        }
        if (into->ns >= VERDERER_SIGHTINGS || x[0] != (int64_t) into->ns + 1) return ZXM_E_STATE;
        verderer_sighting_t *s = &into->s[into->ns];
        s->n = x[0];
        s->observer = x[1];
        s->key = x[2];
        s->confirmed = x[3];
        s->lat = x[4];
        s->lon = x[5];
        s->stamp = x[6];
        s->count = x[7];
        if (x[8] < 0 || x[8] > 3) return ZXM_E_STATE;
        s->said = (lpres_state_t) x[8];
        s->photo_bytes = x[9];
        s->photo_width = x[10];
        s->photo_height = x[11];
        s->photo_stamp = x[12];
        s->wstamp = x[13];
        s->temp = x[14];
        s->wind = x[15];
        s->rain = x[16];
        if (!sighting_fits(s)) return ZXM_E_STATE;
        into->ns++;
    }
    return line ? ZXM_OK : ZXM_E_STATE;
}

int verderer_load(verderer_t *v)
{
    static char text[VERDERER_TEXT_MAX + 1u];
    static verderer_t got;
    bool eof = false;
    if (!v || !v->link) return ZXM_E_ARG;
    zxm_link_t *l = v->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", VERDERER_FILE);
    zxm_p_u64(l, "max", VERDERER_TEXT_MAX);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "eof", &eof) ||
        zxm_get_str(l, l->result, "text", text, sizeof text))
        return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    got = *v; /* the map, search and here stay; the tree and sightings are the file's */
    got.nsp = 0;
    got.ns = 0;
    rc = parse(&got, text, len_of(text, VERDERER_TEXT_MAX));
    if (rc) return rc; /* what is open stays as it was */
    got.dirty = false;
    *v = got;
    return ZXM_OK;
}

/* ---- VM8: the map ---- */

int verderer_here(verderer_t *v)
{
    int64_t lat = 0, lon = 0;
    bool known = false;
    if (!v || !v->link) return ZXM_E_ARG;
    zxm_link_t *l = v->link;
    zxm_begin(l, "place.here");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "known", &known)) return ZXM_E_PROTOCOL;
    if (known && (zxm_get_i64(l, l->result, "lat", &lat) ||
                  zxm_get_i64(l, l->result, "lon", &lon) || !verderer_place_ok(lat, lon)))
        return ZXM_E_PROTOCOL;
    v->here_known = known;
    v->here_lat = known ? lat : 0;
    v->here_lon = known ? lon : 0;
    return ZXM_OK;
}

int verderer_mark(verderer_t *v, const char *name, int64_t lat, int64_t lon, const char *note,
                  bool owner_confirmed)
{
    uint32_t nn = len_of(name, VERDERER_NAME_MAX + 1u);
    uint32_t tn = len_of(note, VERDERER_NOTE_MAX + 1u);
    if (!v || !v->link || !name || !nn || nn > VERDERER_NAME_MAX || !text_ok(name, nn) ||
        tn > VERDERER_NOTE_MAX || (note && !text_ok(note, tn)) || !verderer_place_ok(lat, lon))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_link_t *l = v->link;
    zxm_begin(l, "place.add");
    zxm_p_str(l, "name", name);
    zxm_p_i64(l, "lat", lat);
    zxm_p_i64(l, "lon", lon);
    if (tn) zxm_p_str(l, "note", note);
    int rc = zxm_call(l);
    return rc ? rc : verderer_places(v);
}

int verderer_places(verderer_t *v)
{
    static verderer_place_t got[VERDERER_PLACES];
    if (!v || !v->link) return ZXM_E_ARG;
    zxm_link_t *l = v->link;
    zxm_begin(l, "place.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "places");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), k = 0;
    for (uint32_t i = 0; i < n && k < VERDERER_PLACES; i++) {
        int32_t o = zxm_at(l, arr, i);
        verderer_place_t *p = &got[k];
        if (o < 0 || zxm_get_str(l, o, "name", p->name, sizeof p->name) || !p->name[0] ||
            zxm_get_i64(l, o, "lat", &p->lat) || zxm_get_i64(l, o, "lon", &p->lon) ||
            !verderer_place_ok(p->lat, p->lon))
            return ZXM_E_PROTOCOL;
        k++;
    }
    memcpy(v->place, got, k * sizeof got[0]);
    v->nplaces = k;
    return ZXM_OK;
}

/* ---- VM9: search ---- */

int verderer_search(verderer_t *v, const char *query)
{
    static char got[VERDERER_HITS][VERDERER_PATH_MAX];
    uint32_t n = len_of(query, VERDERER_QUERY_MAX + 1u), k = 0;
    bool word = false;
    if (!v || !v->link || !query || !n || n > VERDERER_QUERY_MAX || !text_ok(query, n))
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < n && !word; i++) word = query[i] != ' ';
    if (!word) return ZXM_E_ARG;
    zxm_link_t *l = v->link;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", query);
    zxm_p_u64(l, "max", VERDERER_HITS);
    zxm_p_bool(l, "refresh", true);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "hits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > VERDERER_HITS)
        return ZXM_E_PROTOCOL;
    uint32_t hits = zxm_len(l, arr);
    for (uint32_t i = 0; i < hits; i++) {
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "path", got[k], sizeof got[k])) continue; /* too long a path */
        k++;
    }
    memcpy(v->hit, got, k * sizeof got[0]);
    v->nhits = k;
    return ZXM_OK;
}

/* ---- advisory ---- */

int64_t verderer_estimate(int64_t marked, int64_t caught, int64_t recaptured)
{
    return verderer_recapture_estimate(marked, caught, recaptured);
}

int64_t verderer_grid(const verderer_t *v, int64_t species, int64_t lat, int64_t lon)
{
    int64_t sp[VERDERER_SIGHTINGS + 1u], cell[VERDERER_SIGHTINGS + 1u];
    int64_t want = verderer_cell_of(lat, lon);
    if (!v || want == VERDERER_REFUSED) return VERDERER_REFUSED;
    for (uint32_t i = 0; i < v->ns; i++) {
        const verderer_sighting_t *s = &v->s[i];
        sp[i] = s->confirmed ? s->confirmed : s->observer;
        cell[i] = verderer_cell_of(s->lat, s->lon);
    }
    sp[v->ns] = species;
    cell[v->ns] = want;
    return verderer_grid_count(sp, cell, v->ns + 1u, v->ns) - 1;
}

bool verderer_visit_ok(int64_t party, int64_t party_max, int64_t distance, int64_t distance_min,
                       int64_t drone)
{
    int64_t room[2];
    return verderer_visit_rooms(party, party_max, distance, distance_min, room) &&
           verderer_reserve_visit_ok(room[0], room[1], drone);
}

int64_t verderer_caution(const int64_t *mark, uint32_t n, char *notice, uint32_t cap)
{
    int64_t level = fieldguide_caution(mark, n);
    uint32_t at = 0;
    if (!notice || cap < sizeof VERDERER_NEVER_SAFE + 80u) return FIELDGUIDE_REFUSED;
    notice[0] = 0;
    if (level == FIELDGUIDE_CAUTION_MARKED)
        put(notice, cap, &at, "Caution: an article marks this species toxic or dangerous. ");
    else if (level == FIELDGUIDE_CAUTION_NONE)
        put(notice, cap, &at, "No article marks this species; that does not make it harmless. ");
    else if (level == FIELDGUIDE_CAUTION_UNKNOWN)
        put(notice, cap, &at, "Toxicity unknown: nothing here says. ");
    put(notice, cap, &at, VERDERER_NEVER_SAFE);
    return level;
}
