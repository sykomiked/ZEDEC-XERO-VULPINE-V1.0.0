# Criteria — cistern

| | |
|---|---|
| app | `cistern` |
| matrix layer | 296 |
| icon group | L3 |
| wave spec | `wave25/296-cistern.md` |

## Purpose

runs a water utility: pipes, pumps, leaks, meter readings, quality tests and customer bills (`files.read`, `files.write`, `journal.query`, `ledger.entries`, `ledger.post`, `print.submit`). zxvd has no method for meter, pump and quality sensor readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `journal`
- `ledger.read`
- `ledger.post`
- `print`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | customer billing |
| FORTRAN | trebago | hydraulic model |
| Ada | eve | pressure and dosing limits |
| APL | kron | meter arrays |
| MUMPS | myra | network asset tree |
| JOVIAL | - | machine control |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id cistern
matrix 296        # the graph's slot IS the matrix layer
name Cistern
needs fs.read
needs fs.write
needs journal
needs ledger.read
needs ledger.post
needs print
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

