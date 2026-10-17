# Criteria — alembic

| | |
|---|---|
| app | `alembic` |
| matrix layer | 155 |
| icon group | L2 |
| wave spec | `wave12/155-alembic.md` |

## Purpose

logs experiments, recipes and results as a lab notebook in the Hoard (`files.read`, `files.write`, `search.query`) with exact integer calculations.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | reagent stock |
| FORTRAN | trebago | reaction math |
| Ada | eve | hazard limits |
| APL | kron | result tables |
| MUMPS | myra | experiment tree |
| AWK | - | text |
| ALGOL 68 | - | formulas |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id alembic
matrix 155        # the graph's slot IS the matrix layer
name Alembic
needs fs.read
needs fs.write
needs search
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

