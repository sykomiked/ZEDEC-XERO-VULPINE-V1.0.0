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
| JSON tokenizer | `kernel/src/web4/web4_json.c` | parses every request |
| Freight packets, harmonic wire frames, stream segments, Endian Hopping frames, Web 4 CIDs and agent cards (pure codecs, no state) | `kernel/src/freight`, `harmonic`, `stream`, `ehop`, `web4` via `zxvd_rpc_modules.c` | `freight.*`, `harmonic.*`, `stream.*`, `ehop.*`, `web4.*` |

It is `zxv_host.c` built with `-DZXVD`, plus `zxvd_rpc.c`, linked against
`libzxv.so` / `libzxv.dylib`. The desktop app (`zxv-host`) is unchanged.

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
| `swarm.earnings` | | `currency`, `rate_minor`, `source_account`, `owner_account`, `balance_minor`, `postings`, `cycles` |

Errors: -32700 parse, -32600 invalid request or batch, -32601 unknown method,
-32602 bad params, -32603 internal, -32010 a peer request refused or failed
(the message says why: price, funds, handshake, signature, connect), -32000 a
libzxv status, with
`error.data` = `{"status": -6, "name": "ZXV_E_FUNDS", "class": "REFUSED"}`
(classes USAGE, REFUSED, FATAL, SYSTEM as in `zxv_api.h`). Capabilities are
never sent over the socket.

### Module methods

`zxvd_rpc_modules.c` serves five kernel modules beside the methods above
(`zxvd_rpc_modules.h` X1-X4). They are pure codecs: no state between calls,
and they never open the books, post to a ledger or touch the swarm. Bytes
travel as hex (`*_hex`); 64-bit values as decimal strings. Input a module
refuses (too few rows, a hash or an AEAD tag that does not verify) is error
-32011 with the reason; a bad parameter is -32602.

| Method | Params | Result |
|---|---|---|
| `freight.pack` | `data_hex` (at most 3360 bytes), `k`?, `freight_id`? | `freight_id`, `k`, `payload_len`, `rows`, `header_hex`, `packets_hex` (168 x 21 bytes; any k rebuild) |
| `freight.unpack` | `header_hex`, `packets_hex` (any k distinct packets) | `freight_id`, `k`, `flags`, `payload_len`, `packets_received`, `data_hex` |
| `harmonic.encode` | `trunk`, `shell`?, `sync`?, `s_plus` (3 words), `s_minus` (2 words) | `tag`, `trunk`, `trunk_hz`, `frame_hex` (21 octets) |
| `harmonic.decode` | `frame_hex`, `threshold_q16`? | `tag`, `trunk`, `shell`, `sync`, `axis_trunk`, `trunk_hz`, `s_plus`, `s_minus`, `idle`, `correlation_q16`, `truth` |
| `stream.plan` | `units` ([len, pts_us, keyframe], at most 120), `target_us`, `max_us`, `max_bytes`?, `tail_us`? | `segments` |
| `stream.segment` | `data_hex`, `k`?, `stream_id`?, `seq`? | `cid`, `nfreights`, `freight_id`, `freight_sha256`, ... |
| `ehop.bands` | | `bands` |
| `ehop.seal` | `key_hex`, `band`, `chan_id`, `epoch`, channel fields?, `sender`, `seq`, `plaintext_hex` | `frame_hex`, `band`, `priority`, `sender`, `seq`, `frame_len` |
| `ehop.open` | `key_hex`, `band`, `chan_id`, `epoch`, channel fields?, `frame_hex` | `plaintext_hex`, `band`, `sender`, `seq` |
| `web4.cid` | `data_hex` or `cid` | `cid`, `uri`, `version`, `codec`, `bytes32_hex` |
| `web4.agent_card` | `seed_hex`, `name`, `bound_ms`? | `agent_id`, `peer_id`, `name`, `bound_ms`, `self_bound`, `pk_hex`, `bind_sig_hex` |
| `web4.card_verify` | `name`, `bound_ms`, `pk_hex`, `bind_sig_hex`, `agent_id`? | `valid`, `agent_id`, `reason`? |

`ehop.*` keeps no channel: the caller owns the nonce rule (never seal two
plaintexts under the same key, channel, configuration, sender and seq) and the
replay window. `web4.agent_card` takes the agent's secret seed over the
owner-only socket and wipes the derived secret key before answering.

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

## Tests

`kernel/arch/hosted/test_zxvd_rpc.sh` runs the RPC layer alone against a
stub host and the real libzxv (exact replies, every error, the socket); it is
the mutation harness's test for `zxvd_rpc.c`; the same script then runs
`test_zxvd_rpc_modules.c`, every module method's results and refusals, and
checks that none of them changes the books. `kernel/arch/hosted/test_zxvd.py ZXVD [GGUF]` drives everything above over
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
  rate. Mesh sales to peers are not posted yet, and the owner's earnings
  account starts again at each start, because the books are not saved to
  disk yet.
- `swarm.ask` returns when the answer is complete; no token streaming yet.
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
