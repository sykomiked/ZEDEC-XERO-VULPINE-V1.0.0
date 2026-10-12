<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# AIPI: content-addressed functions and data

AIPI (AI Programming Interface) is how ZXV agents ask for data and for
computation by **what it is** instead of **where it lives**.

A REST endpoint such as `https://api.example.com/v1/settle` names a server.
If the server goes away, changes its price or changes its behaviour, every
caller is affected. An AIPI CID names the exact code, the contract it must
respect and the way its answers are checked. Any node holding those bytes
can serve the function, and the caller checks every answer it gets back.

| Piece | Where |
|---|---|
| Core: CIDs, descriptors, GET, EXECUTE, verification, escrow, privacy | [`kernel/src/aipi/aipi.h`](../kernel/src/aipi/aipi.h) (freestanding C) |
| Executor on Z-Wasm + z-contract | [`sdk/kits/aipi`](../sdk/kits/aipi/) |
| Blocks, verified fetch, private blocks | [`kernel/src/ipfs_node`](../kernel/src/ipfs_node/ipfs_node.h) |
| Routing, sessions, economy | [Vinea](VINEA.md) (`kernel/src/vinea`) |
| Job state | the 64-state hexagram tag, [`kernel/src/lpres/lpres_hex.h`](../kernel/src/lpres/lpres_hex.h) |

## Contents

1. What a CID can and cannot prove
2. The four kinds of identifier
3. The function descriptor
4. AIPI_GET
5. AIPI_EXECUTE: routes
6. Verification modes
7. Bounties and escrow
8. Privacy
9. Offline operation and availability
10. Job state (hexagram tags)
11. Wire records
12. Integration status and next steps
13. Honest limits
14. Tests

---

## 1. What a CID can and cannot prove

- **Data.** A CID is the hash of the bytes. If the bytes you receive hash
  to the CID, they are the bytes that were named. This is a proof, and it
  does not depend on who sent them.
- **Computation.** A function CID fixes *which* code, contract and checking
  rule are meant. It does **not** prove that an output someone sends back
  is the right output. A hash of a contract cannot certify an arbitrary
  result. So AIPI accepts a remote result only through one of the
  verification modes in section 6. There is no mode that simply trusts the
  executor.

## 2. The four kinds of identifier

All are CIDv1 with a full 256-bit sha2-256 multihash. Nothing is truncated.

| Kind | Codec | Digest of |
|---|---|---|
| data | `raw` 0x55 (or `dag-pb` 0x70 for UnixFS files) | the bytes: weights, ledger states, Wasm modules, contract texts |
| function | `zxv-fn` 0x3A1000, `zxv-tensor-infer` 0x3A1001, `zxv-settle-ledger` 0x3A1002, `zxv-swarm-route` 0x3A1003 | the canonical function descriptor (section 3) |
| call | `zxv-aipi-call` 0x3A10C0 | the call record: function CID + input |
| claim | not a CID | an executor's answer to a call (section 11) |

**The codes are unregistered.** They sit in the multicodec private-use
range (0x300000 to 0x3FFFFF). Other IPFS software sees an unknown codec
but still verifies the sha2-256 multihash, so the blocks remain ordinary
IPFS blocks. If the project registers codes later, only these constants
change. The function codec must match the intent inside the descriptor;
a mismatch is refused (`AIPI_ERR_CODEC`).

**Text form.** `aipi_cid_to_text` writes multibase `z` (base58btc), for
example `zyu3uqS8N5tr3FvViFgu28ZTQ43Chnekt1mjB1ruxGAxhKVTzBvGL`. The `z` is
the multibase prefix. The rest is opaque binary in base58: a CID cannot
spell out a readable "AIPI" or intent name, so the intent is read from the
codec after decoding. `aipi_cid_from_text` also accepts the standard `b`
(base32) form, and refuses non-canonical text.

**Post-quantum note.** SHA-256 at full width keeps about 128-bit security
against Grover-style search, which is the usual post-quantum target for
hashes. Lattice cryptography is for signatures and key exchange; Vinea
already uses ML-DSA and ML-KEM there. A lattice-based *hash* is not needed
for content addressing.

**Provider key.** Providers of a CID are announced on Vinea's Kademlia DHT
under `SHA-256("zxv/aipi/v1/provider" || binary CID)` (`aipi_provider_key`).

## 3. The function descriptor

A function CID is the hash of a canonical descriptor (`aipi_fn_t`,
`aipi_fn_encode`):

