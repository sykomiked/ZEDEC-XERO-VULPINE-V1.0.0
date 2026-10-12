<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# mesh-command: an incident picture held, not guessed

mesh-command is a C11 library for incident command over unreliable
links. Units in the field (engines, crews, drones, medics) send signed
reports that a fact is TRUE or FALSE, such as `road:R12/open`. The library
merges the reports into one incident picture. When units contradict each
other, the fact is held as **BOTH** until a commander resolves it with a
signed decision that names the evidence they saw and says why. Every
resolution is written to the quarantine hexlog. The library also proposes
assignments of resources to tasks, and a commander must approve each one
with a signature. It never dispatches anything. The API is
[`include/zmcommand.h`](include/zmcommand.h).

**Advisory only.** See [Limits](#limits).

## Reports

A unit numbers its own reports, counting upward, and signs each one with
ML-DSA-87. A report is bound to one incident, so it cannot be replayed
into another incident.

| Field | Rule |
|---|---|
| fact | 1..31 of `[A-Za-z0-9._:/-]`, e.g. `road:R12/open`, `gas:main/leak` |
| claim | `ZXV_LP_TRUE` or `ZXV_LP_FALSE` |
| observed | the unit's clock, in unix seconds |
| lat_e6, lon_e6 | microdegrees within ±90 and ±180 degrees, or both `ZMC_NO_POS` |
| note | up to 120 printable ASCII bytes |

The wire layout (little-endian) is:

```
"ZMCR" u8 1 str incident str unit u64 report_no str fact u8 claim
u64 observed u32 lat u32 lon str note   ||   ML-DSA-87 signature
```

`str` is a u8 length followed by the bytes. The signature covers
`zind_subject("mesh-command/report", body)` under the context
`"mesh-command/report"`. The test builds a report byte by byte from this
description and checks that it matches the signed bytes. The largest
report is 233 + 4,627 bytes.

`zmc_ingest` returns one of these:

| The report | Result |
|---|---|
| new and genuine | recorded (`ZXV_OK`), with the fact's state before and after, and whether it is stale or caused a new hold |
| the same report again (same unit, number and body) | dropped (`ZIND_E_REPLAY`) |
| a **different** report under a number the unit already used | refused and logged as a duplicate identity (`ZMC_E_EQUIVOCATION`) |
| a bad signature (another key, a flipped bit) | refused and logged as tamper (`ZIND_E_CHAIN`) |
| for another incident, from an unknown unit, or malformed | `ZMC_E_INCIDENT`, `ZMC_E_UNIT`, `ZXV_E_FORMAT` |

## The picture

Each fact's state comes from `zxv_lp_from_evidence`:

- **Counted claims.** For each unit, only its latest claim on the fact
  counts, and only if it was made in the current epoch. Latest means the
  highest report number. A report with a lower number than that unit's
  latest on the fact is **stale**: it is recorded and journaled but not
  counted. Units are trusted to number their reports in the order they
  made them; `observed` is not used for ordering.
- **State.** The state is `zxv_lp_from_evidence(pos + d_true, neg +
  d_false)`. Here `pos` and `neg` count the units whose counted claim is
  TRUE or FALSE, and `d_true`/`d_false` add one for a standing decision.
  - nobody has reported: **NEITHER**
  - all claims agree: **TRUE** or **FALSE**
  - units disagree: **BOTH** (held)
- **Tag.** `zmc_fact_view` gives each fact a 64-state tag:
  - base: the state above;
  - polarity: converging, steady or diverging, by how the last change
    moved the state;
  - phase: Potential (one claim), Emerging (several) or Manifest (a
    commander's decision stands).

`zxv_lpres` is not used here, because its counts only ever grow. A
commander's decision has to start a fresh count, which is what an epoch
does.

A fact that becomes BOTH is logged to the quarantine log once
(`ZIND_REASON_HELD_BOTH`, submitter: the unit whose report caused it).
The kit never picks a side.

## Decisions

A registered commander settles a held fact by signing
`zmc_decision_message`. The message is:

```
"zmc/decision/v1" str incident str fact u32 epoch evidence[32] u8 decision str commander str why
```

- `evidence` is `zind_subject("mesh-command/evidence", …)`, a
  domain-separated SHA3-256 of the claims counted now, ordered by unit ID
  (number of claims, then unit, report number and claim for each).
- `why` is 1..160 printable ASCII bytes and is required.

If any counted claim changes between signing and `zmc_resolve`, or the
epoch moves, the signature no longer verifies (`ZXV_E_VERIFY`). A
commander can therefore never settle a picture they did not see.

`zmc_resolve` needs `ATTEST`. When it succeeds:

- **The quarantine hexlog** gets one entry:
  - tag: the held BOTH tag;
  - reason: `ZMC_REASON_RESOLVED` (32 + 1);
  - submitter: **who**, the commander's index;
  - subject: SHA3-256 of the signed message, which carries **why**.
- **The journal** gets the full message, the commander's key ID and the
  signature, so the reason is readable there and the hexlog entry can be
  matched to it.
- **The fact** opens a new epoch. Earlier claims stop counting, and the
  decision stands as one piece of evidence. A unit that later reports
  the other way holds the fact again.

A forged or stale decision is refused and logged as tamper. So is one
presented with another value, another reason or another commander's
name.

## Advice and approvals

The kit only advises:

- `zmc_resource_add` registers resources ("BUS-2", kind "bus").
- `zmc_task_add` registers tasks: priority 1..9, the kind of resource
  needed and, optionally, a fact that must be TRUE or FALSE.
- `zmc_recommend` lists the open tasks, highest priority first and then
  by task ID. For each task it gives one result:

| Result | Meaning |
|---|---|
| `ASSIGN` + resource | the first free resource of the kind, by ID, not already proposed for a higher task |
| `HELD` | the task depends on a fact held as BOTH: resolve it first |
| `UNKNOWN` | nobody has reported the fact (NEITHER) |
| `BLOCKED` | the fact is the opposite of what the task needs |
| `NO_RESOURCE` | no free resource of the kind |

A recommendation never relies on a held or unknown fact. An assignment
takes effect only through `zmc_approve`. This needs a commander's
signature over:

```
"zmc/approve/v1" str incident str task str resource u32 fact_epoch str commander
```

`zmc_approve` checks the conditions again when it is applied
(`ZMC_E_BLOCKED` if the task is taken, the resource is busy or of another
kind, or the fact is not as required). An approval signed before a later
decision on its fact no longer verifies.

## Journal and offline verification

Every accepted report, decision, resource, task and approval is appended
to a libzind chain (`"mesh-command/journal"`) signed by the operator key.
`zmc_verify_journal` takes the exported bytes and a trust set: unit IDs
and keys, commander IDs and keys, and the operator key. It replays the
whole incident from nothing:

- every link and operator signature;
- every unit's report signature;
- that every decision names exactly the evidence and epoch the replay
  reaches, and is signed by that commander;
- that every approval's conditions hold at that point.

It returns a verdict with the counts, or the index of the first record
that does not replay. An operator who drops, repeats or reorders records,
even if they re-sign the chain, is caught at the first decision or
approval that no longer matches.

## Example

`make demo` runs [`examples/command_demo.c`](examples/command_demo.c), a
wildfire:

- An engine reports road R12 open and a drone reports it closed, so R12
  is held. A repeated report is dropped, and an equivocating one is
  refused.
- The advice holds the evacuation over R12 and proposes the bus for R9.
- A forged decision, and the genuine one presented as "open", are
  refused. The commander's decision is applied and logged with who and
  why.
- The commander approves the R9 evacuation, and an approval that does
  not fit is blocked.
- The journal replays offline. With one flipped bit it fails.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/mesh-command test     # 1,667 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/mesh-command SANITIZE=1 OUT=build/san test
make -C sdk/industry/mesh-command fuzz FUZZ_ITERS=20000
```

`test_command` covers these areas:

- **Validation:** every fact character class and length, every claim
  value, positions at and past each bound, and every note byte.
- **Wire format:** the byte-for-byte layout and the signing refusals.
- **Setup:** refusals and capacities (256 units, 16 commanders, 256
  resources, 256 tasks, 1,024 facts).
- **Ingest:** replays, re-signed replays, equivocation, other incidents,
  unknown units, other keys, 96 flipped bits, truncation, trailing bytes,
  stale reports and extreme report numbers.
- **Decisions:**
  - message refusals and the exact message layout;
  - refused decisions: forged ones, another commander's key, another
    value or reason, 16 flipped signature bits, a stale decision after
    new evidence, and no `ATTEST`;
  - the resolution's hexlog entry: who, the subject, and the BOTH base;
  - re-holding after a decision.
- **Advice:** the full recommendation order. Approvals: blocked, forged,
  misdirected, stale across a decision, and genuine.
- **Journal:**
  - offline replay, including with the trust set reordered;
  - missing or swapped keys and a wrong operator;
  - flipped bits and truncation;
  - journals re-signed by the operator with records dropped, repeated or
    reordered, or of unknown kinds.

`fuzz_command` runs five units and six facts against an independent
model:

- **Mesh:** delivers reports late, out of order, repeatedly and with
  flipped bits, and units equivocate.
- **Decisions:** genuine, forged and misdirected decisions. Some are
  signed and then overtaken by new evidence before they are applied.
- **Advice:** tasks and resources arrive, and every recommendation is
  compared with the model's.
- **Approvals:** genuine, forged and blocked approvals.

After every operation the status and every fact's state must equal the
model. The journal is replayed offline every 250 operations and at the
end. The fuzzer makes one random draw per statement, so gcc and clang
print identical output.

## Limits

- **Advisory, not a dispatch system.**
  - The kit records reports, shows where they disagree and proposes
    assignments.
  - It sends no units, opens no channels and controls no device. A
    person decides, and the assignment is carried out outside the kit.
  - It is not certified for emergency dispatch, CAD or public-safety
    communications under any standard or regulation.
- **Facts are what units say.** A signature proves which key signed a
  report, not that the road is open. Several units repeating one wrong
  observation make it look agreed. Agreement is counted, not checked.
- **Clocks and numbering are trusted as reported.** `observed` is the
  unit's own clock, which the kit neither corrects nor uses for ordering.
  A unit that numbers its reports out of order makes its newer
  observation look stale. A unit that reuses a number is caught as an
  equivocation, but only if both reports reach the incident.
- **No encryption.** Reports, decisions and the journal are signed, not
  encrypted. Locations and notes can be read in transit unless the
  transport encrypts them.
- **No settlement or payment over amateur radio.** The kit makes no
  claim that amateur (ham) bands may carry this traffic. Many countries
  forbid encrypted or business traffic there. Check the rules of the
  radio service before use.
- **Key management is outside the kit.** Registering, rotating and
  revoking unit and commander keys is the operator's job. A unit or
  commander whose key is stolen can sign anything that key can sign.
- **In memory, bounded:** 256 units, 16 commanders, 1,024 facts, 65,536
  reports, 256 tasks and 256 resources per incident. Resources are never
  released in this version. Persistence and replication are up to the
  application. The journal export can be stored anywhere and verified
  later. Each report carries a 4,627-byte signature, which is large for
  slow mesh links.
