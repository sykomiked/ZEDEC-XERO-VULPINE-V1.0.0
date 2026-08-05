/* browser.h — ZEDEC XERO pqOS Web Browser Module
 *
 * Features: HTTP/HTTPS client, HTML parser, CSS renderer, URL navigation,
 * bookmarks, history, tabs, download manager, dragon-themed UI
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC
 */
#ifndef BROWSER_H
#define BROWSER_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"

#define BROWSER_MAX_URL_LEN     2048
#define BROWSER_MAX_TITLE_LEN   256
#define BROWSER_MAX_TABS        16
#define BROWSER_MAX_HISTORY     256
#define BROWSER_MAX_BOOKMARKS   128
#define BROWSER_MAX_PAGE_SIZE   1048576
#define BROWSER_MAX_ELEMENTS    4096

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

/* ===== HTML element ===== */
typedef struct {
    html_elem_type_t type;
    char tag[64];
    char text[1024];
    char attrs[512];
    char href[2048];
    char src[2048];
    char alt[256];
    uint32_t x, y, w, h;
    uint32_t fg_color;
    uint32_t bg_color;
    bool is_link;
    bool is_image;
    bool is_block;
    int32_t font_size;
    bool bold;
    bool italic;
    bool underline;
} html_element_t;

/* ===== HTTP response ===== */
typedef struct {
    uint16_t status_code;
    char status_text[64];
    char headers[4096];
    uint32_t content_length;
    char content_type[128];
    uint8_t *body;
    uint32_t body_len;
    bool chunked;
    bool success;
} http_response_t;

/* ===== Browser tab ===== */
typedef struct {
    uint32_t tab_id;
    bool active;
    char url[BROWSER_MAX_URL_LEN];
    char title[BROWSER_MAX_TITLE_LEN];
    html_element_t elements[BROWSER_MAX_ELEMENTS];
    uint32_t num_elements;
    http_response_t response;
    bool loading;
    bool loaded;
    int32_t scroll_x, scroll_y;
    uint32_t viewport_w, viewport_h;
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
    uint64_t timestamp;
} browser_history_t;

/* ===== Browser (hardware-as-code device) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];

    /* Tabs */
    browser_tab_t tabs[BROWSER_MAX_TABS];
    uint32_t num_tabs;
    uint32_t active_tab;

    /* History */
    browser_history_t history[BROWSER_MAX_HISTORY];
    uint32_t history_count;
    uint32_t history_index;

    /* Bookmarks */
    browser_bookmark_t bookmarks[BROWSER_MAX_BOOKMARKS];
    uint32_t num_bookmarks;

    /* Settings */
    char homepage[BROWSER_MAX_URL_LEN];
    char user_agent[256];
    bool javascript_enabled;
    bool images_enabled;
    bool cookies_enabled;
    uint32_t default_zoom;

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
uint32_t browser_new_tab(browser_t *browser, const char *url);
int browser_close_tab(browser_t *browser, uint32_t tab_id);
int browser_navigate(browser_t *browser, uint32_t tab_id, const char *url);
int browser_back(browser_t *browser, uint32_t tab_id);
int browser_forward(browser_t *browser, uint32_t tab_id);
int browser_refresh(browser_t *browser, uint32_t tab_id);
int browser_stop(browser_t *browser, uint32_t tab_id);
int browser_scroll(browser_t *browser, uint32_t tab_id, int32_t dx, int32_t dy);

/* HTTP */
int http_request(http_response_t *resp, http_method_t method, const char *url,
                 const char *body, uint32_t body_len);
void http_response_free(http_response_t *resp);

/* HTML parsing */
int html_parse(const char *html, uint32_t len, html_element_t *elements, uint32_t max, uint32_t *count);
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

/* Coverage */
bool browser_verify_coverage(browser_t *browser);

#endif /* BROWSER_H */
