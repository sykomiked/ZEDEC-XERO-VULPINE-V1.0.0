# Criteria — seachest

| | |
|---|---|
| app | `seachest` |
| matrix layer | 403 |
| icon group | L3 |
| wave spec | `wave33/403-seachest.md` |

## Purpose

Keeps wills, deeds, keys and valuables in sealed custody with timed releases and guardian approvals: the estate (`estate.status`, `estate.manifest`), guardians (`recovery.status`, `recovery.approve`, `recovery.veto`, `recovery.claim`), keys (`passkeys.list`), records in the Hoard (`files.read`, `files.write`), the record (`journal.query`, `journal.verify`).

## Capabilities (needs — the exact registry names)

- `peers`
- `journal`
- `fs.read`
- `fs.write`
- `sys.read`
- `ledger.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | custody ledger |
| FORTRAN | trebago | release scheduling |
| Ada | eve | access limits |
| APL | kron | item grid |
| MUMPS | myra | beneficiary tree |
| Eiffel | - | contracts |
| Solidity | - | contracts |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id seachest
matrix 403        # the graph's slot IS the matrix layer
name Seachest
needs peers
needs journal
needs fs.read
needs fs.write
needs sys.read
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

