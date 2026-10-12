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
 *   T5  Windows 10 1803 and later (zxvd_rpc_win.c): the same stream socket
 *       on Winsock's AF_UNIX, at a path such as
 *       %LOCALAPPDATA%\ZXV\run\zxvd.sock. Permissions are ACLs there: a
 *       folder zxvd creates gets a protected DACL for the owner alone; an
 *       existing one must be owned by us, not be a link, and grant write
 *       to nobody but us, SYSTEM and Administrators. The socket file gets a
 *       protected owner-only DACL after bind (the 0600 of T1). T2's peer
 *       check is SIO_AF_UNIX_GETPEERPID, then the peer process token's
 *       user SID must equal ours. T3 and T4 are unchanged.
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
 *   E5  RESUMED. When the books file (F1) brings both accounts back, the
 *       owner keeps that balance: this run's first sync takes the SWC held
 *       then as its base, and later syncs follow the change from it
 *       (balance = carried + (held - base) * rate, never below zero). The
 *       swarm itself starts fresh each run; the books do not.
 *
 * THE BOOKS FILE (--books PATH; zxvd --desktop saves by default)
 *   F1  Without a path the books live in memory, as before. With one, every
 *       change to them (a currency, a private unit, an account, a posting)
 *       is one record in an append-only text file, mode 0600 and ours.
 *   F2  A record is "<64 hex> <JSON>\n": the hex is SHA-256(previous hash ||
 *       JSON), from 32 zero bytes, so an edited, dropped or reordered record
 *       breaks the chain. The first record is {"op":"books","v":1}.
 *   F3  A record is written and fsync'd before the call that made it
 *       answers. ref, name and code travel as JSON strings, amounts and
 *       entry ids as decimal strings, as on the socket.
 *   F4  At start the file is replayed into fresh books through the same
 *       libzxv calls. Each account and entry must get back the number it
 *       had; any mismatch, a broken chain or an unknown record stops zxvd
 *       from starting (move the file aside to start empty). Nothing is
 *       repaired silently.
 *   F5  If a write fails, the change stays in memory for this run only, the
 *       call answers ZXV_E_IO, and every later change is refused with
 *       ZXV_E_IO until zxvd restarts (which replays what was saved).
 *   F6  A last record cut short by a crash (no newline) was never answered;
 *       it is dropped and the file truncated to the last whole record.
 *   F7  One zxvd at a time: the file is locked (flock, exclusive) while the
 *       books are open; a second zxvd on the same file refuses to start.
 *       Not on Windows yet.
 *   books.file reports the path, the record count, the chain head and
 *   whether the books are still writable.
 *
 * MESH SALES IN THE BOOKS (zxv_peer_host.h P6)
 *   S1  The first settled peer request opens three VFV accounts: "mesh
 *       counterparties" (issuer), "owner: mesh wallet" and "mesh: assurance
 *       fees paid" (holders).
 *   S2  A sale posts its price counterparties -> wallet ("mesh sale T"); a
 *       purchase posts the price wallet -> counterparties ("mesh buy T") and
 *       the 0.08889% fee wallet -> fees ("mesh fee T"). T is the first 16
 *       hex digits of the signed invoice hash.
 *   S3  T makes each posting's reference unique, so a request already in
 *       the books is never posted twice.
 *   S4  The peer book's wallet starts from a demo float that the owner's
 *       books never saw; when a purchase needs more than the wallet account
 *       holds, the shortfall is posted first as "mesh float T". A payment
 *       the seller refused (zxv_peer_host.h) is not a settled request and
 *       is not posted. mesh.books reports the three accounts and counts.
 *
 * THE JURISDICTION BRIDGE (jurisdiction.get / jurisdiction.set)
 *   J1  country: an ISO 3166-1 alpha-2 code (two capital letters) or "".
 *   J2  market: "closed" (closed clearing, the default) or "open" (the open
 *       batch-auction market, a per-deployment switch).
 *   J3  standard: the accounting standard reports will follow: "" (not
 *       chosen, the default), IFRS, US_GAAP, IPSAS, AAOIFI, OHADA, CAS or
 *       SAP_INSURER (statutory accounting for insurers).
 *   J4  set takes any of the three; the rest keep their values. A change is
 *       one "juris" record in the books file, written before it applies, so
 *       it survives a restart; an unchanged setting writes nothing.
 *   J5  in_effect is false: the settings are recorded only. No report
 *       adapter and no open market is wired to them yet, and an adapter
 *       will not replace an accountant or a Shariah board.
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

/* peer.* (zxv_peer_host.h): remote inference between nodes, settled with the
 * assurance fee. Set separately from the host ops; unset methods answer
 * -32601. ask and serve return NULL on success (the result object written
 * into `out`) or the reason they failed (error ZXVD_E_PEER). */
