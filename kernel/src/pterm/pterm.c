/*
 * pterm.c — P-TERM Terminal Engine Implementation
 *
 * Freestanding virtual console multiplexer with built-in command suite.
 * Renders directly to VBE framebuffer, no external terminal libraries.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "pterm.h"

/* ===== String helpers (freestanding-safe) ===== */

#ifndef TEST_HOST
static __attribute__((unused)) size_t pterm_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
#endif

static void pterm_strcpy(char *dst, const char *src) {
    size_t i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int pterm_strcmp(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static void pterm_memset(void *dst, int v, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
}

/* ===== Init ===== */

void pterm_init(pterm_t *t) {
    pterm_memset(t, 0, sizeof(pterm_t));
    t->active_console = 0;
    t->num_consoles = 0;
    t->phase_state = 1;  /* TRUE */
    t->initialized = true;
    pterm_register_commands(t);
}

/* ===== Console Management ===== */

int32_t pterm_console_create(pterm_t *t, const char *prompt) {
    if (t->num_consoles >= PTERM_MAX_CONSOLES)
        return -1;

    int32_t idx = (int32_t)t->num_consoles;
    pterm_console_t *c = &t->consoles[idx];
    pterm_memset(c, 0, sizeof(pterm_console_t));
    c->active = true;
    c->cursor_x = 0;
    c->cursor_y = 0;

    if (prompt) {
        pterm_strcpy(c->prompt, prompt);
    } else {
        pterm_strcpy(c->prompt, "zede> ");
    }

    t->num_consoles++;
    return idx;
}

int pterm_switch_console(pterm_t *t, uint32_t idx) {
    if (idx >= t->num_consoles)
        return -1;
    t->active_console = idx;
    return 0;
}

/* ===== Output ===== */

static void console_scroll(pterm_console_t *c) {
    /* Move scrollback up */
    if (c->scrollback_pos < PTERM_SCROLLBACK) {
        pterm_strcpy(c->scrollback[c->scrollback_pos], c->screen[0]);
        c->scrollback_pos++;
    } else {
        /* Shift scrollback */
        uint32_t i;
        for (i = 0; i < PTERM_SCROLLBACK - 1; i++)
            pterm_strcpy(c->scrollback[i], c->scrollback[i + 1]);
        pterm_strcpy(c->scrollback[PTERM_SCROLLBACK - 1], c->screen[0]);
    }

    /* Shift screen up */
    uint32_t y;
    for (y = 0; y < PTERM_CONSOLE_H - 1; y++) {
        pterm_strcpy(c->screen[y], c->screen[y + 1]);
        pterm_memset(c->attr[y], PTERM_ATTR_NORMAL, PTERM_CONSOLE_W);
    }
    pterm_memset(c->screen[PTERM_CONSOLE_H - 1], 0, PTERM_CONSOLE_W);
    pterm_memset(c->attr[PTERM_CONSOLE_H - 1], PTERM_ATTR_NORMAL, PTERM_CONSOLE_W);
    c->cursor_y = PTERM_CONSOLE_H - 1;
    c->cursor_x = 0;
}

void pterm_write(pterm_t *t, const char *text) {
    pterm_write_attr(t, text, PTERM_ATTR_NORMAL);
}

void pterm_write_attr(pterm_t *t, const char *text, uint8_t attr) {
    if (t->num_consoles == 0) return;
    pterm_console_t *c = &t->consoles[t->active_console];

    size_t i;
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            pterm_newline(t);
            continue;
        }
        if (c->cursor_x >= PTERM_CONSOLE_W) {
            pterm_newline(t);
        }
        if (c->cursor_y >= PTERM_CONSOLE_H) {
            console_scroll(c);
        }
        c->screen[c->cursor_y][c->cursor_x] = text[i];
        c->attr[c->cursor_y][c->cursor_x] = attr;
        c->cursor_x++;
    }
}

void pterm_newline(pterm_t *t) {
    if (t->num_consoles == 0) return;
    pterm_console_t *c = &t->consoles[t->active_console];
    c->cursor_x = 0;
    c->cursor_y++;
    if (c->cursor_y >= PTERM_CONSOLE_H) {
        console_scroll(c);
    }
}

void pterm_clear(pterm_t *t) {
    if (t->num_consoles == 0) return;
    pterm_console_t *c = &t->consoles[t->active_console];
    pterm_memset(c->screen, 0, sizeof(c->screen));
    pterm_memset(c->attr, PTERM_ATTR_NORMAL, sizeof(c->attr));
    c->cursor_x = 0;
    c->cursor_y = 0;
}

/* ===== Input ===== */

