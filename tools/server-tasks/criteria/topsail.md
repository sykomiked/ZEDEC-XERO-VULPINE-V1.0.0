# Criteria — topsail

| | |
|---|---|
| app | `topsail` |
| matrix layer | 399 |
| icon group | L3 |
| wave spec | `wave33/399-topsail.md` |

## Purpose

Hosts apps and files on your own devices and trusted peers as one personal cloud: sync (`spaces.list`, `files.cross`, `backup.run`, `backup.list`), devices and peers (`devmesh.devices`, `net.peers`, `peer.status`), the cloud top (`spaces.get`, `spaces.set_cid`, `aipi.put`, `aipi.get`), budgets (`power.budget`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `backup`
- `peers`
- `net`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | hosting ledger |
| FORTRAN | trebago | capacity model |
| Ada | eve | resource caps |
| APL | kron | replica grid |
| MUMPS | myra | tenant tree |
| Gleam | - | messaging |
| Elixir | - | fault handling |
| Nim | - | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id topsail
matrix 399        # the graph's slot IS the matrix layer
name Topsail
needs fs.read
needs fs.write
needs backup
needs peers
needs net
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

