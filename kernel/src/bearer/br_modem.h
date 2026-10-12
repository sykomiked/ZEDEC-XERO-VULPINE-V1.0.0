/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_modem.h — a Hayes AT command modem driver as a byte-stream state
 * machine (dial-up, GSM/LTE modems in data mode, radio modems with an AT
 * command set). The modem is reached through a serial port opened by the
 * host (kernel/arch/hosted/bearer_host.c); this file only sees bytes and
 * the time in milliseconds.
 *
 * STATES
 *   OFF -> INIT (ATZ, then ATE0V1Q0X4) -> READY
 *   READY --dial--> DIALING --CONNECT--> ONLINE
 *   READY --RING, answer--> DIALING (ATA) --CONNECT--> ONLINE
 *   DIALING --NO CARRIER/BUSY/NO DIALTONE/NO ANSWER/ERROR/timeout--> READY
 *   ONLINE --hangup--> ESC_PRE --guard--> "+++" ESC_POST --OK--> "ATH0"
 *          HANGUP --OK--> READY
 *   ONLINE --"NO CARRIER" in the stream or carrier_lost()--> READY
 *   any command that times out --> FAILED (the host drops DTR / power
 *   cycles the modem and calls br_modem_start() again).
 *
 * RULES
 *   M1  Escape guard time (S12, default 1 s): "+++" is written only after
 *       guard_ms with nothing sent, and nothing is sent after it until the
 *       modem has answered OK; br_modem_send() refuses (BR_ERR_STATE) while
 *       an escape is in progress.
 *   M2  Dial strings are checked: 1..BR_MODEM_DIAL_MAX characters from
 *       0-9 * # , W P T + and space. Anything else (a ';' or '\r' that could
 *       smuggle another AT command) is refused.
 *   M3  Result lines are matched exactly in command mode; echoed command
 *       lines (starting with "AT") and empty lines are ignored; over-long
 *       lines are discarded.
 *   M4  In data mode every byte goes to on_data(). The sequence
 *       "\r\nNO CARRIER\r\n" also ends the call (a modem prints it after
 *       losing carrier); hosts with a DCD line should also call
 *       br_modem_carrier_lost(). A peer could send those 14 bytes on
 *       purpose; the worst it can do is hang up its own call.
 *
 * HONEST LIMITS
 *   Tested against a scripted fake modem, not a real one. Only verbose
 *   result codes (V1) are parsed. No AT+CGDCONT / PPP: cellular data uses
 *   the OS's normal data link (BR_CELLULAR), not this driver.
 */
#ifndef ZXV_BR_MODEM_H
#define ZXV_BR_MODEM_H

#include "bearer.h"

#define BR_MODEM_LINE_MAX 80u
#define BR_MODEM_DIAL_MAX 40u

typedef enum {
    BR_MDM_OFF = 0,
    BR_MDM_INIT = 1,
    BR_MDM_READY = 2,
    BR_MDM_DIALING = 3,
    BR_MDM_ONLINE = 4,
    BR_MDM_ESC_PRE = 5,
    BR_MDM_ESC_POST = 6,
    BR_MDM_HANGUP = 7,
    BR_MDM_FAILED = 8,
} br_modem_state_t;

typedef enum {
    BR_MDM_R_NONE = 0,
    BR_MDM_R_OK = 1,
    BR_MDM_R_CONNECT = 2,
    BR_MDM_R_RING = 3,
    BR_MDM_R_NO_CARRIER = 4,
    BR_MDM_R_ERROR = 5,
    BR_MDM_R_NO_DIALTONE = 6,
    BR_MDM_R_BUSY = 7,
    BR_MDM_R_NO_ANSWER = 8,
    BR_MDM_R_TIMEOUT = 9, /* no answer from the modem in time */
    BR_MDM_R_LOCAL = 10,  /* we hung up */
    BR_MDM_R_DCD = 11,    /* carrier lost (host saw DCD drop) */
} br_modem_result_t;

typedef enum {
    BR_MDM_EV_READY = 1,
    BR_MDM_EV_CONNECTED = 2,    /* arg = connect rate (0 if not given) */
    BR_MDM_EV_DIAL_FAILED = 3,  /* arg = br_modem_result_t */
    BR_MDM_EV_DISCONNECTED = 4, /* arg = br_modem_result_t */
    BR_MDM_EV_RING = 5,
    BR_MDM_EV_FAILED = 6,       /* arg = state that timed out or errored */
} br_modem_event_t;

typedef struct {
    void *ctx;
    int (*write)(void *ctx, const uint8_t *p, uint32_t n); /* 0 = ok */
    void (*on_data)(void *ctx, const uint8_t *p, uint32_t n);
    void (*on_event)(void *ctx, int ev, uint32_t arg);
} br_modem_ops_t;

typedef struct {
    br_modem_ops_t ops;
    uint8_t state, init_step, skip_lf, line_bad;
    char line[BR_MODEM_LINE_MAX];
    uint32_t ln;
    uint64_t t_state, last_tx;
    uint32_t guard_ms, cmd_timeout_ms, dial_timeout_ms;
    uint32_t connect_rate;
    uint8_t last_result;
    uint32_t nc_match;
} br_modem_t;

br_status_t br_modem_init(br_modem_t *m, const br_modem_ops_t *ops);
/* Reset and send the init commands. */
br_status_t br_modem_start(br_modem_t *m, uint64_t now_ms);
br_status_t br_modem_dial(br_modem_t *m, const char *number, uint64_t now_ms);
br_status_t br_modem_answer(br_modem_t *m, uint64_t now_ms);
br_status_t br_modem_send(br_modem_t *m, const uint8_t *p, uint32_t n, uint64_t now_ms);
br_status_t br_modem_hangup(br_modem_t *m, uint64_t now_ms);
void br_modem_carrier_lost(br_modem_t *m, uint64_t now_ms);
/* Bytes from the modem. */
void br_modem_rx(br_modem_t *m, const uint8_t *p, uint32_t n, uint64_t now_ms);
/* Timers: escape guard time and command / dial timeouts. */
void br_modem_tick(br_modem_t *m, uint64_t now_ms);
/* Classify one result line (exposed for tests). *rate gets a CONNECT rate. */
int br_modem_parse_result(const char *line, uint32_t len, uint32_t *rate);
bool br_modem_dial_ok(const char *number);

#endif /* ZXV_BR_MODEM_H */
