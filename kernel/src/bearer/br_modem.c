/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_modem.c — Hayes AT modem state machine (br_modem.h, rules M1..M4). */
#include "br_modem.h"

static const char k_nc[] = "\r\nNO CARRIER\r\n";
#define NC_LEN 14u

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static bool line_is(const char *l, uint32_t n, const char *lit)
{
    uint32_t k = slen(lit);
    return n == k && br_meq(l, lit, k);
}

static void emit(br_modem_t *m, int ev, uint32_t arg)
{
    if (m->ops.on_event) m->ops.on_event(m->ops.ctx, ev, arg);
}

static int put(br_modem_t *m, const char *s, uint64_t now)
{
    m->last_tx = now;
    return m->ops.write(m->ops.ctx, (const uint8_t *) s, slen(s));
}

static void set_state(br_modem_t *m, uint8_t st, uint64_t now)
{
    m->state = st;
    m->t_state = now;
    m->ln = 0;
    m->line_bad = 0;
    m->nc_match = 0;
}

static void fail(br_modem_t *m, uint64_t now)
{
    uint8_t was = m->state;
    set_state(m, BR_MDM_FAILED, now);
    emit(m, BR_MDM_EV_FAILED, was);
}

int br_modem_parse_result(const char *l, uint32_t n, uint32_t *rate)
{
    if (!l) return BR_MDM_R_NONE;
    if (rate) *rate = 0;
    if (line_is(l, n, "OK")) return BR_MDM_R_OK;
    if (line_is(l, n, "RING")) return BR_MDM_R_RING;
    if (line_is(l, n, "NO CARRIER")) return BR_MDM_R_NO_CARRIER;
    if (line_is(l, n, "ERROR")) return BR_MDM_R_ERROR;
    if (line_is(l, n, "NO DIALTONE") || line_is(l, n, "NO DIAL TONE")) return BR_MDM_R_NO_DIALTONE;
    if (line_is(l, n, "BUSY")) return BR_MDM_R_BUSY;
    if (line_is(l, n, "NO ANSWER")) return BR_MDM_R_NO_ANSWER;
    if (n >= 7u && br_meq(l, "CONNECT", 7)) {
        if (n == 7u) return BR_MDM_R_CONNECT;
        if (l[7] != ' ') return BR_MDM_R_NONE;
        uint32_t r = 0, i = 8, digits = 0;
        while (i < n && l[i] >= '0' && l[i] <= '9' && digits < 9u) {
            r = r * 10u + (uint32_t) (l[i] - '0');
            i++;
            digits++;
        }
        if (digits == 0) return BR_MDM_R_NONE;
        /* allow a suffix such as "/ARQ" or "/V42BIS" after the rate */
        if (i < n && l[i] != '/' && l[i] != ' ') return BR_MDM_R_NONE;
        if (rate) *rate = r;
        return BR_MDM_R_CONNECT;
    }
    return BR_MDM_R_NONE;
}

bool br_modem_dial_ok(const char *s)
{
    if (!s) return false;
    uint32_t n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        bool ok = (c >= '0' && c <= '9') || c == '*' || c == '#' || c == ',' || c == 'W' ||
                  c == 'P' || c == 'T' || c == '+' || c == ' ';
        if (!ok || n >= BR_MODEM_DIAL_MAX) return false;
    }
    return n > 0;
}

br_status_t br_modem_init(br_modem_t *m, const br_modem_ops_t *ops)
{
    if (!m || !ops || !ops->write) return BR_ERR_ARG;
    br_mset(m, 0, sizeof *m);
    m->ops.ctx = ops->ctx;
    m->ops.write = ops->write;
    m->ops.on_data = ops->on_data;
    m->ops.on_event = ops->on_event;
    m->guard_ms = 1000u;
    m->cmd_timeout_ms = 5000u;
    m->dial_timeout_ms = 60000u;
    return BR_OK;
}

