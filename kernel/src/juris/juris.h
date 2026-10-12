/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* juris.h — the jurisdiction plug-in ABI.
 *
 * Rules that differ by country (capital controls, tax rates, which
 * accounting framework statutory reports follow, whether the open market
 * may run) live in plug-ins, not in the core. A host loads the plug-ins for
 * the jurisdiction zxvd's settings name and asks them; every answer is a
 * 64-state tag (lpres_hex.h), so missing or disputed answers fail closed the
 * same way the V-Bill hub's rules do.
 *
 *   J1 SETTINGS. juris_settings_t is zxvd's jurisdiction.get result (PR #69,
 *      zxvd_rpc.h J1-J5): market "closed" (default) or "open", standard
 *      ("" or one of acct.h's names), country (ISO 3166-1 alpha-2 or ""),
 *      in_effect. juris_settings_parse reads that JSON object strictly: a
 *      wrong type, a bad value or a repeated key refuses; keys it does not
 *      know are skipped; a key left out keeps zxvd's default.
 *   J2 CONTRACT. A plug-in is a juris_plugin_t: abi = JURIS_PLUGIN_ABI,
 *      size = sizeof(juris_plugin_t) or more (later fields are added at the
 *      end), an id, the country it serves, a hook mask and the hooks. A
 *      plug-in from a newer major ABI, or one smaller than this header's
 *      struct, is refused. Hooks never write the ledger; they answer.
 *   J3 SIGNED MANIFEST. Registration needs a manifest signed by the
 *      deployment's jurisdiction gateway key over
 *        "zxv/juris-plugin/v1" || id[32] || country[2] || hooks u32 ||
 *        version u64 || sha256(image)[32]           (little-endian)
 *      checked by the host's verify hook (ML-DSA-87 in production). The
 *      host hashes the plug-in image it was handed, and the plug-in's hook
 *      mask must equal the signed one. A version not above the last one
 *      registered for the same id is refused (no rollback).
 *   J4 SETTINGS GATE. Only plug-ins for the settings' country register.
 *      juris_host_apply_settings with a new country unloads the others. No
 *      country chosen: every question waits.
 *   J5 FAIL CLOSED. Each question goes to every loaded plug-in that has the
 *      hook, and the tags combine with lpx_and (worst wins). No plug-in with
 *      the hook answers NEITHER (wait). Asking for the open market while the
 *      settings say closed is FALSE without asking anyone. Tax rates must
 *      agree: two plug-ins giving different rates is BOTH (escalate to a
 *      person). A forbidden tag goes to the quarantine log; a full log halts
 *      the host.
 *   J6 V-BILL HUB. juris_vhub.h: juris_vhub_rule lets a host stand as one
 *      vhub rule, and juris_limit_plugin wraps vhub's ready-made limit rule
 *      as a plug-in, the first plug-in written to this ABI. It is a separate
 *      file so that a host (zxvd) can embed juris.c without the hub.
 *   J7 ACCOUNTING. juris_host_standard gives the framework reports follow:
 *      the settings' standard when acct.h builds it and no loaded plug-in
 *      refuses it.
 *
 * HONEST LIMITS. This is the in-process contract and the fail-closed host.
 * Loading plug-in code (a shared object, a Z-Wasm module in the SDK's
 * sandbox) is the embedding process's job, and the image hash is only as
 * good as the bytes the loader passes in. A plug-in's answer is the rule its
 * author wrote: it is not legal or tax advice, and a deployment needs local
 * counsel and a tax adviser to sign off on each plug-in. zxvd reports
 * in_effect=false until it wires these settings to the host. Freestanding
 * C11: no allocation, no float.
 */
#ifndef ZXV_JURIS_H
#define ZXV_JURIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../acct/acct.h"
#include "../lpres/lpres_hex.h"

/* ---- J1 ---- */
typedef enum { JURIS_MARKET_CLOSED = 0, JURIS_MARKET_OPEN = 1 } juris_market_t;
typedef struct {
    uint8_t market;   /* juris_market_t */
    uint8_t standard; /* acct_std_t */
    char country[3];  /* "KE", or "" */
    bool in_effect;
} juris_settings_t;

typedef enum {
    JURIS_OK = 0,
    JURIS_E_ARG = -1,
    JURIS_E_PARSE = -2,   /* J1 */
    JURIS_E_ABI = -3,     /* J2 */
    JURIS_E_SIG = -4,     /* J3 */
    JURIS_E_HASH = -5,    /* J3 */
    JURIS_E_HOOKS = -6,   /* J3 */
    JURIS_E_VERSION = -7, /* J3 */
    JURIS_E_COUNTRY = -8, /* J4 */
    JURIS_E_FULL = -9,
    JURIS_E_NO_RULE = -10,  /* J5: nobody answers */
    JURIS_E_CONFLICT = -11, /* J5: plug-ins disagree */
    JURIS_E_HALTED = -12    /* J5: quarantine log full */
} juris_status_t;

void juris_settings_default(juris_settings_t *s);
juris_status_t juris_settings_parse(const char *json, size_t len, juris_settings_t *out);

/* ---- J2 ---- */
#define JURIS_PLUGIN_ABI_MAJOR 1u
#define JURIS_PLUGIN_ABI_MINOR 0u
#define JURIS_PLUGIN_ABI       ((JURIS_PLUGIN_ABI_MAJOR << 16) | JURIS_PLUGIN_ABI_MINOR)
#define JURIS_ID_LEN           32u

#define JURIS_HOOK_TRANSFER 0x01u
#define JURIS_HOOK_MARKET   0x02u
#define JURIS_HOOK_TAX      0x04u
#define JURIS_HOOK_STANDARD 0x08u
#define JURIS_HOOK_COMMIT   0x10u
#define JURIS_HOOK_ALL      0x1Fu

typedef enum {
    JURIS_TAX_INCOME = 0, /* corporate income tax */
    JURIS_TAX_VAT = 1,    /* VAT / GST standard rate */
    JURIS_TAX_WITHHOLDING = 2,
    JURIS_TAX_COUNT = 3
} juris_tax_kind_t;

typedef struct {
    char from_country[3], to_country[3]; /* "" when unknown */
    char ccy[4];                         /* ISO 4217 alpha or "" */
    uint64_t amount;                     /* minor units */
    uint32_t sender, receiver;           /* ledger accounts */
    uint64_t epoch;
} juris_transfer_t;

typedef struct juris_plugin {
    uint32_t abi;          /* JURIS_PLUGIN_ABI */
    uint32_t size;         /* sizeof(juris_plugin_t) */
    char id[JURIS_ID_LEN]; /* NUL-padded */
    char country[3];
    uint32_t hooks; /* JURIS_HOOK_* this plug-in provides */
    void *ctx;
    lpx_t (*transfer)(void *ctx, const juris_transfer_t *t);
    lpx_t (*market)(void *ctx, juris_market_t m);
    /* TRUE with the rate in num and den, FALSE (no such tax), NEITHER (unknown) */
    lpx_t (*tax_rate)(void *ctx, juris_tax_kind_t k, uint64_t *num, uint64_t *den);
    lpx_t (*standard)(void *ctx, acct_std_t s);
    void (*commit)(void *ctx, const juris_transfer_t *t);
} juris_plugin_t;

/* ---- J3 ---- */
#define JURIS_MANIFEST_MSG (19u + JURIS_ID_LEN + 2u + 4u + 8u + 32u)
typedef struct {
    char id[JURIS_ID_LEN];
    char country[3];
    uint32_t hooks;
    uint64_t version;
    uint8_t image_hash[32];
    const uint8_t *sig;
    uint32_t sig_len;
} juris_manifest_t;
void juris_manifest_msg(const juris_manifest_t *m, uint8_t out[JURIS_MANIFEST_MSG]);

/* verify(ctx, msg, len, sig, sig_len): true when sig is the gateway key's */
typedef bool (*juris_verify_fn)(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig,
                                uint32_t sig_len);

/* ---- the host ---- */
#define JURIS_MAX_PLUGINS 8u
#define JURIS_MAX_IDS     16u
typedef struct {
    juris_settings_t set;
    const juris_plugin_t *plug[JURIS_MAX_PLUGINS];
    uint32_t n;
    /* J3: last version per id, kept across unloads */
    char ver_id[JURIS_MAX_IDS][JURIS_ID_LEN];
    uint64_t ver[JURIS_MAX_IDS];
    uint32_t n_ver;
    juris_verify_fn verify;
    void *vctx;
    lpx_qlog_t qlog;
    bool halted;
    lpx_t last_tag;
} juris_host_t;

void juris_host_init(juris_host_t *h, const juris_settings_t *s, juris_verify_fn verify, void *vctx,
                     lpx_qrec_t *qstore, uint32_t qcap);
/* J4: take new settings; plug-ins for another country are unloaded. */
juris_status_t juris_host_apply_settings(juris_host_t *h, const juris_settings_t *s);
/* J2/J3/J4: image/image_len are the plug-in's code bytes as loaded. */
juris_status_t juris_host_register(juris_host_t *h, const juris_plugin_t *p,
                                   const juris_manifest_t *m, const uint8_t *image,
                                   size_t image_len);

/* J5: the combined tag; *action (optional) is lpx_action(tag). */
lpx_t juris_check_transfer(juris_host_t *h, const juris_transfer_t *t);
/* Tell every commit hook a transfer posted (keeps limits' tallies). */
void juris_commit_transfer(juris_host_t *h, const juris_transfer_t *t);
lpx_t juris_check_market(juris_host_t *h, juris_market_t m);
juris_status_t juris_host_tax_rate(juris_host_t *h, juris_tax_kind_t k, uint64_t *num,
                                   uint64_t *den);
/* J7: ACCT_STD_NONE when none applies (not chosen, not built or refused). */
acct_std_t juris_host_standard(juris_host_t *h);

#endif /* ZXV_JURIS_H */
