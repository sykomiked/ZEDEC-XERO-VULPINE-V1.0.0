# Criteria — privateer

| | |
|---|---|
| app | `privateer` |
| matrix layer | 386 |
| icon group | L3 |
| wave spec | `wave32/386-privateer.md` |

## Purpose

Hunts threats on the network and devices, tests defences safely and contains intrusions: events (`log.tail`, `journal.query`, `journal.verify`), files (`files.stat`, `search.query`), network mode (`net.command` off/lan, owner-approved), peers (`net.peers`, `devmesh.devices`, `devmesh.revoke` owner-approved), switches (`features.get`).

## Capabilities (needs — the exact registry names)

- `journal`
- `search`
- `fs.read`
- `sys.read`
- `net`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | incident costs |
| FORTRAN | trebago | anomaly scoring |
| Ada | eve | containment limits |
| APL | kron | event matrix |
| MUMPS | myra | asset tree |
| Go | go | messaging |
| BCPL | - | low-level system |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id privateer
matrix 386        # the graph's slot IS the matrix layer
name Privateer
needs journal
needs search
needs fs.read
needs sys.read
needs net
needs peers
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

