# Criteria — conch

| | |
|---|---|
| app | `conch` |
| matrix layer | 141 |
| icon group | L1 |
| wave spec | `wave11/141-conch.md` |

## Purpose

pair hearing devices, balance and mono, show sound alerts as a screen flash; alerts from `notes.list`. zxvd has no audio-device method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `notify`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | safety rules |
| MUMPS | myra | records |
| Gleam | - | messaging |
| Raku | - | parsing |
| LabVIEW | - | signals |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id conch
matrix 141        # the graph's slot IS the matrix layer
name Conch
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

