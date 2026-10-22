/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* toolchest_model.c — Toolchest, developer tools (see toolchest_model.h).
 * Every input is checked and every record made to fit before anything is
 * kept; nothing is ever installed. */
#include <stdio.h>
#include <string.h>

#include "toolchest_model.h"

/* ---- the catalogue: sdk/kits (README.md's table) and the language SDKs ---------- */

static const toolchest_entry_t CATALOGUE[] = {
    {"Z-Wasm", "kits/z-wasm", TOOLCHEST_LANG_C},
    {"z-contract", "kits/z-contract", TOOLCHEST_LANG_C},
    {"z-ffi", "kits/z-ffi", TOOLCHEST_LANG_C},
    {"adapter-matrix", "kits/adapter-matrix", TOOLCHEST_LANG_C},
    {"AIPI", "kits/aipi", TOOLCHEST_LANG_C},
    {"z-audit", "kits/z-audit", TOOLCHEST_LANG_C},
    {"Tri-Isolate", "kits/tri-isolate", TOOLCHEST_LANG_C},
    {"ZAB-Logic", "kits/zab-logic", TOOLCHEST_LANG_C},
    {"libzxv", "libzxv", TOOLCHEST_LANG_C},
    {"C++ SDK", "cpp", TOOLCHEST_LANG_CPP},
    {"Rust SDK", "rust", TOOLCHEST_LANG_RUST},
    {"Go SDK", "go", TOOLCHEST_LANG_GO},
    {"Python SDK", "python", TOOLCHEST_LANG_PYTHON},
    {"Java SDK", "java", TOOLCHEST_LANG_JAVA},
    {"TypeScript SDK", "typescript", TOOLCHEST_LANG_TYPESCRIPT},
    {"Swift SDK", "swift", TOOLCHEST_LANG_SWIFT},
    {"Ruby SDK", "ruby", TOOLCHEST_LANG_RUBY},
    {"PHP SDK", "php", TOOLCHEST_LANG_PHP},
    {".NET SDK", "dotnet", TOOLCHEST_LANG_DOTNET},
    {"Solidity SDK", "solidity", TOOLCHEST_LANG_SOLIDITY},
};

#define CATALOGUE_N ((uint32_t) (sizeof CATALOGUE / sizeof CATALOGUE[0]))

/* every kit has a record of its own (TM5) and a place MYRA's kit:i3 holds */
_Static_assert(sizeof CATALOGUE / sizeof CATALOGUE[0] <= TOOLCHEST_KITS, "catalogue too long");

uint32_t toolchest_catalogue_n(void)
{
    return CATALOGUE_N;
}

const toolchest_entry_t *toolchest_catalogue(uint32_t i)
{
    return i < CATALOGUE_N ? &CATALOGUE[i] : NULL;
}

int32_t toolchest_find(const char *name)
{
    if (!name) return -1;
    for (uint32_t i = 0; i < CATALOGUE_N; i++)
        if (!strcmp(name, CATALOGUE[i].name)) return (int32_t) i;
    return -1;
}

/* ---- TM1: versions ------------------------------------------------------------ */

int toolchest_parse(const char *s, toolchest_version_t *v)
{
    int64_t part[3] = {0, 0, 0};
    uint32_t i = 0, k = 0, digits = 0;
    if (!s || !v) return ZXM_E_ARG;
    for (;; i++) {
        char c = s[i];
        if (i > TOOLCHEST_TEXT) return ZXM_E_ARG;
        if (c >= '0' && c <= '9') {
            if (digits == 1 && part[k] == 0) return ZXM_E_ARG; /* a leading zero */
            if (++digits > 3) return ZXM_E_ARG;
            part[k] = part[k] * 10 + (c - '0');
            continue;
        }
        if (!digits) return ZXM_E_ARG; /* an empty part */
        if (c == '.' && k < 2) {
            k++;
            digits = 0;
            continue;
        }
        if (c == 0 && k == 2) break;
        return ZXM_E_ARG;
    }
    v->major = part[0];
    v->minor = part[1];
    v->patch = part[2];
    return ZXM_OK;
}

int64_t toolchest_fits(const toolchest_version_t *kit, const toolchest_version_t *run)
{
    if (!kit || !run) return TOOLCHEST_REFUSED;
    return toolchest_version_fits(kit->major, kit->minor, kit->patch, run->major, run->minor,
                                  run->patch);
}

static void vtext(const toolchest_version_t *v, char *out, size_t cap)
{
    snprintf(out, cap, "%lld.%lld.%lld", (long long) v->major, (long long) v->minor,
             (long long) v->patch);
}

/* ---- TM1-TM3: what zxvd says ----------------------------------------------------- */

