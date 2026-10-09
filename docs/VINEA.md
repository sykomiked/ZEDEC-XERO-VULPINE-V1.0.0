# Vinea

Vinea is the ZXV peer-to-peer overlay. It gives you Hotline-style host spaces
(a node publishes what it shares and on what terms) on top of Kademlia
routing. The code is in `kernel/src/vinea/`, with the file prefix `vna_` and
the symbol prefix `vna_` / `VNA_`. It is licensed Apache-2.0.

*Historical note: the module was drafted under the working name "Carracho".
It was renamed before integration because that is an existing product name.
Protocol v2 changed every magic number, context string and version byte, so a
v1 draft peer cannot be confused with a v2 peer.*

## Contents

1. Principles
2. Identity
3. Routing (Kademlia)
4. Wire format, schemas, UBH-168 framing, the frame hook
5. Sessions (hybrid PQ handshake)
6. Agreements and the participation dial
7. Economy
8. File sharing and IPFS compatibility
9. Offline, LAN-only and online operation
10. Efficiencies, and why each one is exact
11. Threat model
12. Honest limits
13. What the host layer must add
14. Build, test and measured results

---

## 1. Principles

- **Purely peer-to-peer.** Vinea never requires a bootstrap server, relay or
  gateway. Two things are enough to start:
  - LAN discovery (section 9)
  - signed node records the host cached from an earlier run

  Well-known bootstrap addresses (`vna_node_bootstrap`) are optional.
- **Servers have no authority.** Someone may run a high-capacity, always-on
  node to strengthen the network and be rewarded for it through the economy.
  That node is an ordinary peer: its record may set `VNA_NR_HIGHCAP`, which is
  advisory only. No code path gives it authority in routing, storage or the
  economy.
- **A library of pure state machines.** Vinea opens no sockets, reads no clock
  and draws no entropy of its own. The host passes in received bytes, the
  sender's address, the current time and DRBG seeds. Vinea hands back bytes to
  send in a caller-provided outbox.
- **Freestanding C11, integer only.** The library uses:
  - no floating point
  - no libc beyond `<stdint.h>`, `<stdbool.h>` and `<stddef.h>`
  - no malloc: every table is caller-provided
  - no 64-bit division: only shifts, `swarm_muldiv` and 32-bit constant
    divisions

  The compiler may emit `memcpy` for struct copies, so the kernel must provide
  it.
- **Post-quantum end to end.**
  - Every signature is ML-DSA-65.
  - Every hash is SHA3-256 or SHA-256 at full width.
  - Key agreement is hybrid X25519 + ML-KEM-768.
- **Fail closed.** Every byte that arrives goes through one schema walker
  (`vna_schema.c`). Anything malformed, out of range, unsigned, stale or
  outside an agreement is refused before it has any effect.
- **No security claim without a test that tries to break it.** See section 14.

## 2. Identity (`vna_id.h`)

- **Key.** Each node has an ML-DSA-65 key: a 1952-byte public key and a
  4032-byte secret key.
- **NodeID.** `NodeID = SHA3-256(pk)`. Every message and record carries the
  public key, so any receiver can check the binding with no directory.
- **Proof of work (optional, S/Kademlia style).** A NodeID is admitted only if
  `SHA3-256("vinea/v2/pow" ‖ NodeID ‖ nonce_le64)` has at least `pow_bits`
  leading zero bits. This makes identities cost CPU, which limits Sybils. It
  does not prevent them.
- **Context strings.** Every signature uses one, so a signature made for one
  purpose can never be accepted for another:
  - `vinea/v2/msg`
  - `vinea/v2/dht-record`
  - `vinea/v2/node-record`
  - `vinea/v2/spool-item`
  - `vinea/v2/hs-responder`
  - `vinea/v2/hs-initiator`
  - `vinea/v2/trade-receipt`
  - `vinea/v2/ledger-head`
