<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# User-space runtime and app model: bare metal vs hosted

Audit of origin/main at 4bea0160 (2026-10-14). Read-only. Builds were run in a
scratch copy: `test_zxapp.sh` 481/481 and `test_views.sh` 137/137 pass. I also
cross-compiled the toolkit for `aarch64-none-elf` freestanding (results below).

## Bottom line

On bare metal there is **no user-space app platform yet**. What exists:

- an EL0 process model on arm64 (per-process page tables, W^X, preemption, 13-call ABI);
- one signed sample program (`hello.zsp`) that `run` loads from ZXVFS.

None of the 144 apps, the zui toolkit, the `.zxapp` / blocks machinery, the language
runtimes, zxvd or the AI swarm runs as a user process on bare metal:

- They are not linked into either bare-metal image.
- No binary anywhere, hosted or bare metal, instantiates `zui_wm` or `zxapp`
  outside tests.
- The hosted desktop app is still the loopback HTML window served by zxvd.

The good news: the app stack is close to freestanding already.

- `zxapp.c`, all of `zui_*` (except three files with include issues), `autoview.c`
  and the views compile for `aarch64-none-elf -ffreestanding`.
- `zxapp.c` needs only `memcpy`.
- The app models and the zxm link need 8 string functions: `strlen`, `memcpy`,
  `memset`, `strcmp`, `strrchr`, `strncmp`, `memmove`, plus the stack protector.
- `zxm_link_t` already abstracts its transport (`zxm_send_fn`).
- zxvd already has a transport-free entry point (`zxvd_rpc_handle_line`).

So the missing piece is mostly the **process, runtime and packaging layer**, not
the apps.

## What I checked, with evidence

### 1. What a bare-metal user program links against

| Item | State | Evidence |
|---|---|---|
| Userland libc / crt0 | **missing** | No crt0, libc or newlib anywhere. `hello.c` is `-nostdlib`, with an inline `svc` per call (`kernel/userapp/hello.c:27-55`, `build_hello.sh:17`). `kernel/include/freestanding_stubs/{string,stdlib,stdio}.h` exist for the kernel, but app models fail against them (`memset` / `strrchr` undeclared). |
| Syscall stubs | **numbers only**, by design | `kernel/include/zxv_syscall_user.h:1-30` exports only `ZXV_SYS_*` and `ZXV_ABI_VERSION`, with no prototypes or structs. The table has 13 calls: `kernel/include/zxv_syscall_abi.h:83-95` (EXIT, WRITE (one byte), GETPID, YIELD, SLEEP, SEND, RECV, OPEN, CLOSE, READ, EXEC, ABI_VERSION). |
| Calls actually served, arm64 | 10 of 12 | `el0_userspace.c:995-1157`: READ is one UART byte, non-blocking (`:1005-1019`). OPEN and CLOSE return ENOSYS (`:1143-1153`). EXEC passes a string to the kernel shell (`:1021-1047`). |
| Calls actually served, x86_64 | 6 of 12, one ring-3 self-test task only | `kernel/arch/x86_64/ring3.c:152-160` (`x86_provides`), `:141-145` ("no process table or filesystem bound to ring 3"). **The golden disc's main consumer target, x86_64, has no user-space process model.** |
| No heap, mmap, brk, spawn, wait, time, entropy, shm, surface or input-event call | **missing** | ABI table above. |
| Memory per process | 64 KB code, 64 KB stack, no heap | `el0_userspace.h:63-67`. |
| Pages for all processes | 2 MB bump pool shared by all, **never freed** | `el0_userspace.c:65-76` (only `proc_pt_init` resets it, `:102`). |
| Processes and IPC | 32 processes; IPC 256 B messages, 8 deep | `el0_userspace.h:33-37`. |
| Userland libzxv, the frozen C ABI | **hosted only** | `sdk/libzxv/src/*.c` use `pthread`, `calloc` and `stdio`. The Makefile has no freestanding or EL0 target. **But** `sdk/embedded/port` (zxp: allocator, mutex, entropy, clock hooks, `PORT=baremetal`) already builds libzxv unmodified for MCUs via `-include zxp_prelude.h` (`sdk/embedded/README.md` "How libzxv is built here"). A `zxp_el0.c` port is the shortest path to libzxv in EL0. |
| `m5_api.h` (the old SDK facade) | declarations without definitions (already in APP_DEVELOPMENT §4.1) | Unchanged. It is not the frozen ABI; `zxv_api.h` is. |

**Can the 144 apps run as separate processes on bare metal? No.**

