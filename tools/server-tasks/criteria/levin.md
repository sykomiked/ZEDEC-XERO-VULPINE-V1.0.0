# Criteria — levin

| | |
|---|---|
| app | `levin` |
| matrix layer | 236 |
| icon group | L2 |
| wave spec | `wave19/236-levin.md` |

## Purpose

Plans home wiring: circuits, load calculations, test runs and safety checks, kept in the Hoard (`files.read`, `files.write`); the machine's own draw from `power.status`. Meter and clamp readings have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | permit records |
| FORTRAN | trebago | load and voltage drop |
| Ada | eve | breaker limits |
| APL | kron | circuit matrix |
| MUMPS | myra | panel tree |
| Verilog | - | circuits |
| Simula I | - | simulation |
| Delphi | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id levin
matrix 236        # the graph's slot IS the matrix layer
name Levin
needs fs.read
needs fs.write
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

