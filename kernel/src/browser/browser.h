/* browser.h — ZEDEC XERO pqOS Web Browser Module
 *
 * Tabs, a back/forward navigation stack, bookmarks, URL parsing, a hostile-
 * input HTML tokenizer, a simple block layout engine, and an HTTP/1.1
 * request builder + response parser.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC
 *
 * ============================ LIMITATIONS ============================
 * Read this before believing anything the names suggest.
 *
 * 1. THERE IS NO SOCKET IN THIS MODULE. ZXV has a TCP stack (src/net) and a
 *    TLS 1.3 record/handshake layer (src/tls), but wiring them up is the job
 *    of a transport shim, not of this file. Everything that must touch a
 *    network goes through http_transport_ops_t. Until http_bind_transport()
 *    is called with a non-NULL ops table:
 *        http_request()        -> BROWSER_ERR_NO_TRANSPORT
 *        browser_navigate()    -> BROWSER_ERR_NO_TRANSPORT (URL still recorded)
 *        browser_refresh()     -> BROWSER_ERR_NO_TRANSPORT
 *    They do NOT return 0, and no tab is ever marked `loaded` without bytes
 *    having actually arrived.
 *
 * 2. What IS real and fully tested, with no hardware anywhere in the path:
 *        browser_parse_url()      RFC 3986-shaped absolute/relative URL split
 *        browser_resolve_url()    relative -> absolute resolution
 *        http_build_request()     byte-exact HTTP/1.1 request serialisation
 *        http_parse_response()    status line, folded headers, Content-Length,
 *                                 and chunked transfer decoding (in place)
 *        html_parse()             the tokenizer (see 3)
 *        html_render()            block layout, exact integer pixel boxes
 *        tabs / history / bookmarks / scroll clamping / coverage
 *
 * 3. html_parse() is a TOKENIZER, not a tree builder. It emits a flat, ordered
 *    token stream (start tags, end tags, text, comments, doctype) with a
 *    nesting `depth` and inherited formatting resolved. It does NOT implement
 *    the HTML5 "insertion mode" machinery: no implied end tags, no foster
 *    parenting, no <table> fixups, no adoption agency. A malformed document
 *    yields a faithful token stream, not a repaired tree.
 *
 * 4. NO CSS. There is no stylesheet cascade. <style> and inline style="" are
 *    captured verbatim as text/attributes and are NOT applied. Formatting
 *    comes only from the tag itself (h1..h6, b/strong, i/em, u, a).
 *    The name "CSS renderer" was in the original banner of this header; it was
 *    never true and has been removed rather than left to mislead.
 *
 * 5. NO JAVASCRIPT. `javascript_enabled` is a stored preference that nothing
 *    reads. <script> bodies are captured as raw text and never executed.
 *
 * 6. NO IMAGE DECODER. `images_enabled` gates whether <img> reserves a box.
 *    Nothing decodes PNG/JPEG; html_element_t.src just holds the URL.
 *
 * 7. NO COOKIE JAR. `cookies_enabled` is stored and unread; Set-Cookie headers
 *    land in http_response_t.headers and are not persisted or resent.
 *
 * 8. NO DOWNLOAD MANAGER. The original banner claimed one. There is none.
 *
 * 9. SIZING. This kernel has no allocator, so every buffer here is fixed and
 *    a browser_t is a large object that MUST live in .bss (static/global) and
 *    MUST NOT be placed on a stack. As declared below it is ~9 MiB:
 *        html_element_t   ~2 KiB
 *        x BROWSER_MAX_ELEMENTS (256) = ~512 KiB of DOM per tab
 *        x BROWSER_MAX_TABS     (16)  = ~8 MiB
 *        + history (256) + bookmarks (128)      = ~0.9 MiB
 *    An earlier revision of this header declared 4096 elements per tab with
 *    2 KiB href/src/text fields, which makes browser_t 375 MiB — a struct no
 *    ZXV target can instantiate, i.e. an API nobody could ever call. The
 *    capacities were cut to numbers that actually fit. Where a field is too
 *    small for its input, the parser sets html_element_t.truncated and, for
 *    href, refuses to mark the element as a navigable link rather than
 *    handing back a silently wrong URL.
 *
 * 10. http_request() receives into a small static pool
 *     (BROWSER_HTTP_POOL_SLOTS x BROWSER_HTTP_RX_BUF bytes). A response larger
 *     than one slot returns BROWSER_ERR_TOO_LARGE. It is never truncated and
 *     reported as success. BROWSER_MAX_PAGE_SIZE is the ceiling html_parse()
 *     accepts from a caller-owned buffer, not what http_request() can fetch.
 * =====================================================================
 */
