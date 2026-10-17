# Criteria — ostinato

| | |
|---|---|
| app | `ostinato` |
| matrix layer | 371 |
| icon group | L3 |
| wave spec | `wave31/371-ostinato.md` |

## Purpose

Runs a circular economy: goods through reuse, repair and recycling (`net.offers`, `ledger.entries`), deposits held and returned (`ledger.hold`, `ledger.holds`, `ledger.release`, `ledger.post`), records and photos in the Hoard (`files.read`, `files.write`), parcels with Caravan's records.

## Capabilities (needs — the exact registry names)

- `net`
- `peers`
- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `camera`
- `location`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | deposit refunds |
| FORTRAN | trebago | material flow model |
| Ada | eve | hazard limits |
| APL | kron | material grid |
| MUMPS | myra | product lineage tree |
| Elixir | - | fault handling |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id ostinato
matrix 371        # the graph's slot IS the matrix layer
name Ostinato
needs net
needs peers
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs camera
needs location
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