| Field | Meaning |
|---|---|
| intent | general, tensor inference, ledger settlement, swarm routing; picks the codec |
| module | raw CID of the Z-Wasm module bytes |
| contract | optional raw CID of the z-contract text: the schema boundary |
| entry | the export that computes the result |
| verify | CHECKER, CERT, QUORUM or LOCAL (section 6) |
| checker, certifier | export names, required exactly when the mode uses them |
| quorum n, k | QUORUM only: ask n executors, accept k matching results, n/2 < k <= n, n <= 8 |
| determinism | QUORUM requires DETERMINISTIC |
| privacy | PUBLIC, or LOCAL for private or key-bearing work (section 8) |
| fuel_max | the most fuel one run may use |

Decoding is strict. Every field is checked, trailing bytes are refused, and
the decoder re-encodes and compares, so **one function has exactly one
CID**. The tests flip every bit of an encoded descriptor and confirm each
flip is refused or names a different function.

Because the module and contract are named by CID inside the descriptor,
changing one byte of the code, the contract or the checking rule gives a
different function CID. A caller who pinned a CID keeps getting exactly
the behaviour they pinned.

## 4. AIPI_GET

`aipi_get(cid)` looks in this order:

1. the local blockstore (re-hashed on every read);
2. if online, Vinea peers (through the IPFS node's peer hook);
3. if online, the trustless HTTP gateway, if the host bound one.

Every block from every source is re-hashed against the CID. Bytes that do
not match are **FORBIDDEN**. They are dropped, never stored, counted, and
appended to the hexagram quarantine log (`lpx_qlog`, SHA-256 hash-chained)
if one is bound. The next source is then tried. A verified block is cached
locally, so the next GET works offline.

Identity multihashes are refused for GET (they carry the data and prove
nothing). Private CIDs are never requested from anyone (section 8).

## 5. AIPI_EXECUTE: routes

`aipi_execute(function CID, input, bounty)` loads the descriptor by GET,
computes the call CID and picks a route with `aipi_plan`:

| Route | When | What happens |
|---|---|---|
| CACHE | deterministic, public, and a verified result for this call CID is memoized | the stored result, re-hashed |
| LOCAL | the code is cached here, and the work is private, LOCAL-verified, `prefer_local`, offline, or no mesh is bound | run in the local sandbox |
| FETCH_LOCAL | as LOCAL, but the code is not cached and the node is online | fetch module and contract by CID, verify, run here |
| MESH | public work, online, mesh bound | escrow the bounty, broadcast the call to providers, verify claims as they arrive |
| (unavailable) | offline and the code is not cached | `AIPI_ERR_OFFLINE`, no job is created |

The verified result of a deterministic, public call is memoized under the
call CID, and its bytes are stored as a raw block, so repeated calls cost
nothing and survive the network going down.

## 6. Verification modes

| Mode | How a remote result is accepted | Cost to the caller | Good for |
|---|---|---|---|
| CHECKER | the function's own checker export runs locally on (input, output) and returns 1 | one cheap local run | functions with an inverse: square root (square it), division (multiply back), sorting (order and multiset), settlement (A = L + E), solving an equation (substitute) |
| CERT | the executor also sends a certificate; the checker uses it | one cheap local run | answers with a witness: a factor for "composite", a path for "reachable", a Merkle proof |
| QUORUM | `k` of `n` executors return byte-identical output (`k > n/2`) | nothing local; `n` executors are paid to work | deterministic functions without a cheap check. ZXV's integer tensor engine is bit-identical across x86 and arm64, and Z-Wasm refuses floats, so honest executors agree bit for bit |
| LOCAL | never from the mesh: always run here | the full run | private work, or code you will not trust others to run |

Details:

- **CHECKER and CERT** fetch the checker's code before broadcasting, so
  nobody is asked to work on a call the caller cannot judge. A checker
  that cannot run (trap, missing export) does not accept.
- **Contract first.** With the kit, a claimed result outside the
  contract's result type is refused before the checker runs.
- **QUORUM** refuses a second claim from the same executor, accepts as
  soon as `k` agree, and declares a **dispute** as soon as no output can
  still reach `k`.

## 7. Bounties and escrow

- The bounty is reserved through the host's `reserve` hook when a call goes
  to the mesh. If it cannot be reserved, nothing is broadcast
  (`AIPI_ERR_FUNDS`).
- **Payment happens only after verification**, and only to the executors
  whose result was accepted: the CHECKER/CERT claimant, or every member of
  the agreeing QUORUM group. A split is equal, with any remainder going one
  unit each to the earliest agreeing executors.
- A rejected, late, duplicate or minority claim is never paid.
- A dispute or an expiry refunds the whole bounty.
- **Invariant: paid + refunded = reserved, for every job.** The tests check
  it over 200 random runs with honest, lying and silent executors.

In a real host, `reserve`, `pay` and `refund` map to the Vinea economy
(`vna_econ.h`): a PAY receipt to the executor, signed by both parties. The
"chiglet bounty" in the original concept is this escrowed amount.

## 8. Privacy

Work is private when the descriptor says `privacy = LOCAL` (private or
key-bearing functions), or when the caller marks the input private.

- Private work is **never broadcast** and no bounty moves.
- Only public code may be fetched for it (the module and contract named by
  CID). The input never leaves the node.
- A private descriptor is stored sealed (ChaCha20-Poly1305, the IPFS node's
  private blocks) and is never announced or served. A peer cannot resolve
  its CID.
- An executor refuses to serve a private function, and also refuses a
  LOCAL-verified one.
- Bytes a node holds sealed never get a plaintext copy: a public put of the
  same block, or a verified result that happens to equal it, is refused
  (`AIPI_ERR_STATE`) before anything is written.
- Results of private calls are not memoized.

## 9. Offline operation and availability

- With the network down (`online = false`) or no mesh bound, every call
  whose code is in the local blockstore runs locally, and memoized results
  are served from the cache. Nothing reports success it did not have.
- **Availability is not guaranteed.** A CID resolves only while some
  reachable node, or the local cache, holds the bytes. If the last host of a
  module disappears and nobody cached it, calls to that function fail with
  `AIPI_ERR_NOTFOUND` or `AIPI_ERR_OFFLINE`. Pinning the modules you depend
  on is how an application protects itself. AIPI makes it possible for
  *anyone* holding the code to serve it, which removes the single point of
  failure; it does not make the code exist when nobody keeps it.
- Deprecation and price changes cannot silently change a pinned function:
  a different behaviour is a different CID.

## 10. Job state (hexagram tags)

Every job carries a 6-bit hexagram tag (`lpres_hex.h`): base / polarity /
phase, and the action the 64-entry table gives for it.

| Moment | Tag | Action |
|---|---|---|
| created | neither / neutral / potential | wait |
| dispatched (broadcast or local run started) | neither / neutral / emerging | wait |
| a claim refuted while the job is open | both / (evidence polarity) / emerging | wait |
| quorum votes agree so far | neither / negentropic / emerging | wait |
| quorum votes disagree but `k` is still reachable | both / negentropic / emerging | wait (a dispute that can still converge) |
| verified | true / negentropic / manifest | proceed |
| quorum dispute (no output can reach `k`) | both / entropic / emerging | **escalate to a person** |
| expired | neither / entropic / emerging | wait (and refunded) |
| local run failed (trap, fuel, contract) | false / neutral / emerging | refuse |
| GET bytes do not match the CID | forbidden | rollback, quarantine log |

The phase only moves one step at a time on the Gray cycle, as `lpx_advance`
enforces.

## 11. Wire records

All little-endian, version byte 1, strict decoding (exact lengths, no
trailing bytes):

```text
descriptor  "AIPF" v intent verify privacy determinism quorum_n quorum_k fuel_max(8)
            len cid(module) | len cid(contract) or 0 | len entry | len checker | len certifier
call        "AIPC" v flags=0 | len cid(function) | in_len(2) input
claim       "AIPR" v flags=0 | len cid(call) | executor(32) | out_len(2) output | cert_len(2) cert
```

The claim bytes are what an executor signs with its Vinea ML-DSA identity.
The host verifies the signature and the session before calling
`aipi_submit`; the core takes the executor id as authenticated.

## 12. Integration status and next steps

Built and tested now:

- the core library (`kernel/src/aipi`), freestanding C11, integer only, no
  allocation, no 64-bit division; it builds for aarch64 and i386 with no
  libc symbols;
- the Z-Wasm executor kit (`sdk/kits/aipi`);
- GET over the real IPFS node blockstore, peer hook and gateway hook;
- **the Vinea transport** (`kernel/src/aipi/aipi_vna.h`, below);
- **the hosted app's glue** (`kernel/arch/hosted/zxv_net_host.h` U12):
  `zxv_net_attach_aipi`, an escrow budget, serving, and bounties settled as
  compute receipts in both nodes' books.

### AIPI over Vinea (`aipi_vna.h`)

Calls, claims, BUSY answers and PAID notices are signed Vinea messages
(`VNA_MSG_AIPI`). The adapter is a pure state machine like both modules.

- **V1 Whom to ask.** `aipi_execute`'s mesh route sends the call to the
  executors the owner chose (`aipi_vna_add_executor`) that are contacts,
  then to the provider the DHT holds under `aipi_provider_key(function
  CID)`. The provider record is verified (ML-DSA-87) before use. Nobody
  else is asked.
- **V2 Which claims count.** A claim counts only if its message verified,
  its executor field equals the message's sender, and that sender was asked
  for this call. Anything else is refused before `aipi_submit` sees it, so
  a stranger cannot stuff a quorum. `aipi_submit` then checks the result
  itself (checker, certificate or quorum).
- **V3 Escrow.** reserve and refund go to the host's economy hooks, and fail
  closed: with no reserve hook, only a zero bounty can be escrowed. After
  verification (rule A2) the pay hook arranges payment, and the adapter
  sends the executor a signed PAID notice naming the call and the amount.
- **V4 Serving.** A CALL reaches the adapter only under an agreement that
  shares compute with the caller (one unit of the compute quota per call).
  The adapter also needs `aipi_vna_serve(true)`, a bounty at or above the
  minimum, and `aipi_serve` to succeed. Otherwise it answers BUSY.
- **V5 PAID.** A PAID notice goes to the host's paid hook.

In the hosted app the escrow budget defaults to 0, so only free calls go
out until the owner sets one. An accepted claim's bounty becomes a want for
one compute receipt on credit, bound to that executor
(`vna_link_want_from`). When the executor hears PAID, it offers the receipt,
but only if its owner shares compute. Both books then apply it through the
trade loop.

Tests: `make -C kernel test-aipi-vna` (102 checks under ASan + UBSan: V1 to
V5, strangers, forged and unasked claims, BUSY, expiry and refunds), and
section [7] of `kernel/arch/hosted/test_zxv_net_host.c`, where one real UDP
node calls another and the bounty settles in both books.

### Not yet wired

1. **zxvd.** zxvd does not create an AIPI context yet. It should call
   `zxv_net_attach_aipi` and expose `AIPI_GET` / `AIPI_EXECUTE`, the budget
   and serving through the daemon socket and the swarm, so agents call
   functions by CID. The zxvd owner has this.
2. **Multi-datagram calls.** A call or claim is one datagram of up to about
   10 KB, which relies on IP fragmentation off the loopback.
3. **libzxv ABI.** Expose AIPI through the libzxv C ABI so every language
   SDK gets it; the SDK track owns that change.
4. **Real intents.** Descriptors for tensor inference (QUORUM, using the
   engine's cross-platform bit-identical output), ledger settlement
   (CHECKER: balances must close) and swarm routing.

## 13. Honest limits

- **QUORUM assumes independent executors.** Two executors that collude on
  the same wrong answer win a 2-of-3 quorum. The tests show this on
  purpose. Distinct node IDs are not distinct operators (Sybil identities
  cost only Vinea's optional proof of work). Choosing whom to ask (signed
  agreements, trust, stake) is the host's job, and higher-value calls
  should use CHECKER or CERT where a check exists.
- **A checker is code.** If it is wrong, AIPI accepts wrong answers. Review
  the checker as carefully as the function.
- **Nondeterministic functions** (sampling with a random seed, float code)
  cannot use QUORUM, and the descriptor rules refuse it. They need CHECKER,
  CERT or LOCAL.
- **Availability depends on someone holding the bytes** (section 9).
- **Inputs and outputs are at most 1024 bytes** in a call or claim. Larger
  data goes in by CID: put it as a block and pass its CID as input.
- **One-block modules** (up to 1 MiB) in this version; larger modules
  would be UnixFS DAGs.
- **The codecs are unregistered** private-use codes (section 2).

## 14. Tests

- `make -C kernel test-aipi`: 617 checks under ASan + UBSan, plus the
  freestanding build for aarch64 and i386. Covers CIDs and text forms,
  descriptor rules and canonical encoding (every bit flip), records, GET
  with a lying peer or gateway, routes, CHECKER / CERT / QUORUM, disputes,
  expiry, privacy, codec mismatches, funds, and the escrow invariant over
  random runs. Edge tests pin every encoder to its exact size, cut and alter
  every record header, drop each hook, and check the memo ring, pins and
  payment shares at their limits (a 2^63 bounty included).
- Mutation testing (`tests/harness/mutator/mutate.py` on `aipi.c`, tests
  built with ASan + UBSan): 90.4% of 1277 mutants killed. The 122 that live
  were reviewed and are equivalent: dead initialisers, sign variants of
  error paths that cannot be reached, and checks that the IPFS node or the
  canonical re-encode repeat, so no test can tell them apart.
- `make -C kernel test-aipi-vna`: 102 checks on the Vinea adapter
  (section 12), under ASan + UBSan.
- `make -C sdk/kits/aipi test`: 60 end-to-end checks on real Wasm modules
  (see the kit README).
- The core is in the kernel's `verify-all`; the kit has its own CI job in
  `.github/workflows/sdk.yml`.