br_status_t br_modem_start(br_modem_t *m, uint64_t now)
{
    if (!m || !m->ops.write) return BR_ERR_ARG;
    set_state(m, BR_MDM_INIT, now);
    m->init_step = 0;
    m->skip_lf = 0;
    if (put(m, "ATZ\r", now) != 0) {
        fail(m, now);
        return BR_ERR_SEND;
    }
    return BR_OK;
}

br_status_t br_modem_dial(br_modem_t *m, const char *number, uint64_t now)
{
    if (!m) return BR_ERR_ARG;
    if (m->state != BR_MDM_READY) return BR_ERR_STATE;
    if (!br_modem_dial_ok(number)) return BR_ERR_ARG; /* M2 */
    set_state(m, BR_MDM_DIALING, now);
    if (put(m, "ATD", now) != 0 || put(m, number, now) != 0 || put(m, "\r", now) != 0) {
        fail(m, now);
        return BR_ERR_SEND;
    }
    return BR_OK;
}

br_status_t br_modem_answer(br_modem_t *m, uint64_t now)
{
    if (!m) return BR_ERR_ARG;
    if (m->state != BR_MDM_READY) return BR_ERR_STATE;
    set_state(m, BR_MDM_DIALING, now);
    if (put(m, "ATA\r", now) != 0) {
        fail(m, now);
        return BR_ERR_SEND;
    }
    return BR_OK;
}

br_status_t br_modem_send(br_modem_t *m, const uint8_t *p, uint32_t n, uint64_t now)
{
    if (!m || (!p && n)) return BR_ERR_ARG;
    if (m->state != BR_MDM_ONLINE) return BR_ERR_STATE; /* M1 */
    if (n == 0) return BR_OK;
    m->last_tx = now;
    return m->ops.write(m->ops.ctx, p, n) == 0 ? BR_OK : BR_ERR_SEND;
}

br_status_t br_modem_hangup(br_modem_t *m, uint64_t now)
{
    if (!m) return BR_ERR_ARG;
    if (m->state != BR_MDM_ONLINE) return BR_ERR_STATE;
    m->state = BR_MDM_ESC_PRE;
    m->t_state = now;
    br_modem_tick(m, now);
    return BR_OK;
}

void br_modem_carrier_lost(br_modem_t *m, uint64_t now)
{
    if (!m) return;
    if (m->state == BR_MDM_ONLINE || m->state == BR_MDM_ESC_PRE || m->state == BR_MDM_ESC_POST) {
        set_state(m, BR_MDM_READY, now);
        emit(m, BR_MDM_EV_DISCONNECTED, BR_MDM_R_DCD);
    }
}

static void on_line(br_modem_t *m, uint64_t now)
{
    const char *l = m->line;
    uint32_t n = m->ln;
    if (n == 0) return;
    if (n >= 2u && l[0] == 'A' && l[1] == 'T') return; /* echo (M3) */
    uint32_t rate = 0;
    int r = br_modem_parse_result(l, n, &rate);
    if (r == BR_MDM_R_NONE) return;
    m->last_result = (uint8_t) r;
    switch (m->state) {
    case BR_MDM_INIT:
        if (r == BR_MDM_R_OK) {
            if (m->init_step == 0) {
                m->init_step = 1;
                m->t_state = now;
                if (put(m, "ATE0V1Q0X4\r", now) != 0) fail(m, now);
            } else {
                set_state(m, BR_MDM_READY, now);
                emit(m, BR_MDM_EV_READY, 0);
            }
        } else if (r == BR_MDM_R_ERROR) {
            fail(m, now);
        }
        break;
    case BR_MDM_READY:
        if (r == BR_MDM_R_RING) emit(m, BR_MDM_EV_RING, 0);
        break;
    case BR_MDM_DIALING:
        if (r == BR_MDM_R_CONNECT) {
            set_state(m, BR_MDM_ONLINE, now);
            m->connect_rate = rate;
            m->skip_lf = 1;
            emit(m, BR_MDM_EV_CONNECTED, rate);
        } else if (r == BR_MDM_R_NO_CARRIER || r == BR_MDM_R_BUSY || r == BR_MDM_R_ERROR ||
                   r == BR_MDM_R_NO_DIALTONE || r == BR_MDM_R_NO_ANSWER) {
            set_state(m, BR_MDM_READY, now);
            emit(m, BR_MDM_EV_DIAL_FAILED, (uint32_t) r);
        }
        break;
    case BR_MDM_ESC_POST:
        if (r == BR_MDM_R_OK) {
            set_state(m, BR_MDM_HANGUP, now);
            if (put(m, "ATH0\r", now) != 0) fail(m, now);
        } else if (r == BR_MDM_R_NO_CARRIER) {
            set_state(m, BR_MDM_READY, now);
            emit(m, BR_MDM_EV_DISCONNECTED, BR_MDM_R_NO_CARRIER);
        }
        break;
    case BR_MDM_HANGUP:
        if (r == BR_MDM_R_OK || r == BR_MDM_R_NO_CARRIER) {
            set_state(m, BR_MDM_READY, now);
            emit(m, BR_MDM_EV_DISCONNECTED, BR_MDM_R_LOCAL);
        } else if (r == BR_MDM_R_ERROR) {
            fail(m, now);
        }
        break;
    default:
        break;
    }
}

