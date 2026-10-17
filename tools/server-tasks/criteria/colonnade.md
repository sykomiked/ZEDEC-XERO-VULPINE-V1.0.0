# Criteria — colonnade

| | |
|---|---|
| app | `colonnade` |
| matrix layer | 127 |
| icon group | L1 |
| wave spec | `wave3/127-colonnade.md` |

## Purpose

write, compile and run code in the pillars (Sutra, Trebago, Eve, KRON, MYRA) and the Lingua languages: `play.catalogue`, `play.run` (scratch state, never the books), `play.draft` (untrusted draft, shown, never run by itself).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `ai.ask`

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
| OCaml | - | parsing |
| Bourne shell | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id colonnade
matrix 127        # the graph's slot IS the matrix layer
name Colonnade
needs fs.read
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

