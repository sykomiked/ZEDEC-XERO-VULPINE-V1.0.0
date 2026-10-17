# Criteria — seine

| | |
|---|---|
| app | `seine` |
| matrix layer | 382 |
| icon group | L3 |
| wave spec | `wave32/382-seine.md` |

## Purpose

Spreads a mesh of low-power sensors across water or land, gathering readings that survive lost nodes: pairing (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), relays (`ehop.bands`, `ehop.seal`, `ehop.open`), peers (`net.peers`), readings in the Hoard (`files.read`, `files.write`). Sensor readings have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `location`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | data sharing credits |
| FORTRAN | trebago | signal filtering |
| Ada | eve | battery limits |
| APL | kron | reading arrays |
| MUMPS | myra | node tree |
| Gleam | - | messaging |
| Ladder logic | - | machine control |
| Clojure | - | dataflow |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id seine
matrix 382        # the graph's slot IS the matrix layer
name Seine
needs peers
needs net
needs location
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

