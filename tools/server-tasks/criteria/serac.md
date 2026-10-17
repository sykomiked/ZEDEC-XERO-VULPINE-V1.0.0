# Criteria — serac

| | |
|---|---|
| app | `serac` |
| matrix layer | 260 |
| icon group | L2 |
| wave spec | `wave21/260-serac.md` |

## Purpose

Fronts the reserve engines (kernel/src/svault, kernel/src/surplus, kernel/src/vswap, kernel/src/pay/pay_assure.h for the fee split) in plain numbers, read only: the vault and fee buckets (`vault.status`, `vault.check`), the V-Bill dividend pool (`swap.status`, `swap.check`, `swap.quote`), snapshots (`stats.snapshot`, `stats.status`), the books (`ledger.check`, `mesh.books`), auctions and grants (`auction.status`, `grants.status`), the trail (`journal.verify`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `sys.read`
- `journal`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | reserve ledger |
| FORTRAN | trebago | fee split arithmetic |
| Ada | eve | reserve-floor limits |
| APL | kron | epoch tables |
| MUMPS | myra | account records |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id serac
matrix 260        # the graph's slot IS the matrix layer
name Serac
needs ledger.read
needs sys.read
needs journal
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

