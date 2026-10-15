> **Superseded (2026-10-15).** The current architecture is [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and every module is described in [docs/SYSTEM_REFERENCE.md](docs/SYSTEM_REFERENCE.md). The order of work is now section 18 of the architecture doc. This file is kept for the record; where it disagrees with them or with the code, they win.

# ZXV / ZEDEC pqOS — Build Roadmap

Working engineering plan. Honest maturity labels, real dependencies, one canonical sequence.
Authoritative alongside `CANONICAL.md`. Last updated 2026-08-05; section 4 (G7 status) 2026-10-14.

> **Note, 2026-10-15.** This plan is behind the code. Items it lists as planned or in progress that now exist:
> virtio-net (B1, `kernel/arch/arm64/virtio_net.c`); the network stack and TLS 1.3 client (B2, `kernel/src/net`, `kernel/src/tls`);
> ZSP v2 signed packages (A2, `kernel/src/loader/zsp.h`, `kernel/src/loader/test_zsp2.c`); A/B updates with rollback for packages (A3, `kernel/src/loader/abupdate.h`);
> NVMe, AHCI, xHCI USB and HDA audio drivers (B5, `kernel/src/hwdrv`); and identity with passkeys and recovery (C8, `kernel/src/ident`).
> Their markers below are updated. Section 4 (virtio-net as the next step) is kept as written. For current status, read `docs/ARCHITECTURE.md`.

> **Note, 2026-10-15.** This plan is behind the code. Items it lists as planned or in progress that now exist:
> virtio-net (B1, `kernel/arch/arm64/virtio_net.c`); the network stack and TLS 1.3 client (B2, `kernel/src/net`, `kernel/src/tls`);
> ZSP v2 signed packages (A2, `kernel/src/loader/zsp.h`, `kernel/src/loader/test_zsp2.c`); A/B updates with rollback for packages (A3, `kernel/src/loader/abupdate.h`);
> NVMe, AHCI, xHCI USB and HDA audio drivers (B5, `kernel/src/hwdrv`); and identity with passkeys and recovery (C8, `kernel/src/ident`).
> Their markers below are updated. Section 4 (virtio-net as the next step) is kept as written. For current status, read `docs/ARCHITECTURE.md`.

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
- **[done]** A2 · ZSP v2 signed packages (`kernel/src/loader/zsp.h`, `test_zsp2.c`) — bind identity/version/arch/ABI/key-id/caps/expiry + monotonic anti-rollback; reject raw `.elf` in release profile. *(task 17)*
- **[partial]** A3 · Package-level A/B with probation and rollback is built (`kernel/src/loader/abupdate.h`). Still owed: signed, path-safe installer + **system-level** A/B (bootloader/kernel/recovery), power-loss durable at every transition. *(task 19)*
- **[planned]** A4 · Real protected **cell** backend (process/VM boundary), signed attestation not digest-equality, real crit168 digest (retire the XOR placeholder). *(task 20)*
- **[planned]** A5 · Entropy/key lifecycle — continuous RNG health, DRBG, fail-closed without entropy, nonce-uniqueness across reboot, offline root ceremony + revocation.
- **[planned]** A6 · Flip build to `-Werror`; keep zero-warning as a hard gate.

### Track B — Networking & drivers  *(the keystone — unblocks everything social)*
Current ARM64 drivers: GICv3, generic timer, PL011 UART, virtio-blk, FEAT_RNG.
- **[done]** B1 · **virtio-net** (`kernel/arch/arm64/virtio_net.c`). RX/TX virtqueues, MAC, poll-driven, host-tested framing.
- **[partial]** B2 · Built and host-tested: `kernel/src/net` (Ethernet, ARP, IP, ICMP, UDP, TCP, DHCP, DNS) and a TLS 1.3 client in `kernel/src/tls`; interop with reference servers not recorded here. Network stack — ARP/IP/ICMP/UDP/TCP, DHCP, DNS, then TLS 1.3 client (reuse audited crypto). Loss/reorder/MTU tests; interoperate with reference servers. *(audit NET_001)*
- **[planned]** B3 · Framebuffer + CPU compositor (later GPU) — for the desktop, UI, and the Holodeck.
- **[planned]** B4 · Input stack — keyboard, mouse, touch, focus, clipboard, accessibility.
- **[partial]** B5 · NVMe, AHCI, xHCI USB, HDA audio and NIC drivers exist in `kernel/src/hwdrv`. Real-silicon drivers (hardware phase) — NVMe/eMMC, USB, Wi-Fi/BT, audio, with IOMMU/DMA containment.

### Track C — Application layer  *(user-facing value; bundle modules via the modular compiler)*
Composition pattern proven (Wyrmgate/WyvernEye/Chronicle). Each app = module `.c` files compiled together.
- **[partial]** C1 · **Den Connect node** — governance **[done]**; wire posts through Wyrmgate + record to Chronicle; Concord introductions; needs B1/B2 transport. *(tasks 26/27)*
- **[planned]** C2 · **The Ledger** — abacus + rational + wyrmgate + chronicle: exact money where each transaction is judged for conservation and durably recorded.
- **[planned]** C3 · **The Archivist** — zxvfs + chronicle + recovery: tamper-evident personal storage that self-repairs.
- **[planned]** C4 · **The Companion** — chiglet + concord + cards: the AI that grows across a person's devices.
- **[planned]** C5 · **The Holodeck** — native VLC-inspired media pipeline + Streamlabs-Pro-style live production/editing + synchronized watch parties; content-addressed distributed delivery. Codecs/GPU are the gated parts; the **watch-party sync clock is buildable now** on the event model. *(task 28)*
- **[planned]** C6 · **Recovery / Disrepair utility** — Disk-Utility-class: fsck for ZXVFS, boot attestation, A/B health, **trash bin + storage manager**, subsystem health dashboard. *(task 21)*
- **[planned]** C7 · **Doctor Quacksworth** — lightweight native security suite: verify signatures/caps instead of CPU-hogging scanning; web-threat protection; sandbox. *(task 24)*
- **[partial]** C8 · `kernel/src/ident` holds identities with passkeys, recovery and optional KYC; binding Chiglet, cards, Concord and den accounts to it is not checked here. **Portable identity** — one keypair binding a person's Chiglet, cards, Concord profile, and den accounts across devices. *(fundamental)*
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

## 4. Where G7 stands (measured 2026-10-14)

Sections 0 and 1 date from August; drivers, networking and the economy have
moved a long way since (`docs/GAP_AUDIT.md` for integration,
`kernel/src/hwdrv` for NVMe, AHCI, xHCI, e1000, igb and HD Audio, checked by
`build_system/hw_drivers_check.sh`). This table measures each G7 item in
the code, not in earlier labels.

| G7 item | arm64 | x86_64 | Proved by | Missing |
|---|---|---|---|---|
| Boot protected userspace | **built**: EL0 with per-process page tables and W^X (`kernel/arch/arm64/el0_userspace.c`) | partial: one ring-3 self-test on the kernel's page tables (`kernel/arch/x86_64/ring3.c`) | `build_system/arm64_session_check.py` (CI `boot` job) | x86: per-process CR3, scheduler, full syscall ABI |
| P-TERM | **built**: the EL0 shell answers typed commands | missing | `arm64_session_check.py` | x86 shell loop |
| Persist files | **built**: ZXVFS on virtio-blk, formatted when blank, read back after a reboot | missing: ZXVFS is linked but not mounted | `arm64_session_check.py`; `test_zxvfs` (crash at every write) | x86: mount ZXVFS on NVMe through `hw_bdev_t` |
| Run only signed apps | **built**: `run` verifies Ed25519 and refuses tampered packages and bare ELFs | missing: no loader path | `arm64_session_check.py`; `test_zsp`, `test_zsp2`, `test_elf` | x86 loader; spawn/wait syscalls (ABI is 1-11) |
| A/B update, interrupted at every transition | partial: app-level A/B recovers from a power cut at every write | none | `test_abupdate` crash sweep (verify-all) | kernel-slot A/B in the EFI stubs |
| Kill/restart a protected cell, siblings survive | partial: an EL0 fault ends only that process | partial | `test_cellular_multikernel` (logical cells only) | supervisor respawn bound to EL0 processes, boot marker |
| Durable 13-phase transaction | missing: `phase_envelope.h` has no user; `chronicle_persist.c` is host-tested but not called | missing | `test_chronicle_persist` | phase executor journaling to ZXVFS, recover on mount |
| Reach a TLS endpoint | partial: TLS 1.3 client linked, boot does plain HTTP | partial: DHCP self-test only | `test_handshake`, `test-tls-verify` | handshake over `net_connect(…, 443)` at boot, checked in QEMU |
| Threshold-signed install on a blank disk | partial: single Ed25519 root (`build_system/sign_release.sh`); installer stages files only | same | `test_release_signing`, `install_zxv.py --selftest` | k-of-n verification; image-to-disk install verified before write; install-then-boot check |
| Evidence capsule from the signed commit | missing | missing | none | `git archive` capsule, double-build hash compare, secrets scan |

Next, in order: TLS at boot on arm64; ZXVFS on NVMe for x86 (the base for
the x86 shell and loader); the durable phase executor; kernel-slot A/B;
the installer and threshold signing; the evidence capsule.
