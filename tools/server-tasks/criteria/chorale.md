# Criteria — chorale

| | |
|---|---|
| app | `chorale` |
| matrix layer | 407 |
| icon group | L3 |
| wave spec | `wave34/407-chorale.md` |

## Purpose

Runs a cooperative's or town's governance: proposals and votes (`spaces.propose`, `spaces.vote`, `spaces.get`), debates (`social.post`, `social.feed`, `pspace.post`), budgets in the books (`ledger.accounts`, `ledger.entries`), published decisions (`aipi.put`, `web4.cid`), the record (`journal.query`, `journal.verify`).

## Capabilities (needs — the exact registry names)

- `peers`
- `net`
- `ledger.read`
- `journal`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | public budget |
| FORTRAN | trebago | turnout modelling |
| Ada | eve | quorum limits |
| APL | kron | ballot arrays |
| MUMPS | myra | member tree |
| Prolog | - | rules |
| TeX | - | typesetting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id chorale
matrix 407        # the graph's slot IS the matrix layer
name Chorale
needs peers
needs net
needs ledger.read
needs journal
needs fs.write
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