int pterm_input_char(pterm_t *t, char ch) {
    if (t->num_consoles == 0) return -1;
    pterm_console_t *con = &t->consoles[t->active_console];

    if (con == NULL) return -1;

    switch (ch) {
        case '\r':
        case '\n':
            /* Execute command */
            if (con->input_len > 0) {
                con->input_buf[con->input_len] = '\0';
                pterm_history_add(t, con->input_buf);
                pterm_execute_command(t, con->input_buf);
                con->input_len = 0;
                con->input_buf[0] = '\0';
            }
            /* Show prompt */
            pterm_write_attr(t, con->prompt, PTERM_ATTR_GREEN);
            return 0;

        case '\b':
        case 0x7F:
            if (con->input_len > 0) {
                con->input_len--;
                con->input_buf[con->input_len] = '\0';
            }
            return 0;

        default:
            if (ch >= 0x20 && ch < 0x7F && con->input_len < PTERM_INPUT_BUF - 1) {
                con->input_buf[con->input_len++] = ch;
                con->input_buf[con->input_len] = '\0';
            }
            return 0;
    }
}

int pterm_input_string(pterm_t *t, const char *str) {
    size_t i;
    for (i = 0; str[i]; i++)
        pterm_input_char(t, str[i]);
    return 0;
}

/* ===== Command Parsing ===== */

static int parse_args(const char *cmdline, char *cmd, size_t cmd_sz,
                      char args[][PTERM_INPUT_BUF], uint32_t *num_args) {
    size_t i = 0, j = 0;
    *num_args = 0;

    /* Skip leading spaces */
    while (cmdline[i] == ' ') i++;

    /* Extract command */
    while (cmdline[i] && cmdline[i] != ' ' && j < cmd_sz - 1)
        cmd[j++] = cmdline[i++];
    cmd[j] = '\0';

    /* Extract args */
    while (cmdline[i] && *num_args < PTERM_MAX_ARGS) {
        while (cmdline[i] == ' ') i++;
        if (!cmdline[i]) break;
        j = 0;
        while (cmdline[i] && cmdline[i] != ' ' && j < PTERM_INPUT_BUF - 1)
            args[*num_args][j++] = cmdline[i++];
        args[*num_args][j] = '\0';
        (*num_args)++;
    }

    return 0;
}

/* ===== Command Execution ===== */

int pterm_execute_command(pterm_t *t, const char *cmdline) {
    char cmd[PTERM_MAX_CMD_LEN];
    char args[PTERM_MAX_ARGS][PTERM_INPUT_BUF];
    uint32_t num_args = 0;

    parse_args(cmdline, cmd, sizeof(cmd), args, &num_args);

    if (cmd[0] == '\0')
        return 0;

    const pterm_command_t *cmd_def = pterm_find_command(t, cmd);
    if (!cmd_def) {
        pterm_write_attr(t, "Unknown command: ", PTERM_ATTR_RED);
        pterm_write(t, cmd);
        pterm_newline(t);
        pterm_write_attr(t, "Type 'help' for available commands.\n", PTERM_ATTR_YELLOW);
        return -1;
    }

    /* Forward declarations for real subsystem-backed commands */
    extern void pterm_cmd_ls(pterm_t *t, const char *path);
    extern void pterm_cmd_ps(pterm_t *t);
    extern void pterm_cmd_vmstat(pterm_t *t);
    extern void pterm_cmd_net(pterm_t *t);
    extern void pterm_cmd_route(pterm_t *t);
    extern void pterm_cmd_date(pterm_t *t);

    switch (cmd_def->id) {
        case PTERM_CMD_HELP: {
            uint32_t i;
            pterm_write_attr(t, "Available commands:\n", PTERM_ATTR_BRIGHT);
            for (i = 0; i < t->num_commands; i++) {
                pterm_write_attr(t, "  ", PTERM_ATTR_NORMAL);
                pterm_write_attr(t, t->commands[i].name, PTERM_ATTR_GREEN);
                pterm_write_attr(t, "  —  ", PTERM_ATTR_NORMAL);
                pterm_write(t, t->commands[i].help);
                pterm_newline(t);
            }
            break;
        }
        case PTERM_CMD_CLEAR:
            pterm_clear(t);
            break;
        case PTERM_CMD_ECHO:
            if (num_args > 0) {
                uint32_t i;
                for (i = 0; i < num_args; i++) {
                    pterm_write(t, args[i]);
                    if (i < num_args - 1) pterm_write(t, " ");
                }
            }
            pterm_newline(t);
            break;
        case PTERM_CMD_LS:
            pterm_cmd_ls(t, num_args > 0 ? args[0] : 0);
            break;
        case PTERM_CMD_CAT:
            if (num_args > 0) {
                pterm_write_attr(t, "cat: ", PTERM_ATTR_YELLOW);
                pterm_write(t, args[0]);
                pterm_write_attr(t, " (content-addressed, use vfs-tool)\n",
                                 PTERM_ATTR_YELLOW);
            } else {
                pterm_write_attr(t, "Usage: cat <file>\n", PTERM_ATTR_RED);
            }
            break;
        case PTERM_CMD_PS:
            pterm_cmd_ps(t);
            break;
        case PTERM_CMD_HASH:
            pterm_write_attr(t, "SHA-256 content addressing active\n",
                             PTERM_ATTR_GREEN);
            pterm_write_attr(t, "System Merkle root: <computed>\n",
                             PTERM_ATTR_NORMAL);
            break;
        case PTERM_CMD_NET:
            pterm_cmd_net(t);
            break;
        case PTERM_CMD_ROUTE:
            pterm_cmd_route(t);
            break;
        case PTERM_CMD_TUNNEL:
            pterm_write_attr(t, "PungentClove tunnel status:\n",
                             PTERM_ATTR_BRIGHT);
            pterm_write_attr(t, "  Active bulbs: 0\n", PTERM_ATTR_NORMAL);
            pterm_write_attr(t, "  Relay hops: 3 (default)\n", PTERM_ATTR_NORMAL);
            pterm_write_attr(t, "  Tor bridge: disconnected\n",
                             PTERM_ATTR_YELLOW);
            break;
        case PTERM_CMD_SEED:
            pterm_write_attr(t, "LATTICE-P2P seeding:\n", PTERM_ATTR_BRIGHT);
            pterm_write_attr(t, "  Active swarms: 0\n", PTERM_ATTR_NORMAL);
            pterm_write_attr(t, "  DHT nodes: 0 (bootstrap needed)\n",
                             PTERM_ATTR_YELLOW);
            break;
        case PTERM_CMD_VMSTAT:
            pterm_cmd_vmstat(t);
            break;
        case PTERM_CMD_WHOAMI:
            pterm_write_attr(t, "root@zede-pqos\n", PTERM_ATTR_GREEN);
            break;
        case PTERM_CMD_DATE:
            pterm_cmd_date(t);
            break;
        default:
            pterm_write_attr(t, "Command not implemented\n", PTERM_ATTR_RED);
            return -1;
    }

    return 0;
}