#ifndef BROWSER_H
#define BROWSER_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"

#define BROWSER_MAX_URL_LEN     2048
#define BROWSER_MAX_TITLE_LEN   256
#define BROWSER_MAX_TABS        16
#define BROWSER_MAX_HISTORY     256
#define BROWSER_MAX_BOOKMARKS   128
#define BROWSER_MAX_PAGE_SIZE   1048576   /* html_parse() input ceiling */
#define BROWSER_MAX_ELEMENTS    256       /* DOM tokens retained per tab */

/* Per-element field capacities. See LIMITATIONS 9. */
#define BROWSER_MAX_TAG_LEN     32
#define BROWSER_MAX_TEXT_LEN    512
#define BROWSER_MAX_ATTRS_LEN   256
#define BROWSER_MAX_HREF_LEN    512
#define BROWSER_MAX_ALT_LEN     128

/* Tokenizer limits. Depth is clamped, not fatal: a document nested deeper
 * than this still tokenizes, it just stops tracking inherited formatting
 * below the clamp. Nothing recurses — the tokenizer is a flat loop, so a
 * 100000-deep document cannot blow the stack. */
#define BROWSER_HTML_MAX_DEPTH  64

/* HTTP receive pool. See LIMITATIONS 10. */
#define BROWSER_HTTP_RX_BUF     65536u
#define BROWSER_HTTP_POOL_SLOTS 2u
#define BROWSER_HTTP_MAX_REQ    4096u     /* request-line + headers + body */

/* Layout metrics — fixed integer arithmetic so layout is bit-reproducible.
 * A glyph cell is font_size/2 wide; a line box is font_size * 1.25 tall. */
#define BROWSER_LAYOUT_MARGIN   8u
#define BROWSER_BLOCK_MARGIN    8u
#define BROWSER_BASE_FONT_PX    16
#define BROWSER_IMG_PLACEHOLDER 64u       /* <img> with no width/height attr */
#define BROWSER_HR_THICKNESS    2u
#define BROWSER_CHAR_W(fs)      ((uint32_t)(fs) / 2u)
#define BROWSER_LINE_H(fs)      ((uint32_t)(fs) + (uint32_t)(fs) / 4u)

/* Coverage floor: r * ell must reach this for browser_verify_coverage().
 * Deliberately below 1.0 so a browser with one unreachable tab out of four
 * still passes, and above 0.5 so half the tabs being broken does not. */
#define BROWSER_COVERAGE_FLOOR  0.75

/* ===== Return codes ===== */
#define BROWSER_OK                0
#define BROWSER_ERR_ARG          -1   /* NULL pointer or out-of-range value */
#define BROWSER_ERR_NOENT        -2   /* no such tab / bookmark / entry */
#define BROWSER_ERR_FULL         -3   /* a fixed table is exhausted */
#define BROWSER_ERR_NO_TRANSPORT -4   /* no HTTP transport bound (see LIM. 1) */
#define BROWSER_ERR_BAD_URL      -5   /* URL did not parse / unsupported scheme */
#define BROWSER_ERR_IO           -6   /* the bound transport reported failure */
#define BROWSER_ERR_TOO_LARGE    -7   /* response exceeds the receive slot */
#define BROWSER_ERR_PROTOCOL     -8   /* malformed HTTP response */
#define BROWSER_ERR_TRUNCATED    -9   /* parse stopped: element table full */
#define BROWSER_ERR_NO_HISTORY   -10  /* nothing to go back/forward to */

const char *browser_strerror(int err);

/* ===== HTTP methods ===== */
typedef enum {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_OPTIONS,
} http_method_t;

/* ===== HTML element types ===== */
typedef enum {
    HTML_DOCTYPE = 0,
    HTML_HTML, HTML_HEAD, HTML_BODY,
    HTML_TITLE, HTML_META, HTML_LINK, HTML_SCRIPT, HTML_STYLE,
    HTML_DIV, HTML_SPAN, HTML_P, HTML_BR, HTML_HR,
    HTML_H1, HTML_H2, HTML_H3, HTML_H4, HTML_H5, HTML_H6,
    HTML_A, HTML_IMG, HTML_TABLE, HTML_TR, HTML_TD, HTML_TH,
    HTML_UL, HTML_OL, HTML_LI, HTML_FORM, HTML_INPUT, HTML_BUTTON,
    HTML_TEXT, HTML_COMMENT, HTML_UNKNOWN,
} html_elem_type_t;

