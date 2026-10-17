# Criteria — ratchet

| | |
|---|---|
| app | `ratchet` |
| matrix layer | 152 |
| icon group | L2 |
| wave spec | `wave12/152-ratchet.md` |

## Purpose

diagnoses hardware faults from the machine's readings (`sys.info`, `sys.disk`, `power.status`), schedules maintenance and walks a person through a repair step by step. zxvd has no method for readings from machines other than this one: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | parts and labour costs |
| FORTRAN | trebago | wear model |
| Ada | eve | safe operating limits |
| APL | kron | sensor grid |
| MUMPS | myra | machine parts tree |
| Erlang | - | fault handling |
| Bourne shell | - | jobs |
| BASIC | basic | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id ratchet
matrix 152        # the graph's slot IS the matrix layer
name Ratchet
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