- **Node record** (`vna_noderec_t`, `vna_wire.h`). This is the identity bound,
  by its own signature, to up to 4 transport addresses, plus:
  - a feature mask
  - flags (`HIGHCAP`, `GOODBYE`)
  - a strictly increasing `seq`
  - `created` and `expires`

  LAN announcements carry it, and hosts persist it as a cached peer. A newer
  `seq` replaces an older one, and an expired record is refused.

## 3. Routing (`vna_kad.h`, `vna_node.h`)

- **Table.** 256 logical buckets with k = 20, sharing one caller-provided
  contact pool. Only verified contacts enter it: a peer whose signed message
  verified, or one with a verified signed node record.
- **Eviction.** Ping before evicting: a full bucket pings its
  least-recently-seen contact, and evicts it only if that ping times out.
- **RPCs.** PING, FIND_NODE, FIND_VALUE, STORE.
- **Lookups.** A caller-driven state machine with alpha = 3, optionally over
  up to 4 disjoint paths (S/Kademlia). A node answers on exactly one path.
  Responses must match both the rpc id and the queried NodeID, otherwise they
  are counted as unexpected and dropped.
- **Records.**
  - STOREd records are verified before they are kept.
  - Records expire, and the holder republishes them.
  - PRIVATE provider records are never stored, served or announced.
- **Refresh.** Only idle buckets are refreshed.
- **Joining.**
  - The first contact starts a lookup of the node's own id. That contact can
    come from a bootstrap PONG, a LAN announcement or a cached record.
  - On a LAN-only island, discovery alone gives every node its whole island.
    The DHT then works with no outside peer.

## 4. Wire format (`vna_schema.h`, `vna_wire.h`, `vna_frame.h`)

### Schemas

Every record is declared once as a schema table, in the spirit of Sutra's DATA
SECTION. One audited walker packs and validates all of them. There are no
hand-written per-message parsers. The rules:

| Rule | Meaning |
|---|---|
| S1 | Integers are little-endian, with upper bounds |
| S2 | Constant magic |
| S3 | Fixed byte fields |
| S4 | Length-prefixed variable fields with a maximum |
| S5 | Arrays with a maximum count |
| S6 | Hackronomicon text must parse and be byte-identical to its canonical form |
| S7 | Signature fields come last, and the signed region is every byte before them |
| S8 | The input must be consumed exactly |

### Messages

Message magic is `"VNA2"`, version 2. A message carries:

- type, flags, features
- src and dst NodeIDs
- PoW nonce
- seq, ts, rpc
- body
- pk and sig

The receive pipeline runs in this order:

1. frame and schema
2. version, dst == self
3. dedupe by message hash
4. replay window and timestamp
5. NodeID binding and PoW
6. ML-DSA-65 signature
7. commit the replay state

Nothing is committed before the signature verifies. Message types:

- PING, PONG, FIND_NODE, NODES, FIND_VALUE, VALUE, STORE, STORE_ACK
- HK: one canonical Hackronomicon line
- SPOOL, SPOOL_ACK

### Replay protection

- R1: each peer has a 64-bit sliding window, plus a ±30 s timestamp window.
- R2: the window advances only after the signature verifies.
- R3: when the bounded replay table evicts a peer, the newest timestamp
  accepted from that peer raises the floor of one of 64 slots chosen by NodeID
  hash. Untracked peers in that slot must be newer than the floor.
  - This is exact against replays of the evicted peer.
  - The first draft used one global "time of last eviction" floor instead. On
    a busy node, every in-flight message from an untracked peer was then
    refused. The 200-node simulation exposed this as degraded lookups, and it
    is fixed and covered by a test.

### UBH-168 framing

UBH-168 is the default between ZXV nodes:

- A 21-octet header packed by `src/ubh/ubh.c`, carrying:
  - the frame class:
    - CAUSAL_EVENT: messages, receipts, ledger entries, spool items
    - CONTENT_OBJECT: DHT and node records, chunks
    - CAPABILITY_POLICY: agreements
    - INTEGRITY: handshakes
  - the schema id
  - the exact payload length
  - a 3-byte integrity reference (a framing check only, not security)
