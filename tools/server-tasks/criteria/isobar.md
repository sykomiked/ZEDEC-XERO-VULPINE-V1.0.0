# Criteria — isobar

| | |
|---|---|
| app | `isobar` |
| matrix layer | 239 |
| icon group | L2 |
| wave spec | `wave19/239-isobar.md` |

## Purpose

Runs a local weather station network: stations and sites (`place.here`, `place.list`), station nodes (`devmesh.devices`, `net.peers`), records in the Hoard (`files.read`, `files.write`). Sensor input has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `peers`
- `net`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | observation records |
| FORTRAN | trebago | forecast model |
| Ada | eve | sensor range limits |
| APL | kron | station grid |
| MUMPS | myra | site tree |
| IEC 61131-3 Structured Text | - | machine control |
| Elixir | - | messaging |
| Clojure | - | dataflow |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id isobar
matrix 239        # the graph's slot IS the matrix layer
name Isobar
needs location
needs peers
needs net
needs fs.read
needs fs.write
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

