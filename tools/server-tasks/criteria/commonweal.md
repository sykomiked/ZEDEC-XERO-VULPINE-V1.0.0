# Criteria — commonweal

| | |
|---|---|
| app | `commonweal` |
| matrix layer | 387 |
| icon group | L3 |
| wave spec | `wave32/387-commonweal.md` |

## Purpose

Your part in the mutual-aid economy in one honest view, read only: bounties earned (`swarm.earnings`, `peer.status`, `mesh.books`), patronage grants (`grants.status`, `grants.account`), Vino routes (`swap.status`, `swap.quote`), the reserve and the dividend (`vault.status`, `vault.check`, `swap.check`), your books (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.check`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `swarm`
- `peers`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | household ledger |
| FORTRAN | trebago | share arithmetic |
| Ada | eve | advisory limits |
| APL | kron | epoch tables |
| MUMPS | myra | holdings tree |
| Erlang | - | messaging |
| Prolog | - | search |
| Logo | logo | geometry |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id commonweal
matrix 387        # the graph's slot IS the matrix layer
name Commonweal
needs ledger.read
needs swarm
needs peers
needs sys.read
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

