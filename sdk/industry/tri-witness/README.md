<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tri-Witness: an offline replay of clearing decisions

Tri-Witness is a C11 library for regulators and auditors. A
[Sutra-Clear](../sutra-clear/) house hands over two exports:

- its signed record chain (`zind_chain_export`);
- its ledger journal (`zsc_journal_export`).

From those two files alone, the witness replays every clearing cycle. Each
discrepancy becomes a Forbidden finding that names:

- the cycle;
- the chain record;
- the field;
- the field's exact bytes.

The findings go into a report that the witness signs with its own key.
**The witness is read-only.** `ztw_create` refuses a capability that holds
`ZXV_CAP_LEDGER_POST`, so it cannot post anywhere. It needs only
`LEDGER_READ` and `CRYPTO_SIGN`, and is meant to run in its own context,
apart from the house.

The witness shares no code with the house. It reads the documented formats
(see the Sutra-Clear README) with its own parser and reimplements the field
rules. For the arithmetic it calls the same public libzxv functions the
house must use. The API is [`include/ztwit.h`](include/ztwit.h).

## What is checked

| Finding | The check |
|---|---|
| `IMPORT` | the chain export imports as a `"sutra-clear"` chain |
| `LINK`, `SIGNATURE`, `UNTRUSTED` | **each** record: its hash recomputed from its contents (the zind record formula), its link to the record before, its key among the trusted house keys, its ML-DSA-87 signature. Every bad record is listed, not only the first |
| `KIND`, `FRAGMENT` | only cycle fragments, in order, complete |
| `FORMAT` | the cycle record parses exactly (`"ZSC1"`, nothing left over), with valid values: house id, settlement time, ISO 4217 currencies with their real minor units, participant codes |
| `SEQUENCE` | cycle numbers run 1, 2, 3…; the house id never changes; the record names the key that signed it; currencies and participants are only ever appended; the ledger head never goes back |
| `INSTRUCTION` | every settled instruction is valid by the house's rules, in a house currency, between participants, and listed in end-to-end id order |
| `REPLAY` | no end-to-end id is settled, withdrawn or decided twice, in one cycle or across cycles |
| `HELD` | held groups have 2..4 distinct versions of one id; none of them is also settled; a version held at a close is still there at the next one |
| `DECISION` | each operator decision verifies under a **trusted operator key**, over the canonical message rebuilt with the house key id. The version chosen is the one that settled (its content digest); a rejection settles nothing; a version held at the last close is the one chosen |
| `NETTING` | positions re-netted per currency through the treaty engine (`zxv_treaty_run`, OP_NET, batches of 16 parties and 64 obligations, its own batching), cross-checked against the plain sum, compared row by row |
| `CONSERVATION` | the recorded positions sum to zero per currency |
| `FEE`, `SPLIT`, `CARRY` | `zxv_fee_assure_carry` and `zxv_fee_split` re-run per debtor and currency, in order. Each participant's remainder is followed from cycle to cycle, so a carry in that is not the last carry out is a finding |
| `POSTING` | the postings the replay needs (payins, payouts, fee shares; accounts, amounts, references, order); entry ids increasing; the first and last entry |
| `JOURNAL` | the journal parses and is internally sound: ids increasing, accounts exist, one currency per posting, holder accounts never below zero, every house account present once as a holder. Every cycle posting is in it exactly as recorded; no other posting falls between a cycle's first and last entry. No posting with a house reference is left uncovered |
| `BALANCE` | every opening and closing balance against the journal's prefix sums; each participant moved by its net position less its fee |
| `JDIGEST` | the journal up to the recorded head hashes to the recorded digest |
| `ISO` | every pacs.008 and camt.053 re-rendered with `zxv_msg_*` from the replay, its SHA3-256 compared |
| `JFORMAT` | no journal, or one that does not parse |

## Use

```c
ztw_create(read_only_cap, auditor_key, &w);   /* ZTW_E_WRITABLE on a cap that can post */
ztw_trust_house(w, house_pk);                 /* up to 8 each */
ztw_trust_operator(w, operator_pk);
st = ztw_verify(w, chain, chain_len, journal, journal_len, &summary);
/* ZXV_OK, or ZTW_E_DISCREPANCY: ztw_finding_count / ztw_finding_at */
const zind_chain *report = ztw_report(w);     /* signed by auditor_key */
```

`ztw_summary` gives these numbers:

- records read and cycles replayed;
- instructions re-netted and postings checked;
- findings;
- journal postings, and how many of them a cycle's digest covers;
- the SHA3-256 of each export.

The report is a `zind_chain` with domain `"tri-witness/report"`. It holds
one record per finding (kind `ZTW_REC_FINDING`), then a summary record
(`ZTW_REC_SUMMARY`):

```
finding: u16 code | u32 cycle | u32 record | u32 item | str field |
         i64 expected | i64 found | str detail | u8 n, raw[n] | u8 tag |
         subject[32] (the chain record's hash, or the journal's digest)
summary: "ZTWS1" | u32 records, cycles, instructions, postings, findings,
         findings listed | u64 journal postings, covered | chain digest[32] |
         journal digest[32] | u8 verdict tag (TRUE, or a Forbidden tag)
```

