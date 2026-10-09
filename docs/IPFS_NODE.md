<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# The ZXV IPFS Node

A local InterPlanetary File System node inside the ZXV kernel. The OS and the
AI use it for three things:

1. **Pulling content from the network.** A CID is fetched and every block is
   checked before it is used.
2. **Naming items in the peer-to-peer economy.** The Carracho network uses
   these CIDs as the IDs of the things it trades.
3. **Naming files in the system.** Every file gets a content ID next to its
   path.

Not every CID is public. Some name private files. CIDs still work well as an
index for everything, as long as the node treats visibility as part of the
data. This document explains how it does that.

Status: built 2026-10-09 in `kernel/src/ipfs_node/` and tested by
`kernel/src/ipfs_node/test_ipfs_node.c` against vectors made with Kubo 0.32.1.
The module is not yet wired into `kernel/Makefile`. The build line is at the
top of the test file.

`kernel/src/ipfs/` has not changed. Its "CID" is a bare SHA-256 digest, not an
IPFS CID. This module makes real CIDs. Code that needs to work with the IPFS
network should use this module.

---

## 1. What interoperates, and what does not

| Works with the IPFS network | Not implemented |
|---|---|
| CIDv1 and CIDv0, byte for byte as other implementations write them | libp2p (no peer ID, no transports, no Noise or TLS handshake) |
| `ipfs add --cid-version=1` root CIDs (Kubo defaults) | Bitswap |
| Trustless gateway `?format=raw` and `?format=car` | The public IPFS DHT (no provider records are published there) |
| CAR v1 read and write (matches `ipfs dag export` byte for byte) | CARv2, directories and HAMT shards, non-file UnixFS |

ZXV talks to IPFS through **gateways and CIDs**. It is not a full libp2p peer.
A CID made here is the same one any IPFS implementation computes for the same
bytes. Content can therefore be published to a pinning service or gateway, and
anyone on IPFS can look it up by that CID. The node itself does not take part
in the DHT or in Bitswap.

## 2. Files and API

All of the code is freestanding C11: integer only, no libc, no allocation. The
caller supplies every buffer and table. There is no 64-bit division. The files
compile cleanly with `gcc -Wall -Wextra -Werror`, `riscv64-linux-gnu-gcc
-ffreestanding` and `clang --target=aarch64-none-elf -ffreestanding`, and they
reference no libc symbols.

| File | Contents |
|---|---|
| `ipfs_node.h` | The whole public API |
| `ipfsn_multiformats.c` | Unsigned varint, multihash (sha2-256 `0x12`, identity `0x00`), CIDv0/v1 binary, base32-lower (`b…`), base58btc (`Qm…`), v0→v1 |
| `ipfsn_unixfs.c` | Streaming UnixFS/dag-pb builder, strict dag-pb and UnixFS readers, DAG walker (`ipfsn_cat`) |
| `ipfsn_store.c` | Blockstore over host storage ops, sealed private blocks, pins, provider gate, `ipfsn_add` |
| `ipfsn_net.c` | CAR v1 reader and writer, trustless-gateway client, Carracho peer hook, `ipfsn_node_cat` |
| `ipfsn_ubh.c` | UBH-168 framing of CIDs and blocks, text form, wire negotiation |
| `ipfsn_fidx.c` | OS file index: path → CID and CID → paths |

Dependencies, all reused rather than copied: `robin_debanks/sha256`,
`tls/aead` (ChaCha20-Poly1305), `tls/hkdf` (HMAC-SHA256), `ubh/ubh` and
`event_space/event_envelope` (UBH-168 header pack and unpack).

## 3. CIDs and UnixFS

`ipfsn_ufs_*` builds a file the same way Kubo does:

- 256 KiB fixed chunks.
- Raw leaves. A file of one chunk or less, including the empty file, is just
  that raw block.
- A balanced DAG with at most 174 links per dag-pb node. A node is closed when
  it is full and another link arrives.
