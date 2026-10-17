# Criteria — swage

| | |
|---|---|
| app | `swage` |
| matrix layer | 146 |
| icon group | L2 |
| wave spec | `wave12/146-swage.md` |

## Purpose

shapes source in the Hoard (`files.list`, `files.read`, `files.write`) into a signed, tested, installable package in one pass, reading the machine's facts from `sys.info`. zxvd has no method for build/test run, package sign and install (the same gaps Satchel and Whetstone report): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | release ledger |
| FORTRAN | trebago | build timing |
| Ada | eve | declared resource limits |
| APL | kron | dependency matrix |
| MUMPS | myra | package tree |
| BCPL | - | low-level system |
| ML | - | proofs of correctness |
| ALGOL 60 | algol60 | proofs of correctness |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id swage
matrix 146        # the graph's slot IS the matrix layer
name Swage
needs fs.read
needs fs.write
needs sys.read
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

