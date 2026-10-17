# Criteria — scriptorium

| | |
|---|---|
| app | `scriptorium` |
| matrix layer | 130 |
| icon group | L1 |
| wave spec | `wave10/130-scriptorium.md` |

## Purpose

the developer docs and the daemon's method reference, searchable: `search.query`, `search.refresh`, `files.read`, `rpc.discover` (method list), examples runnable with `play.run`.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `search`

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
| Icon | - | text |
| OCaml | - | parsing |
| Bash | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id scriptorium
matrix 130        # the graph's slot IS the matrix layer
name Scriptorium
needs fs.read
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

