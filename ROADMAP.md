# ZXV / ZEDEC pqOS — Build Roadmap

Working engineering plan. Honest maturity labels, real dependencies, one canonical sequence.
Authoritative alongside `CANONICAL.md`. Last updated 2026-08-05.

Legend: **[done]** built + tested + in the gate · **[partial]** exists, incomplete · **[planned]** designed, not built · **[research]** speculative, evidence-gated.

---

## 0. Where we are (the honest baseline)

- ARM64 freestanding kernel builds **zero-warning**, boots QEMU **100/100 BOOT_OK**.
- `verify-all`: **715 host assertions, 0 failures**. Git baseline on `main`, secrets excluded.
- Vigorous red-team gate **passed**: 19 confirmed findings fixed with frozen regressions; memory-safety fixes ASan/UBSan-clean.
- **Built this program** (all tested, freestanding): surplus/ISF, Chiglet, cards/ZCA/sigil, Magitech Refinery, Concord (social matching), Wyrmgate (six-fold judgment), WyvernEye (intent→judged), Chronicle (durable tamper-evident ledger), Den Connect (sovereign server + fleet + accountability), rational/abacus/Lightning Rod, zBIOS, ZXVFS, ELF/ZSP loader + A/B, Z-order locality.
- **Governing external assessment**: Proposal-9 audit rates the package **5.7/10** ("largest credible advance in the series; advanced pre-alpha with an emulated vertical slice; not yet a defensible MVP"). Its gate schema (G0–G12) is the definition of "done".

---

## 1. The tracks (everything left)

### Track A — Security & trust P0s  *(the gate to any partner)*
The red-team pass is done; these are the audit's structural P0s that remain.
- **[partial]** A1 · EL0 user-copy boundary — SYS_RECV writable check **[done]**; still owe `copy_to_user`, SYS_SEND bounce-buffer, SYS_OPEN/CLOSE handlers or ENOSYS, fault-safe recovery, syscall fuzzing. *(task 18)*
- **[planned]** A2 · ZSP v2 signed packages — bind identity/version/arch/ABI/key-id/caps/expiry + monotonic anti-rollback; reject raw `.elf` in release profile. *(task 17)*
- **[planned]** A3 · Signed, path-safe installer + **system-level** A/B (bootloader/kernel/recovery), power-loss durable at every transition. *(task 19)*
- **[planned]** A4 · Real protected **cell** backend (process/VM boundary), signed attestation not digest-equality, real crit168 digest (retire the XOR placeholder). *(task 20)*
- **[planned]** A5 · Entropy/key lifecycle — continuous RNG health, DRBG, fail-closed without entropy, nonce-uniqueness across reboot, offline root ceremony + revocation.
- **[planned]** A6 · Flip build to `-Werror`; keep zero-warning as a hard gate.

### Track B — Networking & drivers  *(the keystone — unblocks everything social)*
Current ARM64 drivers: GICv3, generic timer, PL011 UART, virtio-blk, FEAT_RNG.
- **[in progress]** B1 · **virtio-net** — the next build. RX/TX virtqueues, MAC, poll-driven, host-tested framing.
- **[planned]** B2 · Network stack — ARP/IP/ICMP/UDP/TCP, DHCP, DNS, then TLS 1.3 client (reuse audited crypto). Loss/reorder/MTU tests; interoperate with reference servers. *(audit NET_001)*
- **[planned]** B3 · Framebuffer + CPU compositor (later GPU) — for the desktop, UI, and the Holodeck.
- **[planned]** B4 · Input stack — keyboard, mouse, touch, focus, clipboard, accessibility.
- **[planned]** B5 · Real-silicon drivers (hardware phase) — NVMe/eMMC, USB, Wi-Fi/BT, audio, with IOMMU/DMA containment.

