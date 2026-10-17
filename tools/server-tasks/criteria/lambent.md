# Criteria — lambent

| | |
|---|---|
| app | `lambent` |
| matrix layer | 288 |
| icon group | L3 |
| wave spec | `wave24/288-lambent.md` |

## Purpose

guides a life change such as a new career or move: goals, courses, tasks and milestones in one plan in the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | training costs |
| FORTRAN | trebago | progress forecasts |
| Ada | eve | weekly load limits |
| APL | kron | skill matrix |
| MUMPS | myra | life milestone tree |
| Pascal | pascal | step-by-step teaching |
| Python | python | AI |
| IPL | - | lists |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lambent
matrix 288        # the graph's slot IS the matrix layer
name Lambent
needs fs.read
needs fs.write
needs search
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

