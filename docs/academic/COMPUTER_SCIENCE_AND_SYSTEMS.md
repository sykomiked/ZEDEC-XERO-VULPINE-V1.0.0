<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV system architecture for computer scientists

## Abstract

ZXV is an experimental, freestanding C11 code base that is packaged three ways: as a library (`libzxv`)
behind a stable, handle-based C ABI; as a hosted daemon (`zxvd`) that serves that library and an AI swarm
over JSON-RPC 2.0; and, optionally, as a bootable kernel image for five instruction-set architectures under
emulation. This report describes the system for computer scientists and working programmers. We present the
hourglass shape of the platform and its add-only ABI discipline, the stratified dependency rule, the
freestanding kernel (integer-only images, a table-generated capability check on system calls, a
copy-on-write journalled file system), the integer tensor runtime and the tokens-per-cycle swarm scheduler,
the daemon interface, the post-quantum and mesh layers, the four-valued result type and how an API client
should handle its non-classical values, determinism as a design goal, and the hardware emulation matrix.
Each claim is labelled with the strength of its evidence and a repository path. The kernel has booted only
under QEMU, not on physical hardware, and the AI runtime is measurably slower than llama.cpp.

## 1 Introduction and scope

The intended reader knows operating systems, ABIs and systems programming, and has never seen this
repository. We describe *what the code does*, not what the project's design documents aspire to; where the
two differ, we say so. Plain-language background is in [`docs/supplemental/`](../supplemental/README.md);
the cross-field evidence table is
[`docs/supplemental/CLAIMS_AND_EVIDENCE.md`](../supplemental/CLAIMS_AND_EVIDENCE.md). Companion papers cover
the logic ([PARACONSISTENT_LOGIC.md](PARACONSISTENT_LOGIC.md)), the arithmetic
([MATHEMATICS.md](MATHEMATICS.md)), the test methodology ([TESTING_METHODOLOGY.md](TESTING_METHODOLOGY.md))
and the quantum framing ([PHYSICS_AND_QUANTUM_FRAMING.md](PHYSICS_AND_QUANTUM_FRAMING.md)).

On the word "quantum". ZXV runs on classical hardware. It does not make a classical computer a quantum
computer and claims no quantum speedup. It borrows quantum-inspired features (values beyond true/false;
exact rational weights that are probabilities, not complex amplitudes, so there is no interference), and
pairs them with post-quantum cryptography (NIST FIPS 203 ML-KEM, FIPS 204 ML-DSA, FIPS 205 SLH-DSA) and
compatibility with conventional software. Any circuit simulator in the plans is a labelled classical
simulator, exponential in general (`docs/ARCHITECTURE.md` §9, §20).

## 2 Overall shape: an hourglass

**Definition 2.1 (hourglass).** A layered system whose middle layer is a single narrow interface that many
upper layers target and many lower layers implement (cf. Akhshabi and Dovrolis 2011).

In ZXV the neck is `sdk/libzxv/include/zxv_api.h` (1,652 lines; `ZXV_ABI_VERSION_MAJOR 1`, `MINOR 4`). Its
rules R1 to R11, stated in the header, are:

- R1 opaque handles only (`zxv_ctx`, `zxv_msg`, `zxv_cap`, `zxv_swarm`, ...); no kernel struct crosses the
  boundary;
- R2/R11 scalar parameters only, no `float` or `double`; amounts are `int64` minor units, Q16 values are
  `int32`, and a host float enters only as raw IEEE-754 bits through `zxv_rat_from_ieee754`, which returns the
  exact rational they denote;
- R3 every call returns `int32_t` status, `ZXV_OK` or a negative `ZXV_E_*`, mapped by `zxv_status_class` to
  `USAGE`, `REFUSED`, `FATAL` or `SYSTEM`;
- R4 caller-owned buffers with a two-call length protocol (`ZXV_E_BUFFER`);
- R8 versioning: only additions in a minor version; R9 unbuilt groups are present as stubs returning
  `ZXV_E_NOTIMPL`, discoverable through `zxv_features`;
- R10 every state-touching call takes a `const zxv_cap *` first.

