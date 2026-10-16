<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Paper III: The Immutable Epistemic Graph

*A Self-Verifying Operating System Merkle-DAG Across an Eight-Region Sovereign
Edge Mesh*

**Authors:** Michael Laurence Curzi & 36N9 Genetics, LLC
**Implementation Reference:** `kernel/src/ipfs_node/`, `kernel/src/ubh/`, `tools/seal.py`, `build_system/`
**License:** Apache-2.0

---

## Abstract

Digital knowledge and system software today rest on fragile, mutable substrates:
centralized DNS registries, opaque package managers, ephemeral container layers,
and HTTP endpoints subject to link rot, silent tampering, and unilateral
deplatforming. When a dependency disappears or a certificate authority revokes a
domain, downstream builds and archives fail.

This paper describes the **Immutable Epistemic Graph** of ZXV: an architecture
in which an entire operating system—its freestanding source tree, reproducible
compilers and scripts, bare-metal BIOS/UEFI disc images, WebAssembly browser
runtime, 407 layered applications, post-quantum cryptographic primitives, and
scholarly documentation—is sealed into a single content-addressed UnixFS
Merkle-DAG and exported as a deterministic Content-Addressable Archive (`.car`).
Every block in the graph is verified on read by a freestanding C11 IPFS node
embedded directly inside the kernel (`kernel/src/ipfs_node/`). Replicated across
an eight-region planetary bootstrap mesh with MAC-keyed autonomous static link
bring-up, the graph remains permanently addressable, bit-for-bit verifiable, and
bootable across five execution surfaces with zero reliance on centralized naming
or cloud control planes.

---

## 1. The Fragility of Location-Addressed Software Distribution

Conventional operating systems and research archives separate *naming* from
*content*:
- A URL (`https://host.example/pkg.tar.gz`) names a network socket and a file
  path, not the bytes stored there. If the operator modifies the file, goes
  bankrupt, or blocks a region, the URL breaks or serves different bytes.
- Package ecosystems fetch transitive dependencies from dozens of mutable
  registries at build time, making bit-for-bit reproducibility across years or
  architectures nearly impossible.
- Standard IPFS deployments rely on heavy user-space daemons (Go/Rust runtimes
  with hundreds of megabytes of heap allocation) that cannot run inside a
  freestanding Ring-0 kernel or a 184 KB browser WebAssembly image.

ZXV eliminates the gap between the operating system and the content-addressed
graph: **the release is a Merkle-DAG, and the kernel natively speaks that DAG**.

---

## 2. Freestanding Kernel-Native Content Addressing (`kernel/src/ipfs_node/`)

Rather than embedding a heavyweight libp2p runtime, `kernel/src/ipfs_node/`
implements the exact cryptographic and structural core of IPFS in freestanding,
integer-only C11 with zero dynamic memory allocation (`malloc` is never called;
no 64-bit division is emitted; compiles clean on `arm64`, `x86_64`, `riscv64`,
`riscv32`, and `arm32`):

| Component | Source File | Structural Guarantee |
|---|---|---|
| **Multiformats & CIDv0/v1** | `ipfsn_multiformats.c` | Unsigned varint, `sha2-256` (`0x12`) & `identity` (`0x00`) multihash, binary & text CIDv0 (`Qm…` base58btc) and CIDv1 (`b…` base32-lower), lossless v0$\to$v1 upgrade. |
| **Streaming UnixFS Builder & Verifier** | `ipfsn_unixfs.c` | Kubo-identical 256 KiB chunking, raw leaves for single-chunk files, balanced `dag-pb` trees with at most 174 links per node, bounded ~150 KB state regardless of file size. |
| **Tamper-Evident Blockstore** | `ipfsn_store.c` | Append-only log over `ipfsn_storage_ops_t`; `put` hashes before write and rejects mismatches; `get` re-hashes on every read and returns `IPFSN_ERR_HASH` on single-bit corruption. |
| **CARv1 Stream & Trustless Gateway** | `ipfsn_net.c` | Byte-for-byte compatible with `ipfs dag export` / `ipfs dag import`; fetches `?format=raw` and `?format=car` from any trustless HTTP gateway or Vinea peer. |
| **Universal Block Header Framing** | `ipfsn_ubh.c` | Wraps CIDs and blocks in ZXV's 168-byte UBH envelope (`event_envelope`) for uniform transport across UDP, serial, and shared-memory rings. |
| **Dual-Visibility File Index** | `ipfsn_fidx.c` | Bidirectional `path <-> CID` index across the OS filesystem; private blocks are sealed with HKDF-SHA256 + ChaCha20-Poly1305 (`tls/aead`) before entering the store. |

### 2.1 Subtree Integrity Enforcement in `ipfsn_cat`

When `ipfsn_cat` reconstructs a file from a sequence of blocks (whether read
from disk, a `.car` archive, or an untrusted network peer), it enforces three
nested invariants before delivering bytes to the caller:

1. **Block Hash Invariant:** Every block's SHA-256 digest must equal the
   multihash embedded in its link CID.
2. **Child Blocksize Invariant:** In every interior `dag-pb` node, each entry in
   the UnixFS `blocksizes[i]` array must equal the exact byte count produced by
   recursively walking child $i$.
