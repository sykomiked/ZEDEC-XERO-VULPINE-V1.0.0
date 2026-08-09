/* vena.c — Vena System Implementation
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "vena.h"
#include "../zab/zab_exec.h"
#include "../choice/choice_core.h"
#include "../../include/m5_types.h"

static __attribute__((unused)) int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static __attribute__((unused)) int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void str_copy(char *d, const char *s) { int i=0; while(s[i]){d[i]=s[i];i++;} d[i]=0; }

static const char *lang_names[VENA_MAX_LANGUAGES] = {
    "English","中文","日本語","한국어","العربية","עברית","हिन्दी","संस्कृतम्",
    "Русский","Français","Deutsch","Español","Português","Kiswahili","አማርኛ",
    "Diné bizaad","ᐃᓄᒃᑎᑐᑦ","Euskara","Cymraeg","Gàidhlig","tlhIngan Hol",
    "Quenya","Dothraki","Esperanto","Lojban","Ithkuil","Toki Pona",
    "Sign Language","Braille","Morse","Binary","M5-Axiomatic"
};

const char *vena_language_name(vena_language_t lang) {
    return lang < VENA_MAX_LANGUAGES ? lang_names[lang] : "Unknown";
}

void vena_init(vena_runtime_t *vr, vino_ledger_t *ledger) {
    uint32_t i;
    for (i = 0; i < sizeof(*vr); i++) ((uint8_t*)vr)[i] = 0;
    vr->ledger = ledger;
    vr->default_language = LANG_M5_AXIOMATIC;
    vr->active_apps = 0;

    /* Enable all languages */
    for (i = 0; i < VENA_MAX_LANGUAGES; i++)
        vr->languages_supported[i] = true;
}

int32_t vena_register_contract(vena_runtime_t *vr, const char *name,
                                const char *code, vena_language_t lang,
                                const char *creator) {
    if (vr->num_contracts >= VENA_MAX_CONTRACTS) return -1;
    vena_contract_t *c = &vr->contracts[vr->num_contracts];
    c->id = vr->num_contracts;
    str_copy(c->name, name);
    str_copy(c->code, code);
    c->language = lang;
    if (creator) str_copy(c->creator, creator);
    c->active = true;
    return (int32_t)vr->num_contracts++;
}

/* ---- the ZAB host: what a contract may actually touch ----
 * A contract's whole writable surface is the vino ledger it was deployed
 * against. Handlers are provided ONLY for the effects that make sense here;
 * everything else is left NULL, so a contract carrying SEND or SPAWN is stopped
 * by the VM with NO_HOST rather than by a check someone had to remember. */
typedef struct { vena_runtime_t *vr; vena_contract_t *c; } vena_hostctx_t;

static int vh_read(void *cx, uint8_t a, uint16_t b) {
    vena_hostctx_t *h = cx; (void)a; (void)b;
    return (h && h->vr && h->vr->ledger) ? 0 : -1;
}
static int vh_post(void *cx, uint8_t a, uint16_t b) {
    vena_hostctx_t *h = cx; (void)a; (void)b;
    if (!h || !h->vr || !h->vr->ledger) return -1;
    h->vr->ledger->num_audit++;   /* the contract effect: an audit entry */
    return 0;
}
static int vh_observe(void *cx, uint8_t a, uint16_t b) { (void)cx;(void)a;(void)b; return 0; }

