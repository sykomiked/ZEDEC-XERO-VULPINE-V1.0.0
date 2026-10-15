<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ZXV: a synopsis of the whole system

*ZEDEC XERO VULPINE — one freestanding C codebase for a complete personal
operating system: kernel, storage, network, security, interface,
applications, languages, local intelligence and an economy of its own.*

## Abstract

ZXV is an operating system written from scratch in freestanding C. It is
designed as a single, self-contained platform: a deterministic kernel, a
crash-consistent file system, a network stack, post-quantum cryptography, a
native vector interface, hundreds of layered applications, five programming-
language pillars, a local swarm of small AI models, and an exact internal
economy — all governed by four-valued paraconsistent logic. The same source
produces a bare-metal disc image for x86-64 and ARM64, a hybrid BIOS+UEFI
installer, a qcow2 machine image, a WebAssembly boot that runs in a browser
tab, a hosted desktop application, and a library with a frozen C ABI and SDKs
for twelve languages. Its release is itself a content-addressed artifact: the
whole system is sealed into an IPFS DAG whose root CID is both its address and
its integrity proof, and a fleet of seed nodes distributes that DAG while
earning metered, verifiable credit in the system's own voucher unit, `$YHVH`.
Nothing in the system requires a cloud service to function.

## 1. Purpose

The premise is sovereignty at the personal scale. A conventional personal
computer depends on vendors, clouds and registries at every layer. ZXV asks
what a machine would look like if none of those dependencies were required:
if its intelligence ran locally, its ledger balanced locally, its files were
addressed by content rather than by location, and its network was a mesh of
peers rather than a hierarchy of services.

The answer is not a minimal system. It is a *complete* one — the wager being
that a single disciplined codebase, small enough for one person to
understand in principle, can still provide the full breadth of a modern
operating environment.

## 2. Design rules

Five rules govern the codebase and are enforced by its tooling rather than
by convention:

- **Integer-only.** No floating point in the kernel or in any economic
  value. Token denominations, settlement and measurement use exact integer
  arithmetic. The same input produces the same bits on every supported CPU.
- **Paraconsistent.** Contradictions are represented (`lpres`: TRUE, FALSE,
  BOTH, NEITHER), routed and resolved — never silently treated as true, and
  never allowed to halt a decision process.
- **Bounded.** Real-time and cross-process paths use preallocated rings with
  explicit backpressure, not unbounded allocation.
- **Layered.** Every module belongs to a stratum (S0–S8); an include may not
  reach upward. `build_system/verify_layers.sh` enforces it mechanically.
- **Verifiable.** A capability is claimed only where code and a test back
  it. `make -C kernel verify-all` runs the full host gate;
  `build_system/verify_boot.sh` rebuilds the disc from source, boots it in
  QEMU and checks every boot marker.

## 3. The architecture in layers

| Layer | Contents |
|---|---|
| Applications | 407 apps in three tiers — 144 base modules (L1), 127 built from them (L2), 136 built on those (L3). Any app may be a block inside another; users compose apps without writing C. |
| Interface | `zui`, a native vector toolkit (ZXVG format) that never imitates a browser; the ASCW Golden font family; an astrolabe dock; five workspaces (Home, Office, Team, Cloud, Park); the Banker file manager over the Hoard. |
| Languages | Five pillars — COBOL with Sutra, Fortran with Trebago, Ada with Eve, APL/K with KRON, MUMPS with MYRA — plus a shared IR for legacy languages, each with an AI twin; spoken languages are treated on par with programming languages. |
| Intelligence | A swarm of small models on the integer tensor engine (`zt`), scheduled by a tokens-per-cycle budget with Fibonacci scaling and an emotional economy; GGUF loading; native Q4_K/Q6_K kernels (AVX2, AVX-512, NEON); a Metal GPU backend. |
| Logic and audit | Four-valued `lpres` with a 64-state tag on decisions, a paradox engine, a paraconsistent ledger, peer audit of every posting, a policy engine. |
| Economy | An exact, interest-free ledger (assets = liabilities + equity each cycle), an assurance fee, escrow markets, V-Bills, the Sicilian Crown Allotment and Player Cards. |
| Network | Vinea peer-to-peer nodes over post-quantum sessions; Web4 twins of classic protocols; the AIRC chat layer; IPFS content addressing. |
| Security | ML-KEM, ML-DSA, SLH-DSA and HQC from vendored reference code, checked against NIST vectors; capability-checked syscalls; signed packages and updates. |
| Kernel | Phase-tick scheduling; zxvfs with journal and extents; floating point banned at compile time; builds for arm64, x86_64, riscv64, riscv32 and arm32. |
| Hardware | An open emulation standard (ZHP-1 profiles, signed reconfiguration with rollback) and class drivers, including virtio over PCI on x86-64 (SCSI, network, block) and virtio-mmio on ARM64. |

## 4. The surfaces

One source, five ways in:

- **Browser.** The boot is compiled to WebAssembly and sealed into the
  release root. A doorway page offers the launch; the same vault doors,
  launch screen and login run in the tab, streaming every system file by its
  CID and checking each byte against SHA-256 before the boot proceeds.
- **Bare metal.** A UEFI disc image for x86-64 and ARM64; on x86-64 a ring-3
  userspace, PCI enumeration, and virtio block, SCSI and network devices.
