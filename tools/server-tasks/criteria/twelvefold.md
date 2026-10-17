# Criteria — twelvefold

| | |
|---|---|
| app | `twelvefold` |
| matrix layer | 132 |
| icon group | L1 |
| wave spec | `wave3/132-twelvefold.md` |

## Purpose

write formulas over TRUE, FALSE, BOTH, NEITHER, see their tables and the 64-state hexagram tag; try the engines' Playground templates with `play.run`; save formulas in the Hoard (`files.read`, `files.write`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| APL | kron | arrays |
| Lua | - | scripting |
| Clojure | - | symbolic reasoning |
| Raku | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id twelvefold
matrix 132        # the graph's slot IS the matrix layer
name Twelvefold
needs fs.read
needs fs.write
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

