/*
 * pterm.h — P-TERM Terminal Engine
 *
 * Freestanding TTY / virtual console multiplexer built directly on
 * the framebuffer and serial UART drivers. No ncurses/termios dependency.
 *
 * Features:
 *   - Multiple virtual console buffers (workspaces)
 *   - Built-in command suite (ls, cat, ps, hash, net, help)
 *   - Phase-aware prompt showing active trit state
 *   - Scrollback buffer per console
 *   - Direct VBE framebuffer rendering
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: ZEDEC Open Patent License 1.0
 */

#ifndef ZEDEC_PTERM_H
#define ZEDEC_PTERM_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define PTERM_MAX_CONSOLES    8
#define PTERM_CONSOLE_W      80
#define PTERM_CONSOLE_H      25
#define PTERM_SCROLLBACK     200
#define PTERM_INPUT_BUF      256
#define PTERM_MAX_ARGS       16
#define PTERM_MAX_CMD_LEN    64
#define PTERM_MAX_HISTORY    32

/* ===== Console Buffer ===== */

typedef struct {
    char     screen[PTERM_CONSOLE_H][PTERM_CONSOLE_W];
    uint8_t  attr[PTERM_CONSOLE_H][PTERM_CONSOLE_W];  /* Color attribute */
    char     scrollback[PTERM_SCROLLBACK][PTERM_CONSOLE_W];
    uint32_t scrollback_pos;
    uint32_t cursor_x;
    uint32_t cursor_y;
    char     input_buf[PTERM_INPUT_BUF];
    uint32_t input_len;
    bool     active;
    char     prompt[32];
} pterm_console_t;

/* ===== Command Definition ===== */

typedef enum {
    PTERM_CMD_LS      = 0,
    PTERM_CMD_CAT     = 1,
    PTERM_CMD_PS      = 2,
    PTERM_CMD_HASH    = 3,
    PTERM_CMD_NET     = 4,
    PTERM_CMD_HELP    = 5,
    PTERM_CMD_CLEAR   = 6,
    PTERM_CMD_ECHO    = 7,
    PTERM_CMD_VMSTAT  = 8,
    PTERM_CMD_WHOAMI  = 9,
    PTERM_CMD_DATE    = 10,
    PTERM_CMD_ROUTE   = 11,
    PTERM_CMD_TUNNEL  = 12,
    PTERM_CMD_SEED    = 13,
    PTERM_CMD_MAX     = 14,
} pterm_cmd_id_t;

typedef struct {
    pterm_cmd_id_t id;
    char           name[PTERM_MAX_CMD_LEN];
    char           help[128];
} pterm_command_t;

/* ===== Terminal State ===== */

typedef struct {
    pterm_console_t consoles[PTERM_MAX_CONSOLES];
    uint32_t        active_console;
    uint32_t        num_consoles;
    pterm_command_t commands[PTERM_CMD_MAX];
    uint32_t        num_commands;
    char            history[PTERM_MAX_HISTORY][PTERM_INPUT_BUF];
    uint32_t        history_count;
    uint32_t        history_pos;
    bool            initialized;
    /* Phase-aware prompt: shows current trit state */
    uint8_t         phase_state;  /* 0=FALSE, 1=TRUE, 2=GLUT, 3=GLUT+, 4=GLUT-, 5=GLUT0 */
} pterm_t;

/* ===== Color Attributes ===== */

#define PTERM_ATTR_NORMAL     0x07  /* White on black */
#define PTERM_ATTR_BRIGHT     0x0F  /* Bright white */
#define PTERM_ATTR_GREEN      0x0A
#define PTERM_ATTR_CYAN       0x0B
#define PTERM_ATTR_YELLOW     0x0E
#define PTERM_ATTR_RED        0x0C
#define PTERM_ATTR_MAGENTA    0x0D
#define PTERM_ATTR_BLUE       0x09

/* ===== API ===== */

void pterm_init(pterm_t *t);
int32_t pterm_console_create(pterm_t *t, const char *prompt);
int pterm_switch_console(pterm_t *t, uint32_t idx);
int pterm_input_char(pterm_t *t, char c);
int pterm_input_string(pterm_t *t, const char *str);
int pterm_execute_command(pterm_t *t, const char *cmdline);
void pterm_render(pterm_t *t);
void pterm_write(pterm_t *t, const char *text);
void pterm_write_attr(pterm_t *t, const char *text, uint8_t attr);
void pterm_newline(pterm_t *t);
void pterm_clear(pterm_t *t);
void pterm_set_phase(pterm_t *t, uint8_t phase);
const char *pterm_phase_prompt(uint8_t phase);

/* Command registration */
void pterm_register_commands(pterm_t *t);
const pterm_command_t *pterm_find_command(pterm_t *t, const char *name);

/* History */
void pterm_history_add(pterm_t *t, const char *cmd);
const char *pterm_history_prev(pterm_t *t);
const char *pterm_history_next(pterm_t *t);

#endif /* ZEDEC_PTERM_H */
