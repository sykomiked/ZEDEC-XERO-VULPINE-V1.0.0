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

It is `zxv_host.c` built with `-DZXVD`, plus `zxvd_rpc.c`, linked against
`libzxv.so` / `libzxv.dylib`. The desktop app (`zxv-host`) is unchanged.

## Build and run

```sh
kernel/arch/hosted/build_zxvd.sh /tmp/zxvd          # builds libzxv, then zxvd
/tmp/zxvd/zxvd --rpc "$HOME/.zxv/run/zxvd.sock" --model path/to/model.gguf
```

Every `zxv-host` option works too (`--net`, `--peer`, `--models-dir`, ...).
zxvd opens no window; the loopback HTML window is still served on a free
port with its token, printed on the `ZXV-URL:` line.

## The socket

- JSON-RPC 2.0, one request per line, one response per line.
- A UNIX socket, mode 0600, in a folder zxvd creates 0700 (an existing folder
  must belong to the same user and not be writable by group or others). Only processes of
  the same user are served (checked with `SO_PEERCRED` / `getpeereid`).
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

Errors: -32700 parse, -32600 invalid request or batch, -32601 unknown method,
-32602 bad params, -32603 internal, -32010 a peer request refused or failed
(the message says why: price, funds, handshake, signature, connect), -32000 a
libzxv status, with
`error.data` = `{"status": -6, "name": "ZXV_E_FUNDS", "class": "REFUSED"}`
(classes USAGE, REFUSED, FATAL, SYSTEM as in `zxv_api.h`). Capabilities are
never sent over the socket.

## Tests

`kernel/arch/hosted/test_zxvd_rpc.sh` runs the RPC layer alone against a
stub host and the real libzxv (exact replies, every error, the socket); it is
the mutation harness's test for `zxvd_rpc.c`. `kernel/arch/hosted/test_zxvd.py ZXVD [GGUF]` drives everything above over
the socket only. CI (`.github/workflows/zxvd.yml`) runs it on Linux x86_64
(gcc, clang, gcc with ASan+UBSan), Linux arm64 and macOS, without a model and
with the tokenizer test model, and once with the real Qwen2.5-1.5B-Instruct
Q4_K_M (pinned SHA-256, cached, never in git).

## Honest limits

- The swarm's books (the settlement spine, unit "SWC", never leaves the
  node) and the owner's libzxv books are separate ledgers in one process.
  How swarm settlement should show up in the owner's books is an owner
  decision; nothing moves between them yet.
- `swarm.ask` returns when the answer is complete; no token streaming yet.
- Windows: zxvd builds without the RPC socket (`--rpc` fails there).
- The Mac app's engine is zxvd (libzxv.dylib in `Contents/Frameworks`). The
  native shell uses the socket for a polite shutdown and the Dock badge
  (`macos/zxv_rpc_client.c`, tested against zxvd by `macos/test_rpc_client.py`);
  the window itself is still the loopback page. A Mac app built off a Mac
  keeps `zxv-host` and has no socket.
- `zxv_model_host.h` and libzxv both export `zxv_model_open` and
  `zxv_model_info` with different signatures. Linking libzxv as a shared
  library keeps them apart, and `zxvd_rpc.c` never calls libzxv's model API.
  Renaming the host's pair would remove the trap.
