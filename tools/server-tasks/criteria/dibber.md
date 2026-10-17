# Criteria — dibber

| | |
|---|---|
| app | `dibber` |
| matrix layer | 149 |
| icon group | L2 |
| wave spec | `wave12/149-dibber.md` |

## Purpose

plans plantings, watering and harvests for a plot from weather and land budgets, kept in the Hoard (`files.read`, `files.write`), for where the plot is (`place.here`). zxvd has no method for a weather source (as Weatherglass reports), soil sensor readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `location`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | seed and supply costs |
| FORTRAN | trebago | growth model |
| Ada | eve | water limits |
| APL | kron | bed grid |
| MUMPS | myra | plot hierarchy |
| Simula 67 | - | simulation |
| Oberon | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id dibber
matrix 149        # the graph's slot IS the matrix layer
name Dibber
needs fs.read
needs fs.write
needs location
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

