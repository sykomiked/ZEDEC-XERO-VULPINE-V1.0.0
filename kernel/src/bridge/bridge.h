/* bridge.h — one resolver across Web2, Web3, and Web4
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * WHAT THIS IS
 * -----------
 * "Seamless Web2/Web3/Web4" is not a new network — it is one ADDRESSING scheme
 * and one RESOLVER over the three that already exist here:
 *
 *   Web2  a host name          resolved by DNS to an IP     (src/net)
 *   Web3  a content address    resolved to a CID/DID digest (src/decent, identity)
 *   Web4  a Chiglet intent     resolved to an intent handle (src/chiglet)
 *
 * A caller hands this module a URI in any of the three forms and gets back a
 * single result type, without needing to know which realm answered it. The
 * scheme classifier decides the realm; the resolver dispatches to that realm's
 * backend. A gateway mapping lets a plain Web2 HTTP path (`/ipfs/<cid>`,
 * `/did/<id>`, `/chiglet/<intent>`) reach a Web3/Web4 resource, so a legacy
 * HTTP client and a content-addressed peer meet without either knowing about
 * the other.
 *
 * The three realms are OPS BOUNDARIES: DNS, the content resolver, and the
 * Chiglet resolver plug in. With a backend unbound, resolution returns
 * NOT_BOUND — the bridge never invents an address it could not resolve.
 *
 * Freestanding: integer only, no allocation.
 */
#ifndef ZXV_BRIDGE_H
#define ZXV_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#define BRIDGE_URI_MAX   256u
#define BRIDGE_CID_LEN   32u

typedef enum {
    REALM_UNKNOWN = 0,
    REALM_WEB2,     /* DNS host                */
    REALM_WEB3,     /* content address / DID   */
    REALM_WEB4      /* Chiglet intent          */
} bridge_realm_t;

typedef enum {
    BR_OK = 0,
    BR_BAD_URI,     /* not a recognisable address in any realm */
    BR_NOT_BOUND,   /* the realm's backend is not installed    */
    BR_NOT_FOUND    /* the backend could not resolve it        */
} bridge_status_t;

typedef struct {
    bridge_realm_t  realm;
    bridge_status_t status;
    uint8_t  ip[4];                 /* WEB2: resolved A record            */
    uint8_t  cid[BRIDGE_CID_LEN];   /* WEB3: content address / DID digest */
    uint32_t intent;                /* WEB4: Chiglet intent handle        */
    char     canonical[BRIDGE_URI_MAX]; /* the extracted host/ref/intent  */
} bridge_result_t;

/* Backends. Each returns 0 on success. Any may be NULL (that realm is then
 * NOT_BOUND). */
typedef struct {
    int (*resolve_dns)(const char *host, uint8_t ip[4], void *ctx);
    int (*resolve_cid)(const char *ref, uint8_t cid[BRIDGE_CID_LEN], void *ctx);
    int (*resolve_intent)(const char *intent, uint32_t *handle, void *ctx);
    void *ctx;
} bridge_ops_t;

typedef struct { bridge_ops_t ops; } bridge_t;

void bridge_init(bridge_t *b);
void bridge_set_ops(bridge_t *b, const bridge_ops_t *ops);

/* Classify a URI into a realm by its scheme/shape, without resolving it. */
bridge_realm_t bridge_classify(const char *uri);

/* Resolve a URI in any realm into a single result. Fills out->realm/status and
 * the realm's payload. */
bridge_status_t bridge_resolve(bridge_t *b, const char *uri, bridge_result_t *out);

/* Gateway: map a Web2 HTTP request path to a Web3/Web4 resource so a legacy
 * client reaches content-addressed / intent resources. Writes the extracted
 * reference (the part a resolver wants) into `ref_out`. A path that is not a
 * gateway path returns REALM_WEB2 (serve it as ordinary HTTP). */
bridge_realm_t bridge_gateway_route(const char *http_path, char *ref_out, uint32_t cap);

const char *bridge_realm_name(bridge_realm_t r);

#endif /* ZXV_BRIDGE_H */
