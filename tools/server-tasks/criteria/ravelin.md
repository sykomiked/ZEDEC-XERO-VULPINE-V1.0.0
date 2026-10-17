# Criteria — ravelin

| | |
|---|---|
| app | `ravelin` |
| matrix layer | 333 |
| icon group | L3 |
| wave spec | `wave28/333-ravelin.md` |

## Purpose

protects critical sites such as power, water and data halls: physical access, cameras, alarms and continuity plans (`journal.query`, `log.tail`, `sys.info`, `features.get`, `files.read`, `files.write`). zxvd has no method for access control, camera and plant sensor feeds: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `journal`
- `sys.read`
- `notify`
- `fs.read`
- `fs.write`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | incident costs |
| FORTRAN | trebago | failure models |
| Ada | eve | safety interlocks |
| APL | kron | zone sensor grid |
| MUMPS | myra | site asset tree |
| Simula I | - | simulation |
| LabVIEW | - | signals |
| Delphi | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id ravelin
matrix 333        # the graph's slot IS the matrix layer
name Ravelin
needs journal
needs sys.read
needs notify
needs fs.read
needs fs.write
needs net
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

