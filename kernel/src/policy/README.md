<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

```
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
```

# zxv_policy: policy engine and contract DSL

A deterministic policy evaluation engine that enforces machine-verifiable
transactional constraints, bilateral treaty rules, and jurisdictional
parameters prior to ledger commit.

An agreement has two layers:

* **Prose layer.** The agreement people read and sign, kept as an external
  document. Only its SHA-256 enters the system (`DOC`). Every transfer
  carries the same digest, and a transfer bound to another document is a
  fault.
* **Rule layer.** A short contract in the DSL below. It holds only bounded
  predicates: balance caps, cumulative limits, signature thresholds, a
  validity window, lockout periods, jurisdiction allow/deny tags, and dated
  payment terms. It holds nothing open-ended, so every answer is computable
  and reproducible byte for byte.

The engine answers `POLICY_ALLOW`, `POLICY_DENY` or `POLICY_FAULT`. A
contradiction (Belnap BOTH) is never allowed: it is denied with
`escalate` set, so a person decides.

| File | What it is |
|---|---|
| `zxp.h`, `zxp.c` | Contract DSL. Sutra text and the COBOL record (`copybooks/ZXVCNTR.cpy`, 4419 bytes). Parsing fails closed with a byte offset, and each form has one spelling, so parse then emit gives back the input byte for byte. |
| `policy.h`, `policy.c` | Four-valued transfer checks, term states, jurisdiction packs, and lowering to a `zxt` treaty with escrow and mediation bundles. |
| `policy_cobol.h`, `policy_cobol.c` | Transfer and result records (`ZXVPTXN.cpy` 129 bytes, `ZXVPRES.cpy` 41 bytes) and `CALL 'ZXV_POLICY'`. |
| `zxp_draft.h`, `zxp_draft.c` | Templates (services, sale, loan, MOU, bilateral) that produce a valid rule layer and a Markdown prose skeleton marked for counsel. An informational table of legal-family labels. |
| `cobol/PLCYDEMO.cob`, `cobol/policy_parity.c` | A COBOL program that fills the copybooks and calls `ZXV_POLICY`, checked byte for byte against the same contract built in C. |

All of `zxp.c`, `zxp_draft.c`, `policy.c` and `policy_cobol.c` are
freestanding C11 with no libc, allocation, recursion or floats. `make
test-policy` checks this on aarch64 and x86_64.

## The DSL (Sutra form)

```
CONTRACT SUPPLY-2027 20261015
DOC 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
LAW KE
PARTY ACME US-DE
PARTY BETA KE
SIGN 2
WINDOW 20261015 20271231
LOCK 20261224 20261226
CAP USD 2000000
LIMIT ACME USD 1500000
ALLOW KE
ALLOW US
DENY US-NY
DUE T1 ACME BETA USD 1200000 20261231 PENALTY 12000
DUE T2 ACME BETA USD 300000 20270331 AFTER T1
GATE G1 ALL T1 T2
END
```

Lines come in this order. Lists are sorted and hold no duplicates.

| Line | Meaning | Limits |
|---|---|---|
| `CONTRACT name date` | name `[A-Z][A-Z0-9-]*`, signing date `YYYYMMDD` | name 30 |
| `DOC hex` | SHA-256 of the prose, 64 lowercase hex | |
| `LAW jur` | governing-law tags, `CC` or `CC-SUB` | 1 to 4 |
| `PARTY id jur` | parties; `ESCROW` is reserved | 1 to 15, id 8 |
| `SIGN k` | signatures needed on each transfer | 1 to parties |
| `WINDOW from to` | dates the contract is in force | |
| `LOCK from to` | lockouts inside which nothing moves | 8, no overlap |
| `CAP asset amount` | per-transfer ceiling; if any CAP exists, other assets are refused | 8 |
| `LIMIT id asset amount` | cumulative ceiling for one payer | 32 |
| `ALLOW jur` / `DENY jur` | counterparty tags; `US` covers `US-NY` | 16 each |
| `DUE id payer payee asset amount date [PENALTY n] [AFTER ref]` | a dated payment term, optionally conditional on an earlier clause | 32 clauses in all |
| `GATE id ALL\|ANY ref ref ...` | truth meet or join of earlier clauses | 2 to 8 refs |

Amounts are integers in the asset's smallest unit, up to `PIC S9(18)`. A
tag may sit in both ALLOW and DENY. That is legal syntax and is evaluated
as BOTH.

## Four values

The engine reuses the kernel's `lpres_state_t` (`../lpres/lpres.h`). It
encodes two evidence bits: bit 0 means "something says true" and bit 1
means "something says false".

| | bits | meaning |
|---|---|---|
| NEITHER (N) | 00 | nobody has a view yet |
| TRUE (T) | 01 | |
| FALSE (F) | 10 | |
| BOTH (B) | 11 | contradiction: escalate, never halt |

