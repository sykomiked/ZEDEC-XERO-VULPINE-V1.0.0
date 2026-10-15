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
| The Hoard behind the built-in apps (the Banker, Quillon, Glint; [STAPLE_APPS.md](STAPLE_APPS.md)) | `zxv_files.c` | `files.*`, and the window's `POST /api/rpc` |
| Spaces: home, offices, teams, cloud top, parks, with rights per role, held crossings and member votes ([STAPLE_APPS.md](STAPLE_APPS.md#spaces)) | `zxv_spaces.c` | `spaces.*`, `files.cross`, the `space` parameter of `files.*` |
| The machine and zxvd for the system utilities (Almanac, Tidewatch, Sounding; [SYSTEM_UTILITIES.md](SYSTEM_UTILITIES.md)), read only | `zxv_sysinfo.c` | `sys.info`, `sys.disk`, `sys.net` |
| Recent events for the Wake console: refusals and changes, never parameters | `zxvd_log.c` | `log.tail` |
| The same events kept on disk across restarts, hash-chained so a change shows (`--journal DIR`) | `zxv_journal.c` | `journal.query`, `journal.verify` |
| Location, saved places and per-app location permission for the Waymark app (`--places FILE`) | `zxv_place.c` | `place.*` |
| Screen time for the Shepherd app: minutes per app per day and a guardian's limits (`--screen FILE`) | `zxv_screen.c` | `screen.*` |
| Search for the Scent app: an index per space of file names and the words in UTF-8 files, kept up to date incrementally, read only | `zxv_search.c` | `search.query`, `search.refresh` |
| Printing and scanning for the Platen app: IPP Everywhere and eSCL to devices on the local network only | `zxv_print_host.c`, `kernel/src/print` | `print.*`, `scan.*` |
| Backups with versions for the Keepsake app: content-addressed copies and checked snapshots in a folder the owner picks | `zxv_keepsake.c` | `backup.*` |
| JSON tokenizer | `kernel/src/web4/web4_json.c` | parses every request |
| Freight packets, harmonic wire frames, stream segments, Endian Hopping frames, Web 4 CIDs and agent cards (pure codecs, no state) | `kernel/src/freight`, `harmonic`, `stream`, `ehop`, `web4` via `zxvd_rpc_modules.c` | `freight.*`, `harmonic.*`, `stream.*`, `ehop.*`, `web4.*` |

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
| `iso20022.render` | `kind` (`pacs008`, `camt053`), `fields`, `amounts`?, `entries`?, `tx_count`? | `kind`, `xml` |
| `card.check` | `network` (`phoenix`, `dragon`, `thunderbird`), `pan` | `network`, `valid` |
| `netting.open` | `cycle`, `currency` | a cycle: `cycle`, `currency`, `closed`, `participants` |
| `netting.add` | `cycle`, `participant` | a cycle |
| `netting.submit` | `cycle`, `payment_id`, `debtor`, `creditor`, `amount_minor` | `cycle`, `payment_id`, `accepted` |
| `netting.close` | `cycle` | a cycle with `positions` and `sum_minor` |
| `netting.positions` | `cycle` | as `netting.close` |
| `netting.list` | | `cycles` |
| `netting.drop` | `cycle` | `dropped` |
| `passkey.register` | `rp_id`, `authenticator_data` (hex), `require_uv`? | `credential_id`, `sign_count`, `registered` |
| `passkey.verify` | `rp_id`, `credential_id`, `authenticator_data`, `client_data_hash`, `signature` (hex), `require_uv`? | `credential_id`, `verified`, `sign_count` |
| `ledger.hold` | `from`, `to`, `amount_minor` | a hold: `hold`, `state`, `from`, `to`, `amount_minor`, `currency`, `escrow_account`, `entry_id`, `closed_entry_id`, `releases_on` |
| `ledger.release` | `hold` | a hold |
| `ledger.refund` | `hold` | a hold |
| `ledger.holds` | | `holds`, `escrow_accounts`, `unmatched` |
| `aipi.put` | `hex` or `path`, `private`? | `cid`, `bytes`, `private` |
| `aipi.get` | `cid` | `cid`, `bytes`, `hex`, `tag` |
| `aipi.publish` | `module`, `contract`?, `entry`, `checker`?, `certifier`?, `verify`, `fuel_max`, `local_only`?, `deterministic`? | `function`, `local_only` |
| `aipi.execute` | `function`, `args` (up to 8 integers), `private`? | `function`, `call`, `route`, `result`, `output_hex`, `tag` |
| `aipi.status` | | `online`, `blocks`, `store_bytes`, `store_cap`, counts, `sandbox` |
| `features.get` | | `features` (`name`, `enabled`, `regulated`, `groups`), `saved` |
| `features.set` | `feature`, `enabled`? (default true) | `feature`, `enabled`, `regulated`, `saved` |
| `cards.status` | | `issuer`, `network`, `prefix`, `issuer_code`, `fee_minor`, counts, `ledger_balanced`, `receipts_ok`, `in_memory` |
| `cards.check_pan` | `pan` | `digits`, `iin`, `luhn`, `damm`, `verhoeff`, `network` |
| `cards.iso8583_parse` | `hex` | `mti`, `fields` |
| `cards.issuer` | `network`, `issuer_code`?, `fee_minor`?, `seed`? | as `cards.status` |
| `cards.open_account` | `now`? | the account |
| `cards.account` | `account` | `account`, `state`, `held_minor`, `unbilled_minor`, `billed_minor`, `owed_minor`, `due` |
| `cards.issue` | `account`, `form`, `holder_pk`, `entropy`?, `now`? | the card (`card`, `pan`, `network`, `state`, `exp_year`, `exp_month`, `atc`, `replaces`, ...) |
| `cards.reissue` | `card`, `holder_pk`?, `entropy`?, `now`? | the new card |
| `cards.freeze` | `card`, `frozen`? (default true) | the card |
| `cards.card` | `card` or `pan` | the card |
| `cards.challenge` | `pan`, `amount_minor`, `form`, `atc`, `un`, `stan`, `terminal_id`, `merchant_id`, `currency`?, `time`? | `challenge`, `context`, `time` |
| `cards.authorize` | the challenge's fields with `time` and `sig`, or `emv` | `auth`, `approved`, `rc`, `decline`, `approval`, `cryptogram`, `amount_minor`, `iso8583` |
| `cards.capture` | `auth` | `auth`, `state`, `txn`, `receipt`, `amount_minor`, `fee_minor`, `digest`, `account` |
| `cards.reverse` | `auth`, `now`? | `auth`, `state`, `released_minor` |
| `cards.refund` | `auth`, `now`? | as `cards.capture` |
| `cards.pay` | `account`, `amount_minor`, `now`? | the account |
| `cards.advance` | `now`? | `statements`, `new_statements`, `delinquent` |
| `passkeys.status` | `now`? | `rp_id`, `alg`, `attestation`, `credentials`, `active`, `open_challenges`, `max_credentials`, `in_memory` |
| `passkeys.register_begin` | `user`, `rp_id`?, `origin`?, `require_uv`?, `now`? | `type`, `challenge`, `rp_id`, `origin`, `alg`, `require_uv`, `expires`, `user` |
| `passkeys.register_finish` | `challenge`, `auth`, `now`? | the credential (`cred_id`, `user`, `rp_id`, `sign_count`, `revoked`, `registered`, `last_used`) |
| `passkeys.auth_begin` | `user`?, `rp_id`?, `origin`?, `require_uv`?, `now`? | as `register_begin`, with `allow` |
| `passkeys.auth_finish` | `challenge`, `cred_id`, `auth`, `sig`, `now`? | `verified`, `uv`, `credential` |
| `passkeys.list` | `user`? | `credentials` |
| `passkeys.revoke` | `cred_id` | the credential |
| `vault.deposit` | `fee_minor`, `now`? | `fee_minor`, `reserve_minor`, `dividend_minor`, `bounty_minor`, `regen_minor`, `swept_minor`, `kept_minor`, `carry` |
| `vault.buy` | `seller`, `price_minor`, `max_qty`?, `now`? | `bought`, `cost_minor`, `price_minor`, `seller` |
| `vault.first_loss` | `loss_vbills`, `now`? | `burned`, `uncovered` |
| `vault.status` | | `reserve_minor`, `cash_minor`, `outstanding`, `held`, `cap`, `room`, `floor`, counters, fee buckets |
| `vault.check` | `floor_num`?, `floor_den`?, `backing`? | `ok`, `vault_ok`, `ledger_ok`, `solvent` |
| `swap.deposit` / `swap.withdraw` | `holder`, `amount_minor`, `now`? | `holder`, `vino_minor`, `vbills` |
| `swap.enable` | `enabled`? (default true) | `enabled`, `epoch` |
| `swap.quote` | `direction` (`in`/`out`), `amount` | `direction`, `rate`, `par`, `vbills`, `vino_minor` |
| `swap.in` / `swap.out` | `holder`, `amount_minor` / `vbills`, `now`? | as `swap.quote`, `holder` |
| `swap.lock` | `holder`, `tier` (`medium`/`deep`), `vbills`, `reinvest`?, `now`? | `position`, `tier`, `vbills`, `notice_period`, `reinvest` |
| `swap.notice` / `swap.unlock` | `holder`, `position`, `now`? | `position`, `unlock_at` / `vbills`, `holder` |
| `swap.dividend` | `now`? | `pool_minor`, `units`, `paid_minor`, `reinvest_minor`, `vbills`, `postings` |
| `swap.status` / `swap.check` | | switch, rate, reserve, holders, positions / `ok`, `status` |
| `auction.configure` | `floor_num`?, `floor_den`?, `backing`?, `cap`?, `p_start`?, `p_step`?, `p_min`? | the rules |
| `auction.open` | | `epoch`, `state` |
| `auction.bid` | `holder`, `qty`, `limit_minor`, `now`? | `epoch`, `bid`, `locked_minor` |
| `auction.run` / `auction.cancel` | `now`? | the epoch's record (`price_minor`, `qty`, `winners`, reserve and outstanding before and after, `floor_ok`) |
| `auction.quote` | `price_minor`, `now`? | `reserve_minor`, `outstanding`, `supply`, `demand`, `floor_ok` |
| `auction.status` / `auction.check` | `now`? | state, rules, bids, history / `ok`, `floor_ok`, `ledger_ok` |
| `zenny.mint` | `minter`, `kappa`, `legs`, `rules`?, `co_minter`?, `nonce`?, `now`? | `chip`, `slot`, `nonce`, `version`, `chips` |
| `zenny.quote` | `chip`, `want`, `prices`? | `chips` |
| `zenny.decompress` | `chip`, `chips`, `prices`, `now`? | the receipt (`out`, `value`, `total`, `fee`, ...) |
| `zenny.receipts` | `ack`? | `receipts`, `next`, `acked` |
| `zenny.status` / `zenny.check` | | `chips`, `receipts_pending` / per chip `chain_ok`, `claim_ok`, `ok` |
| `stats.snapshot` | `now`?, `req_num`?, `req_den`?, `min_cohort`? | `epoch`, `prev`, `digest`, `signed`, `encoded`, `snapshot` |
| `stats.status` | | `published`, `head`, `signed` |
| `bonds.setup` | `juris`, `gateway`, `terms`?, `weights`?, `share_num`?, `share_den`? | `juris`, `gateway`, `lock_account`, `terms`, `share_num`, `share_den`, ... |
| `bonds.key` | `key`, `public_key` | `key`, `added`, `keys` |
| `bonds.account_open` | `owner`, `asset` | `account`, `owner`, `asset` |
| `bonds.fund` | `account`, `amount_minor` | `account`, `balance_minor`, `sandbox` |
| `bonds.account` | `account` | `account`, `owner`, `asset`, `balance_minor`, `credit_minor`, `sandbox` |
| `bonds.switch` | `enable`?, `epoch`, `sig` | `issuing_enabled`, `epoch` |
| `bonds.message` | `kind`, `enable`?, `epoch`?, `serial`?, `to`?, `payee`? | `message`, `bytes`, `scheme` |
| `bonds.issue` | `owner`, `vbill_account`, `payee`, `qty`, `k`, `now`? | `serial`, `owner`, `payee`, `qty`, `k`, `start`, ... |
| `bonds.transfer` | `serial`, `from`, `to`, `payee`, `sig` | `serial`, `owner`, `payee`, `qty`, `k`, `start`, ... |
| `bonds.redeem` | `serial`, `owner`, `vbill_account`, `now`? | `serial`, `returned_qty`, `vbill_account` |
| `bonds.credit` | `src`, `pool`, `amount_minor`?, `price`?, `floor`?, `qty`?, `now`? | `credited_minor`, `pool_minor`, `premium` |
| `bonds.distribute` | `pool`, `now`? | `pool_minor`, `units`, `paid_minor`, `payees`, `postings`, `paid` |
| `bonds.status` |  | `set_up`, `keys`, `issuing_enabled`, `juris`, `gateway`, `switch_epoch`, ... |
| `derivatives.setup` | `juris`, `gateway`, `first_epoch`? | `juris`, `gateway`, `call_escrow`, `long_escrow`, `short_escrow`, `reserve`, ... |
| `derivatives.key` | `key`, `public_key` | `key`, `added`, `keys` |
| `derivatives.account_open` | `owner`, `asset` | `account`, `owner`, `asset` |
| `derivatives.fund` | `account`, `amount_minor` | `account`, `balance_minor`, `sandbox` |
| `derivatives.account` | `account` | `account`, `owner`, `asset`, `balance_minor`, `credit_minor`, `sandbox` |
| `derivatives.switch` | `enable`?, `epoch`, `sig` | `opening_enabled`, `epoch`, `leverage` |
| `derivatives.message` | `kind`, the fields the kind signs | `message`, `bytes`, `scheme` |
| `derivatives.push` | `epoch`, `price`, `qty`, `now`? | `epoch`, `index`, `moved`, `stopped`, `fixed`, `fees_minor` |
| `derivatives.call_open` | `nonce`?, `writer_key`, `writer`, `buyer_key`, `buyer`, `strike`, `cap`, `size`, `expiry`, `premium`, `writer_sig`, `buyer_sig`, `now`? | `id`, `nonce`, `lock_minor`, `premium_minor`, `fee_minor`, `expiry` |
| `derivatives.call_settle` | `id`, `now`? | `id`, `fixing`, `payoff_minor`, `fee_minor`, `to_buyer_minor`, `to_writer_minor` |
| `derivatives.perp_open` | `nonce`?, `long_key`, `long_account`, `short_key`, `short_account`, `size`, `band`, `lock_long`, `lock_short`, `long_sig`, `short_sig`, `now`? | `id`, `nonce`, `ref_index`, `opened_epoch` |
| `derivatives.perp_close` | `id`, `side`, `sig`?, `now`? | `id`, `to_long_minor`, `to_short_minor` |
| `derivatives.status` |  | `set_up`, `keys`, `leverage`, `opening_enabled`, `juris`, `gateway`, ... |
| `grants.setup` | `funnel`, `underwriters`?, `min_uptime_bps`?, `min_volume`?, `min_stake`?, `cap_num`?, `cap_den`? | `funnel`, `pool`, `reserve`, `issuer`, `underwriters`, `min_uptime_bps`, ... |
| `grants.key` | `key`, `public_key` | `key`, `added`, `keys` |
| `grants.account_open` | `owner`, `asset` | `account`, `owner`, `asset` |
| `grants.fund` | `account`, `amount_minor` | `account`, `balance_minor`, `sandbox` |
| `grants.account` | `account` | `account`, `owner`, `asset`, `balance_minor`, `credit_minor`, `sandbox` |
| `grants.epoch` | `epoch` | `epoch` |
| `grants.pool_credit` | `from`, `amount_minor`, `now`? | `pool_minor`, `inflow_minor` |
| `grants.lot_open` | `lot` | `lot`, `label`, `status` |
| `grants.eval` | `subject`, `uptime_bps`, `dropped_reports`?, `routing_reports`?, `failed_challenges`?, `fraud_findings`?, `volume`, `stake` | `subject`, `eligible`, `mask`, `reasons`, `epoch` |
| `grants.appeal` | `subject`, `epoch`, `mask`, `sig` | `subject`, `mask`, `epoch` |
| `grants.distribute` | `lot`, `price`, `recipients`, `now`? | `barrels`, `cost_minor`, `postings`, `shares`, `pool_minor`, `label` |
| `grants.attest` | `lot`, `count`, `underwriter`, `expiry`, `sig`, `now`? | `lot`, `backed`, `label`, `status` |
| `grants.dispute` | `lot`, `sig` | `lot`, `backed`, `disputed` |
| `grants.redeem` | `holder`, `holder_vino`, `lot`, `n`, `price`, `now`? | `lot`, `barrels`, `paid_minor`, `held` |
| `grants.message` | `kind`, the fields the kind signs | `message`, `bytes`, `scheme` |
| `grants.status` | `now`? | `set_up`, `keys`, `epoch`, `inflow_minor`, `weight`, `rate_defined`, ... |
| `estate.setup` | `owner`, `guardians`, `k`, `attestors`, `court`, `gateway`, `wait`?, `release`? | `guardians`, `k`, `attestors`, `wait`, `release`, `phase` |
| `estate.manifest` | `enclave`, `heirs`?, `husband`?, `wives`?, `father`?, `mother`?, `sons`?, `daughters`?, `others`?, `ids`?, `jurisdiction`?, `children`?, `gifts`?, `will_den`? | `enclave`, `advisory`, `heirs`, `digest` |
| `estate.faraid` | `husband`?, `wives`?, `father`?, `mother`?, `sons`?, `daughters`?, `others`?, `amount_minor`? | `classes`, `unallocated`, `awl`, `radd`, `amount_minor`, `advisory`, ... |
| `estate.report` | `attestor`, `bond_minor`, `sig`, `now`? | `phase`, `epoch` |
| `estate.approve` | `guardian`, `sig` | `approvals`, `k` |
| `estate.confirm` | `now`? | `phase`, `epoch` |
| `estate.rule` | `sig` | `phase`, `epoch` |
| `estate.begin` |  | `phase`, `epoch` |
| `estate.close` |  | `phase`, `epoch` |
| `estate.veto` | `sig` | `phase`, `epoch` |
| `estate.fraud` | `who`, `sig` | `bond` |
| `estate.contest` | `heir`, `who`, `sig` | `heir`, `contested` |
| `estate.resolve` | `heir`, `sig` | `heir`, `released_minor` |
| `estate.distribute` | `amount_minor` | `parts`, `to_escrow_minor`, `held_minor`, `moved` |
| `estate.stream` | `amount_minor` | `parts`, `to_escrow_minor`, `held_minor`, `moved` |
| `estate.message` | `kind`, `bond_minor`?, `heir`? | `message`, `bytes`, `scheme` |
| `estate.status` | `now`? | `set_up`, `phase`, `epoch`, `bond_minor`, `bond`, `approvals`, ... |
| `recovery.setup` | `root`, `guardians`, `gateway`, `first_device`, `timelock`? | `timelock`, `rate_window`, `rate_max`, `state` |
| `recovery.claim` | `new_key`, `now`? | `state`, `claim`, `approvals`, `vetoed` |
| `recovery.approve` | `guardian`, `sig` | `state`, `claim`, `approvals`, `vetoed` |
| `recovery.veto` | `device`, `sig` | `state`, `claim`, `approvals`, `vetoed` |
| `recovery.cancel` | `device`, `sig` | `state`, `claim`, `approvals`, `vetoed` |
| `recovery.device_add` | `key`, `sig` | `active` |
| `recovery.device_revoke` | `key`, `sig` | `active` |
| `recovery.finalize` | `now`? | `state`, `claim`, `approvals`, `vetoed` |
| `recovery.gateway` | `sig` | `gateway_verified` |
| `recovery.message` | `kind`, `key`? | `message`, `bytes`, `scheme` |
| `recovery.status` | `now`?, `vbill_minor`?, `liquid_minor`? | `set_up`, `state`, `claim`, `approvals`, `needed`, `vetoed`, ... |
| `companion.dna` | | `agent_id`, `seed`, `genes` (8 by name), `traits` (`home`, `*_permille`, `specialty`, `specialty_name`), `parent_a`, `parent_b`, `founder`, `generation` |
| `companion.generation` | | `agent_id`, `generation`, `seed`, `steps`, `growths`, `last_step` |
| `companion.mood` | | `agent_id`, `feeling`, `home`, `at_home`, `swarm_mood` (each feeling: `emotion`, `emoji`, `codepoint`, `intensity`, `charge`, `valence`) |
| `web4.open` | `name`?, `seed`?, `now`? | `open`, `new`, `agent_id`, `name`, `pk`, `envelopes`, `held`, `max_envelopes`, `max_held`, `in_memory` |
| `web4.post` | `kind`, `to`, `tool`?, `payload`?, `req_id`?, `chunk`?, `final`?, `pii`?, `amount_minor`?, `now`? | the entry (`id`, `kind`, `status`, `from`, `to`, `tool`, `req_id`, `chunk`, `final`, `payload`, `pii`, `amount_minor`, `seq`, `ts`, `consent`, `answers`, `grant`, `bytes`) |
| `web4.consent` | `id`, `grant`, `expires_in`?, `now`? | `envelope`, `consent` (both entries) |
| `web4.feed` (read-only) | `before`?, `limit`?, `status`? | `open`, `agent_id`, `envelopes`, `next`, `total`, `held` |
| `web4.envelope` (read-only) | `id` | `id`, `kind`, `bytes`, `envelope` |
| `web4.verify` (read-only) | `id` or `envelope`, `pk`? | `valid`, `reason` or the envelope's fields |
| `seats.list` | | `seats` (`id`, `name`, `role`, `lock`, `color`, `wait_ms`), `auto_enter` |
| `seats.add` | `name`, `role`?, `lock`?, `pass`?, `color`?, `session`? | the seat |
| `seats.remove` | `id`, `session` | `removed` |
| `seats.set_lock` | `id`, `lock`, `old_pass`?, `pass`?, `session` | the seat |
| `seats.unlock` | `id`, `pass`?, `passkey_ok`? | `ok`, `wait_ms`, then `session`, `id` or `status` |
| `seats.lock` | `session` | `ended` |
| `seats.auto` | | `seat` (an id or null) |
| `seats.set_auto` | `auto_enter`, `session` | `auto_enter` |
| `phone.link` | `on`, `port`?, `bind`? | `on`, `port` |
| `phone.pair` | `name`, `allow_post`? | `phone_id`, `name`, `allow_post`, `host`, `port`, `link` |
| `phone.list` | | `on`, `port`, `phones` |
| `phone.revoke` | `phone_id` | `revoked`, `closed` |
| `irc.status` (read-only) | | `on`, `listening`, `port`, `clients`, `password`, `cert_sha256`, `cert_file`, `signer`, `signer_id`, `delivered`, `refused`, `bridge` |
| `irc.record` (read-only) | `msgid` | `msgid`, `target`, `nick`, `truth`, `ms`, `text`, `signer_id`, `context`, `public_key`, `signature`, `record` |
| `irc.bridge` | `on`, `host`, `port`?, `remote`, `local`, `nick`, `password`? | the bridge (or null) |
| `pspace.create` | `kind`, `gov`, `quorum_pct`?, `name`, `topic`? | `cid` |
| `pspace.find` | `topic` | `topic`, `done`, `found` |
| `pspace.join` / `pspace.leave` | `cid` | `cid`, `phase` / `left` |
| `pspace.post` | `cid`, `text` | `cid`, `posted` |
| `pspace.events` | `cid`, `since`? | `events`, `next` |
| `pspace.members` | `cid` | `members`, `count` |
| `pspace.list` | | `online`, `spaces` |
| `hall.load` / `hall.play` / `hall.pause` / `hall.seek` | `cid`, then `media` / `pos_ms`?, `rate_q16`? / `pos_ms`? / `pos_ms` | the `hall.status` object |
| `hall.status` | `cid` | `set`, `state`, `media`, `pos_ms`, `at_ms`, `rate_q16`, `now_ms`, `viewers`, `may_control` |

Errors: -32700 parse, -32600 invalid request or batch, -32601 unknown method,
-32602 bad params, -32603 internal, -32010 a peer request refused or failed
(the message says why: price, funds, handshake, signature, connect), -32011
an AIPI call refused or failed (the `AIPI_ERR_*` name and what it means),
-32012 the method's feature is off (see "Feature switches" below),
-32017 a module method refused well-formed input (see "Module methods"),
-32020 a payment step refused (netting, passkey or escrow; the message says
why), -32040 a card operation the issuer refused, -32041 a passkey ceremony
refused (the message says why), -32050 a vault, swap, auction, Zenny or
stats engine refused (the message is its status name, e.g.
`VSWAP_ERR_DISABLED`, or `VAUC_ERR_LEDGER (PAY_ERR_FUNDS)`),
-32060 a bond, derivative, grant or estate engine refused (the message is
`<STATUS>: why`, see "Bonds, derivatives, grants and estates" below),
-32070 no companion yet
(`companion.*` before the swarm has an agent),
-32090 a Web4 feed operation refused (the message says why, see "The Web4
envelope feed" below),
-32096 an IRC gateway refusal (the message says why, see "The IRC gateway"
below),
-32080 a seat refused (the message is `<SEAT_ERR_*>: why`, see "Seats"
below), -32095 power tracking is not running or a budget's target is
unknown (see "Power" below),
-32032 a public space or watch hall request refused (the message says why,
see "Public spaces and the watch hall" below),
-32014 a files.* refusal, -32013 a spaces.* refusal,
-32015 a method a phone may not call (see below), -32016 a play.* call
refused or failed (the message says why), -32022 a printer or scanner
refused or out of reach (`print.*`, `scan.*`), -32018 a backup refused
(`backup.*`), -32019 a screen-time refusal (`screen.*`), -32021 a place
refused (`place.*`), -32030 a people method (`social.*`, `call.*`,
`xlate.*`, `devmesh.*`, `ui.*`) refused, -32031 `xlate.translate` with no
local model that can generate (nothing is invented); each message says why. -32000 a
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
-32017 with the reason; a bad parameter is -32602.

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
## The phone link

ZXV Companion ([mobile/companion](../mobile/companion)) reaches zxvd from a
phone over an encrypted TCP link (`zxvd_phone.c`). It is off by default.

- `phone.link {"on": true}` (or `--phone-link`, `--phone-port N`,
  `--phone-bind IP`) listens on TCP 47410 on all private, loopback and
  link-local IPv4 addresses; `bind` picks one, and a public address is
  refused. `{"on": false}` stops listening and drops every phone.
- `phone.pair {"name", "allow_post"}` makes a pairing and returns its link,
  `zxv-phone:1?h=HOST&p=PORT&id=<32 hex>&k=<64 hex>&n=NAME`, which ZXV Studio
  shows as a QR code. The key is in the link only; zxvd never logs it.
  Pairings live in `phones` next to the socket, mode 0600, at most 16.
- `phone.revoke` deletes a pairing and closes its connections at once.
- The `phone.*` methods are served on the control socket only.

Protocol `zxv-phone/1`, a Noise NNpsk0-style handshake:

1. Phone: `"ZXP1"`, the 16-byte pairing id, a fresh X25519 key `e_c`.
2. zxvd: its fresh key `e_s`, then `HMAC-SHA256(k_conf, "server confirm" | th)`.
   `th = SHA-256("zxv-phone/1" | id | e_c | e_s)`; the keys `k_c2s | k_s2c |
   k_conf` are HKDF-SHA256 with salt = the pairing key, input = the X25519
   shared secret | `th`, info `"zxv-phone/1 keys"`, 96 bytes. The phone checks
   the confirmation before it sends anything.
3. Then frames: a 4-byte big-endian length, then ChaCha20-Poly1305 of one
   JSON-RPC line with the length as associated data, nonce = 4 zero bytes |
   a 64-bit big-endian counter per direction. A frame that fails to open
   closes the connection.

Requests up to 16 KiB, replies up to 4 MiB, up to 4 phones at once, 10 s to
finish the handshake, and an idle phone is dropped after 60 s.

What a phone may call: everything read-only, plus `swarm.ask` and
`notes.mark_read`. A phone paired with `allow_post` may also call
`net.command`, `peer.ask`, `currency.enable`, `ledger.open_account` and
`ledger.post`. `zxvd.shutdown`, `peer.serve`, `jurisdiction.set`, `aipi.put`,
`aipi.publish`, `aipi.execute`, `phone.*`, the people methods (`social.*`,
`call.*`, `xlate.*`, `devmesh.*`, `ui.*`), and the system utilities and
apps' methods (`sys.*`, `log.tail`, `journal.*`, `screen.*`, `place.*`,
`search.*`, `print.*`, `scan.*`, `backup.*`) are never served to a phone. Refusals are -32015, and a phone must send an `id`.

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

`--journal DIR` keeps zxvd's events on disk for Wake's history
([SYSTEM_UTILITIES.md](SYSTEM_UTILITIES.md)). `zxvd --desktop` and the Mac
app keep it in a `journal` folder beside the books file; `--journal -` turns
it off. If the folder cannot be made or written, zxvd says so and runs
without it.

`--screen FILE` keeps Shepherd's screen time and limits
([SYSTEM_UTILITIES.md](SYSTEM_UTILITIES.md)). `zxvd --desktop` and the Mac
app keep `screen.zxst` beside the books file; without it, or with
`--screen -`, screen time lives in memory and is gone when zxvd stops.
`--places FILE` does the same for Waymark's saved places and location
answers (`places.zxpl`); the location itself is never written.

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

## Feature switches and method groups

Engines that move or record money, credit, securities or identity data plug
into zxvd as method groups (`kernel/arch/hosted/zxvd_ext.h`), each behind a
named feature switch. Every feature starts **off**. `zxvd --enable
netting,cards` (or `--enable all`) turns features on at start;
`features.set {"feature":"netting","enabled":true}` turns one on or off while
zxvd runs. With a books file, each `features.set` change is saved there as
one record and replayed at start, so a restart keeps the owner's choice
(`"saved": true`); without one it lasts for this run. `--enable` is never
saved, and a saved "off" does not override it. While a feature is off its methods answer -32012 and do
nothing, except status and description methods that move no money.
`rpc.discover` lists every group's methods whether on or off, so the method
list never depends on the switches.

`features.get` marks a feature `regulated` when what it does is regulated
somewhere (payments, cards, credit, securities, identity checks). Turning one
on is the owner's choice for a test or a licensed deployment; zxvd claims no
licence, membership or compliance. The engines keep their state in memory
for the life of the process; turning a feature off and on again in the same
run keeps it.

## Cards and passkeys

Two method groups sit behind feature switches that start off (`zxvd
--enable cards,passkeys`, or `features.set`).

**cards** (regulated) runs the Dragon / Phoenix / Thunderbird charge-card
issuer from `kernel/src/cardnet` inside zxvd:

- `cards.issuer` picks the network once per run. Then `cards.open_account`
  and `cards.issue` open accounts and issue cards. Each card is bound to the
  holder's ML-DSA-65 public key.
- To pay, the holder's device gets the bytes to sign from `cards.challenge`
  and signs them. `cards.authorize` checks that signature, the counter, the
  card's state and its limits, then approves (placing a hold) or declines.
  A decline is a normal result with its ISO 8583 response code and the 0110
  message. The request can also come as the EMV-shaped TLV container.
- `cards.capture` settles an approval in full (DR 555 / CR 777 and one
  hash-chained receipt). `cards.reverse` releases a hold, and `cards.refund`
  reverses a captured charge. `cards.pay` and `cards.advance` run the
  statement cycle. It is a charge card: the bill is due in full and nothing
  in it can hold interest.
- `cards.check_pan` (Luhn, Damm, Verhoeff, network) and
  `cards.iso8583_parse` are plain tools and answer while the feature is off.
- No secret leaves zxvd. The holder's private key never comes to it. The
  issuer seed comes from the OS and is never returned. `seed` and `entropy`
  exist so that tests can be repeated exactly.

**passkeys** makes zxvd a WebAuthn-shaped relying party for ML-DSA-65
passkeys (`kernel/src/ident`). It does authentication only, so it is not
marked regulated.

- `register_begin` and `auth_begin` hand out single-use challenges that
  expire after 300 s.
- `register_finish` stores the public key from authenticatorData. The
  attestation format is "none", so zxvd learns nothing about the
  authenticator model.
- `auth_finish` rebuilds clientDataJSON from the challenge and origin and
  verifies the signature. The signature counter must go up.
- `list` shows the credentials and `revoke` stops one from logging in.
- Private keys stay on the authenticator.

Honest limits:

- Both groups keep everything in memory. A restart forgets every issuer,
  card, authorization and credential, and the tables are small (8 accounts,
  16 cards, 32 authorizations, 32 credentials).
- Dragon, Phoenix and Thunderbird are not members of, certified by or
  affiliated with any card scheme or EMVCo. Their 880/882/884 numbers and
  F0 AIDs are unregistered and valid inside one deployment only.
- Nothing here is PCI DSS assessed or licensed. Turning `cards` on is the
  owner's choice for a test or a licensed deployment.
- Captures and refunds are for the full amount only.
- Browsers and FIDO servers do not accept ML-DSA passkeys today.
- ident's root identity, recovery (Argon2id, BCH fuzzy extractor, Shamir
  guardians) and optional KYC are not on the socket yet. They have no wire
  format here, and KYC needs a licensed verifier's adapter.
## Economy modules

Five method groups serve the economy engines merged in #66, each behind its
own switch (`kernel/arch/hosted/zxvd_econ.c`): `vault` (the protocol
solvency vault, `kernel/src/svault`), `swap` (Vino to V-Bills and back, lock
tiers and the dividend, `vswap`), `auctions` (V-Bill issuance by
uniform-price descending auction, `vauction`), `zenny` (Zenny chips,
`zenny`) and `stats` (the aggregate statistics feed, `zstats`). The first
four are regulated; `stats` is read-only. All start off.

```sh
zxvd --enable vault,swap,auctions,zenny,stats
call '{"jsonrpc":"2.0","id":1,"method":"swap.deposit","params":{"holder":7,"amount_minor":20000}}'
call '{"jsonrpc":"2.0","id":2,"method":"swap.enable"}'
call '{"jsonrpc":"2.0","id":3,"method":"swap.in","params":{"holder":7,"amount_minor":1000}}'
# 10 V-Bills at par (100 Vino minor units each)
call '{"jsonrpc":"2.0","id":4,"method":"vault.deposit","params":{"fee_minor":10000}}'
# reserve 5000, dividend pool 2500, bounties 1500, regenerative 1000; the vault sweeps 1666
```

- **One economy, one reserve.** The groups share one in-memory pay_ledger:
  Vino (2 minor units), V-Bills, and the system accounts. The reserve-floor
  bucket of the assurance fee is the vault's, the swap's and the auction's
  reserve, as the engine headers intend: fees, swap-ins and auction fiat land
  there; the vault sweeps a third of the fee's reserve share out of it;
  swap-outs are paid from it. One V-Bill issuer serves all three.
- **What zxvd does not have comes in as parameters.** zxvd sees no fee flow:
  `vault.deposit` records a fee collected elsewhere (split exactly into the
  four buckets, then swept). Holders are numbers you choose;
  `swap.deposit` / `swap.withdraw` move a holder's Vino into and out of these
  books. Engines that keep time take `now` (unix seconds, default the clock).
- **Signatures are stand-ins.** zxvd holds no jurisdiction-gateway, minter,
  oracle, custodian or publisher key. `swap.enable` and `zenny.mint` are
  signed by zxvd itself, which proves only that its own method made the
  call. A co-minter's signature and a custodian's attestation never verify,
  so bilateral chips and commodity legs are refused. The auction accepts no
  warehouse receipt or price quote, so its reserve is the reserve account
  alone. Statistics snapshots are hash-chained but not signed.
- **Auctions are backed only.** `auction.run` clears at one uniform price
  and issues only what keeps reserve * floor_den >= floor_num * backing *
  outstanding; the fiat goes to the reserve and the rest of every lock is
  refunded.

Honest limits:

- In memory only: nothing here is saved, it is not your books (`ledger.*`),
  and a restart starts empty. Nothing is connected to a bank, custodian,
  oracle or central bank.
- V-Bills, locked positions with a dividend share, and Zenny chips are likely
  securities (or deposits, e-money or collective investment schemes) in many
  jurisdictions. No exemption is claimed; turning these features on is for a
  test or a licensed deployment. The swap's own switch is not a legal
  opinion.
- "Zenny" is a currency name in Capcom games and has had no trademark
  clearance.
- zxvd's only caller check is the owner-only socket (T1/T2): whoever can use
  the socket acts for every holder. zxvd always passes one holder's own
  pair of accounts, but the engines themselves do not check who owns them.
- Open findings from the 2026-10-12 adversarial audit (section 4), waiting on
  the owner and not designed around here: `vswap`, `vauction` and `svault`
  take two accounts per party and authenticate no caller; `vswap` and
  `vauction` posting ids (`VSWAP-<seq>`, `VAUC-<seq>`) are predictable, so
  any other poster on the same ledger could squat one (only zxvd posts to
  this one); anyone who can post to the ledger can deposit into vault cash
  and break its audit (S6); `svault_sweep` moves reserve funds without
  checking the solvency floor; and a custodian attestation can be replayed
  after a Zenny rebalance (zxvd serves no rebalance and no commodity leg).

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

## Payments: messages, cards, netting, passkeys and escrow

`kernel/arch/hosted/zxvd_pay.h` (P1-P5) puts libzxv's payment pieces on the
socket. Every call goes through libzxv's frozen C ABI, so a refusal is the
same `ZXV_E_*` status the SDKs see (-32000); the module's own refusals are
-32020 with a message that says why.

Switches (P6): `iso20022.render` and `card.check` only compute and are always
served. `netting.*` sit behind the `netting` feature and `ledger.hold`,
`ledger.release` and `ledger.refund` behind `escrow`; both are regulated and
start off. `passkey.*` share the `passkeys` feature with `passkeys.*`.
`ledger.holds` is a read and answers while escrow is off. Turn one on with
`zxvd --enable escrow` or `features.set {"feature":"escrow"}` (saved in the
books file).

- **ISO 20022** (`iso20022.render`): a pacs.008.001.09 credit transfer or a
  camt.053.001.08 statement, built field by field by libzxv. Amounts are
  minor units of `fields.ccy` (the ISO 4217 minor units decide the decimal
  places); a statement must reconcile (opening + booked entries = closing).
  The XML is returned, never sent.
- **Cards** (`card.check`): the check digit of the platform's three networks
  (phoenix: Damm, dragon: Luhn, thunderbird: Verhoeff). Nothing is issued or
  authorized.
- **Netting** (`netting.*`): multilateral netting cycles on the cbank
  engine (`kernel/src/cbank/cb_net.h`) through libzxv. Open a cycle in one
  currency, add participants, submit obligations (idempotent on
  `payment_id`), close it, and read each participant's net position; the
  positions always sum to zero (libzxv halts its context otherwise). The
  cycles have a libzxv context of their own: they touch no account, need no
  books, and at most 8 are held.
- **Passkeys** (`passkey.*`): ident's relying-party check
  (`kernel/src/ident/ident.h`) for device-bound ML-DSA-65 passkeys in
  WebAuthn's shape: registration from the authenticator data, then each
  assertion's signature, relying party, user presence, user verification
  (required unless `require_uv` is false) and a signature counter that must
  grow.
- **Escrow** (`ledger.hold`, `ledger.release`, `ledger.refund`,
  `ledger.holds`): a hold moves funds from the payer's account to the holder
  account `escrow: <CCY>` (opened with the first hold in that currency);
  release pays them to the payee, refund returns them to the payer. Each step
  is one ordinary posting with the reference `escrow N to T`,
  `escrow N release` or `escrow N refund`, so the books file saves it and a
  restart replays it: `ledger.holds` reads every hold back from the escrow
  accounts' entries, says whether each account holds exactly what its open
  holds add up to, and counts escrow entries that are no hold's step
  (`unmatched`). The Studio's Escrow panel lists the holds and releases or
  refunds them.

```sh
call '{"jsonrpc":"2.0","id":0,"method":"features.set","params":{"feature":"escrow"}}'
call '{"jsonrpc":"2.0","id":1,"method":"ledger.hold","params":{"from":1,"to":2,"amount_minor":600}}'
# {"hold":1,"state":"held",...,"escrow_account":4,"closed_entry_id":null,"releases_on":"owner"}
call '{"jsonrpc":"2.0","id":2,"method":"ledger.release","params":{"hold":1}}'
```

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

## Bonds, derivatives, grants and estates

Four method groups put the economy engines behind the socket, each behind
its own switch:

| Feature | Regulated | Engine | Methods |
|---|---|---|---|
| `bonds` | yes | V-Bonds (`kernel/src/vbond`) | `bonds.*` |
| `derivatives` | yes | V-Calls and V-Perps (`kernel/src/vderiv`) | `derivatives.*` |
| `grants` | yes | barrel grants (`kernel/src/grants`) | `grants.*` |
| `estate` | no | estate succession, account recovery, faraid (`kernel/src/estate`) | `estate.*`, `recovery.*` |

How they work:

- Each group starts empty. `<group>.setup` configures the engine; until then
  its methods answer -32060 "`<group>` is not set up". Status methods
  (`*.status`, `*.account`, `*.message`) answer while the switch is off.
- Bonds, derivatives and grants move money on a **sandbox ledger** per group
  (the kernel's `pay_ledger`),
  with two assets, VINO (2 decimals) and VBILL (whole units), and accounts
  opened with `*.account_open` and filled from a sandbox issuer with
  `*.fund`. Nothing on these ledgers is posted to the owner's books, and
  `*.fund` creates sandbox money from nothing: it is a test bench. The
  estate group keeps no ledger.
- Signers are Ed25519 keys registered with `*.key` (`key` = the key id; give
  a party the same id as the ledger owner of its accounts, which the
  derivatives engine is moving to require). `*.message`
  returns the exact bytes the engine checks, as hex; the caller signs them
  and passes the 64-byte signature as `sig` (hex). A list of keys
  (`guardians`, `attestors`) is the 32-byte keys concatenated in one hex
  string.
- 64-bit results are decimal strings; `now` is optional unix seconds
  (default: the clock).
- A refusal answers -32060 with the engine's status name and why, for
  example `VBOND_ERR_SIG: ...` or `GRANT_ERR_INELIGIBLE: ...`.

Who may call: anyone who can open the owner's socket, as for every other
method. The groups add no caller authentication of their own; the engine
signatures authorise the parties to a contract, not the socket caller.

Honest limits:

- In memory only: every group's state and sandbox ledger is lost when zxvd
  stops, and none of it reaches the owner's books or the books file.
- The regulated features (`bonds`, `derivatives`, `grants`) start off, as
  every feature does. On top of the zxvd switch each engine has its own
  jurisdiction switch (`bonds.switch`, `derivatives.switch`), signed by a
  gateway key the owner chooses at setup; it starts off too.
- A V-Bond pays a share of realised revenue, a V-Call or V-Perp pays an
  index difference: they are very likely securities and derivatives where
  you are. zxvd claims no exemption, registration or licence; turning them
  on is for a test or a licensed deployment.
- No interest: a V-Bond has no rate and no coupon, grants are not loans, and
  nothing in these groups charges or pays usury. V-Calls and V-Perps are
  fully collateralised: no leverage (`"leverage":false` always), no margin
  calls, no funding rate.
- `estate.faraid` is a computation of one common majority treatment of the
  Quranic shares, not a ruling: a qualified scholar or court decides. The
  estate group moves no money (`"moves_money":false`); `estate.distribute`
  and `estate.stream` only compute the split.
- `recovery.*` counts the notices a claim would send to the owner's devices
  but delivers none (`"notices_delivered":false`).
- The signatures are Ed25519, not the ML-DSA the engines are meant to be fed
  in production.
- The framework has no string-array reader, so key lists are concatenated
  hex; amounts above 2^63-1 are refused; one request line holds about 20
  `grants.distribute` recipients.

## Seats: the people on this device and the login after boot

After the boot vault opens, the native UI shows a login screen: as many
people as the owner wants on one device, each with their own lock. The
`seats.*` methods serve the roster from `kernel/src/seats`
(`kernel/arch/hosted/zxvd_seats.c`). They sit in a group with the feature
`seats`, but every method is marked to answer while the switch is off: the
login has to work at boot, before anyone could turn a switch on. Seats move
no money and are not marked regulated.

- A seat is an `owner`, a `person` or a `guest`, with the lock `none`,
  `pass` (a passphrase of 4 to 256 bytes) or `passkey`. A passphrase is
  stored only as its Argon2id hash (RFC 9106, 64 MiB, 3 passes, 1 lane) with
  a salt of its own; zxvd takes the 64 MiB for each call that hashes and
  wipes it before answering, so a login takes a moment. `seats.list` never
  shows a hash, a salt or the parameters.
- The first `seats.add` on an empty device needs no session; that seat
  becomes the owner whatever role was asked for. A guest never has a lock.
- `seats.unlock` logs in. On success it answers
  `{"ok":true,"wait_ms":0,"session":"<64 hex>","id":...}`: 32 random bytes
  bound to that seat, kept in memory only (a restart ends every session; at
  most 64 at once, a new one replaces the oldest). `seats.lock` ends a
  session.
- A wrong entry is a result, not an error:
  `{"ok":false,"status":"SEAT_ERR_WRONG","wait_ms":N}`. Four wrong tries
  cost nothing; the fifth starts a 30 s wait and each one after doubles it,
  up to an hour. A try while waiting answers `"status":"SEAT_ERR_WAIT"` with
  the milliseconds left. `seats.list` shows the same `wait_ms` per seat. A
  correct entry clears the count.
- `seats.add` (after the first), `seats.remove`, `seats.set_lock` and
  `seats.set_auto` act as the seat of their `session`. Only an owner adds or
  removes seats or turns `auto_enter` off and on; a person may change their
  own lock (with `old_pass` when they had a passphrase; leaving it out is a
  wrong try) and an owner may
  reset anyone's. The last owner cannot be removed. Removing a seat ends its
  sessions.
- `seats.auto` names the seat to enter without a login screen: the only
  seat, when it has no lock and `auto_enter` is on; else `null`. Enter it
  with `seats.unlock {"id": ...}`, which needs nothing for a seat with no
  lock.
- Refusals are -32080 with the status name first, e.g.
  `SEAT_ERR_DENIED: this session may not do that`, `SEAT_ERR_NAME`,
  `SEAT_ERR_LAST_OWNER`, `SEAT_ERR_NOTFOUND`. An unknown or ended session is
  `SEAT_ERR_DENIED`.

With a books file the roster is saved as `seats` in the same folder: one
text file, mode 0600, rewritten whole to `seats.tmp`, flushed and renamed
over the old one, so a crash leaves either roster. Failed tries and waits
are saved too, so a restart does not reset the back-off. A change that
cannot be saved is undone and answers -32603. At start the file is checked
field by field; a damaged one stops zxvd from starting (move it aside to
start with no seats). Without a books file the roster lasts for this run.

Honest limits:

- A passkey seat takes the host's word: `passkey_ok: true` from any client
  on the socket opens it. zxvd does not tie the seat to a `passkeys.*`
  credential yet, and the module has no call that records one.
- The socket's peer check is the only thing between a same-user process and
  these methods, as for every other method; a session token proves an
  unlock to zxvd, not who is at the screen.
- The seats file holds Argon2id hashes, not plaintext, but it is not
  encrypted or signed: anyone who can read it as you can try passphrases
  offline at Argon2id's cost, and anyone who can write it can replace the
  roster. A short PIN is allowed (the person's choice) and is weak against
  that.
- A guest's space is not wiped when it signs out: zxvd keeps no per-seat
  space yet. Seats do not separate the books, the chat or the swarm.
- Sessions do not expire on their own.
- The folder is not flushed after the rename, so a power cut right after a
  change can bring back the roster from before it (never a torn one).
- The Windows path (`_open`, `MoveFileExA`) is compiled only by the Windows
  build; the unit test and the mutation score cover the POSIX path.

## The companion: DNA, generation and mood

The person's Chiglet companion is the swarm's agent 0, the one seat on
level 0 (`kernel/src/swarm/swarm_evolve.h` V4). `companion.*`
(`kernel/arch/hosted/zxvd_companion.c`, rules H1-H6 in `zxvd_companion.h`)
reads it from the live swarm: a fresh snapshot per call, nothing cached.
The group sits behind the feature `companion`, which is not regulated, and
every method answers with the switch off: they read and move no money.

- `companion.dna`: the 64-bit seed (decimal string, D1), the 8 genes by
  name in `swarm_gene_t` order (emotion, temperament, curiosity, caution,
  warmth, pace, verbosity, specialty), the traits they express (D2: the home
  feeling, five traits in permille, the specialty index and its name), the
  parents' seeds and the generation (D5). `founder` is true while both
  parents are 0.
- `companion.mood`: the companion's current feeling in the emotional
  economy (`swarm_emotion.h`): `emotion`, its `emoji` and `codepoint`, the
  `intensity` (0..5), the Fibonacci `charge` (0, 1, 2, 3, 5, 8; always 0 for
  neutral) and the `valence` (+1, 0, -1). Beside it, the `home` feeling its
  DNA gives it (feelings drift back toward home), `at_home` (same emotion),
  and the swarm's cycle mood (E1).
- `companion.generation`: the generation, and how it got there in this run:
  `steps` (times it evolved), `growths` (GROWTH harmonics seen) and
  `last_step` (the fundamental of the last step, 0 for none).

**How the companion evolves (new wiring).** Before this, the GROWTH harmonic
bred levels 1 and up only and the companion's DNA never changed. Now, on
each GROWTH harmonic (about every 11 s) and before the levels breed, the
companion takes one ratchet step (`swarm_evolve.h` V7,
`swarm_evo_companion`): the candidate is its DNA combined (`swarm_dna_combine`,
D3) with the fittest other agent's, nonce = the fundamental, judged by
`swarm_evo_judge` (V1) with the companion's own fitness (D4: gated value plus
Social capital) against the donor's. Costs are equal, so it steps only when
the donor is strictly fitter. A step gives it the child's DNA: a new seed,
both parents recorded and the generation max(parents') + 1, so a step can
raise the generation by more than one. Its feeling resets to the new home.
The companion keeps its DNA when the swarm is rebuilt for a new number of
levels. All integers.

Limits: the donor's measured fitness stands in for the child's, which has
not run yet, so this is a proxy and not a measurement of the child. When
every agent's fitness ties (the stand-in market can leave them all equal),
the companion does not step, and a run can go many growths without one. The
step can change the home feeling and specialty with a gene. The companion's
DNA is not saved: each start grows a new one from that run's instance seed
(taken from the clock), at generation 0.
## Power: energy per app and agent, battery and budgets

`power.*` (`kernel/arch/hosted/zxvd_power.c`, rules W1-W8 in
`zxvd_power.h`) serves the power ledger (`kernel/src/power`), fed from this
machine by `zxv_power_host.c`. What each OS can and cannot measure is in
`docs/POWER.md`; nothing is invented, and a host with no meter reports
energy as `"none"` with CPU time only. The group sits behind the feature
`power`, which is not regulated (an energy cap is the owner's own
ecological limit, not a payment).

- `power.status` (always on): the whole ledger as integer JSON: device and
  host, the last interval's total energy and power and its source, the
  battery, the posture and its token scale, the meters and every consumer.
- `power.top` (always on): `kind` (`app`, `agent` or `all`), `lifetime`
  and `n` (1..64, default 10): consumers by energy, highest first, each with
  `id`, `kind`, `name`, `parent`, `uj`, `mw`, `source`, `lifetime_uj` and
  `cpu_ns`. Idle and shared energy stays with the `system` consumer.
- `power.budget` (switch on): `target` `device`, `app` (with `app`, the
  executable name) or `agent` (with `agent`, its id), `window_ms` (0
  clears), `max_uj` and `strict`. Sets an energy budget per rolling window.

zxvd samples the host and settles the ledger at most once a second. It is
an app named after its own executable, and the companion is an agent under
it: around each model call zxvd notes the process CPU time the call took
(the engine's worker threads included). Before a model call zxvd asks the
ledger to admit the companion: when its own, zxvd's or the device's budget
window is spent, or a strict budget has nothing measured to judge by, the
model does not run and the answer says "Energy budget reached". Nothing
buys past a budget. The battery posture scales the swarm's tokens per
cycle: x1 on mains or without a battery, x0.618 on battery, x0.382 at 20%
or below, x0 at 5% or below. Error -32095 means power tracking is not
running in this zxvd or the budget's target is unknown.

Limits: the stand-in agents (levels 1 and up) run no model, so only the
companion is noted. GPU time is not measured yet (`gpu_ns` is 0). Budgets
and the ledger are in memory and start again at each start.

## Public spaces and the watch hall

`pspace.*` and `hall.*` (`kernel/arch/hosted/zxvd_pspace.c`, rules W1-W6 in
`zxvd_pspace.h`) put the node's Vinea spaces (`zxv_net_host.h` U14,
`kernel/src/vinea/vna_space.h`) on the socket: rooms and watch halls named
by a CID, hosted by whoever is in them, with a playhead every viewer
computes the same. The group sits behind the feature `pspace`, which is not
regulated; `pspace.leave`, `pspace.events`, `pspace.members`, `pspace.list`
and `hall.status` answer with the switch off. Every method needs the
network on (`net.command` `lan` or `online`); refusals are -32032 with the
reason. The protocol, the methods and the limits (control only: delivering
the video between viewers is not built yet) are in
[SPACES.md](SPACES.md).

## The Web4 envelope feed

Web4 is Chiglet companions talking to each other. The `web4` group
(`kernel/arch/hosted/zxvd_web4.c`) gives the owner's companion an ML-DSA-65
key and a feed of the envelopes it signs, using the envelope, consent and
signature code of `kernel/src/web4/web4_agent.h`. The feature starts off
(`zxvd --enable web4`, or `features.set`). It is not marked regulated:
envelopes that would move money only wait for the owner's consent here, and
nothing is paid.

```sh
# {"open":true,"new":true,"agent_id":"<64 hex>","name":"chiglet","pk":"<3904 hex>",...}
web4.open {}
# a plain REQUEST is signed at once: {"id":1,"kind":"REQUEST","status":"signed","seq":"1",...}
web4.post {"kind":"REQUEST","to":"<their agent_id>","payload":"hello"}
# personal data or money is held, unsigned: {"id":2,...,"status":"held","seq":null,"bytes":0}
web4.post {"kind":"REQUEST","to":"<their agent_id>","tool":"share","payload":"my address","pii":true}
# the owner answers: a signed CONSENT entry (id 3), then envelope 2 is signed
web4.consent {"id":2,"grant":true}
```

- Kinds: `web4.post` takes REQUEST, RESPONSE and STREAM (`req_id` is
  required for the last two; `chunk` and `final` mark stream chunks). A
  CONSENT entry is made only by `web4.consent`.
- Consent: an envelope with `pii: true` or `amount_minor` above 0 is
  **held**. It has no signature and no bytes; `web4.envelope` and
  `web4.verify` refuse it with -32090. `web4.consent` with `grant: true`
  issues a consent token signed by the owner's key, bound to the envelope's
  exact action (tool, parties, request id, payload), its scope (personal
  data, money) and ceiling (`amount_minor`), valid `expires_in` seconds
  (default 600) and single-use; then the token is attached and the envelope
  signed. `grant: false` wipes the payload and the envelope is never
  signed. Either way the answer is a CONSENT entry, signed too, that names
  the envelope it answers (`answers`; the envelope's `consent` points
  back). At most 32 envelopes wait at once and 64 unexpired grants exist at
  once (both refused with -32090 beyond that).
- Reads: `web4.feed` (newest first; pass `next` as `before` for the next
  page; `status` filters), `web4.envelope` (a signed entry's bytes as hex,
  what a carrier would deliver) and `web4.verify` (an entry by `id`, or
  bytes from another companion with its `pk`) only read, and answer while
  the feature is off. `web4.verify` returns `valid: false` with a reason
  (`malformed`, `signature`, `the key is not the sender's`, `consent`).

Honest limits:

- No delivery: nothing is sent to or received from another node yet.
  `web4.envelope` hands the signed bytes to whatever carries them; there is
  no inbox, so incoming envelopes are not held or acted on here.
- Whether an envelope carries personal data or money is what the caller
  declares (`pii`, `amount_minor`); zxvd does not inspect the payload.
- The consent token is signed by the same owner key that zxvd holds, so it
  records the owner's answer on this socket, not a confirmation on a
  separate device. `web4.verify` checks a token's signature and binding but
  not its time window or single use, which are the receiver's to enforce.
- In memory only: 64 entries (the oldest entry that is not held makes room),
  and a restart forgets the feed and makes a new key unless `seed` is given
  again. Payloads are text, at most 2048 bytes; envelopes are signed, not
  encrypted. A `web4.verify` request line holds about 6000 envelope bytes
  with a `pk`, so a large envelope with a consent token is checked by `id`.

## The IRC gateway

`irc.*` (`kernel/arch/hosted/zxvd_irc.c`, rules G1-G8 in `zxvd_irc.h`; the
protocol engine is `zxv_irc.c`, rules E1-E9 in `zxv_irc.h`) lets any IRC
client talk to the swarm. The feature `irc` starts off; while it is on,
zxvd listens on 127.0.0.1 only (`--irc-port N`, default 6697), over TLS 1.3
only, with a self-signed certificate the client pins. Every message carries
its Belnap tag (`[T] `, `[F] `, `[B] `, `[N] `) and is signed with
ML-DSA-87 at the gateway; `#swarm` asks the swarm. It never carries money,
keys or Player Card actions. Bridging one channel to a public network is
`irc.bridge`, off until called. The design, what the tag and signature do
and do not mean, and the threat model are in
[docs/IRC_GATEWAY.md](IRC_GATEWAY.md).

```sh
zxvd --rpc ~/.zxv/zxvd.sock --enable irc   # prints ZXVD-IRC: TLS 127.0.0.1:6697
irc.status {}                              # password, cert_file (irc-cert.pem), cert_sha256
```

## Tests

`kernel/arch/hosted/test_zxvd_rpc.sh` runs the RPC layer alone against a
stub host and the real libzxv (exact replies, every error, the socket); it is
the mutation harness's test for `zxvd_rpc.c`; the same script then runs
`test_zxvd_rpc_modules.c`, every module method's results and refusals, and
checks that none of them changes the books. `kernel/arch/hosted/test_zxvd_aipi.sh`
runs `zxvd_aipi.c` behind the RPC layer on the real AIPI core and the Z-Wasm
sandbox (exact replies, every refusal); it is the harness's test for
`zxvd_aipi.c`. `kernel/arch/hosted/test_zxvd_chat.sh` runs the chat methods
through the same RPC layer (text rules, the log's damage and torn-tail
handling, the read-only state after a failed write).
`kernel/arch/hosted/test_zxvd_pay.sh` runs the payment methods the same way
on the real libzxv and ident (exact replies and refusals for every method,
real ML-DSA-65 passkeys, holds replayed from a books file, and escrow entries
that are no hold's step).
`zxvd_aipi.c`. `kernel/arch/hosted/test_zxvd_econ.sh` runs `zxvd_econ.c` behind the RPC
layer on the real engines and pay_ledger (the switches, exact replies, every
refusal, and full flows); it is the harness's test for `zxvd_econ.c`.
`zxvd_aipi.c`. `test_zxvd_instruments.sh` (bonds, derivatives) and
`test_zxvd_estate.sh` (grants, estate, recovery) run those groups behind the
RPC layer on the real engines, signing with real Ed25519 keys; they are the
harness's tests for `zxvd_instruments.c`, `zxvd_estate.c` and `zxvd_sandbox.c`.
`test_zxvd_companion.sh` runs `companion.*` behind the RPC layer on the real
swarm DNA, emotion and evolution code (exact replies, every emotion and
intensity, the companion's step V7); it is the harness's test for
`zxvd_companion.c`.
`test_zxvd_web4.sh` runs the Web4 feed behind the RPC layer on the real
web4_agent code; it keeps a mirror of the owner's companion and compares
every envelope, consent token and CONSENT answer byte for byte. It is the
harness's test for `zxvd_web4.c`.
`test_zxvd_seats.sh` runs `seats.*` behind the RPC layer on the real seats
module and Argon2id (exact replies, every refusal, the back-off, sessions,
the seats file and its damage checks, the default 64 MiB parameters); it is
the harness's test for `zxvd_seats.c`.
`test_zxvd_power.sh` runs `power.*` behind the RPC layer on the real power
ledger, read from a Linux-style `/sys` and `/proc` fixture with a test clock
(exact replies, the agent under zxvd's app, budgets that make it wait, the
battery posture's token scale); it is the harness's test for `zxvd_power.c`.
`test_zxvd_pspace.sh` runs `pspace.*` and `hall.*` behind the RPC layer
on two real net hosts on 127.0.0.1 (the switch, every parameter rule, a
hall made on one host and joined by CID from the other, the owner's
control, posts both ways); it is the harness's test for `zxvd_pspace.c`.
`kernel/arch/hosted/test_zxvd.py ZXVD [GGUF]` drives everything above over
the socket only. CI (`.github/workflows/zxvd.yml`) runs it on Linux x86_64
(gcc, clang, gcc with ASan+UBSan), Linux arm64 and macOS, without a model and
with the tokenizer test model, and once with the real Qwen2.5-1.5B-Instruct
Q4_K_M (pinned SHA-256, cached, never in git). `.github/workflows/desktop.yml`
runs it on Windows against the packaged zxvd.exe, and on Linux against the
installed .deb.
`kernel/arch/hosted/test_zxvd_phone.py ZXVD` drives the phone link with its
own Python X25519 and ChaCha20-Poly1305 (checked against the RFC vectors).
`kernel/arch/hosted/test_zxvd_irc.sh` runs the IRC engine with no network,
then `irc.*` behind the RPC layer over real sockets with the repo's TLS
client pinned to the gateway (every signed record checked with ML-DSA-87,
the bridge against the gateway itself), and `test_zxvd_irc.py ZXVD` drives
a real zxvd with OpenSSL as the client.

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
- Payment messages are rendered, never sent: there is no SWIFT, PAPSS or
  RTGS link. Netting cycles and registered passkeys live in memory and are
  lost when zxvd stops; netting has no liquidity limits or sanctions
  screening.
- Escrow is released or refunded only by the owner's own call
  (`releases_on: "owner"`): there are no release conditions, no
  counterparty signature, no dispute and no quarantine yet. A plain
  `ledger.post` can still move an escrow account; `ledger.holds` then shows
  it as `unmatched` and the account as not `consistent`.
- The bonds, derivatives, grants and estate groups keep everything in
  memory on sandbox ledgers, not in the owner's books; see their section.
- The Web4 envelope feed signs and holds envelopes but delivers none, and
  lives in memory; see its section.
- The IRC gateway is a door on this machine, not a network: 16 clients, no
  channel operators or modes, no history, and its signatures say what the
  gateway relayed, not who typed it. Not on Windows yet. See
  docs/IRC_GATEWAY.md.
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
