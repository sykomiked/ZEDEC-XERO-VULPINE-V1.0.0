# Criteria — pharos

| | |
|---|---|
| app | `pharos` |
| matrix layer | 326 |
| icon group | L3 |
| wave spec | `wave27/326-pharos.md` |

## Purpose

broadcasts emergency warnings across radio, mesh and screens, confirming who has received them (`ehop.bands`, `ehop.seal`, `harmonic.encode`, `pspace.post`, `chat.post`, `net.peers`), each sent after a person approves. zxvd has no method for screen broadcast to other devices: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `notify`
- `sys.read`
- `journal`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | warning logs |
| FORTRAN | trebago | hazard spread model |
| Ada | eve | broadcast priority limits |
| APL | kron | coverage grid |
| MUMPS | myra | region tree |
| Go | go | messaging |
| LabVIEW | - | signals |
| TypeScript | typescript | view and model |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id pharos
matrix 326        # the graph's slot IS the matrix layer
name Pharos
needs net
needs peers
needs notify
needs sys.read
needs journal
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

