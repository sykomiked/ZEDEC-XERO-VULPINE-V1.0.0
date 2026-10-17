# Criteria — lockbox

| | |
|---|---|
| app | `lockbox` |
| matrix layer | 9 |
| icon group | L1 |
| wave spec | `wave2/009-lockbox.md` |

## Purpose

ML-DSA identity keys, sealed hardware keys, saved passwords (`pqsec`, `identity`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| password strength score (length, classes, repeats as integer inputs) in sutra + kron witnessed | sutra, kron |
| key expiry days left in kron + C++ wasm | kron, c, wasm |
| limits (min length 12, max 1024) in eve. Never log or store secret bytes in tests | eve |
| inputs are lengths and counts only | — |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| APL | kron | arrays |
| C++ | cpp | exact numerics |
| COBOL | sutra | money and ledgers |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lockbox
matrix 9        # the graph's slot IS the matrix layer
name Lockbox
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