### Track C — Application layer  *(user-facing value; bundle modules via the modular compiler)*
Composition pattern proven (Wyrmgate/WyvernEye/Chronicle). Each app = module `.c` files compiled together.
- **[partial]** C1 · **Den Connect node** — governance **[done]**; wire posts through Wyrmgate + record to Chronicle; Concord introductions; needs B1/B2 transport. *(tasks 26/27)*
- **[planned]** C2 · **The Ledger** — abacus + rational + wyrmgate + chronicle: exact money where each transaction is judged for conservation and durably recorded.
- **[planned]** C3 · **The Archivist** — zxvfs + chronicle + recovery: tamper-evident personal storage that self-repairs.
- **[planned]** C4 · **The Companion** — chiglet + concord + cards: the AI that grows across a person's devices.
- **[planned]** C5 · **The Holodeck** — native VLC-inspired media pipeline + Streamlabs-Pro-style live production/editing + synchronized watch parties; content-addressed distributed delivery. Codecs/GPU are the gated parts; the **watch-party sync clock is buildable now** on the event model. *(task 28)*
- **[planned]** C6 · **Recovery / Disrepair utility** — Disk-Utility-class: fsck for ZXVFS, boot attestation, A/B health, **trash bin + storage manager**, subsystem health dashboard. *(task 21)*
- **[planned]** C7 · **Doctor Quacksworth** — lightweight native security suite: verify signatures/caps instead of CPU-hogging scanning; web-threat protection; sandbox. *(task 24)*
- **[planned]** C8 · **Portable identity** — one keypair binding a person's Chiglet, cards, Concord profile, and den accounts across devices. *(fundamental, currently missing)*
- **[planned]** C9 · Consumer shell — desktop session, service manager, Files/Notes/Settings/Update UI, notifications; P-TERM stays first-class. Hypercube = an effects layer over accessible 2D, never a replacement.

### Track D — Distributed fabric  *(the "indestructible" networking layer)*
- **[planned]** D1 · Content-addressed store — chunks by hash (reuse crit168/SHA + zxvfs); every device a node. *(task 26)*
- **[planned]** D2 · Access tiers per object — PUBLIC / PRIVATE / SELECT (capability + Ed25519), matching den visibility.
- **[planned]** D3 · Mass networking — IPFS-style DHT/discovery + classic P2P; NAT traversal; delay-tolerant (store-and-forward) for degraded conditions.
- **[planned]** D4 · **P2P email** — distributed, no central servers, DTN-style; works offline/delayed.

### Track E — Compatibility & emulation  *(large; honest limits)*
- **[planned]** E1 · Universal binary layer — PE/Mach-O/ELF loaders + **syscall-translation personalities** (same-ISA, Wine/WSL1-style). Foreign-ISA needs JIT/AOT translation, **not** full-machine emulation; measure coverage per program. *(task 22)* Also ASCW 3D-framework native compat without exposing the frameworks.
- **[planned]** E2 · On-demand partial emulation — library-OS: provide only the syscalls/ABIs a program touches; JIT only foreign-ISA paths actually executed. Console profiles. *(task 25)*
- **[planned]** E3 · Gaming — Steam-class integration (rides E1); layered negative-color depth rendering; GPU gaming gated behind drivers. *(task 23)*

### Track F — Developer platform
- **[partial]** F1 · Sutra language + Tri-Space artifact registry (.36n9/.9n63/.0n0 etc.) — prototype exists; freeze v1 grammar, emit Event IR + ARM64/x86-64 targets, debugger/formatter/linter.
- **[partial]** F2 · Orbital Elevator — signed, versioned compatibility routes with visible degradation; one real API v1→v2 adapter. *(no "universal compatibility" claims)*
- **[planned]** F3 · SDK + package repository + one nontrivial app built entirely through the toolchain.

### Track G — Cross-ISA parity
- **[partial]** G1 · x86-64 — bootstrap only today; bring ring-3, preemption, virtio-blk, ZXVFS, P-TERM, ELF/ZSP, A/B to ARM64 parity; CI builds+boots both from one commit. *(task 8, audit ISA_001)*
- **[planned]** G2 · Durable **13-phase** transaction with cross-ISA identical semantic trace (canonical event digest) — Chronicle is the in-memory + persisted core; needs OSEQ/IPHASE/phase_coord registries wired + canonical serialization. *(audit PHASE_001)*