- Then the canonical bytes, zero-padded to whole 21-octet frames.
- It is negotiated through the `VNA_FEAT_UBH168` feature bit. A node sends
  plain bytes to peers that have not advertised the bit.
- Signatures always cover the canonical bytes. Hashes and NodeIDs are never
  truncated to 168 bits.

### Per-frame transform hook

`vna_xform_t` is the integration point for `src/ehop`. Every Vinea datagram
carries exactly one frame, so "per frame" and "per datagram" are the same.

- The hook is the outermost layer: `canonical → [UBH-168] → seal → transport`,
  and `transport → open → [unframe] → canonical`.
- It applies to DHT messages (`vna_node_set_xform`) and LAN discovery
  (`vna_lan_set_xform`).
- It never touches what is signed, so it can add privacy or channel separation
  but cannot weaken authentication.
- A frame the hook refuses is dropped before any parsing.
- ehop adapter:
  - `seal` = `ehop_seal(channel_for(addr), in, len, out, cap, &olen)`
  - `open` = `ehop_open(...)`
  - `overhead` = `EHOP_OVERHEAD` (48)
- **Open item:** `EHOP_MAX_PAYLOAD` is 2048, but Vinea frames go up to
  `VNA_MSG_WIRE_MAX` = 13700 bytes. Either the adapter fragments or ehop
  raises its limit. Vinea itself does not fragment.

## 5. Sessions (`vna_session.h`)

The handshake is a three-message, SIGMA-style exchange with an identity-hiding
initiator:

- M1 carries the initiator's ephemeral X25519 key and its ML-KEM-768
  encapsulation key.
- M2 carries the responder's X25519 key and the ML-KEM ciphertext, plus the
  responder's ML-DSA signature over transcript hash th2.
- M3 is encrypted under a handshake key. It carries the initiator's identity
  and its ML-DSA signature over th3.

Key derivation:

- `hybrid = SHAKE256("vinea/v2/hybrid" ‖ ss_kem ‖ ss_x25519 ‖ th2)`
- HKDF-SHA256 then derives the directional ChaCha20-Poly1305 keys from the
  hybrid secret and th4.

Records:

- 13-byte header as AAD
- TLS 1.3-style nonce
- 64-record receive window that advances only after the tag verifies
- sending stops at 2^48 records

A quantum attacker who breaks X25519 gains nothing:

- The traffic key needs the ML-KEM shared secret as well.
- Authentication uses only ML-DSA-65, never anything X25519-derived.

## 6. Agreements (`vna_agree.h`)

An agreement is a signed, content-addressed policy. Its address is the SHA3 of
its canonical bytes. It is published in the DHT under
`htag("vinea/v2/agreement-of", owner)`.

### The participation dial

| Degree | What the node does |
|---|---|
| OFF (0) | answers nothing |
| ROUTE (1) | routes |
| STORE (2) | also stores records |
| SERVE (3) | also serves files |
| COMPUTE (4) | also sells compute |
| MEMORY (5) | also leases memory |

### Contents

- audience: everyone, allowlist, trust ≥ n, or allowlist-or-trust
- a blocklist
- capacities: compute per cycle, storage bytes, memory bytes, maximum lease
  cycles
- per-peer quotas per period
- prices per resource
- a list of shared files with visibility
- free-text terms

### Enforcement

`vna_agree_check` gates every request, fail closed:

- A PRIVATE file is served only to allowlisted peers.
- Memory leases are measured in bytes × cycles, counted against the quota
  while live, and released exactly at expiry.
- HK requests outside the agreement get a signed `reject(...)`. Peers outside
  the audience get `reject(route)`. Unknown verbs are refused.
  - Verbs: ask, offer, bid, counter, accept, reject, lease, fetch.
  - Anything like `set(allocation…)` is refused.

## 7. Economy (`vna_econ.h`)

### Token kinds and forms

Balances are 3 token kinds × 9 forms of capital. The forms follow the
`zcapital.h` / `swarm_cap_t` order.

