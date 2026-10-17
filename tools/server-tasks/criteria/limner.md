# Criteria — limner

| | |
|---|---|
| app | `limner` |
| matrix layer | 166 |
| icon group | L2 |
| wave spec | `wave13/166-limner.md` |

## Purpose

paints and edits images in the Hoard (`files.read`, `files.write`) with palettes and optional AI help (`swarm.ask`, `swarm.state`) held for approval.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `ai.ask`
- `swarm`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | asset licences |
| FORTRAN | trebago | colour math |
| Ada | eve | canvas limits |
| APL | kron | pixel arrays |
| MUMPS | myra | layer tree |
| MATLAB | - | matrices and images |
| Mojo | - | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id limner
matrix 166        # the graph's slot IS the matrix layer
name Limner
needs fs.read
needs fs.write
needs ai.ask
needs swarm
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