**Add-only enforcement.** `sdk/libzxv/tools/abi_check.py` parses the header into function prototypes
(parameter names stripped), callback typedefs, macro values and enum sizes; `snapshot` records a release,
`check HEADER BASELINE [LIBRARY]` fails if anything recorded was removed or changed, including an exported
symbol when the shared library is given. Baselines are in `sdk/libzxv/abi/` (`libzxv-1.1.1.json`,
`libzxv-1.3.0.json`), and `.github/workflows/sdk-release.yml` runs the check against every baseline. Note a
documentary lag: `README.md` describes the ABI as version 1.3, while the header on this tree declares 1.4.0
(the hardware-emulation group, which `README.md` lists as "landing", #6).

**Above the neck.** Language bindings under `sdk/` (`cpp`, `dotnet`, `go`, `java`, `python`, `rust`,
`swift`, `typescript`, and others), a conformance vector set (`sdk/conformance/vectors.json`), and `zxvd`
(§7). `README.md` lists the .NET SDK as merged and the other eleven languages as "landing" (#5).

**Below the neck.** The freestanding modules under `kernel/src/` (one directory per module), linked either
into a hosted process (`kernel/arch/hosted/`) or, opt-in, into a bare-metal image
(`kernel/arch/{arm64,x86_64,riscv,arm32}`, `build_system/Makefile.*`). The same C is meant to serve both;
the default deliverable is hosted.

## 3 Stratification

`docs/ARCHITECTURE.md` §3 assigns modules to a base stratum 0 (hashes, AEAD, ML-KEM, exact rationals) and
seven strata above it: 1 kernel, 2 transport, 3 trust, 4 logic, 5 AI runtime, 6 settlement, 7 adapters.

**Rule R2 (layering).** For modules $m, n$ with strata $s(m), s(n)$: if $m$ depends on $n$ then $s(n) \le s(m)$.
Lower strata never depend on higher ones.

This is **design intent as an enforced property**. `docs/ARCHITECTURE.md` rule R2 says "A CI layering check
is planned (gap B1)", and `docs/GAP_AUDIT.md` B1 records it as open. Readers should expect violations to
exist until the check is built; we have not measured them. `docs/ARCHITECTURE.md` §20 maps the strata onto
quantum-information concepts as a labelled design analogy with no physical claim; its only engineering
content is R2.

## 4 The freestanding kernel

### 4.1 Targets and build discipline

The kernel builds for arm64 (reference), x86_64, riscv64, riscv32 and arm32
(`build_system/Makefile.{arm64,x86_64,riscv,riscv32,arm32}`). CI jobs `boot` and `boot-extra` run each image
under QEMU via `build_system/ci_boot_check.sh`, which requires the marker `[BOOT_OK]` and fails on any fault
marker. **No image has booted on a physical machine** (`README.md`; `docs/BAREMETAL.md` describes a first
physical boot that is not yet recorded).

**Floating point is excluded from kernel images at compile time.** On arm64, x86_64 and arm32 the compiler
is given `-mgeneral-regs-only`, which makes any `float`/`double` use a compile error; riscv targets use
integer-only ISAs and ABIs (`-march=rv64ima_zicsr -mabi=lp64`, `-march=rv32ima_zicsr -mabi=ilp32`); all add
`-Werror=double-promotion -Werror=float-conversion`. The comment in `build_system/Makefile.arm64` gives a
second, non-numerical reason: the IRQ entry path saves only `x0`–`x30`, so compiler-generated SIMD register
use (also disabled with `-fno-tree-vectorize`) would be clobbered on preemption (bug #41 there). See also
[INTEGER_ONLY_KERNEL.md](../supplemental/INTEGER_ONLY_KERNEL.md).

### 4.2 Capability-checked system calls

The syscall contract is one X-macro table, `ZXV_SYSCALL_TABLE` in `kernel/include/zxv_syscall_abi.h`, with
rows `X(nr, NAME, capability, arity, doc)`. Thirteen numbers are defined (0 reserved; `EXIT`, `WRITE`,
`GETPID`, `YIELD`, `SLEEP`, `SEND`, `RECV`, `OPEN`, `CLOSE`, `READ`, `EXEC`, `ABI_VERSION`). The number
enum, name table and permission table are generated from it, so a call without a declared capability does
not compile (`kernel/src/syscall/syscall.h`). Every architecture's dispatcher calls `zxv_syscall_permit(nr,
caps)`, which returns `ZXV_OK`, `ZXV_ENOSYS` or `ZXV_EPERM`; numbers are never reused, and withdrawn calls
leave permanent reserved holes. `zxv_syscall_selfcheck()` verifies density, legal capability bits and arity.

**Definition 4.1.** Here a *capability* is a bit in a per-process mask (`ZSC_CAP_WRITE`, `_PROC`, `_EXEC`,
`_IPC`, `_FS`). This is coarser than object capabilities in the sense of Dennis and Van Horn (1966) or seL4
(Klein et al. 2009): a kernel capability is not an unforgeable reference to a
particular object. At the library level, `zxv_cap` is closer to an object capability: it can be attenuated
(`zxv_cap_attenuate`, bitwise AND only), revoked transitively (`zxv_cap_revoke`), and carries an attestation
that must be TRUE. The header states the limit plainly: inside one process this is "least-privilege
discipline, not a sandbox"; enforcement begins at the `zxvd` process boundary.

### 4.3 zxvfs

`kernel/src/zxvfs/zxvfs.h` is a small file system on a `block_device_t` (virtio-blk, or memory in host
tests): format v2 with a free-space bitmap, up to `ZXVFS_MAX_FILES` = 256 files of up to 10 extents each,
and a redo journal restricted to metadata (`journal_target_ok()`). Whole-file writes are copy-on-write: new
extents are written first and the inode and bitmap are committed in one journalled transaction, giving
old-or-new atomicity at any size; in-place `zxvfs_pwrite` can tear, as in POSIX. `zxvfs_tri.h` stores a
*triad* (an action, its inverse and an unresolved remainder) as four files whose descriptor is written last,
so a triad is either fully bound or absent. The 256-file limit is noted as a blocker for model packs
(`docs/ARCHITECTURE.md` §6).

## 5 AI runtime and swarm scheduler

### 5.1 Integer tensor engine

`kernel/src/tensor/zt.h` fixes the numeric rules: activations are Q16.16 (T1), weights and activations are
block-quantised in blocks of 32 (T2), and RMS norm, softmax and SiLU are integer (T5: integer square root;
$2^x$ from a 257-entry table with interpolation). It reads GGUF files (`zt_gguf.c`), tokenises with
byte-level BPE (`zt_tok.c`) and has NEON, AVX2 and AVX-512 kernels (`zt_simd_*.c`) that `test_zt_simd.c`
checks against the plain C path. The design point is the same as GGML/llama.cpp (quantised CPU inference of
GGUF models) with one difference: no floating-point accumulation anywhere, so reduction order and FMA
contraction cannot change bits. The integer-only inference literature (Jacob et al. 2018) is the closest
academic precedent; ZXV differs in using no float even for scales and nonlinearities. Several modules in
`zt.h` carry golden-ratio constructions (T3, T7–T15); measured effects are mostly null or negative
(`bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §2; summarised in
`docs/supplemental/CLAIMS_AND_EVIDENCE.md` §1).

### 5.2 Swarm budget

**Definition 5.1 (tokens-per-cycle budget).** For $L \le 8$ levels, level $d$ holds at most $F(d+2)$ models
and carries weight $W(d) = F(L-d+1)$, with $F$ the Fibonacci sequence. Each cycle the total $T \le 2^{48}$
is split over levels with an active model in proportion to $W$, then equally within a level, by the
largest-remainder method with ties to the lower index, so $\sum_i a_i = T$ exactly. Unused tokens expire
(`kernel/src/swarm/swarm_budget.h`, rules R1–R6).

The invariant `used <= allotted` after every consume is tested (`test_swarm_budget.c`) and checked as a Lean
lemma on a model (`docs/FORMAL_INVARIANTS.md` H1, H2). Through the ABI, `zxv_swarm_consume` returns
`ZXV_E_BUDGET` with a partial grant written to `*out_granted`, the one documented exception to "out
parameters only on success". In the hosted app the budget gates generated tokens only; prompt tokens are not
charged (`docs/ARCHITECTURE.md` §10). Inside the kernel image the swarm and tensor engine run only a boot
self-check (`[AI_OK]`); no model runs in the kernel image (`docs/AUDIT_REPORT.md`, requirement 11).
Plain-language background:
[TOKEN_BUDGETS_AND_EMOTIONAL_AXIS.md](../supplemental/TOKEN_BUDGETS_AND_EMOTIONAL_AXIS.md).

## 6 Post-quantum crypto, transport and mesh

The PQ layer (`kernel/src/pqsec/`, `kernel/src/mlkem/`) vendors upstream reference code for ML-KEM, ML-DSA,
SLH-DSA and HQC and checks it against NIST ACVP and the official KATs, including cross-compiled runs on
aarch64, riscv64, armhf, i686 and big-endian s390x under qemu-user (`kernel/Makefile` target `kat-cross`;
`docs/PQ_MATRIX.md`). A valgrind secret-branch check runs on ML-KEM-768 and ML-DSA-65 (`ct-valgrind`); it is
not a proof of constant time. Peer links use one session layer at ML-KEM-1024 + ML-DSA-87
(`kernel/src/vinea/vna_session.h`).

**Freight packets** (`kernel/src/freight/freight.h`). A *smart packet* is one UBH-168 frame of 21 octets: a
row index and 20 payload bytes. A *freight* is 168 such rows, a systematic Reed–Solomon code over
$\mathrm{GF}(2^8)$ (polynomial `0x11d`) with Cauchy parity coefficients $1/(r \oplus j)$, so any $k$ of the
168 rows reconstruct the payload (MDS; Reed and Solomon 1960). It corrects erasures only; corruption is
detected by a SHA-256 in the 64-byte header. The header comment calls the design "holographic" and then
defines it as erasure coding; we use the latter term. An optional op stream (`LITERAL`, `COPY`, `CIDREF`,
`SEED`) is expanded under a caller-supplied length limit, which bounds decompression bombs.

**Mesh.** `kernel/src/vinea/` is a Kademlia-style DHT (`vna_kad.c`; Maymounkov and Mazières 2002) with PQ
identities, off by default; NAT traversal, IPv6 and persisted identity are absent (`docs/ARCHITECTURE.md`
§7). `kernel/src/mesh_net/mesh_net.h` keeps route and accounting state only and says so: it "signs nothing,
encrypts nothing and moves no bytes". The two-node demo runs two loopback addresses on one host
(`docs/TWO_NODE_DEMO.md`).

## 7 The zxvd daemon

`zxvd` (`kernel/arch/hosted/zxv_host.c` with `-DZXVD`, plus `zxvd_rpc.c`) speaks JSON-RPC 2.0, one request
per line, over a UNIX socket of mode 0600 (AF_UNIX on Windows 10 1803+ with an owner-only DACL), serving
only same-user peers (`SO_PEERCRED`/`getpeereid`). Batches are refused; amounts are exact integers and `1.5`
is refused rather than rounded; 64-bit results are decimal strings; at most 8 clients, a 60 s idle timeout
and a 16 KiB line limit. It refuses to start against libzxv older than ABI 1.3. Capabilities are never sent
over the socket. The method list is machine-readable in `api/zxvd.openrpc.json`, and CI checks that it
matches what the daemon serves (`kernel/arch/hosted/check_zxvd_spec.py`; `.github/workflows/zxvd.yml`).
libzxv status codes surface as JSON-RPC error `-32000` with `error.data` carrying the status, name and
class. Limits stated in `docs/ZXVD.md`: no token streaming, books not persisted across restarts, and a
duplicate-symbol hazard (`zxv_model_open`) avoided only by linking libzxv as a shared library.

## 8 The four-valued result type in an API

**Definition 8.1.** $\mathbf{4} = \{N, T, F, B\}$, encoded `ZXV_LP_NEITHER = 0`, `TRUE = 1`, `FALSE = 2`,
`BOTH = 3` (`zxv_api.h`; kernel `lpres_state_t` in `kernel/src/lpres/lpres.h`). From evidence counts
$(p, n)$: $B$ if $p>0 \wedge n>0$, $T$ if only $p>0$, $F$ if only $n>0$, $N$ if neither (`zxv_lp_from_evidence`).
Negation swaps $T$ and $F$ and fixes $B$ and $N$; `lpres_conjoin` has $F$ absorbing and $T$ as identity,
`lpres_disjoin` dually.

One deviation matters to programmers who know Belnap (1977) and Dunn (1976): the code gives $N \wedge B = N$
and $N \vee B = B$, whereas Belnap's truth order gives $F$ and $T$ (`kernel/src/lpres/lpres.c`; discussed in
[MULTI_VALUED_LOGIC.md](../supplemental/MULTI_VALUED_LOGIC.md) and
[PARACONSISTENT_LOGIC.md](PARACONSISTENT_LOGIC.md)).