- **Linking.** `build_system/Makefile.arm64` and `Makefile.x86_64` contain no `src/zui`
  and no `zxapp` sources (0 matches). They do link `apps/apps.c` (an old
  text-screen shell app, `Makefile.arm64:479`) and the in-kernel compositor
  `desktop/zxv_shell.c` (`:387`).
- **Size.** The freestanding zui + zxapp + autoview + font objects are
  **~375 KB text+data at -Os** (measured), against a 64 KB code limit and a 2 MB
  pool for every process.
- **Framebuffer.** A 1920x1080 XRGB surface is 8 MB, and no syscall maps a
  framebuffer or shared memory.
- **Model traffic.** `ZXM_RESP_MAX` is 320 KB (`zxm.h:33`), but IPC carries 256 B.

The 144 apps are models + views that talk to zxvd. They exist for 5 apps:

- burrow, glint, quillon, spindle and murmuration (`kernel/src/apps/*_model.c`,
  `kernel/src/zui/views/`);
- plus `display` and `autoview`.

They run only in host tests. Of the 144, only `murmuration.zxapp` and
`fee_check.zxapp` (a demo) have manifests. One block exists
(`apps/blocks/swarm_shares.zxblock`).

### 2. Packaging, signing, install, sandbox, blocks, provenance

| Item | State | Evidence |
|---|---|---|
| Signed executable envelope | **exists**: ZSP1 and ZSP2 (Ed25519; v2 signs version, arch, ABI, caps, key-id, identity, with a rollback floor) | `kernel/src/loader/zsp.h:23-97`. |
| Loader uses v2 | **no** | `run` calls `zsp_verify` (v1-strength, no version/arch/caps enforcement) and accepts an unsigned ELF with a warning (`kernel_main_arm64.c:3797-3816`). |
| Manifest caps → process caps | **missing** | Every loaded app gets `CAP_APP_DEFAULT` = WRITE\|PROC regardless of the signed `caps` (`el0_userspace.c:707`, `el0_userspace.h:103`). |
| Native triad container | **exists, unused by the loader** | `zxpkg` (`.zxvc` S+ / `.cedez` S- / `.cedec` S0, seal, release verify over ZSP2): `kernel/src/zxpkg/zxpkg.h`. It is in `Makefile.arm64` but not called from `run`. |
| Daemon admission | **exists, unwired** | Immigration visas RESIDENT / WORKER / TRANSIT (`kernel/src/immigration/immigration.h:13`), in the image (`Makefile.arm64:370`), with no call from `run`. |
| Derived capabilities | **exists for ZAB only** | `kernel/src/zab/zab.h`: capability is implied by opcode, so it cannot be misdeclared. This is the right principle for app capabilities too (see design D4). |
| A/B app update | exists | `loader/abupdate.c` (tested). |
| Install flow / app store → package | **missing** | No `apps.*` / `pkg.*` / `block.*` among zxvd's 228 methods (`api/zxvd.openrpc.json`). The golden disc lists `assets/apps → ZXV/APPS` (`build_system/golden.contents`), but `assets/apps` does not exist. |
| `.zxapp` manifest, blocks, contracts, compose, intents | **exists (library, host-tested)** | `kernel/src/apps/zxapp.h` A1-A5, B1-B4, C1-C3, I1-I3. Hard limits: `ZXAPP_BLOCKS 64`, `ZXAPP_USES 8`, `ZXAPP_LOGIC_MAX 32` (`zxapp.h:150,155,157`). A 407-entry three-tier catalogue does not fit one registry. |
| Any app as a block | exists (C1) | `zxapp.h` C1. |
| Provenance per block | exists (C2: author, id, version, CID, share in permille) | `zxapp_provenance`. **Not signed**: `author` is free text in the manifest, not bound to a key. |
| Content IDs | exists (C3: sha256 of the manifest text) | Pins cover only inline (`<<`) code; path-based logic is unpinned (documented). |
| Sandbox profile from manifest | **missing** | No `needs` / `cap` directive in the A1 grammar. Already flagged in OS_FEATURE_AUDIT; I confirm it, and add that ZSP2 already carries a signed caps field the loader ignores. |
| Two parallel CID function registries | gap | zxapp blocks (C3) and AIPI functions (`docs/AIPI.md`, z-contract + Z-Wasm, hexagram job tags) both name code by content, with different contracts and tags. They should be one registry. |

### 3. The "three tiers of initial applications"

The only authoritative definition is the owner's note in
`/mnt/project-files/zxv-brand/app-icons/make_tier_icons.py:1-4` (2026-10-14):