/* ===== Token kind =====
 * html_parse() is a tokenizer; it must be able to say "this is </div>" and
 * not just "this is a div", or a flat element list cannot express nesting.
 * The original struct had no such field, which made every end tag either a
 * lie or a dropped token. */
typedef enum {
    HTML_TOK_DOCTYPE = 0,
    HTML_TOK_START,       /* <div ...>                                     */
    HTML_TOK_SELF_CLOSE,  /* <br/> or a void element like <img>/<hr>/<br>  */
    HTML_TOK_END,         /* </div>                                        */
    HTML_TOK_TEXT,        /* character data (entity-decoded, ws-collapsed) */
    HTML_TOK_COMMENT,     /* <!-- ... -->                                  */
} html_token_kind_t;

/* ===== HTML element ===== */
typedef struct {
    html_token_kind_t kind;
    html_elem_type_t type;
    char tag[BROWSER_MAX_TAG_LEN];
    char text[BROWSER_MAX_TEXT_LEN];
    char attrs[BROWSER_MAX_ATTRS_LEN];   /* "name=value;" pairs, verbatim */
    char href[BROWSER_MAX_HREF_LEN];
    char src[BROWSER_MAX_HREF_LEN];
    char alt[BROWSER_MAX_ALT_LEN];
    /* Boxes. html_render() overwrites all four on every element. html_parse()
     * pre-loads w/h for <img> from its width/height attributes (0 when absent)
     * so the layout has an intrinsic size to honour. */
    uint32_t x, y, w, h;
    uint32_t fg_color;
    uint32_t bg_color;
    bool is_link;      /* <a> whose href fitted in full; see LIMITATIONS 9 */
    bool is_image;
    bool is_block;
    int32_t font_size;
    bool bold;
    bool italic;
    bool underline;
    uint16_t depth;    /* nesting depth, clamped at BROWSER_HTML_MAX_DEPTH */
    bool truncated;    /* some field did not fit; contents are incomplete  */
} html_element_t;

/* ===== URL ===== */
typedef struct {
    char scheme[16];              /* lowercased, no "://"                  */
    char host[256];               /* lowercased; empty for relative URLs   */
    char path[BROWSER_MAX_URL_LEN];  /* always starts '/' for absolute URLs */
    char query[512];              /* after '?', without the '?'            */
    char fragment[256];           /* after '#', without the '#'            */
    uint16_t port;                /* explicit, or scheme default           */
    bool tls;                     /* scheme == "https"                     */
    bool absolute;                /* had a scheme AND an authority         */
    bool valid;                   /* parsed without hitting a hard error   */
} browser_url_t;

/* Split `url` into parts. Returns BROWSER_OK, or BROWSER_ERR_BAD_URL if the
 * input is NULL/empty/over-long or the authority is malformed. On a bad URL
 * `out` is zeroed and out->valid is false — it never returns OK for garbage. */
int browser_parse_url(const char *url, browser_url_t *out);

/* Resolve `ref` against `base` into `out` (a full URL string, cap bytes).
 * Handles absolute refs, scheme-relative ("//h/p"), root-relative ("/p"),
 * query-only ("?q"), fragment-only ("#f") and path-relative ("a/b", "../c")
 * with "." / ".." collapsing. Returns BROWSER_OK or an error. */
int browser_resolve_url(const char *base, const char *ref, char *out, uint32_t cap);

/* ===== HTTP response ===== */
typedef struct {
    uint16_t status_code;
    char status_text[64];
    char headers[4096];        /* raw header block, CRLFs preserved */
    uint32_t content_length;   /* value of the Content-Length header, if any */
    char content_type[128];
    uint8_t *body;             /* points INTO the caller's buffer — borrowed */
    uint32_t body_len;
    bool chunked;
    bool success;              /* true only if a status line parsed AND the
                                * body is complete (see http_parse_response) */
} http_response_t;

/* ===== HTTP transport (the hardware boundary) =====
 * A future shim over src/net/tcp.c + src/tls/record.c fills this in. Every
 * call may fail; a negative return is an error and is propagated, never
 * swallowed. `open` returns a non-negative connection handle. `recv` returns
 * the byte count, or 0 to mean the peer closed the connection. */
