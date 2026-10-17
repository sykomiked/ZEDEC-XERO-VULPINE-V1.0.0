# Criteria — chainman

| | |
|---|---|
| app | `chainman` |
| matrix layer | 154 |
| icon group | L2 |
| wave spec | `wave12/154-chainman.md` |

## Purpose

surveys land parcels, boundaries and waypoints (`place.here`, `place.add`, `place.list`) into maps with measured records in the Hoard (`files.read`, `files.write`). zxvd has no method for a general signing method for a survey record (no daemon method signs an arbitrary file): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | title register |
| FORTRAN | trebago | area computation |
| Ada | eve | boundary tolerances |
| APL | kron | parcel grid |
| MUMPS | myra | land hierarchy |
| Haskell | - | proofs of correctness |
| ML | - | proofs of correctness |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id chainman
matrix 154        # the graph's slot IS the matrix layer
name Chainman
needs location
needs fs.read
needs fs.write
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

