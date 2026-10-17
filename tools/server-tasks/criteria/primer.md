# Criteria — primer

| | |
|---|---|
| app | `primer` |
| matrix layer | 100 |
| icon group | L1 |
| wave spec | `wave9/100-primer.md` |

## Purpose

learn Sutra, Trebago, Eve, KRON and MYRA by doing (`play.catalogue`, `play.run` on scratch state, `play.draft` for hints).

## Capabilities (needs — the exact registry names)

- `ai.ask`
- `fs.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | money and ledgers |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| APL | kron | arrays |
| MUMPS | myra | records |
| Python | python | AI |
| BASIC | basic | step-by-step teaching |
| OCaml | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id primer
matrix 100        # the graph's slot IS the matrix layer
name Primer
needs ai.ask
needs fs.read
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

