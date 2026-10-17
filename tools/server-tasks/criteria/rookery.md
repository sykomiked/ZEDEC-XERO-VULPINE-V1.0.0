# Criteria — rookery

| | |
|---|---|
| app | `rookery` |
| matrix layer | 26 |
| icon group | L1 |
| wave spec | `wave5/026-rookery.md` |

## Purpose

letters as signed envelopes to another companion (`web4.post`; held until `web4.consent` when pii is set), read from `web4.feed` and checked with `web4.verify`; drafts and folders in the Hoard (`files.write`, `files.list`). zxvd has no mail transport and no Web4 delivery yet: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| Go | go | messaging |
| Delphi | - | view and model |
| Elixir | - | fault handling |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id rookery
matrix 26        # the graph's slot IS the matrix layer
name Rookery
needs net
needs fs.read
needs fs.write
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

