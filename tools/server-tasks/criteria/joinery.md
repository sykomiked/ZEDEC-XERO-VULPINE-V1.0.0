# Criteria — joinery

| | |
|---|---|
| app | `joinery` |
| matrix layer | 121 |
| icon group | L1 |
| wave spec | `wave3/121-joinery.md` |

## Purpose

edit code in the Hoard (`files.read`, `files.write` begin/add/end, `files.rename`), find in files (`search.query`, `search.refresh`), run a Playground template (`play.run`), suggestions from `swarm.ask` held until the person accepts them.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
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
| Racket | - | parsing |
| Haskell | - | typed data |
| Bourne shell | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id joinery
matrix 121        # the graph's slot IS the matrix layer
name Joinery
needs fs.read
needs fs.write
needs search
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