void br_modem_rx(br_modem_t *m, const uint8_t *p, uint32_t n, uint64_t now)
{
    if (!m || !p) return;
    uint32_t i = 0;
    while (i < n) {
        if (m->state == BR_MDM_ONLINE || m->state == BR_MDM_ESC_PRE) {
            if (m->skip_lf) {
                m->skip_lf = 0;
                if (p[i] == '\n') {
                    i++;
                    continue;
                }
            }
            /* M4: forward a run of data, watching for NO CARRIER. */
            uint32_t start = i;
            bool dropped = false;
            while (i < n) {
                uint8_t b = p[i++];
                if (b == (uint8_t) k_nc[m->nc_match])
                    m->nc_match++;
                else
                    m->nc_match = (b == '\r') ? 1u : 0u;
                if (m->nc_match == NC_LEN) {
                    dropped = true;
                    break;
                }
            }
            if (m->ops.on_data && i > start) m->ops.on_data(m->ops.ctx, p + start, i - start);
            if (dropped) {
                set_state(m, BR_MDM_READY, now);
                emit(m, BR_MDM_EV_DISCONNECTED, BR_MDM_R_NO_CARRIER);
            }
            continue;
        }
        uint8_t b = p[i++];
        if (m->state == BR_MDM_OFF || m->state == BR_MDM_FAILED) continue;
        if (b == '\r' || b == '\n') {
            if (!m->line_bad) on_line(m, now);
            m->ln = 0;
            m->line_bad = 0;
            continue;
        }
        if (m->ln >= BR_MODEM_LINE_MAX) {
            m->line_bad = 1; /* M3: discard over-long lines */
            continue;
        }
        m->line[m->ln++] = (char) b;
    }
}

void br_modem_tick(br_modem_t *m, uint64_t now)
{
    if (!m) return;
    switch (m->state) {
    case BR_MDM_ESC_PRE:
        if (now >= m->last_tx + m->guard_ms) {
            set_state(m, BR_MDM_ESC_POST, now);
            if (put(m, "+++", now) != 0) fail(m, now);
        }
        break;
    case BR_MDM_ESC_POST:
        if (now >= m->t_state + m->guard_ms + m->cmd_timeout_ms) fail(m, now);
        break;
    case BR_MDM_INIT:
    case BR_MDM_HANGUP:
        if (now >= m->t_state + m->cmd_timeout_ms) fail(m, now);
        break;
    case BR_MDM_DIALING:
        if (now >= m->t_state + m->dial_timeout_ms) {
            /* any character aborts a dial in progress */
            (void) put(m, "\r", now);
            set_state(m, BR_MDM_READY, now);
            emit(m, BR_MDM_EV_DIAL_FAILED, BR_MDM_R_TIMEOUT);
        }
        break;
    default:
        break;
    }
}