**Two channels.** A call returns a *status* (did the call work?) and, separately, a *verdict* (what does the
evidence say?). They must not be conflated: `zxv_lp_and` returns `ZXV_OK` with `*out = ZXV_LP_BOTH` when the
inputs conflict. A broken *hard invariant* is never `BOTH`; it is `ZXV_E_INVARIANT`, which poisons the
context, after which every call returns `ZXV_E_POISONED` (`docs/ARCHITECTURE.md` rule R5; `zxv_api.h`
"FAIL-CLOSED").

**Handling rules used throughout the code base.**

1. Only $T$ authorises an effect (`lpres_implies_actuator_authority`; `zxv_cap_set_attestation`: "Anything
   but TRUE blocks gated calls"; `hwm.h` M2: "Only TRUE binds").
2. $N$ waits; unknown never acts (`lpres_hex.h` X2; `hwm.h` M2).
3. $B$ is held, not resolved by voting: it waits while evidence converges and escalates to a person
   otherwise (`lpres_hex.h` X2); in the hardware matrix a shadow-check disagreement puts both providers in $B$
   and stops binding them (`hwm.h` M6).
4. Bindings must not collapse the value to a boolean (`zxv_api.h`: "SDKs must never collapse it to a
   boolean").

```c
int32_t v;
if (zxv_lp_from_evidence(pos, neg, &v) != ZXV_OK) return handle_usage_error();
switch (v) {
case ZXV_LP_TRUE:    return act();                 /* the only branch with effects   */
case ZXV_LP_FALSE:   return refuse();              /* ordinary refusal, nothing to undo */
case ZXV_LP_NEITHER: return defer_until_evidence(); /* unknown never acts             */
case ZXV_LP_BOTH:    return hold_and_escalate();    /* keep both sides on record      */
default:             return treat_as_error();       /* R8: unknown values are errors  */
}
```

`zxv_lpres_gate` reports whether every attestation in a registry is certain ($T$ or $F$). Model answers
carry a 6-bit tag (`kernel/src/lpres/lpres_hex.h`, `kernel/arch/hosted/zxv_answer_tag.h`) whose base states
how the *computation* went, never whether the answer is true; a saturated logit or failed forward pass
withholds the reply and appends it to a hash-chained quarantine log.

## 9 Determinism

**Definition 9.1.** A function $f$ is *bit-deterministic across platforms* if, for every input $x$ and every
supported target $p, q$, $f_p(x) = f_q(x)$ as bit strings.

This is a design goal, pursued by (i) integer arithmetic only (§4.1, §5.1), so no rounding-mode, FMA or
reduction-order variation (Goldberg 1991 explains why float results vary); (ii) exact tie-breaking rules
(largest remainder in `swarm_budget.h` R5; lowest cost, then kind, then index in `hwm.h` M3); (iii) no clock
input to the budget ("No float, no clock"). Evidence is of two kinds. Tested: each SIMD kernel against the C
reference (`test_zt_simd.c`). Measured: one hash over every logit agreed across C, AVX2 and AVX-512 on
x86_64 and C and NEON on arm64, the arm64 run under emulation
(`bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.2, §1.5); after batched prefill, `zt.logits_fnv` was
identical across kernel sets, batch sizes 16 to 256 and 1 to 4 threads
(`bench/results/2026-10-11-batched-prefill/REPORT.md`). `docs/ARCHITECTURE.md` §10 reports that llama.cpp
changed 5% of next-token picks when only its batch size changed. Nothing claims determinism for networking,
scheduling interleavings or wall-clock behaviour; this is deterministic computation, not deterministic
replay of a whole system (contrast ReVirt, Dunlap et al. 2002).

## 10 Hardware emulation matrix

`kernel/src/hwemu/hwm.h` models *functions* (a hash engine, a codec, a tensor dot) served by *providers* of
kind `HARD`, `FABRIC`, `MICRO`, `PEER`, `SOFT` or `REMOTE`. All providers share one call shape (M1); a
provider binds only after its outputs match the function's conformance vectors bit for bit (M2); selection
is deterministic (M3); failover never routes to an unproven provider (M4); dependencies are declared and
bindings acyclic (M5); key-bearing functions are always local and never memoised (M8); vector sets are
versioned contracts (M9). `hwe.c` supplies SOFT engines (SHA-256, Keccak, HMAC, GCD, LOGIC64, tensor dot)
with golden vectors re-derived by `kernel/tools/check_hwe_vectors.py`; `hwr.c` handles dual-signed
(ML-DSA-87 + SLH-DSA-SHAKE-256s) hardware image updates with two slots and rollback. Host tests run under
`make test-hwemu`. `bench_hwm.c` exists, but we found no recorded result. See
[HARDWARE_EMULATION_AND_LICENCE.md](../supplemental/HARDWARE_EMULATION_AND_LICENCE.md).

## 11 Building, testing and reading the source

From the repository root (`README.md`, "Build & verify"): `./build.sh arm64` builds the reference image; it
boots with `qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M -kernel kernel_arm64.elf
-nographic`. `make -C kernel verify-all` runs the host test gate. CI runs it on `ubuntu-22.04`, whose
default compiler is gcc 11, and other jobs explicitly install `gcc-11` and put it first on `PATH`
(`.github/workflows/ci.yml`); use gcc 11 to reproduce CI. `README.md` lists a gcc 13 build fix as landing
(#30), and `BUILD_DEPS.md` lists newer compilers for the cross-builds, so the host gate under newer gcc may
fail with `-Werror` until that lands. `scripts/verify-local.sh --all` (or `--docker --all`) reproduces most
CI jobs locally (`docs/CI.md`); `zxvd` builds with `kernel/arch/hosted/build_zxvd.sh`.

**How to read the source.**

1. Start with the contracts, not the implementations: `sdk/libzxv/include/zxv_api.h`,
   `kernel/include/zxv_syscall_abi.h`, then a module header. Headers in this tree carry their rules (R1…, T1…,
   M1…) and stated limits in the leading comment; tests cite those rule numbers.
2. Each module is a directory `kernel/src/<name>/` with `<name>.h`, `<name>.c` and `test_*.c`; the test
   command line in `kernel/Makefile` shows exactly which files it links.
3. `SOURCE_CODE_NAVIGATION_GUIDE.md` (directory structure, subsystem map) and `SUBSYSTEM_INDEX.md` (module
   index) at the root are older orientation documents. Their test counts ("all 10 test binaries"; "24/24 pass")
   disagree with `docs/AUDIT_REPORT.md` (3,984 passing checks) and `docs/ARCHITECTURE.md` §1 (4,515 local
   checks); use them for layout and `docs/SYSTEM_REFERENCE.md` and `docs/ARCHITECTURE.md` for current state.
4. Many module names are unconventional (`bootlegger`, `robin_debanks`, `porter_house`); a rename script is
   queued (`docs/ARCHITECTURE.md` §6). [GLOSSARY.md](../supplemental/GLOSSARY.md) resolves the vocabulary.

## 12 Status of claims

| Claim | Status | Evidence |
|---|---|---|
| libzxv ABI is add-only, checked against recorded baselines | Tested (CI) | `sdk/libzxv/tools/abi_check.py`; `.github/workflows/sdk-release.yml`; `sdk/libzxv/abi/` |
| No floats in the libzxv ABI; floats enter only as IEEE-754 bits | Implemented, not evaluated (by inspection of the header) | `sdk/libzxv/include/zxv_api.h` R2, R11 |
| Seven strata with no upward dependency | Design intent (check not built) | `docs/ARCHITECTURE.md` R2; `docs/GAP_AUDIT.md` B1 |
| Kernel builds for five ISAs and boots under QEMU | Tested (CI) | `build_system/ci_boot_check.sh`; CI jobs `boot`, `boot-extra` |
| Kernel boots on physical hardware | Design intent | `README.md`; `docs/BAREMETAL.md` |
| Floating point rejected at compile time in kernel images | Tested (CI build) | `build_system/Makefile.arm64` and siblings |
| Every syscall declares a capability; permit decision in one function | Tested (host) | `kernel/src/syscall/syscall.h`; `test_syscall.c` |
| zxvfs whole-file writes are old-or-new atomic | Tested (host, incl. fuzz) | `kernel/src/zxvfs/test_zxvfs.c`, `test_zxvfs_fuzz.c` |
| Swarm allotments sum to $T$; `used <= allotted` | Tested (host); Lean lemma on a model | `kernel/src/swarm/test_swarm_budget.c`; `docs/FORMAL_INVARIANTS.md` H1, H2 |
| Integer engine runs Qwen2.5-1.5B with perplexity 9.371 vs llama.cpp 9.362 | Measured | `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.3 |
| Same logit hash across SIMD kernels and x86_64/arm64 (arm64 emulated) | Measured; kernels tested | REPORT.md §1.2, §1.5; `kernel/src/tensor/test_zt_simd.c` |
| Engine is slower than llama.cpp | Measured | `bench/results/2026-10-11-batched-prefill/REPORT.md`; `docs/release/HUGGING_FACE_PAGE.md` |
| PQ algorithms match KATs on five CPU families | Tested (CI) | `docs/PQ_MATRIX.md`; `kernel/Makefile` `kat-cross` |
| Freight rebuilds from any $k$ of 168 rows | Tested (host) | `kernel/src/freight/test_freight.c` |
| `mesh_net` moves bytes or encrypts | Design intent | `kernel/src/mesh_net/mesh_net.h` ("NOT YET") |
| zxvd serves exactly the methods in its OpenRPC file | Tested (CI) | `api/zxvd.openrpc.json`; `.github/workflows/zxvd.yml` |
| Four-valued ops and evidence mapping as in §8 | Tested (host) | `kernel/src/lpres/test_lpres.c`, `test_lpres_hex.c` |
| Hardware matrix binds only vector-proven providers | Tested (host) | `kernel/src/hwemu/test_hwm.c` |
| Hardware matrix speed benefit | Implemented, not evaluated | `kernel/src/hwemu/bench_hwm.c` (no report found) |
| Host test suite passes on a clean checkout | Tested (CI) | `docs/AUDIT_REPORT.md` |

## 13 Limitations and threats to validity

- **Emulation only.** All boot evidence is QEMU; real devices, firmware and interrupt controllers are
  untested. The arm64 determinism run was emulated. The AI runtime and economy are exercised in the hosted
  process; the kernel image runs only self-checks for them.
- **Capability strength.** Kernel capabilities are per-process bit masks; in-process `zxv_cap` is
  discipline, not isolation (§4.2). No information-flow or isolation proof exists, unlike seL4.
- **Formal methods.** TLA+ and Lean models are hand-written specifications of the C, with no refinement
  proof to the source or binary (`docs/FORMAL_INVARIANTS.md`). This is unlike seL4, whose proofs reach the
  C code and the binary. A change to the C can silently invalidate a model.
- **Coverage.** 36 modules under `kernel/src` are linked by no test or fuzz harness (`docs/COVERAGE.md`);
  line coverage 87.0% and MC/DC 32.2% refer only to modules that tests link.
- **Documents disagree.** Speed figures differ by machine and source (`docs/ARCHITECTURE.md` §10 "~11 vs
  15–18 tokens/s"; `docs/release/HUGGING_FACE_PAGE.md` 37 vs 91 generation tokens/s on an M3 Max); the ABI
  version (1.3 in `README.md`, 1.4.0 in the header); test counts (§11). `README.md` says the kernel is "built
  to run equally well" on a supercomputer, while `docs/AUDIT_REPORT.md` (requirement 9) records HPC as
  deferred.
- **Side channels.** PQ code is reference grade, not hardened against all side channels or fault injection
  (`docs/PQ_MATRIX.md` §4).

## 14 Relation to prior work

- **seL4** (Klein et al. 2009): a formally verified microkernel with object capabilities. ZXV shares the
  goal that every kernel entry is authorised and fails closed, but has neither object capabilities in the kernel nor a
  refinement proof.
- **Plan 9** (Pike et al. 1995): one small, uniform protocol as the system's neck. ZXV's neck is a C ABI
  plus a JSON-RPC mirror of it, rather than a file protocol.
- **Exokernels** (Engler et al. 1995) and **unikernels** (Madhavapeddy et al. 2013): the same freestanding
  modules linked as a library into an application or into a single-image kernel. ZXV follows the library-OS
  packaging but keeps a conventional syscall boundary in its images.
- **Fuchsia/Zircon** (no peer-reviewed reference; Google's public documentation): handle-based capabilities
  with rights that can only be reduced on duplication, which `zxv_cap_attenuate` resembles at library level.
  Dennis and Van Horn (1966) and Miller et al. (2003) give the capability background.
- **Deterministic execution and replay** (Bressoud and Schneider 1995; Dunlap et al. 2002; Olszewski et al.
  2009): those systems make whole executions reproducible by logging or ordering nondeterministic events. ZXV
  removes one source of nondeterminism, floating point, from computation and does not log events.
- **llama.cpp/GGML** (open-source software by G. Gerganov and contributors; no paper): the GGUF format and
  K-quant layouts that ZXV reads, and its comparison baseline.
- **Layering**: strict layering goes back to Dijkstra's THE system (1968); the hourglass argument is
  Akhshabi and Dovrolis (2011); end-to-end arguments (Saltzer et al. 1984) favour a minimal neck.

## References

- Akhshabi, S., and Dovrolis, C. (2011). The evolution of layered protocol stacks leads to an hourglass-shaped architecture. *Proc. ACM SIGCOMM 2011*.
- Belnap, N. D. (1977). A useful four-valued logic. In J. M. Dunn and G. Epstein (eds.), *Modern Uses of Multiple-Valued Logic*, Reidel.
- Bressoud, T. C., and Schneider, F. B. (1995). Hypervisor-based fault tolerance. *Proc. 15th ACM SOSP*.
- Dennis, J. B., and Van Horn, E. C. (1966). Programming semantics for multiprogrammed computations. *Communications of the ACM* 9(3).
- Dijkstra, E. W. (1968). The structure of the "THE"-multiprogramming system. *Communications of the ACM* 11(5).
- Dunlap, G. W., King, S. T., Cinar, S., Basrai, M. A., and Chen, P. M. (2002). ReVirt: enabling intrusion analysis through virtual-machine logging and replay. *Proc. 5th USENIX OSDI*.
- Dunn, J. M. (1976). Intuitive semantics for first-degree entailments and "coupled trees". *Philosophical Studies* 29.
- Engler, D. R., Kaashoek, M. F., and O'Toole, J. (1995). Exokernel: an operating system architecture for application-level resource management. *Proc. 15th ACM SOSP*.
- Goldberg, D. (1991). What every computer scientist should know about floating-point arithmetic. *ACM Computing Surveys* 23(1).
- Jacob, B., et al. (2018). Quantization and training of neural networks for efficient integer-arithmetic-only inference. *Proc. IEEE CVPR 2018*.
- Klein, G., et al. (2009). seL4: formal verification of an OS kernel. *Proc. 22nd ACM SOSP*.
- Madhavapeddy, A., et al. (2013). Unikernels: library operating systems for the cloud. *Proc. ASPLOS 2013*.
- Maymounkov, P., and Mazières, D. (2002). Kademlia: a peer-to-peer information system based on the XOR metric. *Proc. IPTPS 2002*, LNCS 2429.
- Miller, M. S., Yee, K.-P., and Shapiro, J. (2003). Capability myths demolished. Technical Report SRL2003-02, Johns Hopkins University.
- NIST (2024). FIPS 203, *Module-Lattice-Based Key-Encapsulation Mechanism Standard*; FIPS 204, *Module-Lattice-Based Digital Signature Standard*; FIPS 205, *Stateless Hash-Based Digital Signature Standard*.
- Olszewski, M., Ansel, J., and Amarasinghe, S. (2009). Kendo: efficient deterministic multithreading in software. *Proc. ASPLOS 2009*.
- Pike, R., Presotto, D., Dorward, S., Flandrena, B., Thompson, K., Trickey, H., and Winterbottom, P. (1995). Plan 9 from Bell Labs. *Computing Systems* 8(3).
- Reed, I. S., and Solomon, G. (1960). Polynomial codes over certain finite fields. *Journal of the SIAM* 8(2).
- Saltzer, J. H., Reed, D. P., and Clark, D. D. (1984). End-to-end arguments in system design. *ACM Transactions on Computer Systems* 2(4).
