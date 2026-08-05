/* apps.h — ZEDEC pqOS Native Applications
 * Shell, Editor, File Manager, System Monitor, Network Config,
 * Wallet, Bank, Exchange, Messaging, Terminal, Calculator, Clock.
 *
 * Each app is a self-contained module with its own state, input
 * handling, and render callbacks that integrate with the GUI.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef APPS_H
#define APPS_H

#include <stdint.h>
#include <stdbool.h>

#include "../vena/vena.h"
#include "../vino/vino.h"
#include "../net/net.h"
#include "../net/m5route.h"
#include "../sched/sched.h"
#include "../vfs/vfs.h"
#include "gui.h"

/* ---- Common app framework ---- */

#define APP_MAX_WINDOWS    16
#define APP_SHELL_LINES    24
#define APP_SHELL_COLS     80
#define APP_SHELL_HISTORY  32
#define APP_EDITOR_LINES   48
#define APP_EDITOR_COLS    96
#define APP_EDITOR_MAX_BUF 4096
#define APP_FMAN_ROWS      20
#define APP_SYSMON_ROWS    24
#define APP_NETCFG_ROWS    20

typedef enum {
    APP_STATE_IDLE = 0,
    APP_STATE_RUNNING = 1,
    APP_STATE_WAITING = 2,
    APP_STATE_ERROR = 3
} app_state_t;

/* Generic app context — each specific app embeds this */
typedef struct app_base {
    app_type_t type;
    app_state_t state;
    uint32_t gui_win;       /* GUI window index */
    bool active;
    char name[32];
} app_base_t;

/* ---- Shell ---- */

typedef struct shell_app {
    app_base_t base;
    char screen[APP_SHELL_LINES][APP_SHELL_COLS + 1];
    uint32_t cursor_row;
    uint32_t cursor_col;
    char input_line[APP_SHELL_COLS];
    uint32_t input_len;
    char history[APP_SHELL_HISTORY][APP_SHELL_COLS];
    uint32_t hist_count;
    uint32_t hist_pos;
    vfs_state_t *vfs;
    net_state_t *net;
    m5_router_t *router;
    vino_ledger_t *vino;
    vena_runtime_t *vena;
    scheduler_t *sched;
} shell_app_t;

void shell_init(shell_app_t *app, uint32_t gui_win,
                vfs_state_t *vfs, net_state_t *net,
                m5_router_t *router, vino_ledger_t *vino,
                vena_runtime_t *vena, scheduler_t *sched);
void shell_handle_key(shell_app_t *app, char ch);
void shell_handle_special(shell_app_t *app, uint8_t scancode);
void shell_render(shell_app_t *app, gui_desktop_t *gui);
void shell_execute(shell_app_t *app);

/* ---- Editor ---- */

typedef struct editor_app {
    app_base_t base;
    char buffer[APP_EDITOR_MAX_BUF];
    uint32_t buf_len;
    uint32_t cursor_pos;
    uint32_t scroll_row;
    uint32_t scroll_col;
    char filename[64];
    bool modified;
    bool insert_mode;
    vfs_state_t *vfs;
} editor_app_t;

void editor_init(editor_app_t *app, uint32_t gui_win, vfs_state_t *vfs);
void editor_handle_key(editor_app_t *app, char ch);
void editor_handle_special(editor_app_t *app, uint8_t scancode);
void editor_render(editor_app_t *app, gui_desktop_t *gui);
int32_t editor_load_file(editor_app_t *app, const char *path);
int32_t editor_save_file(editor_app_t *app);

/* ---- File Manager ---- */

typedef struct fman_app {
    app_base_t base;
    vfs_node_t entries[APP_FMAN_ROWS];
    uint32_t num_entries;
    uint32_t selected;
    uint32_t scroll;
    char current_path[256];
    vfs_state_t *vfs;
} fman_app_t;

void fman_init(fman_app_t *app, uint32_t gui_win, vfs_state_t *vfs);
void fman_handle_key(fman_app_t *app, char ch);
void fman_handle_special(fman_app_t *app, uint8_t scancode);
void fman_refresh(fman_app_t *app);
void fman_render(fman_app_t *app, gui_desktop_t *gui);
void fman_enter(fman_app_t *app);
void fman_up(fman_app_t *app);
void fman_down(fman_app_t *app);

