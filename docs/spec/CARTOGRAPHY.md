<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Cartography: index nodes and cartographers' overlays

The Grid Chain (GRID_CHAIN.md) gives everything in the Library of
Alexandria a 64-bit word. Cartography maps those words to content: the
library's own blocks through a sealed core index, and content outside the
library through signed overlays that cartographers publish. The code is
`kernel/src/alexandria/alx_carto.c` (format and rules in `alx_carto.h`) and
the `zxv-index` tool in `tools/zxv-index`.

## 1. Rules

1. **An index entry is a key to bytes, never code.** A core entry points
   only at bytes the sealed library already holds. An overlay holds no code
   words at all: executable content enters only as a verified slot of the
   sealed library (verified-slot admission), never because a cartographer
   named it.
2. **Every overlay entry is a claim.** It is trusted provisionally under the
   good-nature rule and demoted on contrary evidence: the target failed its
   check when fetched, or a policy refused it. A demoted target is still
   reported, marked DEMOTED, so the caller can see why it is not used.
3. **Disagreement is held, not settled.** Two mounted overlays that give one
   word different targets resolve to BOTH, and neither is used until the
   operator's trust policy picks one.
4. **Post-quantum signatures only.** Overlays are signed with ML-DSA-65 under
   the context `zxv.alexandria.index.v1`. Elliptic-curve account keys are
   not accepted.
5. **No ledger, no tokens.** Building or mounting an index charges nothing
   and mints nothing.

## 2. The index format

Core and overlay share one format, big-endian:

| Bytes | Field |
|---|---|
| 0..3 | `ALXI` |
| 4 | version 1 |
| 5 | kind: 0 core, 1 overlay |
| 6..7 | division: the overlay's division (100..990, a multiple of 10); 0 for a core |
| 8..11 | entries, 1..10000 |
| 12..15 | zero |
| 16..51 | parent CID: what this index extends |
| 52..63 | zero |

Then one 96-byte entry per word, words strictly rising:

| Offset | Field |
|---|---|
| 0 | the grid word |
| 8 | target CID (36 bytes: CIDv1, sha2-256) |
| 44 | disc sector (LBA); 0 means fetch by CID |
| 48 | length in bytes, never 0 |
| 52 | offset inside the container |
| 56 | container CID, or zero when the target stands alone |
| 92 | zero |

A core index ends with the SHA-256 of everything before it. An overlay ends
with the cartographer's ML-DSA-65 public key (1952 bytes) and the signature
(3309 bytes) over the SHA-256 of everything before the key. Any index fits
one 1 MiB block, so its own CID is the raw sha2-256 CID of its bytes.

## 3. The core index

`tools/alexandria/build_index.py --out-dir` writes `ALEXANDRIA.IDX` beside
the library, and it goes onto Disc 1 with it. It has one entry per block
manifest and per language: the code-domain word, the target's own CID, and
the booklet, offset and length that hold it. Its parent is the CID of the
library's table, so the core hangs from the sealed library.

`alx_carto_core` opens it only if the hash matches, the parent is that
library's table, it lists every manifest and every language of every live
slot and nothing else, each entry gives exactly the booklet, offset and
length the library gives, and each manifest's target CID is the manifest's
content id. A core index can therefore never point anywhere the sealed
library does not, and it works with no network at all.

## 4. Overlays

An overlay maps words in one division of the data (0x20), sensor (0x50),
embedding (0x60) or attestation (0x70) domain to external content: datasets,
recorded telemetry, archives, model files. Targets may be raw or dag-pb
CIDs, so content larger than one block is named by the CID IPFS gives it.

`alx_carto_mount` checks, in order:

1. **FORM:** the format above; every word in the overlay's division and
   domain; no code words.
2. **TRUST:** the signing key is one the operator trusts.
3. **SIGN:** the signature verifies.
4. Already mounted: OK, nothing changes.
5. **LINEAGE:** the parent is the core index or an overlay already mounted.
6. **LIMIT:** at most 16 overlays at once.

`alx_carto_resolve` looks in the core first (SEALED), then in the overlays
(CLAIM, or DEMOTED), and returns which index answered. `alx_carto_demote`
records contrary evidence about a target; core targets cannot be demoted.

## 5. The zxv-index tool

```
zxv-index keygen PK SK [SEED-HEX]      an ML-DSA-65 key pair (SK owner-only)
zxv-index scan DIR DIVISION DOMAIN      a mapping table for the files in DIR
zxv-index build TABLE DIVISION PARENT-CID PK SK OUT   the signed overlay
zxv-index check FILE                    form, signature and entries
```

`scan` gives each file one word (520.01 .. 520.99, then 521.01 ..) and the
CID of its bytes as one raw block, so it refuses files over 1 MiB; those get
the CID IPFS gives them, entered by hand. The table is plain text (word,
CID, length, optional sector) and can be edited before `build`. `build`
refuses code words, words outside the division and malformed lines.

## 6. What the node does (not built here)

Finding overlays and deciding whose to trust belong to the node, not to
this library:

- **Trusted keys** come from the operator's configuration, never from the
  network.
- **Discovery** of new overlays over pubsub runs only when the network mode
  allows it, and is off under a full air-gap seal.
- **Mounting** goes through the community path (evo_comm), which fetches the
  overlay by CID, checks it with `alx_carto_mount` and records the result.
- **Lineage events** (an overlay mounted on its parent) are recorded with
  oseq, as for release layers (GRID_CHAIN.md section 6).

## 7. What is built

- Built (`make test-alexandria`): the format, the core index written by
  build_index.py and checked against the library, overlay signing and
  mounting with ML-DSA-65, trust, lineage, BOTH, demotion, and the zxv-index
  tool with an end-to-end test.
- Not built: the zxvd mount loop, pubsub discovery, the trusted-key
  configuration and the oseq records.