- Links are Hash, an empty Name and Tsize. Data is
  UnixFS{File, filesize, blocksizes}.

The caller feeds in bytes. Blocks go to a sink callback, children before their
parents, and the root comes last. Memory stays bounded: one 256 KiB chunk
buffer plus one pending node per level, about 150 KB of state, whatever the
file size.

`ipfsn_cat` rebuilds a file from any block source. It re-hashes every block.
It also checks every `blocksizes[i]` against the bytes that child actually
produced, and every `filesize` against its subtree. A source that lies is
caught there and never trusted.

## 4. Blockstore

The store is an append-only log over `ipfsn_storage_ops_t` (read, write and
size, supplied by the host: a zxvfs file, a raw partition, or RAM through
`ipfsn_memstore_t`). An in-RAM hash table maps a CID to (offset, length).

- `put` hashes the bytes first and refuses a block that does not match its CID.
- `get` re-hashes on every read. Bit rot or a tampered disk gives
  `IPFSN_ERR_HASH` and never returns data.
- `mount` rebuilds the index from the medium. A torn final append is dropped
  and later overwritten.
- CIDv0 and CIDv1 of the same dag-pb block are the same entry.

Not implemented: garbage collection and deletion. The log only grows.

## 5. Visibility: public and private

Every pin, block and index entry is either **PUBLIC** or **PRIVATE**.

| | PUBLIC | PRIVATE |
|---|---|---|
| Announced (`ipfsn_provide`, `ipfsn_provide_all`) | yes, if pinned public | **refused** (`IPFSN_ERR_PRIVATE`) |
| Served to peers (`ipfsn_serve_block`) | yes | **refused** |
| Put in a gateway URL or a peer request | yes | **never**. A missing private block is "not found". `ipfsn_node_cat` keeps the whole walk of a private DAG local. |
| At rest | plaintext | ChaCha20-Poly1305 under the node key |

Changing a private pin to public, or a public pin to private, gives
`IPFSN_ERR_CONFLICT`. Making something public has to be a deliberate act.

**Sealing.** `seal_key = HMAC(node_key, "zxv-ipfs-seal")`. The nonce is
`HMAC(nonce_key, plaintext CID)` cut to 96 bits. The sealed record holds
`cid_len | plaintext CID | block`, so the plaintext CID is never written to
disk in the clear. A stolen disk does not show that it holds a copy of some
public file.

The nonce is deterministic, and that is safe here. A nonce can only repeat for
the same CID, and one CID names exactly one plaintext. The exception is a
96-bit collision between two different CIDs, which needs about 2^48 private
blocks. Because of this, the same private block always seals to the same
bytes, so private copies deduplicate. The node key is the caller's
responsibility. It should come from a sealed keystore, a TPM or a passphrase
KDF, never from code.

### The confidentiality caveat, and private-CID mode

