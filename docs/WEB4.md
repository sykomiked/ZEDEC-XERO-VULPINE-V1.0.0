<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Web 4: agents, and the bridge between Web 2 and Web 3

Web 4 is the layer where assistants talk to each other and to people. In ZXV
these assistants are Chiglet instances. Every message between them is signed
with a post-quantum key. Web 4 does not replace the two older worlds. It links
them and uses each one for what it does well:

| World | What it is good at | What ZXV uses it for |
|---|---|---|
| **Web 2** (HTTP, OAuth/OIDC, JSON APIs, webhooks, ActivityPub, AT Protocol) | Where people already are. Login and account recovery, reach, existing APIs | Login, social feeds, notifications, outside APIs |
| **Web 3** (Ethereum-style chains, content addressing) | Public, verifiable, final records. Ownership. Data addressed by hash | Chain tokens, ownership proofs, final settlement, CIDs |
| **Web 4** (this layer) | Agent-to-agent work with post-quantum identity, on any link, offline first | Agent messages and streams, discovery, VFV payments, consent |

Status: built 2026-10-09 in `kernel/src/web4/` and not yet in
`kernel/Makefile`. It is tested by three host programs (see
[Tests](#tests)). Every Web 2 and Web 3 encoding below is checked against
published vectors. It has **not** been run against a live OIDC provider, an
Ethereum node, a Mastodon server or a Bluesky PDS. The
[Honest limits](#honest-limits) section explains what that means.

## Layout

| File | Contents |
|---|---|
| `web4_util.h/.c` | Status codes, bounded writer, hex / base64 / base64url, SHA-256, SHA3-256, Keccak-256, HMAC-SHA256, 256-bit unsigned integers |
| `web4_web2.h` + `web4_json.c`, `web4_http.c`, `web4_jwt.c`, `web4_oauth.c`, `web4_social.c` | JSON, HTTP/1.1, URLs and forms, JWS/JWT, OAuth 2.0 PKCE and the device flow, webhooks, RFC 3339, ActivityPub notes, AT Protocol posts |
| `web4_web3.h` + `web4_rlp.c`, `web4_eth.c`, `web4_secp256k1.c`, `web4_btc.c`, `web4_cid.c` | RLP; legacy (EIP-155) and EIP-1559 transactions; addresses and EIP-55; ABI; JSON-RPC; secp256k1 ECDSA; RIPEMD-160, base58check, bech32/bech32m; a bridge from CIDs to the forms Web 3 uses |
| `web4_agent.h/.c` | Agent identity, capability manifests, consent tokens, envelopes, the receiving pipeline, carriage (transport hook, freight, outbox) |
| `web4_bridge.h/.c` | The three-way identity link, cross-world payments, URL-to-CID maps, the routing policy |

All of this is freestanding integer C11. It uses no libc, no `malloc`, no
floating point, no 64-bit division (it calls `w4_udiv64`) and no `__int128`.
Every buffer is fixed in size or supplied by the caller. Every parser is
bounded. A 32-bit ARM build shows no `__aeabi_uldivmod`. The only libc symbols
the objects need are the `memcpy` calls the compiler emits for struct copies,
and `kernel/freestanding.c` provides those.

The module reuses code that already exists in the kernel:

- ML-DSA-65 from `kernel/src/pqsec`.
- SHA3 and SHAKE from `kernel/src/mlkem/keccak.c`.
- SHA-256 from `kernel/src/robin_debanks`.
- Freight from `kernel/src/freight`.
- CIDs and base58 from `kernel/src/ipfs_node`.

It uses Carracho through a small transport interface (`w4_transport_t`) and
does not call Carracho internals.

## Web 4 itself: agents

### Identity

- **A1.** An agent's key pair is ML-DSA-65 (FIPS 204), and
  `agent_id = SHA3-256(public key)`. Carracho uses the same rule for a NodeID.
  So an agent that runs with its node's key has an `agent_id` equal to the
  NodeID, and anyone can check that from the key alone.
- **A2.** A binding record ties an agent to the peer that hosts it. The record
  holds `{agent_id, peer_id, name, time}` and is signed by the node's key.
  A self-hosted agent signs its own binding, and its `peer_id` equals its
  `agent_id`. The *card* (`w4_card_t`) is everything a peer needs to check
  this. `w4_dir_add` adds a card to the directory only after verifying it.
  It also refuses a binding older than the one it already holds, so a binding
  cannot be rolled back.
- **A3.** Every signature carries a context string such as
  `web4/v1/envelope`, `web4/v1/consent` or `web4/v1/link`. A signature made for
  one purpose therefore cannot be used as a signature for another.

### Capabilities

A manifest lists the tools an agent offers. For each tool it gives:

- the name;
- the price in VFV minor units (2 digits);
- a rate limit (calls per minute, plus a burst);
- the largest input the tool accepts;
- flags: `MONEY`, `PII` (personal data) and `STREAM`.

The manifest is signed and has an expiry. It has a binary form for Carracho
and a JSON form for Web 2 discovery, which can be served at, for example,
`/.well-known/web4-agent.json`. The JSON form names the signature algorithm.
It also marks any tool with a price as `moves_money`, because charging for a
call is moving money.

### Envelopes and the receiving pipeline

One envelope shape is used for REQUEST, RESPONSE, STREAM chunks, CANCEL and
ERROR. An envelope carries:

- from, to;
- a sequence number that increases strictly for each sender;
- a timestamp;
- a request id and a chunk index;
- the tool name;
- up to 2048 bytes of payload;
- an optional consent token;
- an ML-DSA-65 signature over all of the above.

`w4_rt_receive` runs these checks in order:

1. decode (bounded and strict);
2. the envelope is addressed to this agent;
3. the sender is in the directory of verified cards;
4. a read-only check of the replay window (the highest sequence number plus a
   64-entry bitmap);
5. the timestamp is inside the window;
6. **the signature**;
7. only now, the replay window is updated.

The window is updated last on purpose. Forged traffic never reaches step 7, so
it cannot use up anyone's sequence numbers. A test checks this: a forged copy
of envelope n is rejected, and then the genuine envelope n is still accepted.

For a request, three more checks follow: the tool exists, the input fits the
tool's limit, and the rate limit allows the call. Then comes the consent rule
below.

Two tables are bounded, and both fail closed when they are full:

- **The replay table** evicts its least recently used peer. It then refuses any
  envelope from an unknown or evicted peer whose timestamp is not newer than
  that eviction. Without this, an evicted peer's old envelopes could be
  replayed against a fresh, empty window.
- **The rate table** never evicts a bucket that was used in the last minute.
  Evicting it would give its owner a full bucket again. A new caller is turned
  away (`W4_ERR_RATE`) until a bucket goes idle.

### Consent (the rule that does not bend)

An agent cannot move money or share personal data without a token from a
human. The token is an ML-DSA-65 signature by the human's own key over:

- `human_id`;
- `agent_id`;
- the action digest;
- the scope: `MONEY` and/or `PII`;
- the ceiling, in VFV minor units;
- the validity window;
- a nonce.

**The action digest names one exact action.** For a remote request it covers
the tool, both agents, the request id and the payload. For a payment it is the
digest of the payment intent. A token for one action cannot be used for any
other.

**Tokens are single use.** A bounded log is keyed by
`SHA3(human_id || nonce)`. When the log is full, expired entries are pruned.
If it is still full, the new consent is refused (fail closed).

**Consent is enforced in both places it is needed:**

- by `w4_rt_receive`, for a request to a tool that has a price, the `MONEY`
  flag or the `PII` flag;
- by `w4_pay_settle`, for local payments.

A token with only the `MONEY` scope does not allow personal data, and a token
with only `PII` does not allow money.

### Carriage, LAN and offline

`w4_transport_t` is the only thing the agent layer needs from a network. It
holds a `send` callback and the current reach: `OFFLINE`, `LAN` or `ONLINE`.
A Carracho session, a raw LAN socket, a radio link or a test harness can all
fill this role. There are two ways to carry envelopes:

- **Raw.** The envelope bytes go to `send` as they are.
- **Freight.** The envelope is split into freight sets. Each frame is
  `[64-byte freight header][168 × 21-byte UBH-168 smart packets]`. The packets
  are Reed–Solomon coded, so any `k` of the 168 rows rebuild the freight, and
  the freight hash catches a corrupted row. With `k = 120`, a lossy LAN or
  radio link can drop 48 rows of every freight and the envelope still arrives
  byte for byte. A test checks exactly this case, and also the cases with 119
  rows and with one corrupted row.

When the device is offline, `w4_post` keeps envelopes in a bounded outbox.
`w4_outbox_flush` sends them when a link appears. If the link drops partway
through a flush, the envelopes not yet sent stay queued. A send that fails is
queued, not lost. When the outbox is full, `w4_post` refuses new envelopes
(`W4_ERR_SPACE`) instead of dropping old ones.

## The bridge

### Identity link: one person in three worlds

`w4_link_t` holds three identities:

- an OIDC subject (issuer, subject and client id) from Web 2;
- an Ethereum address and its public key from Web 3;
- a Web 4 agent id.

The record also has a validity window. **All three worlds sign the same
digest** (SHA3-256 of the fields above):

| Leg | Signature | How it binds the digest |
|---|---|---|
| Web 4 | ML-DSA-65 by the agent's key (`web4/v1/link`) | Signs the digest directly |
| Web 3 | EIP-191 `personal_sign` by the wallet (secp256k1) | The wallet shows and signs the text `web4 link v1 0x<digest hex>` |
| Web 2 | The OIDC ID token from the issuer | The login request's `nonce` is `base64url(digest)`. This is the standard OIDC way to bind a login to a value the client chose |

`w4_link_verify` checks each leg on its own and returns a bit mask of the legs
that failed:

- **Web 4:** the key's id matches `agent_id`, and the signature verifies.
- **Web 3:** the address belongs to the public key, and the low-s ECDSA
  signature over the EIP-191 hash verifies.
- **Web 2:** the JWS parses. `none` and `crit` are refused. The algorithm is
  the one the host expects for this issuer, so the algorithm cannot be
  switched. The host's verifier hook accepts the signature; HS256 is built in.
  Finally, `iss`, `aud`, `exp`, `iat`, `nbf`, the nonce and the subject all
  match.

The tests make each leg fail by itself and check that only that leg's bit is
set. They also edit the record after signing and check that all three bits are
set.

### Payments

A payment intent holds:

- the payer and payee agents;
- the asset: VFV, ETH or ERC-20;
- an **exact integer amount** in the asset's base units (`w4_u256`);
- for chain assets, the chain id, the token contract and the recipient
  address;
- a validity window and a memo.

The payer agent signs the intent with ML-DSA-65. `w4_pay_settle` then works in
this order:

1. Check the shape and validity of the intent and the payer's signature.
2. **Route** with the policy table below, before consent is used. A payment
   that cannot settle now keeps its consent token unused. For example, a VFV
   payment while offline returns `W4_ERR_PENDING`, nothing moves, and the
   token is still valid.
3. Check and use up the human's `MONEY` token. Its action digest must be the
   intent digest. The VFV ceiling applies to VFV intents. For chain assets the
   digest already fixes the exact asset, chain, recipient and amount.
4. Settle:
   - **VFV:** the host's ledger hook (for example `kernel/src/pay`) is asked
     to post `{intent digest, payer, payee, amount, consent nonce}`.
   - **EVM:** an EIP-1559 transaction is built and signed through the signer
     hook, which can be the in-module key or a host wallet or HSM. ETH goes as
     the value field. An ERC-20 goes as `transfer(payee, amount)` calldata to
     the token contract. The result also returns the transaction hash and an
     `eth_sendRawTransaction` request body. When online, `broadcast_now` is
     set. Otherwise the signed transaction is kept, to be sent later.

Converting between VFV and a token is exact, or it is refused: 12.34 VFV
becomes exactly `12340000000000000000` base units of an 18-decimal token. A
token amount with a remainder smaller than one VFV cent gives `W4_ERR_RANGE`.
It is never rounded.

### Content: URL to CID

`w4_cmap_make` hashes the bytes an HTTP(S) URL served into a CIDv1 (raw,
sha2-256), and the agent that fetched them signs the mapping. Web 2 content
then becomes content addressed. In the other direction, `w4_cid_gateway_url`
and `w4_cid_uri` give any CID a Web 2 URL and an `ipfs://` URI, and
`w4_cid_to_bytes32` gives the 32-byte form a contract can store. URLs with
embedded credentials and URL schemes other than http and https are refused.

## The routing policy

`w4_route(op, reach)` returns a world and an action. The action is one of:

- **NOW:** do it in that world;
- **QUEUE:** keep it and do it when the reach improves;
- **LOCAL:** answer from local state;
- **REFUSE:** do not do it.

The rule is: use the primary world if the current reach allows it. If not, use
the fallback world if there is one and the reach allows it. If neither, take
the row's "otherwise" action. At reach `OFFLINE`, a world that is usable is
used locally. The table is in `web4_bridge.c`, and every row carries its
reason as a string. The test checks 25 (operation, reach) cases and checks
that every row routes at every reach.

| Operation | Primary (needs) | Fallback (needs) | Otherwise | Why |
|---|---|---|---|---|
| LOGIN | Web 2 (online) | Web 4 (offline) | refuse | Web 2 identity providers own human identity and account recovery. Once a link record is verified, Web 4 can recognise the person on a LAN or offline without asking the provider |
| DISCOVER | Web 4 (LAN) | — | local | Signed manifests over the Carracho DHT work on a LAN, and the JSON form serves Web 2 clients. Offline, only the cached directory is available |
| AGENT_MSG | Web 4 (LAN) | — | queue | Post-quantum signed envelopes between instances. Offline, they wait in the outbox |
| STREAM | Web 4 (LAN) | — | refuse | A live stream needs a live link. Queuing it would be meaningless |
| FEED_PUBLISH | Web 2 (online) | Web 4 (LAN) | queue | The audience is on ActivityPub and AT Protocol. LAN peers get the native feed. Offline, the post is kept and the mirror to Web 2 is queued |
| FEED_READ | Web 2 (online) | Web 4 (LAN) | local | Posts by outside people come from Web 2 servers. Peers may hold mirrored copies |
| NOTIFY | Web 2 (online) | Web 4 (LAN) | queue | Webhooks are how Web 2 services expect to be told. An agent on the LAN gets an envelope instead |
| API | Web 2 (online) | — | refuse | An outside HTTP API exists only online |
| PAY_VFV | Web 4 (LAN) | — | queue | VFV settles on the platform's own rails, fast and with no gas. It needs the ledger, on a LAN or online. Offline, the signed intent waits and nothing moves |
| PAY_TOKEN | Web 3 (online) | — | queue | A chain token moves only on its own chain. The transaction can be signed offline and is broadcast when online |
| OWNERSHIP | Web 3 (online) | Web 4 (offline) | local | Public, verifiable ownership is what chains are for. Offline, the last verified record is used |
| SETTLE_FINAL | Web 3 (online) | — | queue | A public, final settlement record belongs on a chain. It waits for a connection |
| CONTENT_PUBLISH | Web 3 (offline) | — | local | Content addressing works with no network at all. The CID is announced when a link appears, and it gets a Web 2 gateway URL |
| CONTENT_FETCH | Web 3 (LAN) | — | local | Blocks are verified against their CID, so they can come from any peer, on a LAN or online, with an HTTPS gateway as one more carrier. Offline, only the local blockstore is available |

Notes on the choices:

- **Money is split by asset, not by preference.** VFV is the platform's own
  unit, so its rail is the platform's ledger: no fees and no waiting for
  blocks. A token that lives on a chain can only move on that chain. The
  bridge never "settles" a chain token off-chain, and it never puts VFV on a
  chain itself. A VFV representation on a chain would be a contract plus a
  reserve. That is a custody and legal question, and this code does not take
  it on.
- **Login stays on Web 2 on purpose.** People already have accounts with
  recovery, MFA and support behind them. Web 4 does not build a second,
  weaker login. It *links* to the login once, with the three-way record, and
  after that it can recognise the person offline.
- **"Queue" never means "pretend".** A queued payment has moved nothing. A
  queued post has not been seen by anyone. Callers get `W4_ERR_PENDING` or
  `broadcast_now = false` and must show that state to the person.

## Configuration

Everything is set up by the caller. There is no global state.

- **Identity:** `w4_agent_create(seed, name, node_pk?, node_sk?, now, rnd?)`.
  Pass the node key pair to bind the agent to its Carracho node. Pass `NULL`
  for a self-hosted agent. `rnd = NULL` gives deterministic ML-DSA
  signatures. A 32-byte random value gives hedged signatures.
- **Runtime:** `w4_rt_init` takes:
  - the agent's own manifest;
  - the directory;
  - the replay and rate tables, with their sizes;
  - the consent log;
  - a lookup function from `human_id` to a public key (who counts as a human
    principal on this host);
  - the clock window, in milliseconds, in both directions.
- **Carriage:** `w4_transport_t{ctx, send, reach, freight, freight_k}`.
  Choose `freight_k` by how lossy the link is: `k = 168` has no redundancy, and
  smaller `k` survives the loss of `168 - k` rows per freight. Update `reach`
  whenever the link changes.
- **Payments:** `w4_rails_t` holds:
  - the ledger hook and its context;
  - the EVM signer hook and its context;
  - the EVM nonce, the fee caps and the gas limit;
  - the current reach.
- **Web 2 trust:** the host passes the JWS algorithm it expects for each
  issuer, and a verifier hook. `w4_jws_hs256_verify` is built in and needs a
  key of at least 32 bytes. RS256 and ES256 need a host hook.

## Tests

The tests are run from `kernel/` with gcc 11 or newer and with
`CPATH=$PWD/include:$PWD/src/modbind:$PWD/src/e8:$PWD/src/event_space:$PWD/src/surplus`.

| Program | Checks | Vectors and cases |
|---|---|---|
| `test_web4_web2.c` | 94 | RFC 7515 A.1 (HS256 JWS), RFC 7636 B (PKCE), RFC 4648 (base64), GitHub's webhook signature example, OAuth code and device flows, HTTP request smuggling cases, JSON edge cases, mutation fuzzing |
| `test_web4_web3.c` | 91 | Keccak-256; RLP examples from ethereum.org; the EIP-155 worked example, re-signed byte for byte with our RFC 6979 signer; ten EIP-1559 transactions from ethereumjs, decoded, recovered and re-signed byte for byte; every ERC-55 address; BIP-173 and BIP-350 valid and invalid lists; RIPEMD-160; base58check; ABI selectors; fuzzing |
| `test_web4_agent.c` | 162 | Every rule in this document: identity, manifests, the receive pipeline, consent, freight loss, outbox, the three-way link and each leg's failure, VFV and EVM payments, content maps, the full policy table, mutation fuzzing |

All three pass under `-Wall -Werror -Wextra`, and also under
`-fsanitize=address,undefined`. The library objects build for
`aarch64-none-elf` and `armv7-none-eabi` with `-ffreestanding`.
`gen_web4_vectors.py` writes `test_web4_vectors.h` from the published lists.

## Honest limits

- **Interoperability is shown only where a test shows it.** The tests show
  byte-exact agreement with the published vectors listed above. No test
  talks to a live OIDC provider, Ethereum node, ActivityPub server or AT
  Protocol PDS. The ActivityPub support covers the Note JSON only. It has no
  JSON-LD expansion, no HTTP Signatures and no inbox delivery. The AT Protocol
  support covers the post record JSON only. It has no DAG-CBOR, no repository
  commits and no facets. HTTP is HTTP/1.1 text. TLS is the carrier's job.
- **The Web 4 protocol is ZXV's own.** It interoperates between ZXV instances
  only. MCP, A2A and other agent protocols are not implemented and not
  claimed.
- **Envelopes are signed, not encrypted.** Confidentiality comes from the
  carrier, such as Carracho sessions or `pq_mesh_encapsulate`.
- **Consent proves only that a key holder signed.** It cannot show that the
  holder was the right person, or that they understood the action. That
  depends on the device and the screen that showed it. This code cannot see
  either.
- **The link proves only that three keys signed one record.** It says nothing
  about whether the OIDC issuer can be trusted, or whether the wallet is
  shared. The Web 3 leg is secp256k1, which is classical cryptography and as
  strong as Ethereum's own. The Web 4 leg stays post-quantum.
- **secp256k1 is limited.** The signing path is constant time by construction:
  a ladder that always adds, masked selects, and Fermat inversion. It has not
  been measured for timing on target hardware. `recover` and `verify` take
  variable time, because their inputs are public.
- **Settlement is as final as the chosen rail makes it.** A broadcast EVM
  transaction is not final until the chain says so. The bridge deploys no
  contracts and assumes none exist. Whether a given ERC-20 really is a 1:1
  VFV representation is outside this code.
- **Some limits are deliberate.** The EIP-1559 decoder accepts only an empty
  access list. JWT NumericDates must be integers. The payload limit is 2048
  bytes for envelopes and 4096 bytes for JWS payloads. Replay windows and rate
  limits live in memory and apply to one receiving process.