typedef struct http_transport_ops {
    int  (*open)(void *ctx, const char *host, uint16_t port, bool tls);
    int  (*send)(void *ctx, int conn, const uint8_t *buf, uint32_t n);
    int  (*recv)(void *ctx, int conn, uint8_t *buf, uint32_t cap);
    void (*close)(void *ctx, int conn);
    void *ctx;
} http_transport_ops_t;

/* Bind (or, with NULL, unbind) the process-wide HTTP transport. */
void http_bind_transport(const http_transport_ops_t *ops);
bool http_transport_bound(void);

/* ===== Browser tab ===== */
typedef struct {
    uint32_t tab_id;
    bool active;                       /* slot is occupied */
    char url[BROWSER_MAX_URL_LEN];
    char title[BROWSER_MAX_TITLE_LEN];
    html_element_t elements[BROWSER_MAX_ELEMENTS];
    uint32_t num_elements;
    http_response_t response;
    bool loading;
    bool loaded;
    int32_t scroll_x, scroll_y;
    uint32_t viewport_w, viewport_h;
    uint32_t content_w, content_h;      /* set by html_render() */
    /* Dragon theme colors */
    uint32_t bg_color;
    uint32_t fg_color;
    uint32_t accent_color;
} browser_tab_t;

/* ===== Bookmark ===== */
typedef struct {
    char title[BROWSER_MAX_TITLE_LEN];
    char url[BROWSER_MAX_URL_LEN];
    uint32_t folder;
} browser_bookmark_t;

/* ===== History entry ===== */
typedef struct {
    char url[BROWSER_MAX_URL_LEN];
    char title[BROWSER_MAX_TITLE_LEN];
    uint64_t timestamp;    /* navigation ordinal, not wall clock: ZXV has no
                            * wall clock in the kernel. Monotone per browser. */
} browser_history_t;

/* ===== Browser (hardware-as-code device) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];

    /* Tabs — a slot array. `active` marks occupancy; num_tabs counts the
     * occupied slots. Never iterate 0..num_tabs; iterate all slots. */
    browser_tab_t tabs[BROWSER_MAX_TABS];
    uint32_t num_tabs;
    uint32_t active_tab;      /* tab_id of the focused tab, 0 = none */
    uint32_t next_tab_id;     /* monotone id allocator */

    /* History — a back/forward NAVIGATION STACK, not an append-only log.
     * history_index is the position of the currently-displayed entry.
     * browser_add_history() truncates everything after history_index, which
     * is the only semantics under which back/forward is correct. */
    browser_history_t history[BROWSER_MAX_HISTORY];
    uint32_t history_count;
    uint32_t history_index;
    uint64_t nav_ordinal;     /* increments once per committed navigation */

    /* Bookmarks */
    browser_bookmark_t bookmarks[BROWSER_MAX_BOOKMARKS];
    uint32_t num_bookmarks;

    /* Settings */
    char homepage[BROWSER_MAX_URL_LEN];
    char user_agent[256];
    bool javascript_enabled;   /* stored, unread — LIMITATIONS 5 */
    bool images_enabled;       /* gates <img> box reservation only */
    bool cookies_enabled;      /* stored, unread — LIMITATIONS 7 */
    uint32_t default_zoom;     /* percent; 100 = 1:1. Scales layout fonts. */

    /* Dragon theme */
    uint32_t theme_bg;       /* Deep black-red */
    uint32_t theme_fg;       /* Gold text */
    uint32_t theme_accent;   /* Dragon red */
    uint32_t theme_link;     /* Fire orange */
    uint32_t theme_header;   /* Dark crimson */

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;
} browser_t;

/* ===== API ===== */
void browser_init(browser_t *browser, const char *name);

/* Returns the new tab_id (>= 1), or 0 if all BROWSER_MAX_TABS slots are in
 * use. `url` may be NULL (blank tab); a non-NULL url is recorded but NOT
 * fetched — call browser_navigate() for that. */
uint32_t browser_new_tab(browser_t *browser, const char *url);
int browser_close_tab(browser_t *browser, uint32_t tab_id);
browser_tab_t *browser_get_tab(browser_t *browser, uint32_t tab_id);

/* Commit a navigation: parse the URL, push it on the navigation stack, then
 * try to fetch it through the bound transport. Returns BROWSER_OK only if
 * bytes actually arrived and the document parsed; otherwise an error code,
 * with the URL still committed so back/forward stays coherent. */