| Kind | What it is |
|---|---|
| INTERNAL | The node's own swarm budget. No handler can reach it. |
| EXTERNAL | A liability of the issuing node, spendable only at that issuer. |
| NEUTRAL | Capacity the owner pledged to the mesh, waiting for distribution. |

The four Crown forms are inalienable: any receipt in them is refused with
`VNA_ERR_INALIENABLE`.

- Social
- Natural
- Cultural
- Spiritual

Social capital is still earned (+1 per honest receipt) but never moved.

### Moves

- **EXPORT.** The owner's gate turns INTERNAL into NEUTRAL, through
  `swarm_budget_consume`.
- **IMPORT.** The gate turns verified remote compute into next cycle's
  INTERNAL rate.
- **DISTRIBUTE.** NEUTRAL becomes peers' EXTERNAL, signed by the issuer.
- **PAY.** EXTERNAL is burned at the issuer. Both sides countersign, and the
  seller's agreement is checked.
- **CREDIT.** The buyer records contribution value.
- **EXPIRE.** Undistributable NEUTRAL tokens burn.

Every move posts three entries, sharing one reference, to a hash-chained
ledger whose tip is ML-DSA-signed:

- FINANCIAL
- PROVENANCE
- EXTERNALITY

Conservation (minted − burned = outstanding) is checked per (kind, form).

### Mapping to `finance/triple_ledger.h`

The axes map one to one:

- FIN → `LEDGER_FINANCIAL`
- PROV → `LEDGER_PROVENANCE`
- EXT → `LEDGER_EXTERNALITY`

The forms map to `capital_type_t` (vino) as follows. This is a documented
mapping; Vinea does not link `triple_ledger`.

| zcap form | capital_type_t |
|---|---|
| Financial | CAP_FINANCIAL |
| Manufactured | CAP_MATERIAL |
| Intellectual | CAP_KNOWLEDGE |
| Human | CAP_HUMAN |
| Social | CAP_SOCIAL |
| Natural | CAP_LIVING |
| Cultural | CAP_CULTURAL |
| Spiritual | CAP_SPIRITUAL |
| System | CAP_BUILT |

### The golden-ratio rule

Settlement runs per cycle and per priceable form:

- **G1, commons (8/21 of the pool).** Split over all members by
  `swarm_budget`'s Fibonacci levels, ranked by lifetime contribution. Level
  capacities are 1, 2, 3, 5, … and adjacent levels are in golden proportion.
  Newcomers still receive a share.
- **G2, market (13/21 of the pool).** Paid in proportion to demand-weighted
  contribution this cycle, through `swarm_capped_split`. No peer may take more
  than 8/21 of the market, and any excess returns to the commons.
- **G3, per-peer cap.** No peer's total may exceed max(8/21 of the pool, an
  equal share). With exactly two members the cap is ceil(13/21) instead,
  otherwise the equal-share floor would force a 50/50 split regardless of
  contribution.
- **G4, exactness.** Largest-remainder integer arithmetic makes the parts sum
  to exactly the pool.

### Pricing and negotiation

- Demand price = `base·(S+D)/S`, capped at `base·21/8`. Demand decays by 13/21
  per cycle.
- Negotiation concedes ceil(8/21) of the remaining gap each round, so it ends
  within its round budget.

### Protection against outside manipulation

- Only signed receipts, checked against the agreement, change balances.
- INTERNAL tokens move only through the owner's gate, which requires:
  - the owner's secret (only its SHA3 is stored)
  - per-cycle caps
  - a limit on conversions per cycle
  - Crown forms are refused
- The tests attack this four ways, and all are refused:
  - forged credit
  - a replayed receipt
  - an over-cap conversion
  - a peer sending `set(allocation…)` on the wire (the swarm budget is
    byte-identical afterwards)

### Double spending without consensus

An EXTERNAL token can be spent only at its issuer, which decrements it
atomically, and it cannot be passed on. Each pair of peers keeps a receipt
chain: `pair_seq` plus the hash of the previous receipt, signed by both sides.

- A replayed receipt is refused (REPLAY).
- A fork is two signed receipts with the same `pair_seq`, which is a
  transferable proof of equivocation.

