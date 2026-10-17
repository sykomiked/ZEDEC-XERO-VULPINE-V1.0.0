# Criteria — penumbra

| | |
|---|---|
| app | `penumbra` |
| matrix layer | 284 |
| icon group | L3 |
| wave spec | `wave24/284-penumbra.md` |

## Purpose

hunts quietly for hidden threats across a network the owner declares in scope, replaying traces (`log.tail`, `journal.query`, `sys.net`, `net.peers`) and testing defences before attackers do (`play.run` on scratch state). zxvd has no method for packet capture and trace replay: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `journal`
- `sys.read`
- `net`
- `peers`
- `ai.ask`
- `fs.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | findings ledger |
| FORTRAN | trebago | attack path scoring |
| Ada | eve | test scope limits |
| APL | kron | traffic matrices |
| MUMPS | myra | asset tree |
| Elixir | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id penumbra
matrix 284        # the graph's slot IS the matrix layer
name Penumbra
needs journal
needs sys.read
needs net
needs peers
needs ai.ask
needs fs.read
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