int browser_navigate(browser_t *browser, uint32_t tab_id, const char *url);
int browser_back(browser_t *browser, uint32_t tab_id);
int browser_forward(browser_t *browser, uint32_t tab_id);
int browser_refresh(browser_t *browser, uint32_t tab_id);
int browser_stop(browser_t *browser, uint32_t tab_id);

/* Scroll by (dx,dy), clamped to [0, content - viewport] on each axis using
 * the extents html_render() measured. Returns BROWSER_OK. */
int browser_scroll(browser_t *browser, uint32_t tab_id, int32_t dx, int32_t dy);

/* HTTP */

/* Serialise a request into `out`. Returns the byte count written (> 0) or a
 * negative error. Emits, in order: request line, Host, User-Agent, Accept,
 * Connection: close, Content-Length (only when body_len > 0), CRLF, body. */
int http_build_request(char *out, uint32_t cap, http_method_t method,
                       const browser_url_t *url, const char *user_agent,
                       const char *body, uint32_t body_len);

/* Parse a complete or partial response held in `buf` (which is MUTATED: a
 * chunked body is de-framed in place). resp->body points into `buf` and is
 * only valid while `buf` is. Returns BROWSER_OK when a status line and the
 * full body were recovered; BROWSER_ERR_PROTOCOL for a malformed message;
 * BROWSER_ERR_TRUNCATED when the message is well-formed so far but the body
 * is incomplete (resp->success stays false in that case). */
int http_parse_response(http_response_t *resp, uint8_t *buf, uint32_t len);

/* Copy the value of header `name` (case-insensitive) from an already-parsed
 * response into `out`. Returns the length written, or 0 if absent. */
uint32_t http_get_header(const http_response_t *resp, const char *name,
                         char *out, uint32_t cap);

/* Fetch `url`. Requires a bound transport; returns BROWSER_ERR_NO_TRANSPORT
 * otherwise. On success resp->body points into a static pool slot that
 * http_response_free() returns. */
int http_request(http_response_t *resp, http_method_t method, const char *url,
                 const char *body, uint32_t body_len);

/* Release the pool slot resp->body borrows (if any) and zero resp. There is
 * no allocator in ZXV; this frees a slot, not memory. Safe on a response
 * whose body points at caller memory, and safe to call twice. */
void http_response_free(http_response_t *resp);

/* HTML parsing */

/* Tokenize `len` bytes of `html` into at most `max` elements, writing the
 * count to *count. Reads only html[0..len-1]; `html` need not be NUL
 * terminated and may contain NUL bytes. Returns BROWSER_OK, or
 * BROWSER_ERR_TRUNCATED if the element table filled before the input ended
 * (the elements written are still valid), or BROWSER_ERR_ARG on bad
 * arguments / len > BROWSER_MAX_PAGE_SIZE. */
int html_parse(const char *html, uint32_t len, html_element_t *elements,
               uint32_t max, uint32_t *count);

/* Map a tag name (case-insensitive) to its element type. */
html_elem_type_t html_tag_type(const char *tag);

/* Lay the tab's token stream out as integer pixel boxes for the given
 * viewport, filling x/y/w/h on every element and content_w/content_h on the
 * tab. Deterministic: same tokens + same viewport => same pixels. */
int html_render(browser_tab_t *tab, uint32_t viewport_w, uint32_t viewport_h);

/* Bookmarks */
int browser_add_bookmark(browser_t *browser, const char *title, const char *url);
int browser_remove_bookmark(browser_t *browser, uint32_t index);
browser_bookmark_t *browser_get_bookmarks(browser_t *browser, uint32_t *count);

/* History */
int browser_add_history(browser_t *browser, const char *url, const char *title);
browser_history_t *browser_get_history(browser_t *browser, uint32_t *count);
int browser_clear_history(browser_t *browser);

/* Settings */
void browser_set_homepage(browser_t *browser, const char *url);
void browser_set_dragon_theme(browser_t *browser);

/* Coverage
 * r    = fraction of open tabs whose recorded URL parses to something we can
 *        actually address (absolute, with a host).
 * ell  = fraction of open tabs whose bookkeeping is self-consistent, i.e.
 *        loaded == (num_elements > 0). A tab flagged loaded with an empty DOM
 *        is exactly the kind of lie this function exists to catch.
 * Returns (r * ell) >= BROWSER_COVERAGE_FLOOR. An empty browser has nothing
 * to cover and passes. This CAN return false — test_browser.c has three
 * cases that make it do so. */
bool browser_verify_coverage(browser_t *browser);

#endif /* BROWSER_H */