3. **Subtree Filesize Invariant:** The root `filesize` field must equal the sum
   of all verified leaf payloads.

A gateway or peer that truncates a stream, swaps a leaf, or lies about interior
lengths is rejected deterministically before the application layer ever sees the
corrupted payload.

---

## 3. Anatomy of the Sealed Operating-System DAG (`tools/seal.py`)

A ZXV release cut is sealed by `tools/seal.py` into a unified directory hierarchy
and exported as `seal/stack.car` (5,304 blocks, ~9.5 GB total size, root CID
`bafybeicih44n3ebecncm32ckn3sfdgbycdxnggbd4vmpwbfhxk4y4yrh3u`):

```
/ipfs/<ROOT>/
  ├── index.html          # Public introductory doorway
  ├── boot.html           # Browser Wasm mount surface
  ├── boot.js             # Parallel HTTP-206 range loader & input bridge
  ├── boot.json           # Signed manifest with pack offsets & SHA-256 digests
  ├── boot.pack           # Contiguous packed boot assets (fonts, icons, doors)
  ├── zb.wasm             # Deterministic 183,965-byte clang-23.1.0 Wasm kernel
  ├── 00_base/            # Base system manifests, upstream peer table, git bundle
  ├── disc/               # Bare-metal UEFI/BIOS raw discs, hybrid ISO, VPS qcow2
  ├── models/             # Apache-2.0 integer-quantized GGUF swarm models
  └── docs/               # SYNOPSIS, MANUAL, BUCKET, and the Monograph Curriculum
```

Because the git repository bundle and build scripts reside inside the same
Merkle-DAG as the compiled binaries, any researcher who pulls the root CID
obtains both the running operating system and the complete source tree needed to
reproduce it byte for byte.

---

## 4. Autonomous Bare-Metal Bring-Up Across Heterogeneous Clouds

For a content-addressed graph to remain "always in the room," the seed nodes
hosting it must boot and network themselves autonomously across heterogeneous
bare-metal and virtualized hardware without human SSH intervention or cloud-init
daemons:

1. **ESP-Direct BIOS & UEFI Dual Boot (`mk_golden_disc.sh`, `mk_bios_stage.sh`).**
   Every ZXV disc image carries both a UEFI `BOOTX64.EFI` loader and a legacy
   BIOS GRUB core stage in the GPT BIOS boot partition that loads `ZXV64.ELF`
   directly from the FAT32 EFI System Partition—eliminating traditional memdisk
   size ceilings.
2. **Polled Multi-Vendor NIC & Storage Drivers (`kernel/src/hwdrv/`).** At boot,
   `hw_probe_all()` scans PCI configuration space and binds whichever NIC or
   storage controller the hypervisor or motherboard presents:
   - **Networking:** `vnet` (virtio-net transitional `1af4:1000` and modern
     `1af4:1041`), `vmx3` (VMware vmxnet3 `15ad:07b0`), `e1000` (`8086:100e`),
     `igb`, `igc`, and `r8169`/`r8125`.
   - **Storage:** `nvme` (with TCG Opal SED unlock via `hw_sed.c`), `vscsi`
     (virtio-scsi), `vblk` (virtio-blk), `ahci` (SATA), and `usbmsc` (xHCI).
3. **MAC-Keyed Static Lease Fallback (`sn_link.c` + `vps_seeds.conf`).** Many
   datacenter VPS networks allocate static IPv4 routing without running a DHCP
   responder. When `sn_link_up()` observes three unanswered DHCPDISCOVER cycles,
   it queries the compiled-in seed table (`zxv_seedmap.h`) keyed by the hardware
   NIC's 48-bit MAC address, configures the node's IPv4 address, netmask, and
   gateway deterministically, and brings up the HTTP `:80` doorway and `:8724`
   UDP Vinea listener without external configuration agents.

---

## 5. Anchoring and Verifying the Curriculum Archive

To package and pin this monograph series as a standalone, fast-replicating
Content-Addressable Archive (`zxv-curriculum.car`):

```sh
# 1. Import the curriculum CAR into any Kubo / IPFS node
ipfs dag import docs/curriculum/zxv-curriculum.car

# 2. Recursively pin the root CID and announce provider records to the Kademlia DHT
ROOT_CID=$(cat docs/curriculum/CURRICULUM.CID)
ipfs pin add --recursive "$ROOT_CID"
ipfs routing provide "$ROOT_CID"

# 3. Verify any paper directly by its cryptographic path
ipfs cat "$ROOT_CID/INDEX.md"
ipfs cat "$ROOT_CID/PAPER_1_PARACONSISTENT_RING0.md"
ipfs cat "$ROOT_CID/PAPER_2_TRAFFIC_RESOURCE_ECONOMY.md"
ipfs cat "$ROOT_CID/PAPER_3_IMMUTABLE_EPISTEMIC_GRAPH.md"
```

Every citation of `$ROOT_CID` resolves identically across the eight regional
seed nodes, the Filebase edge gateway, and any researcher's local IPFS daemon—
turning every reference to the curriculum into a self-verifying node on the
epistemic graph.
