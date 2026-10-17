# Criteria — poultice

| | |
|---|---|
| app | `poultice` |
| matrix layer | 148 |
| icon group | L2 |
| wave spec | `wave12/148-poultice.md` |

## Purpose

a care plan of symptoms, medicines and vitals kept in the Hoard (`files.read`, `files.write`), reminding carers through notices (`notes.list`, `notes.mark_read`) and keeping doses within the limits the plan declares. zxvd has no method for a notice-post method (as Sundial reports), vitals device readings: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | care costs |
| FORTRAN | trebago | vital trends |
| Ada | eve | dosage limits |
| APL | kron | symptom grid |
| MUMPS | myra | patient tree |
| Simula I | - | simulation |
| Modula-2 | - | structure |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id poultice
matrix 148        # the graph's slot IS the matrix layer
name Poultice
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

