# Criteria — tideturn

| | |
|---|---|
| app | `tideturn` |
| matrix layer | 6 |
| icon group | L1 |
| wave spec | `wave2/006-tideturn.md` |

## Purpose

dual-signed OS and app updates with an audit trail (`update.check`, `update`).

## Capabilities (needs — the exact registry names)

- `net`
- `sys.read`

## Entries and languages (from the spec's Logic line)

| the entry's job (from the spec) | languages the spec names |
|---|---|
| version ordering (major.minor.patch compare) in sutra + kron witnessed | sutra, kron |
| "both signatures present and valid" as a truth in eve + kron (inputs are the two verify results as 0/1) | eve, kron |
| update record in myra | myra |

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| APL | kron | arrays |
| COBOL | sutra | money and ledgers |
| AWK | - | reports |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tideturn
matrix 6        # the graph's slot IS the matrix layer
name Tideturn
needs net
needs sys.read
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

