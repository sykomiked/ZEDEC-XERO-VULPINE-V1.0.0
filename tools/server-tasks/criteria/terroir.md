# Criteria — terroir

| | |
|---|---|
| app | `terroir` |
| matrix layer | 231 |
| icon group | L2 |
| wave spec | `wave19/231-terroir.md` |

## Purpose

Manages a vineyard: blocks and lots, harvest timing, sales in the books (`ledger.open_account`, `ledger.post`, `ledger.entries`, `ledger.balance`), the place (`place.here`), market offers (`net.offers`, `net.command` sell). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `location`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | lot and excise records |
| FORTRAN | trebago | ripening model |
| Ada | eve | spray limits |
| APL | kron | block grid |
| MUMPS | myra | vine and lot tree |
| FLOW-MATIC | - | business records |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id terroir
matrix 231        # the graph's slot IS the matrix layer
name Terroir
needs ledger.read
needs ledger.post
needs location
needs peers
needs net
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

