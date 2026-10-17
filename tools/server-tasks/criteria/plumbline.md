# Criteria — plumbline

| | |
|---|---|
| app | `plumbline` |
| matrix layer | 250 |
| icon group | L2 |
| wave spec | `wave20/250-plumbline.md` |

## Purpose

Fronts the policy engine (kernel/src/policy with policy.c, policy_cobol.c and copybooks; kernel/src/onepolicy; kernel/src/juris with juris.c and juris_vhub.c). Jurisdiction switches (`jurisdiction.get`, `jurisdiction.set`), feature switches and which are regulated (`features.get`, `features.set`), nature-boundary budgets (`power.budget`, `power.status`), the record (`journal.query`). Reading the rulebook and the list of advisory actions have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `journal`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | rule history |
| FORTRAN | trebago | budget arithmetic |
| Ada | eve | hard limits |
| APL | kron | rule matrix |
| MUMPS | myra | jurisdiction records |
| Prolog | - | rules |
| ALGOL 60 | algol60 | proofs of correctness |
| Haskell | - | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id plumbline
matrix 250        # the graph's slot IS the matrix layer
name Plumbline
needs sys.read
needs journal
needs ledger.read
needs notify
provides <entry> num|truth <arg>[,<arg>...]
logic <lang> <entry> <<
<source>
.
check <entry> <arg>=<int>[,<int>...] = <answer>[,<answer>...]
```

Rules the loader enforces (a violation refuses the manifest):

- Every `provides` entry must have at least one `logic` line.
- Every `logic` entry must have a `check` line with exact answers.
- The checks' inputs are integers; answers are integers, `n/d`,
  `true`, `false`, `neither`, or the word `both` (two languages
  disagreeing).
- A truth entry's checks use the four-valued answers, never 0/1.
- Inline sources end with a line containing only `.`.

