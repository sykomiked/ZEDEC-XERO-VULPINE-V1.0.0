# Criteria — chandler

| | |
|---|---|
| app | `chandler` |
| matrix layer | 161 |
| icon group | L2 |
| wave spec | `wave13/161-chandler.md` |

## Purpose

keeps vessel logs, tides, crew and provisions for boats and small fleets in the Hoard (`files.read`, `files.write`), with places (`place.here`, `place.list`) and radio messages (`harmonic.encode`). zxvd has no method for tide predictions source: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `fs.read`
- `fs.write`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | provision accounts |
| FORTRAN | trebago | tide model |
| Ada | eve | load limits |
| APL | kron | tide tables |
| MUMPS | myra | crew and vessel tree |
| Occam | - | concurrency |
| Java | java | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id chandler
matrix 161        # the graph's slot IS the matrix layer
name Chandler
needs location
needs fs.read
needs fs.write
needs net
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