**A plaintext CID shows equality with every public copy.** A CID is a hash of
the content. If a private file's CID is ever shown, logged, put in the file
index or sent to the economy, anyone holding a public copy of the same bytes
learns that this node holds them too. Anyone can also test a guess ("is this
the leaked report?") by hashing it. Encryption at rest does not help once the
CID itself gets out.

Adding a file with `private_cid = true` turns on **private-CID mode**. The
file's handle is then the **private CID**: a CIDv1 raw sha2-256 of the sealed
root record. The pin's handle and the file index use the private CID, and so
does anything given to the OS or the economy. The plaintext CID stays only in
RAM and inside the ciphertext.

What private-CID mode costs:

- **No global dedup or interop.** A private CID matches nothing on IPFS.
  Gateways and other nodes cannot serve it, and there is no reason they should.
- **Each node has its own private CIDs.** The same file under a different node
  key gets a different private CID. Two private copies on one node still share
  one (sealing is deterministic).
- **Fetching it means having the key.** Without the key, a private CID can only
  check that the ciphertext is intact. It cannot recover or verify the
  plaintext.
- **Equality within one node still shows.** Anyone who sees two equal private
  CIDs from one node knows they are the same file. This is the price of dedup.

Use the plaintext CID when matching the public copy is the point (publishing,
dedup with the network). Use private-CID mode whenever the fact that you hold
the file is itself sensitive.

## 6. File index (`ipfsn_fidx_*`)

The index maps path → (CID, size, visibility) and CID → every path. Paths are
VFS paths (`src/vfs`, `VFS_PATH_LEN` = 256 including the NUL). zxvfs files
appear under their mount point, for example `/zxvfs/notes.txt`, because zxvfs
names are flat 32-byte names. The index accepts only canonical paths: no
`//`, `.`, `..` or trailing `/`. That way one file cannot hide behind two
spellings.

Two paths with the same bytes share one CID and one copy in the blockstore. A
file added in private-CID mode is indexed by its private CID. The index is
caller-owned RAM and is not persisted.

## 7. Network fetch

- **Gateway.** `ipfsn_gw_url` builds `<base>/ipfs/<cidv1>?format=raw|car`. The
  host's `https_get` sends it with `Accept: application/vnd.ipld.raw` or
  `application/vnd.ipld.car`.
- **Raw responses.** A raw response is checked against its CID before it is
  stored.
- **CAR responses.** The CAR's roots must include the CID requested. Every
  block is checked in a first pass, before any block is stored. A single
  tampered block rejects the whole response.
- **Carracho peers.** `ipfsn_peer_ops_t.want(cid_bin) → bytes` is the hook.
  The bytes are decoded in the negotiated wire syntax (§8) and checked here.
- **One source for everything.** `ipfsn_node_source` tries the local store,
  then peers, then the gateway. Fetched blocks are stored as PUBLIC, since they
  came from the network.

## 8. UBH-168: the ZXV wire syntax

The owner's rule: UBH-168 is the standard syntax between ZXV IPFS nodes. It is
the default, and especially the default in the external economy. ZXV still has
to work with systems that do not use 168-bit framing.

**Envelope.** Every frame is 21 octets (168 bits).

| Frame | Contents |
|---|---|
| 0 | `CONTENT_OBJECT` header from `ubh_168_header_pack`: magic `ZXV`, version 1, `source_format_id = UBH_FMT_UBH_168`, `target_type` 1 = CID or 2 = block, flags has_payload\|canonical, `payload_length` = exact bytes, `schema_id` = `"IPFS"`, `integrity_ref` = first 3 octets of the CID digest (a routing hint) |
| 1..k | Payload: a binary CID, or a binary CID followed by the block. The last frame is padded with zeros, and the decoder requires the padding to be zero. |
| k+1 | `INTEGRITY` trailer: `payload_length` repeated, `schema_id = k`, `integrity_ref` = first 3 octets of SHA-256(frames 0..k) |

The payload frames are contiguous, so decoding is zero-copy. The decoder
checks every header field, the exact frame count, the padding and the trailer.
It then **re-hashes the block against the full CID it carries**. The trailer
catches framing damage early, but it is not what makes a block trustworthy.
The CID is.

**Hashes are never cut to 168 bits.** The CID always keeps its full sha2-256
multihash. A 168-bit digest would give only 84-bit collision resistance
(birthday bound), and about 56 bits against quantum collision search
(Brassard–Høyer–Tapp). That is far too weak for an economy where a collision
means two different goods under one ID. 168 bits is the framing unit, not the
digest size.

**Text form.** `ubh168:b<base32 of the UBH-168 CID envelope>`. A `:` never
appears in a multibase string, so this form cannot be taken for a plain CID,
and the parsers reject each other's input. The standard `b…` CIDv1 string is
always available, and it is what goes to gateways and other systems.

**Negotiation.** A ZXV node opens with a 21-octet `FORMAT_IDENTITY` frame
(`ipfsn_wire_hello`). `ipfsn_wire_negotiate` picks UBH-168 only if the other
side's opening is such a frame with the ZXV IPFS schema. Anything else means
plain IPFS bytes: no hello, an HTTP gateway, Kubo, or any non-ZXV peer. The
same is true when UBH-168 is turned off locally. Nodes default to UBH-168 for
peers (`ipfsn_node_init`). The mode is never guessed from the block bytes,
because a plain block can start with anything. A peer that speaks the other
syntax fails closed and is never misread. **The CID is the same either way.**

## 9. Tests and where the vectors come from

`test_ipfs_node.c` makes 536 checks. They take about 3 s at `-O2`, and about
21 s under `-fsanitize=address,undefined`, which runs clean.

- **Root CIDs.** Made by **Kubo 0.32.1**, the official
  `kubo_v0.32.1_linux-amd64` release from github.com/ipfs/kubo, checked
  against its published `.sha512`. The command was
  `ipfs add -Q --offline --cid-version=1 [--chunker=size-N]`, run on files from
  the same xorshift32 generator the test uses. Sizes: 256 KiB (one chunk),
  256 KiB + 1, 1 MiB, 174 chunks exactly, 174 chunks + 1 byte (depth 2),
  50 MiB, and with `size-16` chunks 174 and 175 leaves, 174² leaves (full
  depth 2) and 174² + 1 leaves (depth 3).
- **CAR.** The output of Kubo's `ipfs dag export` for a 200-byte `size-64` file
  is embedded byte for byte. The parser reads it, the walker rebuilds the file
  from it, and the CAR writer reproduces it exactly.
- **CIDv0.** `QmT78z…` for `"hello world\n"` from `ipfs add`, and its v1 form
  from `ipfs cid base32`. A second embedded Kubo CAR holds a CIDv0 file with
  dag-pb leaves (`ipfs add --chunker=size-64`, v0 default), which the walker
  rebuilds.
- **Empty file and `"hello world\n"`.** The raw CIDs, checked against Kubo and
  against the coreutils `sha256sum` digest.
- **base32 and base58.** RFC 4648 `"foo"` and the draft-msporny-base58
  `"hello world"` vector.

The negative tests cover:

- non-minimal and overlong varints;
- uppercase, padded or bad-tail base32;
- CIDv0 inside a multibase string, and unsupported multihashes;
- out-of-order or duplicate dag-pb fields;
- blocksizes that do not match the children;
- a corrupted block in the store or behind the walker;
- a tampered CAR block, which leaves nothing stored;
- a tampered gateway or peer reply;
- a peer speaking the other wire syntax;
- every single-bit flip of a UBH-168 CID envelope;
- non-zero UBH padding;
- a well-framed UBH envelope with the wrong bytes;
- private CIDs: never announced or served, never sent to a gateway or peer
  (checked with transport spies), not in the clear on the medium, a tampered
  sealed record, and a wrong or missing key;
- non-canonical paths.

## 10. Honest limits

- No libp2p, Bitswap or DHT (§1). Announcing goes to a host-supplied provider
  hook (for example a pinning service or the Carracho provider table), not to
  the IPFS DHT.
- Only UnixFS **files** are built and walked. There are no directories, HAMT
  shards, symlinks, mode or mtime (mode and mtime are parsed and skipped), and
  no trickle layout or other chunkers.
- `ipfsn_add` matches Kubo only with the defaults: 256 KiB, raw leaves,
  balanced, 174 links. Other `chunk_size` values match Kubo's
  `--chunker=size-N`. The `--cid-version=0` default (dag-pb leaves) is read
  (tested) but not built.
- CIDs: only sha2-256 and identity multihashes. Strings: only `b` (base32
  lower) and `Qm` (base58btc v0). Anything else is refused, not guessed.
- The blockstore has no GC. The file index and pin table live in RAM and are
  not persisted (the blockstore is). The hash table and pin table have fixed
  sizes chosen by the caller.
- Sealing hides content and plaintext CIDs at rest. It does not hide block
  sizes or how many blocks there are. The key's safety is up to the caller.
- The gateway client trusts no gateway for integrity, but it cannot stop a
  gateway from seeing which public CIDs were requested.