#define ZXVD_E_PEER (-32010)
typedef struct {
    const char *(*ask)(const char *peer, const char *prompt, uint64_t max_price_minor,
                       zxvd_buf_t *out);                                  /* peer.ask    */
    const char *(*serve)(bool on, uint64_t price_minor, zxvd_buf_t *out); /* peer.serve  */
    void (*status)(zxvd_buf_t *out);                                      /* peer.status */
} zxvd_peer_ops_t;
void zxvd_rpc_set_peer_ops(const zxvd_peer_ops_t *ops);

/* V1-V3: read-only views the host builds (zxv_net_host.h U9, sicily.h S2).
 * Each writes one JSON object into `out`; unset members answer -32601.
 *   V1 net.peers   {"peers":[{"id","addr","bucket","ubh","last_seen_ms",
 *                  "in_book","member","held","receivable","social","pending"}],
 *                  "count"}: the routing table's verified contacts, each
 *                  joined with its mesh-book account; empty when off.
 *   V2 net.offers  {"share":{"compute","storage"},"wants":[{"slot",
 *                  "resource","units_left","max_price"}],"outstanding":[{"role",
 *                  "peer","units","seq","tries","want"}]}: open wants and the
 *                  receipts waiting on the other side (a sell offer the buyer
 *                  has not accepted, or a buy the seller has not committed).
 *   V3 player.card the owner's earned tier (sicily.h S2) from the inputs this
 *                  node can measure, each with its source, the next tier's
 *                  thresholds and what is missing, and the perks. */
typedef struct {
    void (*peers)(zxvd_buf_t *out);
    void (*offers)(zxvd_buf_t *out);
    void (*card)(zxvd_buf_t *out);
} zxvd_view_ops_t;
void zxvd_rpc_set_view_ops(const zxvd_view_ops_t *ops);

/* aipi.* (zxvd_aipi.c, AIPI on Z-Wasm, docs/AIPI.md): content-addressed
 * functions run on this node only (offline, rule A3). Set separately; unset
 * methods answer -32601 (a build without the Z-Wasm kit). Each returns NULL
 * on success (the result object written into `out`) or the reason it failed
 * (error ZXVD_E_AIPI). */
#define ZXVD_E_AIPI        (-32011)
#define ZXVD_AIPI_ARGS_MAX 8u    /* the Z-Wasm call ABI: up to 8 signed 64-bit integers */
#define ZXVD_AIPI_HEX_MAX  8000u /* aipi.put "hex": bytes in one request line */
typedef struct {
    char module[128], contract[128]; /* CIDs as text; contract "" = none */
    char entry[64], checker[64], certifier[64];
    uint8_t verify;  /* AIPI_VERIFY_* */
    bool local_only; /* privacy LOCAL: never leaves this node */
    bool deterministic;
    uint64_t fuel_max;
} zxvd_aipi_fn_t;
typedef struct {
    /* data/len, or path (data NULL): a file read by zxvd */
    const char *(*put)(const uint8_t *data, uint32_t len, const char *path, bool private_,
                       zxvd_buf_t *out);                               /* aipi.put */
    const char *(*get)(const char *cid, zxvd_buf_t *out);              /* aipi.get */
    const char *(*publish)(const zxvd_aipi_fn_t *fn, zxvd_buf_t *out); /* aipi.publish */
    const char *(*execute)(const char *fn_cid, const int64_t *args, uint32_t n, bool private_,
                           zxvd_buf_t *out); /* aipi.execute */
    void (*status)(zxvd_buf_t *out);         /* aipi.status */
} zxvd_aipi_ops_t;
void zxvd_rpc_set_aipi_ops(const zxvd_aipi_ops_t *ops);

/* files.* (zxv_files.h F1-F6): the ZXV folder behind the Files, Text Editor
 * and Photos apps. A refusal is error ZXVD_E_FILES with zxf_strerror's text. */
#define ZXVD_E_FILES (-32012)

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
void zxvd_rpc_books_close(void); /* Windows: the transport in zxvd_rpc_win.c */
/* E1: choose the earnings currency and rate before the books open. 0, or -1
 * (books already open, code not 3..8 bytes, rate 0 or above ZXV_AMOUNT_MAX);
 * a code libzxv refuses makes the books fail to open. */
int zxvd_rpc_earnings_set(const char *ccy, uint64_t rate);
/* E2: mirror the SWC the owner's models hold. 0 (also when the books are not
 * open), or -1 when the amount is out of range or libzxv refused the posting
 * (the balance is then unchanged). */
int zxvd_rpc_earnings_sync(uint64_t held_swc);
/* F1: the books file, set before the books open; NULL or "" keeps them in
 * memory. 0, or -1 (books already open, path too long). */
int zxvd_rpc_books_file(const char *path);
/* S1-S4: post one settled peer request; sold = this node was the seller.
 * 0 (also when the books are not open or gross is 0), or -1 when the
 * amounts are out of range or the books refused a posting. */
int zxvd_rpc_mesh_post(bool sold, uint64_t gross, uint64_t fee, const uint8_t invoice[32]);
void zxvd_rpc_set_ops(const zxvd_host_ops_t *ops);

#endif
