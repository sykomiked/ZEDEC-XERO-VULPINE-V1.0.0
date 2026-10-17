# Criteria — col

| | |
|---|---|
| app | `col` |
| matrix layer | 269 |
| icon group | L2 |
| wave spec | `wave22/269-col.md` |

## Purpose

Gates connections between ZXV and the outside web, names and identities: network mode (`net.command`, `sys.net`), answering peers (`peer.serve`), identities and signatures (`web4.card_verify`, `web4.verify`, `web4.cid`), peers (`net.peers`), switches (`features.get`). An HTTP gate and DNS resolution have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | access ledger |
| FORTRAN | trebago | traffic model |
| Ada | eve | rate limits |
| APL | kron | route table |
| MUMPS | myra | identity tree |
| Gleam | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id col
matrix 269        # the graph's slot IS the matrix layer
name Col
needs net
needs peers
needs sys.read
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

