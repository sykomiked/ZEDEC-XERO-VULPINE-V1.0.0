# Criteria — heliostat

| | |
|---|---|
| app | `heliostat` |
| matrix layer | 402 |
| icon group | L3 |
| wave spec | `wave33/402-heliostat.md` |

## Purpose

Plans, monitors and shares solar power across homes, settling surplus energy credits between neighbours: power (`power.status`, `power.top`, `power.budget`), the books (`ledger.post`, `ledger.entries`, `ledger.hold`, `ledger.holds`, `ledger.release`), votes on shared rules (`spaces.propose`, `spaces.vote`). Inverter and meter readings have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `ledger.read`
- `ledger.post`
- `peers`
- `net`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | energy credits |
| FORTRAN | trebago | irradiance model |
| Ada | eve | inverter limits |
| APL | kron | panel output grid |
| MUMPS | myra | household tree |
| Erlang | - | messaging |
| Bash | - | jobs |
| Tcl | - | scripting |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id heliostat
matrix 402        # the graph's slot IS the matrix layer
name Heliostat
needs sys.read
needs ledger.read
needs ledger.post
needs peers
needs net
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

