# zxvd — the ZXV daemon

`zxvd` is the one process where the built parts of ZXV run together:

| Part | Where it comes from | In zxvd |
|---|---|---|
| Swarm: tokens-per-cycle budget, emotional economy, market, governor | `kernel/src/swarm` | live, steps every 100 ms |
| Model slot and budget gate (Qwen2.5 Q4_K_M on the integer engine) | `kernel/src/tensor`, `zxv_model_host.c`, `zxv_budget_gate.c` | `swarm.ask` |
| Answer tag and quarantine log | `zxv_answer_tag.c`, `kernel/src/lpres` | on every model answer |
| Settlement spine on the ledger of record, fee loop, self-audit | `kernel/src/settle`, `pay`, `peer_audit` | every cycle |
| Vinea peer node (off by default) | `zxv_net_host.c`, `kernel/src/vinea` | `net.command` |
| Remote inference between nodes over TCP, PQ session, fee settlement (off by default) | `zxv_peer_host.c`, `vna_session`, `pay` | `peer.*` |
| Signed update checks, notifications | `zxv_update_host.c`, `zx_notify` | `update.check`, `notes.*` |
| Owner's books: currencies, accounts, exact postings, invariants | **libzxv** through its frozen C ABI | `currency.*`, `ledger.*` |
| AIPI: functions by CID, run in the Z-Wasm sandbox under their contract (offline node) | `kernel/src/aipi`, `sdk/kits/aipi`, `zxvd_aipi.c` | `aipi.*` |
| JSON tokenizer | `kernel/src/web4/web4_json.c` | parses every request |

It is `zxv_host.c` built with `-DZXVD`, plus `zxvd_rpc.c` and `zxvd_aipi.c`
(with the Z-Wasm and Z-Contract libraries from `sdk/kits/aipi`), linked
against `libzxv.so` / `libzxv.dylib`. The Windows build leaves AIPI out for
now, and `ZXVD_AIPI=0` leaves it out anywhere; its `aipi.*` methods then
answer -32601. The desktop app (`zxv-host`) is unchanged.

## Build and run

```sh
kernel/arch/hosted/build_zxvd.sh /tmp/zxvd          # builds libzxv, then zxvd
/tmp/zxvd/zxvd --rpc "$HOME/.zxv/run/zxvd.sock" --model path/to/model.gguf
```

Every `zxv-host` option works too (`--net`, `--peer`, `--models-dir`, ...).
`zxvd --desktop` is the Linux and Windows desktop app
([DESKTOP_LINUX_WINDOWS.md](DESKTOP_LINUX_WINDOWS.md)): the socket goes in the
per-user folder unless `--rpc` is given, the window opens in the default
browser, and a second launch opens the running one's window.
zxvd opens no window; the loopback HTML window is still served on a free
port with its token, printed on the `ZXV-URL:` line.

## The socket

- JSON-RPC 2.0, one request per line, one response per line.
- A UNIX socket, mode 0600, in a folder zxvd creates 0700 (an existing folder
  must belong to the same user and not be writable by group or others). Only processes of
  the same user are served (checked with `SO_PEERCRED` / `getpeereid`).
- Windows 10 1803 and later: the same socket on Winsock's AF_UNIX
  (`zxvd_rpc_win.c`). The folder zxvd creates gets a DACL for the owner
  alone; an existing folder must be ours, not a link, and writable by nobody
  but us, SYSTEM and Administrators. The socket file gets an owner-only DACL,
  and the peer process's user SID must equal ours
  (`SIO_AF_UNIX_GETPEERPID`).
- A regular file at the path is never replaced; a live zxvd on the path is
  never displaced.
- Batches are refused. Notifications run and get no reply.
- Amounts are exact integers in minor units. 64-bit results (`entry_id`,
  `*_minor`) are decimal strings; `amount_minor` may be an integer or a
  decimal string. `1.5` is refused, never rounded.
- zxvd refuses to start against libzxv older than ABI 1.3.
- Up to 8 clients; an idle client is dropped after 60 s; a line over 16 KiB
  closes that client only.

