# ZXV / ZEDEC pqOS — System Map, Composition Debt, and the Abstraction Roadmap

_2026-08-09. 160 modules, 424 `.c` files, 139 test suites, ~3400 gated checks._

---

## 0. The finding that matters most

The modules are good and the modules are **not connected**. That is not a stylistic
observation; it is measurable, and it is the single biggest gap in the system:

| seam | state |
|---|---|
| `vena_execute_contract` → `zab_execute` | **STUB** — increments counters, returns 0 |
| zmedia S+/S−/S0 → `zxvfs_tri_write` | **NOT WIRED** — produces triad members, stores none |
| bombsquad → real subsystem margins | **NOT WIRED** — watches test fixtures only |
| ZXVFS placement → `zorder` | **NOT WIRED** — the measured 32% locality win is unused |
| shell layout → `display.scale_permille` | **NOT WIRED** — negotiates scale, ignores it |
| mrschema → `sutra` | wired |

Five of six. Each module is individually correct, tested, and freestanding-clean.
Together they are a parts bin, not a system — and this OS's whole thesis is that
the parts are one nonlinear decentralized whole. **Composition is now the
priority, ahead of any new module.**

The pattern behind the mistake: a module with its own passing test suite *feels*
finished. It is not. A component that nothing calls has not been shown to work in
the system — only in a harness that was written to agree with it.

---

## 1. What exists (verified this session unless noted)

**Boot & architecture** — all 5 arches boot: arm64 (BOOT_OK + EL0 + desktop),
x86_64 (ring-3 + full subsystem set), riscv64 + riscv32 (OpenSBI/S-mode),
arm32 (`-M virt`). EFI payloads BOOTX64/BOOTAA64 verified on OVMF/AAVMF; one
universal disc boots two architectures.

**Storage (new)** — ZXVFS v2: copy-on-write, extent allocator, positional I/O,
redo journal, 256 files, 4 MB region. `zxvfs_tri`: the triad is the storage unit,
proven by exhaustive crash injection. Crash fuzzer with an fsck oracle + temporal
lag detection.

**Trust (new)** — ZAB capability verifier (capabilities *derived* from bytecode,
no GRANT opcode), ZAB VM (per-instruction capability enforcement), ZXI inverse
witness (a proven inverse must *apply*), offline-root release signing with key
pinning.

**Media / display (new)** — tri-space codec (lossy S+ plus S− restores exactly),
display negotiation 640×480 → 8K with UI scaling.

**Diagnostics (new)** — bomb squad: margin-based preemptive fault detection with
fuse-length estimation and defusal during the burn.

**Pre-existing and substantial** — Sutra language (lexer/parser/runtime + capital,
rails, chiglet bindings), tri-space packaging (`trispace`, `zxpkg`), economy
(vino, vena, triple ledger, rails, bridge), emulation (7 CPU cores + machines),
desktop shell, network stack, ML-KEM/Ed25519/SHA-256, `zorder` fractal locality
(measured 32% less data movement), `dimfold`, ZCA card language.

---

## 2. Composition debt — the actual next work

Ordered by how much each proves the system is one thing:

1. **`vena` → ZAB VM.** Contracts are the OS's execution story and the runtime is
   a stub. Wiring it makes ZAB, capabilities, tri-space and the ledger one path.
2. **Bomb squad → real margins.** Watch `zxvfs_free_sectors`, rmag slot pressure,
   surplus, ledger drift, phase-tick jitter. Today it is a mechanism with nothing
   real to watch; this is what makes it a *system* diagnostic.
3. **zmedia → `zxvfs_tri`.** An image should be *stored as a triad*, so the
   codec's S−/S0 pass through the same binding, quarantine and capability rules
   as code. This is the cleanest proof that tri-space is universal, not
   code-only.
4. **ZXVFS placement → `zorder`.** Morton-order extent placement so related
   triad members land near each other. `zorder` already measures the benefit;
   the filesystem simply never calls it.
5. **Shell → display scale.** The desktop negotiates a scale and then lays out
   as if it had not.
6. **Game Master → the FS fuzzer.** The harness drives the whole booted system;
   the fuzzer drives storage. Neither drives the other.

Each is small. Together they turn 160 modules into one machine.

---

## 3. Still missing (not yet built)