Not prevented:

- an issuer refusing to honour its own tokens (provable, not preventable)
- Sybil members diluting a commons the owner admitted them to
- two colluding identities trading with each other

Operational rule: the seller applies a receipt first and sends it on only on
`VNA_OK`. If one side applies a receipt and the other refuses it, the chain
diverges, and the next receipt is refused (FORK) until the pair re-syncs. The
re-sync protocol is host-level and not built.

## 8. File sharing (`vna_file.h`, `vna_cid.h`)

### Chunks and the tree

- Chunk size is a power of two from 1 to 16 KiB, default 16 KiB, with at most
  65 536 chunks.
- Each chunk has a real IPFS raw-block CIDv1: `0x01 0x55 0x12 0x20` followed
  by SHA-256, from `vna_cid_raw`. It is the same CID IPFS gives that block with
  raw leaves. A one-chunk file matches `ipfs add --cid-version=1 --raw-leaves`.
- The tree has the RFC 6962 / 9162 shape:
  - leaf = `SHA256(0x00 ‖ CID)`
  - node = `SHA256(0x01 ‖ L ‖ R)`
  - root = `SHA256(0x02 ‖ size ‖ chunk_size ‖ mth)`

  The root commits to the length, so a file cannot be truncated or extended.
- **Honest difference:** the root is not a UnixFS dag-pb root. An IPFS node
  can fetch every chunk by its raw CID, but it cannot resolve the Vinea root.

### Transfer

- **Streaming verification.** Each chunk travels with its audit path (at most
  17 hashes) and is verified on arrival. The receiver keeps only a bitmap of
  accepted chunks, and duplicates are refused.
- **Serving.** The server checks the agreement and charges each chunk request
  before building the chunk.
- **Transport.** Chunks travel inside sessions.
- **Visibility.** Provider announcements respect visibility: a PRIVATE item is
  never announced, stored or served outside its agreement.

## 9. Offline, LAN-only and online (`vna_lan.h`, the spool in `vna_node.h`)

### LAN discovery

Discovery uses an mDNS / DNS-SD-style service, `_zxv._udp`.

- **Message types.**
  - QUERY lists the NodeIDs the querier already knows (known-answer
    suppression, RFC 6762 §7.1).
  - ANNOUNCE carries one signed node record.
  - GOODBYE is a signed announce with the `GOODBYE` flag set, so a peer cannot
    be knocked off by a forgery.
- **Timing.**
  - At start, a node sends one query and one announce, then a burst of 3
    announces and 3 queries at doubling gaps (1 s, 2 s, …).
  - After the burst it re-announces every `steady_ms` and re-signs its record
    at half its lifetime.
  - It answers a query after a random 20–120 ms delay, and at most once per
    second.
- **Receiving an announce.**
  1. Refuse its own id, and any seq not newer than the one held. This check
     happens before any signature work.
  2. Verify the record: binding, PoW, signature, `created` no more than the
     skew into the future, not expired.
  3. Seed the routing table.
- **Compatibility.** These are Vinea schema records, not DNS resource records.
  A stock mDNS responder neither parses nor answers them. A host that wants
  DNS-SD visibility can also publish PTR/SRV/TXT records itself.

### Joining islands

Islands that meet merge through ordinary Kademlia lookups. A single cached
record or bootstrap link is enough (tested below).

### Offline outbox (spool)

- **Sender.**
  - `vna_node_spool_hk` signs an item immediately (context
    `vinea/v2/spool-item`), so a queue the host persists is tamper-evident.
  - Items to one destination go strictly in sseq order, one in flight at a
    time.
  - With no route, the item waits. A sender that knows any peer looks the
    destination up, with backoff.
  - When a contact is added, retries to it start at once.
  - A lost acknowledgement makes the sender retry, never skip ahead.
  - Expired items are dropped, never delivered late.
- **Receiver.**
  - Keeps the highest sseq accepted per origin. Items at or below it are
    acknowledged as DUP and not surfaced again. The result is in-order,
    exactly-once delivery while the origin stays in the receiver's table.
  - Applies the agreement and verb checks of HK requests.
  - Answers BUSY rather than drop an item when its event queue is full.
