<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Industry kits

An industry kit is a small C11 library for one trade (customs, courier
escrow, store inventory, chain of custody, sequence alignment, field
triage, incident command, ground stations, home networking, field
sensors). Each kit is built only on the
public libzxv ABI (`sdk/libzxv/include/zxv_api.h`). It uses exact
rationals, four-valued logic, 64-state hexagram tags, capabilities, the
ledger, the quarantine hexlog and ML-DSA-87 / SHA3-256. A kit includes
nothing under `kernel/` and links nothing from it.

Every kit has the same layout:

```
<kit>/include/   the public header (the whole API)
<kit>/src/       the implementation
<kit>/test/      a unit test with hundreds of checks and a deterministic fuzz target
<kit>/examples/  a small program that tells the kit's story end to end
<kit>/README.md  what it does, how to use it, and its Limits
<kit>/Makefile   a few variables, then `include ../mk/kit.mk`
```

## The rules every kit follows

- **No floating point.** Quantities, money, rates and times are integers or
  exact rationals (`zind_qty`, built on `zxv_rat`). `make check-nofloat` runs
  the R11 checker over every source, header, test and example. Kit code is
  also compiled with `-mgeneral-regs-only` wherever the compiler accepts it,
  so a float that slips through does not compile.
- **Honest limits.** Every kit README has a "Limits" section. It says what
  the kit cannot know, such as a physical object, a clock or a person's
  honesty, and what it does not do.
- **Advisory wherever physical actuation is involved.** Kits recommend and
  record. People, or certified safety systems, act. A kit never opens a
  gate, moves goods or stops a machine by itself. A conflict is held as
  BOTH and frozen for a named human role: a broker, arbiter, approver or
  reviewer. That person decides by signing a canonical message.
- **Third-party platforms are public-API integrations, not partnerships.**
  A kit names a customs authority, a courier, a point-of-sale system or a
  serialization standard only to say what it can exchange data with. None
  of them has endorsed or certified these kits.
- **No certification claims.** No kit claims compliance with any
  regulation or standard, including customs, pharmacy serialization
  (DSCSA, the EU FMD), payments and food safety. These are building
  blocks. Certification belongs to whoever deploys them.

## Kits

| Kit | Folder | What it does | Status codes | Tests (local, gcc 13) |
|---|---|---|---|---|
| common (libzind) | [`common`](common/) | Shared layer: kit status codes, exact quantity parse/format/rounding, seen-sets, ML-DSA-87 signed SHA3-256 record chains with offline export/verify, the quarantine sink for Both/Forbidden events, wire reader/writer | -3000s | 11,237 checks + fuzz |
| Logix-Clear | [`logix-clear`](logix-clear/) | Container lifecycle on the hexagram phase (booked/in transit/released/closed), rule-table release across several jurisdictions, conflicts held as BOTH for a human broker, signed release records, exact duties posted to the ledger | -3100s | 1,401 checks + fuzz |
| Tri-Deliver | [`tri-deliver`](tri-deliver/) | Courier escrow: funds held (Potential) until the recipient's offline Ed25519 or ML-DSA-87 handoff signature releases them (Manifest); disputes frozen as BOTH for an arbiter; replays and double handoffs refused | -3200s | 729 checks + fuzz |
| V-Shelf | [`v-shelf`](v-shelf/) | Exact inventory ledger: receive, sell, transfer, shrink in whole units or exact weights; negative stock refused; count mismatches logged and held for an approver; full journal replay | -3300s | 831 checks + fuzz |
| Tri-Trace | [`tri-trace`](tri-trace/) | Per-item hash-chained, party-signed custody (commission, ship, receive, dispense); a duplicate scan freezes the item as BOTH for a reviewer; a whole chain verifies offline | -3400s | 627 checks + fuzz |
| Z-Genome | [`z-genome`](z-genome/) | Exact Needleman-Wunsch / Smith-Waterman alignment with affine gaps in checked int64 (overflow refused, documented tie-break), BLOSUM62 and DNA matrices, strict bounded FASTA, signed result records carrying the input digests, parameters and score; research use only | -3500s | 5,556 checks + fuzz |
| Vinea-Triage | [`vinea-triage`](vinea-triage/) | Store-and-forward START/SALT triage records with integer vitals, ML-DSA-87 signed bundles hash-chained per responder, dedupe and reordering at the station, forks caught, disagreeing records held as BOTH for a reviewer; signed, not encrypted (the transport must encrypt); not a medical device | -3600s | 816 checks + fuzz |
| Mesh-Command | [`mesh-command`](mesh-command/) | Incident command: signed field reports merged with `zxv_lp_from_evidence`, contradictions held as BOTH until a commander's signed decision (logged to the hexlog with who and why), advisory assignments a commander approves by signature, offline journal replay | -3700s | 1,667 checks + fuzz |
| Aero-Orbital | [`aero-orbital`](aero-orbital/) | Ground-side aid for SmallSat operators: integer WGS-84 geodetic/ECEF conversion with measured error bounds (correctly rounded to mm and nano-degree on the reference vectors), telemetry over UBH-168, redundant copies that disagree or fail their CRC held as BOTH for an analyst, never corrected silently. Not flight software | -3800s | 3,676 checks + fuzz |
| Tri-Modem | [`tri-modem`](tri-modem/) | Neighbourhood mesh fallback planner: signed (Ed25519 / ML-DSA-87) neighbour adverts, replays and expired adverts refused, contradictory reachability held as BOTH for the owner, exact max-min fair shares charged to rmag budgets; amateur-radio relays never selected. Configures no router | -3900s | 2,904 checks + fuzz |
| V-Band | [`v-band`](v-band/) | Low-power sensor log in the MCU subset (no heap, fixed buffers): exact readings with four-valued states, batches sized to the LoRaWAN data rate, carried over UBH-168, accepted in order by the gateway with duplicates and gaps refused; Cortex-M4 core 3.2 KB flash. Not a LoRaWAN stack | -4000s | 6,018 checks + fuzz |

