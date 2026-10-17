# Criteria — cupel

| | |
|---|---|
| app | `cupel` |
| matrix layer | 179 |
| icon group | L2 |
| wave spec | `wave14/179-cupel.md` |

## Purpose

simulates atoms, molecules and physical systems on the integer engine and compares models, runs kept in the Hoard (`files.read`, `files.write`), machine facts from `sys.info`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | run records |
| FORTRAN | trebago | simulation core |
| Ada | eve | precision bounds |
| APL | kron | lattice arrays |
| MUMPS | myra | model tree |
| Simula 67 | - | simulation |
| Python | python | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cupel
matrix 179        # the graph's slot IS the matrix layer
name Cupel
needs fs.read
needs fs.write
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

