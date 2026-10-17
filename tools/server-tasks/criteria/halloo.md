# Criteria — halloo

| | |
|---|---|
| app | `halloo` |
| matrix layer | 28 |
| icon group | L1 |
| wave spec | `wave3/028-halloo.md` |

## Purpose

place, answer and end calls (`call.start`, `call.accept`, `call.hangup`, `call.status`, `call.signal`). Media (capture, codecs, RTP) is not wired (H5): use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `peers`
- `mic`
- `camera`
- `notify`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| Ada | eve | limits |
| MUMPS | myra | records |
| Gleam | - | messaging |
| Python | python | AI |
| LabVIEW | - | signals |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id halloo
matrix 28        # the graph's slot IS the matrix layer
name Halloo
needs peers
needs mic
needs camera
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

