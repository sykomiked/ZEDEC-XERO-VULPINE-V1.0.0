# Criteria — bindery

| | |
|---|---|
| app | `bindery` |
| matrix layer | 168 |
| icon group | L2 |
| wave spec | `wave13/168-bindery.md` |

## Purpose

catalogues a home, school or town library in the Hoard (`search.query`, `files.read`, `files.write`) and handles lending and returns, with slips printed by `print.submit`.

## Capabilities (needs — the exact registry names)

- `search`
- `fs.read`
- `fs.write`
- `print`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | loans ledger |
| FORTRAN | trebago | demand forecasting |
| Ada | eve | loan limits |
| APL | kron | catalogue grid |
| MUMPS | myra | shelf hierarchy |
| Python | python | AI |
| Pascal | pascal | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id bindery
matrix 168        # the graph's slot IS the matrix layer
name Bindery
needs search
needs fs.read
needs fs.write
needs print
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

