# Criteria — joist

| | |
|---|---|
| app | `joist` |
| matrix layer | 158 |
| icon group | L2 |
| wave spec | `wave13/158-joist.md` |

## Purpose

plans construction projects: structure models, materials, costs and task schedules, kept in the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | bill of quantities |
| FORTRAN | trebago | load calculation |
| Ada | eve | safety margins |
| APL | kron | material grid |
| MUMPS | myra | project tree |
| Delphi | - | structure |
| Simula I | - | simulation |
| Julia | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id joist
matrix 158        # the graph's slot IS the matrix layer
name Joist
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

