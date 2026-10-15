<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The Grid Chain

The Grid Chain gives every stored thing in the Library of Alexandria
(docs/ALEXANDRIA.md) a fixed 64-bit number, and checks each release layer
against the layer below it. It adds two things to `kernel/src/alexandria`
(`alx_grid.c`): a coordinate word with a radix resolver, and a lineage
check. It adds no ledger and no token.

## 1. Two rules

1. **A coordinate is a key, never code.** "The coordinate is the opcode"
   means a word is a dispatch key into the sealed, verified index. Resolving
   it gives a booklet and the offset and length of verified bytes inside
   it. It never gives a pointer to jump to. The caller checks the booklet
   against its CID (`alx_booklet_check`) before using it. Running a block
   still goes through zxapp's checked call, with its contract checked on
   every call, and code pages stay read-only and executable while data pages
   stay writable and never executable (W^X, as the ELF loader and EL0 page
   tables enforce it). Nothing is executed because a number named it.
2. **No new ledger, no gas, no tokens.** Lineage events use the causal
   sequencer that exists (oseq). Nothing is charged per step and nothing is
   minted; the token-yield rule (measurement only) holds.

## 2. The coordinate word

A word is 64 bits, written high byte first:

| Bits | Field | Values |
|---|---|---|
| 63..56 | domain | see section 3 |
| 55..40 | place | 100..999, the class, division and section ("512") |
| 39..32 | item | 1..99 ("01") |
| 31..16 | extension | 0 for the sealed base; any other value names an addendum |
| 15..8 | format | 0 logic, 1 manifest |
| 7..0 | language | 1..99 and 101..120 for logic; 0 for a manifest |

So gcd in FORTRAN (`@512.01-001`) is `0x1002000100000001`, and gcd's
manifest is `0x1002000100000100`. The word holds the same place, item and
language number as the text names; it is the same address in a fixed width.

`alx_grid_word` makes the word for a slot. `alx_grid_resolve` turns a word
back into a slot and its bytes. It refuses, in this order:

- **FORM:** any field out of range, a domain that is not one of section 3,
  a manifest word with a language or a logic word without one.
- **DOMAIN:** a reserved domain. Nothing is stored there yet.
- **OVERLAY:** a non-zero extension. Addenda are not mounted (ALEXANDRIA.md
  section 5). An extension number alone never says which parent CID it
  extends, so the lineage check needs the addendum's text name.
- **NAME:** nothing at that place and item.
- **RETIRED:** the slot was withdrawn. Its number is never reused.
- **LANG:** the block has no logic in that language. Another language is
  never substituted.

## 3. Domains

| Domain | Holds | State |
|---|---|---|
| 0x10 | code: verified blocks, their logic and manifests | resolves |
| 0x20 | data: tables and reference sets | reserved |
| 0x50 | sensor streams: telemetry schemas and recorded readings | reserved |
| 0x60 | embeddings: language tables and model embeddings | reserved |
| 0x70 | attestations: signed test, review and build records | reserved |

A reserved domain is refused until its booklets and its checks exist.
Data in any domain is resolved to bytes and checked; it is never run.

## 4. The radix resolver

`alx_grid_build` builds a two-level radix table from an open index: the
first level is the place (900 buckets, one per section), the second the
items in that place, kept in rising order. A lookup is one bucket and a
binary search of at most 99 items. The table is about 12 KB, takes no
allocation and points into the index, so the index must outlive it.

## 5. Layer lineage

A release is a stack of layers. Each layer is named by the CID of its
manifest, and each manifest names its parent's CID:

| Layer | Holds |
|---|---|
| L0 | the hardware spec |
| L1 | Disc 1, the core |
| L2 | the swarm discs |
| L3 | the Library of Alexandria |
| L4 | community overlays; an overlay may sit on another overlay |

`alx_lineage` checks a chain given root first. It hashes each manifest to
its CID (CIDv1 raw sha2-256; a manifest is at most 1 MiB) and checks that
the next layer names exactly that CID as its parent. L0 names no parent.
Levels rise by one, except that L4 may follow L4. A chain is at most 64
layers. It returns the top layer's CID, or the first layer that fails:
FORM for a malformed layer, LINEAGE for a parent that does not match.
Changing any byte of a lower manifest breaks every layer above it.

The layer manifest format itself belongs to the bucket owner (ipfs-dist).
The check needs only the manifest bytes and the parent CID it names.

## 6. Recording lineage: oseq, not a new chain

Each verified step (parent CID, then the delta, then the child CID) is
recorded as an oseq event: the schema `alx.layer`, the child's CID digest as
the payload hash, and the parent layer's event as its causal parent. oseq
already gives monotonic ordinals, causal parents, replay refusal and cycle
detection, which is everything this record needs.

pdx_ledger is not used for lineage. Its claims move amounts between
accounts, and lineage moves no value, so recording lineage there would mean
inventing an amount. What lineage takes from it is its first rule: two
children that claim the same place on one parent are both kept and neither
is mounted. The slot is BOTH and is held until the operator's trust policy
picks one. Neither is ever silently dropped.

## 7. What is built

- Built (`make test-alexandria`): the word, the resolver, the lineage check
  and the `cell.` namespace (class 900) for swarm cells.
- Not built: the oseq recording of layer events, the reserved domains,
  mounting addenda, and the layer manifest format.
