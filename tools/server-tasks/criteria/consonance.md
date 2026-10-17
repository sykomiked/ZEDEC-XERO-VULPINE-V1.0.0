# Criteria — consonance

| | |
|---|---|
| app | `consonance` |
| matrix layer | 352 |
| icon group | L3 |
| wave spec | `wave29/352-consonance.md` |

## Purpose

Negotiates, drafts and signs agreements between groups or nations: drafts in the Hoard (`files.read`, `files.write`), revisions on the journal (`journal.query`, `journal.verify`), signatures checked (`web4.verify`), translation (`xlate.translate`, `xlate.detect`), the negotiating room (`pspace.create`, `pspace.post`).

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `peers`
- `net`
- `ai.ask`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | treaty obligations ledger |
| FORTRAN | trebago | quota modelling |
| Ada | eve | binding limits |
| APL | kron | clause matrix |
| MUMPS | myra | party hierarchy |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id consonance
matrix 352        # the graph's slot IS the matrix layer
name Consonance
needs fs.read
needs fs.write
needs journal
needs peers
needs net
needs ai.ask
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

