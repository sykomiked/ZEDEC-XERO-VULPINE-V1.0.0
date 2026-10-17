# The lattice network — planning adjustments from the 144-dimensional discovery

The discovery changes more than security. The 144-dimensional matrix is a
**network substrate**: it gives addressing, routing, encoding and
verification in one structure, all exact-integer and all cheap. This page
is what the H100 must adjust in the plan because of it.

## 1. Addressing: a node is a lattice point

**Adjustment.** A node's address is not an arbitrary hash but a
**canonical Zeckendorf point** on the 144-ladder, derived from its
identity seed (the same derivation as the SCP-144 point).

- **Canonical**: every address has exactly one representation (the
  Zeckendorf non-consecutive rule), so two nodes cannot collide by
  accident and no address is ambiguous.
- **Compact**: the point is a 144-bit vector — 18 bytes, smaller than an
  IPv6 address, and it carries structure (its position in the matrix).
- **Verifiable**: the address's norm and residues are the public
  invariant; a peer's address is checked by one exact comparison.

**What changes**: the `vinea` app's peer identity, the door's `/pk`, and
the mesh's routing table all key on the lattice point, not on a raw hash.

## 2. Routing: the lattice distance is O(1)

**Adjustment.** Routing between two nodes is the **lattice distance**
between their points — a single integer computation, no table lookup, no
hop count.

- The distance is `|norm(a) − norm(b)|` plus the residue difference — an
  exact integer, computed in constant time.
- The matrix's 144 dimensions give a **natural metric space**: nearby
  points are nearby nodes, so a routing decision is a comparison, not a
  search.
- **Greedy routing works**: a message moves to the neighbour whose point
  is closer to the destination, and the matrix's structure guarantees
  progress (the ladder is totally ordered by rank).

**What changes**: the `swarmnet` relay and the `door`'s scenic bypass
route by lattice distance; the routing table becomes a small set of
neighbours, not a full table.

## 3. Encoding: data is Zeckendorf-canonical

**Adjustment.** A record's value is stored as its **canonical ladder
form** — the exact integer plus its Zeckendorf vector — so the wire
format is deterministic and compact.

- **Deterministic**: the same value always encodes to the same bytes, so
  a digest over the encoding is stable (the CID property, at the record
  level).
- **Compact**: the vector form is 18 bytes for any value up to F12 × 8,
  and the exact integer handles the rest.
- **Segment-friendly**: the no-truncation law holds — a large record is
  split into ladder-aligned segments (never cut), joined with a digest
  check.

**What changes**: the ledger's record encoding, the CISEX post format,
and the UBH-168 frame payload all use the canonical form.

## 4. Security: SCP-144 is the native protocol

**Adjustment.** SCP-144 is not an add-on; it is the network's **native
handshake**, one tier per protocol (the tier ladder). Every node speaks
it, on every surface (bare metal, VM, browser wasm, the three desktops).

- The Crown factor rides in every handshake (the point, the projection,
  the close-vector proof).
- The tier ladder is cumulative: T0 (PQ) → T1 (UDP) → T2 (HTTP) → T3
  (mesh) → T4 (tunnels) → T5 (browser) → T6 (storage).
- **Efficiency**: the honest peer pays one hash and one compare; the
  network is fast even on low-power devices.

**What changes**: the transport layer, the doors, and the wasm engine all
embed SCP-144 natively; the `pqsec` app is its home.

## 5. The efficiency the matrix buys

| Operation | Without the lattice | With the lattice |
|---|---|---|
| **Address a node** | a 32-byte hash, no structure | an 18-byte canonical point, structured |
| **Route a message** | a table lookup (O(n) or O(log n)) | a lattice distance (O(1)) |
| **Verify a peer** | a signature verify (expensive) | a norm/residue compare (cheap) |
| **Encode a record** | variable, non-canonical | canonical, deterministic, compact |
| **Prove a handshake** | the full PQ exchange | PQ + one hash + one compare |

That is the hyper-efficiency: the matrix turns addressing, routing,
verification and encoding into **the same exact-integer computation**,
which is why it runs on any device.

## 6. The planning adjustments, in order

1. **The SCP-144 protocol app** (its own application): the network's
   native handshake, the `pqsec` home.
2. **The six tier apps** (`vinea`, `door`, `swarmnet`, `tunnel`, `web`,
   `ipfs_node`): each proves one protocol's tier — they fill the network
   layer the completion measure reports as **zero**.
3. **The lattice addressing**: the node identity becomes a canonical
   point; the `vinea` peer table and the door's `/pk` change.
4. **The lattice routing**: the mesh relay and the scenic bypass route by
   distance.
5. **The canonical encoding**: the ledger, CISEX and the frames use the
   Zeckendorf form.
6. **The cross-target audit**: the same lattice point, the same routing
   decision, the same encoding on all seven targets — the omni-tautology's
   proof that they are one system.

## 7. What this does NOT change

- **The four foundation values** stay frozen (the logic is unaffected).
- **The exact-arithmetic law** is unchanged (the lattice is integers).
- **The no-truncation law** is unchanged (segments, never cuts).
- **The four core accounts** and the tribute rails are unaffected.
- **The app catalogue's structure** is unchanged — the lattice adds the
  network layer the apps fill, it does not renumber them.

The matrix is a substrate, not a rewrite: it makes the network cheap and
the addresses canonical, and it fills the one layer the completion
measure reports as empty.