### Track H — Hardware
- **[research]** H1 · Reference board bring-up on an **off-the-shelf ARM64 SoM** (do NOT gate software MVP on custom silicon).
- **[research]** H2 · Germanium chip / EPU / fractal-ID — parallel research profile, evidence-gated, never a dependency. *(task 15)*
- **[planned]** H3 · Manufacturing pack (for the partner) — schematics, PCB stackup, power tree, secure element, BOM, EVT/DVT/PVT, compliance — produced AFTER software proves the interfaces.

### Track I — Evidence, release, commercial
- **[planned]** I1 · Release capsule — generate binaries/SBOM/provenance/claim-ledger from one clean signed commit; fail packaging on any stale hash/claim; package via `git archive`, never a raw zip. *(audit P0-5)*
- **[planned]** I2 · Claim ledger — every external claim tagged verified/partial/planned/hypothesis/prohibited.
- **[planned]** I3 · **Tata Electronics** pitch + legal — one reconciled term sheet (exclusive worldwide production; Dubai-foundation patent path); customized, counsel-reviewed. Gated behind a defensible MVP. *(task 16)*
- **[planned]** I4 · Regenerate partner PDFs with embedded Devanagari fonts + evidence dashboard.

---

## 2. Sequencing (what unblocks what)

```
Track A (security P0s) ─┐
                        ├─► defensible MVP ─► Track I3 (Tata pitch)
Track B (net+drivers) ──┼─► Track C (apps) ─► Track D (fabric) ─► Track E (compat/gaming)
                        │        │
Track G (x86 parity) ───┘        └─► Track C5 Holodeck / C9 shell
Track H (hardware) runs parallel as research; H3 pack follows software proof.
```

**Critical path to a partner-ready MVP:** A1–A4 + B1–B2 + one real protected cell + the durable 13-phase proof + x86 parity. Everything in Tracks C/D/E/H makes it *cooler*, but the gate is A+B+G2.

---

## 3. Definition of done (the audit's gates, compressed)

- **Technical MVP (G7):** install a threshold-signed build on blank ARM64 **and** x86-64; boot protected userspace; P-TERM; persist files; run only signed apps; complete+recover one durable 13-phase transaction; kill/restart one protected cell without sibling loss; interrupt an A/B system update at every transition and recover; reach a TLS endpoint; reproduce the evidence capsule from the signed commit with zero warnings/mismatches/secrets/unsupported claims.
- **Partner-ready (G8):** MVP + one reconciled counsel-reviewed term sheet + a named reference board + verified claims.
- **Consumer GA (G11):** hardware validation, accessible shell, signed app/update ops, privacy, reliability, compliance, support.

---

## 4. Immediate next step — virtio-net (Track B1)

**Goal:** the kernel can send and receive Ethernet frames in QEMU, on the same virtio-mmio path as virtio-blk.

1. Discover the virtio-mmio net device (device id 1); reset → ACK → DRIVER.
2. Negotiate features (MAC; no checksum offload for v1); read the 6-byte MAC from config space.
3. Set up two virtqueues: RX (0) and TX (1) — descriptor table, avail ring, used ring.
4. Pre-fill RX with buffers (virtio-net header + frame); notify.
5. TX: prepend the 10/12-byte virtio-net header, chain the frame, put on TX, notify, reclaim used.
6. Poll used rings (interrupt-free v1, like virtio-blk).
7. **Host-test the framing + ring management** with a mock device (descriptor/avail/used bookkeeping, header layout, wrap-around); prove RX/TX round-trip in QEMU with `-netdev user`.

**Unblocks:** Den Connect transport, the distributed fabric (D), TLS/DHCP/DNS (B2), and the Holodeck's streaming rails.
