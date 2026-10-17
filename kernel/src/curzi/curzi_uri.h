/* curzi_uri.h — the 36n9:// addressing standard for the Curzi 8889
 * engine: seed-bank slices, live streams, AIPI sockets and the raw-IPFS
 * intercept. A scheme may start with a digit (RFC 3986 forbids it), so
 * every 36n9:// URI is parsed here, in the kernel, and the fallback for
 * legacy browsers resolves the exact IPFS CID through a Filebase gateway.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0.
 */
#ifndef CURZI_URI_H
#define CURZI_URI_H

#include <stdbool.h>
#include <stdint.h>

#define CURZI_SCHEME "36n9://"
#define CURZI_DEFAULT_PORT 8724u
#define CURZI_CID_MAX 64u

typedef enum {
    CURZI_ROUTE_INVALID = 0,
    CURZI_ROUTE_BANK = 1,   /* 36n9://bank.<id>/<hex_offset>:<frames> */
    CURZI_ROUTE_STREAM = 2, /* 36n9://stream.<node>:<port>/<id>?basis=<bank> */
    CURZI_ROUTE_AIPI = 3,   /* 36n9://aipi.<swarm>:<port>/tensor/<slot> */
    CURZI_ROUTE_IPFS = 4    /* 36n9://ipfs/<cid> — the raw intercept */
} curzi_route_t;

typedef struct {
    curzi_route_t route;
    uint32_t basis_bank_id;
    uint32_t byte_offset;
    uint32_t frame_count;
    uint16_t port;
    uint8_t slot;
    char cid[CURZI_CID_MAX]; /* the ipfs route's CID, "" otherwise */
    char node[24];           /* stream/aipi authority, "" otherwise */
} curzi_uri_t;

/* Parse a 36n9:// URI into `out` (zero allocation). Returns 0, or a
 * negative status. */
int curzi_uri_parse(const char *uri, uint32_t len, curzi_uri_t *out);

/* The legacy-browser fallback: render the URI as an HTTPS URL on a
 * Filebase gateway. A bank route maps to the bank CID path (the caller
 * supplies it); an ipfs route maps to its own CID. Writes at most `cap`
 * bytes into `dst`, returns the length, or a negative status when the
 * URI is not gateway-mappable. */
int curzi_uri_gateway(const curzi_uri_t *u, const char *bank_cid, char *dst, uint32_t cap);

#endif /* CURZI_URI_H */