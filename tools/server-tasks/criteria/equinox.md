# Criteria — equinox

| | |
|---|---|
| app | `equinox` |
| matrix layer | 312 |
| icon group | L3 |
| wave spec | `wave26/312-equinox.md` |

## Purpose

time capsules and time-locked promises: messages, wills and payments released on a chosen date (`files.write`, `files.read`, `estate.status`, `journal.query`, `sys.info`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `sys.read`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | scheduled payments |
| FORTRAN | trebago | date arithmetic |
| Ada | eve | release date limits |
| APL | kron | capsule calendar grid |
| MUMPS | myra | recipient tree |
| Eiffel | - | contracts |
| Solidity | - | contracts |
| Gleam | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id equinox
matrix 312        # the graph's slot IS the matrix layer
name Equinox
needs fs.read
needs fs.write
needs journal
needs sys.read
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