void toolchest_init(toolchest_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->pending = -1;
}

int toolchest_read_daemon(toolchest_t *t)
{
    zxm_link_t *l = t->link;
    char app[TOOLCHEST_TEXT + 1], lib[TOOLCHEST_TEXT + 1];
    toolchest_version_t abi;
    zxm_begin(l, "zxvd.version");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "app", app, sizeof app) != ZXM_OK ||
        zxm_get_str(l, l->result, "libzxv", lib, sizeof lib) != ZXM_OK ||
        toolchest_parse(lib, &abi) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    zxm_begin(l, "rpc.discover");
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t ms = zxm_get(l, l->result, "methods");
    if (ms < 0 || l->tok[ms].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, ms);
    bool fab = false;
    for (uint32_t i = 0; i < n; i++) {
        int32_t m = zxm_at(l, ms, i);
        if (m < 0 || l->tok[m].type != W4_J_STR) return ZXM_E_PROTOCOL;
        if (w4_json_str_eq(l->resp, &l->tok[m], "fab.receive")) fab = true;
    }
    char a[64];
    memcpy(t->app, app, sizeof app);
    t->abi = abi;
    t->known = true;
    t->methods = n;
    t->fab_receive = fab;
    vtext(&abi, a, sizeof a);
    snprintf(t->notice, sizeof t->notice,
             "zxvd %s on libzxv %s: %u methods; %s. zxvd has no method that installs a kit.",
             t->app, a, n,
             fab ? "a kit's recipe can be received (fab.receive)" : "no recipe can be received");
    return ZXM_OK;
}

int toolchest_read_samples(toolchest_t *t)
{
    zxm_link_t *l = t->link;
    zxm_begin(l, "play.catalogue");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = l->result;
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), kept = 0;
    toolchest_sample_t got[TOOLCHEST_SAMPLES]; /* kept only once every template reads */
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        char id[TOOLCHEST_TEXT + 1], lang[TOOLCHEST_TEXT + 1];
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "id", id, sizeof id) != ZXM_OK ||
            zxm_get_str(l, o, "lang", lang, sizeof lang) != ZXM_OK || !id[0])
            return ZXM_E_PROTOCOL;
        if (kept < TOOLCHEST_SAMPLES) {
            memcpy(got[kept].id, id, sizeof id);
            memcpy(got[kept].lang, lang, sizeof lang);
            kept++;
        }
    }
    memcpy(t->sample, got, kept * sizeof got[0]);
    t->samples = n;
    t->kept = kept;
    snprintf(t->notice, sizeof t->notice, "%u Playground samples%s.", n,
             n > kept ? ", the first 64 kept" : "");
    return ZXM_OK;
}

uint32_t toolchest_samples_in(const toolchest_t *t, const char *lang)
{
    uint32_t n = 0;
    if (!t || !lang) return 0;
    for (uint32_t i = 0; i < t->kept; i++) n += !strcmp(t->sample[i].lang, lang);
    return n;
}

int toolchest_read_disk(toolchest_t *t)
{
    zxm_link_t *l = t->link;
    int64_t total = 0, avail = 0;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "total", &total) != ZXM_OK ||
        zxm_get_i64(l, l->result, "avail", &avail) != ZXM_OK || total < 0 || avail < 0 ||
        avail > total || total > TOOLCHEST_ROOM_MAX)
        return ZXM_E_PROTOCOL;
    t->disk_known = true;
    t->disk_total = total;
    t->disk_avail = avail;
    snprintf(t->notice, sizeof t->notice, "%lld of %lld bytes may still be written.",
             (long long) avail, (long long) total);
    return ZXM_OK;
}

int toolchest_check_update(toolchest_t *t)
{
    zxm_link_t *l = t->link;
    char status[TOOLCHEST_TEXT + 1];
    zxm_begin(l, "update.check");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "status", status, sizeof status) != ZXM_OK || !status[0])
        return ZXM_E_PROTOCOL;
    memcpy(t->status, status, sizeof status);
    snprintf(t->notice, sizeof t->notice, "Update check: %s. Nothing is installed.", status);
    return ZXM_OK;
}

/* ---- TM4, TM5: offers and the owner's word ------------------------------------- */