| Tier | Numbers | Owner's words | Icons | What exists |
|---|---|---|---|---|
| L1 | 001-144 | "base modules" | gold on green, top level | `APP_LIST_144.md`: names and roles for all 144 (38 critical). Models + views for ~5-8 (some on branches). Manifests for 1 (murmuration). |
| L2 | 145-271 | "modules on L1" | green on gold, `l2/` (127 files) | Icon concepts only (`DRAGON_SERIES_145_225.md`, `_226_271.md`: dragon roles such as archivist, smith, accountant). **No functions, contracts or names as software.** |
| L3 | 272-407 | "apps compiled from modules" | own colours on gold, `l3/` (136 files) | Icon concepts only (`CRYSTAL_SERIES_272_407.md`). **No definitions.** |

"Layer 2/3" in `ARCHITECTURE_COMPLETE.md` and TURNKEY_OS_GAPS mean other things
(kernel layers, the driver VM). They are not these tiers.

**Mapping to the existing packaging (my proposal):**

- **L1 = system blocks.** First-party, native C precompiled into the golden image.
  Each exports `provides` contracts (C1) so it can be used as a block. Signed by the
  release root and admitted as Immigration RESIDENT.
  Note the naming mismatch: the repo calls the 144 "apps", the owner calls them
  "base modules". Each L1 item should be both, a visible app and an exported block
  set, which C1 already allows.
- **L2 = composite modules.** Apps-with-provides that `use` L1 exports. No
  mandatory view. Shipped as portable blocks (pillar source or Z-Wasm), CID-pinned,
  admitted as WORKER.
  zxapp B1 forbids blocks using blocks, so every L2 module must be an app-as-block
  (C1), not a `zxblock`. That is fine, but should be stated.
- **L3 = assembled apps.** Manifest + `view auto` (or a declarative view) + `use`
  lines over L1/L2. Little or no code of their own. This is the no-code tier and
  the one end users will create. WORKER, or TRANSIT when unsigned or locally made.
- **"Precompiled per tier."** Proposed meaning:
  - L1: native machine code per arch.
  - L2 and L3: validated, content-addressed portable artifacts (pre-parsed pillar
    IR, or `.wasm`), compiled once at package time and cached on the device.
    Sources are kept beside them for provenance.

### 4. Language runtimes: where each runs

`docs/LANGUAGES.md` covers human languages (i18n, CLDR), not programming
languages. The programming-language basket is in `APP_MODEL.md` and `zxapp.h`.

| Runtime | In-kernel (bare metal) | EL0 user space | Hosted | Evidence |
|---|---|---|---|---|
| C toolkit (zui, views, models) | no (not linked) | no | tests only; the shipped hosted UI is HTML | Makefiles above; `kernel/arch/hosted/zxv_ui.html` |
| zxapp core (manifests, blocks, join) | no | no | tests + CI (`zxvd.yml:109-113`) | compiles freestanding (measured) |
| Sutra (COBOL pillar) via zxapp | no | no | yes (`libzsutra.a`, malloc) | `zxapp_sutra.c:54`, `test_zxapp.sh` |
| Separate in-kernel Sutra (lexer, parser, runtime) | **yes, EL1** | no | tests | `Makefile.arm64:525-531`. Not the zxapp runtime: **two Sutra implementations.** |
| Eve (Ada), KRON (APL/K), Trebago (Fortran), MYRA (MUMPS) | no | no | yes (`libz*.a`, each uses malloc) | `zxapp_{eve,kron,trebago,myra}.c` include `<stdlib.h>` |
| Z-Wasm (wasm3, fuel, no-float) | no | no | yes; Tri-Isolate gives per-plugin processes on POSIX | `sdk/kits/z-wasm/src/zw_host.c:291-394` (calloc), `sdk/kits/README.md` |
| `orbital_compat_wasm` | yes, EL1 | no | no | a "lowering" model of 143 lines, **not a wasm interpreter** (`orbital_compat_wasm.c`) |
| Tensor engine + swarm | yes, EL1: **boot self-check only** | no | yes (zxvd `swarm.ask`) | `Makefile.arm64:536-555`; no `zt_model_*` call under `kernel/arch/{arm64,x86_64}` |

The paraconsistent join exists:

- **How it works.** `zxapp_call_witnessed`
  (`zxapp.h` A5) returns TRUE / BOTH / NEITHER. BOTH leads to `ZXAPP_E_CONFLICT`
  (hold).
- **What is wrong with it.** It is four-valued only. It does not use the hexagram
  tag or `lpx_action`, so a BOTH cannot distinguish a converging dispute
  (WAIT) from a stuck one (ESCALATE).
- **Inconsistent with AIPI.** AIPI already tags its jobs with the 64-state tag.

Adding more languages is owned by the "Every legacy language plus AI twin"
thread; it is not duplicated here.

### 5. No-code / low-code

What exists:

