# Criteria — orrery

| | |
|---|---|
| app | `orrery` |
| matrix layer | 101 |
| icon group | L1 |
| wave spec | `wave9/101-orrery.md` |

## Purpose

the sky from where the machine is (`place.here`, `sys.info` for the clock). No sky data method: the block holds the integer tables.

## Capabilities (needs — the exact registry names)

- `location`
- `sys.read`

## Entries and languages (from the spec's Logic line)

(the spec names no Logic line; assign per the language matrix)

## The language matrix rows for this app

| language | runtime | feature |
|---|---|---|
| C | c | view and model |
| MUMPS | myra | records |
| APL | kron | arrays |
| Mojo | - | AI |
| Simula I | - | simulation |
| Pascal | pascal | step-by-step teaching |

## The manifest contract (the exact grammar the node parses)

```
zxapp 1
id orrery
matrix 101        # the graph's slot IS the matrix layer
name Orrery
needs location
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