- **Installer.** A hybrid ISO that boots under legacy BIOS (GRUB/El Torito)
  and UEFI (an appended ESP and El Torito FAT image), suitable for CD, USB
  or a hypervisor's optical drive.
- **Virtual machine and VPS.** A qcow2 image — including a single-drive
  variant with the swarm partition grafted on — for QEMU and for cloud
  providers that accept a custom image.
- **Library.** `libzxv`, the engine behind a frozen C ABI, with SDKs for C,
  C++, Java, .NET, Python, TypeScript, Go, Rust, Ruby, PHP, Swift and
  Solidity, and a Z-Wasm plugin sandbox.

## 5. The network

Nodes are Vinea peers speaking over post-quantum sessions on UDP. Each node
derives a distinct identity from its machine seed. The peer table is seeded
from the release's `UPSTREAM.CID`; contacts are authenticated and tracked by
transport address. IPv4 fragmentation and reassembly carry Vinea datagrams
larger than a single MTU. Above transport, the node keeps a routing table, a
usage meter per peer, an agreement layer with quotas and retries, and a
trade link that settles receipts between parties.

Web4 names the system's twins of the classic protocols — the same jobs,
done the ZXV way — and AIRC carries chat between nodes.

## 6. The economy

The ledger is exact and interest-free: assets equal liabilities plus equity
at every cycle boundary. Contribution is what the economy pays for, and it
is measured, not asserted — a node's usage table records the records and
chunks it verifiably served each peer, a settle cycle distributes the
owner-funded pool contribution-weighted, and receipts are checked before
they count.

What a node earns — its settled incoming value in the financial form — is
bridged into the voucher ledger under the ticker `$YHVH` and issued to card
accounts in the Player Card system. For the bootstrap generation, the
infrastructure's own earnings split evenly between Black Swan Club cards
#0 and #1; every other participant earns by the same open mechanism. The
bridge retains unposted balances and retries until the ledger accepts them,
so the book and the chain cannot drift apart.

## 7. The release as content

A ZXV release is not a file but a DAG. `tools/seal.py` composes the packed
matrix, the vector plane, the source as a git bundle, the disc images, the
model weights, the browser boot, the developer documents, the installers
and the manifest into one UnixFS tree, and writes it out three ways: the
blocks, a `stack.car`, and a plain file tree. Every file sits at its own
CID; directories sit at theirs; the root's CID names the whole. Fetching
the root through any gateway or node yields exactly the sealed bytes — the
address is the checksum, and the manifest lists every path with its CID and
SHA-256 for independent verification.

Because the whole repository travels as a git bundle inside the DAG, the
system is clonable from the network it describes: `git clone` the bundle and
the full history, toolchain instructions and documentation arrive with it.

## 8. The bootstrap fleet

A new mesh needs its first faithful peers. Five servers run the ZXV image
natively, each with its own derived identity, each seeded with the others'
transport addresses. Their task is to jumpstart the network: they serve the
release DAG (every shard and every intermediate CID pinned and provided),
they accept and stabilize peer connections, and they meter and report the
service they perform. Their contribution settles like anyone else's, through
the same open mechanism — the seeds are participants, not privileged
processes — and this generation's earnings route to the two card accounts in
the established split.

## 9. Verification

Quality is gated, not asserted:

- `make -C kernel verify-all` — the host test gate, hundreds of named stages.
- `build_system/verify_layers.sh`, the license, workflow-tier, dependency
  and mutator-config checks, and a pinned clang-format pass on changed lines.
- `tools/seal.py` runs twice per cut; the two `stack.car` files must be
  byte-identical before the cut is accepted.
- `tools/qc.py` audits the sealed root: reachability of every block, the
  boot manifest's hashes against the tree, CID-named files against their
  content, and the doorway's links against the DAG they point into.
- Boot matrix: BIOS and UEFI, virtio-scsi and virtio-net, the grafted
  one-drive VPS image, and the ISO booted as a CD — each required to print
  the system's boot markers.

## 10. Honest limits

The system is early, and says so where it counts. It boots under emulation
and in virtual machines; it has not yet run on physical boards. The hosted
application answers with a real model end to end; the bare-metal surfaces
are younger. The application catalogue is deliberately layered and still
building out. Where a capability is partial, the tree, the tests and the
documentation say so — the project's first design rule is that nothing
claims what the code does not do.

## 11. Where to read next

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — the module map.
- [`SYSTEM_REFERENCE.md`](SYSTEM_REFERENCE.md) — every module, briefly.
- [`VINEA.md`](VINEA.md), [`WEB4.md`](WEB4.md) — the peer layer and its protocol twins.
- [`BUCKET.md`](BUCKET.md) — how the release DAG is laid out.
- [`academic/`](academic/README.md) — the field-by-field papers: mathematics,
  paraconsistent logic, testing methodology, economics, systems.
- [`supplemental/CLAIMS_AND_EVIDENCE.md`](supplemental/CLAIMS_AND_EVIDENCE.md) —
  each claim, and the evidence for it.

---

*ZXV is created by Michael Laurence Curzi and 36N9 GENETICS, LLC, and
licensed Apache-2.0. The ready-to-run system is licensed to end users and
supported by SCENIC BYPASS CONSULTING, LLC.*
