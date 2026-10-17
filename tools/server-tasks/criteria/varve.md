# Criteria — varve

| | |
|---|---|
| app | `varve` |
| matrix layer | 243 |
| icon group | L2 |
| wave spec | `wave20/243-varve.md` |

## Purpose

Builds history timelines from archives, sources and dated events: the Hoard (`files.list`, `files.read`, `files.write`, `search.query`), versions (`backup.versions`), the companion's reading only with Lore's leave (`swarm.ask`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `ai.ask`
- `backup`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | archive catalogue |
| FORTRAN | trebago | date estimation |
| Ada | eve | source rules |
| APL | kron | event matrix |
| MUMPS | myra | era tree |
| JCL | - | jobs |
| Python | python | AI |
| Icon | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id varve
matrix 243        # the graph's slot IS the matrix layer
name Varve
needs fs.read
needs fs.write
needs search
needs ai.ask
needs backup
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