The **knowledge join** (`a | b`) merges evidence. Observer 1 saying T and
observer 2 saying F gives B. Each jurisdiction pack is an independent
evaluator merged this way, so civil T, common F and customary N together
give B. That isolates the clause, and the rest of the contract proceeds.

**Truth operations** use the kernel's own tables (`lpres_conjoin`,
`lpres_disjoin`, `lpres_negate`). They keep B ∧ N = N and B ∨ N = B, where
textbook Belnap-Dunn FOUR gives F and T. The kernel rule (an unknown never
turns into a definite answer) is deliberate and shared by every module, and
this engine does not fork it. It shows up only in GATEs that mix a
contested clause with an undecided one. `ALL(B, N)` is N, so the gate
waits. `ANY(B, N)` is B.

## Transfers

`policy_check` judges a proposed transfer against every rule. The rules are
window, lock, signature count, payer signed, asset, cap, cumulative limit,
payer jurisdiction, payee jurisdiction, and operator packs. Each rule is T
or F, except jurisdictions, which are the knowledge join of what the
contract and every applicable pack say, with N counted as permitted. The
decision is the truth meet of all of them:

| value | decision |
|---|---|
| T | ALLOW |
| F | DENY, with `deny_mask` naming the rules |
| B | DENY with `escalate`, with `both_mask` naming the rules |
| malformed input, or the wrong DOC | FAULT |

`policy_commit` does the same and, on ALLOW, adds the amount to the
running LIMIT totals. The host commits its own ledger only after an ALLOW.

## Terms and settlement

`policy_terms` evaluates every clause at a date from signed attestations.
An attestation says T (performed) or F (not performed), with a date and an
attestor number. `policy_att_text` gives the canonical text an attestor
signs.

| status | when |
|---|---|
| WAITING | the AFTER condition is N |
| OPEN / DUE | before its date / on its date, nothing attested |
| PERFORMED / LATE | attested T on or before / after its date |
| BREACHED | attested F, or the date passed with nothing attested |
| CONTESTED | the evidence or condition is B |
| EXCUSED | the AFTER condition came out F |

`policy_lower` turns the terms into a NET `zxt` treaty for one asset at one
date. Feed it to `zxt_exec`, which charges the 0.08889% fee.

| term | lowered as |
|---|---|
| T, performed | nothing (it already moved) |
| due today, undisputed | an OWE line, payer to payee |
| F, breached or lapsed | an OWE line, plus a PENALTY line if the contract names one |
| N, open or waiting | deferred until its date |
| B, contested | an OWE line from the payer to the reserved ESCROW party, plus a mediation entry |
| denied by a pack | not lowered, and listed as blocked |

`policy_bundle_text` writes the mediation bundle for each escrowed clause.
It names the clause, its byte offset in the canonical Sutra, the document
digest, the amount, and the attestations on each side. The host hashes and
signs it for whoever resolves the dispute. Everything else settles
normally. The system does not halt on ambiguity.

## Jurisdiction packs

A pack is operator configuration. It holds a scope tag, an informational
family label, flags (`NO_PENALTY`: penalty terms are F; `DEFER`: all terms
N), counterparty allow/deny lists, and optional `term_fn`/`txn_fn` hooks. A
pack applies when its scope covers a LAW tag or a party's tag. `txn_fn` is
where a host binds another module, such as a jurisdiction-adapter check.

A pack does not state what the law is. It states what this deployment has
been configured to allow. Writing packs that match real law is the
operator's job, with counsel.

## COBOL

```cobol
       COPY ZXVCNTR.  COPY ZXVPTXN.  COPY ZXVPRES.
       ...
       CALL 'ZXV_POLICY' USING ZXV-CONTRACT ZXV-TRANSFER
           ZXV-POLICY-RESULT
```

`RETURN-CODE` is 0 for ALLOW, 1 for DENY, or the negative fault code, with
`ZP-WHERE` holding the byte offset of a contract-record fault. The call is
stateless. It uses no packs, and limits are judged from zero. A host that
keeps running totals calls `policy_commit` from C.

## Drafting

`zxp_draft` fills a template from a few parameters and always yields a
contract that validates. Its `DOC` is zero until the prose is final.
`zxp_draft_prose` writes a Markdown skeleton. It opens with a not-legal-advice
notice, marks every substantive section `[TO BE COMPLETED BY COUNSEL]`, and
shows the governing law's family label for information only. These are
templates, not advice.

## Limits

* The engine checks only what the rule layer states. It cannot tell whether
  a contract is lawful, fair or enforceable anywhere.
* Jurisdiction tags are labels the parties and operator supply. Nothing
  here verifies where anyone is.
* Attestations are only as good as their signers. The engine counts and
  merges them and does not judge them.
* The family table in `zxp_draft.c` is a rough, informational
  classification. No result depends on it.

## Tests

```
make test-policy        # unit suites under ASan/UBSan, fuzz, freestanding check
make test-policy-cobol  # GnuCOBOL parity (skips without cobc unless ZXV_REQUIRE_COBOL=1)
make test-policy-mut    # what the mutation harness runs per mutant
```
