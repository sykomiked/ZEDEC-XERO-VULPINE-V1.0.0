# Criteria — tatting

| | |
|---|---|
| app | `tatting` |
| matrix layer | 372 |
| icon group | L3 |
| wave spec | `wave31/372-tatting.md` |

## Purpose

Keeps vaccines, food and samples cold from source to door: consignments and sensor logs in the Hoard (`files.read`, `files.write`), cold archives (`backup.run`, `backup.list`, `backup.verify`), parcels (`freight.pack`, `net.peers`, `peer.status`), places (`place.list`). Temperature loggers have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `backup`
- `sys.read`
- `peers`
- `net`
- `location`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | shipment billing |
| FORTRAN | trebago | thermal decay model |
| Ada | eve | temperature limits |
| APL | kron | sensor arrays |
| MUMPS | myra | consignment tree |
| JOVIAL | - | machine control |
| Gleam | - | messaging |
| Scala | - | dataflow |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id tatting
matrix 372        # the graph's slot IS the matrix layer
name Tatting
needs fs.read
needs fs.write
needs backup
needs sys.read
needs peers
needs net
needs location
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

