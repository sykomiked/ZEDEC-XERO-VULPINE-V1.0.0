# Criteria — ticker

| | |
|---|---|
| app | `ticker` |
| matrix layer | 135 |
| icon group | L1 |
| wave spec | `wave11/135-ticker.md` |

## Purpose

captions for speech in calls, media and the room, translated with `xlate.translate` (language from `xlate.detect`). zxvd has no speech-to-text method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `mic`
- `ai.ask`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | safety rules |
| MUMPS | myra | records |
| Python | python | AI |
| SNOBOL | - | text |
| Raku | - | parsing |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id ticker
matrix 135        # the graph's slot IS the matrix layer
name Ticker
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