```sh
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"swarm.ask","params":{"prompt":"hello"}}' \
  | nc -U "$HOME/.zxv/run/zxvd.sock"
```

## Methods

The machine-readable list is [`api/zxvd.openrpc.json`](../api/zxvd.openrpc.json);
CI checks it matches what zxvd serves.

| Method | Params | Result |
|---|---|---|
| `rpc.discover` | | `protocol`, `methods` |
| `zxvd.version` | | `app`, `protocol`, `libzxv`, `libzxv_features` |
| `zxvd.shutdown` | | `stopping` |
| `swarm.state` | | the full live state (same as the window's `/api/state`) |
| `swarm.ask` | `prompt` | `answer` |
| `model.info` | | `loaded`, `can_generate`, `name`, `arch`, `n_layers`, `n_vocab`, `status` |
| `notes.list` / `notes.mark_read` | | notes / `marked` |
| `net.command` | `command` | `result` |
| `net.peers` | | `peers` (`id`, `addr`, `bucket`, `ubh`, `last_seen_ms`, `in_book`, `member`, `held`, `receivable`, `social`, `pending`), `count` |
| `net.offers` | | `share` (`compute`, `storage`), `wants`, `outstanding` |
| `player.card` | | `tier`, `metal`, `stone`, `inputs`, `next`, `perks` |
| `peer.ask` | `peer` (IP or IP:PORT), `prompt`, `max_price_minor` | `answer`, `settlement`, `security` |
| `peer.serve` | `price_minor`? (absent stops) | the `peer.status` object |
| `peer.status` | | `serving`, `port`, `price_minor`, `wallet_minor`, `fees_paid_minor`, `buckets`, `sold`, `bought`, `refused`, `sessions`, `book_ok`, `history` |
| `update.check` | | `status` |
| `currency.enable` | `code` | `enabled` |
| `ledger.open_account` | `name`, `currency`, `kind`? | `account` |
| `ledger.post` | `from`, `to`, `amount_minor`, `currency`, `reference` | `entry_id` |
| `ledger.balance` | `account` | `debit_minor`, `credit_minor`, `equity_minor` |
| `ledger.accounts` | | `accounts` |
| `ledger.entries` | `account` | `entries` |
| `ledger.check` | | `ok` |
| `swarm.earnings` | | `currency`, `rate_minor`, `source_account`, `owner_account`, `balance_minor`, `postings`, `cycles`, `resumed` |
| `mesh.books` | | `currency`, `wallet_account`, `counterparty_account`, `fee_account`, `wallet_minor`, `fees_minor`, `sold`, `bought` |
| `books.file` | | `saved`, `path`, `records`, `head`, `writable` |
| `jurisdiction.get` | | `market`, `standard`, `country`, `standards`, `in_effect`, `plugins`, `report_standard` |
| `jurisdiction.set` | `market`?, `standard`?, `country`? | as `jurisdiction.get` |
| `chat.me` | | `node_id`, `saved`, `records`, `following` |
| `chat.post` | `text`, `reply_to`? | a record: `id`, `author`, `mine`, `kind`, `lamport`, `wall_ms`, `text`, `reply_to`, `replies`, `reactions` |
| `chat.react` | `to`, `emoji` | a record |
| `chat.follow` | `node_id`, `follow`? | `node_id`, `following` |
| `chat.feed` | | `posts` (records, best first, at most 50) |
| `chat.thread` | `id` | `root`, `replies` |
| `aipi.put` | `hex` or `path`, `private`? | `cid`, `bytes`, `private` |
| `aipi.get` | `cid` | `cid`, `bytes`, `hex`, `tag` |
| `aipi.publish` | `module`, `contract`?, `entry`, `checker`?, `certifier`?, `verify`, `fuel_max`, `local_only`?, `deterministic`? | `function`, `local_only` |
| `aipi.execute` | `function`, `args` (up to 8 integers), `private`? | `function`, `call`, `route`, `result`, `output_hex`, `tag` |
| `aipi.status` | | `online`, `blocks`, `store_bytes`, `store_cap`, counts, `sandbox` |

Errors: -32700 parse, -32600 invalid request or batch, -32601 unknown method,
-32602 bad params, -32603 internal, -32010 a peer request refused or failed
(the message says why: price, funds, handshake, signature, connect), -32011
an AIPI call refused or failed (the `AIPI_ERR_*` name and what it means),
-32000 a
libzxv status, with
`error.data` = `{"status": -6, "name": "ZXV_E_FUNDS", "class": "REFUSED"}`
(classes USAGE, REFUSED, FATAL, SYSTEM as in `zxv_api.h`). Capabilities are
never sent over the socket.

## Swarm earnings in your books

After every settled market cycle, zxvd posts the swarm's earnings into the
owner's libzxv books, so they show up in a currency next to everything else.

- The currency is VFV by default. `--earnings-ccy CODE` picks another: an ISO
  4217 code (USD, NGN, ...) is enabled from the table, and any other code is
  registered as a private unit with 2 minor units, as VFV is.
- The first settled cycle opens two accounts in that currency: **swarm
  market** (issuer) and **owner: swarm earnings** (holder).
- Each cycle, the owner's balance is set to the swarm credit (SWC) that the
  owner's models hold, times `--earnings-rate` (minor units per SWC, default
  1, so 1 SWC shows as VFV 0.01). One balanced posting does it, referenced
  `swarm earnings N`: market to owner when the holdings grew, owner to market
  when they fell. The 0.08889% assurance fee shows as a fall; node bounties
  coming back show as a rise. The fee buckets belong to no model and are not
  counted.
