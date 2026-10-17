# Criteria — athenaeum

| | |
|---|---|
| app | `athenaeum` |
| matrix layer | 339 |
| icon group | L3 |
| wave spec | `wave28/339-athenaeum.md` |

## Purpose

a community or national library: catalogue, lending, digital collections and reading rooms (`search.query`, `files.read`, `files.write`, `print.submit`).

## Capabilities (needs — the exact registry names)

- `search`
- `fs.read`
- `fs.write`
- `print`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | lending fees and fines |
| FORTRAN | trebago | demand forecasts |
| Ada | eve | loan limits |
| APL | kron | catalogue arrays |
| MUMPS | myra | collection hierarchy |
| Java | java | domain objects |
| Python | python | AI |
| BASIC | basic | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id athenaeum
matrix 339        # the graph's slot IS the matrix layer
name Athenaeum
needs search
needs fs.read
needs fs.write
needs print
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

