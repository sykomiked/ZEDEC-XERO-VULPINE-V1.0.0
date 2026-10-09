<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ZXV Architecture Overview, with Gaps and Improvements

Surveyed 2026-10-09 on branch `claude/project-thread-j44rs9`. Line numbers are as of that date and will drift. `kernel/Makefile` was being edited by other workers during the survey, so its line numbers are approximate.

## Summary

The repository holds two products that share some code:

1. **The ZXV OS image.** A freestanding C kernel for ARM64 (the reference target), x86-64, RISC-V and ARM32. It boots in QEMU. `build_system/Makefile.arm64` links about 165 of the 195 directories in `kernel/src`.
2. **The ZXV Swarm desktop app** (`kernel/arch/hosted/zxv_host.c`). A native macOS, Windows and Linux program. It runs 11 swarm-economy modules on the host OS and serves a local web window on `127.0.0.1:8722`. This app is how the product is meant to run as a companion to macOS, and it is the only part that fits the stated product goal today.

What is solid:

- The fail-closed host test gate, `make verify-all` (about 158 named PASS stages).
- Post-quantum crypto checked against NIST ACVP vectors (ML-KEM-768, ML-DSA-65, SLH-DSA-128s).
- TLS 1.3 pieces checked against RFC vectors.
- The integer tensor-engine parts: GGUF reader, BPE tokenizer, RoPE and E8/Leech quantisers.
- The swarm economy.
- The Kubo-compatible IPFS CID/UnixFS node.
- Freight erasure coding.
- The newer post-quantum mesh, Carracho.

Several product-goal pieces are new, untracked work in progress: calls, streaming, social, pay, cardnet, cbank, i18n, the `pq_matrix` hybrid and the IPFS directory reader.

What is missing:

- **No model runs end to end.** There is no forward pass, and the desktop app's agents are stand-ins.
- **Nothing joins the new network modules to real sockets.** Carracho, ipfs_node, call and social are pure state machines, and `arch/hosted` has no UDP, TCP or LAN-discovery glue for them. So offline, LAN and online operation does not exist yet.
- **The licences contradict each other.** The root `LICENSE` is Apache-2.0, but 736 file headers and `NOTICE` declare the OPL/SEL/Royal Writ/CC-BY-SA stack.
- **An older layer overstates what it does.** The networking, storage and finance code in `plnp`, `pungent`, `decent`, `smap`, `bootlegger`, `tripartite_fs`, `financial_fabric` and `vino` claims capabilities its code does not provide. Most of it still links into the ARM64 image.

## Layer diagram

