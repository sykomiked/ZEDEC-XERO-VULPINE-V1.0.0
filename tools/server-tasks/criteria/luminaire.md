# Criteria — luminaire

| | |
|---|---|
| app | `luminaire` |
| matrix layer | 308 |
| icon group | L3 |
| wave spec | `wave26/308-luminaire.md` |

## Purpose

controls lighting across a home or building, saving energy with schedules, presence and daylight (`power.status`, `power.budget`, `sys.info`, `files.read`, `files.write`). zxvd has no method for light and presence device control: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `fs.read`
- `fs.write`
- `notify`
- `location`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | energy bills |
| FORTRAN | trebago | daylight model |
| Ada | eve | circuit load limits |
| APL | kron | room light grid |
| MUMPS | myra | building zone tree |
| Bash | - | jobs |
| IEC 61131-3 Structured Text | - | machine control |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id luminaire
matrix 308        # the graph's slot IS the matrix layer
name Luminaire
needs sys.read
needs fs.read
needs fs.write
needs notify
needs location
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

