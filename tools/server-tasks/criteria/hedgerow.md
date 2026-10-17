# Criteria — hedgerow

| | |
|---|---|
| app | `hedgerow` |
| matrix layer | 30 |
| icon group | L1 |
| wave spec | `wave2/030-hedgerow.md` |

## Purpose

who may reach this machine: network mode (`net.command` off/lan/online), answering peers (`peer.serve`), the phone link (`phone.link`, `phone.list`), switches that expose methods (`features.get`), hosted spaces (`pspace.list`) and the IRC bridge (`irc.status`). Packet filtering and per-port rules have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| Erlang | - | messaging |
| HyperTalk | - | view and model |
| Occam | - | concurrency |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id hedgerow
matrix 30        # the graph's slot IS the matrix layer
name Hedgerow
needs net
needs peers
needs sys.read
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

