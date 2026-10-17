# Criteria — pinion

| | |
|---|---|
| app | `pinion` |
| matrix layer | 317 |
| icon group | L3 |
| wave spec | `wave26/317-pinion.md` |

## Purpose

automates and maintains factory equipment: machine states, predictive maintenance, spare parts and downtime (`sys.info`, `files.read`, `files.write`). zxvd has no method for machine sensor readings and control: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `sys.read`
- `notify`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| COBOL | sutra | maintenance costs |
| FORTRAN | trebago | wear models |
| Ada | eve | interlock limits |
| APL | kron | sensor arrays |
| MUMPS | myra | machine hierarchy |
| Bourne shell | - | jobs |
| REXX | - | scripting |
| Mojo | - | AI |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id pinion
matrix 317        # the graph's slot IS the matrix layer
name Pinion
needs sys.read
needs notify
needs fs.read
needs fs.write
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

