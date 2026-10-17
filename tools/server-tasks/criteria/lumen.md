# Criteria — lumen

| | |
|---|---|
| app | `lumen` |
| matrix layer | 300 |
| icon group | L3 |
| wave spec | `wave25/300-lumen.md` |

## Purpose

a small clinic: appointments, patient records, consultations, referrals and billing in one private system (`files.read`, `files.write`, `notes.list`); bills are kept as Hoard files.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `notify`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | clinic billing |
| FORTRAN | trebago | dosing calculations |
| Ada | eve | clinical safety limits |
| APL | kron | lab result grid |
| MUMPS | myra | patient record tree |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id lumen
matrix 300        # the graph's slot IS the matrix layer
name Lumen
needs fs.read
needs fs.write
needs notify
needs sys.read
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

