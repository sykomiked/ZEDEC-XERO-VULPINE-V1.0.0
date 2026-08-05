/* vena.h — Vena System: Native Runtime Integration with Vino Ledger
 * Vena is the execution engine that runs on top of the Vino ledger,
 * providing smart contracts, programmable capital flows, and the
 * M5 axiomatic application runtime.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef VENA_H
#define VENA_H

#include <stdint.h>
#include <stdbool.h>
#include "vino.h"

#define VENA_MAX_CONTRACTS 256
#define VENA_MAX_APPS      128
#define VENA_MAX_ORACLES   64
#define VENA_CODE_LEN      1024
#define VENA_MAX_LANGUAGES 32

/* Supported languages (including obscure ones) */
typedef enum {
    LANG_ENGLISH = 0,
    LANG_CHINESE,      /* 中文 */
    LANG_JAPANESE,     /* 日本語 */
    LANG_KOREAN,       /* 한국어 */
    LANG_ARABIC,       /* العربية */
    LANG_HEBREW,       /* עברית */
    LANG_HINDI,        /* हिन्दी */
    LANG_SANSKRIT,     /* संस्कृतम् */
    LANG_RUSSIAN,      /* Русский */
    LANG_FRENCH,       /* Français */
    LANG_GERMAN,       /* Deutsch */
    LANG_SPANISH,      /* Español */
    LANG_PORTUGUESE,   /* Português */
    LANG_SWAHILI,      /* Kiswahili */
    LANG_AMHARIC,      /* አማርኛ */
    LANG_NAVAJO,       /* Diné bizaad */
    LANG_INUKTITUT,    /* ᐃᓄᒃᑎᑐᑦ */
    LANG_BASQUE,       /* Euskara */
    LANG_WELSH,        /* Cymraeg */
    LANG_GAELIC,       /* Gàidhlig */
    LANG_KLINGON,      /* tlhIngan Hol */
    LANG_QUENYA,       /* Elvish */
    LANG_DOthraki,     /* Constructed */
    LANG_ESPERANTO,
    LANG_LOJBAN,
    LANG_ITHKUIL,
    LANG_TOKI_PONA,
    LANG_SIGN_LANG,    /* Sign language support */
    LANG_BRAILLE,      /* Braille encoding */
    LANG_MORSE,        /* Morse code as language */
    LANG_BINARY,       /* Raw binary */
    LANG_M5_AXIOMATIC  /* Native M5 axiomatic language */
} vena_language_t;

/* Smart contract */
typedef struct vena_contract {
    uint32_t id;
    char name[64];
    char code[VENA_CODE_LEN];
    vena_language_t language;
    char creator[VINO_ADDR_LEN];
    bool active;
    uint32_t call_count;
    uint64_t gas_used;
} vena_contract_t;

/* Application */
typedef enum {
    APP_SHELL = 0,
    APP_EDITOR,
    APP_FILE_MANAGER,
    APP_SYSMON,
    APP_NET_CONFIG,
    APP_WALLET,
    APP_BANK,
    APP_EXCHANGE,
    APP_MESSAGING,
    APP_BROWSER,
    APP_TERMINAL,
    APP_CALCULATOR,
    APP_CLOCK,
    APP_MEDIA_PLAYER,
    APP_CUSTOM
} app_type_t;

typedef struct vena_app {
    uint32_t id;
    char name[64];
    app_type_t type;
    vena_language_t language;
    char code[VENA_CODE_LEN];
    char creator[VINO_ADDR_LEN];
    bool running;
    uint32_t pid;
    uint32_t memory_kb;
    uint32_t cpu_ticks;
} vena_app_t;

/* Oracle (external data feed) */
typedef struct vena_oracle {
    uint32_t id;
    char name[32];
    char source[64];
    uint64_t last_value;
    uint32_t last_update;
    bool active;
} vena_oracle_t;

typedef struct vena_runtime {
    vena_contract_t contracts[VENA_MAX_CONTRACTS];
    uint32_t num_contracts;

    vena_app_t apps[VENA_MAX_APPS];
    uint32_t num_apps;

    vena_oracle_t oracles[VENA_MAX_ORACLES];
    uint32_t num_oracles;

    vino_ledger_t *ledger;
    vena_language_t default_language;

    /* Supported languages */
    bool languages_supported[VENA_MAX_LANGUAGES];

    /* Runtime stats */
    uint32_t active_apps;
    uint64_t total_gas;
    uint32_t contracts_executed;
} vena_runtime_t;

void vena_init(vena_runtime_t *vr, vino_ledger_t *ledger);
int32_t vena_register_contract(vena_runtime_t *vr, const char *name,
                                const char *code, vena_language_t lang,
                                const char *creator);
int32_t vena_execute_contract(vena_runtime_t *vr, uint32_t contract_id,
                               const char *args, char *result, uint32_t max_result);
int32_t vena_load_app(vena_runtime_t *vr, const char *name, app_type_t type,
                      const char *code, vena_language_t lang);
int32_t vena_start_app(vena_runtime_t *vr, uint32_t app_id);
int32_t vena_stop_app(vena_runtime_t *vr, uint32_t app_id);
int32_t vena_list_apps(vena_runtime_t *vr, uint32_t *ids, uint32_t max_ids);
int32_t vena_register_oracle(vena_runtime_t *vr, const char *name, const char *source);
int32_t vena_update_oracle(vena_runtime_t *vr, uint32_t oracle_id, uint64_t value);
int32_t vena_set_language(vena_runtime_t *vr, vena_language_t lang);
bool vena_is_language_supported(vena_runtime_t *vr, vena_language_t lang);

const char *vena_language_name(vena_language_t lang);

#endif
