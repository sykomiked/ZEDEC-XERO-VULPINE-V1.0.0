<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Check a patient chart against the pharmacy record

> **An advisory aid, not a medical device.** It checks the shape of data
> and shows where two records disagree. A clinician decides what is
> right. It makes no HIPAA, GDPR or other compliance claim; at most it is
> designed to support good privacy practice (it only reads the files you
> give it and sends nothing anywhere). Every name and value here is
> **invented sample data**.

Many health systems keep records in M (MUMPS) globals. Here one patient's
chart is a MYRA tree, `chart.myra`: demographics, allergies and active
medications, with each dose an exact decimal (at most 3 decimals).

The clinic's chart and the pharmacy's record describe the same patient.
Both are decoded exactly through the same schema, then compared node by
node:

| The two records | Verdict |
|---|---|
| agree on every node | TRUE: an advisory summary, a clinician approves |
| disagree on a node | BOTH: **held for a clinician**, both values shown |
| a line breaks the schema | FALSE: refused, with the file, line and column |

Nothing is ever picked, averaged or overwritten.

## Run it

```sh
zxv new clinic-records
cd clinic-records
zxv run
```

As shipped the run is **held**: the clinic chart says the penicillin
allergy is `severe`, the pharmacy says `moderate`. Change `moderate` to
`severe` in `pharmacy-record.txt` and run again: TRUE, a proposal for a
clinician to approve.

To see a refusal, write the levothyroxine dose in `clinic-chart.txt` as
`.0755`: `dose` holds 3 decimals, so it is refused, never rounded. (Doses
use M's canonical form: `.075`, not `0.075`.)

Add `require-both` to the step in `template.zxv` and delete a line from
one record: that node is then NEITHER (undecided) instead of a note.

## Files

- `chart.myra`: the schema. `draft` lets the local AI propose a schema;
  its draft is parsed and checked like anything you type.
- `clinic-chart.txt`, `pharmacy-record.txt`: the two records, one node per
  line as M's ZWRITE prints them. Dates are `$H` day counts.

## Limits

It compares data shapes and values, not clinical meaning: it does not
know that `moderate` and `severe` are related, check interactions or
doses, or replace any clinical system. No real records, no database
access, no FileMan.
