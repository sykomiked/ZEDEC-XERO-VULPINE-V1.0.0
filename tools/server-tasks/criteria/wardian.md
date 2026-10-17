# Criteria — wardian

| | |
|---|---|
| app | `wardian` |
| matrix layer | 405 |
| icon group | L3 |
| wave spec | `wave34/405-wardian.md` |

## Purpose

Runs greenhouses, vertical farms or terrariums: climate, water, light, plant health and harvests in the Hoard (`files.read`, `files.write`), stations (`place.list`, `devmesh.devices`), produce sales (`ledger.post`, `ledger.entries`). Climate sensors and actuators have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `location`
- `peers`
- `net`
- `ledger.read`
- `ledger.post`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | produce sales |
| FORTRAN | trebago | climate model |
| Ada | eve | humidity and CO2 limits |
| APL | kron | sensor grid |
| MUMPS | myra | bed and plant tree |
| Ladder logic | - | machine control |
| Simula I | - | simulation |
| Delphi | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id wardian
matrix 405        # the graph's slot IS the matrix layer
name Wardian
needs fs.read
needs fs.write
needs location
needs peers
needs net
needs ledger.read
needs ledger.post
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