- **Real codecs for foreign media** — no JPEG/PNG/AV1/Opus decoder. `zmedia`
  decodes its own format only. This gap is unchanged.
- **Entropy coding** in zmedia (Huffman/arithmetic), chroma, colour transforms.
- **Production root key** — ceremony built; the key must be minted air-gapped by
  the owner. Deliberately not done in-session.
- **riscv64/riscv32 EFI** — firmware-blocked (no RISC-V UEFI on the build box).
- **Role shadowing** in `zxvfs_tri` so triad *rewrite* is old-or-new atomic
  rather than fail-closed to quarantine.
- **Narrative/social annotation** for MegaROM — cannot come from ROM bytes; needs
  a human or licensed pipeline.
- **Multi-surface compositing, DPI-aware fonts, damage tracking** in the shell.

---

## 4. Abstraction roadmap — after the base kernel passes

The kernel is a machine an expert can drive. These layers are what make it a
machine a *person* can drive. Order matters: each rests on the one below.

**L1 — Syscall surface / stable ABI.** One documented, versioned entry point.
Today callers reach subsystems directly, so nothing can change without breaking
everything. Nothing above this layer is safe to build until it exists.

**L2 — Capability handles.** Userland holds *handles*, not pointers, with ZAB
capabilities attached. The verifier and VM already model this; L2 extends it
past the kernel boundary.

**L3 — Object/resource model.** Triads, files, devices, ledgers and contracts
behind one uniform open/act/close with the tri-space rules enforced centrally
rather than per-subsystem.

**L4 — Service layer.** Long-lived services (storage, display, network, ledger,
Chiglet) with supervision — the bomb squad becomes their health monitor rather
than a library.

**L5 — Language bindings.** Sutra as the first-class scripting surface; then a C
API; then whatever else. Sutra already parses and runs, so this is binding, not
invention.

**L6 — Application framework.** AppKit over L3/L4: windows, documents, triad-native
save/load, undo backed by ZXI witnesses. Undo becomes a *system* guarantee rather
than a per-app convention.

**L7 — Human surface.** Settings (including display mode selection — the mode
table exists and nothing exposes it), file manager, installer UX, first-run.

**L8 — Remote/distributed.** See §5.

---

## 5. Remote display — yes, and it is the natural shape of this system

Worth doing, and worth doing properly rather than as a bolt-on.

The composition step already gives most of it: once the desktop composes into a
*negotiated* mode rather than a hardcoded one, the display becomes a **surface
the kernel renders into**, not a device it owns. A remote surface is then the
same abstraction with a longer wire.

Three honest options, cheapest first:

- **Frame streaming.** Server composes; client receives frames and returns input.
  Simple, works everywhere, bandwidth-hungry. **Damage tracking** (send only
  changed regions) is the difference between usable and not — and it is a
  prerequisite the shell does not have yet.
- **Command streaming.** Send drawing *operations* instead of pixels; the client
  rasterises. Far less bandwidth, needs the renderer split from the compositor.
- **Triad transport (the ZXV-native one).** A frame is S+ (the lossy image, cheap
  to send) plus S− (the residual, sent only when exactness is needed) plus S0
  (unresolved colour/gamma). **Degrade by dropping S−, restore by sending it** —
  which is exactly what the tri-space codec already does, and exactly the
  "adaptive quality" problem every remote-desktop protocol solves ad hoc.

That third option is not a metaphor: `zmedia` already produces those three
members and `zxi_verify` already proves the restoration. What is missing is the
transport and the damage tracking, not the codec.

**Prerequisites, in order:** damage tracking in the shell → surface abstraction
(local and remote are the same thing) → transport over the existing net stack →
input round-trip → the triad quality ladder. Security is not optional here: a
remote surface is a remote *input* path, so it needs the signed-session
treatment before it is exposed, not after.

---

## 6. Recommended sequence

1. **Composition debt (§2)** — highest value, all small, proves the thesis.
2. **L1 syscall surface** — everything above it is unsafe to build first.
3. **Damage tracking + surface abstraction** — unblocks both a better local
   desktop and remote display.
4. **L2–L4** — handles, object model, services.
5. **Remote display over the triad ladder.**
6. **Foreign codecs** — independent of all the above; schedule when the media
   story needs to read other people's files.
