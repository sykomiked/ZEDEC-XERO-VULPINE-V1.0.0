<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# ZXV Architecture

ZEDEC XERO VULPINE (ZXV) / VOVINA SHAKINA. Created by 36N9 GENETICS LLC (Michael Laurence Curzi).
Code licence Apache-2.0; hardware designs under the [ZXV Hardware License](../LICENSE-HARDWARE.md).

Version 5.0, **2026-10-15**. This is the one architecture document for ZXV: every part of the
system, how the parts fit together, what state each one is in, and where its detailed doc lives.
It replaces version 4.1 (2026-10-11) and absorbs the older overlapping architecture files listed
in section 22, which now point here. [SYSTEM_REFERENCE.md](SYSTEM_REFERENCE.md) is its companion:
one entry for every kernel module.

**What this version was checked against.** The tree at merge batch #156 (`7d4297c2`, which brings
22 pull requests onto `main` `4bea0160`), the open draft pull requests #129, #140, #141, #153,
#154 and #155, the branches other threads have committed locally and are holding for the CI
freeze, the audits of 2026-10-15 (vertical integration, docs against code, GUI review) and the
decisions recorded with the owner. Module facts come from `build_system/strata.txt`,
`kernel/Makefile`, each module's tests (run for this audit) and a search for each module's
callers. The audit behind this version, with its gap list and fill plan, is
[ARCHITECTURE_AUDIT_2026-10-15.md](ARCHITECTURE_AUDIT_2026-10-15.md).

---

## Contents

1. What ZXV is, and where it stands
2. The rules every part follows
3. Two views of one system: value strata and hardware layers, and the chiglet that governs both
4. How ZXV runs: the four surfaces
5. Building and shipping: images, the golden disc and packages
6. The loops that join it
7. Stratum 0: shared base
8. Stratum 1: kernel and hardware
9. Stratum 2: transport and networking
10. Stratum 3: trust and identity
11. Stratum 4: logic and fault handling
12. Stratum 5: AI runtime and the Tiamat agency
13. Stratum 6: settlement and economy
14. Stratum 7: adapters, languages and the five pillars
15. Above the strata: the desktop, apps and people features
16. Cross-cutting: privacy, transport continuum, use cases
17. Quality: tests, self-audit, the 13:1 target
18. Order of work
19. Decisions made
20. Decisions still open
21. Where spec text and the design differ
22. Document map
23. Honest notes

Appendix A: every kernel module by stratum.

### Status labels

| Label | Meaning |
|---|---|
| ✅ **Built** | Code and tests on `main`, or in merge batch #156 (marked "#156"), run on every CI pass |
| 🧩 **Built, not wired** | Works and is tested, but nothing a user runs (the app, zxvd, an image) calls it |
| 🟡 **Partial** | Some of it works; the gaps are named |
| 📝 **Draft PR** | Code and tests on an open draft pull request (number given), not merged |
| 🔒 **Local** | Code and tests committed on a thread's branch that waits for the CI freeze to lift; not yet on GitHub |
| 📋 **Designed** | A written design, no code |
| 💭 **Open** | Needs a decision first |

Every row also says what the part deliberately will not do or claim, where that matters.

---

## 1. What ZXV is, and where it stands

**ZXV is one freestanding, integer-only C code base that runs three ways: as a desktop app on
macOS, Linux and Windows, as a bootable operating system, and as an embeddable library behind
twelve language SDKs.** On it sit a local multi-agent AI (the Tiamat Productive Agency AI, a
swarm of specialist models that check each other), a post-quantum peer-to-peer network, a
four-valued (paraconsistent) logic used for every verdict, and an exact, interest-free settlement
economy with adapters to the outside financial world.

**The chiglet is the system's intelligence and governor, not an app installed on it.** The
kernel is the deterministic body; the chiglet drives it through typed calls that are checked
before they run, and anything that would change the machine waits for the person (section 3.4).

### Where it stands (2026-10-15)

