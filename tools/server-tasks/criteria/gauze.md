# Criteria — gauze

| | |
|---|---|
| app | `gauze` |
| matrix layer | 353 |
| icon group | L3 |
| wave spec | `wave29/353-gauze.md` |

## Purpose

Runs a clinic or field medic post: triage, charts, medicines given and handover notes in the Hoard (`files.read`, `files.write`), the patient's medical ID, location (`place.here`), calls on a person's request (`call.start`). Vital-sign devices have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `fs.read`
- `fs.write`
- `location`
- `peers`
- `mic`
- `sys.read`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | billing |
| FORTRAN | trebago | dosage curves |
| Ada | eve | hard dose limits |
| APL | kron | vitals arrays |
| MUMPS | myra | patient record tree |
| AWK | - | text |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id gauze
matrix 353        # the graph's slot IS the matrix layer
name Gauze
needs fs.read
needs fs.write
needs location
needs peers
needs mic
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

