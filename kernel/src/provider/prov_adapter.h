/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_adapter.h — the request/response envelope that lets an existing
 * endpoint plug in unchanged (G1 of prov.h).
 *
 * The network carries one canonical, binary ENVELOPE per job (what the user
 * asked for, the privacy flags, the job id). At the edge the adapter renders
 * it into the request body shape the provider declared in its descriptor
 * (prov_api_t.shape) and reads the provider's own response back through the
 * key paths it declared (usage_in, usage_out, text_path). The provider keeps
 * its own API, credentials, terms and prices: the adapter never stores or
 * forwards provider API keys (the host's transport attaches the user's own
 * credentials) and the kernel opens no connection.
 *
 * Envelope encoding (little-endian):
 *   "ZXPE" | u8 version=1 | u8 shape | u8 flags | u8 n_msgs | u8 n_inputs |
 *   u32 max_tokens | job_id[32] | str model | str system | str prompt |
 *   n_msgs x (u8 role | str text) | n_inputs x str input
 * where str = u32 length || bytes (no NUL). Decoding is strict: exact
 * length, known shape and roles, bounded counts, no trailing bytes.
 * Decoded strings point into the input buffer (zero copy).
 *
 * Body shapes (JSON, RFC 8259, written with kernel/src/web4's JSON writer):
 *   MESSAGES   {"model","max_tokens","system"?,"messages":[{"role","content"}]}
 *   CHAT       {"model","messages":[{"role","content"}],"max_tokens"}
 *              (a system text becomes a leading {"role":"system"} message)
 *   COMPLETION {"model","prompt","max_tokens"}
 *   EMBEDDINGS {"model","input":[...]}
 *   RAW        the prompt bytes verbatim
 * These are common public API styles; no particular vendor is implied.
 */
#ifndef ZXV_PROV_ADAPTER_H
#define ZXV_PROV_ADAPTER_H

#include "prov.h"

#define PROV_ENV_MAX_MSGS   16u
#define PROV_ENV_MAX_INPUTS 16u
#define PROV_ENV_STR_MAX    (1u << 20)

typedef enum { PROV_ROLE_USER = 0, PROV_ROLE_ASSISTANT = 1, PROV_ROLE_SYSTEM = 2 } prov_role_t;

#define PROV_EF_NO_TRAIN  0x01u
#define PROV_EF_NO_RETAIN 0x02u

typedef struct {
    const char *p;
    uint32_t n;
} prov_str_t;

typedef struct {
    uint8_t role; /* prov_role_t */
    prov_str_t text;
} prov_msg_t;

typedef struct {
    uint8_t shape; /* prov_shape_t */
    uint8_t flags; /* PROV_EF_*    */
    uint32_t max_tokens;
    uint8_t job_id[PROV_HASH_LEN];
    prov_str_t model; /* empty: use the descriptor's model */
    prov_str_t system;
    prov_str_t prompt; /* COMPLETION, RAW */
    prov_msg_t msgs[PROV_ENV_MAX_MSGS];
    uint8_t n_msgs;
    prov_str_t inputs[PROV_ENV_MAX_INPUTS]; /* EMBEDDINGS */
    uint8_t n_inputs;
} prov_env_t;

prov_str_t prov_s(const char *z); /* from a NUL-terminated string */

int32_t prov_env_encode(const prov_env_t *e, uint8_t *out, uint32_t cap);
int prov_env_decode(const uint8_t *in, uint32_t len, prov_env_t *e);
/* SHA3-256 of the canonical encoding: the job's request_hash. */
int prov_env_hash(const prov_env_t *e, uint8_t *scratch, uint32_t cap, uint8_t out[PROV_HASH_LEN]);

/* Render the request body for `api`. The envelope's shape must equal the
 * declared shape (PROV_ERR_UNSUPPORTED otherwise). Returns bytes written
 * (NUL-terminated for JSON shapes) or negative. */
int32_t prov_adapter_body(const prov_api_t *api, const prov_env_t *e, char *out, uint32_t cap);
/* The provider's declared privacy header line ("Name: 1\r\n") when the
 * envelope has no_train and the provider declared one; else "" (0). */
int32_t prov_adapter_privacy_header(const prov_api_t *api, const prov_env_t *e, char *out,
                                    uint32_t cap);

typedef struct {
    uint64_t units_in, units_out;
    bool has_in, has_out;
    char text[1024]; /* text_path result, truncated to fit */
    bool has_text;
    bool text_truncated;
} prov_resp_t;

/* Parse the provider's JSON response along the declared key paths
 * ("a.b.0.c": object keys and array indices). Missing paths leave the has_
 * flag false. PROV_ERR_PARSE on malformed JSON. */
int prov_adapter_parse(const prov_api_t *api, const char *json, uint32_t len, prov_resp_t *r);
/* Metered units for a token-priced offer: units_in + units_out (checked). */
int prov_adapter_units(const prov_resp_t *r, uint64_t *units);

#endif /* ZXV_PROV_ADAPTER_H */
