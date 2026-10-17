# Criteria — quarry

| | |
|---|---|
| app | `quarry` |
| matrix layer | 69 |
| icon group | L1 |
| wave spec | `wave7/069-quarry.md` |

## Purpose

search the Hoard (`search.query`, `files.read`), the owner's Web4 feed (`web4.feed`), summarize with the companion (`swarm.ask`). Searching the web has no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `search`
- `fs.read`
- `ai.ask`
- `net`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| MUMPS | myra | index |
| dBase | - | queries |
| Python | python | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id quarry
matrix 69        # the graph's slot IS the matrix layer
name Quarry
needs search
needs fs.read
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

