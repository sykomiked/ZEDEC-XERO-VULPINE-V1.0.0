# Criteria — atrium

| | |
|---|---|
| app | `atrium` |
| matrix layer | 332 |
| icon group | L3 |
| wave spec | `wave28/332-atrium.md` |

## Purpose

runs a guesthouse or venue: rooms, bookings, catering, housekeeping and guest bills (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `print.submit`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ledger.read`
- `ledger.post`
- `print`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | guest folios |
| FORTRAN | trebago | occupancy forecasts |
| Ada | eve | capacity limits |
| APL | kron | room grid |
| MUMPS | myra | guest history |
| System/360 assembler | - | low-level system |
| Swift | swift | domain objects |
| WebAssembly | wasm | sandboxing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id atrium
matrix 332        # the graph's slot IS the matrix layer
name Atrium
needs fs.read
needs fs.write
needs ledger.read
needs ledger.post
needs print
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

