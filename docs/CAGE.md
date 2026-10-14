# The Cage: quarantine and clearance for anything from outside

Anything that comes from outside enters **the Cage** first, whatever its
kind: a file, a program, a legacy batch job, a model weight, a swarm peer's
result, a circuit the user has just drawn. In the Cage it runs or is checked
with no access to the disk, the network or live state. It leaves only when
it is **cleared**, and the clearance records exactly which checks it passed.
Until then it is *uncleared*, and the Registrar (`docs/REGISTRAR.md`)
keeps it out of the permanent store.

Status: design, not built. Section 6 gives the build order. Much of the
machinery already exists (section 2).

## 1. The flow

```
intake  ->  the Cage: a desk runs the checks for this kind of item  ->  cleared / held / refused
                                                                         |        |        |
                                                     Registrar admits it   B or N   F: quarantine log
```

Each check gives a four-valued verdict, and the item's clearance is their
Belnap conjunction:

- **TRUE:** every check passed. The item is cleared.
- **FALSE:** a check failed. The item is refused and recorded in the
  quarantine log (`lpx_qlog`, hash-chained).
- **BOTH:** checks disagree, for example two runs gave different results,
  or two peers returned different bits. The item is held in escrow until a
  rule or a person resolves it. It never reaches the store as if it were
  clean.
- **NEITHER:** a check could not decide, for example the run used up its
  fuel. The item is held, and the user can extend the check or refuse.

## 2. What already exists

| Need | Existing part | State |
|---|---|---|
| Admission by identity and signature | `kernel/src/immigration`: signed packages, visa tiers RESIDENT / WORKER / TRANSIT, rejection log | Built; signatures Ed25519 today (ML-DSA planned in its header) |
| A hermetic runner with bounded execution | Z-Wasm (`sdk/kits/z-wasm`): validator with no floats, fuel-metered wasm3 built without float support | Built |
| Legacy code without native execution | `kernel/src/legacy`: COBOL and Fortran interpreters, EBCDIC | Built |
| Checking a peer's result | AIPI (`kernel/src/aipi`): CHECKER, CERT, QUORUM per function descriptor | Built for Z-Wasm functions |
| Checking an accelerator's output | `kernel/src/cotier` `ct_reconcile`: TRUE / FALSE / GLUT / PARADOX / UNKNOWN | Built |
| Quarantine and its log | `lpres_hex.h` `lpx_qlog`, hash-chained | Built |
| Capabilities that only narrow | `zxsh` `cap` and `sandbox exec` | Built, as checks inside the shell |

## 3. What the Cage can and cannot run

The Cage observes only what it executes itself. It therefore runs foreign
code in forms it interprets:

- Z-Wasm modules;
- COBOL and Fortran through the legacy interpreters;
- a circuit through its own runtime;
- a model through the integer engine.

In each case every memory access and every step is the Cage's own, so fuel
limits, memory limits and "no disk, no network" are real, not requested.

A **native foreign binary** (x86 or ARM machine code) cannot be watched
instruction by instruction without an emulator. The Cage gives it one of two
treatments:

1. It runs under an emulator the Cage controls, which is slow but fully
   observed. `kernel/src/emu` emulates game consoles today (a 6502 core),
   so an x86 or ARM emulator would be new work; or
2. It is refused clearance and can only run outside ZXV's guarantees,
   labelled *uncleared*, under the host operating system's own sandbox.

The Cage never claims to have watched code it did not interpret.

## 4. What clearance means

Clearance is a record of the checks passed, never a proof that the item is
safe for every possible input. A dry run shows how a program behaves on the
inputs it was given. Whether an arbitrary program ever stops cannot be
decided in general. The Cage's checks:

| Check | Catches | Its verdict when it cannot decide |
|---|---|---|
| Admission: signature, identity, visa tier | Unsigned or unknown origin | — |
| Static validation: format, types, Z-Wasm validator, copybook layout | Malformed input, floats, forbidden imports | — |
| Bounded run: fuel and memory limits, no disk, no network | Runaway loops, memory exhaustion, attempted I/O | NEITHER when fuel runs out |
| Determinism: the same run twice, or on two CPUs (`ai_cross` pattern) | Hidden nondeterminism | BOTH when the results differ |
| Exact arithmetic bounds: integer and rational sizes stay within declared limits | Overflow, unbounded fraction growth | FALSE on overflow |
| Property and fuzz runs (the `fuzz/` harness pattern) | Crashes and broken invariants on generated inputs | — |
| Quorum: two or more peers' bit-identical results | A wrong or lying peer | BOTH on disagreement |
| A machine-checked proof, where the repository has one (`proofs` job) | Anything the proof covers | — |

The clearance record lists the checks, their verdicts, the fuel used, the
inputs' hashes and the checker's version. It is written to the Registrar's
journal, so "why was this admitted?" always has an exact answer.

## 5. The desks

A desk is the set of checks for one kind of item. The names describe the
work, not any financial activity:

| Desk | Takes | Its checks |
|---|---|---|
| Raw Materials Desk | Bulk streams, binary blobs, sensor logs, punch-card images | Format validation, size limits, hashing |
| Archives Desk | COBOL, Fortran, EBCDIC datasets, copybooks | Legacy interpreters, copybook layout, bounded runs |
| Protocol Desk | Swarm and network payloads, peer results, model weights | Signatures, AIPI checks, quorum, GGUF validation and digests |
| Arbitration Desk | Contradictory results (BOTH) | `pdx_agent` merge rules, or a held decision for a person |
| Operations Desk | Circuits and programs before their first live run | Dry run with fuel, determinism, arithmetic bounds |
| Registry Desk | The user's own keys and local state | Identity checks; never leaves the device |

## 6. Build order

| # | Slice | Acceptance test |
|---|---|---|
| 1 | Clearance record and verdict combination over existing checks (admission, Z-Wasm validation, quarantine log) | A refused item is in the quarantine log; a cleared item's record lists every check |
| 2 | Operations Desk: dry-run circuits and Z-Wasm with fuel, run twice for determinism | A looping program ends NEITHER (fuel); a nondeterministic one ends BOTH |
| 3 | Archives Desk: COBOL/Fortran through the interpreters with bounded runs and copybook checks | A legacy job's output digest is identical across two CPUs |
| 4 | Protocol Desk: model weights (GGUF validation, pinned digests as in `golden.models`) and peer results (AIPI quorum) | A tampered weight file is refused; two disagreeing peers give BOTH |
| 5 | The Registrar admits only cleared items; uncleared ones are visible but marked | No uncleared item reaches the permanent store (fault-injected test) |
| 6 | The Cage window: the user watches a dry run's trace (steps, memory, verdicts) | The trace replays to the same verdicts |
| 7 | Emulated native binaries (optional, slow) | An x86 test binary runs observed under the emulator, or is refused |

## 7. Limits

- Clearance covers the checks performed, on the inputs used. It is not a
  proof of safety for all inputs, and the record says which checks ran.
- Native machine code is cleared only under emulation. Otherwise it runs
  outside ZXV's guarantees, marked uncleared.
- On a hosted system (macOS, Linux, Windows), the Cage isolates what it
  interprets. It cannot stop another program on the same machine from
  touching the disk directly. The Registrar's audit detects such changes
  afterwards (FALSE).
