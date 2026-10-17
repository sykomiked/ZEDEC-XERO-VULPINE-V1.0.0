# Criteria — cabochon

| | |
|---|---|
| app | `cabochon` |
| matrix layer | 278 |
| icon group | L3 |
| wave spec | `wave23/278-cabochon.md` |

## Purpose

gathers accounts, savings goals, bills and investments into one honest picture of your wealth (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.holds`).

## Capabilities (needs — the exact registry names)

- `ledger.read`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | household books |
| FORTRAN | trebago | compound growth model |
| Ada | eve | spending caps |
| APL | kron | portfolio grid |
| MUMPS | myra | family account tree |
| Objective-C | - | domain objects |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cabochon
matrix 278        # the graph's slot IS the matrix layer
name Cabochon
needs ledger.read
needs fs.read
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

