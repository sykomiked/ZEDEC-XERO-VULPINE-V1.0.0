# Criteria — carapace

| | |
|---|---|
| app | `carapace` |
| matrix layer | 383 |
| icon group | L3 |
| wave spec | `wave32/383-carapace.md` |

## Purpose

Manages a fishing fleet or co-op: catch logs and quotas (`place.here`, `place.list`), landing payments in the books (`ledger.post`, `ledger.entries`, `ledger.hold`, `ledger.holds`, `ledger.release`), traceable labels printed (`print.submit`), market listings (`net.offers`).

## Capabilities (needs — the exact registry names)

- `location`
- `ledger.read`
- `ledger.post`
- `print`
- `net`
- `peers`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | landing payments |
| FORTRAN | trebago | fish stock models |
| Ada | eve | quota limits |
| APL | kron | catch grid |
| MUMPS | myra | vessel tree |
| Occam | - | concurrency |
| PostScript | - | page layout |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id carapace
matrix 383        # the graph's slot IS the matrix layer
name Carapace
needs location
needs ledger.read
needs ledger.post
needs print
needs net
needs peers
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

