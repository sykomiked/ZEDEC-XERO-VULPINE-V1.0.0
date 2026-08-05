/* bridge.c — the Web2/Web3/Web4 resolver. See bridge.h. */
#include "bridge.h"

/* ---- tiny string helpers (freestanding) ---- */
static uint32_t bstrlen(const char *s) { uint32_t n = 0; if (s) while (s[n]) n++; return n; }

static bool starts_with(const char *s, const char *pfx) {
    if (!s || !pfx) return false;
    for (uint32_t i = 0; pfx[i]; i++) if (s[i] != pfx[i]) return false;
    return true;
}

static void bcpy(char *d, const char *s, uint32_t cap) {
    uint32_t i = 0; if (s) while (s[i] && i + 1u < cap) { d[i] = s[i]; i++; } if (cap) d[i] = 0;
}

/* copy s starting at offset `from` up to (not incl) first char in `stops` */
static void bcpy_until(char *d, const char *s, uint32_t from, const char *stops, uint32_t cap) {
    uint32_t i = 0;
    for (uint32_t j = from; s[j]; j++) {
        bool stop = false;
        for (uint32_t k = 0; stops[k]; k++) if (s[j] == stops[k]) { stop = true; break; }
        if (stop) break;
        if (i + 1u < cap) d[i++] = s[j];
    }
    if (cap) d[i] = 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse up to `outlen` bytes of hex from s into out. Returns bytes parsed, or
 * 0 if s does not begin with a full `outlen`-byte hex string. */
static uint32_t parse_hex(const char *s, uint8_t *out, uint32_t outlen) {
    for (uint32_t i = 0; i < outlen; i++) {
        int hi = hexval(s[2*i]);
        int lo = (hi < 0) ? -1 : hexval(s[2*i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return outlen;
}

/* ---- lifecycle ---- */
void bridge_init(bridge_t *b) {
    if (!b) return;
    for (uint32_t i = 0; i < sizeof(*b); i++) ((uint8_t *)b)[i] = 0;
}
void bridge_set_ops(bridge_t *b, const bridge_ops_t *ops) {
    if (!b) return;
    if (ops) b->ops = *ops;
    else { b->ops.resolve_dns = 0; b->ops.resolve_cid = 0; b->ops.resolve_intent = 0; b->ops.ctx = 0; }
}

const char *bridge_realm_name(bridge_realm_t r) {
    switch (r) {
        case REALM_WEB2: return "web2";
        case REALM_WEB3: return "web3";
        case REALM_WEB4: return "web4";
        default:         return "unknown";
    }
}

/* ---- classification ----
 * Decide the realm from the scheme/shape alone. Order matters: the Web3/Web4
 * schemes are checked before the "bare host with a dot" heuristic so that
 * `did:zxv:...` is not mistaken for a host name. */
bridge_realm_t bridge_classify(const char *uri) {
    if (!uri || !uri[0]) return REALM_UNKNOWN;
    uint32_t n = bstrlen(uri);
    if (n >= BRIDGE_URI_MAX) return REALM_UNKNOWN;

    /* reject control characters outright (hostile input) */
    for (uint32_t i = 0; i < n; i++)
        if ((uint8_t)uri[i] < 0x20 || (uint8_t)uri[i] == 0x7F) return REALM_UNKNOWN;

    /* WEB4 — an intent for the Chiglet layer */
    if (starts_with(uri, "chiglet:") || starts_with(uri, "intent:") || starts_with(uri, "web4:"))
        return REALM_WEB4;

    /* WEB3 — content address or decentralised identity */
    if (starts_with(uri, "cid:")   || starts_with(uri, "ipfs://") ||
        starts_with(uri, "ipns://")|| starts_with(uri, "bzz://")  ||
        starts_with(uri, "did:")   || starts_with(uri, "web3:"))
        return REALM_WEB3;

    /* WEB2 — explicit HTTP(S) */
    if (starts_with(uri, "http://") || starts_with(uri, "https://"))
        return REALM_WEB2;

    /* a scheme we do not know => not addressable here */
    for (uint32_t i = 0; i + 2 < n; i++)
        if (uri[i] == ':' && uri[i+1] == '/' && uri[i+2] == '/') return REALM_UNKNOWN;

    /* bare "host.tld" with no scheme: treat as Web2 if it looks like a host —
     * has a dot, no spaces, and a plausible label after the last dot */
    bool has_dot = false, has_space = false; uint32_t last_dot = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (uri[i] == '.') { has_dot = true; last_dot = i; }
        if (uri[i] == ' ' || uri[i] == '/') has_space = true;
    }
    if (has_dot && !has_space && last_dot > 0 && last_dot + 1 < n) return REALM_WEB2;

    return REALM_UNKNOWN;
}

/* ---- resolution ---- */
bridge_status_t bridge_resolve(bridge_t *b, const char *uri, bridge_result_t *out) {
    if (!out) return BR_BAD_URI;
    for (uint32_t i = 0; i < sizeof(*out); i++) ((uint8_t *)out)[i] = 0;
    if (!b || !uri) { out->status = BR_BAD_URI; return BR_BAD_URI; }

    bridge_realm_t realm = bridge_classify(uri);
    out->realm = realm;

    switch (realm) {
    case REALM_WEB2: {
        /* extract the host: strip scheme, stop at '/', ':' or '?' */
        uint32_t from = 0;
        if (starts_with(uri, "https://")) from = 8;
        else if (starts_with(uri, "http://")) from = 7;
        bcpy_until(out->canonical, uri, from, "/:?#", BRIDGE_URI_MAX);
        if (!out->canonical[0]) { out->status = BR_BAD_URI; return BR_BAD_URI; }
        if (!b->ops.resolve_dns) { out->status = BR_NOT_BOUND; return BR_NOT_BOUND; }
        if (b->ops.resolve_dns(out->canonical, out->ip, b->ops.ctx) != 0) {
            out->status = BR_NOT_FOUND; return BR_NOT_FOUND;
        }
        out->status = BR_OK; return BR_OK;
    }
    case REALM_WEB3: {
        /* the reference is everything after the scheme delimiter */
        uint32_t from = 0;
        const char *schemes[] = { "ipfs://", "ipns://", "bzz://", "cid:", "web3:", "did:", 0 };
        for (uint32_t s = 0; schemes[s]; s++)
            if (starts_with(uri, schemes[s])) { from = bstrlen(schemes[s]); break; }
        bcpy(out->canonical, uri + from, BRIDGE_URI_MAX);
        if (!out->canonical[0]) { out->status = BR_BAD_URI; return BR_BAD_URI; }
        if (!b->ops.resolve_cid) { out->status = BR_NOT_BOUND; return BR_NOT_BOUND; }
        if (b->ops.resolve_cid(out->canonical, out->cid, b->ops.ctx) != 0) {
            out->status = BR_NOT_FOUND; return BR_NOT_FOUND;
        }
        out->status = BR_OK; return BR_OK;
    }
    case REALM_WEB4: {
        uint32_t from = 0;
        const char *schemes[] = { "chiglet:", "intent:", "web4:", 0 };
        for (uint32_t s = 0; schemes[s]; s++)
            if (starts_with(uri, schemes[s])) { from = bstrlen(schemes[s]); break; }
        bcpy(out->canonical, uri + from, BRIDGE_URI_MAX);
        if (!out->canonical[0]) { out->status = BR_BAD_URI; return BR_BAD_URI; }
        if (!b->ops.resolve_intent) { out->status = BR_NOT_BOUND; return BR_NOT_BOUND; }
        if (b->ops.resolve_intent(out->canonical, &out->intent, b->ops.ctx) != 0) {
            out->status = BR_NOT_FOUND; return BR_NOT_FOUND;
        }
        out->status = BR_OK; return BR_OK;
    }
    default:
        out->status = BR_BAD_URI;
        return BR_BAD_URI;
    }
}

/* ---- gateway: web2 HTTP path -> web3/web4 resource ----
 * A legacy client asks for  GET /ipfs/<cid>   and we route it to Web3.
 *          GET /did/<id>     -> Web3 "did:<id>"
 *          GET /chiglet/<x>  -> Web4 intent <x>
 * Anything else is ordinary Web2 content and is served as-is. */
bridge_realm_t bridge_gateway_route(const char *http_path, char *ref_out, uint32_t cap) {
    if (ref_out && cap) ref_out[0] = 0;
    if (!http_path) return REALM_WEB2;

    if (starts_with(http_path, "/ipfs/") || starts_with(http_path, "/ipns/")) {
        bcpy_until(ref_out, http_path, 6, "?#", cap);
        return ref_out && ref_out[0] ? REALM_WEB3 : REALM_WEB2;
    }
    if (starts_with(http_path, "/did/")) {
        /* re-form as a did: reference */
        if (ref_out && cap) {
            bcpy(ref_out, "did:", cap);
            uint32_t at = bstrlen(ref_out);
            bcpy_until(ref_out + at, http_path, 5, "?#", cap > at ? cap - at : 0);
        }
        return (ref_out && bstrlen(ref_out) > 4) ? REALM_WEB3 : REALM_WEB2;
    }
    if (starts_with(http_path, "/chiglet/")) {
        bcpy_until(ref_out, http_path, 9, "?#", cap);
        return ref_out && ref_out[0] ? REALM_WEB4 : REALM_WEB2;
    }
    return REALM_WEB2;
}

/* exported so a resolver backend can turn a hex ref into a CID without pulling
 * in its own hex parser */
uint32_t bridge_parse_cid_hex(const char *ref, uint8_t cid[BRIDGE_CID_LEN]) {
    return parse_hex(ref, cid, BRIDGE_CID_LEN);
}