Status codes: libzxv uses small negative codes, and ranges -1000 to -2499
are taken by other SDK layers. The industry kits use -3000 to -4999. The
shared layer has -3001 to -3099, and each kit gets a block of one hundred  A
later kit takes the next free hundred.

## Build and test

```sh
make -C sdk/libzxv all                         # the library every kit calls
make -C sdk/industry test                      # every kit: tests, fuzz smoke, examples, no-float check
make -C sdk/industry test CC=clang
make -C sdk/industry test SANITIZE=1 OUT=build/san      # ASan + UBSan
make -C sdk/industry fuzz FUZZ_ITERS=20000     # longer fuzz runs (ZIND_FUZZ_SEED=n for another stream)
make -C sdk/industry/tri-trace demo            # one kit's example, with output
```

Shared build: [`mk/kit.mk`](mk/kit.mk) holds the compiler flags (C11,
`-Wall -Wextra -Werror -Wshadow`, `-fstack-protector-strong`, the R11
guard), `SANITIZE=1`, `OUT=`, and the `test`, `fuzz`, `demo`,
`check-nofloat` and `clean` targets. A kit's Makefile sets `KIT`,
`LIBNAME`, `SRC`, `HDRS`, `TESTS`, `FUZZ` and `EXAMPLES`. The shared layer
is compiled into each kit's build directory with that kit's flags, so
sanitizer builds cover it too.

To add a kit:

1. Create `sdk/industry/<name>/` with the layout above.
2. Add its name to `KITS` in [`Makefile`](Makefile).
3. Take the next status-code hundred.

CI ([`.github/workflows/sdk-industry.yml`](../../.github/workflows/sdk-industry.yml))
runs three checks however many kits there are:

- `industry ubuntu-24.04` (gcc);
- `industry macos-14` (clang);
- `industry sanitize` (gcc, ASan + UBSan, tests and fuzz).

## What the shared layer gives a kit

- `zind_qty`: exact quantities. `zind_qty_parse` accepts `12`, `-3/4` and
  `0.125`. `zind_qty_format` writes the shortest exact decimal, or `n/d`.
  `zind_qty_to_minor` converts to minor units with an explicit rounding
  mode and returns the exact residual. Its default mode, `EXACT`, refuses
  an inexact amount.
- `zind_chain`: an append-only record chain. Each record is
  SHA3-256(domain, seq, kind, payload, previous hash, key id), signed with
  ML-DSA-87 under the chain's domain as context. `zind_chain_export` and
  `zind_chain_import` give a self-contained byte form.
  `zind_chain_verify` checks every link and signature against a trusted
  key list, offline.
- `zind_qsink`: the quarantine sink. It accepts only Both or Forbidden
  events, which it writes into a libzxv hexlog as Forbidden tags with a
  reason, an index and a subject hash. When the log fills, it halts the
  context instead of dropping evidence.
- `zind_seen`: a set of 32-byte digests for replay refusal.
- `zind_wr` / `zind_rd`: the little-endian canonical encoding that every
  signed message uses.