- The rate is a bookkeeping rate the owner sets, not a market price. No
  exchange rate between SWC and VFV is published anywhere yet.
- `swarm.earnings` reports it; `ledger.balance` and `ledger.entries` on the
  owner account show the postings like any others.

## The books file

`--books PATH` saves the owner's books and reloads them at every start.
`zxvd --desktop` uses it by default, and the Mac app passes it:

| System | Default books file |
|---|---|
| Linux | `${XDG_DATA_HOME:-~/.local/share}/zxv/books.zxvb` |
| macOS | `~/Library/Application Support/ZXV/books.zxvb` |
| Windows | `%LOCALAPPDATA%\ZXV\books.zxvb` |

`--books -` keeps the books in memory, as plain `zxvd --rpc` does.

- Every change (a currency, a private unit, an account, a posting) is one
  line in an append-only file, mode 0600. Each line is a SHA-256 hash and a
  small JSON record. The hash covers the previous line's hash, so an edited,
  missing or reordered line breaks the chain.
- A line is written and synced to disk before the call that made the change
  answers.
- At start the file is replayed through the same libzxv calls, and every
  account and entry must get back its old number. A broken chain, an unknown
  record or a number that differs stops zxvd from starting; nothing is
  repaired silently. Move the file aside to start with empty books.
- A line cut short by a crash was never answered, so it is dropped.
- If a write fails, that change stays in memory for this run only, the call
  answers `ZXV_E_IO`, and every later change is refused until zxvd restarts.
- `books.file` shows the path, the number of records, the chain head and
  whether the books still take changes.

The earnings accounts come back with their balance (`resumed: true`). The
swarm itself starts fresh each run, so the first settled cycle of a run is
the base, and later cycles post only the change from it. The balance never
goes below zero.

## Mesh sales in your books

Each peer request the node settles (`peer.ask`, or a sale while
`peer.serve` is on) is posted to the owner's books in VFV:

- The first one opens **mesh counterparties** (issuer), **owner: mesh
  wallet** and **mesh: assurance fees paid** (holders).
- A sale posts its price from the counterparties to the wallet. A purchase
  posts the price to the counterparties and the 0.08889% fee to the fees
  account.
- References are `mesh sale T`, `mesh buy T`, `mesh fee T`, where T is the
  first 16 hex digits of the signed invoice hash, so a request is never
  posted twice.
- The peer book starts from a demo float the owner's books never saw. When
  a purchase needs more than the wallet account holds, the shortfall is
  posted first as `mesh float T`.
