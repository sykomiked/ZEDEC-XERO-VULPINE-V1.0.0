# Criteria — ramble

| | |
|---|---|
| app | `ramble` |
| matrix layer | 25 |
| icon group | L1 |
| wave spec | `wave2/025-ramble.md` |

## Purpose

the owner's envelope feed (`web4.open`, `web4.feed`, `web4.envelope`, `web4.verify`), content by CID (`aipi.get`, `web4.cid`), agent cards (`web4.card_verify`) and Chiglet intents held for consent (`web4.consent`). Pages over HTTP have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| Dart | dart | view and model |
| Gleam | - | messaging |
| Occam | - | concurrency |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id ramble
matrix 25        # the graph's slot IS the matrix layer
name Ramble
needs net
needs ledger.read
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

