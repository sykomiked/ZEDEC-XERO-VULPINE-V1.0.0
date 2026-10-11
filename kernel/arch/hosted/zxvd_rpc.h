/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_rpc.h — zxvd's control socket: JSON-RPC 2.0 over a local UNIX socket.
 *
 * zxvd is the hosted swarm (zxv_host.c) built with -DZXVD and linked with
 * libzxv through its frozen C ABI (sdk/libzxv/include/zxv_api.h). One
 * process then holds the swarm and its budgets, the model slot and its
 * budget gate, the settlement spine on pay_ledger, the Vinea node, and a
 * libzxv context for the owner's books. This file is the one door into it
 * for the native UI, the SDKs and scripts. docs/ZXVD.md lists the methods;
 * api/zxvd.openrpc.json is the machine-readable form.
 *
 * TRANSPORT
 *   T1  A stream UNIX socket at a path the owner chooses (--rpc PATH). It is
 *       created with mode 0600 inside a directory zxvd creates with 0700 if
 *       missing; an existing directory must be ours and not group- or
 *       other-writable. A stale socket file is replaced only if nothing
 *       answers on it, and a path that exists and is not a socket is refused.
 *   T2  Every connection's peer must run as the same user as zxvd
 *       (SO_PEERCRED on Linux, getpeereid on macOS/BSD); others are closed
 *       before a byte is read. There is no token: the file permission and
 *       the peer check are the authentication.
 *   T3  Framing: one JSON-RPC 2.0 request per line (LF), one response per
 *       line. A line longer than ZXVD_RPC_MAX_LINE closes the connection.
 *       Batches (a JSON array) are refused with -32600.
 *   T4  At most ZXVD_RPC_MAX_CLIENTS connections at once; a client that
 *       stays silent for ZXVD_RPC_IDLE_MS is closed, so a stalled client can
 *       never hold the swarm's loop.
 *   T5  Windows: not built yet (zxvd_rpc_open returns -1 there).
 *
 * METHODS
 *   M1  Host methods (swarm.*, model.*, notes.*, net.*, update.*,
 *       zxvd.shutdown) are served by callbacks the host fills in
 *       (zxvd_host_ops_t), so they act on the same state as the window.
 *   M2  Book methods (ledger.*, currency.*) call libzxv's *_cap forms
 *       through one root capability, created at start with the
 *       LEDGER_READ|LEDGER_POST bits only and never put on the wire. zxvd
 *       refuses to start against libzxv older than ABI 1.3. A libzxv
 *       failure is error -32000 with data {"status": ZXV_E_* number,
 *       "name": its symbolic name, "class": USAGE|REFUSED|FATAL|SYSTEM}; once
 *       the context is poisoned every book call returns the FATAL class.
 *   M3  Amounts are exact integers in minor units. 64-bit values travel as
 *       decimal strings in results ("entry_id", "*_minor"), as the SDKs
 *       expect; "amount_minor" may be sent as a JSON integer or a decimal
 *       string. A fraction or exponent is -32602, never rounded.
 *   M4  A notification (no "id") is executed and gets no response.
 *
 * SWARM EARNINGS (the owner's books mirror the swarm's money)
 *   E1  At start the books get one currency for earnings, VFV by default
 *       (--earnings-ccy): an ISO 4217 code is enabled from the table, any
 *       other code is registered as a private unit with 2 minor units. The
 *       first settled cycle opens two accounts in it: "swarm market"
 *       (issuer) and "owner: swarm earnings" (holder).
 *   E2  After every settled market cycle the host calls
 *       zxvd_rpc_earnings_sync with the Financial capital (SWC) all the
 *       owner's models hold. The owner's balance is set to that times the
 *       rate (--earnings-rate, minor units per SWC, default 1) by one
 *       balanced posting: market -> owner when it grew, owner -> market when
 *       it fell (the assurance fee leaves the models; returned bounties come
 *       back). Fee buckets are not the owner's and are not counted.
 *   E3  The rate is a fixed bookkeeping rate set by the owner, not a market
 *       price: no exchange rate between SWC and VFV is posted anywhere yet.
 *   E4  swarm.earnings reports the currency, rate, both accounts, the
 *       balance and the counts; ledger.balance and ledger.entries show the
 *       postings like any others.
 */
#ifndef ZXVD_RPC_H
#define ZXVD_RPC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ZXVD_RPC_MAX_CLIENTS 8
#define ZXVD_RPC_MAX_LINE    16384
#define ZXVD_RPC_IDLE_MS     60000u

/* A growable output buffer. zxvd_buf_raw appends bytes, zxvd_buf_printf
 * formats; an allocation failure sets oom and later writes are dropped. */
typedef struct {
    char *p;
    size_t len, cap;
    bool oom;
} zxvd_buf_t;

void zxvd_buf_raw(zxvd_buf_t *b, const char *s, size_t n);
void zxvd_buf_printf(zxvd_buf_t *b, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void zxvd_buf_json_str(zxvd_buf_t *b, const char *s); /* a quoted, escaped JSON string */

/* What the host provides. Each writes one JSON value into `out` (an object,
 * or a string via zxvd_buf_json_str). NULL members answer -32601. */
typedef struct {
    void (*state)(zxvd_buf_t *out);                   /* swarm.state   */
    void (*ask)(const char *prompt, zxvd_buf_t *out); /* swarm.ask     */
    void (*model)(zxvd_buf_t *out);                   /* model.info    */
    void (*notes)(zxvd_buf_t *out);                   /* notes.list    */
    uint32_t (*notes_read)(void);                     /* notes.mark_read */
    const char *(*net)(const char *command);          /* net.command; NULL = bad command */
    void (*update_check)(zxvd_buf_t *out);            /* update.check  */
    void (*shutdown)(void);                           /* zxvd.shutdown */
    const char *app_version;
} zxvd_host_ops_t;

/* Open the socket and the libzxv context. 0, or -1 with a reason on stderr
 * (nothing is left open). */
int zxvd_rpc_open(const char *path, const zxvd_host_ops_t *ops);
/* Add the listener and every client to `set` (an fd_set *); returns the
 * highest descriptor added, or -1 when closed. */
int zxvd_rpc_fds(void *set);
/* Serve whatever `set` (an fd_set * after select) says is ready, and close
 * idle clients. now_ms: a monotonic clock. */
void zxvd_rpc_service(void *set, uint64_t now_ms);
/* Close every connection, remove the socket file, free the context. */
void zxvd_rpc_close(void);

/* One request line in, one response line out (no trailing LF); false when
 * the request was a notification and nothing is to be sent. Used by the
 * socket loop and directly by the unit test. */
bool zxvd_rpc_handle_line(const char *line, size_t len, zxvd_buf_t *out);
/* The libzxv side only, for the unit test: open/close the context and cap
 * without a socket. */
int zxvd_rpc_books_open(void);
void zxvd_rpc_books_close(void);
/* E1: choose the earnings currency and rate before the books open. 0, or -1
 * (books already open, code not 3..8 bytes, rate 0 or above ZXV_AMOUNT_MAX);
 * a code libzxv refuses makes the books fail to open. */
int zxvd_rpc_earnings_set(const char *ccy, uint64_t rate);
/* E2: mirror the SWC the owner's models hold. 0 (also when the books are not
 * open), or -1 when the amount is out of range or libzxv refused the posting
 * (the balance is then unchanged). */
int zxvd_rpc_earnings_sync(uint64_t held_swc);
void zxvd_rpc_set_ops(const zxvd_host_ops_t *ops);

#endif