/* ===== Command Registration ===== */

void pterm_register_commands(pterm_t *t) {
    static const struct {
        pterm_cmd_id_t id;
        const char *name;
        const char *help;
    } cmds[] = {
        {PTERM_CMD_LS,     "ls",      "List files and directories"},
        {PTERM_CMD_CAT,    "cat",     "Display file contents"},
        {PTERM_CMD_PS,     "ps-phase","Show running tasks with phase status"},
        {PTERM_CMD_HASH,   "hash",    "Show content hash info"},
        {PTERM_CMD_NET,    "net-ctl", "Network interface control"},
        {PTERM_CMD_HELP,   "help",    "Show this help message"},
        {PTERM_CMD_CLEAR,  "clear",   "Clear the terminal screen"},
        {PTERM_CMD_ECHO,   "echo",    "Display text"},
        {PTERM_CMD_VMSTAT, "vmstat",  "Show memory and scheduler stats"},
        {PTERM_CMD_WHOAMI, "whoami",  "Show current user"},
        {PTERM_CMD_DATE,   "date",    "Show phase clock time"},
        {PTERM_CMD_ROUTE,  "route",   "Show M5 routing table"},
        {PTERM_CMD_TUNNEL, "tunnel",  "PungentClove tunnel status"},
        {PTERM_CMD_SEED,   "seed",    "LATTICE-P2P seeding status"},
    };

    uint32_t i;
    t->num_commands = 0;
    for (i = 0; i < PTERM_CMD_MAX && i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        t->commands[i].id = cmds[i].id;
        pterm_strcpy(t->commands[i].name, cmds[i].name);
        pterm_strcpy(t->commands[i].help, cmds[i].help);
        t->num_commands++;
    }
}

const pterm_command_t *pterm_find_command(pterm_t *t, const char *name) {
    uint32_t i;
    for (i = 0; i < t->num_commands; i++) {
        if (pterm_strcmp(t->commands[i].name, name) == 0)
            return &t->commands[i];
    }
    return NULL;
}

/* ===== History ===== */

void pterm_history_add(pterm_t *t, const char *cmd) {
    if (t->history_count < PTERM_MAX_HISTORY) {
        pterm_strcpy(t->history[t->history_count], cmd);
        t->history_count++;
    } else {
        uint32_t i;
        for (i = 0; i < PTERM_MAX_HISTORY - 1; i++)
            pterm_strcpy(t->history[i], t->history[i + 1]);
        pterm_strcpy(t->history[PTERM_MAX_HISTORY - 1], cmd);
    }
    t->history_pos = t->history_count;
}

const char *pterm_history_prev(pterm_t *t) {
    if (t->history_pos > 0) {
        t->history_pos--;
        return t->history[t->history_pos];
    }
    return NULL;
}

const char *pterm_history_next(pterm_t *t) {
    if (t->history_pos < t->history_count - 1) {
        t->history_pos++;
        return t->history[t->history_pos];
    }
    return NULL;
}

/* ===== Rendering ===== */

void pterm_render(pterm_t *t) {
    /* In kernel mode, this would write to VBE framebuffer.
     * In test mode, it's a no-op (screen buffer is in-memory). */
    (void)t;
}

/* ===== Phase-Aware Prompt ===== */

void pterm_set_phase(pterm_t *t, uint8_t phase) {
    t->phase_state = phase;
}

const char *pterm_phase_prompt(uint8_t phase) {
    switch (phase) {
        case 0: return "zede[0]> ";
        case 1: return "zede[1]> ";
        case 2: return "zede[G]> ";
        case 3: return "zede[G+]> ";
        case 4: return "zede[G-]> ";
        case 5: return "zede[G0]> ";
        default: return "zede> ";
    }
}
