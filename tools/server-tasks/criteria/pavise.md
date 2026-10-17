# Criteria — pavise

| | |
|---|---|
| app | `pavise` |
| matrix layer | 153 |
| icon group | L2 |
| wave spec | `wave12/153-pavise.md` |

## Purpose

one security perimeter combining firewall paths, app permissions and keys into a single policy, read from `features.get`, `sys.net`, `net.peers`, `devmesh.devices`, `passkeys.list`, with its history from `journal.query` and `journal.verify`. zxvd has no method for firewall rule write, capability grant write: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `net`
- `peers`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | access ledger |
| FORTRAN | trebago | threat scoring |
| Ada | eve | declared policy bounds |
| APL | kron | permission matrix |
| MUMPS | myra | trust tree |
| Prolog | - | rules |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id pavise
matrix 153        # the graph's slot IS the matrix layer
name Pavise
needs sys.read
needs net
needs peers
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