Each finding's tag is Forbidden: base FALSE, polarity FORBIDDEN, phase
MANIFEST. The verdict tag is TRUE only when nothing was found. The first
1,024 findings are listed and signed; the rest are counted.

Some tools work on the exports alone, with no witness and no capability:

- `ztw_cycle_count` counts the cycles in a chain export.
- `ztw_cycle_blob` reassembles one cycle record from its fragments.
- `ztw_blob_fields` lists every field of a cycle record: its name, offset,
  length, type, and whether the replay **derives** it or takes it as
  **declared** input.

A change to a derived field is always a finding, even if someone re-signs
the record with the house's own key. Declared fields include:

- message ids, names and the cycle label;
- the withdrawn and held instructions.

These are protected only by the house's signature, so a change re-signed
with a stolen house key is not visible to the replay.

## Example

`make demo` runs [`examples/audit_demo.c`](examples/audit_demo.c). A house
closes two cycles, one with an operator decision, and exports. A witness in
another context, holding a read-only capability, then does the following:

- it refuses the house's own capability;
- it finds the exports consistent and signs a one-record report;
- it is handed a journal in which one fee posting was changed by one minor
  unit, and lists six findings: the posting, the journal digest, two
  balances, the participant's movement and the camt.053 statement.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/tri-witness test        # 6,242 checks, fuzz smoke, demo, no-float
make -C sdk/industry/tri-witness SANITIZE=1 OUT=build/san test
make -C sdk/industry/tri-witness fuzz FUZZ_ITERS=20000
```

The tests, the fuzzer and the example link Sutra-Clear's library, which is
built into this kit's build directory, so they check real exports.
`test_twit` covers these areas:

- **The full round trip.** Two cycles in three currencies (one with no
  minor units) and four participants, with these events:
  - a conflict decided in the cycle;
  - a conflict held over and decided in the next cycle;
  - a withdrawal;
  - a Sutra treaty;
  - a cycle record that spans two chain records.

  The house exports, and the witness replays in its own context with no
  finding. The summary, the digests, the signed report and the auditors'
  tools are all checked.
- **Every field of every cycle record.**
  - Each of the 1,563 fields is changed and re-signed with the house key.
    All 1,354 derived fields are found.
  - The same change is made in the export without re-signing, for every
    declared field and every eighth derived one (381). Each is a `LINK`
    finding.
- **Every field of the journal.** Each of the 505 fields (account id,
  name, currency, kind; posting id, from, to, amount, reference) is
  changed, and each change is found.
- **Byte flips, cuts and bad inputs.**
  - Byte flips across the chain export.
  - Every cut of the journal, and cuts of the chain.
  - Another domain, and an empty chain.
- **Records.**
  - Cycle records cut into 500-byte fragments still replay clean, and a
    finding names the fragment that holds the changed field, with that
    record's hash as its subject.
  - A record of another kind.
  - A cycle replayed as cycle 3, swapped cycles and a dropped cycle.
  - Every fragment dropped in turn, and two fragments swapped.
  - Records signed by an untrusted key, then by a trusted key that is not
    the one the record names.
- **Operators, writers and capabilities.**
  - Operator keys the witness does not trust.
  - A second writer posting with a house reference.
  - Capabilities: a cap that can post, a cap that cannot sign, a revoked
    cap. The witness opens no accounts.
- **The report.** Each finding record is decoded against
  `ztw_finding_at`, and the report verifies under the witness key only.
- **Random histories.** Random multi-cycle histories replay clean.

`fuzz_twit` runs random traffic through a live house: instructions from a
small id pool (so conflicts and replays are common), operator decisions,
withdrawals and treaties. Cycles close at random. A fresh house starts
every 8 closes, so each replay stays short. The fuzzer checks these
properties:

- after every close, the replay is clean, every journal posting is covered
  and the report verifies;
- a random field of a random cycle record, changed and re-signed, is always
  found if it is derived;
- a random bit flipped in the chain export is always found;
- a random bit flipped in the journal export is always found.

The output is byte-identical under gcc and clang.

## Limits

- **Consistency, not truth.** The witness can show that the records agree
  with each other, with the journal and with the arithmetic. It cannot show
  any of the following:
  - that an instruction was authorised by the payer;
  - that the journal is the whole ledger;
  - that the house's or an operator's key was not stolen.

  Declared fields re-signed with the house key are invisible to the
  replay.
- **One house per export.** A chain is replayed from its first record, and
  a carry or replay check needs every earlier cycle. A partial export is
  reported as such: sequence, carry and coverage findings.
- **The journal is taken as given.** It is the house's own export. A house
  that controls its ledger could export a journal that agrees with its
  records. The witness checks the journal's internal soundness and its
  agreement with the records, not its origin.
- **Same formats, same library.** The witness re-runs `zxv_treaty_run`,
  `zxv_fee_*` and `zxv_msg_*` from the libzxv it is linked with. A
  different library version that changed their output would show as
  findings, and that is intended.
- **No connectivity or certification.** Like Sutra-Clear, it is designed to
  support PAPSS and SWIFT ISO 20022 formats. It is not connected to, and is
  not endorsed or certified by, PAPSS, SWIFT or any central bank or
  regulator. A report is evidence for a human auditor, not a legal finding.
