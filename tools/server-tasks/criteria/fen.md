# Criteria — fen

| | |
|---|---|
| app | `fen` |
| matrix layer | 230 |
| icon group | L2 |
| wave spec | `wave19/230-fen.md` |

## Purpose

Identifies plants from photos, logs specimens and builds a local flora dataset: photos and the register in the Hoard (`files.read`, `files.write`, `search.query`), the place only after `place.ask` (`place.here`), an identification suggestion from `swarm.ask` (held), the dataset published locally (`aipi.put`, `web4.cid`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `location`
- `camera`
- `ai.ask`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | specimen register |
| FORTRAN | trebago | growth measures |
| Ada | eve | protected-species limits |
| APL | kron | trait matrix |
| MUMPS | myra | taxonomy tree |
| Natural | - | queries |
| MATLAB | - | matrices and images |
| Bourne shell | - | jobs |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fen
matrix 230        # the graph's slot IS the matrix layer
name Fen
needs fs.read
needs fs.write
needs search
needs location
needs camera
needs ai.ask
needs net
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

