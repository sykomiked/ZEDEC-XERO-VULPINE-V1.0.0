# Criteria — counterpoint

| | |
|---|---|
| app | `counterpoint` |
| matrix layer | 378 |
| icon group | L3 |
| wave spec | `wave31/378-counterpoint.md` |

## Purpose

Shares money, plans and chores as a couple, with joint goals and each person's view kept: joint accounts (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.hold`), plans in the Hoard (`files.read`, `files.write`), the shared clock (`sys.info`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `ledger.post`
- `fs.read`
- `fs.write`
- `sys.read`
- `peers`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | joint account |
| FORTRAN | trebago | budget forecasting |
| Ada | eve | spending limits |
| APL | kron | chore grid |
| MUMPS | myra | household tree |
| Go | go | messaging |
| Simula I | - | simulation |
| Delphi | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id counterpoint
matrix 378        # the graph's slot IS the matrix layer
name Counterpoint
needs ledger.read
needs ledger.post
needs fs.read
needs fs.write
needs sys.read
needs peers
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

