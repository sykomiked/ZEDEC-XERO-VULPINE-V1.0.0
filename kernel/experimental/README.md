# kernel/experimental: unbuilt placeholders

Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC. SPDX-License-Identifier: Apache-2.0.

The directories here are **not built, not tested and not shipped**. No Makefile, build script,
test or other source file in the tree compiles or includes them, and most of them do not compile
at all (they include headers that do not exist, or use types that were never defined). They were
moved here from `kernel/src/` so that the kernel source tree only holds code that is built, and are
kept for the design record: they show what the "fabric" umbrella layer was meant to be.

Nothing in these files is a working capability. Do not wire any of them into a build. If one of
these ideas is wanted, rebuild it on the tested modules named in its row below, with tests, under
`kernel/src/`.

How this was checked (2026-10): for each module, `grep -r <name>` over the whole repository
(excluding Markdown) found no reference outside the moved directories themselves, except a comment
in `build_system/Makefile.arm64` that documents why `p2p_caracho` is excluded, and unrelated uses of
the type name `security_fabric_t` in `subsystems/security/` (a different module). The status of each
one is in [docs/SYSTEM_REFERENCE.md](../../docs/SYSTEM_REFERENCE.md).

| Directory | What it was meant to be | Why it is here | Real code to use instead |
|---|---|---|---|
| `abstraction_layer/` | A registry that would register language runtimes (Fortran, COBOL, Sutra, asm, Rust, ...) and kernel modules and initialise them in order (`boot_modules.c`). | Does not compile (missing types and headers); not built. | `kernel/src/orbital_compat` (value IR and language adapters) |
| `app_constellation/` | Header-only API for deploying, rolling back, pausing and resuming services across nodes. Listed as STUB, not DEAD, but it has no `.c` and includes only the dead fabric headers below, so it moved with them. | No implementation; not built. | none yet |
| `app_fabric/` | Registry of application descriptors and running app instances over the SDK. | Does not compile; not built. | `kernel/src/sdk` (declarations only), `kernel/src/apps` |
| `civilizational_stack/` | Deployment, cross-language translation and self-audit of built-in apps. Its header's "military-grade reliability" claim is unsupported. | Does not compile; not built. | `kernel/src/orbital_compat` |
| `compute_fabric/` | Manager for compute cells: create, start, stop, migrate between domains. | Does not compile; not built. | `kernel/src/sched`, `kernel/src/hypercube` |
| `governance_fabric/` | Umbrella over legal_engine, concord, ministry, crown and ZAB: proposals, votes, policies, contracts. Its "ZK-ABFT consensus" claim has nothing behind it. | Does not compile (unknown `zab_state_t`, `concord_state_t`, `crown_state_t`); not built. | `kernel/src/concord`, `kernel/src/crown`, `kernel/src/onepolicy` |
| `identity_fabric/` | Umbrella over identity, Vino/Vena, ZAB, reputation, concord, crown and ministry. | Does not compile; not built. | `kernel/src/ident`, `kernel/src/reputation` |
| `media_fabric/` | Umbrella over audio, video, codec, art and games. | Does not compile (`codec.h` not found); not built. | `kernel/src/codec`, `kernel/src/stream` |
| `network_fabric/` | Umbrella over mesh, radio, Bluetooth, Wi-Fi, LoRa and event transport. Its "post-quantum security" claim is unbacked. | Does not compile (unknown adapter types); not built. | `kernel/src/mesh_net`, `kernel/src/vinea`, `kernel/src/ehop` |
| `p2p_caracho/` | An older P2P transport. It compiles, but its own header says it is superseded by `kernel/src/bootlegger` and must not be built. | Superseded; excluded from every build. | `kernel/src/bootlegger` |
| `security_fabric/` | Signing, verification and encryption services for the app fabric. Every crypto routine is a placeholder; the audit made verification fail closed and added a banner. | Does not compile (missing headers, `return -1.` syntax errors); not built. | `kernel/src/pqsec`, `kernel/src/mlkem`, `kernel/src/tls`, `kernel/src/robin_debanks` |
| `storage_fabric/` | NVMe and network storage for apps, with replication revenue accounting (placeholder numbers). | Does not compile (`nvme.h` and others missing); not built. | `kernel/src/zxvfs`, `kernel/src/ipfs_node` |

Two include paths inside these files were adjusted when they moved (`identity_fabric.c` and
`p2p_caracho.c` used `../` paths relative to `kernel/src/`). That keeps the record accurate; it does
not make them compile.

`gematria` was also listed as UNUSED/DEAD but is compiled into the kernel images, so it was not
moved.
