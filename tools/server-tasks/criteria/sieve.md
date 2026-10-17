# Criteria — sieve

| | |
|---|---|
| app | `sieve` |
| matrix layer | 63 |
| icon group | L1 |
| wave spec | `wave3/063-sieve.md` |

## Purpose

every held suggestion (M3) in one place: tagged answers (`swarm.ask`), untrusted drafts (`play.draft`), held envelopes (`web4.feed`, `web4.consent`), and what was accepted (`journal.query`).

## Capabilities (needs — the exact registry names)

- `ai.ask`
- `journal`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| Python | python | AI |
| Occam | - | concurrency |
| Common Lisp | - | symbolic reasoning |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id sieve
matrix 63        # the graph's slot IS the matrix layer
name Sieve
needs ai.ask
needs journal
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

