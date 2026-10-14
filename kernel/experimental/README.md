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
| `chiglet_triage/` | `chiglet_triage.c`, a host tool that groups crash reports by the cosine of their Chiglet vectors, with bug-identity dimensions weighted above ROM context. Moved from `kernel/src/chiglet` (B3 in `docs/GAP_AUDIT.md`). | In no image, app or test, and its own documented build fails under `-std=c11` (`strtok_r` needs a POSIX feature macro); not built. | `kernel/src/chiglet` (`chg_interaction`, tested by `test_chiglet`) |
| `civilizational_stack/` | Deployment, cross-language translation and self-audit of built-in apps. Its header's "military-grade reliability" claim is unsupported. | Does not compile; not built. | `kernel/src/orbital_compat` |
| `compute_fabric/` | Manager for compute cells: create, start, stop, migrate between domains. | Does not compile; not built. | `kernel/src/sched`, `kernel/src/hypercube` |
| `governance_fabric/` | Umbrella over legal_engine, concord, ministry, crown and ZAB: proposals, votes, policies, contracts. Its "ZK-ABFT consensus" claim has nothing behind it. | Does not compile (unknown `zab_state_t`, `concord_state_t`, `crown_state_t`); not built. | `kernel/src/concord`, `kernel/src/crown`, `kernel/src/onepolicy` |
| `identity_fabric/` | Umbrella over identity, Vino/Vena, ZAB, reputation, concord, crown and ministry. | Does not compile; not built. | `kernel/src/ident`, `kernel/src/reputation` |
| `m5_apps/` | Six sources from `kernel/src/apps` (B3): four standalone M5 programs with their own `main()` (`wallet_app.c`, `derivatives_app.c`, `assurance_app.c`, `treaty_app.c`) over `m5_api.h`; `clock_app.c` and `dharma_monitor.c`. | In no image, app or test. The M5 programs call `m5_*` functions that no file defines (`kernel/src/sdk/SDK_README.md`); the other two use types `apps.h` does not declare (`clock_app_t`, `dharma_monitor_app_t`, as `build_system/Makefile.arm64` records); not built. | `kernel/src/apps/apps.c` (the built-in apps, in the images), `kernel/src/sdk/app_template.c` (compile-checked by `make -C kernel verify-all`) |
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

`chiglet_triage/` and `m5_apps/` came later (2026-10, roadmap step 2.5, rows A5 and B3 of
`docs/GAP_AUDIT.md`), checked the same way: `grep` over every Makefile, build script, `.mk` file,
CI workflow and test for each file name and for any pattern that could match it found none. No
include path in them needed changing. That grep missed `kernel/Makefile`'s `test-orphans`,
which `verify-all` runs: it builds `notes_app.c` with `test_notes.c` (through
`tests/notes_app_shim.h`) and builds `vfs_stub.c` into `test_apps`, the test of the built-in apps.
Those three were moved here and then back to `kernel/src/apps`, where the tests find them. The rest of those two rows was not moved: every A5 module
is compiled by a `make -C kernel verify-all` test (several also by an image or by zxvd), and the
remaining B3 files are in an image (`render_boot.c`, `arch_globals.c`, `blockdev/ramdisk.c`),
`#include`d by built code (`pq_hqc5_gf2x.c`), run by `verify-all` (the seven `mlkem/*_validate.c`,
and now `sdk/app_template.c`), or named by a build script (`sdk_bridge/polyglot_matrix.c`, whose
header `apps/demo_cross_lang.c` in the arm64 image includes).