| Measure | Now | Evidence |
|---|---|---|
| Kernel modules | 261 directories under `kernel/src` (242 on `main`, 19 added by #156), each assigned a stratum | `build_system/strata.txt`, checked by `verify_strata.py` in CI |
| Code size | About 460,000 lines of C and headers in `kernel/src` outside tests; 61,000 in `kernel/arch` | `wc -l` at `7d4297c2` |
| Tests | 408 test programs in `kernel/src` (about 200,000 lines); 707 test files in the repository | same |
| Kernel images | Five CPU targets boot under QEMU and CI fails any that does not reach `[BOOT_OK]`; arm64 and x86_64 also pass the AI self-check `[AI_OK]` | `build_system/ci_boot_check.sh` |
| What a user runs today | The desktop app (zxvd engine with an HTML window), ZXV Studio on the Mac, the phone companion, and plain UEFI discs that boot to a self-test | section 4 |
| Real AI model | Qwen2.5-1.5B runs on the integer engine, bit-identical on every CPU; perplexity 9.371 against llama.cpp's 9.362; the whole forward pass also runs on an Apple GPU, bit-exact | [TENSOR_ENGINE.md](TENSOR_ENGINE.md), [GPU_STRATEGY.md](GPU_STRATEGY.md) |
| libzxv | ABI 1.6.0 (#156; `main` has 1.5.0), 468 exported functions, 15 capability bits | `sdk/libzxv/include/zxv_api.h`, `sdk/docs/ABI.md` |
| Test-to-code ratio | About 0.6:1 against the 13:1 target | 2026-10-15 audit (H1) |
| Physical hardware | Never booted on a physical PC or board | [BAREMETAL.md](BAREMETAL.md) |

### What merge batch #156 brings

The x86_64 handoff and the four-state frame allocator, crash records and recovery boot (arm64),
the bash-familiar terminal, the QUBO workbench and the other exact solvers, protocol twins and
AFTP, the whole forward pass on the GPU, the paraconsistent decision engines, the IRC gateway,
Lingua with FORTRAN, BASIC and Forth, the circuit-board app builder, the depth filter and login
screen, AIRC, vector graphics with the astrolabe dock, the offline outbox counter, 104 spoken
languages, public spaces, the end-user agreement, the launch screens, libzxv 1.6 (Vino) in every
SDK, the README rewrite and the adversarial-audit fixes. Until #156 merges, those parts are on
GitHub but not on `main`.

### One sentence on maturity

The parts are mostly built and tested one by one; joining them into one product that boots on
real hardware with one native interface is the work that remains (section 18). ZXV is
vertically **designed** but not yet vertically **integrated**.

---

## 2. The rules every part follows

These rules came from the owner or were agreed with the owner. Every module is held to them.

| # | Rule | Where it bites |
|---|---|---|
| R1 | **Integer only in the kernel.** No floating point in kernel images; banned at compile time | Bit-identical results on every chip, so answers can be signed and re-checked |
| R2 | **Lower strata never depend on higher ones** | Checked in CI: `build_system/strata.txt` gives every module a stratum and `build_system/verify_strata.py` (run by `verify_layers.sh`) fails on an upward include |
| R3 | **Money is exact.** A = L + E after every posting; claims equal deposits; issuance is backed only; no usury | Hard invariants that fail closed |
| R4 | **The AI proposes; the exact kernel decides.** Model output is untrusted input | Nothing a model says can write a balance |
| R5 | **Hard invariants are FALSE when broken, never BOTH.** Only genuinely allowed disagreements (GAAP against IFRS, two jurisdictions) become BOTH | Four-valued logic can't hide a bug |
| R6 | **No silent fixes.** Forecasts report; production may only refuse, roll back or isolate, each with a signed receipt | Self-healing |
| R7 | **Shorthand parser in libzxv, execution in user space, signed integer results back** | All five language pillars |
| R8 | **Life-safety, clinical and grid actions are advisory and human-approved.** Defense work is limited to logistics, medical and mapping, including the tactics and strategy engines; no weapons targeting. Financial analysis is advisory | Use cases, `pce` engines |
| R9 | **Measured, never promised.** Speed, compression and netting savings are reported only after measurement on named hardware | All docs and pitches |
| R10 | **Designed to follow, never "certified" or "compliant".** No claimed connection to PAPSS, SWIFT, central banks or card schemes | Adapters |
| R11 | **Local first; networking off by default** | The app opens no socket until the user turns it on |
| R12 | **No monopoly; cooperative markets; one fee for everyone** (0.08889%) | Economy |
| R13 | **Holographic weight coding stays on the device; the wire carries original codes** | Measured: the coded form is lossless but larger |
| R14 | **No defensive reflection.** Tarpit and isolate; never send traffic anywhere unasked | Network defence |
| R15 | **Guardians approve with their own signatures and hold no key shares** | Recovery. The code differs: `kernel/src/ident` gives each guardian a Shamir share of one of the three recovery factors (G); G alone opens nothing. 💭 section 20 |
| R16 | **GPU-first, integer-exact.** Model work runs on the GPU wherever one exists, with no per-step round trips; every GPU kernel gives the C reference's exact integers | [GPU_STRATEGY.md](GPU_STRATEGY.md); kernel images stay CPU-only (R1) |
| R17 | **Native vector graphics everywhere.** Icons, logos and boot art are vectors (ZXVG) drawn by `zui_vec`; PNG, JPEG and video can still be opened | Brand and UI (2026-10-14) |
| R18 | **One native look on every surface.** Bare metal, the desktop app and Studio draw the same desktop from the same zui tokens; never browser-looking | UI (2026-10-12, 2026-10-15) |
| R19 | **Nature boundaries are hard budgets** | Energy, compute and growth limits in the swarm and the economy |
| R20 | **Shipped model weights must allow commercial use** (MIT, Apache-2.0, BSD, CC0 allow list); weights never go in git | AI lineup, golden disc |
| R21 | **No settlement over amateur radio.** Ham links carry only cleartext, signed messages | Protocol twins (`aproto`), transport continuum |
| R22 | **No central OS releases.** Users maintain the OS; forks must keep the compatibility contract; updates are audited by the community | User-evolved OS (section 15.6) |
| R23 | **Paraconsistent by design, not by convention.** Where two sources disagree the result is BOTH and is held, never resolved by majority or averaged away | Swarm verdicts, ledger, routing, drivers (`fwadapt`) |
| R24 | **The chiglet governs through a verified ABI.** Its output is a typed call checked against bounds, its grants, the rulebook and the lpres gate; a change waits for the person's accept of its exact digest; IRQs, page tables and DMA stay deterministic kernel code | `kernel/src/chiglet/chg_abi.h`, the arm64 console (section 3.4) |

---

## 3. Two views of one system: value strata and hardware layers

ZXV is described two ways. Both are true at once.

### 3.1 The value strata (how the code depends on itself)

Every module sits on one of nine strata, and a module may include only its own stratum or lower
ones (R2). This is enforced in CI.

```text
 ┌───────────────────────────────────────────────────────────────────────────────┐
 │ S8 ABOVE     desktop, zui, apps, launch, i18n, terms, games, social, browser   │  45
 ├───────────────────────────────────────────────────────────────────────────────┤
 │ S7 ADAPTERS  ISO 20022 · accounting · jurisdiction · zxv_policy · treaties ·   │  31
 │              Lingua · five pillars · hardware emulation · fwadapt              │
 │ S6 SETTLE    ledger of record · 0.08889% fee · V-Bills · auctions · Vino ·     │  42
 │              central bank · cards · markets · estates · AIRC                   │
 │ S5 AI        integer tensor engine · GPU · swarm · chiglet · AIPI · speech ·   │  15
 │              Web4                                                              │
 │ S4 LOGIC     paradox · peer audit · recovery · decision engines · QUBO ·       │  12
 │              spatial                                                           │
 │ S3 TRUST     ML-KEM/ML-DSA/SLH-DSA · identity · seats · TLS · Vinea · loader · │  28
 │              hwkey · protocol twins                                            │
 │ S2 TRANSPORT net stack · freight · IPFS · calls · streams · bearers            │  15
 │ S1 KERNEL    M5 substrate · boot (5 CPUs) · memory · pmm · scheduler ·         │  53
 │              syscalls · zxvfs · drivers · crash · power · time                 │
 ├───────────────────────────────────────────────────────────────────────────────┤
 │ S0 BASE      SHA-2/3 · AEAD · lpres four-valued truth · rationals · ISF maths  │  20
 └───────────────────────────────────────────────────────────────────────────────┘
   numbers = kernel module directories per stratum (Appendix A)
```

A few modules sit higher than their role because their includes force it, and `strata.txt` says
why on each line (AIRC, for example, sits in settlement because it carries paid relay records).

### 3.2 The hardware layers (silicon to apps)

The strata put everything hardware-facing into stratum 1, which hides how much of a consumer OS
is still missing. The vertical view, from the 2026-10-15 vertical-integration audit, says what
runs on bare metal, what runs only in the hosted build, and what is absent.

| Layer | What it holds | On bare metal today | Main gaps |
|---|---|---|---|
| L0 Silicon and firmware | UEFI stubs, handoff, ACPI, model checks on the disc | UEFI boot on x86_64 and arm64 under QEMU/OVMF; x86_64 handoff v2 (#156) | ACPI AML interpreter (📝 #140), device tree, PSCI, Secure Boot, measured boot, firmware updates |
| L1 Hardware interface | CPU modes, exceptions, interrupts, timers, PCIe, RNG | arm64 GICv3 and timer; x86_64 long mode, one ring-3 test | x86 LAPIC/IOAPIC, MSI/MSI-X, IOMMU, SMP, ECAM, XSAVE |
| L2 Kernel | memory, processes, scheduler, syscalls, IPC, faults, power | arm64: EL0 processes, preemption, four-state `pmm`; x86_64: none of these (halts in a `hlt` loop after self-tests) | x86 process runtime, kernel heap with `free`, threads, channel IPC, signals |
| L3 Drivers | NVMe, AHCI, xHCI, USB keyboard and storage, NICs, HDA, virtio | x86_64 class drivers pass boot self-tests; arm64 virtio set | HID parser, I2C-HID, USB audio/video/serial, Bluetooth, Wi-Fi, GPU, display modes |
| L4 System services | filesystems, update, login, network, TLS, time, search, backup | arm64: ZXVFS, signed `.zsp` apps, TCP/DHCP/DNS, crash records and recovery boot (#156) | root filesystem from disk, disc v2 layout, service manager, installer, system A/B, encryption at rest, IPv6, firewall |
| L5 Runtimes | tensor engine, swarm, pillars, Lingua, Z-Wasm, SDKs | tensor self-check `[AI_OK]`; Sutra on arm64 | models from the disc, the swarm as a service, user-space runtime `zxrt`, four pillars and Lingua on bare metal |
| L6 GUI | zui toolkit, window manager, fonts, shaping, input methods, accessibility | old framebuffer shell (`desktop/zxv_shell.c`); zui is in no image yet (🔒 one visual system ports the bare-metal desktop to zui) | zui as the compositor, window protocol, login on bare metal |
| L7 Apps and tools | 407 apps, zxsh, editor, browser, no-code builder | `xedit`, the arm64 shell, a small browser | app container `.zxa`, sandbox from manifests, self-hosting toolchain, debugger |

The bare-metal detail is in [BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md) and its four companion
files in `docs/baremetal-gaps/`.

### 3.3 Hosted and bare-metal split

Many finished libraries reach users only through the hosted build. These are linked into the
desktop app or zxvd but into neither bare-metal image: zui (all of it), seats, ident, terms,
power, crash (x86), hwkey, i18n, speech, legacy, four of the five pillars, Z-Wasm, the app
runtime `zxapp`, every zxvd method group, the live Vinea node and the GPU path. Bringing them to
bare metal is steps 4 to 11 of section 18.

### 3.4 The chiglet governs: one intelligence over a verified ABI

**The chiglet is the system, not an app on it.** In a conventional stack a model is a heavy
user-space program at the top of Python, CUDA, shared libraries, libc and a monolithic kernel. In
ZXV the operating system is the runtime harness for the chiglet: the chiglet is the system's
intelligence and governor and the way a person meets every layer (L0 to L7), and the kernel is
the deterministic body it governs. Each chiglet is a companion with its own personality; the
person decides if and where chiglets meet, and its icon grows with interaction, never with IQ.

```
CONVENTIONAL                         ZXV
┌──────────────────────────────┐     ┌──────────────────────────────────────────────┐
│ LLM / application            │     │ CHIGLET: the system's intelligence, the      │
├──────────────────────────────┤     │ person's companion, the governor             │
│ Python / PyTorch / CUDA      │     ├──────────────── verified ABI ────────────────┤
├──────────────────────────────┤     │ typed calls, each checked: bounds, grants,   │
│ user-space libraries, libc   │     │ rulebook, lpres gate; changes held for you   │
├──────────────────────────────┤     ├──────────────────────────────────────────────┤
│ monolithic kernel            │     │ integer kernel and cell fabric: IRQs, page   │
├──────────────────────────────┤     │ tables, DMA, drivers, four-valued faults     │
│ silicon                      │     ├──────────────────────────────────────────────┤
└──────────────────────────────┘     │ silicon                                      │
                                     └──────────────────────────────────────────────┘
```

**How it governs (decided 2026-10-15: governor via a verified ABI).**

1. **The console is the chiglet's.** Boot ends by arming the governor and handing it the
   console; the arm64 kernel prints `[CHIGLET] the console is the chiglet's session`. The P-TERM
   commands stay as the chiglet's tools and work with no model loaded (the rulebook still works
   with the model off).
2. **Calls, not register writes.** The chiglet's output is a typed call from a fixed table
   (`kernel/src/chiglet/chg_abi.h` A1): `storage_dma_read`, `framebuffer_blit`, `cell_reload`,
   `network_pulse`, `cell_quarantine`, `model_exec`, appended to, never renumbered. A grammar-constrained model
   emits the same text form a person types.
3. **Every call is gated (A3).** In bounds, granted to the chiglet, and the kernel rulebook's
   verdict, joined with `lpres_conjoin`, so only TRUE authorises. FALSE refuses. A look (read the
   disk, redraw the person's own screen) runs on TRUE. A change from the chiglet, a "not sure"
   (NEITHER) or a dispute (BOTH) is held until the person accepts its exact SHA-256 digest, and
   the gate is checked again at that moment (A4). Safe and recovery boots change nothing.
4. **The body stays deterministic.** IRQs, page tables, DMA addressing and drivers are kernel
   code; a handler runs only after the gate, and its result goes into a bounded journal (A5).
5. **Faults are isolated, not panics.** A faulted cell is quarantined by the cell fabric's own
   fault model and can be reloaded as a new incarnation with its routes; the same four-valued
   quarantine the swarm and `pdx_agent` use (A6).
6. **Above the kernel, the same loop.** Read through zxvd, an organ answers (`chg_organ`, a lead
   plus a witness of another lineage, `swarm.organ`), anything that would change the machine is
   a held act (`intent` "act" verbs, `chg_vocab`), and apps declare which organ serves them and
   which acts they may raise (`zxapp` "chiglet" lines).
7. **A model is named, not jumped to (A7).** The "EXEC @coordinate <event>" of the
   InterPlanetary BIOS design is the typed call `model_exec <coord> <event>`: the coordinate is a
   Grid Chain word in domain 0x60 (models), a key the kernel resolves to a model cell, never an
   address; the event is the ordinal of its input in this boot's log. It is a look: what the model
   answers is data, and anything it asks the machine to do comes back as a gated call. The
   coordinate resolves through the Tiamat cell table (`chg_cells.h`; place is the organ number,
   item the cell's ordinal), and a call it cannot run is refused with one of three reasons, never
   guessed: no model at that coordinate, an engine path this build lacks, or a model not
   installed on this machine. This runs after
   the UEFI firmware has handed over, not in place of it.
8. **Every call is on the record (A8).** Each journal record is folded into a SHA-256 hash
   chain as it is committed and printed as an `[EVT]` line; on arm64 each is also an `oseq`
   event (schema `chga.call`, parent the record before it). The console command `events` signs
   the head with ML-DSA-65. `tools/zxv-events` reads a serial log, recomputes every head itself
   and verifies the signature, so a changed, removed, repeated or reordered record fails at its
   line. The signing key is made per boot (from RNDR, or marked weak on a CPU without it): it
   shows the log is whole, not which device wrote it, until a device key exists. Replay is
   claimed only where it holds: the integer `zt` engine, weights pinned by content ID, the same
   inputs.

| Part | Status | Where |
|---|---|---|
| Verified ABI gate: six typed calls, grants, rulebook, lpres, held calls with digests, bounded journal, hash-chained records | 🔒 | `kernel/src/chiglet/chg_abi.{h,c}`, 253 tests, `make test-chg-organ` |
| Event log: `[EVT]` lines, oseq events, ML-DSA-65 head signature; `zxv-events` verifies it | 🔒 | `tools/zxv-events`, `make test-zxv-events`; `arm64_session_check.py` verifies a real boot's log |
| arm64 boot hands the console to the chiglet governor; disk read, screen blit, cell reload and quarantine, network pulse handlers | 🔒 | `kernel/arch/arm64/kernel_main_arm64.c`; `arm64_session_check.py` drives it in QEMU |
| Organ routing with a witness; `swarm.organ` in zxvd | 🔒 | `kernel/src/chiglet/chg_organ.{h,c}` |
| The chiglet on screen: a click-through layer above the windows where it swims to what it can help with, fades through other windows, waits at the glass, opens only on a tap, offers only what the companion gate passed; Accept, Not now (snooze), Pin, Away; reduced motion and transparency honoured | 🔒 | `kernel/src/zui/zui_swim.{h,c}` SW1-SW9; the shell's call site is with the native GUI work |
| Held acts at every layer; organ and acts in app manifests | 🔒 | `intent` I7, `chg_vocab`, `zxapp` K1-K3 |
| `model_exec` coordinate to cell: the 80 Tiamat cells, three refusal reasons | 🔒 | `kernel/src/chiglet/chg_cells.{h,c}`, the table generated from `tools/tiamat/cell_coords.json` by `tools/tiamat/cell_table.py`, `make test-chg-cells`; the arm64 boot resolves Spinewyrm and Greetwyvern |
| `model_exec` running a model on bare metal | 📋 | The arm64 kernel loads no weights, so a runnable cell says it is not installed on this machine |
| A model on bare metal emitting the calls | 📋 | Today the calls come from the person or from a session relaying a model's output as `chiglet <call>` lines; models load from the disc once PCI and the disc v2 layout land (section 18) |
| x86_64 governor | 📋 | The x86_64 image has no user processes or console session yet (section 4.3) |

---

## 4. How ZXV runs: the four surfaces

### 4.1 The desktop app (what users run today)

| Part | Status | What it is | Limits |
|---|---|---|---|
| Engine `zxv_host` / `zxvd` | ✅ | `kernel/arch/hosted`: kernel modules compiled as a native program; zxvd is the same code with a JSON-RPC socket and libzxv | macOS (universal), Linux and Windows |
| Window | 🟡 | An HTML page (`kernel/arch/hosted/zxv_ui.html`) served on `127.0.0.1` with a per-launch token, shown in a WKWebView on the Mac (`macos/ZXVApp.m`) and in the default browser on Linux and Windows | Breaks R18. 🔒 The one-visual-system branch replaces the Mac web view with the native zui desktop; not compiled on a Mac yet |
| ZXV Studio | ✅ | SwiftUI Mac app over zxvd's socket (`macos/studio`); colours generated from the zui tokens on the one-visual-system branch | A second Mac front end until zui ships |
| Native zui present layers | 🧩 | Metal, Vulkan, D3D11, X11 and Win32 present code (`kernel/arch/hosted/zui_present_*`), tested under Xvfb in `zxvd.yml` | Shipped nowhere; the Metal backend is built by no script |
| First-run model download | ✅ | `macos/zxv-model-fetch.sh` with a pinned manifest | Weights are never bundled in git |

[MAC_APP.md](MAC_APP.md) and [DESKTOP_LINUX_WINDOWS.md](DESKTOP_LINUX_WINDOWS.md) have the build
and install detail.

### 4.2 zxvd, libzxv and the SDKs (the developer surface)

| Part | Status | Detail |
|---|---|---|
| zxvd core methods | ✅ | About 100 methods: swarm, model, notes, net, ledger, AIPI, files (the Banker), spaces, phone link, playground, social, calls, translation, device mesh, UI settings. [ZXVD.md](ZXVD.md); spec `api/zxvd.openrpc.json` checked in CI |
| zxvd method groups | ✅ | 17 groups, each behind a feature switch: cards, passkeys, vault, swap, auction, zenny, stats, bonds, derivatives, grants, estate, companion, web4, seats, power, irc (#156), pspace and hall (#156) |
| libzxv C ABI | ✅ | 1.6.0 on #156 (468 functions; the 21 new ones are the `zxv_vino_*` group); frozen symbol lists per version in `sdk/libzxv/abi/`; policy [sdk/docs/STABILITY.md](../sdk/docs/STABILITY.md) |
| Language SDKs | ✅ | Python, Java, .NET, TypeScript, Rust, Ruby, C++, Kotlin and Dart at 1.6.0 in lockstep; Go, PHP, Swift and Solidity tag-based. Each has its own CI workflow |
| Kits, engines, frameworks | ✅ (preview) | 8 kits (Z-Wasm, Z-Contract, Adapter-Matrix, Z-FFI, AIPI, Z-Audit, Tri-Isolate, ZAB-Logic); 5 engine plug-ins; 4 web frameworks; 15 industry kits; Z-Fabric HDL; embedded ports |
| Pillars | ✅ | `sdk/pillars`: COBOL/Sutra, Fortran/Trebago, Ada/Eve, APL-K/KRON, MUMPS/MYRA (section 14) |

Gaps: zxvd's economy groups keep their books in memory, use stand-in signatures and touch no
real payment rail; estate and grants checks use Ed25519 where the modules specify ML-DSA; the
jurisdiction host starts with no verify hook, so no plug-in can register; the AIPI mesh binding
is linked but never switched on; IRC and AIPI are missing on Windows ([ZXVD.md](ZXVD.md) "Honest
limits"). The cb_mx and cb_vss bindings, card issue calls and the acct, juris, intent and privacy
groups are not in libzxv yet.

### 4.3 The bootable OS (bare metal)

| Image | Status | What it does | What it lacks |
|---|---|---|---|
| x86_64 (first physical target) | 🟡 | UEFI boot, handoff v2 (#156), PCI class drivers with self-tests, one ring-3 test, `[AI_OK]` | No timer interrupt, scheduler or user processes; halts in a `hlt` loop |
| arm64 (most complete) | 🟡 | GICv3, timer preemption, EL0 processes, `pmm` (#156), virtio, ZXVFS, signed `.zsp` apps, crash records, framebuffer desktop, network stack; boot hands the console to the chiglet governor (🔒 section 3.4); CI types into its shell across two boots | No handoff in its EFI stub, no PCI (cannot see the golden disc's models), no spawn/wait/kill |
| riscv64, riscv32, arm32 | 🟡 | Boot to `[BOOT_OK]` under QEMU | No AI self-check, no user processes |

[BAREMETAL.md](BAREMETAL.md) covers building and booting; [BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md)
ranks the 29 gaps to a consumer OS.

### 4.4 Phones and devices

| Part | Status | Detail |
|---|---|---|
| Phone companion (Flutter, iOS and Android) | ✅ | Five views like Studio; drives zxvd over the phone link and holds no books or model; sideload only. `mobile/companion`, CI `companion.yml` |
| Native device-mesh apps | 🧩 | `mobile/android`, `mobile/ios`, `mobile/core/zxv_mobile.c`; no CI builds them |
| Hardware designs | 📋 | OEM specification package in `hardware/docs/oem` (consumer, institutional, servers); the design folders are empty on purpose: designs stay on the owner's machine ([LICENSE-HARDWARE.md](../LICENSE-HARDWARE.md)) |

---

## 5. Building and shipping: images, the golden disc and packages

### 5.1 What gets built

| Artifact | Status | How | Signed |
|---|---|---|---|
| Plain UEFI discs (arm64, x86_64) | ✅ | `build_system/baremetal_images.sh` in `Dockerfile.baremetal` → `mk_efi_disc.sh` → GPT image with one FAT32 ESP; booted under OVMF in CI (`uefi-disc`) and in the release workflow | No: SHA-256 sums only; Secure Boot must be turned off |
| Golden disc (arm64, x86_64) | 🧩 | `build_system/mk_golden_disc.sh` adds two Qwen2.5 GGUF models (Apache-2.0, pinned commit, hash and size) and assets; the EFI stub re-hashes the models on every boot (`[MODELS_OK]`) | No; models hash-checked only |
| Desktop packages | ✅ | `build_system/package_desktop.sh`: Mac `.dmg`/`.app.zip`, Studio `.zip`, Linux `.deb`/`.tar.gz`/AppImage (x86_64, aarch64), Windows `.zip` | Mac ad hoc unless a Developer ID is set; Windows unsigned |
| Draft release | ✅ | `.github/workflows/release.yml` (manual): preflight, every package above, the plain UEFI discs (booted first), `SHA256SUMS`, then a **draft** GitHub release | Publishing is manual ([release/README.md](release/README.md)) |

### 5.2 The gap between this and "golden ISO dist images"

1. **There is no ISO.** The golden disc is a raw GPT `.img` (about 1.63 GB) for USB sticks and
   VMs. `build_system/mkuniversal_disc.sh` can wrap one ESP with several boot files into a hybrid
   ISO, but no workflow runs it and only x86_64 and arm64 have EFI stubs.
2. **The golden disc is never released.** Only `golden-disc.yml` builds it, by hand, and keeps it
   7 days. `release.yml` refuses model weights by rule; shipping weights as a release asset or a
   mirror needs the owner's call (section 20).
3. **It boots to a self-test, not a desktop.** The kernel verifies the models but does not run
   them; x86_64 halts after its tests (section 4.3).
4. **Unsigned, no installer, one partition.** No Secure Boot signature, no on-device installer,
   no system A/B image, no recovery boot on x86_64.
5. **Missing assets.** `golden.contents` lists `assets/boot`, `assets/brand`, `assets/app-icons`
   and `assets/apps`; they are not in the tree yet (the brand and the 407 icon vectors are held
   back until the trademark checks).
6. **Two Linux/Windows engines.** `package_desktop.sh` ships zxvd; `build_desktop.sh` also
   builds `zxv-swarm` (zxv_host). One should be retired.
7. **Release signing exists but is not used.** `sign_release.sh`, the offline root ceremony and
   `verify_release.sh` are tested by `test_release_signing.sh`, but `release.yml` does not call
   them. `build_system/mk_deliverable_zip.sh` copies a `PROVENANCE/` folder that no longer exists,
   and `make_release.sh` still ships the superseded `CANONICAL.md` and `ROADMAP.md`.

Steps 5, 8 and 13 of section 18 close these.

### 5.3 How the OS evolves after it ships

ZXV has **no central OS releases** (R22). The release workflow above produces a starting image;
after that, each install is maintained by its user and updated through community updates that
pass the compatibility contract and an audit by owner-chosen auditors (📝 #155, section 15.6).
Signed content-addressed packages (`update`, `zxpkg`, checked by `provenance`) and app-level A/B
(`loader/abupdate`) are the mechanisms on arm64 today.

---

## 6. The loops that join it

### Loop A: value

```text
            ┌──── 7 ADAPTERS ◄────── 6 SETTLEMENT ◄──────┐
            │                                             │ A5
            ▼ A7                                     5 AI RUNTIME
      OUTSIDE WORLD                                       ▲
            │ A8  signed manifests, updates,              │ A4
            ▼     custodian receipts                      │
        1 KERNEL ──A1──► 2 TRANSPORT ──A2──► 3 TRUST ──A3──► 4 LOGIC
```

| Arc | What flows | Status |
|---|---|---|
| A1 kernel → transport | Sockets for Vinea | 🟡 App: one UDP socket, LAN or online, off by default, NAT hole punching. Bare metal: arm64 TCP/IP only |
| A2 transport → trust | Every datagram verified (ML-DSA-87, proof-of-work, replay window) | ✅ in the app |
| A3 trust → logic | Verified records become four-valued verdicts; peers replay them | 🟡 The app emits peer-audit records through settle's audit hook; no network transport for records and votes |
| A4 logic → AI | The swarm budget gates every generated token | ✅ in the app |
| A5 AI → settlement | Each swarm cycle settles on the ledger of record, with A = L + E reconciled every cycle; peers' delivered compute, proven by signed receipts, raises the next budget (off until the owner turns it on) | ✅ in the app |
| A6 settlement → adapters | Ledger entries become ISO 20022 and statements | 🧩 both sides exist; not chained |
| A7 adapters → outside | Wallet, gateways, statements | 🟡 zxvd exposes ledger, netting, ISO 20022 and cards; no live rail (R10) |
| A8 outside → kernel | Signed updates and model hashes; custodian receipts | 🟡 update checks in the app; custodian receipts built in `vauction`, not fed from outside |

Every cycle the 0.08889% fee comes off the swarm's pot into the reserve, dividend, node-bounty
and regenerative buckets, and last cycle's bounties go back into the next cycle's budget. The
reserve, dividend and regenerative buckets only accumulate in the app today; the modules that
spend them are built (section 13) and served by zxvd, not yet joined to the app's loop.

### Loop B: quality

```text
 code ─► tests, fuzzers, proofs ─► mutation score ─► audits ─► fixes ─► code
   ▲                                                                       │
   └─ peer-audit receipts ◄─ signed repair receipts ◄─ forecasts in production
```

Built: tests, tier suites, fuzzers, sanitizers, proofs, the mutation gate, the strata check, the
adversarial self-audit and community bounties (📝 #154, which pay for fixes). Not yet joined:
forecasting monitors feeding repairs, and peer-audit receipts feeding the test corpus.

---

## 7. Stratum 0: shared base

Small, pure building blocks that every stratum may use (20 modules).

| Part | Status | Limits |
|---|---|---|
| SHA-256, SHA-3/Keccak, ChaCha20-Poly1305, HKDF, X25519, ECDSA/RSA verify (`robin_debanks`, `mlkem/keccak`, `tls/*` primitives) | ✅ | Vendored Keccak copies pinned to upstream vectors |
| Four-valued truth `lpres` (NEITHER, TRUE, FALSE, BOTH) | ✅ | The one truth type; `lpres_hex` (twelve-state) and `trit_t` are older encodings still to fold in (audit H4) |
| Exact rationals `rational` | ✅ | 64-bit parts with overflow flags; never 128-bit types |
| ISF maths `surplus` | ✅ | Interaction Surplus Framework; used by the swarm, chiglet and economy |
| Nine forms of capital `zcapital` | ✅ | One canonical order for every module |
| UBH-168 framing `crit168`, `ubh`; QR encoder `qr` (#156) | ✅ | |
| Divide by zero | ✅ | Returns an explicit "underdetermined" flag; never a number |

## 8. Stratum 1: kernel and hardware

53 modules. Section 3.2 has the bare-metal picture; this section is the parts list.

### 8.1 The M5 substrate

Five kernel axioms, each a module, replace the usual kernel primitives. All are built and tested.

| Axiom | Module | What it does |
|---|---|---|
| K1 OSEQ | `oseq` | Ordinal sequencer: monotonic ordinal ids, a causal DAG of events, replay and cycle rejection, happens-before queries |
| K2 RMAG | `rmag` | Rational magnitudes: exact compute, memory, bandwidth and energy budgets and quotas across the 13-phase pipeline |
| K3 LPRES | `lpres` (stratum 0) | The four-valued truth type every verdict uses |
| K4 IPHASE | `iphase` (stratum 2) | Deterministic route contracts between endpoints, with priority, weight and failover chains |
| K5 CHOICE | `choice` | Deterministic selection among candidates with explicit tie policies; ambiguous choices are deferred, never guessed |
| K6 PHASE_COORD | `phase_coord` | Admits, vetoes or defers coupled steps and hardware actions under coverage and health policy |

### 8.2 Parts

| Part | Status | Limits and doc |
|---|---|---|
| Boot on arm64, x86_64, riscv64, riscv32, arm32; zero-float images | ✅ | Emulated hardware only |
| x86_64 handoff v2 (memory map, RSDP, SMBIOS, clock, model verdict), `[HANDOFF_OK]` | ✅ #156 | arm64 stub has no handoff yet; 📝 a pushed follow-up moves the 90 MB of kernel buffers onto map frames ([BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md)) |
| Four-state frame allocator `pmm`, page free, slot reuse, a faulting app ends only itself (arm64) | ✅ #156 | x86 has no allocator from the firmware map yet |
| Capability-checked syscalls, scheduler, memory (`syscall`, `sched`, `mm`, `el0_userspace`) | 🟡 | Processes on arm64 only; ZXV error numbers (#156) |
| zxvfs (journal, extents), FAT32 | 🟡 | 4 MB, 256 files, flat; FAT32 read-only 8.3 names; no fsck |
| Class drivers `hwdrv`: NVMe, AHCI, xHCI, USB keyboard and storage, e1000/igb/r8169, HDA | ✅ (polled, x86_64 image) | No MSI, no IOMMU; [HW_DRIVERS.md](HW_DRIVERS.md) |
| Crash records, safe mode, recovery boot `crash` | ✅ #156 (arm64 image) | x86 port missing; [CRASH_RECOVERY.md](CRASH_RECOVERY.md) |
| ACPI tables `acpi`; AML interpreter, sleep, power-off, fault-safe MSR access | 🟡 / 📝 #140 | |
| Power: per-app energy, battery, budgets `power` | 🟡 | zxvd `power.*`; bare-metal battery, thermal and sleep wait on #140; [POWER.md](POWER.md) |
| Time: SNTP, RTC, IANA zones `timesvc` | 🧩 | Linked into all five images, never called; no NTS; [TIME.md](TIME.md) |
| Exact quantum circuit simulator `qsim`, equivalence checker `qeq` (#156) | ✅ | Classical simulators, labelled as such; [QUANTUM_SIMULATOR.md](QUANTUM_SIMULATOR.md), [CIRCUIT_EQUIVALENCE.md](CIRCUIT_EQUIVALENCE.md) |
| Hardware emulation standard `hwemu` | ✅ | Public emulation of the hardware classes (libzxv 1.4); designs stay private |
| Hardware RNG | 🟡 | arm64 entropy source (`kernel/arch/arm64/entropy.c`), mixed into the generator, never used raw for keys; continuous health tests missing |
| WORM journal, retention lock | 📋 | |

## 9. Stratum 2: transport and networking

| Part | Status | Limits and doc |
|---|---|---|
| Network stack `net`: TCP, DHCP, DNS (IPv4) | 🟡 | Runs on arm64 virtio-net; x86 NIC drivers exist but are not joined to it; no IPv6, firewall or VPN |
| Vinea: PQ Kademlia DHT, sessions (X25519 + ML-KEM-1024), agreements, file exchange, LAN discovery, NAT hole punching, persisted identity | ✅ in the app | Spool re-sync between hosts not built; [VINEA.md](VINEA.md) (module in stratum 3) |
| Public spaces and the watch hall (`vna_space`, `pspace.*`, `hall.*`) | ✅ #156 | [SPACES.md](SPACES.md) |
| IPFS node: Kubo-identical CIDs, UnixFS, HAMT directories, private pins | 🟡 | No libp2p, Bitswap or DHT interop; index in RAM; [IPFS_NODE.md](IPFS_NODE.md) |
| Freight (168-row erasure-coded transfer), harmonic wire, endian hopping | ✅ library | Served by zxvd module methods; ehop not on Vinea's frame hook; [HARMONIC_WIRE.md](HARMONIC_WIRE.md), [ENDIAN_HOPPING.md](ENDIAN_HOPPING.md) |
| Calls (RTP, STUN, FEC, jitter, congestion) and streams | ✅ | zxvd `call.*` and the people features |
| Bearers: cellular, satellite and radio link models `bearer` | ✅ #156 library | Models and selection, no modem driver; no caller yet; [retail/BEARERS.md](retail/BEARERS.md) |
| Protocol twins `aproto`: catalogue of network protocols from the 1950s on, each with a P2P, PQ, auditable twin; AFTP built first; ham gate | ✅ #156 library | No caller yet; [P2P_PROTOCOL_TWINS.md](P2P_PROTOCOL_TWINS.md) |
| AIRC agent relay chat; IRC gateway into the swarm on TLS 1.3 | ✅ #156 | Gateway in zxvd `irc.*`; AIRC core has no caller yet. Quorum-gated jumbo frames and adaptive zero trust adopted; the AIRC-XDCC training cache is next. [AIRC.md](AIRC.md), [IRC_GATEWAY.md](IRC_GATEWAY.md) |
| Web4 agent envelopes, consent, Web2/Web3 bridge | ✅ library | Signs and holds, delivers nothing; no MCP/A2A; [WEB4.md](WEB4.md) |
| `mesh_net` | 🧩 | Moves no bytes ("NOT YET" in its header) |
| Sybil and eclipse defence | 📋 | Per-prefix bucket caps, credibility weighting, quarantine partition |

## 10. Stratum 3: trust and identity

| Part | Status | Limits and doc |
|---|---|---|
| ML-KEM-768/1024, ML-DSA-65/87, SLH-DSA, HQC-5, layered PQ matrix `pqsec` | ✅ | Vendored upstream reference code with NIST vectors; [PQ_MATRIX.md](PQ_MATRIX.md) |
| TLS 1.3 client with X.509 chain checks and a root store; TLS server for the IRC gateway | ✅ | No OCSP, CRL or CT; [TLS_CERTIFICATES.md](TLS_CERTIFICATES.md) |
| Identity: passkeys (ML-DSA), optional KYC, guardian recovery, fuzzy extractor `ident` | ✅ library | zxvd `passkeys.*`; no classical passkey for browsers; [IDENTITY_AND_RECOVERY.md](IDENTITY_AND_RECOVERY.md) |
| Seats: people on a device, Argon2id login `seats` | ✅ hosted | zxvd `seats.*`; a passkey seat unlocks on the caller's flag (zxvd does not check the passkey itself); not in an image |
| Hardware keys and the offline outbox counter `hwkey`, `outbox` | ✅ library (#156 adds the monotonic counter) | A restored backup can no longer pay twice; no image or app uses them yet |
| Signed packages and loader (ZSP v2), A/B app updates, provenance (`loader`, `update`, `zxpkg`, `provenance`) | 🟡 | arm64 runs signed apps only (#156); system-image A/B missing |
| Boot attestation `bootattest`, capability control `capctl`, zero-touch provisioning `ztp` (retail track) | ✅ #156 library | No kernel image uses them; bare-metal TPM missing; [retail/ROOT_OF_TRUST.md](retail/ROOT_OF_TRUST.md) |
| Constant-time check | 🟡 | Valgrind secret-branch test; not a full side-channel audit |

## 11. Stratum 4: logic and fault handling

| Part | Status | Limits and doc |
|---|---|---|
| Paradox engine and paraconsistent ledger with four-valued agents `paradox` | ✅ | Used by `pce`, `recover` and libzxv; [PARADOX_ENGINE.md](PARADOX_ENGINE.md), [PARACONSISTENT_LEDGER.md](PARACONSISTENT_LEDGER.md) |
| Paraconsistent probability, tactics, strategy and finance engines `pce` | ✅ #156 | Advisory (R8); usable as app blocks; [PARACONSISTENT_ENGINES.md](PARACONSISTENT_ENGINES.md) |
| QUBO workbench `qubo`, exact risk `xrisk`, gridlock solver `gridlock` | ✅ #156 library | Exact classical solvers; [QUBO_WORKBENCH.md](QUBO_WORKBENCH.md), [EXACT_RISK.md](EXACT_RISK.md), [GRIDLOCK_RESOLUTION.md](GRIDLOCK_RESOLUTION.md) |
| Peer audit (self-check before signing; peers replay and challenge) | 🟡 | Local hook wired; no network transport; [PEER_AUDIT.md](PEER_AUDIT.md) |
| Designed recovery `recover` (refuse, roll back, isolate, each with a signed proof) | ✅ #156 | Callers: `tripartite_fs`, component rollback |
| Forecasting `bombsquad`, `predictive` | 🧩 | Report only (R6) |
| Spatial engine (exact geometry, fusion, fleet planning) | 🟡 | ROS 2 bridge, RTK over Vinea, Eve geofences not built; [SPATIAL_ENGINE.md](SPATIAL_ENGINE.md) |
| TLA+ and Lean models of the ledger, headroom and rationals | ✅ | Models of the C, not proofs of the binary; [FORMAL_INVARIANTS.md](FORMAL_INVARIANTS.md) |
| The Pit Boss: rulebook that enforces, advisor that proposes | 📋 | [PIT_BOSS.md](PIT_BOSS.md) |
| The Cage: quarantine and clearance with desks | 📋 | [CAGE.md](CAGE.md) |

## 12. Stratum 5: AI runtime and the Tiamat agency

### 12.1 The model runtime

| Part | Status | Limits and doc |
|---|---|---|
| Integer tensor engine: GGUF reader, tokenizer, RoPE, forward pass, E8/Leech quantisers `tensor` | ✅ | Reads qwen2, qwen3 and llama; no SentencePiece, no YaRN/LongRoPE; [TENSOR_ENGINE.md](TENSOR_ENGINE.md) |
| SIMD (NEON, AVX2, AVX-512) and a row-parallel thread pool | ✅ | Bit-identical to the C path; hosted only (images are `-mgeneral-regs-only`) |
| GPU: matrix products and the whole forward pass in one command buffer on Metal, plus the same op list on a C backend in CI | ✅ #156 | Bit-exact on an M3 Max; prompt reading 2.3× the CPU; generation still slower than the CPU until kernels are fused; Vulkan and CUDA next ([GPU_STRATEGY.md](GPU_STRATEGY.md)) |
| Several models at once, LoRA per agent | ✅ | 4 model slots, 16 agents; no real adapter trained yet |
| Grammar-constrained decoding `zt_gram`, evidence head `zt_evh` (μ/λ → T/F/B/N) | 🧩 | Not called by the app's generation path or libzxv; [AI_MODEL.md](AI_MODEL.md) slice 3 |
| Whisper speech in, OuteTTS speech out on the engine | 🧩 | Tested; the speech module does not call them yet (📝 #153 wires them, section 12.4) |
| Tensor-parallel inference across machines (slice 1) | ✅ | Head split, pipeline, batching and Vinea transport next; [HPC_DESIGN.md](HPC_DESIGN.md) |
| VRAM block store, co-processor tiers, edge clusters (`vblock`, `cotier`, `edgeclus`) | 🧩 | No real GPU behind them; [VRAM_BLOCKS.md](VRAM_BLOCKS.md), [COPROCESSOR_TIERS.md](COPROCESSOR_TIERS.md), [retail/EDGE_NODES.md](retail/EDGE_NODES.md) |
| Fitted and compiled specialists, adapter synthesis, prompt-to-circuit | 📋 | [AI_MODEL.md](AI_MODEL.md) slices 1-9 |

### 12.2 The swarm and its economy

| Part | Status | Limits and doc |
|---|---|---|
| Fibonacci tokens-per-cycle budget, emotional economy (emoji type and intensity on an imaginary axis), cooperative market, governor, witness, DNA `swarm` | ✅ in the app | [SWARM_ECONOMY.md](SWARM_ECONOMY.md), [TOKEN_DENOMINATIONS.md](TOKEN_DENOMINATIONS.md) |
| Venn computing: shared work done once (`swarm_overlap`, `swarm_logic`) | ✅ | |
| Token yield: verified work per token, candle chart, pool weights by verified work | 📝 three pushed commits on PR #138's branch after the head #156 took | Measurement only; the pool link is off by default and never mints. Its doc, `TOKEN_YIELD.md`, lands with it |
| Chiglet: small on-device mixture of experts weighted by ISF | ✅ | |
| Growth and shrink by Fibonacci step, speculative decoding, shared KV cache, mmap loading | 📋 | [SWARM_ECONOMY.md](SWARM_ECONOMY.md) §11, §15 |

### 12.3 The Tiamat Productive Agency AI

The owner named the local AI **Tiamat Productive Agency AI**: an agency of many specialist
models at many levels, not one agent. Its design (2026-10-15) is in the swarm model lineup report
held in the project files; it names upstream models and their licences, so it stays out of the
public tree until the lineup ships. The architecture is:

| Agency level | Biology level | What it is in ZXV |
|---|---|---|
| Specialist | Organelle, cell | One chiglet: a proposer, a witness from a different model lineage, its evidence head and a verifier, behind companion grants and spend caps. Gives one claim a T/F/B/N verdict |
| Desk | Tissue, organ | Chiglets on one job (voice, documents, code, translation), picked by a router head |
| Department | Organ system | Desks joined across apps: senses, reasoning, memory, defence, action |
| Agency | Organism | One device's whole swarm, from its signed tier pack |
| Agency network | Species, environment | Devices with the same pack, trading organs over Vinea through AIPI, checked by QUORUM |

Three properties hold at every level: the same propose-verify-join operation (the Belnap join is
a lattice join, so grouping order never changes a verdict and a BOTH stays BOTH); growth bounded
by resources (Fibonacci budgets split exactly; levels that don't fit stay dormant); and evolution
only under verification and user approval. Coordination comes from the Venn rule (shared work
done once) and the Interaction Surplus Framework (members picked for diversity, paid by measured
contribution, levels grown only while `surplus_can_grow` holds).

| Part | Status |
|---|---|
| The parts the hierarchy is built from | ✅ (`swarm`, `surplus`, `chiglet`, `aipi`) |
| Pre-installed, rebrandable lineup per device tier (3 personal and 3 institutional devices, 3 personal and 3 institutional servers); licence allow list (R20); ternary BitNet and TriLM as cheap witnesses | 📋 report done; shipping the weights needs the owner's call on the release rule |
| Digital DNA, hobbies (3 to 5 per agent, tilting proposals, never verdicts), chiglet-to-chiglet visits under both owners' grants | 📝 #155 (`chg_kin`) |
| Pre-registered benchmark: one big model against Tiamat at equal memory and tokens | 📋 |
| Brand | 💭 trademark search on "Tiamat" (classes 9 and 42); never marketed as "superintelligence" |

### 12.4 Voice and the chiglet companion

| Part | Status | Detail |
|---|---|---|
| The chiglet speaks only when tapped, held or asked; reads sections aloud like a person (names hashes and ids instead of spelling them); stops on tap; words-only mode; waits for the screen reader | 📝 #153 (`zui_chgvoice`, `speech_tidy`) | English voice only; other languages show words, never another language's voice |
| On-device text to speech on the integer engine | 📝 #153 (OuteTTS); 🔒 switching to Kokoro-82M (Apache-2.0) | OuteTTS slurs long sentences; about 40 s per sentence on a 4-core x86 container; M3 Max not measured |
| System-wide speech to text (Whisper) with dictation and "Chiglet, ..." routing `zui_hear` | 📝 #153 | Tests run on stand-ins and a tiny fixture, not the real models |
| Face-to-face call tile, visits inside an open call, vector skins `.zxcs` | 📝 #153 / 🔒 | The network side of visits is not built |

## 13. Stratum 6: settlement and economy

42 modules. The economy follows the owner's rules: one 0.08889% assurance fee for everyone
(8889/10,000,000, split 50% reserve floor, 25% V-Bill dividend, 15% node bounties, 10%
regenerative), backed-only issuance, no usury, nine forms of capital, cooperative markets. Every
legal point is pending counsel, and a name never changes legal substance.

### 13.1 Built

| Part | Status | Doc |
|---|---|---|
| Ledger of record `pay_ledger` and settlement spine `settle`; fee and trade loops | ✅ in the app | |
| libzxv economy engine (compress, route, decompress with the fee) | ✅ | |
| Central-bank toolkit: currencies, settlement net, ISO 20022 MX, VSS, no-usury `cbank` | ✅ library | [CENTRAL_BANK.md](CENTRAL_BANK.md) |
| Charge cards (ISO 8583, EMV) `cardnet` | ✅ zxvd `cards.*` | [CARD_NETWORKS.md](CARD_NETWORKS.md) |
| V-Bills: backed-only Dutch uniform-price auctions with custodian receipts `vauction`; dividend `vswap`; solvency vault `svault`; V-Bonds `vbond`; V-Calls and V-Perps `vderiv` | ✅ zxvd groups | Likely securities; regulated switches off by default |
| Zenny chips (user-built baskets) `zenny`; public stats feed `zstats` | ✅ zxvd groups | Trademark check pending |
| Sicily Account and Player Cards `sicily` | ✅ | `player.card` reports bond and giving as 0 today, so the higher tiers can't be reached yet |
| Mint, Q-Note quarantine, credibility `cred`, three-level disputes `dispute`, FX epochs `fxepoch`, royalties, grants, estates | ✅ library | Estates and grants in zxvd; `dispute` uses a SHA-256 stand-in for its VRF |
| Vino: three-ledger bank node `vino`, floating vouchers `vino_stores`, V-Bill hub `vhub`; Vino in libzxv 1.6 | ✅ | |
| Markets: capacity, provider, commerce `capmkt`, `provider`, `market` | ✅ library | [PROVIDERS.md](PROVIDERS.md), [COMMERCE.md](COMMERCE.md) |
| Multilateral netting (`cbank`, `qnet`) | ✅ | 222M fuzz operations clean; [QUANTUM_NETTING.md](QUANTUM_NETTING.md) |
| Retail track: tax hooks `taxhook`, KYC wrapper `kycwrap` (stratum 7) | ✅ #156 library | [retail/COMPLIANCE_PLUGINS.md](retail/COMPLIANCE_PLUGINS.md) |

### 13.2 New designs of 2026-10-15

| Part | Status | What it is | Limits |
|---|---|---|---|
| **Naval ports** `navport` | 🔒 | Floating vouchers glue V-Bills to CBDC, fiat, commodity and compute rails. Parties that keep accepting each other's vouchers form a socket; the members, their pegs and one pooled **Sicilian account** of V-Bills form a naval port (one peg for K units without K(K−1)/2 bilateral pegs). Each party keeps nostro, vostro and its share of the Sicilian account. Only nets settle, signed by a quorum; peg health and reconciliation are four-valued | The share **is** the reserve: outflow past it is refused, which is the de-peg; no bail-out by other members; compression is netting and earns no yield; pipe yield (off by default) comes only from the realised 25% pool. Its doc, `NAVAL_PORTS.md`, lands with it |
| **Jurisdiction router** `jroute` (routing Vino allotments) | 🔒 | When a user accepts a discretionary system allotment, a deterministic fallback stack delivers the first asset class the locale's 32-bit capability mask allows: compute (TPC ticks, 1 VINO = 2^16), AIRC bandwidth or storage quota, then local mutual credit. A contradiction between registries holds the value in escrow (BOTH); unknown territory gives non-transferable local compute (NEITHER). Remainders stay as exact p/q vouchers | Software first: the physical layer is off; commodities (olive-oil barrels) arrive only through a detachable producer-ingress driver. Its doc, `JURISDICTION_ROUTER.md`, lands with it |
| **Community bounties** `pay_bounty` (the Sicilian Royal Navy board) | 📝 #154 (later commits 🔒) | The main way VFV is issued. Anyone posts a request free; a taker gets a letter of marque and returns with a proof and one signed bid, judged against a fair value; VFV is minted after audit and a 5-person panel drawn by beacon; BOTH goes to the arbiter. Standing vulnerability bounty, triage of frivolous and hostile requests, leaderboards | Payout amounts, caps and reviewer pay await the owner; never touches the 15% node-bounty pool. Its doc, `BOUNTIES.md`, lands with it |
| **The Sicilian Crown Allotment** `allotment` | 🔒 | Selection of one steward by sortition each epoch. FREE mode by default: one ballot per verified person, endowment from the operator's surplus, the whole endowment to the steward; free civic ballots are accepted in every mode. App #075 Crown is its screen | Off until a jurisdiction plug-in sets the switch after legal review; SAVINGS and PAID modes stay regulated gaming. Renaming changes nothing legally; removing consideration does |
| Token yield link to the node-bounty pool | 📝 | Section 12.2 | Never mints; pay for service, not a return to holders |

### 13.3 Still designed only

| Part | Design and limits |
|---|---|
| Community and institution build profiles | Not linkable; V-Bills, ports and reserve in the institution profile; no protocol fiat on- or off-ramp (closed-loop framing review) |
| Sovereign and co-minted baskets | Each bank signs with its own steward threshold |
| FX markets | Uniform-price batch epochs; cooling epochs instead of cliff liquidation; leverage off by default |
| Demurrage; dividend only to active users | 💭 section 20 |

## 14. Stratum 7: adapters, languages and the five pillars

### 14.1 Adapters

| Part | Status | Doc |
|---|---|---|
| ISO 20022 (pacs.008, camt.053, CBPR+), schema-validated | ✅ | Format-valid, not connected |
| Accounting adapters (IFRS, US GAAP, AAOIFI and others) `acct`; jurisdiction plug-in ABI `juris` | ✅ | zxvd `jurisdiction.*`; no libzxv group yet; [ADAPTERS.md](ADAPTERS.md) |
| Policy engine and contract DSL `policy` (zxv_policy) | ✅ library | Deterministic. `legal_engine` (treaties, precedents, DAO) is a separate older module; its strings still cite an old licence |
| Treaties (COBOL copybooks, Sutra) `treaty`, `sutra` | ✅ | |
| Legacy bridge (SIP, SS7, EBCDIC, COBOL, Fortran) `legacy` | ✅ library | [LEGACY_BRIDGE.md](LEGACY_BRIDGE.md) |
| Firmware adapter `fwadapt` (paraconsistent driver joins over `hwemu`) | 🧩 | No caller outside tests |
| Manufacturing checker `mfg` | 🧩 | [MANUFACTURING.md](MANUFACTURING.md) |
| Adapter box (retail) | ✅ #156 | [ADAPTER_BOX.md](ADAPTER_BOX.md) |

### 14.2 The five language pillars

Every pillar pairs a legacy language with a compact AI shorthand (R7). No pillar can mint, set κ
or touch a balance.

| Pillar | Legacy side | Shorthand | Role | Status |
|---|---|---|---|---|
| Treaty | COBOL copybooks | **Sutra** | Agreements and settlement records | ✅ `kernel/src/treaty`, `kernel/src/sutra`, `sdk/pillars/cobol-sutra`; Sutra also runs in the arm64 image |
| Physics | Fortran | **Trebago** | Commodity flows, netting proposals | ✅ `sdk/pillars/fortran-trebago` (hosted) |
| Spatial | Ada/SPARK | **Eve** | Geofences, corridors | ✅ `sdk/pillars/ada-eve` (hosted) |
| Time series | APL/K | **KRON** | SCADA and market series | ✅ `sdk/pillars/apl-kron` (hosted) |
| Clinical | MUMPS/M | **MYRA** | Clinical and genomic trees | ✅ `sdk/pillars/mumps-myra` (hosted) |

### 14.3 Every programming language, and every spoken one

| Part | Status | Detail |
|---|---|---|
| Lingua: one shared IR with FORTRAN, BASIC and Forth front ends as app blocks, each with an AI twin; a 104-row language catalogue | ✅ #156 | [LANGUAGE_CATALOGUE.md](LANGUAGE_CATALOGUE.md). About 10 of the 104 run in ZXV today (C, Sutra, Trebago, Eve, KRON, MYRA, Wasm and the three Lingua front ends); the rest are host languages through SDKs, queued or recorded only. Not built into libzxv despite its README |
| Dragon-themed twin names (purpose + wyrm, drake, hatchling or lindworm) | 📋 naming rule | Pillar names stay |
| Apps are never single-language: each feature uses the best-fit language; critical features run in two and are joined with `lpres` | 🟡 | [APP_MODEL.md](APP_MODEL.md); apps use only C, JS and Swift today |
| 104 spoken languages in the user space, keyboard layouts, Brahmic shaping, ASCW Golden faces for every script | ✅ #156 (machine translations, unreviewed) | [SPOKEN_LANGUAGES.md](SPOKEN_LANGUAGES.md), [FONTS.md](FONTS.md); input methods for Chinese, Cantonese, Japanese, Korean and Ge'ez in one pushed commit #156 did not take |
| i18n library (778 locales, CLDR formatting) | 🟡 | Hosted; app strings now generated from it; non-Gregorian calendars missing; [LANGUAGES.md](LANGUAGES.md) |

## 15. Above the strata: the desktop, apps and people features

### 15.1 The native interface `zui`

| Part | Status | Detail |
|---|---|---|
| Toolkit: canvas, layout, widgets, theme tokens, text shaping, bidi, IME, keymaps, accessibility (screen reader, magnifier, captions, on-screen keyboard, switch access, voice control), lock, notices, settings, privacy indicators | 🧩 | Tested in verify-all and CI; linked into no image and no shipped app yet |
| Window manager with the astrolabe dock (a corner seal blooming into rings) and icon motion that shows state (fast: needs input, medium: running, slow: idle, still: not running) | ✅ #156 | |
| Vector renderer `zui_vec` and the ZXVG format; `tools/zxvg` converts and lints the 407 icons | ✅ #156 | Icons and partner logos held in the project files until the trademark checks |
| Four-axis depth effect filter driven by the four-valued tag (integer only, also for video); vault-door login after boot; foil | ✅ #156 | Contrast tokens and glyph fallback in one pushed commit #156 did not take |
| Image and video decoders (PNG, JPEG, GIF, BMP, QOI, MJPEG, Y4M) `media` | 🔒 | |
| ZXVA vector audio (procedural app sounds, the launch synth) `vaudio` | 🔒 | Its doc, `ZXVA.md`, lands with it |
| **One visual system**: bare-metal desktop ported to zui with ASCW text, 15 keyboard layouts and Spaces; Studio theme generated from the zui tokens; the Mac web view replaced by the native desktop | 🔒 | All five kernels build and every zui test passes; the Mac code is not compiled yet; title bars small on high-DPI; full-frame redraw |
| End-user agreement and its screen `terms`, `zui_terms` (first boot, per seat, read to the end) | ✅ #156 | Licensor SCENIC BYPASS CONSULTING, LLC; shell hookup once login lands |
| Chiglet companion: pops up like a call tile, shows its feeling, emoji status bar | 🟡 (`zui_chiglet`) / 📝 #153 | |

### 15.2 Boot and launch screens

| Part | Status | Detail |
|---|---|---|
| Boot: all gold on #002911; fox at the top; 36N9, ACOTO, ASCW crest, SMIC, CIS logos; mirrored bars that track real progress; vault doors | ✅ #156 | SMIC trademark check pending |
| Every app launch replays the boot screen inside its window with the app's icon and its own sounds, at the speed of real loading | ✅ #156 (`launch`) | [APP_LAUNCH_SCREENS.md](APP_LAUNCH_SCREENS.md) |

### 15.3 The 407 apps

| Part | Status | Detail |
|---|---|---|
| Three tiers: L1 001-144 base modules, L2 145-271 built from L1, L3 272-407 built from L2 | 🟡 | Only a handful of L1 apps have real code (audit: 6 of 144); the catalogue is in the project files |
| App runtime `zxapp`: blocks in many languages (C, Sutra, Trebago, Eve, KRON, MYRA, Wasm, Lingua, pce) | ✅ hosted | [APP_DEVELOPMENT.md](APP_DEVELOPMENT.md) |
| Circuit board: build an app by wiring apps together | ✅ #156 | [CIRCUIT_BOARD.md](CIRCUIT_BOARD.md) |
| Built-in system utilities (journal, backup, search, print, location, screen time) | 📝 #141 | hosted zxvd |
| The Banker (file manager) and the Hoard (storage root), Sealed Hoard snapshots | 🟡 | Files layer in the app; encryption at rest missing; [LEXICON.md](LEXICON.md) |
| Registry of 407 slots with crossing rules; Registrar; Rosetta Shell; trade routes | 📋 | [REGISTRY.md](REGISTRY.md), [REGISTRAR.md](REGISTRAR.md), [ROSETTA_SHELL.md](ROSETTA_SHELL.md), [TRADE_ROUTES.md](TRADE_ROUTES.md) |
| Terminal zxsh: four-valued exit codes, bash-familiar syntax, line editing, sub-terminals | ✅ #156 | Hosted only |

### 15.4 People features

Calls, streaming, the social feed ranked by ISF, translation, quests and game mechanics, Spaces
(Home, Office, Team, Cloud top, Park), the phone link and the device mesh are ✅ in zxvd;
[STAPLE_APPS.md](STAPLE_APPS.md), [ENGAGEMENT.md](ENGAGEMENT.md),
[MOBILE_AND_DEVICES.md](MOBILE_AND_DEVICES.md).

### 15.5 Coding engine

How sessions build ZXV (settle on the first pass, conflicts are BOTH, gaps are NEITHER) is
[CODING_ENGINE.md](CODING_ENGINE.md), with the short form in `CLAUDE.md`.

### 15.6 The user-evolved OS

| Part | Status | Detail |
|---|---|---|
| Compatibility contract (identity is its CID, grows only by extension), ML-DSA audits, community gate (two owner-chosen auditors, no self-audit, any fail vetoes), trial and rollback journal `evo_comm` | 📝 #155 | Builds on the fork and capability module `evolve` ([FORKS_AND_EVOLUTION.md](FORKS_AND_EVOLUTION.md)) |
| Personal chiglet bond: on-device habit model, three ISF-combined experts, export only with opt-in `chg_bond` | 📝 #155 | |
| Pop-up UI, Web4 transport, zxvd methods, pinned launch contract | 📋 | `USER_EVOLVED_OS.md` lands with #155 |

## 16. Cross-cutting: privacy, transport continuum, use cases

### Privacy

| Part | Status | Limits |
|---|---|---|
| Privacy layer `privacy`, intent layer `intent` | 🧩 | No caller in the app or libzxv |
| Lattice zero-knowledge proofs; discrete-Gaussian differential privacy; four-valued access with minimal emergency release | 📋 | Protects data, not metadata or side channels; designed to support HIPAA and GDPR, not certified |

### Transport continuum (Morse to 5G, satellite and subsea)

| Part | Status | Limits |
|---|---|---|
| Bearer models and link selection | ✅ #156 library (`bearer`) | We are a customer of satellite IP service and can't steer its routing; arriving first is not finality |
| Self-contained PQ-signed envelopes, store-and-forward, graceful degradation | 📋 | |
| Software modems, LoRa/Meshtastic, JS8/VARA, APRS, AREDN | 🟡 twins catalogued in `aproto` | Ham links: cleartext, signed, no settlement (R21) |

### Use cases

| Area | How ZXV fits | Limits |
|---|---|---|
| Central banks and cross-border clearing | Netting, ISO 20022, interest-free design, naval ports | No connection claimed; regulators decide |
| Disaster and emergency | Offline-first, contradiction-tolerant | Recommendations a person confirms |
| Defense | Logistics, medical, mapping, tactics and strategy engines | No weapons targeting (R8) |
| AI agents trading | Agent cards under their owner | Owner's identity and limits apply |

## 17. Quality: tests, self-audit, the 13:1 target

| Measure | Value |
|---|---|
| verify-all | Runs every module's host test (many under ASan/UBSan), freestanding cross-compiles and the sub-suites. The last recorded count is 3,987 passing checks (2026-10-10, `fuzz/FUZZ_REPORT.md`), before 50+ pull requests merged; a fresh count belongs in the next report |
| CI tiers | Drafts run the fast tier; ready PRs, nightly and manual runs add the slow tier; `ci-gate` needs both ([CI.md](CI.md)) |
| Mutation gate | 102 targets; a CI subset of 21 with per-target baselines |
| Strata check | 287 entries, 9 strata, run on every pass |
| Fuzzing | 24 parser and 12 economy harnesses; 222M netting operations clean |
| Proofs | TLA+ and Lean models; found two real arithmetic bugs |
| Test-to-code ratio | About 0.6:1 against 13:1, counted only by mutation-killing tests |
| Audits | [AUDIT_REPORT.md](AUDIT_REPORT.md) (2026-10-10), [ADVERSARIAL_AUDIT_2026-10-12.md](ADVERSARIAL_AUDIT_2026-10-12.md) (its six design findings fixed in #156), [ARCHITECTURE_AUDIT_2026-10-15.md](ARCHITECTURE_AUDIT_2026-10-15.md) |

Self-healing is designed recovery only (R6): refuse, roll back to a checkpoint or isolate a
peer, each with a signed proof; never auto-patch state.

## 18. Order of work

The owner's instruction of 2026-10-15: add nothing new unless it is a missing component;
consolidate cleanly; finish everything on GitHub and in the golden disc images.

1. **Consolidate.** Merge #156. Then one more batch for everything committed locally (one visual
   system, naval ports, jurisdiction router, Crown Allotment, media and ZXVA, voice, bounties,
   user-evolved OS, documentation fixes) and the pushed commits #156 did not take (token yield,
   x86 kernel buffers, contrast tokens and glyph fallback, input methods, the OEM partner pages,
   the image-averaging fix). Then the open drafts #129, #140, #141.
2. **x86 memory:** frames from the handoff map, kernel buffers moved, a heap with `free`.
3. **x86 interrupts and time:** LAPIC/IOAPIC, timer tick, XSAVE, `syscall`, RTC, ACPI reset and
   power-off (#140).
4. **Processes on both arches:** address spaces, ELF loader and scheduler on x86, spawn/wait/kill,
   threads, syscall ABI 1.1, channel IPC.
5. **Storage:** block layer v2, GPT by type, golden disc v2 layout (ESP, bootctl, system A/B,
   models, data), signed system root, zxvfs v2 and fsck.
6. **Service manager:** journal, crash records on x86, safe and recovery modes (`[SERVICES_OK]`).
7. **User-space runtime `zxrt`:** libc subset, libzxv for user space, zxvd as a service, Z-Wasm,
   the pillars and Lingua.
8. **One native GUI everywhere:** zui as the compositor on bare metal, login and seats with the
   agreement, then the hosted app on zui's present layers (`[LOGIN_READY]`).
9. **Input breadth:** HID parser, PS/2, I2C-HID, keymaps on bare metal.
10. **Apps as processes:** `.zxa` container, sandbox from manifests, L1→L2→L3 slots, the Banker
    as the first bare-metal app.
11. **Network and trust on bare metal:** NIC to IP on x86, SNTP then TLS live, IPv6, firewall,
    WireGuard, Vinea and the swarm as services, models streamed from the disc.
12. **Platform depth:** ECAM, MSI-X, SMP, IOMMU.
13. **Security chain and distribution:** Secure Boot signing, signed model manifest, TPM measured
    boot, encrypted data partition, installer, a signed release with the golden disc published as
    a disc image (ISO or raw) once the owner settles where the weights are hosted.
14. **Device breadth:** audio, USB classes, Bluetooth, display modes, battery and thermal UI,
    then the driver VM for Wi-Fi and GPU.
15. **Close the loop:** a physical hardware lab and compatibility list, hermetic CI with an SBOM,
    and a self-hosting toolchain.

Each step is one or more small pull requests, green on GitHub before the next starts.

## 19. Decisions made

| Date (UTC) | Decision |
|---|---|
| 2026-10-09 | Apache-2.0 only; Vinea replaces Carracho; rails 555/777/888, class 811 |
| 2026-10-10 | Closed clearing default, open batch auction per jurisdiction; 0.08889% fee split 50/25/15/10; 25% to the V-Bill dividend; vault funded by a third of the reserve share, capped at 1/3 of V-Bills; founder powers behind a steward threshold; guardians sign, hold no shares (3 of 5); one fee for all; backed-only V-Bill issuance; 13:1 test target counted by mutation-killing tests; transport by compatibility adapters and link selection; holographic coding on the device only; publish to GitHub and Hugging Face when ready |
| 2026-10-12 | Own native UI, never browser-looking; palette #002911, light green text, gold #F2C230; boot and launch screens as in section 15.2; the policy engine is `zxv_policy`, never a "legal engine" |
| 2026-10-14 | GPU-first, integer-exact (R16); vector graphics everywhere (R17); every programming language from the 1950s on, each with an AI twin; every network protocol with a P2P, PQ, auditable twin; bare metal and the golden disc as the universal platform, designed by paraconsistent logic; 407 apps in three tiers; the Banker and the Hoard; astrolabe dock and icon motion by state |
| 2026-10-14 | End-user agreement 1.0 approved; California law for non-consumers |
| 2026-10-15 | Licensor SCENIC BYPASS CONSULTING, LLC (Arizona); creator and root organisation 36N9 GENETICS LLC; manufacturers partner only through ILLUMAHEART FOUNDATION or SCENIC BYPASS CONSULTING, LLC |
| 2026-10-15 | Tiamat Productive Agency AI as the local AI's brand; nested agency levels; lineage diversity; DNA and hobbies; licence allow list for shipped weights |
| 2026-10-15 | 104 spoken languages on par with coding languages, all in ASCW Golden; chiglet voice on tap only; no central OS releases (R22); bounties as the main VFV issuance path; naval ports and Sicilian accounts; Vino allotments routed by jurisdiction, software first; the fee stays 0.08889%; "allotment" wording, Crown Allotment FREE by default |
| 2026-10-15 | Consolidate; add only missing components (section 18) |

## 20. Decisions still open

| Question | Default today |
|---|---|
| Ship model weights in the golden disc release (changes "weights never attached") and where to host the 1.6 GB image | Weights fetched at first run; golden disc built by hand |
| Guardian recovery: signatures only (R15), or keep `ident`'s Shamir share of one factor | Code keeps the share; R15 says no shares |
| Commons endowment's share of the fee | Half of regenerative (5%) |
| Demurrage on idle vouchers | Not built |
| Every VFV coin a Fibonacci rung? | Only minted vouchers |
| Dividend only to active users | Not built |
| Custodian default covered from fee income? | No; loss stays in the basket |
| Bounty payout amounts, epoch cap, reviewer pay, self-fixes, minting at posting | Claude's defaults in #154 |
| Naval ports: cycle length, SUSPEND or DEFEND default, settling silent members, yield weights | SUSPEND |
| Pipe yield and any yield-linked voucher return | Off; waits for counsel |
| Mac app icon: navy 36N9 logo or gold fox on green | Navy logo |
| Trademarks: ZEDEC, ZXV, VOVINA SHAKINA held by 36N9; Tiamat, Zenny, Thunderbird, SMIC searches | Open |
| Apple Developer ID | Needed to ship signed |

Legal questions for the owner and counsel: V-Bills and Zenny chips as securities; operating the
fee and vault; money-transmitter status; the closed-loop store-credit framing; the allotment's
modes per market; inheritance and Shariah rulings; HIPAA and GDPR; the bounty leaderboards.

## 21. Where spec text and the design differ

| Spec text | Design |
|---|---|
| Guardians hold key shards | Guardians sign approvals (the code still gives each a share of one factor; section 20) |
| Sinks reflect attacks | Tarpit and isolate only |
| Forecasting silently fixes bugs | Reports; designed recovery with receipts |
| Arbitrary-precision rationals | 64-bit rationals with overflow checks |
| Clean-room PQC | Vendored upstream reference code |
| κ multiplies value or exposure | κ sets chip size; claim = deposit; overdrafts fail closed |
| Auction burns fiat | Fiat goes to the reserve or back to bidders |
| Library-only escapes regulation; a new name changes the legal category | A lawyer decides; labels are not dispositive |
| Zero-KYC agents | Owner's identity and limits apply |
| "Impossible to leak" | Protects data, not metadata |
| Four million lines of formal verification | 13:1 mutation-killing tests are a quality bar, not a certification |
| Lattice head replaces softmax | Sits beside it |
| Settlement over ham bands; steering satellite paths | Not allowed; not possible as a customer |
| V-Bills backed by every commodity ever traded | Backed by registered, haircut custodian receipts and the reserve |
| Compression multiplies value | Compression is netting; it moves less and creates nothing |
| Fixed Fibonacci bounty tiers | One signed bid judged against a fair value (the bidding model the owner chose) |
| Superintelligence | An agency of specialist AIs that check each other |

## 22. Document map

This file is the architecture; [SYSTEM_REFERENCE.md](SYSTEM_REFERENCE.md) is the per-module
reference. The others do one job each.

| Kind | Documents |
|---|---|
| Gaps and plans | [ARCHITECTURE_AUDIT_2026-10-15.md](ARCHITECTURE_AUDIT_2026-10-15.md) (this version's audit and fill plan), [BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md) with `baremetal-gaps/`, [GAP_AUDIT.md](GAP_AUDIT.md) (2026-10-10 integration gaps) |
| Subsystem specs (built) | ADAPTERS, ADAPTER_BOX, AIPI, AIRC, CARD_NETWORKS, CENTRAL_BANK, CIRCUIT_BOARD, CIRCUIT_EQUIVALENCE, COMMERCE, CRASH_RECOVERY, ENDIAN_HOPPING, EXACT_RISK, FONTS, FORKS_AND_EVOLUTION, GPU_STRATEGY, GRIDLOCK_RESOLUTION, HARMONIC_WIRE, HPC_DESIGN, HW_DRIVERS, IDENTITY_AND_RECOVERY, IPFS_NODE, IRC_GATEWAY, LANGUAGE_CATALOGUE, LEGACY_BRIDGE, P2P_PROTOCOL_TWINS, PARACONSISTENT_ENGINES, PARACONSISTENT_LEDGER, PARADOX_ENGINE, PEER_AUDIT, POWER, PQ_MATRIX, PROVIDERS, QUANTUM_NETTING, QUANTUM_SIMULATOR, QUBO_WORKBENCH, SPACES, SPATIAL_ENGINE, SPEECH_AND_TRANSLATION, SPOKEN_LANGUAGES, SWARM_ECONOMY, TENSOR_ENGINE, TIME, TLS_CERTIFICATES, TOKEN_DENOMINATIONS, VINEA, VRAM_BLOCKS, WEB4, ZXVD, `retail/*` |
| Design only | AI_MODEL, CAGE, PIT_BOSS, REGISTRAR, REGISTRY, ROSETTA_SHELL, TRADE_ROUTES, LEXICON |
| Products | MAC_APP, DESKTOP_LINUX_WINDOWS, MOBILE_AND_DEVICES, BAREMETAL, STAPLE_APPS, APP_DEVELOPMENT, APP_MODEL, APP_LAUNCH_SCREENS, DOTNET_SDK, `sdk/docs/*` |
| Process | CI, CODING_ENGINE, COVERAGE, ADVERSARIAL_AUDIT, FORMAL_INVARIANTS, `release/*` |
| Dated reports | AUDIT_REPORT (2026-10-10), ADVERSARIAL_AUDIT_2026-10-12, CI_QUALITY_AUDIT_2026-10-12, ARCHITECTURE_AUDIT_2026-10-15 |
| Explainers | `academic/*`, `supplemental/*`, [DESIGN_PHILOSOPHY.md](../DESIGN_PHILOSOPHY.md) |
| Superseded, kept for history (each opens with a pointer here) | [ARCHITECTURE_OVERVIEW.md](ARCHITECTURE_OVERVIEW.md) (2026-10-09 survey), [TURNKEY_OS_GAPS.md](TURNKEY_OS_GAPS.md) (2026-10-12, followed by BAREMETAL_OS_GAPS), and in the repository root: ARCHITECTURE.md (early M5 substrate notes; section 8.1 replaces them), ARCHITECTURE_COMPLETE.md and ARCHITECTURE_VERIFICATION.md (CURZI-8889-A, 2026-09-12), ARCHITECTURE_WHITEPAPER.md, CANONICAL.md (2026-08-04, an older tree layout), ROADMAP.md (August tracks; section 18 replaces it), SUBSYSTEM_INDEX.md and SOURCE_CODE_NAVIGATION_GUIDE.md (Appendix A and SYSTEM_REFERENCE replace them), ZEDEC_NEW_MODULES.md, DEVELOPMENT_MAP.yaml, SERVER_HANDOVER.md, CID_TRACK_LOG.md, VOVINA_SHAKINA_README.md and VOVINA_SHAKINA_WHITE_PAPER.md |

## 23. Honest notes

- "Built" means tested in CI. Nothing here has had real users or moved real money, and there has
  been no outside audit.
- The bootable images are self-tests under emulation; what people can use today is the desktop
  app, its SDKs and the phone companion.
- The real model runs and is measured, and is slower than llama.cpp on the CPU today.
- Standards are followed by design and checked against schemas or published examples; nothing is
  certified or connected.
- The 🔒 rows describe work that exists only on a thread's branch until the CI freeze lifts; if a
  branch is lost, so is that work, so the consolidation batch comes first.
- The owner maps the strata onto quantum information theory (vacuum, entanglement, topological
  protection, superposition, unitary evolution, measurement, decoherence). It is a design
  analogy: the software runs on classical computers and claims no quantum properties.

---

## Appendix A: every kernel module by stratum

From `build_system/strata.txt` at `7d4297c2`. A module whose files are pinned to different strata
is listed once, at its directory's stratum. Every module has an entry in
[SYSTEM_REFERENCE.md](SYSTEM_REFERENCE.md).

| Stratum | Count | Modules |
|---|---|---|
| S0 base | 20 | axiom_matrix, crit168, dharana, dual_space, e8, edp_risk, event_space, fractal, invproof, lpres, mixmat, onepolicy, qr, rational, rce, rmag, sephirot, surplus, ubh, zcapital |
| S1 kernel | 53 | acpi, alloc, ata, audio, blockdev, bluetooth, cellular_multikernel, choice, clock, codec, crash, deploy, display, el0_userspace, event_sched, fat32, fs, gdt, hardware, holographic, hwdrv, idt, keyboard, license, mbcomp, mm, modbind, mouse, oseq, pci, phase_coord, pic, pmm, power, qeq, qsim, quantum, recon, sched, sdk, smap, syscall, telemetry, timer, timesvc, trispace, vbe, vfs, video, virtio, wifi, yantra, zxvfs |
| S2 transport | 15 | bearer, bridge, bringup, call, constellation, event_transport, freight, ipfs_node, iphase, lattice, mesh_net, net, plnp, porter_house, stream |
| S3 trust | 28 | aproto, ascent, bios, bootattest, bootlegger, crypto_wallet, curzi, decent, ehop, hwkey, ident, identity, immigration, ipfs, loader, mage, mlkem, pqsec, provenance, pungent, reputation, robin_debanks, seats, tls, update, vinea, zab, ztp |
| S4 logic | 12 | bombsquad, dharma, panopticon, paradox, pce, peer_audit, predictive, qubo, recover, situation, spatial, tripartite_fs |
| S5 AI | 15 | ai_layer, aipi, chiglet, cotier, edgeclus, epu, harmonic, megarom, speech, swarm, tensor, vblock, voice, web4, xlate |
| S6 settlement | 42 | abacus, airc, battering_ram, capmkt, cardnet, cbank, concord, count_house, cred, devmesh, dispute, engine, estate, finance, finance_markets, fxepoch, grants, gridlock, logistics, market, mesh_token, mint, outbox, pay, provider, qnet, qnote, royalty, settle, sicily, svault, vauction, vbond, vderiv, vena, vhub, vino, vino_stores, vswap, xrisk, zenny, zstats |
| S7 adapters | 31 | acct, capctl, crown, evolve, financial_fabric, fwadapt, gematria, hdcm, hwemu, intent, interspace, iso20022, juris, kycwrap, legacy, legal_engine, lightningrod, lingua, mfg, ministry, orbital_compat, orbital_elevator, orbital_fabric, policy, privacy, rur, sdk_bridge, sovereign_node, sutra, taxhook, treaty |
| S8 above | 45 | appkit, apps, art_studio, audiogenomics_pro, bootfeat, broker, browser, cards, chronicle, community_chest, denconnect, desktop, dualtrack, emu, font, fusion, games, holodeck, hypercube, i18n, icon, launch, macgyver, nlb, pirate_apps, pirate_fleet, prism_break, pterm, quest, reality, refinery, shimmer, social, social_spaces, subterm, superpos, synthesis, terms, theme, tolvovina, wyrmgate, wyverneye, xedit, zui, zxpkg |

Modules on local branches or drafts, not yet in this list: `navport`, `allotment`, `jroute`,
`media` and `vaudio`, plus new files inside `evolve`, `chiglet`, `pay`, `speech` and `zui`
(sections 12 to 15).