- **Delivery message.** Each delivery is a fresh signed SPOOL message with a
  new seq and ts, so the message replay window still holds.

## 10. Efficiencies, and why each one is exact

| Efficiency | Why it is exact |
|---|---|
| **Key cache** | A public key byte-identical to one already hashed to that NodeID (same nonce, PoW at ≥ the required bits) skips re-hashing. Comparing 1952 bytes is an exact test of what was hashed. |
| **Signature before expensive work** | Every record is verified before it is stored, served or used. Handshake signatures are checked before decapsulation results are used. Nothing unauthenticated reaches a costly path. |
| **Cheap refusals first** | Dedupe by message hash, replay, timestamp and dst checks all run before ML-DSA verify. LAN announces with a seq that is not newer are refused before verify. A refusal accepts nothing, so skipping verification on that path cannot admit a forgery. |
| **Dedupe ring** | Identical bytes hash identically, so a repeat is dropped before any work. |
| **Idle-only bucket refresh** | Kademlia's refresh exists to keep buckets populated when they see no traffic. A bucket with recent activity has just been refreshed by that traffic. |
| **Streaming Merkle verification** | A chunk verified against the length-committing root is exactly the chunk at that index in the file the root names. No buffering of other chunks is needed. |
| **Schema walker** | One audited parser for every record means one place to get bounds right. |

## 11. Threat model

### Defended (each has a test that tries to break it)

- forged sender (binding)
- wrong signer
- tampered bytes
- replay and stale messages, including the R3 eviction case
- misaddressed messages
- unsolicited and wrong-responder responses
- garbage and truncation
- UBH length and padding tricks
- signature reuse across contexts
- handshake MITM (four variants), M3 tampering, impersonation
- record tampering
- private-record announcement
- out-of-agreement requests, unknown verbs
- forged, replayed and forked receipts
- trading Crown forms
- over-cap gate conversions, gate use without the owner's secret
- outside writes to a node's internal allocation
- corrupted or duplicate chunks
- LAN: forged, replayed, tampered, expired, future-dated and wrong-service
  announcements, and forged goodbyes
- spool items not signed by their origin
- frames refused by the transform hook

### Not defended

- **Sybils and eclipse attacks.** PoW and disjoint paths raise the cost but
  cannot rule them out.
- **Traffic analysis.** Use the frame hook (ehop) and the host transport for
  this.
- **Denial of service by volume.** Rate limiting belongs to the host.
- **Compromised node keys.** There is no revocation yet beyond record
  expiry.
- **Equivocation.** It is provable, but nothing punishes it automatically.

## 12. Honest limits

- **NAT traversal, hole punching and the UDP glue** live in the host layer
  (`arch/hosted`), not here.
- **There is no global consensus.** See the double-spend section for what
  that leaves open.
- **The receipt pair chain can diverge** if one side applies a receipt and the
  other does not. Re-sync is not built.
- **Spool dedupe state is bounded** (`rx_cap` origins). An origin evicted from
  the table could have an old item accepted again. The receiver's
  message-hash dedupe ring reduces this risk but does not eliminate it.
- **Proof generation is O(n) hashing per chunk.** Servers should cache inner
  nodes.
- **Some functions use static scratch buffers** and so are not reentrant.
  Serialize calls, or give each thread its own instance after adding
  per-instance scratch:
  - `vna_agree_cid`
  - `vna_rec_verify` (agreement case)
  - `vna_node_seed_record`
  - `vna_lookup_init` (seed list)
  - the session `g_buf`
  - `vna_cmd` (AST)
  - `vna_econ` (receipt scratch)
  - `vna_econ_split`
  - some node reply scratch
- **`vna_pq.h` re-declares the three `pq_mldsa65_*` prototypes.** This keeps
  `pq_security.h`, which pulls in `<complex.h>` via `m5_types.h`, out of
  freestanding code. The test includes both headers, so any prototype drift
  fails to compile. The test file and anything else that includes
  `pq_security.h` or `zcapital.h` (which pulls in `math.h`) cannot be
  freestanding. All `vna_*.c` files can.
