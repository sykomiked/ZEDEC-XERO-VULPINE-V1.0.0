# Criteria — rapids

| | |
|---|---|
| app | `rapids` |
| matrix layer | 228 |
| icon group | L2 |
| wave spec | `wave18/228-rapids.md` |

## Purpose

Supports emergency crews in one place: triage and vitals records in the Hoard (`files.read`, `files.write`), the patient's medical ID through Watchfire's records, location (`place.here`, `place.ask`), calls (`call.start`, `call.status`, `call.hangup`, signalling only), crew devices (`phone.list`). Vital-sign devices have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `location`
- `peers`
- `mic`
- `fs.read`
- `fs.write`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | incident billing |
| FORTRAN | trebago | vitals trends |
| Ada | eve | dosage limits |
| APL | kron | triage grid |
| MUMPS | myra | patient record tree |
| Elixir | - | messaging |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id rapids
matrix 228        # the graph's slot IS the matrix layer
name Rapids
needs location
needs peers
needs mic
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

