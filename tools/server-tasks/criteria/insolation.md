# Criteria — insolation

| | |
|---|---|
| app | `insolation` |
| matrix layer | 335 |
| icon group | L3 |
| wave spec | `wave28/335-insolation.md` |

## Purpose

runs a community wind and solar co-op: generation, storage, shares, mutual-aid grants and grid exports (`power.status`, `ledger.entries`, `ledger.post`, `ledger.hold`, `devmesh.devices`). zxvd has no method for turbine, inverter and meter readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `ledger.read`
- `ledger.post`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | member grants |
| FORTRAN | trebago | wind and sun yield model |
| Ada | eve | grid export limits |
| APL | kron | turbine and panel grid |
| MUMPS | myra | member share tree |
| Erlang | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id insolation
matrix 335        # the graph's slot IS the matrix layer
name Insolation
needs sys.read
needs ledger.read
needs ledger.post
needs peers
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

