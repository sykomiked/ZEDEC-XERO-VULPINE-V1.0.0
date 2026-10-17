# Criteria — granary

| | |
|---|---|
| app | `granary` |
| matrix layer | 64 |
| icon group | L1 |
| wave spec | `wave7/064-granary.md` |

## Purpose

the model slot (`model.info`), room on disk (`sys.disk`), energy (`power.status`). Downloading a model and reading its licence have no method: use only existing methods; list what's missing in your report.

## Capabilities (needs — the exact registry names)

- `net`
- `sys.read`
- `fs.read`
- `fs.write`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| APL | kron | arrays |
| Ada | eve | limits |
| Mojo | - | AI |
| Occam | - | concurrency |
| Simula I | - | simulation |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id granary
matrix 64        # the graph's slot IS the matrix layer
name Granary
needs net
needs sys.read
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