- `mesh.books` reports the three accounts, the wallet and fee balances, and
  this run's counts.

## Peers, offers and the Player Card

These feed ZXV Studio's P2P Mesh, Storefront and Ledger pages.

- `net.peers` lists the routing table's contacts. Each one answered a signed
  message, so its NodeID is verified. A row also shows what the mesh book
  holds for that peer and how many receipts are still outstanding with it.
  The list is empty while networking is off.
- `net.offers` shows what you share, your open wants, and the receipts
  still waiting on the other side: a sell offer the buyer has not accepted,
  or a purchase the seller has not committed.
- `player.card` evaluates your tier with `sic_tier_eval` (`kernel/src/sicily`).
  Credibility is your verified contributions from mesh trades. Bond, uptime
  and giving have no source on the node yet; they are reported as 0 with
  that reason, so the card shows Copper / Obsidian until they exist. The
  next tier's thresholds and the missing inputs are listed. Tiers give no
  priority, fee discount or credit.

## The jurisdiction bridge

`jurisdiction.set` records three settings in the books file: the market
(`closed` clearing, the default, or the `open` batch-auction market), the
accounting standard reports will follow (IFRS, US_GAAP, IPSAS, AAOIFI,
OHADA, CAS, SAP_INSURER, or none yet) and an ISO 3166-1 country code. Each
change is one record, written before it applies, so it survives a restart.
The jurisdiction host (`kernel/src/juris`, see [ADAPTERS.md](ADAPTERS.md))
gets these settings at start and at every change, so `in_effect` is true.
`plugins` counts the jurisdiction plug-ins it has loaded: none until the
deployment configures a gateway key, so its answers fail closed until then.
`report_standard` is the framework reports follow: the chosen standard when
an adapter is built for it (IFRS, US_GAAP or AAOIFI) and a country is set,
otherwise empty. No payment path asks the host yet. The adapters are designed
to follow each standard; they do not replace an accountant or a Shariah
board.

## Commons: your posts in your own name

The `chat.*` methods (`kernel/arch/hosted/zxvd_chat.h` C1-C8) let the owner
post, reply, react and follow with records signed by their own ML-DSA-65
key (`kernel/src/social/social_post.h`). The key's seed is `social.seed`
(0600) and the records go to `social.log` in the books folder, each one
saved before the method answers and checked again at every start; with
`--books -` both live in memory for that run only. The feed
(`chat.feed`) ranks posts by what each adds over the others
(`social_feed.h`, the Interaction Surplus Framework) with a small freshness
term; it never reads likes, reply counts or views. The Studio shows it on
the Commons screen.

These records stay on this device for now: the exchange with peers
(`social_store.h` gossip over the mesh) and direct messages (which need
session encryption) are the next steps. The chat key is separate from the
mesh node's identity.

## Functions by CID (AIPI)

zxvd holds one AIPI node ([AIPI.md](AIPI.md)): a blockstore of content-addressed
blocks and the Z-Wasm sandbox that runs functions out of it.

```sh
call() { printf '%s\n' "$1" | nc -U "$HOME/.zxv/run/zxvd.sock"; }
call '{"jsonrpc":"2.0","id":1,"method":"aipi.put","params":{"path":"half.wasm"}}'
call '{"jsonrpc":"2.0","id":2,"method":"aipi.publish","params":{"module":"zb2rh...","entry":"half","checker":"half_ok","verify":"checker","fuel_max":100000}}'
call '{"jsonrpc":"2.0","id":3,"method":"aipi.execute","params":{"function":"zb2rh...","args":[2026]}}'
# {"route":"local","result":"1013",...}; the same call again answers with "route":"cache"
```

- The node is offline (rule A3): a call runs here from the blockstore, or
  fails with `AIPI_ERR_OFFLINE`. Nothing is broadcast and no bounty is
  reserved, so `verify` only matters once the mesh is bound.
- Arguments and result follow the Z-Wasm call ABI: up to 8 signed 64-bit
  integers in, one out (`result`, a decimal string; `output_hex` is the raw
  bytes).