```text
 ┌──────────────────────────────── HOST OS (macOS first; Windows, Linux) ───────────────────────────────┐
 │  ZXV-Swarm.app  (kernel/arch/hosted/zxv_host.c + zxv_ui.html, built by build_system/build_desktop.sh) │
 │    platform layer: hw scan, loopback HTTP :8722, opens a browser "app" window                           │
 │    links ONLY: swarm_{budget,emotion,market,ledger,reserve,harmonic,overlap,quality,hk,governor,dna}   │
 │    [WIP] zx_notify_host.c → osascript / notify-send                                                     │
 └───────────────▲──────────────────────────────────────────────────────────────────────────────────────┘
                 │ (no glue yet for: tensor, carracho, ipfs_node, call, stream, social, pay, cardnet)
 ┌───────────────┴────────────── PORTABLE FREESTANDING MODULES (kernel/src) ─────────────────────────────┐
 │ UI / apps     desktop, shimmer, theme, icon, font(+script), appkit, pterm, subterm, pirate_apps,        │
 │               browser, art_studio, games, emu, holodeck            [WIP] i18n                          │
 │ Social/media  social_spaces, concord, reputation, voice, audio, codec, video, display                  │
 │               [WIP] social (feed/post/store/notify), call, stream, legacy (SIP/SS7/GSM)                │
 │ Economy       swarm/*, zcapital, onepolicy, surplus, alloc, ministry, crown, battering_ram, broker,    │
 │  & finance    logistics, finance/*, vino, vino_stores, iso20022, finance_markets, count_house, abacus, │
 │               mesh_token, crypto_wallet, financial_fabric     [WIP] pay, cardnet, cbank                │
 │ AI            tensor (zt, gguf, tok, rope, lattice, coil, isf, holo), chiglet, cards/sigil/zca,        │
 │               refinery, reality, ai_layer, swarm_hk/enochian/logic   [WIP/planned] tensor/zt_model*    │
 │ Networking    net (TCP/IP, DHCP, DNS), tls, bridge (Web2/3/4), freight, ubh,                           │
 │               carracho [WIP], ipfs_node(+dir [WIP]), ipfs                                              │
 │               legacy layer: bootlegger, p2p_caracho (superseded), mesh_net, plnp, pungent, decent,     │
 │               lattice, smap, porter_house, panopticon, sovereign_node, interspace, denconnect          │
 │ Storage       zxvfs (+journal), trispace, fs, vfs, blockdev, fat32, chronicle, fusion, zxpkg,          │
 │               tripartite_fs, storage_fabric, holographic                                               │
 │ Security/PQ   mlkem (768), pqsec (ML-DSA-65, SLH-DSA-128s; [WIP] ML-KEM-1024, ML-DSA-87, SLH-256s,    │
 │               HQC-5, pq_matrix), robin_debanks (SHA-256, AES-GCM, Ed25519 verify), curzi, mage,        │
 │               immigration, zab, invproof, bombsquad, loader (ZSP, A/B), update                         │
 │ Kernel core   oseq K1, rmag K2, lpres K3, iphase K4, choice K5, phase_coord K6, crit168, e8, modbind,  │
 │               event_space, event_sched, sched, syscall, clock, cellular_multikernel, el0_userspace     │
 └───────────────▲──────────────────────────────────────────────────────────────────────────────────────┘
 ┌───────────────┴────────────── BARE-METAL ARCH (kernel/arch, kernel/boot) ────────────────────────────┐
 │ arm64 (EFI stub, GICv3, MMU, virtio blk/net/input/snd, EL0, boot evidence, entropy) — reference       │
 │ x86_64 (EFI stub, ring3, 16550) · riscv (rv64/rv32, PLIC) · arm32 · arm (legacy) · kernel/boot (x86_32)│
 │ launched by ZXV-Desktop.command → build_system/run_desktop.sh → qemu-system-aarch64 (a VM, not host)  │
 └──────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

Legend: **[WIP]** means untracked work by other workers at survey time, described from its headers.

## How to read the maturity column

| Grade | Meaning |
|---|---|
| **Real** | Does what its header says. Checked by a test against an external anchor (an RFC or FIPS vector, a reference tool's output, or a conservation identity) that runs in `verify-all`. |
| **Partial** | Real code with a tested core, but missing integration, a transport, or part of its scope. |
| **Model** | Data structures and bookkeeping that simulate a capability: plausible API, little or no real effect. |
| **Stub** | Comments say "in real implementation", returns are hard-coded, or cryptography is faked. |
| **WIP** | Untracked, being written now. Graded from headers only. |

"In verify-all" means the module's test is compiled and run by `kernel/Makefile` target `verify-all` (line 232 onward). "In ARM64 image" means it is listed in `build_system/Makefile.arm64`.

---

## 1. Boot and host integration

### What exists

| Path | What it is | Maturity |
|---|---|---|
| `kernel/arch/hosted/zxv_host.c` (680 lines) | The desktop swarm app. It scans the hardware (cores, memory and load per OS), builds the swarm, runs market and evolution cycles every 100 ms, and serves `/`, `/api/state`, `/api/ask` and `/api/quit` on loopback only (line 633). `--server` mode is meant for remote use through an `ssh -L` tunnel. | Partial |
| `kernel/arch/hosted/zxv_ui.html` + `gen_ui.py` | A single-page window (swarm, chat, machine budget, harmonic cycles, ledger and market), embedded into the binary as a C string. | Partial |
| `kernel/arch/hosted/zx_notify_host.c` | **[WIP]** Bridges the notification bus to `osascript` or `notify-send` through fork and execvp with escaped arguments. Not yet in `build_desktop.sh`'s source list. | WIP |
| `build_system/build_desktop.sh` | Uses zig 0.13 to cross-compile a universal macOS binary (lipo) packaged as a `.dmg`, plus Windows `.zip` and Linux `.tar.gz` builds. Info.plist has `LSUIElement=true` and a minimum of 10.15. Not notarised (line 147). | Partial |
| `dist/ZXV-Swarm-0.1.0-preview-*` | Built preview artefacts. Ignored by git. | — |
| `ZXV-Desktop.command` → `build_system/run_desktop.sh` | Boots the **whole ZXV OS** in `qemu-system-aarch64` and needs Homebrew QEMU. This is a VM, not a companion app. | Real (as a VM) |
| `kernel/arch/arm64/*` | EFI stub, GICv3, MMU, virtio drivers, EL0 userspace, boot evidence and entropy. The reference port. | Real |
| `kernel/arch/x86_64`, `riscv`, `arm32`, `arm` | Other ports. CI builds each one, but the QEMU run step ends in `\|\| true` (`.github/workflows/ci.yml:24` and similar lines), so a boot failure never fails CI. | Partial |
| `kernel/Makefile` default target | A **third** build path: a 32-bit x86 GRUB ISO built from 23 `COMMON_SRCS` plus x86 drivers (lines 54–108). | Legacy |

### How the Mac app is meant to start

The user double-clicks `ZXV-Swarm.app`. `zxv-swarm` scans the hardware, and `swarm_governor` caps it as a guest: at most 13/21 of **free** compute, and at least 1/8 of RAM left untouched (`swarm_governor.h` G2/G3). It then listens on `127.0.0.1:8722` and opens a window. On macOS it runs `open -na 'Google Chrome' --args --app=...` and falls back to the default browser (`zxv_host.c:155`). So the "app window" is really a browser tab, and it prefers a third-party browser.

### Connections

The host app includes the swarm headers directly and calls `swarm_*` functions unchanged. Nothing else in `kernel/src` is linked: `build_desktop.sh:62-64` lists only `budget emotion market ledger reserve harmonic overlap quality hk governor dna`. The agents are stand-ins. `answer()` replies that "the model packs aren't installed" (`zxv_host.c:388-412`).

### Tests

- `build_desktop.sh` runs the swarm tests and `smoke_test.py` before packaging.
- `verify-all` does not build or test `zxv_host.c`.
- No CI job builds the desktop app, and there is no macOS runner (`.github/workflows/ci.yml`).

---

## 2. Kernel core

| Module | Role | verify-all | ARM64 image | Maturity |
|---|---|---|---|---|
| `oseq` K1 | Causal event ordering (DAG) | yes | yes | Real |
| `rmag` K2 | Exact rational resource budgets | yes | yes | Real |
| `lpres` K3 | Paraconsistent attestation (TRUE/BOTH/FALSE) | yes | yes | Real |
| `iphase` K4 | Route contracts | yes | yes | Real |
| `choice` K5 | Deterministic selection | yes | yes | Real |
| `phase_coord` K6 | Admission and veto tokens | yes | yes | Real |
| `crit168`, `ubh`, `e8`, `modbind` | 168-bit word, UBH-168 framing, E8 geometry, electrical-coupling binder | yes (crit168 OS, ubh); `e8/test_e8.c` **not wired** | yes | Real / Partial |
| `event_space`, `event_sched`, `event_transport`, `clock` | Event-domain infrastructure, event-budget scheduler, event clock | yes (event_transport: no test) | yes | Real / Partial |
| `syscall`, `loader`, `zab`, `el0_userspace` | Syscall ABI, ELF64 and ZSP loader, capability bytecode verifier and VM, EL0 | yes (`el0_userspace/test_el0_userspace.c` **not wired**) | yes | Real |
| `cellular_multikernel`, `dual_space`, `hypercube`, `yantra`, `rur`, `rce`, `macgyver` | Tri-Space programming model pieces | yes (via `tests/host`) | yes | Partial |
| `axiom_matrix`, `telemetry` | M5 "axiom matrix" | yes | yes, and in the x86_32 COMMON_SRCS | Model. Uses `double complex` (`axiom_matrix_core.h:19`) in freestanding code. |
| `sched`, `mm`, `gdt`, `idt`, `pic`, `timer`, `pci`, `ata`, `keyboard`, `mouse`, `vbe`, `fat32` | x86_32 legacy drivers | no | no (x86 only) | Legacy |
| `*_fabric` (compute, network, storage, security, governance, identity, media, orbital, app, financial) and `abstraction_layer`, `civilizational_stack`, `app_constellation` | "Fabric" umbrella modules | no | only `financial_fabric` and `orbital_fabric` | Model, partly Stub (see Gaps) |

`kernel/include/m5_types.h:9` includes `<complex.h>`, and the core shared types carry `double complex`. Every module that pulls in this header, including `pqsec/pq_security.h`, inherits a floating-point dependency. That is why `carracho/carr_pq.h`, `social/social_sign_mldsa.h` and `cardnet/cn_pq.h` each re-declare the ML-DSA prototypes instead of including the real header.

---

## 3. Security and post-quantum crypto

| Module | Content | verify-all | Maturity |
|---|---|---|---|
| `mlkem` | ML-KEM-768 (FIPS 203), checked against NIST ACVP KATs (30 comparisons) | yes | Real |
| `pqsec` | ML-DSA-65 (vendored pq-crystals) and SLH-DSA-SHAKE-128s (vendored slhdsa-c), ACVP KATs, falsification harness | yes | Real |
| `pqsec` [WIP] | ML-KEM-1024, ML-DSA-87, SLH-DSA-256s, HQC-5 (public-domain PQClean-style code; symbols renamed through a unity build in `pq_hqc5.c`), and `pq_matrix` (an X-Wing-style combiner at three levels; MATRIX = ML-KEM-1024 + HQC-5 + X25519 with ML-DSA-87 + SLH-DSA-256s). `test_pq_matrix.c` exists but is **not wired**. | no | WIP |
| `tls` | TLS 1.3 key schedule and record layer, ChaCha20-Poly1305, X25519, HKDF and HMAC, all against RFC vectors | yes | Real |
| `robin_debanks` | SHA-256, AES-256-GCM, and Ed25519 verify (wraps `kernel/third_party/ed25519`). `test_robin_debanks.c` is **not wired**. | KATs yes | Real |
| `loader` (ZSP v1/v2, A/B update, installer), `bios` (10-key measured boot), `immigration`, `ai_layer` registry, `community_chest`, `update`, `build_system/sign_release*.sh` | Signed packages, boot chain, admission | yes | Real, but **Ed25519 only** (`zsp.h:1`, `update.h:110`) |
| `curzi` | CURZI-8889-A composite key establishment, Shamir, tiers. Three tests are **not wired**. | no | Partial |
| `mage`, `zab`, `invproof`, `bombsquad`, `porter_house`, `panopticon` | Authorization hats, capability VM, inverse witness, fault correction, port admission, watcher tracking | mostly yes (`panopticon`: no test) | Real / Partial |
| `pungent`, `plnp` | Garlic and onion routing, and the "phase-lattice network protocol" | no | Stub (see Gaps 6 and 7) |

How it connects:

- Carracho authenticates with ML-DSA-65 and derives keys from ML-KEM-768 + X25519.
- Social records are signed with ML-DSA-65.
- cardnet authorizations carry an ML-DSA-65 signature.
- The OS's own trust chain (boot, packages, updates, releases) uses Ed25519 only.

---

## 4. Storage and IPFS

| Module | Content | verify-all | Maturity |
|---|---|---|---|
| `zxvfs` | Crash-consistent filesystem with journal, fsck oracle and crash fuzzer | yes (and fuzz) | Real |
| `trispace`, `fs`, `fusion`, `zxpkg`, `chronicle` | Tri-Space triads with atomicity under crash injection, the `.zxvc/.cedez/.cedec` package, a hash-chained ledger | yes | Real |
| `ipfs_node` | Real CIDv1, UnixFS chunking identical to Kubo 0.32.1, blockstore, visibility-aware pins with AEAD for private content, OS file index, trustless-gateway fetch hook, UBH-168 wire syntax | yes | Real (no libp2p, by design: `ipfs_node.h:15-20`) |
| `ipfs_node/ipfsn_dir` [WIP] | Strict plain and HAMT directory reader. `test_ipfsn_dir.c` is **not wired**. | no | WIP |
| `ipfs` | Older "smuggler's hold": a bare SHA-256 called a "CID", self-certifying get, bridge install | yes | Real, but a different CID type from `ipfs_node` |
| `tripartite_fs` | "Holographic tripartite FS" | no | Stub: fake CID and verify-always (Gap 5) |
| `smap`, `lattice`, `decent` | Reassembly manifests, P2P chunk sharing, "IPFS/libp2p, BitTorrent v2, Matrix…" | no (lattice: root `tests/test_lattice.c`) | Model / Stub. FNV-1a stands in for a cryptographic hash. |
| `blockdev`, `vfs`, `fat32`, `holographic`, `storage_fabric` | Block layer and older FS layers | partial | Partial / Model |

The docs are good here: `docs/IPFS_NODE.md` (301 lines) has an honest-limits section.

---

## 5. Networking

| Module | Content | verify-all | ARM64 image | Maturity |
|---|---|---|---|---|
| `net` | TCP/IP: RFC 1071 checksums, ARP, ICMP, DHCP, DNS, TCP endpoint. Also older `m5route`, `dtmf`, `radio`, `jdr_piratenet`. `test_smart_adapter.c` is **not wired**. | yes | yes | Real (core) |
| `bridge` | One resolver for Web2 (DNS), Web3 (CID/DID) and Web4 (Chiglet intent). The "Web 4 agent layer" lives here. | yes | yes | Partial |
| `freight` | 168-row systematic Cauchy Reed-Solomon over GF(2^8), smart-content op stream, SHA-256 integrity | yes | **no** | Real |
| `carracho` [WIP, 28 files] | Post-quantum Kademlia DHT: ML-DSA-65 NodeIDs, S/Kademlia PoW and disjoint paths, signed records, a Noise-IK-like hybrid handshake, a sharing agreement, and a nine-capital external economy (`carr_econ.h`). Pure state machine; the host supplies the bytes. `test_carracho.c` is **not wired**. | no | no | WIP (well designed on paper) |
| `bootlegger` | Older "direct-stream P2P". 14 "In real implementation" stubs; `bootlegger_authenticate` checks a flag, not a signature (`bootlegger.c:731-739`). | no | **yes** (`Makefile.arm64:469`) | Stub |
| `p2p_caracho` | Superseded copy of bootlegger (`p2p_caracho.h:2-6`). Excluded from the ARM64 build. | no | no | Dead |
| `mesh_net`, `mesh_token`, `porter_house`, `sovereign_node`, `interspace`, `denconnect` | Mesh, settlement routing, admission, federation | yes | yes | Partial / Model |
| `plnp`, `pungent`, `decent`, `lattice`, `smap` | "Replaces TCP/IP", garlic routing, "IPFS/BitTorrent/Matrix" | no | yes | Stub (Gaps 6 and 7) |
| `wifi`, `bluetooth`, `epu`, `video` | Driver host halves (4k+ lines each) | yes (hardened) | yes | Partial (not inspected in depth) |
| `legacy` [WIP] | SIP (RFC 3261), SDP (RFC 4566), SS7/SIGTRAN M3UA+SCCP, GSM 03.38 / USSD. `test_sip.c` is **not wired**. | no | no | WIP |

Every new network module (carracho, ipfs_node, call, social gossip, stream) says the socket, NAT and clock glue is "the host layer's job (arch/hosted)" (`carr_node.h:8-10`). `arch/hosted` does not have that glue yet.

---

## 6. AI: tensor engine, tokenizer, RoPE, lattices, model, swarm

### Tensor engine (`kernel/src/tensor`)

| Part | Header | Test in verify-all | Maturity |
|---|---|---|---|
| Q16.16 integer math, Q8 blocks, golden scales, Fibonacci tiling, integer RMSnorm/softmax/SiLU, golden coils, ISF gate | `zt.h` T1–T12 | `test_zt` | Real (correctness only; "Speed is not yet measured", `docs/TENSOR_ENGINE.md:50`) |
| GGUF v2/v3 reader (F32, F16, BF16, Q8_0, Q4_0, Q4_K, Q6_K) | `zt_gguf.h` T18 | `test_zt_gguf` | Real |
| Byte-level BPE (qwen2, llama-bpe), checked on 172 texts against HF `tokenizers` | `zt_tok.h` T19 | `test_zt_tok` | Real. SentencePiece is unsupported. |
| RoPE, Q64 phase, NEOX and interleaved, Llama 3 divisors | `zt_rope.h` T20 | `test_zt_rope` | Real. No YaRN or LongRoPE. |
| E8 (1.875 bits/weight) and Leech quantisers | `zt_lattice.h` T17 | `test_zt_lattice` | Real (measured +0.3 dB over a 2-bit scalar quantiser) |
| **Forward pass / model runtime** (`zt_model*`) | announced by the owner; **not on disk** at survey time | — | Missing ("not yet joined into one forward pass", `docs/TENSOR_ENGINE.md:52`) |

### Swarm (`kernel/src/swarm`, 17 modules, 6 tests, all in verify-all)

- `swarm_budget` divides tokens per cycle by the Fibonacci rule.
- `swarm_emotion` gives each allotment a complex value: a real logic axis and an imaginary emotion axis.
- `swarm_market` keeps an 8/21 commons floor and sells the other 13/21 on a nine-capital market with cooperation and no monopoly.
- `swarm_ledger` adds a rotating witness and the triple ledger.
- The other modules: `swarm_reserve` (fractal emotion reserve), `swarm_governor` (guest compute caps), `swarm_quality` (the r × l ≥ 1.8 gate), `swarm_hk` (Hackronomicon request language), and `swarm_enochian`, `swarm_logic`, `swarm_self`, `swarm_evolve`, `swarm_enterprise`, `swarm_phase` and `swarm_dna`.
- Maturity: **Real** as an economy simulator. It is **not** joined to the tensor engine. No `#include "zt*.h"` appears anywhere outside `tensor/` except `pay/pay_util.c`.

### Other AI modules

- `chiglet` (ISF mixture-of-experts): Real.
- `cards`, `sigil` and `zca`: AI-companion loadout cards, not payment cards. Real.
- `refinery` and `reality`: Real.
- `ai_layer`: a signed model registry. Real for Ed25519, but it identifies models by a **168-bit** content hash (`ai_layer.h:51`). The repo's own IPFS doc warns against that width (`ipfs_node.h:29`).
- `voice`: a text-to-phoneme bridge that fails closed to the real models. Partial.

---

## 7. Economy and finance

| Module | Content | verify-all | Maturity |
|---|---|---|---|
| `zcapital` | Canonical nine forms: Financial, Manufactured, Intellectual, Human and System (priceable), plus the four inalienable Crown forms Social, Natural, Cultural and Spiritual (`zcapital.h:36-45`) | yes | Real |
| `onepolicy`, `surplus`, `alloc`, `edp_risk`, `predictive` | The Symbiotic Maxim, the ISF functional f(u)=ln(1+(N-1)u) in Q32.32, and allocation | yes (mostly under `-DTEST_HOST`, which swaps in `double`; see Gap 9) | Real / Partial |
| `finance` | `triple_ledger`, floating voucher, rails, derivatives, assurance, treaty tokenization, crypto bridge | indirectly (through ministry and vino_stores tests) | Partial ("Simplified Black-Scholes", `financial.c:156`) |
| `vino` | "Vino Decentralized Bank Node". Claims SWIFT, CIPS, SPFS, Visa and MasterCard compatibility (`vino.h:20-21`) | no | Model |
| `vino_stores` | VFV settlement engine: single-active-state (bearer or ledger), a ≥1.8× solvency gate, usury veto, DEBIT 846 / CREDIT 810 / EQUITY 888 | yes | Real |
| `iso20022` | Emits pacs.008 and camt.053 with a bounded writer. No XSD validation, by design. | yes | Partial |
| `pay` [WIP] | Exact three-rail nine-capital ledger (`pay_ledger.h` L1–L5), φ% tithe `floor((a+isqrt(5a²))/200)` with VFV credit for contributions above it, roles (LEI, BIC, IBAN), ISO 3166 table, vendored ISO 20022 **base** XSDs (not CBPR+), and a planned `pay_iso.c` | no | WIP |
| `cardnet` [WIP] | Dragon (880, Luhn), Phoenix (882, Damm) and Thunderbird (884, Verhoeff) charge-card networks: no interest, no APR, ML-DSA-65 per authorization | no | WIP |
| `cbank` [WIP] | ISO 4217 (SIX List One), ISO 3166, AU member list and regions, 128-bit integer helpers | no | WIP |
| `battering_ram`, `broker`, `logistics`, `ministry`, `crown`, `count_house`, `abacus`, `finance_markets`, `pirate_fleet`, `mesh_token` | Exchange, info brokerage, escrow, treasury, credentials, stash, clearing, market tracker, DAO | yes (`count_house_fractal` test not wired) | Real / Partial |
| `financial_fabric`, `crypto_wallet` | Umbrella fabric and "wallet is the OS" | no | Stub / Model (Gap 4) |

How it connects:

- The pieces that fit together: `zcapital` forms → `swarm_market` (same order) → `carr_econ` (the five priceable forms tradable, Crown forms refused) → `pay_ledger` (L5 Crown wall).
- `vino_stores` posts through `finance/triple_ledger`.
- `iso20022` and `pay` share the rail codes.
- `cbank` holds the platform ISO 4217 table, and `pay_tables.h` defers to it.
- No central-bank or PAPSS message flow exists yet. "PAPSS" appears only as a label in `finance/capital_forms.h:1-5` and `finance/assurance.h:1` (Gap 15).

---

## 8. Social and media

| Module | Content | verify-all | Maturity |
|---|---|---|---|
| `social_spaces`, `concord`, `reputation` | Free-forum spaces, ISF social matching, Pig Badge | yes | Real |
| `social` [WIP] | `social_post`: signed ZXS1 records with ML-DSA-65, Lamport clock and canonical encoding. `social_store`: ring store plus HAVE/WANT/REC gossip. `social_feed`: an **ISF ranker**, a greedy minimum surplus over the seen set, which reads no engagement signals. `social_bucket`: rateable update buckets. `zx_notify`: system notification bus with coalescing, DND and AI-task hook. `test_social_feed.c` is **not wired**. | no | WIP |
| `call` [WIP] | Call session state machine (header only; no `call_session.c` yet), mesh relay forest with simulcast, integer GCC congestion control, FEC, ICE, jitter buffer, RTP. Codecs: Opus, VP8, VP9, AV1. | no | WIP |
| `stream` [WIP] | ~1 s segments named by IPFS CIDv1, carried as freight. MDS for ids 0..255, RLNC beyond. Refers to `stream_swarm.h`, which does not exist yet (`stream_seg.h:39`). | no | WIP |
| `audio`, `codec`, `video`, `display`, `voice` | Mixer (also under ASan), Tri-Space media codec, ramfb, mode negotiation | yes | Real / Partial |

---

## 9. UI

| Surface | Status |
|---|---|
| Hosted app window (`zxv_ui.html`) | Five panels: swarm, chat, machine budget, harmonic cycles, ledger/market. Only `/api/state`, `/api/ask` and `/api/quit`. No social, pay, calls, files, model manager, settings or update UI. |
| In-OS desktop (`desktop`, `shimmer`, `theme`, `icon`, `font`, `appkit`, `pterm`, `subterm`, `pirate_apps`, `browser`) | Tested in verify-all. Runs only inside the QEMU OS image. |
| `gui/`, `init/` (repo root) | x86_32 legacy GUI, used only by the `kernel/Makefile` ISO. |
| i18n | `font/script` itemizes 29 scripts, including Ethiopic, Tifinagh, N'Ko, Vai, Adlam and Arabic. It does not do full UAX #9 bidi or Arabic shaping. **[WIP]** `kernel/src/i18n/catalog` has just been started (empty at survey time). There are no message catalogs, so no string is translated. |

---

## 10. Update path

| Piece | Status |
|---|---|
| `update` | Opt-in, content-addressed, publisher-signed updates and bundles. Ed25519 verify hook (`update.h:110`). The transport is meant to be "`decent` bitswap" (`update.h:14`), and `decent` is a stub. Tested in verify-all. |
| `loader/abupdate`, ZSP v2 | A/B slots, probation, auto-rollback, anti-rollback, key-id. Tested. Real. |
| `build_system/sign_release*.sh`, `keyceremony_root.sh`, `verify_release.sh` | Offline-root release signing. Tested (`test_release_signing.sh`). |
| `update/zx_upcheck` | Announced; **not on disk** at survey time. |
| Hosted app | No update check, no signature check of its own binary, no auto-update. |

---

## 11. Docs and tests

**Docs.**

- `docs/` holds three good, honest documents: `SWARM_ECONOMY.md` (321 lines), `IPFS_NODE.md` (301) and `TENSOR_ENGINE.md` (55).
- The top level has 19 Markdown files, including four architecture documents that overlap (`ARCHITECTURE.md`, `ARCHITECTURE_COMPLETE.md`, `ARCHITECTURE_VERIFICATION.md`, `ARCHITECTURE_WHITEPAPER.md`).
- None of the top-level files mentions carracho, ipfs_node, freight, pay, cardnet or cbank.
- `docs/CARRACHO.md` is cited by `carr_common.h:4` and `carr_econ.h:4` but does not exist. Neither does `zxv_docs/SUTRA_LANGUAGE_DIRECTIVE.md`.
- `ROADMAP.md` tracks the OS (virtio, MVP G7) and does not cover the companion app, the model runtime or payments.

**Tests.**

- `kernel/Makefile verify-all` runs about 158 stages, fail-closed.
- 165 `test_*.c` files exist under `kernel/src`. These 26 are **not** wired into any Makefile:
  - `apps/test_notes`
  - `carracho/test_carracho`
  - `count_house/test_count_house_fractal`
  - `curzi/test_curzi_{code,shamir,tier}`
  - `dharana`
  - `dharma/test_{chakra_naga,dharma,tantra,upaah}`
  - `e8/test_e8`
  - `el0_userspace`
  - `emu/test_{cpu6502,cpu_z80,emu_relate,nes,sms}`
  - `ipfs_node/test_ipfsn_dir`
  - `legacy/test_sip`
  - `net/test_smart_adapter`
  - `pqsec/test_pq_matrix`
  - `robin_debanks/test_robin_debanks`
  - `sephirot`
  - `social/test_social_feed`
  - `virtio/test_virtio_bus`
- Also unwired: root `tests/{test_apps,test_apps_stubs,test_holo,test_net_vino,test_new_modules}.c` and `tests/host/{test_ipc,test_net}.c`.
- About 80 directories have no test file at all, among them `bootlegger`, `call`, `pay`, `stream`, `financial_fabric`, `tripartite_fs`, `vino`, `finance`, `identity`, `ubh` (tested through `tests/host`), and all the `*_fabric` modules.
- Every test writes to fixed `/tmp/test_*` paths, so two concurrent runs overwrite each other.

---

## Gaps and improvements

Ordered by how much each one blocks the product goal: a signed macOS companion app that runs a real local model swarm, networks offline, on a LAN and online, and pays.

### P0: blocks shipping

**1. No model runs. The swarm's agents are stand-ins.**
- **Why it matters:** The product is "a local multi-agent AI swarm that runs real GGUF models". Today no token is generated.
- **Evidence:**
  - `docs/TENSOR_ENGINE.md:52` ("not yet joined into one forward pass").
  - `zxv_host.c:17` and `:397`: stand-in agents.
  - No `zt_model*` file in `kernel/src/tensor`.
  - `build_desktop.sh:62-64` links no tensor code.
- **Fix:**
  - Land `zt_model.{h,c}`: a Qwen2/Llama forward pass over `zt_gguf` + `zt_tok` + `zt_rope` with a KV cache, checked against llama.cpp logits on a tiny model, with tolerance stated in Q16 units.
  - Add `test_zt_model` to verify-all.
  - Link tensor into `build_desktop.sh`, mmap the GGUF in `zxv_host.c`, and route `/api/ask` through `swarm_market` allotments into `zt_model` decode steps.

**2. The licences contradict each other.**
- **Why it matters:** A shipped binary must carry one clear licence. Commercial and app-store distribution, contributor terms and NOTICE compliance all depend on it.
- **Evidence:**
  - The root `LICENSE` is Apache-2.0 (lines 1–190).
  - `kernel/src/license/license.c:1-5` and `test_license.c:18-24` assert "Apache-2.0 alone".
  - `NOTICE:7-11` says "Licensed under OPL-1.1, SEL-3.3, Royal Writ, CC-BY-SA-4.0 … See the LICENSE file".
  - 315 file headers carry the four-instrument SPDX expression, 421 carry only `Apache-2.0` while their prose names all four, and 4 carry `Apache-2.0`.
  - The verify-all banner says "four-instrument share-alike stack" but runs the Apache test (`Makefile:311`).
  - No OPL, SEL or Royal Writ text is in the repo (no `LICENSES/` directory).
  - CC-BY-SA-4.0 is not a software licence and is share-alike, which conflicts with pairing it with Apache-2.0 vendored code inside one binary. That conflict is inferred and needs counsel.
- **Fix:**
  - The owner decides the outbound licence.
  - Add a REUSE-style `LICENSES/` directory holding the full text of each instrument.
  - Make every SPDX header one canonical expression.
  - Make `license.c` and its test assert the chosen expression.
  - Run `reuse lint` in CI.

**3. The new network layer has no host glue, so offline, LAN and online modes do not exist.**
- **Why it matters:** "Works offline, on a LAN, and online" needs sockets, NAT traversal, LAN discovery and a clock and entropy source on the host. The kernel modules are pure state machines that expect `arch/hosted` to supply these.
- **Evidence:**
  - `carracho/carr_node.h:8-10` ("NAT traversal, hole punching and the UDP/stream glue are the host layer's job (arch/hosted)").
  - `ipfs_node.h:328-334`: the gateway fetch is a host callback.
  - `social_store.h` S5 and `call_session.h` both rely on callbacks.
  - `kernel/arch/hosted` contains only `zxv_host.c`, `zx_notify_host.c`, `zxv_ui.html` and `gen_ui.py`.
  - No mDNS or LAN discovery anywhere (grep for mdns/bonjour finds nothing).
- **Fix:**
  - Add `arch/hosted/zxv_net_host.c`: a non-blocking UDP socket, a Carracho bootstrap list, mDNS/DNS-SD (`_zxv._udp`) for LAN peers, and an HTTPS trustless-gateway fetcher for `ipfsn` with libc and `getentropy()` seeding the DRBGs.
  - Add three modes (offline: loopback only; LAN: mDNS only; online: bootstrap and gateway), switchable in the UI.
  - Add a two-process loopback integration test to the desktop build.

**4. `financial_fabric` lets any capital form through any rail, including the inalienable Crown forms.**
- **Why it matters:** The cooperative market and the no-alienation rule depend on rail and form checks. This function is the gate for `ff_transfer_capital`, and it always passes.
- **Evidence:**
  - `financial_fabric.c:873-880` returns `true` with the comments "In real implementation, would check …" and "Simplified".
  - Its `uint8_t rail_id` cannot hold the 888 rail named in its own comment (line 877).
  - It is called at line 868 before `ff_transfer_capital`.
  - It is linked into the ARM64 image and included by `tripartite_fs.c`, `civilizational_stack.c`, `boot_modules.c`, `sdk_bridge_lang.c` and `apps/demo_cross_lang.c`.
- **Fix:**
  - Implement a real `form → allowed rails` table derived from `finance/capital_forms.h:96-101`.
  - Widen `rail_id` to `uint16_t`.
  - Call `zcap` inalienability before any transfer.
  - Add `test_financial_fabric` that proves a Crown-form transfer is refused.
  - Better still, retire `financial_fabric` in favour of `pay_ledger`, whose L5 rule already enforces this (Gap 10).

**5. `tripartite_fs` invents CIDs and marks files verified without checking.**
- **Why it matters:** Content addressing and signatures are the trust base of the decentralized design. A filesystem that fakes both undermines every layer above it.
- **Evidence:**
  - `tripartite_fs.c:213-215` sets the CID to `file->id + i`, not a hash.
  - `:645-655` sets `file->verified = true` and `LPRES_STATE_TRUE` with no signature check.
  - `:634`, `:673` and `:691` are further "In real implementation" stubs.
  - Not in verify-all. Not in the ARM64 image, but its header is consumed by other modules.
- **Fix:**
  - Replace the fake CID with `ipfsn` CIDv1 (or `sha256`).
  - Make verify fail closed (`-ENOTSUP`) until a wallet verifier is wired.
  - Add a test that a tampered payload is rejected.
  - If nothing depends on it, delete it in favour of `trispace` and `zxvfs`.

**6. The older privacy and P2P stack is in the OS image and fakes cryptography.**
- **Why it matters:** The product promises a post-quantum decentralized network. Shipping modules that set `encrypted = true` with a key anyone can derive is worse than shipping nothing.
- **Evidence:**
  - `pungent.c:125-129` derives `session_key` from the public `dest_hash`, then sets `c->encrypted = true`.
  - `decent.c:56-63` uses FNV-1a as the "pubkey" and hash, yet `decent.h:4` claims "Zero-dependency C implementations of IPFS/libp2p, BitTorrent v2, Matrix E2EE".
  - `smap.c:57` uses "FNV-1a Hash (BLAKE3 substitute)" for its "Root CID".
  - `plnp.c:306` ("would verify incoming frame") counts frames as verified.
  - `plnp.h:4` says "Replaces TCP/IP".
  - `bootlegger.c` has 14 stubs, including `bootlegger_authenticate` at `:731-739`.
  - All of these are in `Makefile.arm64`; `bootlegger` is at line 469.
- **Fix:**
  - Remove `pungent`, `decent`, `smap`, `plnp`, `lattice` and `bootlegger` from `Makefile.arm64`, or gate them behind `ZXV_EXPERIMENTAL`.
  - Change their headers to say "model, no security".
  - Route their callers to `carracho`, `ipfs_node` and `freight`.
  - Add a CI grep that fails on `encrypted = true` without an AEAD call in the same function.

**7. There are too many overlapping networking and content-addressing modules.**
- **Why it matters:** Maintainers cannot tell which transport is canonical, and the identities do not interoperate. The same ML-DSA key yields different NodeIDs in different modules.
- **Evidence:**
  - Transports:
    - `p2p_caracho`: superseded, still on disk (`p2p_caracho.h:2-6`).
    - `bootlegger`: stub, in the image.
    - `carracho`: new, WIP.
    - `mesh_net`, `plnp`, `pungent`, `decent`, `lattice`.
  - Content addresses:
    - `ipfs`: a bare SHA-256 called a "CID" (`ipfs.h:25`).
    - `ipfs_node`: real CIDv1.
    - `smap` and `decent`: FNV.
    - `tripartite_fs`: fake.
  - Identities:
    - Carracho: `SHA3-256(pk)` (`carr_id.h:9`).
    - Social: `SHA-256(pk)` (`social_post.h:10`).
    - `ai_layer`, `community_chest`, `mesh_net`, `porter_house`: `word168_t` 168-bit IDs (`ai_layer.h:51`, `community_chest.h:49`).
- **Fix:**
  - Write `docs/NETWORK_STACK.md` declaring carracho (transport and DHT), ipfs_node (content), freight (erasure) and UBH-168 (framing) canonical.
  - Make social and call derive NodeIDs with `carr_node_id()`, SHA3-256 of the ML-DSA-65 public key.
  - Move `ipfs` onto `ipfsn` CIDs, or rename it `sha256_hold`.
  - Delete `p2p_caracho` (the "tooling cannot delete" reason in its header no longer applies).

**8. The update, boot and package trust chain is classical-only.**
- **Why it matters:** A post-quantum product whose updates, packages and boot chain trust only Ed25519 can be fully taken over by a quantum adversary, whatever the mesh uses.
- **Evidence:**
  - `loader/zsp.h:1` ("Ed25519 + SHA-256").
  - `update/update.h:110-112` (Ed25519 hook).
  - `build_system/build_signed_app.sh:16-18` (OpenSSL Ed25519 root).
  - `ai_layer` and `immigration` are Ed25519-verified.
  - Meanwhile `pqsec` ships ML-DSA-65 and SLH-DSA with ACVP KATs.
- **Fix:**
  - Add a ZSP v3 that carries hybrid signatures: Ed25519 plus ML-DSA-65 (or SLH-DSA-128s for the long-lived root), and require both.
  - Thread it through `update`, `abupdate`, `sign_release*.sh` and the hosted app's own update check (Gap 13).
  - Keep the v2 verifier for rollback only.

### P1: correctness and integrity

**9. The economy tests run on `double`, but the shipped code runs on Q32.32.**
- **Why it matters:** The solvency gate (≥1.8×), the usury veto and the ISF functional are what the money relies on. Testing them in floating point proves nothing about the integer code that ships.
- **Evidence:**
  - `surplus/surplus.h:33-50`: under `TEST_HOST`, `surplus_real_t` is `double` and `SR_LN` is `log`.
  - Almost every economy test in verify-all is built with `-DTEST_HOST`: vino_stores, ministry, battering_ram, onepolicy, zcapital, logistics, pirate_fleet and social (`kernel/Makefile` lines ~233–310).
  - Only `test_alloc_q32` (`:280`) and the "ISF axioms on the FIXED-POINT path" stage (`:576`) exercise Q32.32.
  - `SR_FROM_FLOAT` uses `double` even on the fixed-point path (`surplus.h:64`).
- **Fix:**
  - Build every economy test twice, once with and once without `TEST_HOST`, asserting the same decisions. Do the same for the swarm and tensor tests, which are already integer.
  - Replace `SR_FROM_FLOAT` with rational constants so freestanding code never needs the FPU.

**10. There are too many ledgers, rail meanings and nine-capital enumerations.**
- **Why it matters:** Payments, VFV equity and the cooperative market have to agree on what DEBIT 846, CREDIT 810 and EQUITY 888 mean and on which capital form is which. A form index exchanged between modules would currently be misread.
- **Evidence:**
  - Ledgers: `finance/triple_ledger`, `vino`, `vino_stores`, `swarm_ledger`, `carr_econ`, `pay_ledger` (WIP) and `financial_fabric`.
  - Rail meaning: `finance/capital_forms.h:44-46` names 810 "PROVENANCE" and 888 "EXTERNALITY", while `pay_ledger.h:6-14` and `iso20022.h:14-16` name them CREDIT and EQUITY.
  - Form order: `zcapital.h:37-45` and `swarm_market.h:55-63` put FINANCIAL at 0. `finance/capital_forms.h:28-38` puts SOCIAL at 0 and FINANCIAL at 4 (its comment at `:96` then calls Financial "(5)"). `vino.h:9-18` uses another naming (Material, Knowledge, Living, Built).
- **Fix:**
  - Make `zcapital.h` the single enum, with `_Static_assert` cross-checks in `capital_forms.h`, `swarm_market.h` and `pay_ledger.h`.
  - Write one rail glossary (846, 810, 888) in a new `docs/PAY_RAILS.md` and make every header cite it.
  - Converge on `pay_ledger` as the system of record, with `vino_stores` posting through it.

**11. Several modules claim compliance or capabilities the code cannot back up.**
- **Why it matters:** These claims would mislead regulators, partners and users. The README's own rule is "no hollow capabilities" (`README.md:18-20`).
- **Evidence:**
  - `vino.h:20-21`: "Cross-compatibility: ISO 20022, CAMT.053, SWIFT MT/MX, CIPS, SPFS, Visa, MasterCard, Hormung, EVC, all blockchain families, all asset classes".
  - `decent.h:4-6`.
  - `pungent.h:4-5`: "combining I2P garlic routing and Tor onion routing".
  - `plnp.h:4`.
  - `finance/capital_forms.h:1-5`: "PAPSS-Aligned" and "central-bank-acceptable", with no PAPSS message implementation.
  - `ai_layer.h:12`: "On-device inference runs within the post-quantum security boundary", with no inference.
  - The newer code is careful (`pay_tables.h` "HONEST LIMITS", `cb_ccy.h`, `iso20022.h` "OPS BOUNDARIES").
- **Fix:**
  - Rewrite these header claims to say what the code does now.
  - Add an `UNPROVEN-CLAIMS` CI grep for brand and standard names (SWIFT, Visa, MasterCard, PAPSS, CIPS, I2P, Tor, BitTorrent, libp2p) in headers that have no matching test.

**12. Test coverage holes, including the WIP modules.**
- **Why it matters:** The fail-closed gate only protects what it runs. Carracho, social feed, pq_matrix and the IPFS directory reader are product-critical and not in it.
- **Evidence:** The 26 unwired `test_*.c` files listed in section 11, 7 unwired root tests, and no tests at all for `pay`, `call`, `stream`, `bootlegger`, `vino`, `finance`, `financial_fabric`, `tripartite_fs` or any `*_fabric`.
- **Fix:**
  - Wire each WIP test as it lands, starting with `carracho`, `social_feed`, `pq_matrix`, `ipfsn_dir`, `legacy/sip`, `e8`, `robin_debanks` and `curzi`.
  - Add a verify-all preamble that fails when a `test_*.c` under `kernel/src` is not referenced, with an explicit allow-list file.
  - Use `mktemp -d` instead of fixed `/tmp` names.

### P1: shipping the Mac app

**13. There is no turnkey, signed and notarised Mac app, and no self-update.**
- **Why it matters:** The first target is macOS on Intel and Apple Silicon. An unsigned app opened by right-click, with no model download and no updates, is not a product.
- **Evidence:**
  - `build_desktop.sh:147` ("not notarised").
  - No `codesign`, `notarytool`, entitlements or Hardened Runtime anywhere in `build_system`.
  - `LSUIElement=true` (`:138`) hides the app from the Dock.
  - No macOS CI runner (`.github/workflows/ci.yml`).
  - The hosted app has no update check.
- **Fix:**
  - Add a `macos-14` CI job that runs `build_desktop.sh` then `codesign --options runtime` with entitlements (network client and server, user-selected files read-only for models), `notarytool submit --wait` and `stapler`.
  - Add a first-run model picker that downloads a licence-checked GGUF to `~/Library/Application Support/ZXV/models` and verifies it by CID.
  - Add a hybrid-signed update check (Gap 8).
  - Ship a LaunchAgent plist as an opt-in "start at login".

**14. The desktop window depends on Chrome, and its local API is open to any web page.**
- **Why it matters:** A companion app must not depend on a third-party browser. Its localhost API must also not be drivable by any website the user visits.
- **Evidence:**
  - `zxv_host.c:155` (`open -na 'Google Chrome' --args --app=…`).
  - `handle()` at `:541-578` checks neither `Host` nor `Origin` and has no token. A cross-site `fetch('http://127.0.0.1:8722/api/quit', {method:'POST'})` is a CORS "simple request", so it executes and quits the app. Through DNS rebinding a page could also read `/api/state`. This is inferred from the code, not exploited.
- **Fix:**
  - Use a native WKWebView shell on macOS (a small Objective-C or Swift wrapper) and WebView2 on Windows, falling back to the default browser.
  - Generate a random per-launch token passed in the URL fragment and required in a header.
  - Reject requests whose `Host` is not `127.0.0.1:port` or whose `Origin` does not match.

**15. Third-party product names and a misused acronym in module names.**
- **Why it matters:** Trademark and confusion risk for a commercial app.
- **Evidence:**
  - `kernel/src/carracho/` (28 files, CARR_* symbols, "carracho/v1" context strings in signatures) reuses the name of the 1990s–2000s Mac file-sharing client Carracho. That is the very reason `p2p_caracho` was renamed (`p2p_caracho.h:2-3`: "no reference to any third-party product name").
  - `cardnet.h:1` names a network "Thunderbird" (a Mozilla trademark).
  - `finance/*` uses "PAPSS", the name of the Afreximbank/AfCFTA Pan-African Payment and Settlement System, for an unrelated "Pay-It-Forward Assurance Protocol" (`assurance.h:1`). That will collide when the real PAPSS integration lands.
  - `emu/test_nes.c` and `test_sms.c` (console trademarks; lower risk).
- **Fix:**
  - Rename `carracho` before its wire format freezes, because the context strings are part of every signature. A ZXV-original name keeps the bootlegger convention.
  - Rename Thunderbird.
  - Rename the finance "PAPSS" to, say, "PIFA", and keep "PAPSS" for the real settlement adapter.

### P2: quality and maintainability

**16. Floating point and libc in freestanding core code.**
- **Evidence:**
  - `kernel/include/m5_types.h:9` includes `<complex.h>`, and lines 29–47 hold `double complex` fields.
  - `axiom_matrix_core.c` is in the freestanding `COMMON_SRCS` (`kernel/Makefile:60`).
  - `bootlegger.c:118-123` and `p2p_caracho.c:124` use `double` and `fs_cos`.
  - `[WIP] pq_hqc5.c:29-30` includes `<stdio.h>` and `<string.h>`.
  - Non-comment `float`/`double` use also appears in `audiogenomics_pro` (221 lines), `epu` (155), `audio` (46), `synthesis` (35) and `clock` (24).
- **Fix:**
  - Split `m5_types.h` into an integer core and a hosted-only `m5_complex.h`, so `pq_security.h` can be included freestanding and the three re-declared ML-DSA prototype copies (`carr_pq.h`, `social_sign_mldsa.h`, `cn_pq.h`) can go.
  - Add `-mgeneral-regs-only` (aarch64) or `-mno-sse` builds of the security-critical modules to CI.

**17. The vendored-code and data licences are not recorded centrally, and binaries are committed.**
- **Evidence:**
  - `NOTICE:106-116` lists no specific third-party components. The components are: `kernel/third_party/ed25519` (zlib-style, Orson Peters), `pqsec/mldsa` (CC0 / Apache-2.0), `pqsec/slhdsa` (ISC), `pqsec/mlkem1024` (CC0 / Apache-2.0), `pqsec/hqc5` (public domain), ISO 20022 XSDs (from moov-io Go modules, `pay/xsd/PROVENANCE.txt`), SIX ISO 4217 List One (`cbank/data/list-one.xml`), Debian iso-codes, and the HF and gguf-py test fixtures.
  - `cb_ccy.h` cites `data/PROVENANCE.txt`, which is missing.
  - `kernel/third_party/ed25519/ed25519_{32,64}.dll` and a root `a.out` are tracked by git.
  - There is no model-licence policy. Llama 3 weights are under the Llama Community Licence (acceptable-use policy and a 700M-MAU clause). Some Qwen2.5 sizes are under the Qwen licence rather than Apache-2.0. The tokenizer explicitly targets both families (`zt_tok.h`).
- **Fix:**
  - Add a `THIRD_PARTY.md` (or SPDX SBOM) listing every vendored component, its licence and origin hash.
  - Add `cbank/data/PROVENANCE.txt`.
  - Remove the `.dll` files and `a.out`.
  - Add a model allow-list (`docs/MODELS.md`) that permits only weights licensed for commercial redistribution and use, for example Apache-2.0 Qwen2.5 sizes, and checks them by CID in the model picker.

**18. Built-in UI wiring is missing for most product features.**
- **Evidence:** `zxv_ui.html` calls only `/api/state`, `/api/ask` and `/api/quit`. There are no endpoints for social feed, pay or VFV, calls, streams, files/IPFS, peers or mode (offline, LAN, online), models, settings or notifications.
- **Fix:** Define a versioned local API (`/api/v1/...`) in `docs/HOST_API.md`. Add the panels one at a time as each kernel module gains host glue: the notification bus first (already WIP), then social, then pay.

**19. i18n is script-level only.**
- **Why it matters:** The product promises African Union language support. Today no user-visible string can be translated, and Arabic and other cursive or complex scripts will render without joining or shaping.
- **Evidence:** `font/script.h:28-31` (no UAX #9, no shaping). `kernel/src/i18n/catalog` is empty. All UI strings are English literals in `zxv_ui.html` and `zxv_host.c`.
- **Fix:**
  - Add a message-catalog format keyed by stable IDs, with locale fallback.
  - Start with the AU working languages (Arabic, English, French, Portuguese, Spanish, Kiswahili) plus Amharic (Ethiopic) and Tamazight (Tifinagh).
  - Implement UAX #9 and Arabic joining, or document reliance on the host's text engine for the hosted UI.

**20. The docs are fragmented and stale.**
- **Evidence:**
  - Four overlapping top-level ARCHITECTURE documents.
  - Missing `docs/CARRACHO.md`, cited by `carr_common.h:4` and `carr_econ.h:4`.
  - `ROADMAP.md` does not cover the companion app, the model runtime, payments or the network glue.
  - `README.md:28` sends readers to `LICENSE` (Apache) while the header above it declares OPL.
- **Fix:**
  - Fold the four architecture files into this overview plus `ARCHITECTURE.md` (kernel internals), and move the rest to `docs/archive/`.
  - Write `docs/CARRACHO.md`, `docs/PAY_RAILS.md`, `docs/NETWORK_STACK.md`, `docs/HOST_API.md` and `docs/MODELS.md`.
  - Add a product track to `ROADMAP.md`: model, glue, Mac signing, pay, social, calls.

**21. CI is weaker than the local gate.**
- **Evidence:** QEMU boots end in `|| true` (`.github/workflows/ci.yml:24, 45, 66, 89, 110`). No desktop build, no fuzz job, no `verify-experimental`. CI triggers only on `main` and `develop`.
- **Fix:**
  - Make the boot step grep the serial log for the boot-complete banner and fail without it.
  - Add jobs for `build_desktop.sh` (Linux and macOS), `make fuzz` (nightly) and `verify-experimental`.

**22. Three kernel build paths drift apart.**
- **Evidence:**
  - `kernel/Makefile` builds an x86_32 GRUB ISO from 23 modules (lines 54–108). Its `ARCH ?= x86_32` default conflicts with the README's "reference target is ARM64".
  - `build_system/Makefile.{arm64,x86_64,riscv,...}` are the real image builds.
  - `build_desktop.sh` builds the hosted app with its own source list.
- **Fix:**
  - Retire or rename the `kernel/Makefile` ISO target (`legacy-iso`) so `kernel/Makefile` is the test harness only.
  - Generate the hosted app's source list from one manifest shared with the tests, so a swarm module added to tests cannot be left out of the app. Six swarm modules (`enochian`, `logic`, `self`, `evolve`, `enterprise`, `phase`) are tested but not in the app today.

---

### Appendix: directories not referenced by any Makefile

`kernel/Makefile` and `build_system/Makefile.arm64` together never reference:

- `abstraction_layer`, `app_constellation`, `app_fabric`
- `call`, `carracho`, `cardnet`, `cbank`, `legacy`, `pay`, `social`, `stream`, `i18n` (WIP)
- `civilizational_stack`, `compute_fabric`, `governance_fabric`, `identity_fabric`, `media_fabric`, `network_fabric`, `security_fabric`, `storage_fabric`
- `tripartite_fs`

`swarm`, `tensor`, `ipfs_node` and `freight` are tested by verify-all but not linked into any OS image. Only `swarm` is linked into the desktop app.