int32_t vena_execute_contract(vena_runtime_t *vr, uint32_t id,
                               const char *args, char *result, uint32_t max_result) {
    if (!vr || id >= vr->num_contracts || !vr->contracts[id].active) return -1;
    vena_contract_t *c = &vr->contracts[id];
    (void)args;
    if (result && max_result > 0) result[0] = 0;

    /* If the contract carries a ZAB program, RUN IT -- capabilities derived
     * from its own instructions and enforced per instruction by the VM. A
     * contract can no longer claim an authority it does not contain, and it
     * cannot exceed what its deployment granted. */
    const uint8_t *code = (const uint8_t *)c->code;
    uint32_t derived = 0;
    if (zab_derive_capabilities(code, VENA_CODE_LEN, &derived) == ZAB_OK) {
        vena_hostctx_t hc = { vr, c };
        zab_host_t host;
        for (unsigned i = 0; i < sizeof(host); i++) ((uint8_t *)&host)[i] = 0;
        host.read_state = vh_read;
        host.ledger_post = vh_post;
        host.observe = vh_observe;
        host.ctx = &hc;

        /* The grant is what this runtime permits, intersected by the VM with
         * what the program contains. Ledger + read + observe, nothing else. */
        uint32_t granted = ZAB_CAP_LEDGER | ZAB_CAP_READ_STATE | ZAB_CAP_OBSERVE;
        zab_exec_t ex;
        zab_exec_result_t r = zab_execute(code, VENA_CODE_LEN, granted, &host, &ex);

        c->call_count++;
        c->gas_used += 10u + ex.effects * 100u;   /* gas tracks REAL effects */
        vr->contracts_executed++;
        vr->total_gas += 10u + ex.effects * 100u;
        if (r != ZABX_OK) return -2;              /* denied / failed: report it */
        return 0;
    }

    /* Not ZAB bytecode. Accounted, but NOT pretended to have executed -- a
     * contract in a language with no runtime does nothing, and saying so is
     * better than returning success. */
    c->call_count++;
    c->gas_used += 10;
    vr->contracts_executed++;
    vr->total_gas += 10;
    return -3;
}

int32_t vena_load_app(vena_runtime_t *vr, const char *name, app_type_t type,
                      const char *code, vena_language_t lang) {
    if (vr->num_apps >= VENA_MAX_APPS) return -1;
    vena_app_t *a = &vr->apps[vr->num_apps];
    a->id = vr->num_apps;
    str_copy(a->name, name);
    a->type = type;
    a->language = lang;
    if (code) str_copy(a->code, code);
    a->running = false;
    a->pid = 0;
    return (int32_t)vr->num_apps++;
}

int32_t vena_start_app(vena_runtime_t *vr, uint32_t id) {
    if (id >= vr->num_apps) return -1;

    /* M5 CHOICE: deterministic collapse resolution before app start.
     * The collapse state is a deterministic toggle — same input always
     * produces the same decision. First call executes, then alternates. */
    collapse_t state = choice_get_state();
    /* Use bit corresponding to app ID to determine execute/defer */
    uint32_t bit_idx = id % 64;
    uint32_t word = bit_idx / 32;
    uint32_t bit = bit_idx % 32;
    bool should_execute = (state.bits[word] & (1u << bit)) == 0;
    /* Toggle the bit — deterministic collapse resolution */
    state.bits[word] ^= (1u << bit);
    choice_set_state(&state);
    if (!should_execute) {
        return -1; /* App deferred to next cycle — call again to execute */
    }

    vr->apps[id].running = true;
    vr->apps[id].pid = id + 1;
    vr->active_apps++;
    return 0;
}

int32_t vena_stop_app(vena_runtime_t *vr, uint32_t id) {
    if (id >= vr->num_apps) return -1;
    if (vr->apps[id].running) {
        vr->apps[id].running = false;
        if (vr->active_apps > 0) vr->active_apps--;
    }
    return 0;
}

int32_t vena_list_apps(vena_runtime_t *vr, uint32_t *ids, uint32_t max_ids) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < vr->num_apps && count < max_ids; i++) {
        if (vr->apps[i].running) ids[count++] = i;
    }
    return (int32_t)count;
}

int32_t vena_register_oracle(vena_runtime_t *vr, const char *name, const char *source) {
    if (vr->num_oracles >= VENA_MAX_ORACLES) return -1;
    vena_oracle_t *o = &vr->oracles[vr->num_oracles];
    o->id = vr->num_oracles;
    str_copy(o->name, name);
    str_copy(o->source, source);
    o->active = true;
    return (int32_t)vr->num_oracles++;
}

int32_t vena_update_oracle(vena_runtime_t *vr, uint32_t id, uint64_t value) {
    if (id >= vr->num_oracles) return -1;
    vr->oracles[id].last_value = value;
    vr->oracles[id].last_update = 0;
    return 0;
}

int32_t vena_set_language(vena_runtime_t *vr, vena_language_t lang) {
    if (lang >= VENA_MAX_LANGUAGES) return -1;
    vr->default_language = lang;
    return 0;
}

bool vena_is_language_supported(vena_runtime_t *vr, vena_language_t lang) {
    if (lang >= VENA_MAX_LANGUAGES) return false;
    return vr->languages_supported[lang];
}
