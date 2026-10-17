# Criteria — patchwork

| | |
|---|---|
| app | `patchwork` |
| matrix layer | 388 |
| icon group | L3 |
| wave spec | `wave32/388-patchwork.md` |

## Purpose

Gathers a family's history: stories, photos, records and voices in the Hoard (`files.read`, `files.write`, `search.query`), versions (`backup.versions`), the family space (`spaces.get`, `spaces.set_members`), published to the family by CID (`aipi.put`, `web4.cid`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `search`
- `backup`
- `peers`
- `net`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | archive storage costs |
| FORTRAN | trebago | date estimation |
| Ada | eve | privacy limits |
| APL | kron | timeline grid |
| MUMPS | myra | family tree |
| Julia | - | matrices and images |
| Gleam | - | messaging |
| Python | python | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id patchwork
matrix 388        # the graph's slot IS the matrix layer
name Patchwork
needs fs.read
needs fs.write
needs search
needs backup
needs peers
needs net
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

