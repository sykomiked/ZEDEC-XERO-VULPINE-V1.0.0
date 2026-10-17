# The boot and quality control — the acceptance gate

A system is not complete when it compiles. It is complete when it
**boots** and passes **quality control**: every feature exercised on the
running system, every layer verified from the bootloader to the browser.
This is the final gate, and it runs on the H100 (and on the fleet).

## 1. The compile (both architectures)

```sh
make -f build_system/Makefile.x86_64 kernel_x86_64.elf
make -f build_system/Makefile.arm64 all
```

Both must produce their ELF with zero errors, and
`build_system/verify_layers.sh` must print PASS. This is the entry
condition; a system that does not build is not booted.

## 2. The boot — three surfaces, in order

### 2a. The bare-metal boot (QEMU, x86_64)
```sh
build_system/ci_boot_check.sh x86_64 build_system/Makefile.x86_64 30 "[AI_OK]"
```
The kernel boots to the banner. The check is **the banner is backed by
symbols in the binary** — every claimed feature is present, not merely
printed.

### 2b. The bare-metal boot (QEMU, arm64)
```sh
build_system/ci_boot_check.sh arm64 build_system/Makefile.arm64 30 "[AI_OK]"
python3 build_system/arm64_session_check.py kernel_arm64.elf
```
The same, for the ARM path, plus the session check.

### 2c. The wasm boot (the browser)
The wasm engine loads a `.zxapp` and runs it in a headless engine:
```sh
node -e "const w=require('./web/boot/zxv_app.wasm'); /* the load + check */"
```
The browser engine must load the same manifest the node loads and pass
the same checks — the byte-identity proof at the wasm surface.

## 3. The quality control — every feature, three spaces, both ends

QC is not a smoke test. It runs the **feature matrix** against the
**running system**:

| What | The check |
|---|---|
| **every feature is there** | the boot's feature banners are backed by symbols (`feature_matrix.py`) |
| **every feature works** | the feature's own test passes on the running system |
| **every feature fits** | the feature is reached by a caller and reaches a caller (the wiring) |
| **both ends** | the UI end (a click reaches the kernel) and the system end (a kernel event reaches the framebuffer) |
| **the three spaces** | positive (the path works), negative (the forbidden cannot happen), neutral (the boundary, the empty, the idempotent) |
| **SCP-144** | the handshake completes node-to-node, and the legacy wrap holds when the inner cipher is assumed broken |
| **the four accounts** | the Crown, Ministry, Fountain and Union addresses match their local files; no secret appears anywhere |
| **the deterministic generation** | the browser generates a `.zxapp` and the bytes equal the node's (the cross-surface identity) |

## 4. The fleet QC (the nine nodes)

Once the single machine boots clean:

```sh
# for each of the nine nodes:
rsync the release; reimage; boot; run the QC suite
# verify per node: /bank, /telemetry, /pk, /sig, /spaces, /cisex, the mesh
```

A node that boots but fails the mesh check is **not** in the fleet. The
fleet is the system at scale, and QC at scale means every node passes
every check.

## 5. The release QC (the packaging)

- the multi-volume set builds (`zxv_core_m5`, `zxv_userland_407`,
  `zxv_polyglot_testbench`);
- the curriculum CAR reproduces its CID on the build host (the
  determinism proof);
- the certificate (`.zxv_cert`) is present and signed on every artifact;
- the negative-space audit passes on every binary.

## The acceptance statement

```
BUILT     both architectures, zero errors, the layers PASS
BOOTS     the banner backed by symbols, x86_64 and arm64, and the wasm
QC        every feature there, working and fitting; both ends; three
          spaces; SCP-144; the four accounts; the deterministic bytes
FLEET     nine nodes, each passing the mesh
RELEASE   the volumes, the CAR's CID, the certificates, the negative space
```

Only when all four hold is the system complete. Anything less is a
partial build, and the completion measure (`completion_state.py`) reports
it as BOTH or NEITHER — never TRUE.

## The honest note

The boot checks exist (`ci_boot_check.sh`, `arm64_session_check.py`,
`verify_layers.sh`); the QC suite above **does not yet exist as one
runnable artifact**. Building it is the last piece of the acceptance
gate: a single script that runs the whole list and prints one verdict.
Until it runs and passes, the system is **specified complete** but not
**proven complete** — and the difference is exactly the one this system
refuses to blur.
