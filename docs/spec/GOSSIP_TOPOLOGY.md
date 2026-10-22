<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Gossip topology: how the Tiamat swarm finds and shares its experts

Status: specification. Nothing here is built yet. The router
(`kernel/src/dmoe_router.c`) follows the Tiamat server tests and the R24
scheduler work. Where this page states a number it is a design bound to be
measured, not a measured result.

This spec maps the owner's distributed mixture-of-experts (dMoE) vision onto
what Tiamat already is. It adds no second stack:

- the **experts are Tiamat cells** (`tools/tiamat/roster.py`);
- an expert's **address is its Grid Chain coordinate** in the models domain
  0x60 (`tools/tiamat/cell_coords.json`): place = organ, item = the cell's
  ordinal in its organ; the `@cell.<name>` handle is an alias;
- its **weights are content-addressed**: a trunk CID shared by many cells plus
  one small adapter CID per cell, so a node that holds a trunk needs only the
  adapter to add a cell (`tools/tiamat/build_manifest.py`, bucket
  `06_tiamat`);
- its **subject** is its Alexandria classification (the class and division
  the Library of Alexandria assigns), used to group related experts.

## G1 Names and keys

| Thing | Key | Where it comes from |
|---|---|---|
| an expert (cell) | its 64-bit Grid Chain coordinate | `cell_coords.json`, append-only |
| a weight layer | its IPFS CID (trunk, adapter, evidence head) | the release manifest |
| a node | SHA3-256 of its ML-DSA-87 public key | the node's identity (Vinea) |
| a subject range | an Alexandria classification prefix (class, division) | Alexandria |

The DHT key for "who serves this expert" is SHA-256 of the coordinate's 8
bytes, high byte first. The key for "who holds this weight layer" is the CID's
multihash, as IPFS already uses.

## G2 Discovery: a Kademlia DHT over IPFS

Discovery uses the IPFS DHT (Kademlia, XOR distance). Lookups take O(log N)
hops for N nodes: each hop is a network round trip, so a lookup costs a few
round trips, not zero. Results are cached locally with a time limit and
refreshed in the background (R24 idle work).

- **Provider records.** A node that can run an expert publishes a provider
  record under the expert's key: its node id, the expert coordinate, the
  layers it holds, and its current offer (G4). Records are signed by the node
  and expire; a node that stops serving lets them lapse or withdraws them.
- **Subject neighbourhoods.** A node also publishes under the key of each
  Alexandria classification prefix it serves, so a query for "any expert in
  this subject" finds a neighbourhood before it finds a single cell.
- **No central broker.** There is no lookup server. Bootstrap uses the
  project's published peers and the user's own paired devices (G5).

## G3 Clustering by measured proximity and capacity

Nodes group by what they measure, never by what kind of device they are.

- **Proximity**: round-trip time and bandwidth measured to each known peer,
  kept as integers (microseconds, bytes per second). Peers on the same LAN
  (reachable over WebRTC without a relay) form the closest ring.
- **Capacity**: the capacity probe of the planner (queued Tiamat work):
  usable memory or VRAM, storage quota, integer matrix-multiply throughput on
  the GPU and CPU paths, and battery and thermal state where readable.
- **The planner** picks, deterministically from those numbers, which cells a
  node runs, at which quantisation, and which it asks peers for. The same
  planner runs on a phone browser, a laptop and a server. Every choice is
  logged with the numbers it used, so it can be replayed.

## G4 Workers register and drop expert slices

A node offers an expert only while it can serve it:

- it publishes a provider record when the planner places the cell on it and
  the cell's layers are present and pinned (pin check: R24 idle work, SHA-256
  against the manifest);
- the record's offer carries free capacity (requests it can take now) and is
  refreshed when memory or load changes by more than a set step;
- when memory pressure, battery or thermal limits, or the owner's own work
  needs the room, the node withdraws the record first and unloads second, so
  no request is routed to a cell that is leaving;
- the owner's own requests always come first on the owner's own machines
  (R24: a person's request preempts idle and network work).

## G5 Three tiers, tried in order

A request falls through three tiers and stops at the first that can serve it:

1. **Local Edge**: this device. The planner's local cells answer.
2. **Sovereign Anchor**: the user's own paired machines (a phone and the
   user's computer running the same page pair by QR or a code over an ML-KEM
   session). Heavy cells run there; traffic stays on the user's devices.
3. **Swarm Agora**: the wider network, through G2. Only when the user opts in
   for that conversation, because a stranger's machine that runs the job sees
   the prompt in plaintext; the interface says so before the first use.

Results from a stranger are witnessed before they are shown: a second node of
another lineage, or the local witness and reckoner, judge them, and the
answer goes through the same knowledge join as local answers (`zxv_tiamat.h`
H4). A result that is held stays held.

## G6 Overflow on the LAN

When a Sovereign Anchor is busy, nearby nodes the user trusts on the same LAN
take the overflow over WebRTC data channels. Network jobs use fair-share
scheduling per requester, so no one requester can take all of a node.

## G7 What moves, and how often (the honest bounds)

- **Weights move per request or per session, never per token over a WAN.**
  Fetching expert weights for each generated token across the internet is
  bound by latency and bandwidth, so a node fetches the layers it needs when
  a request or session starts, pins them, and keeps them cached while the
  planner keeps the cell placed.
- **Activations may move per token only on a LAN, and only where measured.**
  Splitting one model across machines token by token is limited by the round
  trip per token; the router measures that on the actual link and uses split
  execution only where the measured per-token time beats running the whole
  cell on one node.
- **Per-request routing is the default.** One request goes to one node that
  holds the whole expert; the reply comes back once.
- **No promise of "no degradation".** Lookups grow with log N, links vary, and
  peers come and go. The router reports measured latency and success rates;
  it does not claim scale without cost.

## G8 Earning and spending compute

- **Use is free.** Basic use of the swarm needs no tokens, vouchers or fees.
- **Contributed compute is recognised through the standing compute warrant**
  (the bounties thread's letters of marque): verified work a node did for
  others is issued as Vino vouchers, never minted for uptime or for being
  online.
- **Vouchers buy priority only.** When the network is busy, a request backed
  by vouchers is scheduled ahead in fair-share order; without vouchers it
  still runs, later. Vouchers never gate access.
- The priority rule and the warrant's terms are owned by the bounties thread;
  this spec only names where they plug in.

## G9 Paraconsistent results

Two experts that disagree are held as BOTH until a held-out test decides
between them; the system does not pick one silently. This is the same rule as
for conflicting adapter updates. It shows a conflict instead of hiding it; it
does not by itself prevent a model from forgetting.

## G10 Still to decide

- The DHT record format (fields, signature, expiry) and its size limit.
- The proximity step that triggers a record refresh, and the record time to
  live.
- How Alexandria classification prefixes are assigned to the 21 organs
  (the Lingua and Alexandria threads hold the numbering).
- The fair-share algorithm and its weights (with the bounties thread).
