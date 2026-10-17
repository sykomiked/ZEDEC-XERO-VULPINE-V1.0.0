# Criteria — buckram

| | |
|---|---|
| app | `buckram` |
| matrix layer | 377 |
| icon group | L3 |
| wave spec | `wave31/377-buckram.md` |

## Purpose

Hardens devices and edge sensors: firmware checks and update rollouts (`update.check`), the fleet's links (`net.peers`, `devmesh.devices`, `devmesh.revoke`), failure reports (`log.tail`, `journal.query`), machine facts (`sys.info`). Firmware reads from other devices and tamper seals have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `sys.read`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | device warranties |
| FORTRAN | trebago | failure rate model |
| Ada | eve | firmware limits |
| APL | kron | fleet grid |
| MUMPS | myra | device tree |
| B | - | low-level system |
| Forth | forth | small interpreters |
| RPG | - | reports |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id buckram
matrix 377        # the graph's slot IS the matrix layer
name Buckram
needs net
needs peers
needs sys.read
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