- **The UBH integrity reference is 3 bytes** and only a framing check.
- **LAN packets are about 5.5 KB**, because of the ML-DSA key and signature.
  They IP-fragment on 1500-byte links, which works on ordinary LANs.
- **The ehop payload limit** (2048 bytes) is below the largest Vinea frame.

## 13. What the host layer must add

- **Sockets.**
  - UDP (and/or stream) sockets for node traffic.
  - A multicast socket for `_zxv._udp` discovery. Suggested groups:
    239.255.90.86 or ff02::5a56. The group is an opaque token to Vinea.
  - NAT traversal.
- **Time and entropy.** A monotonic millisecond clock and DRBG seeds.
- **Persistence:**
  - identity
  - cached peer records (from `vna_lan_t.last_rec` and
    `vna_node_contacts`)
  - spool entries and `next_sseq`
  - agreement, books, ledger
- **Rate limiting and resource accounting** per source address.
- **The ehop adapter** for the frame hook, plus fragmentation if needed.
- **Receipt exchange and re-sync** between book holders.
- **Wiring into `kernel/Makefile`.** Not done by this module.

## 14. Build, test and measured results

Run from `kernel/`:

```
export CPATH=$PWD/include:$PWD/src/modbind:$PWD/src/e8:$PWD/src/event_space:$PWD/src/surplus
gcc -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST -Isrc/zcapital -Isrc/lpres -Isrc/edp_risk \
  src/vinea/test_vinea.c src/vinea/vna_*.c \
  src/pqsec/pq_mldsa65.c src/pqsec/mldsa/[a-z]*.c \
  src/mlkem/keccak.c src/mlkem/mlkem768.c src/mlkem/mlkem_kpe.c src/mlkem/mlkem_ntt.c \
  src/mlkem/mlkem_sample.c src/mlkem/mlkem_encode.c \
  src/tls/x25519.c src/tls/aead.c src/tls/hkdf.c src/robin_debanks/sha256.c \
  src/swarm/swarm_budget.c src/swarm/swarm_emotion.c src/swarm/swarm_market.c \
  src/swarm/swarm_hk.c src/ubh/ubh.c src/event_space/event_envelope.c \
  src/zcapital/zcapital.c -o /tmp/test_vinea -lm && /tmp/test_vinea [64..200]
```

### Results

| Run | Checks | Time |
|---|---|---|
| 128 nodes (default) | 210 pass | 18.2 s |
| 64 nodes | 210 pass | 15.3 s |
| 200 nodes | 210 pass | 22.4 s |
| ASan + UBSan, 64 nodes | 210 pass, no reports | 44 s |

At all three sizes, lookups match the true 20 closest at overlap 1.000 and
are exact in 24 of 24.

### Section 7 (two islands of 28 nodes)

- No bootstrap: each island discovers itself over multicast alone. No contacts
  leak across islands.
- The DHT works inside each island: STORE / FIND_VALUE, and lookups are exact.
- Offline items wait.
- One cached record then joins the islands. After one self-lookup per node:
  - about 12 cross-island contacts per node
  - 12 of 12 lookups exact over the merged 56-node network
  - island B's record is found from island A
- The spool delivers 3 items in order and exactly once, despite a deliberately
  dropped acknowledgement: 1 retry, 1 receiver dup. It refuses one item
  outside the receiver's agreement and lets one item expire.

### Freestanding and format checks

- Every `vna_*.c` compiles clean (`-Wall -Wextra -Werror`) with:
  - `gcc -ffreestanding`
  - `riscv64-linux-gnu-gcc -ffreestanding`
  - `clang --target=aarch64-none-elf -ffreestanding`

  The only external symbol beyond the reused crypto, swarm and UBH modules is
  compiler-emitted `memcpy`.
- `clang-format --dry-run --Werror` (repo `.clang-format`, clang-format 18) is
  clean on all files.