- manifests;
- `use` / compose;
- apps-as-blocks;
- `view auto` (a form per contract: whole-number inputs and a Run button,
  `kernel/src/zui/views/autoview.h`);
- intents;
- provenance;
- the witnessed join.

That is the correct core.

Missing:

1. **A builder.**
   - No app lets a person browse the block catalogue, wire one entry's output to
     another's input, and save a manifest.
   - The nearest things are Spindle (#22, calls zxvd methods by hand) and
     Treadle (#24, automation, not built).
2. **Data flow between entries.** A manifest cannot say "feed `a.out` into
   `b.x`". There are no `flow` / `bind` lines, so every composition needs a C model.
3. **Inputs beyond integer vectors.**
   - `zxapp_arg_t` is `int64_t[]` only.
   - There is no text, record, file, image or decimal-with-scale type.
   - Most consumer apps need text and files.
4. **Triggers.** No "on event / on schedule / on intent run X" in the manifest.
5. **Catalogue by CID.**
   - Publish and fetch are listed as "the next step" (`APP_MODEL.md`).
   - The registry holds 64 blocks in RAM.
6. **AI drafting.** The swarm drafting a manifest from plain words is a natural
   fit for zxm M3 (held AI). It is not built.
7. **Declarative views.** Only `auto` or hand-written C. There is nothing in
   between, such as a layout template bound to contract names.

### 6. zui toolkit and compositor

- **zui is a library inside the app's address space.** `zui_wm.h` WM1-WM5: the
  manager "asks [the view] to paint into its content rectangle", a function call
  in the same process. `zui_present.h` P1-P4 (damage rectangles, XRGB8888, the app
  owns memory) is a good basis for a protocol, but **there is no cross-process
  window protocol**. TURNKEY row 14's statement is still true.
- **Bare metal uses a different compositor.** `desktop/zxv_shell.c` is the live
  bare-metal one, in-kernel. It is fixed to 5 dock apps (`zxv_shell.h:18`) and
  draws through vbe/ramfb. `desktop/desktop.c` is a geometry model only
  (`desktop.c:425-436`).
- **Hosted present backends exist:** Metal, Vulkan, D3D11, X11, Win32
  (`kernel/arch/hosted/zui_present_*`). No shipped hosted binary uses them for the
  app windows yet: ZXVD.md "Honest limits" says the window is still the loopback
  page, even in the Mac app.

### 7. zxvd: bare metal or hosted?

- **Hosted only.** It is `zxv_host.c -DZXVD` + `zxvd_rpc.c` + method groups
  (about 12.3k lines in `kernel/arch/hosted/zxvd_*.c`), linked to `libzxv.so`.
  It uses a UNIX socket with `SO_PEERCRED` (`docs/ZXVD.md` "The socket").
  `zxvd_rpc.c` includes `stdio`, `stdlib` and `errno`.
- **Nothing on bare metal serves the same 228 methods.**
- **The seams for one RPC surface already exist:**
  - `zxvd_rpc_handle_line(line, len, out)` (`zxvd_rpc.h:393`) is transport-free;
  - every app model goes through `zxm_send_fn` (`zxm.h` M1).
- **So one surface is achievable.** Hosted: socket. Bare metal: a kernel IPC
  channel to a zxvd EL0 service built on libzxv-el0.

### 8. The AI swarm on bare metal

- **Where it runs.** The tensor engine and swarm are linked into the kernel at
  EL1, but run only a boot self-check (`[AI_OK]`, `kernel_main_arm64.c:512, 2922`).
  The golden disc carries the models and the EFI stub re-hashes them
  (`mk_golden_disc.sh:9-13`, `kernel/arch/efi_models.h`).
- **No inference path on bare metal.** Nothing loads a model for generation.
- **Isolation.** Running GGUF parsing and inference at EL1 would put an untrusted
  file parser in the kernel.
- **Recommendation: a user-space service.** The model should be a read-only
  shared mapping, with budget and tag handled by `zxv_budget_gate` /
  `zxv_answer_tag` as on hosted.

## Corrections to existing docs

- **APP_MODEL.md**, "The same code runs hosted and on bare metal". True of the
  source (it compiles freestanding, as measured). False of the product: zui and
  zxapp are not built into any bare-metal image.
- **APP_DEVELOPMENT.md §4.5**, "There is no app manifest". Now stale: `.zxapp`
  exists. Still true: there is no packaging, install or capability step.
- **TURNKEY_OS_GAPS row 16**, "processes are built into the kernel
  (user_procs.c)". Partly stale on arm64: an ELF from ZXVFS loads as EL0 via `run`
  (`kernel_main_arm64.c:3782-3832`). Still true on x86_64. Spawn and wait are still
  missing.
- **OS_FEATURE_AUDIT**, "Per-app sandbox profile at install: missing". Confirmed.
  Add: a signed caps field already exists (ZSP2 `caps`) and the loader ignores it.
  `run` also uses v1-strength verification and accepts unsigned ELF.
- **docs/LANGUAGES.md** is about human languages; the programming-language basket
  is documented in `APP_MODEL.md`.

## Ranked gaps

Sizes are rough: S < 500 lines, M 500-2k, L 2-6k, XL > 6k. Each is about one
engineer-week per 1k lines, plus tests.

| Rank | Gap | Status | Evidence | Size | Depends on |
|---|---|---|---|---|---|
| 1 | Real EL0 process and memory model: page allocator with free, per-process heap (`mem.map` / `unmap`), larger code region, spawn / wait / kill | partial (arm64 MVP), missing (x86_64) | `el0_userspace.c:65-76,707`; `el0_userspace.h:63-67`; `ring3.c:141-160` | L | kernel `mm/` allocator |
| 2 | x86_64 parity: process table, ELF load, scheduler at ring 3 (the golden disc's consumer target) | missing | `ring3.c:141-145` | L | 1 |
| 3 | Channel IPC with shared memory (ports, 4 KB+ messages, shared ring), replacing 256 B / 8-deep queues | missing | `el0_userspace.h:36-37` | M | 1 |
| 4 | Userland runtime "zxrt": crt0, ~20-function libc subset, syscall wrappers, `zxp_el0` port so libzxv builds unmodified for EL0 | missing | no crt0; `sdk/embedded/port` shows the route | M | 1, 3 |
| 5 | Loader enforcement: `zsp_verify2` / `zxpkg_verify_release` only, refuse unsigned outside dev, caps from signed header ∧ derived, Immigration visa per tier | partial (pieces exist, unwired) | `kernel_main_arm64.c:3797-3816`; `zsp.h`; `zxpkg.h`; `immigration.h` | S-M | 1 |
| 6 | One app container + manifest capability lines (`needs`), sandbox profile from manifest, four-valued grant | missing | zxapp A1 grammar has no caps; OS audit | M | 5 |
| 7 | zxvd on bare metal as an EL0 service, the same `zxvd_rpc_handle_line`, over channel IPC; zxm transport for EL0 | missing | `zxvd_rpc.h:393`; `zxm.h` M1 | L (port plus libc subset) | 3, 4 |
| 8 | Window protocol: app-owned shared surface + damage + input events; compositor = `zui_wm` as an EL0 service owning the framebuffer; retire in-kernel `zxv_shell.c` | missing | `zui_wm.h` WM1; `zui_present.h` P1-P3; TURNKEY row 14 | L | 1, 3 |
| 9 | Pillar runtimes and Z-Wasm in user space on bare metal (link into zxrt via the zxp allocator); unify the two Sutras | missing | `zxapp_*.c` stdlib; `zw_host.c` calloc; `Makefile.arm64:525` | M (mostly build) | 4 |
| 10 | Witnessed join upgraded to the 64-state tag + `lpx_action`; app lifecycle phases; one CID registry with AIPI | partial (4-valued only) | `zxapp.h` A5; `docs/AIPI.md` §10 | S-M | none |
| 11 | AI swarm as an EL0 service on bare metal (model mapped read-only, budget gate, answer tag) | missing (self-check only) | `Makefile.arm64:554`; no `zt_model_*` in arch | M | 1, 3, 7 |
| 12 | Hosted desktop on native zui (replace the HTML window), the same app binaries via zxrt-hosted | partial (present backends exist, unused) | ZXVD.md honest limits; `kernel/arch/hosted/zui_present_*` | M | 8 (protocol), 6 |
| 13 | No-code: builder app, `flow` lines, typed inputs (text / record / file / decimal), triggers, declarative views, AI-drafted manifests | partial (compose + `view auto`) | `autoview.h`; `zxapp_arg_t` | L | 6, 10 |
| 14 | Catalogue by CID: publish / fetch / install over IPFS / Vinea; registry beyond 64 entries; signed authorship (C2 author bound to a key) | missing | APP_MODEL "next step"; `ZXAPP_BLOCKS 64` | M | 6, 10 |
| 15 | Tier content: L2 (127) and L3 (136) exist as icons only; contracts and manifests for L1 exports | missing | icon dirs; `*_SERIES_*.md` | owner + XL | 6, 13 |
| 16 | Golden disc carries app packages (`assets/apps` absent) and an install / seed step | missing | `golden.contents`; `ls assets` | S | 5, 6 |

## Designs for the top gaps

### Design principles

- **Tags on units of work, not on every syscall.** This follows lpres_hex X1. Hot
  paths (yield, write, map) keep plain integer status, which is the conventional
  choice and the right one.
- **Paraconsistent tags drive decisions about authority and trust:**
  - install, launch, capability grant;
  - block calls witnessed in two languages;
  - service requests that move money or data across spaces;
  - AI output.
- **One action table.** Every such decision ends in `lpx_action(tag)` →
  PROCEED / WAIT / REFUSE / ESCALATE / ROLLBACK. The kernel never invents a fifth
  outcome.
- **Base codes.** lpres numbers states N0 T1 F2 B3 and lpx uses F0 N1 B2 T3.
  Always convert with `lpx_base_from_lpres`; never add a third numbering. The
  zxapp `ZXAPP_*` truth values equal libzxv's `ZXV_LP_*`, which equal lpres.
- **Integer only.** Every value crossing the boundary is an integer or an exact
  num/den, as zxapp A3 already requires.

### D1. One app container: `.zxa` = a zxpkg triad + ZSP2 release (no new format)

Reuse `zxpkg` instead of inventing a format. One `.zxa` is a release of three
members, sealed by `zxpkg_seal` and signed by `zxpkg_verify_release` (ZSP2).

| Member | Role | Contents |
|---|---|---|
| S+ `.zxvc` | what it does | a small index (TLV): the manifest text (`zxapp 1`, unchanged grammar plus `needs` lines, D4), then each block artifact keyed by its CID: pillar source or pre-parsed IR, `.wasm`, or a native ELF per arch (L1 only), and optional view code |
| S- `.cedez` | undo | uninstall and data-migration inverse: which files and spaces it created, the schema-downgrade steps, the previous release CID for A/B (`abupdate.c`) |
| S0 `.cedec` | unresolved | capabilities requested but not yet granted, open BOTH conflicts carried from a previous run, pending consent records. Tri-Space requirement 3 already says S0 holds no production capability, which is exactly a "held" bucket |

Why the triad and not a zip:

- The seal makes substitution between members detectable.
- S- is the rollback path: `lpx_action == ROLLBACK` is answered by the package's own
  S-.
- The S0 member gives held state a place on disk.

The ZSP2 header already signs version, arch, ABI, caps and identity. Use
`arch = ZSP_ARCH_ANY` for a portable (L2 / L3) release and a specific arch for L1
native code.

### D2. One runtime ABI "zxrt", identical hosted and bare metal

The smallest surface that runs the same app binary in both places is three
layers.

1. **Portable code (L2 / L3 blocks, and any third-party app logic) uses no
   syscalls.**
   - It is a zxapp entry: pillar IR or Z-Wasm, with integers in and exact values
     out.
   - Its only effects are host calls that the runtime grants (Z-Wasm already gates
     imports).
   - This code is byte-identical on every platform: one artifact and one CID. The
     same `.wasm` already runs on hosted zxvd (AIPI).
   - **Convention is best here.** WebAssembly is the right universal binary. Its
     validation, metering and no-float rule are already in `sdk/kits/z-wasm`. ZXV's
     part is the contract (z-contract / zxapp B2), the witnessed join and derived
     capabilities, not a new bytecode.
   - ZAB stays what it is, a total policy language, and does not become an app ISA.
2. **The app host process (zxrt) is native, per platform, and identical in
   source.** It holds:
   - `zxapp.c`;
   - the pillar runtimes and Z-Wasm (wasm3), allocating through the zxp
     allocator;
   - zui and the views;
   - the zxm link.

   It is built from the same files three ways:

   | Build | Where it runs | Links against |
   |---|---|---|
   | (a) | EL0 / ring 3 | `zxrt-crt0` + `zxlibc-min` + `zxp_el0` |
   | (b) | macOS, Linux, Windows | libc and `zxp_posix` (already exists) |
   | (c) | in zxvd, for headless use | as (b) |

   An app process is one zxrt instance with one `.zxa` loaded. L1 first-party apps
   are additionally linked natively into zxrt images per arch. That is the
   "precompiled into the applications" the owner describes.
3. **Services: one RPC surface.**
   - zxrt talks to zxvd only through `zxm_send_fn` (JSON-RPC 2.0, already the
     contract for 228 methods plus `api/zxvd.openrpc.json`). Hosted it uses the
     UNIX socket; bare metal uses a channel (D3) to the zxvd EL0 service, which
     calls the same `zxvd_rpc_handle_line`.
   - JSON lines are not the fastest encoding, but they are already the frozen
     contract, CI checks them, and they are bounded (16 KiB request). Keep them.
   - Add a binary fast path only for bulk data (pixels, file bytes, tensors) via
     shared-memory handles passed in the JSON (`"shm": <handle>, "len": n`).

Kernel syscalls zxrt needs (bare metal). This is about 14 new table rows, as
`X(...)` lines in `zxv_syscall_abi.h` with numbers only (keeping the "no structs"
rule):

| Area | Calls | Cap |
|---|---|---|
| Memory | `MEM_MAP(len, prot)` and `MEM_UNMAP(va, len)` | PROC |
| Channels | `CHAN_OPEN(name)`, `CHAN_CALL(ch, req_shm, len, resp_shm, cap)` (send + wait reply, the RPC primitive), `CHAN_SERVE` / `CHAN_REPLY` for services | IPC |
| Shared memory | `SHM_CREATE(len)` and `SHM_GRANT(shm, pid, prot)` | IPC |
| Processes | `PROC_SPAWN(pkg_handle)`, `PROC_WAIT`, `PROC_KILL` (own children only) | EXEC (split from shell EXEC) |
| Misc | `CLOCK_NOW` (phase ticks), `ENTROPY(buf, len)` | NONE / PROC |
| Events | `EVENT_WAIT(mask, timeout)` (input, channel, timer), replacing the busy `READ` + `SLEEP` polling | PROC |
| Files | `OPEN`, `READ` and `CLOSE` served by a zxvfs service over a channel, **not** in-kernel. The existing numbers become thin forwards. | FS |

### D3. Channel IPC and shared memory

Model: capability-addressed ports (convention: seL4 / Fuchsia style).

- **Calls.** A request is synchronous, and copies at most 4 KB inline plus
  optional shm handles. A service sees the caller's pid and granted caps on every
  request (the bare-metal equivalent of zxvd's `SO_PEERCRED` check).
- **Tags.** Each channel request is a unit of work and carries an `lpx_t`.
  - The service answers with a status and a tag.
  - A tag whose action is WAIT parks the reply in the service's S0 queue. The
    kernel only delivers; it never decides. This is exactly zxvd's held-envelope
    behaviour (Web4 feed, escrow) made uniform.
- **Cost.** One tag byte per request and no per-byte tagging (X1).

### D4. Sandbox profile from the manifest: a four-valued grant

Add one manifest line: `needs <cap> [<scope>]`, for example `needs fs.read
space:home`, `needs net`, `needs ledger.post`, `needs camera`. Then compute,
per capability:

- **D (derived)**, from what the code can do:
  - Z-Wasm: the imports it declares (z-audit already lists them).
  - Pillar IR: the host calls referenced.
  - Native L1 code: the signed ZSP2 `caps`. The release signer attests it, which
    is the best one can do for machine code, so it is honest that native code
    relies on the signer.
- **M (manifest)**: the `needs` lines.

| D \ M | declared | not declared |
|---|---|---|
| used | **TRUE**: grant if the person or policy allows (PROCEED) | **FALSE + forbidden polarity**: the package hides an effect → **ROLLBACK** (refuse install, quarantine in `lpx_qlog`) |
| not used | **NEITHER**: over-asking → grant nothing, WAIT (shown in Gatehouse #8 as "asked, not needed") | nothing |

The person's decision is a second witness. Combine with `lpx_and(signer_tag,
person_tag)`:

| Signer | Person | Join | Result |
|---|---|---|---|
| TRUE | TRUE | TRUE | grant |
| TRUE | FALSE | BOTH | ESCALATE if the app asks again while neutral or entropic. Never silently granted. |
| either | unanswered | NEITHER | WAIT (held in the package's S0 member) |

Tiers set the default admission, mapped onto the existing Immigration visas:

- L1: RESIDENT (release root);
- L2: WORKER (signed publisher);
- L3 made locally: TRANSIT (no production capability until granted, which matches
  S0's rule).

### D5. Blocks precompiled per tier, and a better witnessed join

- **Precompile at package time.** Parse and validate each logic entry with its
  runtime's own `load`, and store the canonical artifact plus the source, both
  under the block CID.
  - The runtime's `load` becomes a cache hit keyed by CID.
  - Pins (C3) then cover path-based logic too, because the artifact is inside the
    `.zxa`. This closes the C3 limitation.
- **Upgrade `zxapp_call_witnessed`** to return an `lpx_t` instead of the bare
  four-valued value:
  - **Base:** today's join.
  - **Polarity:** from that entry's agreement history, computed with
    `lpx_polarity(pos0, neg0, pos1, neg1, stale)` over agree / disagree counts.
  - **Phase:** the call's lifecycle.
  - Then `lpx_action` decides:
    - a converging dispute (BOTH, negentropic) → WAIT, held but not escalated;
    - a stuck or worsening one → ESCALATE to the person;
    - a contract breach on a critical entry → forbidden → ROLLBACK (the S- member
      reverts the app to its previous release, via A/B).
  - This is about 150 lines on top of `zxapp.c` and `lpres_hex.c`.
- **App lifecycle on the X3 Gray cycle.** Installed = potential, launching =
  emerging (`zui_wm` launch screen at progress < 256), running = manifest,
  closing = dissolving. `lpx_advance` refuses jumps, so for example an app cannot
  go from installed to running without passing load. The launch screen already
  reads real progress.
- **One registry.** zxapp blocks and AIPI functions share the CID space and the
  descriptor: the contract is z-contract, compatible with zxapp B2 `provides`.
  AIPI execution is then "call a block somewhere else", and a local block is
  "AIPI with route = local". This removes a duplicate concept.

### D6. Compositor and window protocol

- **The compositor is an EL0 service.** It is `zui_wm` plus the `zui_present_fb`
  backend, and it alone maps the framebuffer.
- **Each app process owns its surfaces** in shm (`zui_present.h` P3: "the app
  owns memory", so this is consistent).
- **Protocol (on a channel):**

  | Message | Fields |
  |---|---|
  | `surface.create` | w, h, shm |
  | `surface.commit` | damage[≤16] (P1 already caps at `ZUI_DAMAGE_MAX`) |
  | `surface.configure` | from the compositor: size, scale_q8 (P4) |
  | `input.event` | from the compositor: `zui_input` events |
  | `launch.progress` | 0..256 (WM2) |

- **Hosted.** The same messages go to a per-window present backend in-process, so
  the app code is unchanged.
- **Paraconsistent part.** A window's content tag (`zui_logic` depth: S+ forward,
  S0 at the glass) travels on commit, so the compositor can draw held content
  distinctly, for example a held AI suggestion with a dashed gold edge, without
  trusting the app's own pixels to say "held". The compositor draws that frame
  itself, so an app cannot spoof a TRUE / held state. This is the one place where
  the logic adds security rather than decoration.

### D7. zxvd and the AI swarm on bare metal

- **zxvd-bm** is one EL0 service. It is built from `zxvd_rpc.c` + groups +
  libzxv-el0 (via `zxp_el0`) + `zxlibc-min` (needs `snprintf`, `strtoll`,
  `malloc` / `free` over `MEM_MAP`), and serves `CHAN_SERVE("zxvd")`.
- **Method parity** is checked by the existing `check_zxvd_spec.py` against the
  same `openrpc.json`.
- **Groups that need sockets** (`net.*`, `peer.*`) answer -32601 until the network
  service exists. That is honest and uses the same mechanism as `ZXVD_AIPI=0`
  today.
- **The swarm / tensor service** is a separate EL0 process: model pages mapped
  read-only from the golden disc's verified copy, `swarm.ask` forwarded from zxvd,
  and the answer tag plus quarantine log exactly as hosted (`zxv_answer_tag.c`).
  - Move the inference path out of EL1. Keep only the boot self-check in the
    kernel.
  - Threads: start with one worker per core as separate processes sharing the
    weights in shm, which avoids needing kernel threads first.

## What I would build first (in order)

1. **Loader hardening (S, about 1 week).**
   - `run` → `zxpkg_verify_release` / `zsp_verify2`;
   - process caps = signed caps ∩ the four-valued grant (D4);
   - refuse unsigned outside a dev flag;
   - Immigration visa per tier.

   This closes a live trust hole and needs nothing else.
2. **Memory + spawn + channels on arm64 (L).** A real page allocator, a
   per-process heap, `PROC_SPAWN` / `WAIT`, `CHAN_*`, `SHM_*`, `EVENT_WAIT`.
   Then the same on x86_64 (rank 2), because the consumer golden disc boots PCs.
3. **zxrt-el0 (M).**
   - crt0, `zxlibc-min` (the 8 measured functions + `snprintf` / `strtoll` /
     `malloc`), `zxp_el0`;
   - first milestone: `test_zxapp`'s own checks pass inside an EL0 process under
     QEMU, with all six runtimes linked. That proves "the same code, user space".
4. **zxvd-bm over channels (L)**, then **one real app end to end on bare metal**:
   Burrow (#1, model + view exist) as an EL0 process drawing through the
   compositor service (D6) and calling `files.*` on zxvd-bm.
5. **The hexagram witnessed join + `needs` lines + `.zxa` packaging (M)**, then the
   **builder app** and `flow` lines (rank 13). Defining L2 / L3 contents is an
   owner and product task; the platform should be ready for them before that.