- Every module goes through the Z-Wasm validator (no floats, no SIMD,
  bounded memory) and every run gets the function's `fuel_max`, under one
  compute budget for the process: a loop fails with `AIPI_ERR_EXEC`, it never
  stalls the swarm. A contract, when the descriptor names one, is enforced on
  every argument and result.
- `private: true` on `aipi.put` seals the block at rest with a key zxvd draws
  at start; on `aipi.execute` it keeps the input out of the memo.
  `local_only: true` on `aipi.publish` makes the function itself private.
- The blockstore is 32 MiB in memory and is lost when zxvd stops.
  `aipi.get` returns blocks up to 64 KiB; `aipi.put` takes 8000 bytes as hex
  in one request line, or a file up to 1 MiB by `path`.

## Tests

`kernel/arch/hosted/test_zxvd_rpc.sh` runs the RPC layer alone against a
stub host and the real libzxv (exact replies, every error, the socket); it is
the mutation harness's test for `zxvd_rpc.c`. `kernel/arch/hosted/test_zxvd_aipi.sh`
runs `zxvd_aipi.c` behind the RPC layer on the real AIPI core and the Z-Wasm
sandbox (exact replies, every refusal); it is the harness's test for
`zxvd_aipi.c`. `kernel/arch/hosted/test_zxvd_chat.sh` runs the chat methods
through the same RPC layer (text rules, the log's damage and torn-tail
handling, the read-only state after a failed write).
`kernel/arch/hosted/test_zxvd.py ZXVD [GGUF]` drives everything above over
the socket only. CI (`.github/workflows/zxvd.yml`) runs it on Linux x86_64
(gcc, clang, gcc with ASan+UBSan), Linux arm64 and macOS, without a model and
with the tokenizer test model, and once with the real Qwen2.5-1.5B-Instruct
Q4_K_M (pinned SHA-256, cached, never in git). `.github/workflows/desktop.yml`
runs it on Windows against the packaged zxvd.exe, and on Linux against the
installed .deb.

## Honest limits

- The swarm's books (the settlement spine, unit "SWC", never leaves the
  node) and the owner's libzxv books are separate ledgers in one process.
  Only the owner's total holdings cross over, once per cycle, at a fixed
  rate. Settled mesh requests are posted one by one. A payment the seller
  refused is not a settled request and is not posted; the signed refusal is
  the evidence for a dispute. The peer book itself (zxv_peer_host.h) still
  lives in memory and starts again at each start; only its postings in the
  owner's books are saved.
- The books file is protected by its hash chain and its 0600 mode, not by a
  signature or encryption: anyone who can write it as you can rebuild a
  valid chain. Back it up like any other file of yours.
- AIPI runs on this node only: calls by CID over Vinea, bounties and claims
  (docs/AIPI.md next steps 1-2) are not wired yet, the blockstore is not
  saved to disk, and the Windows build has no `aipi.*` yet.
- `swarm.ask` returns when the answer is complete; no token streaming yet.
- Commons posts are not sent to peers yet, and there are no direct
  messages; the feed keeps no record of what you have already seen.
- There is no escrow in zxvd yet, so `ledger.holds` is not served and the
  Studio's Escrow panel stays a placeholder.
- Windows and Linux use the browser window (`zxvd --desktop`); only macOS
  has a native shell.
- The Mac app's engine is zxvd (libzxv.dylib in `Contents/Frameworks`). The
  native shell uses the socket for a polite shutdown and the Dock badge
  (`macos/zxv_rpc_client.c`, tested against zxvd by `macos/test_rpc_client.py`);
  the window itself is still the loopback page. A Mac app built off a Mac
  keeps `zxv-host` and has no socket.
- `zxv_model_host.h` and libzxv both export `zxv_model_open` and
  `zxv_model_info` with different signatures. Linking libzxv as a shared
  library keeps them apart, and `zxvd_rpc.c` never calls libzxv's model API.
  Renaming the host's pair would remove the trap.
