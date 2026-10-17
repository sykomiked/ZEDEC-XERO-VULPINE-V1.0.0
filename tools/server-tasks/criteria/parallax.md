# Criteria — parallax

| | |
|---|---|
| app | `parallax` |
| matrix layer | 294 |
| icon group | L3 |
| wave spec | `wave24/294-parallax.md` |

## Purpose

a visual effects studio for layered images, motion and the depth effect, from sketch to finished shot, kept in the Hoard (`files.read`, `files.write`, `sys.disk`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | production budget |
| FORTRAN | trebago | render maths |
| Ada | eve | frame budget limits |
| APL | kron | pixel arrays |
| MUMPS | myra | shot hierarchy |
| MATLAB | - | matrices and images |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id parallax
matrix 294        # the graph's slot IS the matrix layer
name Parallax
needs fs.read
needs fs.write
needs sys.read
needs ai.ask
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