static void compose_offer(toolchest_t *t, uint32_t i)
{
    const toolchest_kit_t *k = &t->kit[i];
    const char *name = CATALOGUE[i].name;
    char v[64], need[64], run[64];
    vtext(&k->ver, v, sizeof v);
    vtext(&k->abi, need, sizeof need);
    vtext(&t->abi, run, sizeof run);
    switch (k->outcome) {
    case TOOLCHEST_ALLOWED:
        snprintf(t->notice, sizeof t->notice,
                 "%s %s fits libzxv %s, both signatures verify and its %lld bytes fit "
                 "(%lld left). It waits for your word; nothing is installed until you approve it.",
                 name, v, run, (long long) k->need, (long long) k->room);
        break;
    case TOOLCHEST_UNSIGNED:
        snprintf(t->notice, sizeof t->notice,
                 "%s %s refused: it needs both signatures verified (first %s, second %s).", name, v,
                 k->firstsig == 1 ? "verified" : "not verified",
                 k->secondsig == 1 ? "verified" : "not verified");
        break;
    case TOOLCHEST_NO_FIT:
        snprintf(t->notice, sizeof t->notice, "%s %s refused: it needs libzxv %s, and %s runs.",
                 name, v, need, run);
        break;
    default:
        snprintf(t->notice, sizeof t->notice,
                 "%s %s refused: it needs %lld bytes and %lld may be written.", name, v,
                 (long long) k->need, (long long) t->disk_avail);
        break;
    }
}

int toolchest_offer(toolchest_t *t, uint32_t kit, const char *version, const char *needs,
                    const int64_t *blocks, uint32_t nblocks, const int64_t *files, uint32_t nfiles,
                    int64_t firstsig, int64_t secondsig)
{
    toolchest_version_t v, abi;
    if (!t->known || !t->disk_known || t->pending >= 0) return ZXM_E_STATE;
    const toolchest_entry_t *e = toolchest_catalogue(kit);
    if (!e || toolchest_parse(version, &v) != ZXM_OK || toolchest_parse(needs, &abi) != ZXM_OK)
        return ZXM_E_ARG;
    int64_t down = toolchest_download_bytes(blocks, nblocks);
    int64_t inst = toolchest_installed_bytes(files, nfiles);
    if (down == TOOLCHEST_REFUSED || inst == TOOLCHEST_REFUSED) return ZXM_E_ARG;
    if (!toolchest_kit_record_fits((int64_t) kit, e->lang, v.major, v.minor, v.patch, inst))
        return ZXM_E_ARG;
    /* both sums are far below int64 (TC3, TC4), and avail is at most 18 digits */
    int64_t need = down + inst;
    int64_t room = t->disk_avail - need;
    int64_t fits = toolchest_fits(&abi, &t->abi);
    int64_t outcome;
    if (firstsig != 1 || secondsig != 1)
        outcome = TOOLCHEST_UNSIGNED;
    else if (fits != 1)
        outcome = TOOLCHEST_NO_FIT;
    else if (!toolchest_install_allowed(firstsig, secondsig, fits, room))
        outcome = TOOLCHEST_NO_ROOM;
    else
        outcome = TOOLCHEST_ALLOWED;
    toolchest_kit_t *k = &t->kit[kit];
    k->known = true;
    k->ver = v;
    k->abi = abi;
    k->download = down;
    k->installed = inst;
    k->need = need;
    k->room = room;
    k->firstsig = firstsig;
    k->secondsig = secondsig;
    k->fits = fits;
    k->outcome = outcome;
    k->used_after_x100 = toolchest_used_x100(t->disk_total, room);
    t->pending = outcome == TOOLCHEST_ALLOWED ? (int64_t) kit : -1;
    compose_offer(t, kit);
    return ZXM_OK;
}

int toolchest_confirm(toolchest_t *t, bool approve)
{
    char v[64];
    if (t->pending < 0) return ZXM_E_STATE;
    uint32_t i = (uint32_t) t->pending;
    toolchest_kit_t *k = &t->kit[i];
    k->outcome = approve ? TOOLCHEST_APPROVED : TOOLCHEST_DECLINED;
    t->pending = -1;
    vtext(&k->ver, v, sizeof v);
    if (approve)
        snprintf(t->notice, sizeof t->notice,
                 "You approved %s %s. zxvd has no method that installs a kit, so install it from "
                 "sdk/%s; Toolchest installs nothing.",
                 CATALOGUE[i].name, v, CATALOGUE[i].folder);
    else
        snprintf(t->notice, sizeof t->notice, "You declined %s %s. Nothing is installed.",
                 CATALOGUE[i].name, v);
    return ZXM_OK;
}

bool toolchest_record_ok(const toolchest_t *t, uint32_t i)
{
    if (!t || i >= CATALOGUE_N || !t->kit[i].known) return false;
    const toolchest_kit_t *k = &t->kit[i];
    return toolchest_kit_record_fits((int64_t) i, CATALOGUE[i].lang, k->ver.major, k->ver.minor,
                                     k->ver.patch, k->installed);
}