/* ---- System Monitor ---- */

typedef struct sysmon_app {
    app_base_t base;
    uint32_t tick;
    scheduler_t *sched;
    net_state_t *net;
    m5_router_t *router;
    vino_ledger_t *vino;
    vena_runtime_t *vena;
    /* Cached stats */
    uint32_t cpu_usage;
    uint32_t mem_total;
    uint32_t mem_used;
    uint32_t net_rx;
    uint32_t net_tx;
    uint32_t active_tasks;
    uint32_t vino_txns;
    uint32_t vino_peers;
    uint32_t vena_apps;
} sysmon_app_t;

void sysmon_init(sysmon_app_t *app, uint32_t gui_win,
                 scheduler_t *sched, net_state_t *net,
                 m5_router_t *router, vino_ledger_t *vino,
                 vena_runtime_t *vena);
void sysmon_tick(sysmon_app_t *app);
void sysmon_handle_key(sysmon_app_t *app, char ch);
void sysmon_render(sysmon_app_t *app, gui_desktop_t *gui);

/* ---- Network Config ---- */

typedef struct netcfg_app {
    app_base_t base;
    uint32_t selected;
    uint32_t scroll;
    net_state_t *net;
    m5_router_t *router;
    char detail_buf[512];
    uint32_t detail_len;
} netcfg_app_t;

void netcfg_init(netcfg_app_t *app, uint32_t gui_win,
                 net_state_t *net, m5_router_t *router);
void netcfg_handle_key(netcfg_app_t *app, char ch);
void netcfg_handle_special(netcfg_app_t *app, uint8_t scancode);
void netcfg_refresh(netcfg_app_t *app);
void netcfg_render(netcfg_app_t *app, gui_desktop_t *gui);

/* ---- Wallet ---- */

typedef struct wallet_app {
    app_base_t base;
    vino_ledger_t *vino;
    char current_account[64];
    uint32_t selected_capital;
    uint32_t scroll;
} wallet_app_t;

void wallet_init(wallet_app_t *app, uint32_t gui_win, vino_ledger_t *vino);
void wallet_handle_key(wallet_app_t *app, char ch);
void wallet_handle_special(wallet_app_t *app, uint8_t scancode);
void wallet_render(wallet_app_t *app, gui_desktop_t *gui);

/* ---- App launcher — manages all app windows ---- */

typedef struct app_launcher {
    shell_app_t shell;
    editor_app_t editor;
    fman_app_t fman;
    sysmon_app_t sysmon;
    netcfg_app_t netcfg;
    wallet_app_t wallet;

    uint32_t num_windows;
    uint32_t active_app;

    gui_desktop_t *gui;
    vfs_state_t *vfs;
    net_state_t *net;
    m5_router_t *router;
    vino_ledger_t *vino;
    vena_runtime_t *vena;
    scheduler_t *sched;
} app_launcher_t;

void app_launcher_init(app_launcher_t *al, gui_desktop_t *gui,
                       vfs_state_t *vfs, net_state_t *net,
                       m5_router_t *router, vino_ledger_t *vino,
                       vena_runtime_t *vena, scheduler_t *sched);
int32_t app_launcher_open(app_launcher_t *al, app_type_t type);
int32_t app_launcher_close(app_launcher_t *al, app_type_t type);
void app_launcher_handle_key(app_launcher_t *al, char ch, uint8_t scancode);
void app_launcher_render(app_launcher_t *al);
void app_launcher_tick(app_launcher_t *al);

/* Utility */
void app_put_char(char *screen, uint32_t row, uint32_t col,
                  uint32_t max_row, uint32_t max_col, char ch);
void app_put_str(char *screen, uint32_t row, uint32_t col,
                 uint32_t max_row, uint32_t max_col, const char *str);
void app_clear_screen(char *screen, uint32_t max_row, uint32_t max_col);
void app_scroll_up(char *screen, uint32_t max_row, uint32_t max_col);

#endif /* APPS_H */
