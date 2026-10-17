# Criteria — parley

| | |
|---|---|
| app | `parley` |
| matrix layer | 68 |
| icon group | L1 |
| wave spec | `wave4/068-parley.md` |

## Purpose

which language (`xlate.detect`, `xlate.languages`), translate on this device (`xlate.translate`). Speech recognition and speech output have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `mic`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| Mojo | - | AI |
| Go | go | concurrency |
| OCaml | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id parley
matrix 68        # the graph's slot IS the matrix layer
name Parley
needs mic
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

