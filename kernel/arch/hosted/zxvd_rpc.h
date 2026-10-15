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
 *   M5  Method groups (zxvd_ext.h): engines such as netting and cards
 *       register their own methods behind feature switches that start off;
 *       features.get lists the switches and features.set flips one for this
 *       run. A method whose feature is off answers ZXVD_E_OFF (-32012).
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
 *   J5  in_effect is true: the jurisdiction host (kernel/src/juris) gets
 *       the settings at start and at every change. plugins counts the
 *       plug-ins it has loaded: none until the deployment configures a
 *       jurisdiction gateway key, so until then its answers fail closed
 *       (wait). report_standard is the framework reports follow: the chosen
 *       standard when an adapter is built for it (IFRS, US_GAAP, AAOIFI)
 *       and a country is set, else "". No payment path asks the host yet,
 *       and an adapter does not replace an accountant or a Shariah board.
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

/* play.* (sdk/playground, zxvplay.h): templates and examples in ZXV's
 * languages, run in this process on scratch state; nothing touches the
 * books. Set separately; unset methods answer -32601 (a build without the
 * Playground). Each returns NULL on success (one JSON value written into
 * `out`) or the reason it failed (error ZXVD_E_PLAY).
 *   P1 play.catalogue  {}  -> [{"id","title","lang","audience","safety",
 *                      "summary","steps","files","draft"?}]
 *   P2 play.run        {"template", "files"?: {name: text}} -> the result
 *                      object (verdict TRUE/FALSE/BOTH/NEITHER, steps, ...)
 *   P3 play.draft      {"template", "files"?, "request"?} -> {"model": bool,
 *                      "draft": text}: the local swarm's draft of the
 *                      template's draft file. Untrusted text: it is not run. */
#define ZXVD_E_PLAY (-32016)
typedef struct {
    const char *name;
    const char *text;
    size_t len;
} zxvd_play_file_t;
typedef struct {
    const char *(*catalogue)(zxvd_buf_t *out);
    const char *(*run)(const char *template_id, const zxvd_play_file_t *files, uint32_t n,
                       zxvd_buf_t *out);
    const char *(*draft)(const char *template_id, const zxvd_play_file_t *files, uint32_t n,
                         const char *request, zxvd_buf_t *out);
} zxvd_play_ops_t;
void zxvd_rpc_set_play_ops(const zxvd_play_ops_t *ops);

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
/* X1-X7: methods other modules add (zxvd_chat.h, zxvd_pay.h).
 *   X1 zxvd_rpc_add_methods appends a table, which must outlive zxvd's use
 *      of it; up to ZXVD_RPC_MAX_TABLES tables at once. 0, or -1 when m is
 *      NULL, n is 0 or every slot is taken. Adding a table already held
 *      only updates its count. zxvd_rpc_remove_methods takes one table out
 *      (a table not held is ignored); zxvd_rpc_clear_methods takes them all.
 *      rpc.discover lists the tables' names after the built-in ones, in the
 *      order the tables were added. A built-in name wins, then the table
 *      added first.
 *   X2 a method reads its params with zxvd_param_str / _bool / _u32 /
 *      _amount (M3) or walks one with zxvd_param_json, and writes one JSON
 *      value into zxvd_result(c); zxvd_fail sets the error and, as for the
 *      built-ins, the first error stands.
 *   X3 a method that neither fails nor writes a result answers -32603.
 *   X4 the codes a method may use are the JSON-RPC ones below or its own,
 *      in -32099..-32001. Every code in use, and its one meaning (a new
 *      code takes a free number from this list and is added here, to
 *      docs/ZXVD.md's error list and to api/zxvd.openrpc.json's):
 *        -32000 a libzxv status (X5)        -32010 peer      -32011 AIPI
 *        -32012 the method's feature is off (zxvd_ext.h X4)
 *        -32013 spaces   -32014 files   -32015 not for a phone
 *        -32016 play     -32017 module methods (zxvd_rpc_modules.h)
 *        -32020 payments (zxvd_pay.h)   -32030 people   -32031 no model
 *        -32040 cards    -32041 passkeys
 *        -32050 vault, swap, auctions, Zenny, stats (zxvd_econ.c)
 *        -32060 bonds, derivatives, grants, estates (zxvd_sandbox.h)
 *        -32070 companion   -32080 seats   -32090 Web4 feed
 *        -32095 power (zxvd_power.h)   -32096 IRC gateway (zxvd_irc.h)
 *        -32032 public spaces and the watch hall (zxvd_pspace.h)
 *   X5 zxvd_libzxv_ok turns a libzxv status into error -32000 exactly as
 *      the book methods report it (M2: data status, name, class).
 *   X6 zxvd_books_cap / zxvd_books_ctx are the books' root capability and
 *      context (NULL while the books are closed), never put on the wire.
 *   X7 zxvd_books_account / zxvd_books_post change the books exactly as
 *      ledger.open_account / ledger.post do, each change one record in the
 *      books file before it answers (F1-F5). */
