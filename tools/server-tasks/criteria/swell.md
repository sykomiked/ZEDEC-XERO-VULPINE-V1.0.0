# Criteria — swell

| | |
|---|---|
| app | `swell` |
| matrix layer | 251 |
| icon group | L2 |
| wave spec | `wave20/251-swell.md` |

## Purpose

Rehearses ensembles: a rehearsal hall (`pspace.create`, `pspace.members`, `hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`), players' devices (`devmesh.devices`, `net.peers`), scores and parts in the Hoard (`files.read`, `files.write`), the clock (`sys.info`). Audio in and out has no method (as Fiddlehead): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `fs.read`
- `fs.write`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | player roster |
| FORTRAN | trebago | tempo sync |
| Ada | eve | latency limits |
| APL | kron | score matrix |
| MUMPS | myra | part tree |
| Erlang | - | messaging |
| Simula 67 | - | simulation |
| System/360 assembler | - | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id swell
matrix 251        # the graph's slot IS the matrix layer
name Swell
needs peers
needs net
needs fs.read
needs fs.write
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

