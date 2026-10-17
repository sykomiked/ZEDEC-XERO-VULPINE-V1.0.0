# Criteria — shanty

| | |
|---|---|
| app | `shanty` |
| matrix layer | 362 |
| icon group | L3 |
| wave spec | `wave30/362-shanty.md` |

## Purpose

Forecasts tides, waves and coastal flooding for harbours and shore towns: districts (`place.here`, `place.list`), stations (`net.peers`, `devmesh.devices`), records (`files.read`, `files.write`), alerts sent once a person approves (`pspace.post`), history (`journal.query`). Gauge and buoy readings have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `peers`
- `net`
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
| COBOL | sutra | harbour dues |
| FORTRAN | trebago | wave models |
| Ada | eve | flood thresholds |
| APL | kron | tide grids |
| MUMPS | myra | coastline tree |
| Elixir | - | messaging |
| Forth | forth | machine control |
| Clojure | - | dataflow |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id shanty
matrix 362        # the graph's slot IS the matrix layer
name Shanty
needs location
needs peers
needs net
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