#define ZXVD_RPC_MAX_TABLES 4u
#define ZXVD_E_LIBZXV       (-32000)
#define ZXVD_E_METHOD       (-32601)
#define ZXVD_E_PARAMS       (-32602)
#define ZXVD_E_INTERNAL     (-32603)
typedef struct zxvd_call zxvd_call_t;
typedef struct {
    const char *name;
    void (*fn)(zxvd_call_t *c);
} zxvd_method_t;
int zxvd_rpc_add_methods(const zxvd_method_t *m, uint32_t n);
void zxvd_rpc_remove_methods(const zxvd_method_t *m);
void zxvd_rpc_clear_methods(void);
/* A string parameter: true if read, or absent and not required (out = "").
 * It must be a JSON string of at most cap - 1 bytes with no NUL. */
bool zxvd_param_str(zxvd_call_t *c, const char *key, char *out, uint32_t cap, bool required);
/* A boolean parameter: true if read, or absent and not required (*v kept). */
bool zxvd_param_bool(zxvd_call_t *c, const char *key, bool *v, bool required);
/* A JSON integer 0..UINT32_MAX: true if read, or absent and not required
 * (*v kept). */
bool zxvd_param_u32(zxvd_call_t *c, const char *key, uint32_t *v, bool required);
/* M3: a JSON integer or a decimal string, as "amount_minor" of ledger.post:
 * true if read, or absent and not required (*v kept). */
bool zxvd_param_amount(zxvd_call_t *c, const char *key, int64_t *v, bool required);
/* Any parameter as web4_web2.h tokens (tok is a const w4_jtok_t *; at is
 * the value's index): true if present. NULL key: the whole params object,
 * true if params were sent. */
typedef struct {
    const char *js;
    const void *tok;
    int32_t n, at;
} zxvd_json_t;
bool zxvd_param_json(zxvd_call_t *c, const char *key, zxvd_json_t *v);
zxvd_buf_t *zxvd_result(zxvd_call_t *c);
void zxvd_fail(zxvd_call_t *c, int code, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
/* X5: true if st is ZXV_OK, else the call fails with -32000. */
bool zxvd_libzxv_ok(zxvd_call_t *c, int32_t st);
/* X6 (opaque here: zxv_api.h's zxv_cap and zxv_ctx) */
struct zxv_cap;
struct zxv_ctx;
const struct zxv_cap *zxvd_books_cap(void);
struct zxv_ctx *zxvd_books_ctx(void);
/* X7: libzxv statuses; ZXV_E_IO when the books file did not take it. */
int32_t zxvd_books_account(const char *name, const char *ccy, int32_t kind, uint32_t *id);
int32_t zxvd_books_post(uint32_t from, uint32_t to, int64_t amount, const char *ccy,
                        const char *ref, uint64_t *entry);

/* files.* (zxv_files.h F1-F6): the ZXV folder behind the Burrow, Quillon
 * and Glint apps (docs/STAPLE_APPS.md). A refusal is error ZXVD_E_FILES with zxf_strerror's text.
 */
#define ZXVD_E_FILES (-32014)
/* spaces.* and the "space" parameter of files.* (zxv_spaces.h): a missing
 * space, or a role that may not do what was asked. */
#define ZXVD_E_SPACE (-32013)

/* phone.* (zxvd_phone.h): the phone link and pairing the owner's phones,
 * set by zxvd_phone_init. Unset, the methods answer -32601. link, pair and
 * revoke return NULL on success (result in out) or why they failed (error
 * -32602). Control socket only: a phone gets ZXVD_E_PHONE. link's port is
 * -1 for the default and bind_ip NULL for every interface. */
#define ZXVD_E_PHONE (-32015)
typedef struct {
    const char *(*link)(bool on, int32_t port, const char *bind_ip,
                        zxvd_buf_t *out);                                    /* phone.link */
    const char *(*pair)(const char *name, bool allow_post, zxvd_buf_t *out); /* phone.pair   */
    void (*list)(zxvd_buf_t *out);                                           /* phone.list   */
    const char *(*revoke)(const char *phone_id, zxvd_buf_t *out);            /* phone.revoke */
} zxvd_phone_ops_t;
void zxvd_rpc_set_phone_ops(const zxvd_phone_ops_t *ops);

/* One request from a paired phone (zxvd_phone.h A1-A3): like
 * zxvd_rpc_handle_line, but a method the phone may not call answers
 * ZXVD_E_PHONE, and a notification is refused (-32600) rather than run.
 * allow_post: the pairing may move money and change the network. */
bool zxvd_rpc_handle_phone_line(const char *line, size_t len, zxvd_buf_t *out, bool allow_post);

/* social.*, call.*, xlate.*, devmesh.*, ui.* (zxvd_people.c): the people
 * features. Set separately; unset methods answer -32601. call() gets the
 * method's full name and the params object's JSON text ("{}" when absent)
 * and returns 0 with the result written into `out`, or a JSON-RPC error code
 * (-32602, ZXVD_E_PEOPLE, ZXVD_E_NO_MODEL) with the reason in msg. */
#define ZXVD_E_PEOPLE   (-32030)
#define ZXVD_E_NO_MODEL (-32031) /* xlate.translate: no local model can generate */
typedef struct {
    int (*call)(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                size_t msg_cap);
} zxvd_people_ops_t;
void zxvd_rpc_set_people_ops(const zxvd_people_ops_t *ops);

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
