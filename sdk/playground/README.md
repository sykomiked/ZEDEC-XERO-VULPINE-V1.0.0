<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV Playground: templates for ZXV's languages

ZXV has its own compact languages, each one a shorthand for a language
that institutions already run:

| Language | Shorthand for | What it is for |
|---|---|---|
| **treaty Sutra** | COBOL treaty records | Who owes whom; net and settle a batch with the 0.08889% assurance fee |
| **Sutra layouts** | COBOL copybooks | Exact record layouts for payments and statements |
| **Trebago** | Fortran | Exact numeric kernels: allocations, balances, matrices |
| **Eve** | Ada | Types with hard ranges: setpoints, payment fields, units |
| **KRON** | APL and K | Array analytics in a line: reconciliation, alerts, statistics |
| **MYRA** | MUMPS (M globals) | Tree-shaped records: charts, inventory |

The Playground turns them into things you can run: **templates**, each a
small project with a README, source files, inputs and a manifest. You
pick one, change it, and run it, in the app window or from a terminal.

## Four answers, not two

Every step ends in one of libzxv's four truth values:

| Verdict | Meaning | `zxv` exit |
|---|---|---|
| TRUE | ran, and every check passed | 0 |
| FALSE | refused: a rule was broken (with the line and column) | 1 |
| BOTH | contradictory evidence, **held for a person** | 3 |
| NEITHER | not enough to decide yet (missing data, or not run) | 4 |

A contradiction is kept and shown. It is never averaged away, never
resolved silently, and never allowed to "explode" into an arbitrary
answer. The run's verdict is the worst of its steps: FALSE, then BOTH,
then NEITHER, then TRUE.

## Safety

- A step marked `gate` runs only when every earlier step is TRUE. That is
  how a settlement waits for reconciliation.
- A step marked `advisory` only proposes. Grid, water and clinical
  templates are advisory: the result says a person must approve, and the
  Playground never actuates anything.
- Treaties settle on a fresh scratch engine. The owner's books are never
  touched.
- AI drafts are untrusted. The local swarm can draft a template's file
  (the manifest's `draft` line), but the draft is shown to you, not run,
  and it is checked like anything you type.
- Exact numbers only: integers, int64/int64 rationals, fixed decimals. A
  value that does not fit is refused, never rounded. No floats (R11).

## Templates

| Template | Languages | Audience | As shipped |
|---|---|---|---|
| `payment-reconciliation` | KRON, treaty | central banks, payment operators | BOTH: one payment's reports conflict, so settlement waits |
| `trade-corridor` | treaty (AI draft) | banks, trade finance | TRUE: three obligations net to two transfers |
| `grid-load-alerts` | KRON (advisory, AI draft) | grid operators | TRUE: one overload alert for an operator |
| `relief-allocation` | Trebago | public agencies, central banks | TRUE: a fund shared to the cent |
| `reservoir-balance` | Trebago (advisory) | water utilities | TRUE: a week's storage, for approval |
| `pump-controller` | Eve (advisory) | water and industrial operators | TRUE: a setpoint proposal inside its ranges |
| `payment-message-check` | Eve and Ada (AI draft) | banks, fintechs | TRUE: one layout in two spellings |
| `cobol-payment-record` | COBOL/Sutra (AI draft) | banks with COBOL cores | TRUE: copybook and Sutra agree; posting read, not posted |
| `account-statement` | Sutra, KRON (AI draft) | banks, credit unions | TRUE: opening + lines = closing, exactly |
| `clinic-records` | MYRA (advisory, AI draft) | clinics | BOTH: chart and pharmacy disagree on an allergy |
| `warehouse-stock` | MYRA (AI draft) | retailers, aid logistics | TRUE: stock totals by site |

Each template's README says what to change to see a refusal and a hold.

## From a terminal: `zxv`

```sh
make -C sdk/playground CC=gcc-11      # builds the pillars first, then build/zxv
zxv list                               # the templates
zxv new payment-reconciliation         # copy one into ./payment-reconciliation
cd payment-reconciliation
zxv run                                # run it; exit 0/1/3/4 for TRUE/FALSE/BOTH/NEITHER
zxv test                               # compare with expected.json (zxv test --update to accept)
zxv run sum.k                          # one file; the language comes from the extension
zxv prompt . "Acme owes Beta 12,000"   # the prompt the local AI would get, to pipe to a model
```

## In the app: the Playground tab

The desktop window (macOS app, Linux and Windows) has a **Playground**
tab: templates on the left, the files in an editor, **Run**, and the
result with each step's verdict, outputs, what is held and why, and a link
to the line of any refusal. Templates with a `draft` line add **Draft
with AI**.

zxvd serves the same over its socket: `play.catalogue`, `play.run` and
`play.draft` (see `api/zxvd.openrpc.json`).

## Writing a template

`templates/<id>/template.zxv`:

```text
id my-template                  # the folder name
title A short title
lang kron                       # the main language, for the catalogue
audience who it is for
safety none                     # none | advisory | settlement
summary One or two sentences.
inputs inputs.txt               # NAME = VALUE lines (optional)
step check kron check.k out=ok evidence labels=items
step settle treaty batch.treaty gate
draft batch.treaty What the local AI should write, in one line.
```

A step is `step NAME LANG FILE [gate] [advisory] [options]`. Inputs and
every earlier step's outputs are available by name, and `${name}` in a
step's file is replaced with that value before it runs. Each language's
options are documented at the top of its adapter, `src/zp_<lang>.c`.
Then run `make test` (and `make golden` to record a new result after
checking it by hand).

## Tests

`make test` runs every template and every case in `test/cases/` against
the goldens in `test/golden/`, twice each to check the bytes are the same,
plus library checks, the `zxv` command end to end (`test/test_cli.sh`)
and the no-float check. CI: `.github/workflows/playground.yml` (gcc-11,
and gcc-11 under ASan + UBSan).

## Limits

These are examples on scratch state. They do not connect to a real RTGS,
SCADA system, pump, pharmacy or warehouse, and their verdicts support a
person's review, never replace it. No template is a certified or
compliant system of any kind.

## Licence

Apache-2.0. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Provided without warranty.
