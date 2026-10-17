# Criteria — chronometer

| | |
|---|---|
| app | `chronometer` |
| matrix layer | 404 |
| icon group | L3 |
| wave spec | `wave34/404-chronometer.md` |

## Purpose

Coordinates shifts, bookings and deadlines across time zones, with exact time from trusted clocks: the clock (`sys.info`, `devmesh.devices`, `net.peers`), rotas and bookings in the Hoard (`files.read`, `files.write`), records proven by the journal (`journal.query`). A peer clock-offset query has no method (as Escapement): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `peers`
- `fs.read`
- `fs.write`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | billable hours |
| FORTRAN | trebago | clock drift correction |
| Ada | eve | rest limits |
| APL | kron | rota grid |
| MUMPS | myra | team tree |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |
| Occam | - | concurrency |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id chronometer
matrix 404        # the graph's slot IS the matrix layer
name Chronometer
needs sys.read
needs peers
needs fs.read
needs fs.write
needs journal
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

