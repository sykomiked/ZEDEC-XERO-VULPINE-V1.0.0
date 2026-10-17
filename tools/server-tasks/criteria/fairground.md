# Criteria — fairground

| | |
|---|---|
| app | `fairground` |
| matrix layer | 79 |
| icon group | L1 |
| wave spec | `wave1/079-fairground.md` |

## Purpose

what the owner shares, wants and sells, settled sales and fees (`net.offers`, `net.command` share/want/sell, `swarm.state` market, `mesh.books`, `peer.status`). Networking is off by default.

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | business records |
| FORTRAN | trebago | exact numerics |
| Ada | eve | limits |
| Eiffel | - | contracts |
| Solidity | - | contracts |
| SAS | - | reports |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id fairground
matrix 79        # the graph's slot IS the matrix layer
name Fairground
needs peers
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

